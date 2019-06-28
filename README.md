# DeLogoHD

DelogoHD r3 Copyright(C) 2016-2019 msg7086

"The" [delogo](https://github.com/makiuchi-d/delogo-avisynth) filter, which was originally written by [MakKi](https://github.com/makiuchi-d) many years ago, is now HD Remastered™! /joke

Basically, you will find that some of the options are gone. For example, no more quarter pixel offset adjusting or color changing on the logo pattern.

Why? You ask.

It's chiefly because those code are complicated. They are useful back to 10 years ago when you may have different recoding devices and you need to adjust the logo accordingly. However nowadays people get sources in a much better quality, and those code becomes just a payload and almost doing nothing. I guess it's the time to get them removed.

Sure, if someone found them useful, we can try porting them again.

An AVS filter is the first step. A cross-platform (AVS & VS, Windows & POSIX) filter is our goal.

## License 

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

## Usage

```
LoadPlugin("DelogoHD.dll")
DelogoHD(clip, string logofile, ...)

    clip               - clip to be processed.
    logofile           - lgd file, scanned by logoscan tool.
    logoname           - (optional) the name of the logo. (default = first found in file)
    start / end        - (optional) beginning / end of the logo, in frames. (default = 0 / -1)
    fadein / fadeout   - (optional) fade in / out of the logo, in frames. (default = 0 / 0)
    left / top         - (optional) adjust logo position, in pixels. (default = 0 / 0)
    mono   (new)       - (optional) skip chroma, may work better with monochrome logo. Use at your own risk. (default = false)
    cutoff (new)       - (optional) zerofill logo pixels whose alpha (depth) of luma is below cutoff. (default = 0)
```

## Example

```
DelogoHD("CCAV 3840x2160.lgd", start = 5, end = 95)
```

## Known issues

* Does not support VapourSynth, yet
* Does not support delogo with negative left or top
