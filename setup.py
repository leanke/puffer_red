# Debug command:
#    DEBUG=1 python setup.py build_ext --inplace --force
#    CUDA_VISIBLE_DEVICES=None LD_PRELOAD=$(gcc -print-file-name=libasan.so) python3.12 -m pufferlib.pufferl train

from setuptools import find_packages, find_namespace_packages, setup, Extension
import numpy
import os
import glob
import platform
import shutil

from setuptools.command.build_ext import build_ext
from torch.utils import cpp_extension
from torch.utils.cpp_extension import (
    CppExtension,
    CUDAExtension,
    BuildExtension,
    CUDA_HOME,
)

# Build with DEBUG=1 to enable debug symbols
DEBUG = os.getenv("DEBUG", "0") == "1"

# Shared compile args for all platforms
extra_compile_args = [
    '-DNPY_NO_DEPRECATED_API=NPY_1_7_API_VERSION',
]
extra_link_args = [
    '-fwrapv'
]
cxx_args = [
    '-fdiagnostics-color=always',
]
nvcc_args = []

if DEBUG:
    extra_compile_args += [
        '-O0',
        '-g',
        '-fsanitize=address,undefined,bounds,pointer-overflow,leak',
        '-fno-omit-frame-pointer',
    ]
    extra_link_args += [
        '-g',
        '-fsanitize=address,undefined,bounds,pointer-overflow,leak',
    ]
    cxx_args += [
        '-O0',
        '-g',
    ]
    nvcc_args += [
        '-O0',
        '-g',
    ]
else:
    extra_compile_args += [
        '-O2',
        '-flto',
    ]
    extra_link_args += [
        '-O2',
    ]
    cxx_args += [
        '-O3',
    ]
    nvcc_args += [
        '-O3',
    ]

system = platform.system()
if system == 'Linux':
    extra_compile_args += [
        '-Wno-alloc-size-larger-than',
        '-Wno-implicit-function-declaration',
        '-fmax-errors=3',
    ]
    extra_link_args += [
        '-Bsymbolic-functions',
    ]
elif system == 'Darwin':
    extra_compile_args += [
        '-Wno-error=int-conversion',
        '-Wno-error=incompatible-function-pointer-types',
        '-Wno-error=implicit-function-declaration',
    ]
    extra_link_args += [
        '-framework', 'Cocoa',
        '-framework', 'OpenGL',
        '-framework', 'IOKit',
    ]
else:
    raise ValueError(f'Unsupported system: {system}')


# Extensions 
class BuildExt(build_ext):
    def run(self):
        # Propagate any build_ext options (e.g., --inplace, --force) to subcommands
        build_ext_opts = self.distribution.command_options.get('build_ext', {})
        if build_ext_opts:
            # Copy flags so build_torch and build_c respect inplace/force
            self.distribution.command_options['build_torch'] = build_ext_opts.copy()
            self.distribution.command_options['build_c'] = build_ext_opts.copy()

        # Run the torch and C builds (which will handle copying when inplace is set)
        self.run_command('build_torch')
        self.run_command('build_c')

class CBuildExt(build_ext):
    def run(self, *args, **kwargs):
        self.extensions = [e for e in self.extensions if e.name != "pufferlib._C"]
        super().run(*args, **kwargs)

class TorchBuildExt(cpp_extension.BuildExtension):
    def run(self):
        self.extensions = [e for e in self.extensions if e.name == "pufferlib._C"]
        super().run()

INCLUDE = [numpy.get_include()]
extension_kwargs = dict(
    include_dirs=INCLUDE,
    extra_compile_args=extra_compile_args,
    extra_link_args=extra_link_args,
)

# Find C extensions for pokered
c_extensions = []

c_extension_paths = glob.glob('*/binding.c', recursive=True)
c_extensions = [
    Extension(
        path.rstrip('.c').replace('/', '.'),
        sources=[path],
        **extension_kwargs,
    )
    for path in c_extension_paths if 'matsci' not in path
]
c_extension_paths = [os.path.join(*path.split('/')[:-1]) for path in c_extension_paths]

for c_ext in c_extensions:
    if "pokered" in c_ext.name:
        print(f"Configuring {c_ext.name} with mGBA library")
        c_ext.extra_objects = []  # Remove raylib
        c_ext.include_dirs.append('/usr/include/mgba')
        c_ext.include_dirs.append('pokered/includes')
        c_ext.extra_compile_args.append('-DENABLE_VFS')
        # Link against mGBA library - use default -lmgba 
        # Note: System has version mismatch (headers=0.10, lib=0.11)
        # If this fails at runtime, fix by reinstalling libmgba0.10t64
        c_ext.extra_link_args.extend(['-lmgba'])




# Check if CUDA compiler is available. You need cuda dev, not just runtime.
torch_extensions = []
torch_sources = [
    "pufferlib/extensions/pufferlib.cpp",
]
if shutil.which("nvcc"):
    extension = CUDAExtension
    torch_sources.append("pufferlib/extensions/cuda/pufferlib.cu")
else:
    extension = CppExtension

torch_extensions = [
   extension(
        "pufferlib._C",
        torch_sources,
        extra_compile_args = {
            "cxx": cxx_args,
            "nvcc": nvcc_args,
        }
    ),
]

# Prevent Conda from injecting garbage compile flags
from distutils.sysconfig import get_config_vars
cfg_vars = get_config_vars()
for key in ('CC', 'CXX', 'LDSHARED'):
    if cfg_vars[key]:
        cfg_vars[key] = cfg_vars[key].replace('-B /root/anaconda3/compiler_compat', '')
        cfg_vars[key] = cfg_vars[key].replace('-pthread', '')
        cfg_vars[key] = cfg_vars[key].replace('-fno-strict-overflow', '')

for key, value in cfg_vars.items():
    if value and '-fno-strict-overflow' in str(value):
        cfg_vars[key] = value.replace('-fno-strict-overflow', '')

# Find all package directories
packages = ['pufferlib', 'pufferlib.extensions', 'pokered']

setup(
    name="pokemon_red_rl",
    version="0.1.0",
    description="Pokemon Red Reinforcement Learning with PufferLib",
    packages=packages,
    include_package_data=True,
    package_data={
        'pokered': ['states/*.state', 'states/*.ss1', 'include/*.h'],
    },
    ext_modules=c_extensions + torch_extensions,
    cmdclass={
        "build_ext": BuildExt,
        "build_torch": TorchBuildExt,
        "build_c": CBuildExt,
    },
    include_dirs=[numpy.get_include()],
    python_requires=">=3.9",
)
