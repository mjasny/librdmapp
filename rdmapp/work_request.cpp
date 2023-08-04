#include "work_request.hpp"

#include "my_asserts.hpp"
#include "utils.hpp"


namespace rdma {
namespace wr {


void post_write(void* memAddr, uint32_t size, ibv_qp* qp, uint32_t lkey, Flags flags, uint32_t rkey, uintptr_t remote_offset) {
    struct ibv_sge send_sgl;
    send_sgl.addr = reinterpret_cast<uint64_t>(memAddr);
    send_sgl.length = size;
    send_sgl.lkey = lkey;

    struct ibv_send_wr sq_wr;
    sq_wr.opcode = IBV_WR_RDMA_WRITE;
    sq_wr.send_flags = flags;
    // sq_wr.send_flags |= IBV_SEND_FENCE;
    // #ifdef USE_INLINE
    //     sq_wr.send_flags |= (size <= INLINE_SIZE) ? IBV_SEND_INLINE : 0;
    // #endif
    sq_wr.sg_list = &send_sgl;
    sq_wr.num_sge = 1;
    sq_wr.wr.rdma.rkey = rkey;
    sq_wr.wr.rdma.remote_addr = remote_offset;
    // sq_wr.wr_id = wcId;  ???
    sq_wr.next = nullptr; // do not forget to set this otherwise it crashes

    struct ibv_send_wr* bad_wr;
    check_ret(ibv_post_send(qp, &sq_wr, &bad_wr));
}


void post_read(void* memAddr, uint32_t size, ibv_qp* qp, uint32_t lkey, Flags flags, uint32_t rkey, uintptr_t remote_offset) {
    struct ibv_sge send_sgl;
    send_sgl.addr = reinterpret_cast<uint64_t>(memAddr);
    send_sgl.length = size;
    send_sgl.lkey = lkey;

    struct ibv_send_wr sq_wr;
    sq_wr.opcode = IBV_WR_RDMA_READ;
    sq_wr.send_flags = flags;
    // sq_wr.send_flags |= IBV_SEND_FENCE;
    sq_wr.sg_list = &send_sgl;
    sq_wr.num_sge = 1;
    sq_wr.wr.rdma.rkey = rkey;
    sq_wr.wr.rdma.remote_addr = remote_offset;
    // sq_wr.wr_id = wcId;
    sq_wr.next = nullptr; // do not forget to set this otherwise it crashes

    struct ibv_send_wr* bad_wr;
    check_ret(ibv_post_send(qp, &sq_wr, &bad_wr));
}

void post_cmp_swap(void* memAddr, ibv_qp* qp, uint32_t lkey, Flags flags, uint32_t rkey, uint64_t expected, uint64_t desired, uintptr_t remote_offset) {
    struct ibv_sge send_sgl;
    send_sgl.addr = reinterpret_cast<uintptr_t>(memAddr);
    send_sgl.length = sizeof(uint64_t);
    send_sgl.lkey = lkey;

    struct ibv_send_wr sq_wr;
    sq_wr.opcode = IBV_WR_ATOMIC_CMP_AND_SWP;
    sq_wr.send_flags = flags;
    // sq_wr.send_flags |= IBV_SEND_FENCE;
    sq_wr.sg_list = &send_sgl;
    sq_wr.num_sge = 1;
    sq_wr.wr.atomic.remote_addr = remote_offset;
    sq_wr.wr.atomic.rkey = rkey;
    sq_wr.wr.atomic.compare_add = expected;
    sq_wr.wr.atomic.swap = desired;
    sq_wr.next = nullptr; // do not forget to set this otherwise it crashes

    struct ibv_send_wr* bad_wr; // TODO error-message for check_ret?
    check_ret(ibv_post_send(qp, &sq_wr, &bad_wr));
}


void post_fetch_add(void* memAddr, ibv_qp* qp, uint32_t lkey, Flags flags, uint32_t rkey, uint64_t value, uintptr_t remote_offset) {
    struct ibv_sge send_sgl;
    send_sgl.addr = reinterpret_cast<uintptr_t>(memAddr);
    send_sgl.length = sizeof(uint64_t);
    send_sgl.lkey = lkey;

    struct ibv_send_wr sq_wr;
    sq_wr.opcode = IBV_WR_ATOMIC_FETCH_AND_ADD;
    sq_wr.send_flags = flags;
    sq_wr.sg_list = &send_sgl;
    sq_wr.num_sge = 1;
    sq_wr.wr.atomic.remote_addr = remote_offset;
    sq_wr.wr.atomic.rkey = rkey;
    sq_wr.wr.atomic.compare_add = value;
    sq_wr.wr.atomic.swap = 0;
    sq_wr.next = nullptr; // do not forget to set this otherwise it crashes

    struct ibv_send_wr* bad_wr; // TODO error-message for check_ret?
    check_ret(ibv_post_send(qp, &sq_wr, &bad_wr));
}


void post_send(void* memAddr, uint32_t size, ibv_qp* qp, uint32_t lkey, Flags flags) {
    struct ibv_sge send_sgl;
    send_sgl.addr = reinterpret_cast<uintptr_t>(memAddr);
    send_sgl.length = size;
    send_sgl.lkey = lkey;

    struct ibv_send_wr sq_wr;
    sq_wr.opcode = IBV_WR_SEND;
    sq_wr.send_flags = flags;
    // sq_wr.send_flags |= IBV_SEND_FENCE;
    sq_wr.sg_list = &send_sgl;
    sq_wr.num_sge = 1;
    sq_wr.next = nullptr; // do not forget to set this otherwise it crashes

    struct ibv_send_wr* bad_wr; // TODO error-message for check_ret?
    check_ret(ibv_post_send(qp, &sq_wr, &bad_wr));
}


void post_receive(void* memAddr, uint32_t size, ibv_qp* qp, uint32_t lkey) {
    struct ibv_sge recv_sgl;
    recv_sgl.addr = reinterpret_cast<uintptr_t>(memAddr);
    recv_sgl.length = size;
    recv_sgl.lkey = lkey;

    struct ibv_recv_wr rq_wr;
    rq_wr.sg_list = &recv_sgl;
    rq_wr.num_sge = 1;
    rq_wr.next = nullptr;

    struct ibv_recv_wr* bad_wr; // TODO error-message for check_ret?
    check_ret(ibv_post_recv(qp, &rq_wr, &bad_wr));
}


} // namespace wr
} // namespace rdma
