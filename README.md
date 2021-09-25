# FFmpeg code examples
FFmpeg coding tutorial - learn how to code custom transmuxing, transcoding, metadata extraction, frame-by-frame reading and more

## Intro
Welcome, weary traveller! Let me guess why you're here ;) Maybe because you've been trying to figure out how to use libav libraries (libavformat/libavcodec/etc) and failed miserably? Well, it's no suprise, given the fact that ffmpeg is a product made by engineers for engineers. Existing documentation is either lacking or outdated, examples you find online don't work for whatever reason. This repo will hopefully help :3 I've spent weeks of research on ffmpeg internals while trying to figure out how to do remuxing with libav and decided to share my findings. This repo was inspired by github.com/leandromoreira/ffmpeg-libav-tutorial, some other really useful pointers can be found here: https://ffmpeg.org/doxygen/trunk/examples.html

## Required software and dependencies
We will need 3 things:
- linux (my examples are tested on centos and ubuntu only)
- c++ compiler (even though c++ is used marginally in my examples)
- compiled ffmpeg libraries

## Installing dependengies
2 DO: compiler, dep libs

## Compiling FFmpeg
It's possible to install libav binaries from ready packages, for example **sudo apt-get install -y libav-tools**, but I prefer to use the latest stable release of FFmpeg for this and compile everything from source.

2 DO: ffmpeg compilation and installation of libav libs, static and dynamic

## Compile all examples
```bash
git clone git@github.com:tinybit/ffmpeg_code_examples.git
cd ffmpeg_code_examples
make
```

## Examples
After compilation you will find a bunch of binaries in project root. Their meaning and usage are described below.
This examples are not ordered in any particular way, "Example 1" does not mean it's simplier that other ones, even though I've tries keeping code clean and well commented for ease of understanding. Any suggestions / bugfixes are welcome.

### Example 1 - Remuxing
Source: 01-remuxing.cpp
Binary: **remux**
Function: remux from any container with h264 encoded video to FLV container.
Usage: tool takes 2 input arguments
1) path to video file. file should be encoded with h264 codec, in whatever container (mpeg ts, for example)
2) output filename. output file will be written to current directory you're in

### Example 2 - Reading input stream from memory
Source: 02-reading-from_memory.cpp
Binary: **remux**
Function: receiving h264 data in mpeg ts container and remux to FLV on the fly.
2 DO

### Example 3 - Get media info
2 DO

### Example 4 - Transcoding
2 DO

### Example 5 - Streaming to rtmp server
2 DO
