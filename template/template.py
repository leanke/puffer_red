"""Template mGBA environment for PufferLib.

Copy this module and customize it for your game. See template/README.md
for a step-by-step guide.
"""
import numpy as np
from gymnasium import spaces
import pufferlib
from template import binding


class TemplateGame(pufferlib.PufferEnv):
    """Minimal PufferEnv wrapping an mGBA-emulated game."""

    def __init__(
        self,
        num_envs=1,
        render_mode=None,
        headless=False,
        rom_path=None,
        state_path=None,
        frameskip=4,
        max_episode_length=20480,
        full_reset=True,
        log_interval=128,
        buf=None,
        seed=0,
    ):
        self.rom_path = rom_path
        self.frame_skip = frameskip
        self.max_episode_length = max_episode_length
        self.headless = headless
        self.num_agents = num_envs
        self.log_interval = log_interval
        self.tick = 0

        # Adjust these to match SCALED_WIDTH/HEIGHT and EXTRA_OBS in template.h
        scaled_width = 80
        scaled_height = 72
        extra_obs = 0
        self.single_observation_space = spaces.Box(
            low=0,
            high=255,
            shape=(scaled_height * scaled_width + extra_obs,),
            dtype=np.float32,
        )
        # 9 discrete actions: noop + 4 d-pad + A/B/Start/Select
        self.single_action_space = spaces.Discrete(9)

        super().__init__(buf)

        self.c_envs = binding.vec_init(
            self.observations,
            self.actions,
            self.rewards,
            self.terminals,
            self.truncations,
            num_envs,
            seed,
            headless=headless,
            rom_path=rom_path,
            state_path=state_path,
            frameskip=frameskip,
            max_episode_length=max_episode_length,
            full_reset=full_reset,
        )

    def reset(self, seed=None):
        self.tick = 0
        binding.vec_reset(self.c_envs, seed or 0)
        return self.observations, []

    def step(self, actions):
        self.actions[:] = actions
        self.tick += 1
        binding.vec_step(self.c_envs)

        info = []
        if self.tick % self.log_interval == 0:
            info.append(binding.vec_log(self.c_envs))

        return (
            self.observations,
            self.rewards,
            self.terminals,
            self.truncations,
            info,
        )

    def render(self):
        binding.vec_render(self.c_envs, 0)

    def close(self):
        binding.vec_close(self.c_envs)
