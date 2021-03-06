# FFmpeg code examples
FFmpeg coding tutorial - learn how to code custom transmuxing, transcoding, metadata extraction, frame-by-frame reading and more

## Intro
Welcome, weary traveller! Let me guess why you're here ;) Maybe because you've been trying to figure out how to use libav libraries (libavformat/libavcodec/etc) and failed miserably? Well, it's no suprise, given the fact that ffmpeg is a product made by engineers for engineers. Existing documentation is either lacking or outdated, examples you find online don't work for whatever reason (most probably they are not compatible with latest FFmpeg source). This repo will hopefully help :3 I've spent a bit of time to research on ffmpeg internals while trying to figure out how to do remuxing and transcoding with libav. This repo is my attempt to share my findings and spare you some time. Code in this repo is not just some snippets, each file is fully finished example that compiles and runs. This repo was inspired by https://github.com/leandromoreira/ffmpeg-libav-tutorial, some other really useful pointers can also be found here: https://ffmpeg.org/doxygen/trunk/examples.html and here: https://github.com/FFmpeg/FFmpeg/tree/master/doc/examples

## Required software and dependencies
We will need 3 things:
- linux (my examples are tested on centos and ubuntu only)
- c++ compiler (even though c++ is used marginally in my examples)
- compiled ffmpeg libraries

## Installing dependencies
2 DO: compiler, dep libs

## Compiling FFmpeg
It's possible to install libav binaries from ready packages, for example **sudo apt-get install -y libav-tools**, but I prefer to use the latest stable release of FFmpeg for this and compile everything from source.

2 DO: ffmpeg compilation and installation of libav libs, static and dynamic

## Compiling all examples
```bash
git clone git@github.com:tinybit/ffmpeg_code_examples.git
cd ffmpeg_code_examples
make
```

## Examples
WARNING: THIS IS NOT PRODUCTION READY CODE! It's intended use is to get familiar with libav and compile working examples.

After compilation you will find a bunch of binaries in project root. Their meaning and usage are described below.
These examples are not ordered in any particular way, "Example 1" does not mean it's simplier that others, even though I've tried keeping code clean and well commented for ease of understanding. Any suggestions / bugfixes are welcome.

### Example 1 - Remuxing
**Source**: 01-remuxing.cpp \
**Binary**: remux \
**Function**: Remuxes from any container with h264 encoded video to FLV container \
**Notes**: None \
**Usage**: Tool takes 2 input arguments
1) Path to video file. file should be encoded with h264 codec, in whatever container (mpeg ts, for example)
2) Output filename. output file will be written to current directory you're in
```bash
./remux test_x264.mp4 test.flv
```

### Example 2 - Reading input stream from memory
**Source**: 02-reading-from-memory.cpp \
**Binary**: remux_from_memory \
**Function**: Reads mpeg ts h264 data from file, puts it into memory buffer and remuxes to FLV on the fly \
**Notes**: Shows how to create AVFormatContext that reads from memory buffer using customized i/o context (AVIOContext) \
**Usage**: Tool takes 2 input arguments
1) Path to video file. file should be encoded with h264 codec, in whatever container (mpeg ts, for example)
2) Output filename. output file will be written to current directory you're in

```bash
./read_from_memory test_x264.mp4 test.flv
```

### Example 3 - Writing output stream to memory
**Source**: 03-writing-to-memory.cpp \
**Binary**: remux_to_memory \
**Function**: Reads mpeg ts h264 data from file stream, remuxes it to FLV on the fly and writes results to memory buffer
**Notes**: Shows how to create AVFormatContext that reads from memory buffer using customized i/o context (AVIOContext) \
**Usage**: Tool takes 2 input arguments
1) Path to video file. file should be encoded with h264 codec, in whatever container (mpeg ts, for example)
2) Output filename. output file will be written to current directory you're in

```bash
./write_to_memory test_x264.mp4 test.flv
```

### Example 4 - Reading input stream from SRT, remux to FLV and write result to file
**Source**: 04-reading-from-srt.cpp \
**Binary**: srt_to_flv \
**Function**: Receives mpeg ts h264 data from SRT stream, puts it into memory buffer and remuxes to FLV on the fly \
**Notes**: Advanced example. Shows how to create simple SRT server and process received media stream with libav. Similar to example 2, but we're reading data sent over the network. Please note that this is not a full-fledged server, it will correctly handle one incoming connection only. \
**Usage**: Tool takes 3 input arguments
1) ip. for SRT server to bind to
2) port. for SRT server to run on
3) Output filename. output file will be written to current directory you're in

```bash
./srt_to_flv 0.0.0.0 9999 test.flv
```

### Example 5 - Get media info
2 DO

### Example 6 - Transcoding
2 DO

### Example 7 - Streaming to rtmp server
2 DO

