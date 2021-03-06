/*
*
* File: 04-reading-from-srt.cpp
*
* Author: Rim Zaydullin
* Repo: https://github.com/tinybit/ffmpeg_code_examples
*
* advanced libav remuxing example.
* receive live video stream from SRT client, write data into memory buffer, configure AVFormatContext
* to read from memory buffer, remux to FLV and write result to a file. receiving SRT data happens on
* main thread and remuxing into FLV runs on separate thread. ring buffer is used to pass stream data
* between threads.
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
#include <thread>
#include <chrono>
#include <mutex>
#include <atomic>
#include <condition_variable>

extern "C" {
    #include <libavformat/avformat.h>
}

#include <srt/srt.h>

#include "helpers.hpp"
#include "ring_buffer.hpp"

int srt_server_socket = 0;
int bytes_remuxed_to_flv = 0;
std::mutex buf_mutex;
std::condition_variable cond;
std::atomic<bool> receiving_srt_data_done{false};
std::atomic<bool> remuxing_thread_done{false};

// functions predeclarations
bool make_input_ctx(AVFormatContext** input_ctx, AVIOContext** avio_input_ctx, RingBuffer* reader);
bool make_output_ctx(AVFormatContext** output_ctx, const char* format_name, const char* filename);
bool make_streams_map(AVFormatContext** input_ctx, int** streams_map);
bool ctx_init_output_from_input(AVFormatContext** input_ctx, AVFormatContext** output_ctx);
bool open_output_file(AVFormatContext** output_ctx, const char* filename);
bool remux_streams(AVFormatContext** input_ctx, AVFormatContext** output_ctx, int* streams_map);
bool close_output_file(AVFormatContext** output_ctx);
void remux_to_flv_worker(RingBuffer* buff, const char* out_filename);
int start_srt_server(const char* ip, const char* port);
int stop_srt_server();

int main(int argc, char **argv) {
    if (argc != 4) {
        std::cout << "Usage: " << argv[0] << " <host> <port> <output file>\n";
        return EXIT_FAILURE;
    }

    const char* ip  = argv[1];
    const char* port  = argv[2];
    const char* out_filename = argv[3];

    // RingBuffer is our "memory reader", we receive raw TS packets from SRT client and write them
    // to ring buffer to be consumed by libav
    RingBuffer buff(40960);

    // start SRT server
    int their_fd = start_srt_server(ip, port);
    int bytes_received_from_srt = 0;

    // start remuxing thread
    std::thread remuxing_thread(remux_to_flv_worker, &buff, out_filename);

    // receive data from SRT client
    int i;
    for (i = 0; i < 40000; i++) {
        char msg[2048];
        int st = srt_recvmsg(their_fd, msg, sizeof msg);
        if (st == SRT_ERROR) {
            break;
        }

        bytes_received_from_srt += st;

        if (!remuxing_thread_done.load()) {
            std::unique_lock<std::mutex> lk(buf_mutex);

            // wait for available free space in ring buffer
            while (buff.avail() < st) {
                // are we done processing data?
                if (receiving_srt_data_done.load()) {
                    break;
                }

                cond.wait(lk);   // wait till ringbuffer has enough available space again
            }

            buff.write(msg, st); // write SRT bytes to ring buffer
            cond.notify_one();   // wake up remuxing thread to continue data consumption from ring buffer
        }
    }

    // set done flag
    receiving_srt_data_done.store(true);

    // allow remuxing thread to consume whatever is left in ring buffer
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    cond.notify_all(); // notify current thread and remuxing thread that we're done
    remuxing_thread.join(); // join remuxing thread

    std::cout << "Received from SRT: " << bytes_received_from_srt << " bytes.\n" << std::flush;
    std::cout << "Remuxed to FLV:    " << bytes_remuxed_to_flv << " bytes.\n" << std::flush;

    stop_srt_server();

    return EXIT_SUCCESS;
}

void remux_to_flv_worker(RingBuffer* buff, const char* out_filename) {
    // create input format context
    AVIOContext* avio_input_ctx = NULL; // this is IO (input/output) context, needed for i/o customizations
    AVFormatContext* input_ctx = NULL;  // this is AV (audio/video) context
    if (!make_input_ctx(&input_ctx, &avio_input_ctx, buff)) {
        return;
    }
    
    // create output format context
    AVFormatContext* output_ctx = NULL; // this is AV (audio/video) context
    if (!make_output_ctx(&output_ctx, "flv", out_filename)) {
        return;
    }

    // create streams map, filtering out all streams except audio/video
    int* streams_map = NULL;
    if (!make_streams_map(&input_ctx, &streams_map)) {
        return;
    }

    // init output context from input context (create output streams in output context, copying codec params from input)
    if (!ctx_init_output_from_input(&input_ctx, &output_ctx)) {
        return;
    }

    // dump input and output formats/streams info
    // https://ffmpeg.org/doxygen/trunk/group__lavf__misc.html#gae2645941f2dc779c307eb6314fd39f10
    std::cout << "-------------------------------- IN ------------------------------------\n";
    av_dump_format(input_ctx, 0, "", 0);
    std::cout << "-------------------------------- OUT -----------------------------------\n";
    av_dump_format(output_ctx, 0, out_filename, 1);
    std::cout << "------------------------------------------------------------------------\n";

    // create and open output file and write file header
    if (!open_output_file(&output_ctx, out_filename)) {
        return;
    }

    // read input file streams, remux them and write into output file
    if (!remux_streams(&input_ctx, &output_ctx, streams_map)) {
        return;
    }

    // close output file
    if (!close_output_file(&output_ctx)) {
        return;
    }

    // close input context
    avformat_close_input(&input_ctx);

    // cleanup: free memory
    avformat_free_context(input_ctx);
    avformat_free_context(output_ctx);
    av_freep(&streams_map);
    av_freep(&avio_input_ctx);
}

// this callback will be used for our custom i/o context (AVIOContext)
static int read_callback(void* opaque, uint8_t* buf, int buf_size) {
    auto& buff = *reinterpret_cast<RingBuffer*>(opaque);

    std::unique_lock<std::mutex> lk(buf_mutex);

    // wait for more data arrival
    while (buff.size() == 0) {
        if (receiving_srt_data_done.load()) {
            return AVERROR_EOF; // this is the way to tell our input context that there's no more data
        }
        
        cond.wait(lk);
    }
    
    size_t read_size = buff.read((char*)buf, buf_size);
    cond.notify_one();

    bytes_remuxed_to_flv += read_size;
    return read_size;
}

bool make_input_ctx(AVFormatContext** input_ctx, AVIOContext** avio_input_ctx, RingBuffer* buff) {
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
    void* reader_ptr = reinterpret_cast<void*>(static_cast<RingBuffer*>(buff));

    // now the important part, we need to create a custom AVIOContext, provide it buffer and
    // buffer size for reading and read callback that will do the actual reading into the buffer
    *avio_input_ctx = avio_alloc_context(
        ctx_buffer,        // memory buffer
        buffer_size,       // memory buffer size
        0,                 // 0 for reading, 1 for writing. we're reading, so ??? 0.
        reader_ptr,        // pass our reader to context, it will be transparenty passed to read callback on each invocation
        &read_callback,    // out read callback
        NULL,              // write callback ??? we don't need one
        NULL               // seek callback - we don't need one
    );

    // allocate new AVFormatContext
    *input_ctx = avformat_alloc_context();

    // assign our new and shiny custom i/o context to AVFormatContext
    (*input_ctx)->pb = *avio_input_ctx;

    // tell our input context that we're using custom i/o and there's no backing file
    //(*input_ctx)->flags |= AVFMT_FLAG_CUSTOM_IO | AVFMT_NOFILE;

    // note "some_dummy_filename", ffmpeg requires it as some default non-empty placeholder
    int ret = avformat_open_input(input_ctx, "some_dummy_filename", NULL, NULL);
    if (ret < 0) {
        std::cout << "Could not open input stream, reason: " << av_err2str(ret) << '\n';
        return false;
    }

    ret = avformat_find_stream_info(*input_ctx, NULL);
    if (ret < 0) {
        std::cout << "Failed to retrieve input stream information, reason: " << av_err2str(ret) << '\n';
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

        // set stream codec tag to 0, for libav to detect automatically
        out_stream->codecpar->codec_tag = 0;
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

        AVRounding avr = (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
        packet.pts = av_rescale_q_rnd(packet.pts, in_stream->time_base, out_stream->time_base, avr);
        packet.dts = av_rescale_q_rnd(packet.dts, in_stream->time_base, out_stream->time_base, avr);
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

int start_srt_server(const char* ip, const char* port) {
    struct sockaddr_in sa;
    struct sockaddr_storage their_addr;

    printf("srt startup\n");
    srt_startup();

    printf("srt socket\n");
    srt_server_socket = srt_create_socket();
    if (srt_server_socket == SRT_ERROR) {
        fprintf(stderr, "srt_socket: %s\n", srt_getlasterror_str());
        return 1;
    }

    printf("srt bind address\n");
    sa.sin_family = AF_INET;
    sa.sin_port = htons(atoi(port));
    if (inet_pton(AF_INET, ip, &sa.sin_addr) != 1) {
        return 1;
    }

    printf("srt setsockflag SRTO_RCVSYN = true\n");
    int yes = 1;
    srt_setsockflag(srt_server_socket, SRTO_RCVSYN, &yes, sizeof yes);

    printf("srt bind\n");
    int st = srt_bind(srt_server_socket, (struct sockaddr*)&sa, sizeof sa);
    if (st == SRT_ERROR) {
        fprintf(stderr, "srt_bind: %s\n", srt_getlasterror_str());
        return 1;
    }

    printf("srt listen\n");
    st = srt_listen(srt_server_socket, 2);
    if (st == SRT_ERROR) {
        fprintf(stderr, "srt_listen: %s\n", srt_getlasterror_str());
        return 1;
    }

    printf("srt accept\n");
    int addr_size = sizeof their_addr;
    SRTSOCKET client_socket = srt_accept(srt_server_socket, (struct sockaddr *)&their_addr, &addr_size);

    printf("srt client connected\n");
    return client_socket;
}

int stop_srt_server() {
    printf("srt close\n");

    if (srt_close(srt_server_socket) == SRT_ERROR) {
        fprintf(stderr, "srt_close: %s\n", srt_getlasterror_str());
        return 1;
    }

    printf("srt cleanup\n");
    srt_cleanup();
}