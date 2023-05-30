#pragma once

#include <cstddef>
#include <cstdint>


namespace rdma {


struct MemInfo {
    void* addr;
    size_t length;
    uint32_t rkey;
    // 4 byte padding
};


struct InitMsg {
    static constexpr size_t MAX_SIZE = 56;
    static constexpr size_t USER_SIZE = MAX_SIZE - sizeof(MemInfo);

    MemInfo meminfo;
    uint8_t buffer[USER_SIZE]{};

    template <typename T>
    T* as() {
        static_assert(sizeof(T) <= USER_SIZE);
        return reinterpret_cast<T*>(buffer);
    }

    template <typename T>
    void set(const T& val) {
        *as<T>() = val;
    }

    operator void*() {
        return this;
    }
} __attribute__((packed));


} // namespace rdma
