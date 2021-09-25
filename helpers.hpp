/*
* File: ring_buffer.hpp
*
* Author: Rim Zaydullin
* Repo: https://github.com/tinybit/ffmpeg_code_examples
*
* simple ringbuffer implementation, non-threadsafe
*
*/

#ifndef helpers_hpp
#define helpers_hpp

extern "C" {
    #include <libavformat/avformat.h>
}

// av_err2str — fixed version
#ifdef av_err2str
#undef av_err2str
#include <string>
av_always_inline std::string av_err2string(int errnum) {
    char str[AV_ERROR_MAX_STRING_SIZE];
    return av_make_error_string(str, AV_ERROR_MAX_STRING_SIZE, errnum);
}
#define av_err2str(err) av_err2string(err).c_str()
#endif

#endif /* helpers_hpp */
