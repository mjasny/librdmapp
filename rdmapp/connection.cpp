#include "connection.hpp"

#include "my_asserts.hpp"
#include "work_request.hpp"


namespace rdma {


void Connection::write(void* local_addr, uint32_t size, uintptr_t remote_offset, size_t mr_id) {
    auto mr = mrs.at(mr_id);
    if (outstanding_wr == max_wr) {
        poll_cq(1);
        outstanding_wr = 0;
    }

    uintptr_t remote_addr = reinterpret_cast<uintptr_t>(init_msg.meminfo.addr) + remote_offset;
    uint32_t rkey = init_msg.meminfo.rkey;
    wr::Flags flags = outstanding_wr == (max_wr - 1) ? wr::Flags::signaled : wr::Flags::unsignaled;
    wr::post_write(local_addr, size, qp, mr->lkey, flags, rkey, remote_addr);

    ++outstanding_wr;
}


void Connection::write_sync(void* local_addr, uint32_t size, uintptr_t remote_offset, size_t mr_id) {
    auto mr = mrs.at(mr_id);
    if (outstanding_wr == max_wr) {
        poll_cq(1);
        outstanding_wr = 0;
    }

    uintptr_t remote_addr = reinterpret_cast<uintptr_t>(init_msg.meminfo.addr) + remote_offset;
    uint32_t rkey = init_msg.meminfo.rkey;
    wr::post_write(local_addr, size, qp, mr->lkey, wr::Flags::signaled, rkey, remote_addr);

    poll_cq(1);
    outstanding_wr = 0;
}

void Connection::write_inlined(void* local_addr, uint32_t size, uintptr_t remote_offset, size_t mr_id) {
    auto mr = mrs.at(mr_id);
    // TODO init_attr.cap.max_inline_data, add check for that
    if (outstanding_wr == max_wr) {
        poll_cq(1);
        outstanding_wr = 0;
    }

    uintptr_t remote_addr = reinterpret_cast<uintptr_t>(init_msg.meminfo.addr) + remote_offset;
    uint32_t rkey = init_msg.meminfo.rkey;
    wr::Flags flags = outstanding_wr == (max_wr - 1) ? wr::Flags::signaled : wr::Flags::unsignaled;
    wr::post_write(local_addr, size, qp, mr->lkey, flags | wr::Flags::inlined, rkey, remote_addr);

    ++outstanding_wr;
}

void Connection::read(void* local_addr, uint32_t size, uintptr_t remote_offset, size_t mr_id) {
    auto mr = mrs.at(mr_id);
    if (outstanding_wr == max_wr) {
        poll_cq(1);
        outstanding_wr = 0;
    }

    uintptr_t remote_addr = reinterpret_cast<uintptr_t>(init_msg.meminfo.addr) + remote_offset;
    uint32_t rkey = init_msg.meminfo.rkey;
    wr::Flags flags = outstanding_wr == (max_wr - 1) ? wr::Flags::signaled : wr::Flags::unsignaled;
    wr::post_read(local_addr, size, qp, mr->lkey, flags, rkey, remote_addr);

    ++outstanding_wr;
}


void Connection::read_sync(void* local_addr, uint32_t size, uintptr_t remote_offset, size_t mr_id) {
    auto mr = mrs.at(mr_id);
    if (outstanding_wr == max_wr) {
        poll_cq(1);
        outstanding_wr = 0;
    }

    uintptr_t remote_addr = reinterpret_cast<uintptr_t>(init_msg.meminfo.addr) + remote_offset;
    uint32_t rkey = init_msg.meminfo.rkey;
    wr::post_read(local_addr, size, qp, mr->lkey, wr::Flags::signaled, rkey, remote_addr);

    poll_cq(1);
    outstanding_wr = 0;
}


void Connection::cmp_swap(uint64_t* local_addr, uint64_t expected, uint64_t desired, uintptr_t remote_offset, size_t mr_id) {
    auto mr = mrs.at(mr_id);
    if (outstanding_wr == max_wr) {
        poll_cq(1);
        outstanding_wr = 0;
    }

    uintptr_t remote_addr = reinterpret_cast<uintptr_t>(init_msg.meminfo.addr) + remote_offset;
    uint32_t rkey = init_msg.meminfo.rkey;
    wr::Flags flags = outstanding_wr == (max_wr - 1) ? wr::Flags::signaled : wr::Flags::unsignaled;
    wr::post_cmp_swap(local_addr, qp, mr->lkey, flags, rkey, expected, desired, remote_addr);

    ++outstanding_wr;
}


void Connection::cmp_swap_sync(uint64_t* local_addr, uint64_t expected, uint64_t desired, uintptr_t remote_offset, size_t mr_id) {
    auto mr = mrs.at(mr_id);
    if (outstanding_wr == max_wr) {
        poll_cq(1);
        outstanding_wr = 0;
    }

    uintptr_t remote_addr = reinterpret_cast<uintptr_t>(init_msg.meminfo.addr) + remote_offset;
    uint32_t rkey = init_msg.meminfo.rkey;
    wr::post_cmp_swap(local_addr, qp, mr->lkey, wr::Flags::signaled, rkey, expected, desired, remote_addr);

    poll_cq(1);
    outstanding_wr = 0;
}


void Connection::fetch_add(uint64_t* local_addr, uint64_t value, uintptr_t remote_offset, size_t mr_id) {
    auto mr = mrs.at(mr_id);
    if (outstanding_wr == max_wr) {
        poll_cq(1);
        outstanding_wr = 0;
    }

    uintptr_t remote_addr = reinterpret_cast<uintptr_t>(init_msg.meminfo.addr) + remote_offset;
    uint32_t rkey = init_msg.meminfo.rkey;
    wr::Flags flags = outstanding_wr == (max_wr - 1) ? wr::Flags::signaled : wr::Flags::unsignaled;
    wr::post_fetch_add(local_addr, qp, mr->lkey, flags, rkey, value, remote_addr);

    ++outstanding_wr;
}

void Connection::fetch_add_sync(uint64_t* local_addr, uint64_t value, uintptr_t remote_offset, size_t mr_id) {
    auto mr = mrs.at(mr_id);
    if (outstanding_wr == max_wr) {
        poll_cq(1);
        outstanding_wr = 0;
    }

    uintptr_t remote_addr = reinterpret_cast<uintptr_t>(init_msg.meminfo.addr) + remote_offset;
    uint32_t rkey = init_msg.meminfo.rkey;
    wr::post_fetch_add(local_addr, qp, mr->lkey, wr::Flags::signaled, rkey, value, remote_addr);

    poll_cq(1);
    outstanding_wr = 0;
}


void Connection::poll_cq(int num) {
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
}


} // namespace rdma
