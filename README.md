<div align="center">
    <h1>nitroefx</h1>
    <img src="data/nitroefx.png" width=256>
    <br></br>
    <p>Visual Editor for NITRO Particle Files. Primarily used in Nintendo DS Pokémon games.</p>
</div>

## Building
### Prerequisites
- [Rust toolchain](https://rustup.rs/) (for libimagequant)
- CMake (version 3.15 or higher)
- [vcpkg](https://github.com/microsoft/vcpkg) (for dependencies)
  - You also need to set the `VCPKG_ROOT` environment variable to the path of your vcpkg installation.

### Cloning the Repository
```bash
git clone --recurse-submodules https://github.com/Fexty12573/nitroefx.git
```
Or if you already have the repository, run:
```bash
git submodule update --init --recursive
```

Regardless of the platform you are on, you will need to install [vcpkg](https://github.com/microsoft/vcpkg) and set the `VCPKG_ROOT` environment variable to the path of your vcpkg installation.

### Windows
1. Install Visual Studio 2022 with the C++ development workload.
2. Install vcpkg and run `vcpkg integrate install` to integrate vcpkg with Visual Studio.
3. Open the `nitroefx` directory in Visual Studio.
4. Select one of the Windows configurations (x64-Debug/Release/Dist) and build the project.

Or via the command line: (Open a VS Developer Command Prompt)
```bash
cmake --preset windows-x64-<debug/release/dist>
cmake --build build
```

### Linux
On Ubuntu, the following packages are required (and probably some others):
```bash
sudo apt install libxmu-dev libxi-dev libgl-dev zip autoconf automake libtool pkg-config libglu1-mesa-dev libltdl-dev
```

Then, run the following commands:
```bash
cmake --preset linux-x64-<debug/release/dist>
cmake --build build
```
Alternatively, without presets:
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-linux -G "Unix Makefiles"
make
```
