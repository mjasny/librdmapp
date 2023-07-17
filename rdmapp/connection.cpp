#include "connection.hpp"

#include "my_asserts.hpp"


namespace rdma {


void Connection::write(void* local_addr, uint32_t size, uintptr_t remote_offset, Flags flags, size_t mr_id) {
    if (flags.is_signaled()) {
        ++outstanding_signaled_wr;
    }

    auto mr = mrs.at(mr_id);
    uintptr_t remote_addr = reinterpret_cast<uintptr_t>(init_msg.meminfo.addr) + remote_offset;
    uint32_t rkey = init_msg.meminfo.rkey;
    wr::post_write(local_addr, size, qp, mr->lkey, flags, rkey, remote_addr);
}


void Connection::read(void* local_addr, uint32_t size, uintptr_t remote_offset, Flags flags, size_t mr_id) {
    if (flags.is_signaled()) {
        ++outstanding_signaled_wr;
    }

    auto mr = mrs.at(mr_id);
    uintptr_t remote_addr = reinterpret_cast<uintptr_t>(init_msg.meminfo.addr) + remote_offset;
    uint32_t rkey = init_msg.meminfo.rkey;
    wr::post_read(local_addr, size, qp, mr->lkey, flags, rkey, remote_addr);
}


void Connection::cmp_swap(uint64_t* local_addr, uint64_t expected, uint64_t desired, uintptr_t remote_offset, Flags flags, size_t mr_id) {
    if (flags.is_signaled()) {
        ++outstanding_signaled_wr;
    }

    auto mr = mrs.at(mr_id);
    uintptr_t remote_addr = reinterpret_cast<uintptr_t>(init_msg.meminfo.addr) + remote_offset;
    uint32_t rkey = init_msg.meminfo.rkey;
    wr::post_cmp_swap(local_addr, qp, mr->lkey, flags, rkey, expected, desired, remote_addr);
}


void Connection::fetch_add(uint64_t* local_addr, uint64_t value, uintptr_t remote_offset, Flags flags, size_t mr_id) {
    if (flags.is_signaled()) {
        ++outstanding_signaled_wr;
    }

    auto mr = mrs.at(mr_id);
    uintptr_t remote_addr = reinterpret_cast<uintptr_t>(init_msg.meminfo.addr) + remote_offset;
    uint32_t rkey = init_msg.meminfo.rkey;
    wr::post_fetch_add(local_addr, qp, mr->lkey, flags, rkey, value, remote_addr);
}

void Connection::sync_signaled(int num) {
    ensure(num <= outstanding_signaled_wr, "Cannot poll for more than is sent out with signaled");
    if (num == 0) {
        num = outstanding_signaled_wr;
    }
    ibv_wc wc[num];
    int left = num;

    do {
        int nc = ibv_poll_cq(cq, left, wc);
        check_ret(nc);
        for (int i = 0; i < nc; ++i) {
            ensure(wc[i].status == IBV_WC_SUCCESS, [&] {
                return ibv_wc_status_str(wc[i].status);
            });
        }
        left -= nc;
    } while (left > 0);

    outstanding_signaled_wr -= num;
}


} // namespace rdma
