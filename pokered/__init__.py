from pokered import torch
from pokered.pokered import (
    PokemonRed,
    STREAM_COLOR_PURPLE,
    STREAM_COLOR_BLUE,
    STREAM_COLOR_GREEN,
    STREAM_COLOR_RED,
    STREAM_COLOR_PINK,
    STREAM_COLOR_YELLOW,
)

__all__ = ["PokemonRed", "STREAM_COLOR_PURPLE", "STREAM_COLOR_BLUE", 
           "STREAM_COLOR_GREEN", "STREAM_COLOR_RED", "STREAM_COLOR_PINK", 
           "STREAM_COLOR_YELLOW", "env_creator", "torch"]

def env_creator(name='pokered', *args, **kwargs):
    return PokemonRed
