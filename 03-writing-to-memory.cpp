/*
*
* File: 03-writing-to-memory.cpp
*
* Author: Rim Zaydullin
* Repo: https://github.com/tinybit/ffmpeg_code_examples
*
* more advanced libav remuxing example.
* read video file from disk, remux to FLV and write results to memory
* we will use customized AVIOContext to handle write requests from AVFormatContext
*
* input file requirements:
* - video must be encoded with wither h264 or vp6 video codecs
* - audio must be encoded with mp3 or aac codecs
* the above are FLV container limitations
*
*/

#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <fstream>

extern "C" {
    #include <libavformat/avformat.h>
}

#include "helpers.hpp"

// FileWriter class emulates writing from memory, you can implement your own memory writer/buffer following this
// code. you only need to feed AVIOContext.write_packet callback with data, that's all.
// AVIOContext.read_packet callback will be called during av_write_frame(context, packet),
// av_interleaved_write_frame(context, packet), avformat_write_header() and other context write operations.
// see more in functions make_output_ctx(), write_callback() below
// NOTE: FileWriter.seek() and seek_callback(void*, int64_t, int) can be omited for either file processing or
// live streaming, they are needed for the method av_write_trailer(context), which writes non-critical metadata
// to video file header. that video header will be missing in case of live streaming especially, so seeking
// code can be removed. for video files it's more correct to update this meta, so it's still useful and
// supresses errors "Failed to update header with correct duration."
class FileWriter {
public:
    FileWriter(const char* filename) {
        output_file.open(filename, std::ifstream::binary | std::ifstream::out);
    }

    void write(char* data, int size) {
        output_file.write(data, size);
    }

    void seek(int pos) {
        output_file.seekp(pos);
    }

    int size() {
        // store current writing position
        auto pos = output_file.tellp();

        // jump to end writing position, this is the side of data in bytes
        output_file.seekp(std::ios_base::end);
        int file_size = output_file.tellp();

        // restore previous position
        output_file.seekp(pos);

        return file_size;
    }

    void close() {
        output_file.close();
    }

    std::ofstream output_file;
};

// functions predeclarations
bool make_input_ctx(AVFormatContext** input_ctx, const char* filename);
bool make_output_ctx(AVFormatContext** output_ctx, AVIOContext** avio_output_ctx, FileWriter* writer, const char* format_name, const char* filename);
bool make_streams_map(AVFormatContext** input_ctx, int** streams_map);
bool ctx_init_output_from_input(AVFormatContext** input_ctx, AVFormatContext** output_ctx);
bool open_output_file(AVFormatContext** output_ctx, const char* filename);
bool remux_streams(AVFormatContext** input_ctx, AVFormatContext** output_ctx, int* streams_map);
bool close_output_file(AVFormatContext** output_ctx);

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cout << "Usage: " << argv[0] << " <input file> <output file>\n";
        return EXIT_FAILURE;
    }

    const char* in_filename  = argv[1];
    const char* out_filename = argv[2];

    // create input format context
    AVFormatContext* input_ctx = NULL;
    if (!make_input_ctx(&input_ctx, in_filename)) {
        return EXIT_FAILURE;
    }
    
    // create output format contex
    FileWriter writer(out_filename);     // this is output "memory writer"
    AVIOContext* avio_output_ctx = NULL; // this is IO (input/output) context, needed for i/o customizations
    AVFormatContext* output_ctx = NULL;  // this is AV (audio/video) context
    if (!make_output_ctx(&output_ctx, &avio_output_ctx, &writer, "flv", out_filename)) {
        return EXIT_FAILURE;
    }

    // create streams map, filtering out all streams except audio/video
    int* streams_map = NULL;
    if (!make_streams_map(&input_ctx, &streams_map)) {
        return EXIT_FAILURE;
    }

    // init output context from input context (create output streams in output context, copying codec params from input)
    if (!ctx_init_output_from_input(&input_ctx, &output_ctx)) {
        return EXIT_FAILURE;
    }

    // dump input and output formats/streams info
    // https://ffmpeg.org/doxygen/trunk/group__lavf__misc.html#gae2645941f2dc779c307eb6314fd39f10
    std::cout << "-------------------------------- IN ------------------------------------\n";
    av_dump_format(input_ctx, 0, in_filename, 0);
    std::cout << "-------------------------------- OUT -----------------------------------\n";
    av_dump_format(output_ctx, 0, out_filename, 1);
    std::cout << "------------------------------------------------------------------------\n";

    // create and open output file and write file header
    if (!open_output_file(&output_ctx, out_filename)) {
        return EXIT_FAILURE;
    }

    // read input file streams, remux them and write into output file
    if (!remux_streams(&input_ctx, &output_ctx, streams_map)) {
        return EXIT_FAILURE;
    }

    // close output file
    if (!close_output_file(&output_ctx)) {
        return EXIT_FAILURE;
    }

    // close input context
    avformat_close_input(&input_ctx);

    // close our "memory writer"
    writer.close();

    // cleanup: free memory
    avformat_free_context(input_ctx);
    avformat_free_context(output_ctx);
    av_freep(&streams_map);
    av_freep(&avio_output_ctx);

    return EXIT_SUCCESS;
}

