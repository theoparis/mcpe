# MCPE 0.6.1 Alpha (but better)

## Changes

- Fixed multiplayer
- Added a direct connect option
- Ported to SDL3
- Added the CMake build system
- Added a userame field

## Developing With Nix

```bash
nix develop
cmake -B build -G Ninja -DBUILD_WITH_NIX=ON
cmake --build build
# Start the server
./build/mcpe_dedicated
# Start the client
./build/mcpe_sdl3
```
