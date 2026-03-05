#pragma once

#include <types.h>

#include <algorithm>
#include <array>
#include <bit>
#include <span>
#include <type_traits>
#include <vector>


class DataWriter {
public:
    DataWriter() = default;
    DataWriter(std::span<const u8> initialData)
        : m_data{initialData.begin(), initialData.end()}
        , m_ptr{ initialData.size() }
    {
    }

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
    void seekEnd() { m_ptr = m_data.size(); }

    template<typename T>
    void write(const T& t) requires std::is_trivially_copyable_v<T> {
        const auto bytes = std::bit_cast<std::array<u8, sizeof(T)>>(t);
        writeBytes(bytes);
    }

    void writeBytes(std::span<const u8> bytes) {
        if (m_ptr >= m_data.size()) {
            // Option A: Append to end
            std::ranges::copy(bytes, std::back_inserter(m_data));
        } else if (m_ptr + bytes.size() <= m_data.size()) {
            // Option B: Overwrite existing
            std::ranges::copy(bytes, m_data.begin() + m_ptr);
        } else {
            // Option C: Overwrite existing, but new data goes beyond current capacity
            m_data.resize(m_ptr + bytes.size());
            std::ranges::copy(bytes, m_data.begin() + m_ptr);
        }

        m_ptr += bytes.size();

        // Should never happen
        assert(m_ptr <= m_data.size());
    }

    std::span<const u8> getData() const { return m_data; }
    size_t size() const { return m_data.size(); }

    std::vector<u8> finalize() && { return std::move(m_data); }

private:
    std::vector<u8> m_data;
    size_t m_ptr = 0;
};