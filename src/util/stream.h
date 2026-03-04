#pragma once

#include <istream>
#include <ostream>
#include <type_traits>


template<class T> requires std::is_trivially_copyable_v<T>
std::istream& operator>>(std::istream& stream, T& v) {
    return stream.read(reinterpret_cast<char*>(&v), sizeof(T));
}

template<class T> requires std::is_trivially_copyable_v<T>
std::ostream& operator<<(std::ostream& stream, const T& v) {
    return stream.write(reinterpret_cast<const char*>(&v), sizeof(T));
}

class VectorStreamBuf : public std::streambuf {
public:
    explicit VectorStreamBuf(std::vector<u8>& out) : m_out{out} {}

protected:
    int_type overflow(int_type ch) override {
        if (traits_type::eq_int_type(ch, traits_type::eof())) {
            return traits_type::not_eof(ch);
        }

        const u8 b = static_cast<u8>(traits_type::to_char_type(ch));
        m_out.push_back(b);
        return traits_type::not_eof(ch);
    }

    std::streamsize xsputn(const char_type* ptr, std::streamsize count) override {
        if (count <= 0) {
            return 0;
        }

        m_out.append_range(std::span{ ptr, static_cast<std::size_t>(count) });
        return count;
    }

private:
    std::vector<u8>& m_out;
};

class VectorOStream : public std::ostream {
public:
    explicit VectorOStream(std::vector<u8>& out)
        : std::ostream{ nullptr }, m_buf{ out } {
        rdbuf(&m_buf);
    }

private:
    VectorStreamBuf m_buf;
};