// this callback will be used for our custom i/o context (AVIOContext)
static int write_callback(void* opaque, uint8_t* buf, int buf_size) {
    auto& writer = *reinterpret_cast<FileWriter*>(opaque);
    writer.write((char*)buf, buf_size);

    std::cout << buf_size << std::endl;

    return buf_size; // we always write all requested data succesfully
}

// this callback will be used for seeking through our data
// NOTE: FileWriter.seek() and seek_callback(void*, int64_t, int) can be omited for either file processing or
// live streaming, they are needed for the method av_write_trailer(context), which writes non-critical metadata
// to video file header. that video header will be missing in case of live streaming especially, so seeking
// code can be removed. for video files it's more correct to update this meta, so it's still useful and
// supresses errors "Failed to update header with correct duration."
static int64_t seek_callback(void *opaque, int64_t offset, int whence) {
    // auto& writer = *reinterpret_cast<FileWriter*>(opaque);

    // if (whence == 0 || (whence & AVIO_SEEKABLE_NORMAL)) {
    //     writer.seek(int(offset));
    //     return offset;
    // } else if (whence & AVSEEK_SIZE) {
    //     return writer.size();
    // }
    
    return AVERROR(EIO); // unexpected seek request, treat it as error
}

bool make_input_ctx(AVFormatContext** input_ctx, const char* filename) {
    int ret = avformat_open_input(input_ctx, filename, NULL, NULL);
    if (ret < 0) {
        std::cout << "Could not open input file " << filename << ", reason: " << av_err2str(ret) << '\n';
        return false;
    }

    ret = avformat_find_stream_info(*input_ctx, NULL);
    if (ret < 0) {
        std::cout << "Failed to retrieve input stream information from " << filename << ", reason: " << av_err2str(ret) << '\n';
        return false;
    }

    return true;
}

bool make_output_ctx(AVFormatContext** output_ctx, AVIOContext** avio_output_ctx, FileWriter* writer, const char* format_name, const char* filename) {
    // now we need to allocate a memory buffer for our context to use. keep in mind, that buffer size
    // should be chosen correctly for various containers, this noticeably affectes performance
    // NOTE: this buffer is managed by AVIOContext and you should not deallocate it by yourself
    const size_t buffer_size = 8192;
    unsigned char* ctx_buffer = (unsigned char*)(av_malloc(buffer_size));
    if (ctx_buffer == NULL) {
        std::cout << "Could not allocate read buffer for AVIOContext\n";
        return false;
    }

    // let's setup a custom AVIOContext for AVFormatContext

    // cast writer to convenient short variable
    void* writer_ptr = reinterpret_cast<void*>(static_cast<FileWriter*>(writer));

    // now the important part, we need to create a custom AVIOContext, provide it buffer and
    // buffer size for writing and write callback that will do the actual reading into the buffer
    // NOTE: seek callback is implemented too, see seek_callback() function description
    *avio_output_ctx = avio_alloc_context(
        ctx_buffer,        // memory buffer
        buffer_size,       // memory buffer size
        1,                 // 0 for reading, 1 for writing. we're writing, so — 1.
        writer_ptr,        // pass our writer to context, it will be transparenty passed to write callback on each invocation
        NULL,              // read callback — we don't need one
        &write_callback,   // our write callback 
        &seek_callback     // our seek callback
    );

    // allocate new AVFormatContext. note "some_dummy_filename", ffmpeg requires it as some default non-empty placeholder
    int ret = avformat_alloc_output_context2(output_ctx, NULL, "flv", "some_dummy_filename");
    if (ret < 0) {
        std::cout << "Could not create output context, reason: " << av_err2str(ret) << '\n';
        return false;
    }

    if (!(*output_ctx)) {
        std::cout << "Could not create output context, no further details.\n";
        return false;
    }

    // assign our new and shiny custom i/o context to AVFormatContext
    (*output_ctx)->pb = *avio_output_ctx;
    (*output_ctx)->flags |= AVFMT_FLAG_CUSTOM_IO | AVFMT_NOFILE;

    return true;
}

