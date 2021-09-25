/*
* File: ring_buffer.cpp
*
* Author: Rim Zaydullin
* Repo: https://github.com/tinybit/ffmpeg_code_examples
*
* simple ringbuffer implementation, non-threadsafe
*
*/

#include "ring_buffer.hpp"

RingBuffer::RingBuffer(size_t capacity) :
    m_head(0), m_tail(0), m_size(0), m_capacity(capacity)
{
    if (capacity >= RingBufferMaxSize) {
        std::ostringstream ss;
        ss << "requested creation of ring buffer with capacity ";
        ss << capacity << " which is larger than supported ";
        ss << RingBufferMaxSize;
        
        throw std::invalid_argument(ss.str());
    }

    try{
        m_buff = new char[capacity];
    }
    catch(std::bad_alloc&) {
        throw std::runtime_error("failed to allocate ring buffer, out of memory");
    }
}

RingBuffer::~RingBuffer() {
    delete []m_buff;
}

size_t RingBuffer::size() const {
    return m_size;
}

size_t RingBuffer::capacity() const {
    return m_capacity;
}

size_t RingBuffer::avail() const {
    return m_capacity - m_size;
}

const char* const RingBuffer::buf() const {
    return m_buff;
}

std::string RingBuffer::str() const {
    return std::string(m_buff, m_capacity);
}

size_t RingBuffer::write(const char *data, size_t sz) {
    if (sz == 0) {
        return 0;
    }
    
    size_t available_space = m_capacity - m_size;
    if (available_space == 0) {
        return RingBufferErrOutOfSpace;
    }
    
    // if we can't write all data, we will write whatever we can
    // and report written size
    sz = std::min(sz, available_space);
    
    if (sz <= m_capacity - m_tail) {
        memcpy(m_buff + m_tail, data, sz);
        m_tail += sz;

        if (m_tail == m_capacity) {
            m_tail = 0;
        }
    } else {
        size_t size_1 = m_capacity - m_tail;
        memcpy(m_buff + m_tail, data, size_1);

        size_t size_2 = sz - size_1;
        memcpy(m_buff, data + size_1, size_2);

        m_tail = size_2;
    }

    m_size += sz;
    return sz;
}

size_t RingBuffer::read(char *data, size_t sz) {
    if (sz == 0) {
        return 0;
    }
    
    // if we can't read all data, we will read whatever we can
    // and report written size
    sz = std::min(sz, m_size);

    if (sz <= m_capacity - m_head) {
        memcpy(data, m_buff + m_head, sz);
        m_head += sz;

        if (m_head == m_capacity) {
            m_head = 0;
        }
    } else {
        size_t size_1 = m_capacity - m_head;
        memcpy(data, m_buff + m_head, size_1);

        size_t size_2 = sz - size_1;
        memcpy(data + size_1, m_buff, size_2);

        m_head = size_2;
    }

    m_size -= sz;
    return sz;
}
