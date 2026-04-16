import json
import os

import numpy as np
import torch
from torch import nn

import pufferlib
import pufferlib.models


class VizCapture:
    """Compact binary activation capture using compressed .npz chunks."""

    def __init__(self, capture_dir, mode="light", flush_every=1000):
        self.dir = capture_dir
        self.mode = mode
        self.flush_every = flush_every
        self._chunk_idx = 0
        self._count = 0
        self._buf = {}
        self._pending = None
        os.makedirs(self.dir, exist_ok=True)
        with open(os.path.join(self.dir, "capture_meta.json"), "w") as f:
            json.dump({"format": "npz", "version": 1, "mode": mode}, f)

    def begin(self, step, phase, screen, activations, embeddings=None):
        self._pending = {
            "step": step, "phase": phase, "screen": screen,
            "activations": activations, "embeddings": embeddings or {},
        }

    def end(self, hidden, action_logits, value):
        if self._pending is None:
            return
        p = self._pending
        self._pending = None

        self._add("steps", np.int32(p["step"]))
        self._add("screens", p["screen"])
        self._add("actions", action_logits.astype(np.float16))
        self._add("values", np.float32(value))

        for name in ["conv1", "conv2", "conv3"]:
            if name in p["activations"]:
                act = p["activations"][name]  # (C, H, W) float32
                self._add(f"{name}_spatial",
                          act.mean(axis=0).astype(np.float16))
                if self.mode == "full":
                    self._add(f"{name}_full", act.astype(np.float16))

        if self.mode == "full":
            self._add("hidden", hidden.astype(np.float16))
            for k in ["cnn_flat", "coord_emb", "menu_emb", "battle_emb", "map_emb", "heatmap_emb", "mode_emb", "ram_cat"]:
                if k in p["embeddings"]:
                    self._add(k, p["embeddings"][k].astype(np.float16))

        self._count += 1
        if self._count >= self.flush_every:
            self.flush()

    def _add(self, key, arr):
        self._buf.setdefault(key, []).append(arr)

    def flush(self):
        if self._count == 0:
            return
        data = {}
        for key, arrs in self._buf.items():
            data[key] = np.stack(arrs) if arrs[0].ndim > 0 else np.array(arrs)
        path = os.path.join(self.dir, f"chunk_{self._chunk_idx:04d}.npz")
        np.savez_compressed(path, **data)
        self._chunk_idx += 1
        self._buf.clear()
        self._count = 0

    def close(self):
        self.flush()


class PokemonRedLSTM(pufferlib.models.LSTMWrapper):
    def __init__(self, env, policy, input_size=128, hidden_size=128):
        super().__init__(env, policy, input_size, hidden_size)


