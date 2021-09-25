/*
* File: 03-reading-from-srt.cpp
*
* Author: Rim Zaydullin
* Repo: https://github.com/tinybit/ffmpeg_code_examples
*
* receive SRT video stream, remux from mpeg-ts to FLV and write to a file
*
*/

#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <memory>
#include <vector>
#include <thread>
#include <chrono>
#include <mutex>
#include <atomic>
#include <condition_variable>

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavcodec/codec.h>
    #include <libavutil/avutil.h>
    #include <libavutil/mem.h>
    #include <libavutil/mathematics.h>
    #include <libavformat/avio.h>
    #include <libavutil/timestamp.h>
    #include <libavformat/avformat.h>
}

#include <srt/srt.h>

#include "ring_buffer.hpp"

// predeclarations
int start(int argc, char** argv);

#ifdef av_err2str
#undef av_err2str
#include <string>
av_always_inline std::string av_err2string(int errnum) {
    char str[AV_ERROR_MAX_STRING_SIZE];
    return av_make_error_string(str, AV_ERROR_MAX_STRING_SIZE, errnum);
}
#define av_err2str(err) av_err2string(err).c_str()
#endif  // av_err2str

std::mutex buf_mutex;
std::condition_variable cond;
std::atomic<bool> done{false};
std::atomic<bool> done_converting_to_flv{false};
int count_read = 0;

std::ofstream wf("test.flv", std::ios::out | std::ios::binary);

static int readFunction(void* opaque, uint8_t* buf, int buf_size) {
    auto& buff = *reinterpret_cast<RingBuffer*>(opaque);

    std::unique_lock<std::mutex> lk(buf_mutex);

    // wait for more data arrival
    while (buff.size() == 0) {
        if (done.load()) {
            return 0;
        }
        
        cond.wait(lk);
    }
    
    size_t size = buff.read((char*)buf, buf_size);
    cond.notify_one();

    count_read += size;
    return size;
}

static int writeFunction(void* opaque, uint8_t* buf, int buf_size) {
    //std::cout << "write flv chunk with size: " << buf_size << std::endl << std::flush;
    wf.write((char *)buf, buf_size);
}

