# vrm2mdl

A Standalone C++ tool to convert VRoid Studio (.vrm) avatars into Source Engine (.mdl) compatible files for Source Filmmaker (SFM).

## Features
- **Bone Mapping:** Automatically renames VRM humanoid bones to `ValveBiped` convention.
- **Coordinate Conversion:** Converts Y-up (VRM) to Z-up (Source Engine) orientation.
- **Multi-Weight Support:** Supports up to 4 bone weights per vertex for smooth skinning.
- **Real Normals & UVs:** Reads actual normal vectors and UV coordinates from the VRM mesh for correct lighting and texturing.
- **Facial Flexes (VTA):** Automatically extracts VRM Expressions (Morph Targets) and converts them to `.vta` format.
- **Named Expressions:** Flex sliders in SFM will show meaningful names (e.g., `happy`, `blink`, `mouth_a`) instead of generic IDs.
- **Auto-Jigglebones:** Automatically converts VRM SpringBones to Source Engine `$jigglebone` commands for realistic hair and cloth physics.
- **Advanced Materials:** Generates `.vmt` files with support for Normal Maps and Self-Illumination.
- **Texture Extraction:** Automatically unpacks embedded PNG/JPG textures.
- **QC Scripting:** Generates a `.qc` file ready for `studiomdl.exe` compilation.
- **Auto Folder Structure:** Automatically creates Source Engine standard directory layout on export.
- **VRM 0.x & 1.0 Support:** Handles both VRM 0.x (`VRM` extension) and VRM 1.0 (`VRMC_vrm` extension) formats.
- **JOINTS_0 Compatibility:** Supports both `UNSIGNED_BYTE` and `UNSIGNED_SHORT` joint index formats.

## Build Requirements
- GCC (C++17 or later)
- GNU Make

## How to Build
```bash
make
```
or manually:
```bash
g++ -std=c++17 -O3 -I. -I./include main.cpp -o vrm2mdl -lpthread -lstdc++fs
```

## How to Use
```bash
./vrm2mdl <input_file.vrm> [--scale <value>] [--output-dir <path>]
```

### Options
| Option | Default | Description |
|--------|---------|-------------|
| `--scale` | `39.37` | Global scale factor (inches per meter) |
| `--output-dir` | `./output` | Root output directory |

### Batch Conversion
```bash
./convert_all.sh [output_directory]
```

## Output Structure
The tool automatically creates a Source Engine standard folder layout:

```
output/<model_name>/
├── modelsrc/                           ← Source files for studiomdl
│   ├── <model_name>.smd                ← Reference mesh
│   ├── <model_name>_phys.smd           ← Physics collision mesh
│   ├── <model_name>.vta                ← Vertex animation (facial flexes)
│   └── <model_name>.qc                 ← Compile script
│
├── models/<model_name>/                ← Ready for compiled .mdl output
│
└── materials/models/<model_name>/      ← Materials & Textures
    ├── *.vmt                           ← Material definitions
    └── *.png / *.jpg                   ← Extracted textures
```

## Post-Conversion Steps
1. Convert the extracted `.png`/`.jpg` files in `materials/models/<name>/` to `.vtf` using **VTFEdit**.
2. Run `studiomdl.exe` on the `.qc` file inside `modelsrc/` to compile the final `.mdl`.
3. Copy the `models/` and `materials/` directories into your game directory.

## Credits
- **tinygltf** for glTF/VRM parsing.
- **stb_image** for image handling.
- **nlohmann/json** for JSON processing.
