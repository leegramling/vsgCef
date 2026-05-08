# vsgCef

This directory builds the `vsgCef` VSG app.

## Build

```bash
cmake -S . -B build -G Ninja
cmake --build build --target vsgCef
./build/vsgCef
```

CMake defaults to the copied VSG/vsgImGui install prefix at:

```text
external/vsg_deps/install
```

Override it if needed:

```bash
cmake -S . -B build -DVSG_DEPS_INSTALL_DIR=/path/to/vsg_deps/install
```

System dependencies such as the Vulkan loader/driver and a shader compiler from the Vulkan SDK are not vendored.
