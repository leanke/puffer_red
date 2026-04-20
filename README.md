# Pokémon Red Reinforcement Learning

This repository contains code and resources for reinforcement learning experiments using the Pokémon Red game environment. It includes environment files and a minimal PufferLib and Gambatte setup to facilitate easy experimentation and development.

## Installation

### Prerequisites
- Python 3.9+
- CUDA toolkit (optional, for GPU acceleration)
- Gambatte library (`libgambatte`) installed locally or system-wide

### Install Gambatte

```bash
make install-deps
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
│   └── extensions/         # C/CUDA extensions for fast GAE computation
│       ├── pufferlib.cpp   # CPU implementation
│       └── cuda/           # CUDA implementation
├── pokered/                # Gambatte Python bindings for Pokémon Red
│   ├── binding.c           # C Python extension
│   ├── pokered.py          # Python wrapper (PokemonRed env)
│   ├── torch.py            # PyTorch policy (CNN + LSTM)
│   ├── pokered.h           # C environment logic (rewards, state)
│   └── includes/           # Header files (battle, events, party)
├── gambatte/               # Gambatte emulator abstraction layer
│   ├── gambatte_wrapper.h  # Core init, state save/load, SDL rendering
│   ├── gambatte_c.h        # C API for libgambatte
│   ├── gambatte_c.cpp      # C++ wrapper implementation
│   ├── optim.h             # Performance macros
│   └── env_binding.h       # Generic Python ↔ C binding
├── config/                 # Training configurations
│   ├── default.ini         # Default hyperparameters
│   └── pokered.ini         # Pokemon Red overrides + sweep config
├── scripts/                # Utility scripts
└── test.py                 # Environment test suite
```

## Usage

### Training
```bash
```bash
# Train using the default configuration
python -m pufferlib.pufferl train pokered

# Train with Weights & Biases logging
python -m pufferlib.pufferl train pokered --wandb

# Train with custom parameters
python -m pufferlib.pufferl train pokered --train.total-timesteps 10000000
```

### Evaluation
```bash
python -m pufferlib.pufferl eval pokered --load-model-path experiments/your_model.pt
```

## Dependencies

The package includes a minimal set of PufferLib dependencies for training:

- **Core**: numpy, gymnasium, gym, pettingzoo
- **Training**: torch, psutil, pynvml, rich, wandb, neptune
- **Sweeps**: pyro-ppl, heavyball

The `_C` extension provides fast CUDA/CPU kernels for advantage computation (V-trace/GAE).
