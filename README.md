
# Memory Machine — CMake + CI build

This repo is set up to build a **VST3** using CMake and GitHub Actions.

## Local build (once JUCE is installed via vcpkg/Homebrew)

1. Place `MemoryMachine.cpp` (from our chat canvas) next to `CMakeLists.txt`.
2. Configure + build:
   - Windows (VS 2022, vcpkg):
     ```powershell
     cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake"
     cmake --build build --config Release
     ```
   - macOS (Homebrew):
     ```bash
     cmake -B build -S . -DCMAKE_PREFIX_PATH="$(brew --prefix)/opt/juce"
     cmake --build build --config Release
     ```
3. The built `.vst3` will be under the build artefacts (e.g., `build/.../VST3/MemoryMachine.vst3`).

## CI build (GitHub Actions)

1. Push this folder to a new GitHub repo.
2. Add `MemoryMachine.cpp` at the repo root.
3. The included workflow will build on **Windows** and **macOS**, and upload `MemoryMachine.vst3` as artifacts.

Retrieve them from the "Actions" tab → latest workflow run → Artifacts.
