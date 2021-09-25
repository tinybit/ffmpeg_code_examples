/*
*
* File: 02-reading-from-memory.cpp
*
* Author: Rim Zaydullin
* Repo: https://github.com/tinybit/ffmpeg_code_examples
*
* more advanced libav remuxing example.
* read video file from disk, write data into memory buffer, configure AVFormatContext to use
* customized AVIOContext to read from memory buffer, remux to FLV and write result to a file
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
#include "ring_buffer.hpp"

// FileReader class emulates reading from memory, you can implement your own memory reader/buffer following this
// code. you only need to feed AVIOContext.read_packet callback with data, that's all.
// AVIOContext.read_packet callback will be called during av_read_frame(context, packet) and other
// context read operations. see more in functions make_input_ctx(), read_callback() below
class FileReader {
public:
    FileReader(const char* filename) {
        input_file.open(filename, std::ifstream::binary | std::ifstream::in);
    }

    int read(char* data, int size) {
        if (input_file.eof()) {
            return -1;
        }

        input_file.read(data, size);
        return input_file.gcount();
    }

    void close() {
        input_file.close();
    }

    std::ifstream input_file;
};

// functions predeclarations
bool make_input_ctx(AVFormatContext** input_ctx, AVIOContext** avio_input_ctx, FileReader* reader, const char* filename);
bool make_output_ctx(AVFormatContext** output_ctx, const char* format_name, const char* filename);
bool make_streams_map(AVFormatContext** input_ctx, int** streams_map);
bool ctx_init_output_from_input(AVFormatContext** input_ctx, AVFormatContext** output_ctx);
bool open_output_file(AVFormatContext** output_ctx, const char* filename);
bool remux_streams(AVFormatContext** input_ctx, AVFormatContext** output_ctx, int* streams_map);
bool close_output_file(AVFormatContext** output_ctx);

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("You need to pass at least two parameters.\n");
        return EXIT_FAILURE;
    }

    const char* in_filename  = argv[1];
    const char* out_filename = argv[2];

    // create input format context
    FileReader reader(in_filename);     // this is out "memory reader"
    AVIOContext* avio_input_ctx = NULL; // this is IO (input/output) context, needed for i/o customizations
    AVFormatContext* input_ctx = NULL;  // this is AV (audio/video) context
    if (!make_input_ctx(&input_ctx, &avio_input_ctx, &reader, in_filename)) {
        return EXIT_FAILURE;
    }
    
    // create output format context
    AVFormatContext* output_ctx = NULL; // this is AV (audio/video) context
    if (!make_output_ctx(&output_ctx, "flv", out_filename)) {
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

    // close our "memory reader"
    reader.close();

    // cleanup: free memory
    avformat_free_context(input_ctx);
    avformat_free_context(output_ctx);
    av_freep(&streams_map);

    return EXIT_SUCCESS;
}

// this callback will be used for our custom i/o context (AVIOContext)
static int read_callback(void* opaque, uint8_t* buf, int buf_size) {
    auto& reader = *reinterpret_cast<FileReader*>(opaque);
    int read_data_size = reader.read((char*)buf, buf_size);

    if (read_data_size == -1) {
        return AVERROR_EOF; // this is the way to tell our input context that there's no more data
    }

    return read_data_size;
}

bool make_input_ctx(AVFormatContext** input_ctx, AVIOContext** avio_input_ctx, FileReader* reader, const char* filename) {
    // now we need to allocate a memory buffer for our context to use. keep in mind, that buffer size
    // should be chosen correctly for various containers, this noticeably affectes performance
    // NOTE: this buffer is managed by AVIOContext and you should not deallocate by yourself
    const size_t buffer_size = 8192;
    unsigned char* ctx_buffer = (unsigned char*)(av_malloc(buffer_size));
    if (ctx_buffer == NULL) {
        std::cout << "Could not allocate read buffer for AVIOContext\n";
        return false;
    }

    // let's setup a custom AVIOContext for AVFormatContext

    // cast reader to convenient short variable
    void* reader_ptr = reinterpret_cast<void*>(static_cast<FileReader*>(reader));

    // now the important part, we need to create a custom AVIOContext, provide it buffer and
    // buffer size for reading and read callback that will do the actual reading into the buffer
    *avio_input_ctx = avio_alloc_context(
        ctx_buffer,        // memory buffer
        buffer_size,       // memory buffer size
        0,                 // 0 for reading, 1 for writing. we're reading, so — 0.
        reader_ptr,        // pass our reader to context, it will be transparenty passed to read callback on each invocation
        &read_callback,     // out read callback
        NULL,              // write callback — we don't need one
        NULL               // seek callback - we don't need one
    );

    // allocate new AVFormatContext
    *input_ctx = avformat_alloc_context();

    // assign our new and shiny custom i/o context to AVFormatContext
    (*input_ctx)->pb = *avio_input_ctx;

    // note "some_dummy_filename", ffmpeg requires it as some default non-empty placeholder
    int ret = avformat_open_input(input_ctx, "some_dummy_filename", NULL, NULL);
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

bool make_output_ctx(AVFormatContext** output_ctx, const char* format_name, const char* filename) {
    int ret = avformat_alloc_output_context2(output_ctx, NULL, format_name, filename);
    if (ret < 0) {
        std::cout << "Could not create output context, reason: " << av_err2str(ret) << '\n';
        return false;
    }

    if (!(*output_ctx)) {
        std::cout << "Could not create output context, no further details.\n";
        return false;
    }

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

        out_stream->codecpar->codec_tag = 0;
        //av_codec_get_tag(
    }

    return true;
}

bool open_output_file(AVFormatContext** output_ctx, const char* filename) {
    // unless it's a no file (we'll talk later about that) write to the disk (FLAG_WRITE)
    // but basically it's a way to save the file to a buffer so you can store it
    // wherever you want.
    int ret = avio_open(&((*output_ctx)->pb), filename, AVIO_FLAG_WRITE);
    if (ret < 0) {
        std::cout << "Could not open output file " << filename << ", reason: " << av_err2str(ret) << '\n';
        return false;
    }

    // https://ffmpeg.org/doxygen/trunk/group__lavf__encoding.html#ga18b7b10bb5b94c4842de18166bc677cb
    ret = avformat_write_header(*output_ctx, NULL);
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
        
        /* copy packet */
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

    /* close output */
    if (output_ctx && !((*output_ctx)->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_closep(&(*output_ctx)->pb);
        if (ret < 0) {
            std::cout << "Failed to close AV output, reason: " << av_err2str(ret) << '\n';
            return false;
        }
    }

    return true;
}
