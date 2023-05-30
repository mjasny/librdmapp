#pragma once


#include <cstddef>
#include <cstdint>
#include <infiniband/verbs.h>

namespace rdma {
namespace wr {


enum Flags : uint32_t {
    signaled = IBV_SEND_SIGNALED,
    inlined = IBV_SEND_INLINE,
    unsignaled = 0,
};

inline Flags operator|(Flags a, Flags b) {
    return static_cast<Flags>(static_cast<int>(a) | static_cast<int>(b));
}

void post_write(void* memAddr, uint32_t size, ibv_qp* qp, uint32_t lkey, Flags flags, uint32_t rkey, uintptr_t remote_offset);

void post_read(void* memAddr, uint32_t size, ibv_qp* qp, uint32_t lkey, Flags flags, uint32_t rkey, uintptr_t remote_offset);

void post_cmp_swap(void* memAddr, ibv_qp* qp, uint32_t lkey, Flags flags, uint32_t rkey, uint64_t expected, uint64_t desired, uintptr_t remote_offset);

void post_fetch_add(void* memAddr, ibv_qp* qp, uint32_t lkey, Flags flags, uint32_t rkey, uint64_t value, uintptr_t remote_offset);

} // namespace wr
} // namespace rdma
