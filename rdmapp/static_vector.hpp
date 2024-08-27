#pragma once

#include <cstddef>
#include <stdexcept>

template <typename T, size_t N>
struct static_vector {
    static_vector() : size_(0) {}

    void push_back(const T& value) {
        if (size_ < N) {
            data_[size_++] = value;
        } else {
            throw std::out_of_range("StackVector overflow");
        }
    }

    T& operator[](std::size_t index) {
        return data_[index];
    }

    const T& operator[](std::size_t index) const {
        return data_[index];
    }

    // Non-const access, bounds-checked
    T& at(std::size_t index) {
        if (index >= size_) {
            throw std::out_of_range("StackVector: index out of range");
        }
        return data_[index];
    }

    // Const access, bounds-checked
    const T& at(std::size_t index) const {
        if (index >= size_) {
            throw std::out_of_range("StackVector: index out of range");
        }
        return data_[index];
    }

    std::size_t size() const { return size_; }
    std::size_t capacity() const { return N; }

private:
    size_t size_ = 0;
    T data_[N];
};