class PokemonRed(nn.Module):
    def __init__(self, env, framestack=1, hidden_size=128,
                 capture_viz=False, capture_interval=100,
                 capture_dir="viz_data", capture_mode="light",
                 capture_flush=1000, **kwargs):
        super().__init__()
        self.hidden_size = hidden_size
        self.is_continuous = False
        
        cnn_out_size = 1920
        self.cnn = nn.Sequential(
            pufferlib.pytorch.layer_init(
                nn.Conv2d(framestack, 32, 8, stride=4)
                ),
            nn.ReLU(),
            pufferlib.pytorch.layer_init(
                nn.Conv2d(32, 64, 4, stride=2)
                ),
            nn.ReLU(),
            pufferlib.pytorch.layer_init(
                nn.Conv2d(64, 64, 3, stride=1)
                ),
            nn.ReLU(),
            nn.Flatten(),
        )
        
        coord_emb_size = 16
        menu_emb_size = 16
        battle_emb_size = 32
        map_emb_size = 16
        heatmap_emb_size = 32
        mode_emb_size = 8
        ram_out_size = (coord_emb_size + menu_emb_size + battle_emb_size +
                        map_emb_size + heatmap_emb_size + mode_emb_size + 5)
        self.final = nn.Sequential(
            pufferlib.pytorch.layer_init(
                nn.Linear(cnn_out_size + ram_out_size, 512)
                ),
            nn.GELU(),
            pufferlib.pytorch.layer_init(
                nn.Linear(512, hidden_size)
                ),
            nn.GELU(),
        )

        num_coord_features = 3
        self.coord_emb = nn.Sequential(
            pufferlib.pytorch.layer_init(
                nn.Linear(num_coord_features, coord_emb_size)
                ),
            nn.ReLU(),
        )

        num_menu_features = 6
        self.menu_emb = nn.Sequential(
            pufferlib.pytorch.layer_init(
                nn.Linear(num_menu_features, menu_emb_size)
                ),
            nn.ReLU(),
        )

        num_battle_features = 9
        self.battle_emb = nn.Sequential(
            pufferlib.pytorch.layer_init(
                nn.Linear(num_battle_features, battle_emb_size)
                ),
            nn.ReLU(),
        )

        num_map_features = 7
        self.map_emb = nn.Sequential(
            pufferlib.pytorch.layer_init(
                nn.Linear(num_map_features, map_emb_size)
                ),
            nn.ReLU(),
        )

        num_heatmap_tiles = 90
        self.heatmap_emb = nn.Sequential(
            pufferlib.pytorch.layer_init(
                nn.Linear(num_heatmap_tiles, heatmap_emb_size)
                ),
            nn.ReLU(),
        )

        num_mode_features = 2
        self.mode_emb = nn.Sequential(
            pufferlib.pytorch.layer_init(
                nn.Linear(num_mode_features, mode_emb_size)
                ),
            nn.ReLU(),
        )
        
        num_actions = env.single_action_space.n
        self.actor_general = pufferlib.pytorch.layer_init(
            nn.Linear(hidden_size, num_actions), std=0.01)
        self.actor_battle = pufferlib.pytorch.layer_init(
            nn.Linear(hidden_size, num_actions), std=0.01)
        self.value_fn = pufferlib.pytorch.layer_init(
            nn.Linear(hidden_size, 1), std=1)
        
        self._mode_idx = None

        self._viz_enabled = bool(capture_viz)
        self._viz_interval = int(capture_interval)
        self._viz_step = 0
        self._viz_activations = {}
        self._viz = None

        if self._viz_enabled:
            self._viz = VizCapture(capture_dir, mode=capture_mode,
                                   flush_every=capture_flush)
            self._register_viz_hooks()
            print(f"[viz] Capture enabled: mode={capture_mode}, "
                  f"interval={capture_interval}, dir={capture_dir}")

    def _register_viz_hooks(self):
        layer_names = {1: "conv1", 3: "conv2", 5: "conv3"}
        for idx, name in layer_names.items():
            self.cnn[idx].register_forward_hook(self._make_viz_hook(name))

    def _make_viz_hook(self, name):
        def hook(module, input, output):
            self._viz_activations[name] = output.detach()
        return hook

    def _maybe_capture_encode(self, screen, screen_net, coord_net, menu_net,
                              battle_net, map_net, heatmap_net, mode_net, ram_cat, ram_raw, hidden):
        if not self._viz_enabled:
            return
        self._viz_step += 1
        if self._viz_step % self._viz_interval != 0:
            self._viz_activations.clear()
            return

        screen_np = screen[0, 0].detach().cpu().to(torch.uint8).numpy()
        acts = {name: t[0].detach().cpu().numpy()
                for name, t in self._viz_activations.items()}
        self._viz_activations.clear()

        embeds = {
            "cnn_flat": screen_net[0].detach().cpu().numpy(),
            "coord_emb": coord_net[0].detach().cpu().numpy(),
            "menu_emb": menu_net[0].detach().cpu().numpy(),
            "battle_emb": battle_net[0].detach().cpu().numpy(),
            "map_emb": map_net[0].detach().cpu().numpy(),
            "heatmap_emb": heatmap_net[0].detach().cpu().numpy(),
            "mode_emb": mode_net[0].detach().cpu().numpy(),
            "ram_cat": ram_cat[0].detach().cpu().numpy(),
        }
        self._viz.begin(
            step=self._viz_step,
            phase="train" if self.training else "eval",
            screen=screen_np,
            activations=acts,
            embeddings=embeds,
        )

    def _maybe_capture_decode(self, hidden, action, value):
        if not self._viz_enabled or self._viz._pending is None:
            return
        self._viz.end(
            hidden=hidden[0].detach().cpu().numpy(),
            action_logits=action[0].detach().cpu().numpy(),
            value=value[0].item(),
        )

    def close_viz(self):
        if self._viz is not None:
            self._viz.close()
            self._viz = None

    def forward(self, observations, state=None):
        hidden = self.encode_observations(observations)
        actions, value = self.decode_actions(hidden)
        return actions, value

    def forward_train(self, x, state=None):
        return self.forward(x, state)

    def encode_observations(self, observations, state=None):
        batch = observations.shape[0]
        # screen
        screen_flat = observations[:, :72*80*1]
        screen = screen_flat.view(batch, 72, 80, 1).permute(0, 3, 1, 2).float()
        screen_norm = screen / 255.0
        screen_net = self.cnn(screen_norm)

        # ram features layout (122 values after pixels):
        # [0:3]   x, y, map_n
        # [3:8]   badges, party_count, in_battle, hp_fraction, facing
        # [8:14]  menu: item, max_item, scroll, textbox_id, menu_y, menu_x
        # [14:23] battle: dmg_mult, miss, p_status1/2/3, e_status1/2/3, opponent
        # [23:30] map: moving_dir, steps_to_take, num_sprites, repel, tileset, height, width
        # [30:32] game_mode: one-hot [general, battle]
        # [32:122] heatmap: 10×9 tile visit counts (row-major)
        ram_flat = observations[:, 72*80*1:]

        # resolve game mode first for conditional gradient gating
        mode_onehot = ram_flat[:, 30:32].float()
        mode_net = self.mode_emb(mode_onehot)
        self._mode_idx = mode_onehot.argmax(dim=1).long()
        is_general = mode_onehot[:, 0:1].bool()  # (batch, 1)
        is_battle = mode_onehot[:, 1:2].bool()

        coords = ram_flat[:, 0:3].float() / 255.0
        coord_net = self.coord_emb(coords)
        coord_net = torch.where(is_general, coord_net, coord_net.detach())

        badge = ram_flat[:, 3:4].float() / 8.0
        party_size = ram_flat[:, 4:5].float() / 6.0
        in_battle = ram_flat[:, 5:6].float()
        hp_fraction = ram_flat[:, 6:7].float()
        facing = ram_flat[:, 7:8].float() / 3.0
        menu_raw = ram_flat[:, 8:14].float() / 255.0
        menu_net = self.menu_emb(menu_raw)

        battle_raw = ram_flat[:, 14:23].float() / 255.0
        battle_net = self.battle_emb(battle_raw)
        battle_net = torch.where(is_battle, battle_net, battle_net.detach())

        map_raw = ram_flat[:, 23:30].float() / 255.0
        map_net = self.map_emb(map_raw)
        map_net = torch.where(is_general, map_net, map_net.detach())

        heatmap_raw = ram_flat[:, 32:122].float()
        heatmap_norm = torch.log1p(heatmap_raw) / 11.0
        heatmap_net = self.heatmap_emb(heatmap_norm)
        heatmap_net = torch.where(is_general, heatmap_net, heatmap_net.detach())

        ram_cat = torch.cat([coord_net, menu_net, battle_net, map_net,
                             heatmap_net, mode_net,
                             badge, party_size, in_battle, hp_fraction, facing], dim=1)
        
        # final cat
        final_cat = torch.cat([screen_net, ram_cat], dim=1)
        hidden = self.final(final_cat)

        self._maybe_capture_encode(
            screen, screen_net, coord_net, menu_net, battle_net, map_net,
            heatmap_net, mode_net, ram_cat, ram_flat, hidden)

        return hidden

    def decode_actions(self, hidden, state=None):
        logits_gen = self.actor_general(hidden)
        logits_bt = self.actor_battle(hidden)

        all_logits = torch.stack([logits_gen, logits_bt], dim=1)
        idx = self._mode_idx.view(-1, 1, 1).expand(-1, 1, all_logits.size(2))
        action = all_logits.gather(1, idx).squeeze(1)

        value = self.value_fn(hidden)
        self._maybe_capture_decode(hidden, action, value)
        return action, value
