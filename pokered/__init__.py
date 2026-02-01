import importlib
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

MAKE_FUNCTIONS = {
    # 'scape': 'Scape',
    'pokered': 'PokemonRed',
}
def env_creator(name='pokered', *args, **kwargs):
    try:
        module = importlib.import_module(f'{name}.{name}')
        return getattr(module, MAKE_FUNCTIONS[name])
    except ModuleNotFoundError:
        return MAKE_FUNCTIONS[name]
