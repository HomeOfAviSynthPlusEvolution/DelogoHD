# DeLogoHD

DelogoHD Copyright(C) 2016-2019 msg7086

DelogoHD is an overhaul of the original [delogo](https://github.com/makiuchi-d/delogo-avisynth) filter, which was originally written by [MakKi](https://github.com/makiuchi-d) many years ago.

Legacy parameters are removed due to analog TV signal being deprecated years ago.

This filter provides both AviSynth+ and VapourSynth interfaces. It compiles under both MSVC and GCC.

## Usage

```python
# AviSynth+
DelogoHD(clip, "CCAV 3840x2160.lgd", start = 5, fadein = 2, end = 95, mono = true, cutoff = 5)

# VapourSynth
delogohd.DelogoHD(clip, "CCAV 3840x2160.lgd", start = 5, fadein = 2, end = 95, mono = True, cutoff = 5)
```


Parameters:

- *clip*

    A clip to process. It must have constant format and it must be YUV 8..16 bit with integer samples.

- *logofile*

    Logo file in lgd format.

- *logoname*

    Logo name to be selected from logo file.

    Default: `NULL`, selecting the first one.

- *start*, *end*

    First and last frame that has the logo.

    Default: From 0 to Max

- *fadein*, *fadeout*

    Number of fading frames.

    Default: 0

- *left*, *top*

    Number of pixels in adjusting logo position.

    For example, `left = -5` results logo being 5 pixels left to its original position.

    Default: 0

- *mono*

    Force mono logo, wiping chroma part of the logo.

    Default: false

- *cutoff*

    Wiping near-transparent part of the logo, if its depth is lower than cutoff. Should be less than 1000 (Max logo depth).

    Default: 0

## Compilation (MSVC)

```cmd
mkdir build\x86
pushd build\x86
cmake -DCMAKE_GENERATOR_PLATFORM=Win32 -D_DIR=x86 ..\..\
popd
mkdir build\x64
pushd build\x64
cmake -DCMAKE_GENERATOR_PLATFORM=x64 -D_DIR=x64 ..\..\
popd
cmake --build build\x86 --config Release
cmake --build build\x64 --config Release
```

## Compilation (GCC)

```bash
mkdir -p build/gcc
pushd build/gcc
cmake -G "MSYS Makefiles" -DCMAKE_CXX_FLAGS=-msse4.1 -D_DIR=gcc ../../
popd
cmake --build build/gcc
```

## License 

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

