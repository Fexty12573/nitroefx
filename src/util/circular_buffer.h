#pragma once

#include <cstddef>
#include <type_traits>
#include <vector>

template<typename T, std::size_t N>
class CircularBuffer {
public:
    using container_type = std::vector<T>;
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = container_type::difference_type;
    using reference = container_type::reference;
    using const_reference = container_type::const_reference;
    using pointer = container_type::pointer;
    using const_pointer = container_type::const_pointer;
    
    CircularBuffer() = default;
    CircularBuffer(std::initializer_list<value_type> values) : m_data{ std::move(values) } {}
    explicit CircularBuffer(size_type count, const_reference value = value_type{}) : m_data(count, value) {}
    CircularBuffer(const CircularBuffer&) = default;
    CircularBuffer(CircularBuffer&&) = default;
    CircularBuffer& operator=(const CircularBuffer&) = default;
    CircularBuffer& operator=(CircularBuffer&&) = default;

    void push(const_reference v) {
        if (size() < capacity()) {
            m_data.push_back(v);
        } else {
            m_data[m_offset] = v;
            m_offset = (m_offset + 1) % capacity();
        }
    }

    template<typename X>
    void push(X&& v) {
        if (size() < capacity()) {
            m_data.emplace_back(std::forward<X>(v));
        }
        else {
            m_data[m_offset] = v;
            m_offset = (m_offset + 1) % capacity();
        }
    }

    size_type size() const {
        return m_data.size();
    }

    static constexpr size_type capacity() {
        return N;
    }

    const_pointer data() const {
        return m_data.data();
    }

    pointer data() {
        return m_data.data();
    }

    size_type offset() const {
        return m_offset;
    }

private:
    std::vector<value_type> m_data;
    size_type m_offset = 0;
};
