#pragma once


#include "nostd.hpp"

#include <cstring>
#include <experimental/source_location>
#include <sstream>


namespace rdma {

template <typename T>
inline void check_ptr(T ptr, const std::experimental::source_location& location = std::experimental::source_location::current()) {
    static_assert(std::is_pointer_v<T> || nostd::is_smart_ptr_v<T>);
    if (ptr == nullptr) [[unlikely]] {
        std::stringstream err;
        err << location.file_name() << ":" << location.line() << " got error: " << std::strerror(errno) << '\n';
        throw std::runtime_error(err.str());
    }
}


template <typename T>
inline void check_ret(T ret, const std::experimental::source_location& location = std::experimental::source_location::current()) {
    if (ret < 0) [[unlikely]] {
        std::stringstream err;
        err << location.file_name() << ":" << location.line() << " got error: " << std::strerror(errno) << '\n';
        throw std::runtime_error(err.str());
    }
}


inline void ensure(bool value, const std::experimental::source_location& location = std::experimental::source_location::current()) {
    if (!value) [[unlikely]] {
        std::stringstream err;
        err << location.file_name() << ":" << location.line() << " does not evaluate to true\n";
        throw std::runtime_error(err.str());
    }
}

inline void ensure(bool value, const char* err_msg, const std::experimental::source_location& location = std::experimental::source_location::current()) {
    if (!value) [[unlikely]] {
        std::stringstream err;
        err << location.file_name() << ":" << location.line() << " failed: " << err_msg << "\n";
        throw std::runtime_error(err.str());
    }
}

template <typename T>
inline void ensure(bool value, T&& err_msg, const std::experimental::source_location& location = std::experimental::source_location::current()) {
    if (!value) [[unlikely]] {
        std::stringstream err;
        err << location.file_name() << ":" << location.line() << " failed: " << err_msg() << "\n";
        throw std::runtime_error(err.str());
    }
}

} // namespace rdma
