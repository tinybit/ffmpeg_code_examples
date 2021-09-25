/*
* File: ring_buffer.hpp
*
* Author: Rim Zaydullin
* Repo: https://github.com/tinybit/ffmpeg_code_examples
*
* simple ringbuffer implementation, non-threadsafe
*
*/

#ifndef ring_buffer_h
#define ring_buffer_h

#include <cstddef>
#include <algorithm>
#include <limits>
#include <sstream>
#include <cstring>

const size_t RingBufferMaxSize = std::numeric_limits<size_t>::max() - 128;
const size_t RingBufferErrOutOfSpace = std::numeric_limits<size_t>::max() - 127;

class RingBuffer {
public:
    RingBuffer(size_t capacity);
    ~RingBuffer();

    size_t capacity() const;        // return ring buffer capacity
    size_t size() const;            // return ring buffer size
    size_t avail() const;           // return available free bytes size
    size_t write(const char *data, size_t sz);   // return number of bytes written.
    size_t read(char *data, size_t sz);          // return number of bytes read.
    const char* const buf() const;  // return internal data buffer
    std::string str() const;        // return internal data buffer as string

private:
    size_t m_head;
    size_t m_tail;
    size_t m_size;
    size_t m_capacity;
    char*  m_buff;
};

#endif /* ring_buffer_h */
