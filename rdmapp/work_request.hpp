#pragma once


#include <cstddef>
#include <cstdint>
#include <infiniband/verbs.h>

namespace rdma {

struct Flags {
    constexpr operator uint32_t() {
        return value;
    };

    // makes it possible to synchronize on the RDMA op
    constexpr Flags& signaled() {
        value |= IBV_SEND_SIGNALED;
        return *this;
    }

    constexpr Flags& inlined() {
        value |= IBV_SEND_INLINE;
        return *this;
    }

    constexpr Flags& fenced() {
        value |= IBV_SEND_FENCE;
        return *this;
    }

    constexpr bool is_signaled() {
        return static_cast<bool>(value & IBV_SEND_SIGNALED);
    }
    constexpr bool is_inlined() {
        return static_cast<bool>(value & IBV_SEND_INLINE);
    }
    constexpr bool is_fenced() {
        return static_cast<bool>(value & IBV_SEND_FENCE);
    }

private:
    uint32_t value = 0;
};


namespace wr {


void post_write(void* memAddr, uint32_t size, ibv_qp* qp, uint32_t lkey, Flags flags, uint32_t rkey, uintptr_t remote_offset);

void post_read(void* memAddr, uint32_t size, ibv_qp* qp, uint32_t lkey, Flags flags, uint32_t rkey, uintptr_t remote_offset);

void post_cmp_swap(void* memAddr, ibv_qp* qp, uint32_t lkey, Flags flags, uint32_t rkey, uint64_t expected, uint64_t desired, uintptr_t remote_offset);

void post_fetch_add(void* memAddr, ibv_qp* qp, uint32_t lkey, Flags flags, uint32_t rkey, uint64_t value, uintptr_t remote_offset);

} // namespace wr
} // namespace rdma
