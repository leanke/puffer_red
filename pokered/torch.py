from types import SimpleNamespace
from typing import Any, Tuple

from gymnasium import spaces

from torch import nn
import torch
from torch.distributions.normal import Normal
from torch import nn
import torch.nn.functional as F

import pufferlib
import pufferlib.models

from pufferlib.models import Default as Policy
from pufferlib.models import Convolutional as Conv
Recurrent = pufferlib.models.LSTMWrapper
from pufferlib.pytorch import layer_init, _nativize_dtype, nativize_tensor
import numpy as np


class PokemonRedLSTM(pufferlib.models.LSTMWrapper):
    """LSTM wrapper for mGBA Pokemon Red policy."""
    def __init__(self, env, policy, input_size=128, hidden_size=128):
        super().__init__(env, policy, input_size, hidden_size)


class PokemonRed(nn.Module):
    """CNN policy for mGBA Pokemon Red environment.
    Observation: (144, 160, 3) screen pixels
    Action: 9 discrete actions
    """
    def __init__(self, env, framestack=1, hidden_size=128, **kwargs):
        super().__init__()
        self.hidden_size = hidden_size
        self.is_continuous = False
        
        cnn_out_size = 1920 # did i match pokegym?
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
        
        ram_out_size = 3
        self.final = nn.Sequential(
            pufferlib.pytorch.layer_init(
                nn.Linear(cnn_out_size + ram_out_size, hidden_size)
                ),
            nn.GELU(),
        )

        num_coord_features = 3
        self.coord_emb = nn.Sequential(
            pufferlib.pytorch.layer_init(
                nn.Linear(num_coord_features, 1)
                ),
            nn.ReLU(),
        )
        
        self.actor = pufferlib.pytorch.layer_init(
            nn.Linear(hidden_size, env.single_action_space.n), std=0.01)
        self.value_fn = pufferlib.pytorch.layer_init(
            nn.Linear(hidden_size, 1), std=1)

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

        # ram
        ram_flat = observations[:, 72*80*1:]
        coords = ram_flat[:, 0:3].float()
        coord_net = self.coord_emb(coords)
        badge = ram_flat[:, 3:4].float()
        badge = badge / 8.0
        party_size = ram_flat[:, 4:5].float()
        party_size = party_size / 6.0
        ram_cat = torch.cat([coord_net, badge, party_size], dim=1)
        
        # final cat
        # print('coord net: ', coord_net[0].detach().cpu().numpy().tolist())
        # print('badge: ', badge[0].detach().cpu().numpy().tolist())
        # print('party size: ', party_size[0].detach().cpu().numpy().tolist())
        # print('ram cat: ', ram_cat[0].detach().cpu().numpy().tolist())
        # print('screen net: ', screen_net[0].shape)
        final_cat = torch.cat([screen_net, ram_cat], dim=1)
        return self.final(final_cat)

    def decode_actions(self, hidden, state=None):
        action = self.actor(hidden)
        value = self.value_fn(hidden)
        return action, value
# Alias for compatibility
Policy = PokemonRed