void convert_to_flv(AVIOContext* avio_input_context, AVIOContext* avio_output_context) {
    std::cout << "convert_to_flv" << std::endl << std::flush;

    AVFormatContext *output_format_context = NULL;
    AVPacket packet;

    int  ret, i;
    int  stream_index = 0;
    int* streams_list = NULL;
    int  number_of_streams = 0;

    AVFormatContext* input_format_context = avformat_alloc_context();
    input_format_context->pb = avio_input_context;
    avformat_open_input(&input_format_context, "dummyFilename", nullptr, nullptr);

    if ((ret = avformat_find_stream_info(input_format_context, NULL)) < 0) {
        fprintf(stderr, "Failed to retrieve input stream information");
        done_converting_to_flv.store(true);
        return;
    }

    // https://ffmpeg.org/doxygen/trunk/group__lavf__misc.html#gae2645941f2dc779c307eb6314fd39f10
    std::cout << "---------------------------------- INPUT FORMAT ----------------------------" << std::endl << std::flush;
    av_dump_format(input_format_context, 0, NULL, 0);
    
    avformat_alloc_output_context2(&output_format_context, NULL, "flv", "dummyFilename");
    if (!output_format_context) {
        fprintf(stderr, "Could not create output context\n");
        ret = AVERROR_UNKNOWN;
        done_converting_to_flv.store(true);
        return;
    }

    output_format_context->pb = avio_output_context;
    output_format_context->flags |= AVFMT_FLAG_CUSTOM_IO | AVFMT_NOFILE;

    number_of_streams = input_format_context->nb_streams;
    streams_list = (int*)av_mallocz_array(number_of_streams, sizeof(*streams_list));

    if (!streams_list) {
        ret = AVERROR(ENOMEM);
        done_converting_to_flv.store(true);
        return;
    }

    for (i = 0; i < input_format_context->nb_streams; i++) {
        AVStream *out_stream;
        AVStream *in_stream = input_format_context->streams[i];
        AVCodecParameters *in_codecpar = in_stream->codecpar;

        if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
            streams_list[i] = -1;
            continue;
        }

        streams_list[i] = stream_index++;
        
        out_stream = avformat_new_stream(output_format_context, NULL);
        if (!out_stream) {
            fprintf(stderr, "Failed allocating output stream\n");
            ret = AVERROR_UNKNOWN;
            done_converting_to_flv.store(true);
            return;
        }

        ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
        if (ret < 0) {
            fprintf(stderr, "Failed to copy codec parameters\n");
            done_converting_to_flv.store(true);
            return;
        }

        out_stream->codecpar->codec_tag = 0;
    }

    std::cout << "---------------------------------- OUTPUT FORMAT ----------------------------" << std::endl << std::flush;
    av_dump_format(output_format_context, 0, NULL, 1);

    // // unless it's a no file (we'll talk later about that) write to the disk (FLAG_WRITE)
    // // but basically it's a way to save the file to a buffer so you can store it
    // // wherever you want.
    // if (!(output_format_context->oformat->flags & AVFMT_NOFILE)) {
    //     ret = avio_open(&output_format_context->pb, "test.flv", AVIO_FLAG_WRITE);
    //     if (ret < 0) {
    //         fprintf(stderr, "Could not open output file '%s'", "test.flv");
    //         done_converting_to_flv.store(true);
    //         return;
    //     }
    // }

    // https://ffmpeg.org/doxygen/trunk/group__lavf__encoding.html#ga18b7b10bb5b94c4842de18166bc677cb
    ret = avformat_write_header(output_format_context, NULL);
    if (ret < 0) {
        fprintf(stderr, "Error occurred when opening output file\n");
        goto end;
    }

    while (!done.load()) {
        AVStream *in_stream, *out_stream;
        ret = av_read_frame(input_format_context, &packet);
        if (ret < 0) {
            break;
        }

        in_stream  = input_format_context->streams[packet.stream_index];
        if (packet.stream_index >= number_of_streams || streams_list[packet.stream_index] < 0) {
            av_packet_unref(&packet);
            continue;
        }

        packet.stream_index = streams_list[packet.stream_index];
        out_stream = output_format_context->streams[packet.stream_index];

        /* copy packet */
        packet.pts = av_rescale_q_rnd(packet.pts, in_stream->time_base, out_stream->time_base, AVRounding(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
        packet.dts = av_rescale_q_rnd(packet.dts, in_stream->time_base, out_stream->time_base, AVRounding(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
        packet.duration = av_rescale_q(packet.duration, in_stream->time_base, out_stream->time_base);

        // https://ffmpeg.org/doxygen/trunk/structAVPacket.html#ab5793d8195cf4789dfb3913b7a693903
        packet.pos = -1;

        //https://ffmpeg.org/doxygen/trunk/group__lavf__encoding.html#ga37352ed2c63493c38219d935e71db6c1
        ret = av_interleaved_write_frame(output_format_context, &packet);
        if (ret < 0) {
            fprintf(stderr, "Error muxing packet\n");
            break;
        }

        av_packet_unref(&packet);
    }

    //https://ffmpeg.org/doxygen/trunk/group__lavf__encoding.html#ga7f14007e7dc8f481f054b21614dfec13
    av_write_trailer(output_format_context);

end:
    avformat_close_input(&input_format_context);

    /* close output */
    if (output_format_context && !(output_format_context->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&output_format_context->pb);
    }

    avformat_free_context(output_format_context);
    av_freep(&streams_list);

    if (ret < 0 && ret != AVERROR_EOF) {
        fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
    }

    done_converting_to_flv.store(true);
}

int ss, st;

int main(int argc, char **argv) {
    RingBuffer buff(40960);

    unsigned char* in_buffer = (unsigned char*)(av_malloc(8192));
    AVIOContext* avio_input_context = avio_alloc_context(
        in_buffer,
        8192,
        0,
        reinterpret_cast<void*>(static_cast<RingBuffer*>(&buff)),
        &readFunction, nullptr, nullptr);

    unsigned char* out_buffer = (unsigned char*)(av_malloc(8192));
    AVIOContext* avio_output_context = avio_alloc_context(
        out_buffer,
        8192,
        1,
        reinterpret_cast<void*>(static_cast<RingBuffer*>(&buff)),
        nullptr, &writeFunction, nullptr);


    int their_fd = start(argc, argv);
    int count = 0;

    std::thread flv_thread(convert_to_flv, avio_input_context, avio_output_context);

    int i;
    for (i = 0; i < 20000; i++) {
        char msg[2048];
        st = srt_recvmsg(their_fd, msg, sizeof msg);

        if (st == SRT_ERROR) {
            break;
        }

        count += st;

        if (!done_converting_to_flv.load()) {
            std::unique_lock<std::mutex> lk(buf_mutex);

            // wait for available free space in ring buffer
            while (buff.avail() < st) {
                if (done.load()) {
                    break;
                }
                
                cond.wait(lk);
            }

            buff.write(msg, st);
            cond.notify_one();
        }
    }

    std::cout << "DONE\n";
    wf.close();

    done.store(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    cond.notify_all();
    flv_thread.join();

    std::cout << "RECEIVED " << count << " bytes.\n" << std::flush;
    std::cout << "READ " << count_read << " bytes for avformat.\n" << std::flush;

    av_free(avio_input_context);
    //av_free(avio_output_context);

    return 0;
}

int start(int argc, char** argv) {
    struct sockaddr_in sa;
    int yes = 1;
    struct sockaddr_storage their_addr;

    if (argc != 3) {
      fprintf(stderr, "Usage: %s <host> <port>\n", argv[0]);
      return 1;
    }

    printf("srt startup\n");
    srt_startup();

    printf("srt socket\n");
    ss = srt_create_socket();
    if (ss == SRT_ERROR) {
        fprintf(stderr, "srt_socket: %s\n", srt_getlasterror_str());
        return 1;
    }

    printf("srt bind address\n");
    sa.sin_family = AF_INET;
    sa.sin_port = htons(atoi(argv[2]));
    if (inet_pton(AF_INET, argv[1], &sa.sin_addr) != 1) {
        return 1;
    }

    printf("srt setsockflag\n");
    srt_setsockflag(ss, SRTO_RCVSYN, &yes, sizeof yes);

    printf("srt bind\n");
    st = srt_bind(ss, (struct sockaddr*)&sa, sizeof sa);
    if (st == SRT_ERROR) {
        fprintf(stderr, "srt_bind: %s\n", srt_getlasterror_str());
        return 1;
    }

    printf("srt listen\n");
    st = srt_listen(ss, 2);
    if (st == SRT_ERROR) {
        fprintf(stderr, "srt_listen: %s\n", srt_getlasterror_str());
        return 1;
    }

    printf("srt accept\n");
    int addr_size = sizeof their_addr;
    return srt_accept(ss, (struct sockaddr *)&their_addr, &addr_size);
}

int stop() {
    // outdata.close();
    printf("srt close\n");

    st = srt_close(ss);
    if (st == SRT_ERROR)
    {
        fprintf(stderr, "srt_close: %s\n", srt_getlasterror_str());
        return 1;
    }

    printf("srt cleanup\n");
    srt_cleanup();
}