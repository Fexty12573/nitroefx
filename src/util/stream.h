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
