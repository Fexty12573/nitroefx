#pragma once

#include <types.h>

#include <algorithm>
#include <cstring>
#include <type_traits>
#include <span>
#include <stdexcept>


class DataReader {
public:
    DataReader(std::span<const u8> data) : m_data{data} {}

    size_t getPtr() const { return m_ptr; }
    void setPtr(size_t ptr) {
        if (ptr > m_data.size()) {
            ptr = m_data.size();
        }

        m_ptr = ptr;

        if (m_ptr > m_data.size()) {
            m_ptr = m_data.size();
        }
    }
    void offsetPtr(s64 offset) {
        if (offset < 0) {
            offset = std::clamp<s64>(offset, -static_cast<s64>(m_ptr), 0);
        }

        m_ptr += offset;

        if (m_ptr > m_data.size()) {
            m_ptr = m_data.size();
        }
    }
    void seekBeg() { m_ptr = 0; }

    template<typename T>
    T read() requires std::is_trivially_copyable_v<T> {
        if (m_ptr + sizeof(T) > m_data.size()) {
            throw std::runtime_error("Read out of bounds");
        }

        T t;
        std::memcpy(&t, m_data.data() + m_ptr, sizeof(T));
        m_ptr += sizeof(T);

        return t;
    }

    template<typename It>
    void readN(It dest, size_t count) {
        if (m_ptr + count > m_data.size()) {
            throw std::runtime_error("Read out of bounds");
        }

        std::copy_n(m_data.begin() + m_ptr, count, dest);
    }

    bool eof() const { return m_ptr >= m_data.size(); }

private:
    std::span<const u8> m_data;
    size_t m_ptr = 0;
};
