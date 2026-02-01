# Pokémon Red Reinforcement Learning

This repository contains code and resources for reinforcement learning experiments using the Pokémon Red game environment. It includes environment files and a minimal PufferLib and mGBA setup to facilitate easy experimentation and development.

## Installation

### Prerequisites
- Python 3.9+
- CUDA toolkit (optional, for GPU acceleration)
- mGBA library (`libmgba`) installed system-wide

### Install mGBA (Ubuntu/Debian)
```bash
sudo apt-get install libmgba-dev
```

### Install the package

```bash
# Clone and enter the directory
cd pokemon_red_rl

# Install in development mode (recommended)
pip install -e .

# Or build the extensions in place
python setup.py build_ext --inplace
```

## Project Structure

```
pokemon_red_rl/
├── pufferlib/              # Minimal PufferLib training framework
│   ├── __init__.py
│   ├── pufferl.py          # Main training loop
│   ├── models.py           # Neural network policies
│   ├── vector.py           # Environment vectorization
│   ├── pytorch.py          # PyTorch utilities
│   ├── sweep.py            # Hyperparameter sweeping
│   ├── config/             # Training configurations
│   └── extensions/         # C/CUDA extensions for fast GAE computation
│       ├── pufferlib.cpp   # CPU implementation
│       └── cuda/           # CUDA implementation
├── mgba/                   # mGBA Python bindings for Pokémon Red
│   ├── binding.c           # C Python extension
│   ├── mgba.py             # Python wrapper
│   └── include/            # Header files
├── config/                 # Environment configs
├── webserver/              # Training visualization server
└── Docs/                   # Documentation
```

## Usage

### Training
```bash
# Train using the default configuration
python -m pufferlib.pufferl train mgba

# Train with Weights & Biases logging
python -m pufferlib.pufferl train mgba --wandb

# Train with custom parameters
python -m pufferlib.pufferl train mgba --train.total-timesteps 10000000
```

### Evaluation
```bash
python -m pufferlib.pufferl eval mgba --load-model-path experiments/your_model.pt
```

## Dependencies

The package includes a minimal set of PufferLib dependencies for training:

- **Core**: numpy, gymnasium, gym, pettingzoo
- **Training**: torch, psutil, pynvml, rich, wandb, neptune
- **Sweeps**: pyro-ppl, heavyball

The `_C` extension provides fast CUDA/CPU kernels for advantage computation (V-trace/GAE).