bool make_streams_map(AVFormatContext** input_ctx, int** streams_map) {
    int* smap = NULL;
    int stream_index = 0;
    int input_streams_count = (*input_ctx)->nb_streams;

    smap = (int*)av_mallocz_array(input_streams_count, sizeof(int));
    if (!smap) {
        std::cout << "Could not allocate streams list.\n";
        return false;
    }

    for (int i = 0; i < input_streams_count; i++) {
        AVCodecParameters* c = (*input_ctx)->streams[i]->codecpar;
        if (c->codec_type != AVMEDIA_TYPE_AUDIO && c->codec_type != AVMEDIA_TYPE_VIDEO) {
            smap[i] = -1;
            continue;
        }

        smap[i] = stream_index++;
    }

    *streams_map = smap;
    return true;
}

bool ctx_init_output_from_input(AVFormatContext** input_ctx, AVFormatContext** output_ctx) {
    int input_streams_count = (*input_ctx)->nb_streams;

    for (int i = 0; i < input_streams_count; i++) {
        AVStream* in_stream = (*input_ctx)->streams[i];
        AVCodecParameters* in_codecpar = in_stream->codecpar;

        if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO && in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO) {
            continue;
        }

        AVStream* out_stream = avformat_new_stream(*output_ctx, NULL);
        if (!out_stream) {
            std::cout << "Failed allocating output stream\n";
            return false;
        }

        int ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
        if (ret < 0) {
            std::cout << "Failed to copy codec parameters, reason: " << av_err2str(ret) << '\n';
            return false;
        }

        // set stream codec tag to 0, for libav to detect automatically
        out_stream->codecpar->codec_tag = 0;
    }

    return true;
}

bool open_output_file(AVFormatContext** output_ctx, const char* filename) {
    // https://ffmpeg.org/doxygen/trunk/group__lavf__encoding.html#ga18b7b10bb5b94c4842de18166bc677cb
    int ret = avformat_write_header(*output_ctx, NULL);
    if (ret < 0) {
        std::cout << "Failed to write output file header to " << filename << ", reason: " << av_err2str(ret) << '\n';
        return false;
    }

    return true;
}

bool remux_streams(AVFormatContext** input_ctx, AVFormatContext** output_ctx, int* streams_map) {
    AVPacket packet;
    int input_streams_count = (*input_ctx)->nb_streams;

    while (1) {
        int ret = av_read_frame(*input_ctx, &packet);
        if (ret == AVERROR_EOF) { // we have reached end of input file
            break;
        }

        // handle any other error
        if (ret < 0) {
            std::cout << "Failed to read packet from input, reason: " << av_err2str(ret) << '\n';
            return false;
        }

        // ignore any packets that are present in non-mapped streams
        if (packet.stream_index >= input_streams_count || streams_map[packet.stream_index] < 0) {
            av_packet_unref(&packet);
            continue;
        }

        // set stream index, based on our map
        packet.stream_index = streams_map[packet.stream_index];
        
        // copy packet
        AVStream* in_stream  = (*input_ctx)->streams[packet.stream_index];
        AVStream* out_stream = (*output_ctx)->streams[packet.stream_index];
        packet.pts = av_rescale_q_rnd(packet.pts, in_stream->time_base, out_stream->time_base, AVRounding(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
        packet.dts = av_rescale_q_rnd(packet.dts, in_stream->time_base, out_stream->time_base, AVRounding(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
        packet.duration = av_rescale_q(packet.duration, in_stream->time_base, out_stream->time_base);

        // https://ffmpeg.org/doxygen/trunk/structAVPacket.html#ab5793d8195cf4789dfb3913b7a693903
        packet.pos = -1;

        //https://ffmpeg.org/doxygen/trunk/group__lavf__encoding.html#ga37352ed2c63493c38219d935e71db6c1
        ret = av_interleaved_write_frame(*output_ctx, &packet);
        if (ret < 0) {
            std::cout << "Failed to write packet to output, reason: " << av_err2str(ret) << '\n';
            return false;
        }

        av_packet_unref(&packet);
    }

    return true;
}

bool close_output_file(AVFormatContext** output_ctx) {
    //https://ffmpeg.org/doxygen/trunk/group__lavf__encoding.html#ga7f14007e7dc8f481f054b21614dfec13
    int ret = av_write_trailer(*output_ctx);
    if (ret < 0) {
        std::cout << "Failed to write trailer to output, reason: " << av_err2str(ret) << '\n';
        return false;
    }

    return true;
}
