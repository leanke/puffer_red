#!/usr/bin/env python3
"""
Comprehensive test script for Pokemon Red environment.
Tests creation, stepping, reset, and benchmarking.

Usage:
    python pokered.py              # Run quick test (100 steps)
    python pokered.py 1000         # Run benchmark (1000 steps)
    python pokered.py --full       # Run full test suite
"""

import sys
import time
import argparse
import numpy as np
import warnings
warnings.filterwarnings("ignore", category=FutureWarning)

# Default environment configuration from pokered.ini [env] section
DEFAULT_ENV_CONFIG = {
    'num_envs': 8,
    'rom_path': './pokemon_red.gb',
    'state_path': 'pokered/states/nballs.ss1',
    'stream_enabled': False,
    'stream_interval': 400,
    'stream_user': ['leanke'],
    'stream_color': ['#800080'],
    'stream_extra': '',
    'max_episode_length': 20480,
    'full_reset': True,
}


def create_env(num_envs=None, **overrides):
    """Create Pokemon Red environment with default config + overrides."""
    from pokered import PokemonRed
    
    config = DEFAULT_ENV_CONFIG.copy()
    if num_envs is not None:
        config['num_envs'] = num_envs
    config.update(overrides)
    
    return PokemonRed(**config)


def test_create(num_envs=1):
    """Test basic environment creation."""
    print(f'[TEST] Creating env with {num_envs} envs...')
    env = create_env(num_envs=num_envs)
    print(f'[PASS] Created! Observation shape: {env.single_observation_space.shape}')
    print(f'       Action space: {env.single_action_space}')
    return env


def test_reset(env):
    """Test environment reset."""
    print('[TEST] Testing reset...')
    obs, info = env.reset()
    assert obs.shape[0] == env.num_agents, f"Obs shape mismatch: {obs.shape}"
    print(f'[PASS] Reset successful. Obs shape: {obs.shape}')
    return obs


def test_step(env, n_steps=100, verbose=True):
    """Test stepping the environment and measure performance."""
    if verbose:
        print(f'[TEST] Testing {n_steps} steps...')
    
    obs, _ = env.reset()
    total_reward = 0
    terminals = 0
    
    start = time.time()
    for i in range(n_steps):
        actions = np.random.randint(0, 9, size=env.num_agents)
        obs, rewards, terms, truncs, infos = env.step(actions)
        total_reward += rewards.sum()
        terminals += terms.sum()
        
        # Print info every log_interval if available
        if infos and verbose:
            for info in infos:
                if isinstance(info, dict):
                    print(f'  Step {i}: {info}')
    
    elapsed = time.time() - start
    fps = n_steps / elapsed
    steps_per_env = n_steps * env.num_agents
    env_fps = steps_per_env / elapsed
    
    if verbose:
        print(f'[PASS] Completed {n_steps} steps in {elapsed:.2f}s')
        print(f'       Steps/sec: {fps:.1f} (total env steps: {env_fps:.1f})')
        print(f'       Total reward: {total_reward:.2f}, Terminals: {terminals}')
    
    return fps, env_fps


def test_close(env):
    """Test closing the environment."""
    print('[TEST] Closing env...')
    env.close()
    print('[PASS] Closed!')


def test_observation_values(env):
    """Test that observations contain valid data."""
    print('[TEST] Checking observation values...')
    obs, _ = env.reset()
    
    # Take a few steps to get non-initial state
    for _ in range(10):
        actions = np.random.randint(0, 9, size=env.num_agents)
        obs, _, _, _, _ = env.step(actions)
    
    # Check observation bounds
    assert obs.min() >= 0, f"Obs min below 0: {obs.min()}"
    assert obs.max() <= 255, f"Obs max above 255: {obs.max()}"
    
    # Check RAM values are embedded (last 5 values per env)
    obs_size = env.single_observation_space.shape[0]
    ram_obs = obs[:, -5:]  # Last 5 values are RAM state
    
    print(f'[PASS] Observations valid. Range: [{obs.min():.0f}, {obs.max():.0f}]')
    print(f'       RAM state sample (x,y,map,badges,party): {ram_obs[0]}')


