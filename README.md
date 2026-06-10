# DelogoHD

DelogoHD is an AviSynth+ and VapourSynth filter for removing or re-applying
logos stored in `.lgd` logo files. It supports planar YUV 4:2:0, 4:2:2, and
4:4:4 clips with 8, 10, 12, 14, or 16-bit integer samples.

## Usage

### AviSynth+

```avisynth
LoadPlugin("DelogoHD.dll")

src = FFVideoSource("input.mp4")
clean = DelogoHD(src, logofile="logo.lgd", start=5, end=95, fadein=2, fadeout=2)
restored = AddlogoHD(clean, logofile="logo.lgd", start=5, end=95, fadein=2, fadeout=2)
return clean
```

### VapourSynth

```python
import vapoursynth as vs

core = vs.core
core.std.LoadPlugin(path="./libDelogoHD.so")

src = core.ffms2.Source("input.mp4")
clean = core.delogohd.DelogoHD(src, logofile="logo.lgd", start=5, end=95, fadein=2, fadeout=2)
restored = core.delogohd.AddlogoHD(clean, logofile="logo.lgd", start=5, end=95, fadein=2, fadeout=2)
clean.set_output()
```

`DelogoHD` removes a logo from the input clip. `AddlogoHD` applies the logo
back to the input clip using the same `.lgd` data and timing controls.

## Parameters

| Parameter | Type | Default | Description |
| --- | --- | --- | --- |
| `clip` | clip | required | Input clip. It must be planar YUV 4:2:0, 4:2:2, or 4:4:4 with 8, 10, 12, 14, or 16-bit integer samples. |
| `logofile` | string | required | Path to an `.lgd` logo file. |
| `logoname` | string | first logo | Logo name to select when the file contains multiple logos. |
| `start` | integer | `0` | First frame on which the logo is active. |
| `end` | integer | max frame number | Last frame on which the logo is active. |
| `fadein` | integer | `0` | Number of frames used to fade the logo in. |
| `fadeout` | integer | `0` | Number of frames used to fade the logo out. |
| `left` | integer | `0` | Horizontal adjustment added to the logo position stored in the `.lgd` file. Negative values move the logo left. |
| `top` | integer | `0` | Vertical adjustment added to the logo position stored in the `.lgd` file. Negative values move the logo up. |
| `mono` | boolean | `false` | Use only the luma part of the logo and ignore chroma logo data. |
| `cutoff` | integer | `0` | Ignore logo pixels whose depth is below this value. `0` disables the cutoff. |
| `opt` | integer | `0` | Backend selector. `1` uses the scalar C backend; all other values use the Highway SIMD backend. Both backends are expected to produce identical output. |

## AviSynth script variables

When an AviSynth filter instance is created, `DelogoHD` and `AddlogoHD`
intentionally set the following local script variables with AviSynth `SetVar`.
They are not set with `SetGlobalVar`.

| Variable | Value |
| --- | --- |
| `delogohd_left` | Original left position from the selected `.lgd` logo header. |
| `delogohd_top` | Original top position from the selected `.lgd` logo header. |
| `delogohd_width` | Logo width from the selected `.lgd` logo header. |
| `delogohd_height` | Logo height from the selected `.lgd` logo header. |

The `left` and `top` parameters are runtime offsets applied by the filter; the
script variables report the original geometry stored in the logo file. These
variables are AviSynth-only and are not exposed by the VapourSynth API.

## Building

DelogoHD uses CMake and FetchContent. A normal build downloads its C++ source
dependencies automatically; no local dependency override is required.

Requirements:

- CMake 3.24 or newer
- A C++23-capable compiler
- Git, for FetchContent dependency downloads
- Python 3, when building and running the test tools

### Linux and other single-config generators

```bash
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --parallel
```

The plugin is written to the build directory, for example
`build/release/libDelogoHD.so` on Linux.

### Windows with Visual Studio

```powershell
cmake -S . -B build/x64 -G "Visual Studio 18 2026" -A x64
cmake --build build/x64 --config Release --parallel
```

For 32-bit builds, use `-A Win32` and a separate build directory.

### Tests

The default CMake configuration builds the baseline test tool. After building,
run:

```bash
ctest --test-dir build/release --output-on-failure
```

The baseline tests verify the same golden output with both the scalar C backend
and the Highway backend.

## License

DelogoHD is licensed under the GNU General Public License version 2 or later.