def test_multi_env_scaling(max_envs=24):
    """Test scaling with multiple environments."""
    print(f'scaling up to {max_envs} envs...\n')
    
    results = []
    vec_env_counts = [4, 8, 16, 24]
    
    for n in [1, 2, 4, 8, 12, 16, 20, 24]:
        if n > max_envs:
            break
        try:
            env = create_env(num_envs=n)
            _, env_fps = test_step(env, n_steps=100, verbose=False)
            env.close()
            results.append((n, env_fps))
        except Exception as e:
            print(f'[FAIL] {n} envs: {e}')
            break
    
    if not results:
        return results
    
    # Build transposed table: rows are metrics, columns are env counts
    row_labels = ['Envs', '1x Vec'] + [f'{c}x Vec' for c in vec_env_counts]
    
    # Build columns: first is labels, then one per env count tested
    columns = [row_labels]
    for n, env_fps in results:
        col = [str(n), f' {env_fps:,.0f}']
        for cores in vec_env_counts:
            estimated = round((env_fps * cores * 0.6)/1000, ndigits=1)
            col.append(f'{estimated:,}k')
        columns.append(col)
    
    # Calculate dynamic column widths
    col_widths = []
    for col in columns:
        col_widths.append(max(len(val) for val in col) + 2)
    
    # Helper to format a row
    def fmt_row(row_idx):
        cells = [columns[col_idx][row_idx] for col_idx in range(len(columns))]
        return '│' + '│'.join(f'{v:^{col_widths[i]}}' for i, v in enumerate(cells)) + '│'
    
    def separator(left, mid, right):
        return left + mid.join('─' * w for w in col_widths) + right
    
    # Print table
    num_rows = len(row_labels)
    print(separator('┌', '┬', '┐'))
    print(fmt_row(0))  # Envs header row
    print(separator('├', '┼', '┤'))
    print(fmt_row(1))  # serial row
    for row_idx in range(2, num_rows):  # Vec rows
        print(fmt_row(row_idx))
    print(separator('└', '┴', '┘'))
    
    return results


def run_benchmark(n_steps=1000, num_envs=8):
    """Run performance benchmark."""
    print(f'\n{"="*50}')
    print(f'BENCHMARK: {n_steps} steps, {num_envs} envs')
    print(f'{"="*50}\n')
    
    env = create_env(num_envs=num_envs)
    
    # Warmup
    print('Warmup (50 steps)...')
    test_step(env, n_steps=50, verbose=False)
    
    # Benchmark
    print(f'Benchmarking ({n_steps} steps)...')
    fps, env_fps = test_step(env, n_steps=n_steps, verbose=False)
    
    env.close()
    
    print(f'\n{"="*50}')
    print(f'RESULTS:')
    print(f'  Steps/sec:      {fps:.1f}')
    print(f'  Env-steps/sec:  {env_fps:.1f}')
    print(f'  Time per step:  {1000/fps:.2f}ms')
    print(f'{"="*50}\n')
    
    return fps, env_fps


def run_full_test_suite():
    """Run the full test suite."""
    print('\n' + '='*60)
    print('FULL TEST SUITE')
    print('='*60 + '\n')
    
    # Test 1: Single env creation
    env = test_create(num_envs=1)
    test_reset(env)
    test_observation_values(env)
    test_step(env, n_steps=50)
    test_close(env)
    print()
    
    # Test 2: Multi-env creation
    env = test_create(num_envs=8)
    test_reset(env)
    test_step(env, n_steps=100)
    test_close(env)
    print()
    
    # Test 3: Scaling test
    test_multi_env_scaling(max_envs=32)
    print()
    
    # Test 4: Benchmark
    run_benchmark(n_steps=500, num_envs=8)
    
    print('\n' + '='*60)
    print('ALL TESTS PASSED!')
    print('='*60 + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Pokemon Red Environment Test Suite')
    parser.add_argument('steps', nargs='?', type=int, default=100,
                        help='Number of steps for quick test/benchmark')
    parser.add_argument('--full', action='store_true',
                        help='Run full test suite')
    parser.add_argument('--scale', action='store_true',
                        help="Test for training speed")
    parser.add_argument('--envs', type=int, default=8,
                        help='Number of environments')
    parser.add_argument('--benchmark', '-b', action='store_true',
                        help='Run benchmark mode')
    args = parser.parse_args()
    
    if args.full:
        run_full_test_suite()
    elif args.benchmark:
        run_benchmark(n_steps=args.steps, num_envs=args.envs)
    elif args.scale:
        test_multi_env_scaling(max_envs=32)
        print()
    else:
        # Quick test
        env = test_create(num_envs=args.envs)
        fps, _ = test_step(env, args.steps)
        test_close(env)
        print(f'\n=== BENCHMARK: {fps:.1f} steps/s ===')









# ┌──────────┬──────────┬───────────┐
# │  Envs    │    1     │     8     │
# ├──────────┼──────────┼───────────┤
# │  SPS     │  5,000   │   35,000  │
# ├──────────┼──────────┼───────────┤
# │  2x Vec  │  6,000   │   42,000  │
# │  4x Vec  │  6,000   │   84,000  │
# │  8x Vec  │ 24,000   │  168,000  │
# │ 16x Vec  │ 48,000   │  336,000  │
# └──────────┴──────────┴───────────┘


