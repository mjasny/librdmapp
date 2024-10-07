#include "connection.hpp"

#include "my_asserts.hpp"


namespace rdma {

Connection::Connection(std::vector<struct ibv_mr*>& mrs) {
    entries.reserve(1024);
    for (auto* mr : mrs) {
        lkeys.push_back(mr->lkey);
    }
    local_mr(0);
}

void Connection::add_local_mr(uint32_t lkey) {
    lkeys.push_back(lkey);
}

void Connection::add_remote_mr(MemInfo meminfo) {
    r_mrs.push_back({
        .base_addr = reinterpret_cast<uintptr_t>(meminfo.addr),
        .rkey = meminfo.rkey,
    });
    if (r_mrs.size() == 1) {
        remote_mr(0);
    }
}


void Connection::local_mr(size_t mr_id) {
    lkey = lkeys.at(mr_id);
}
void Connection::remote_mr(size_t mr_id) {
    auto& mr = r_mrs.at(mr_id);
    base_addr = mr.base_addr;
    rkey = mr.rkey;
}
void Connection::write(void* local_addr, uint32_t size, uintptr_t remote_offset, Flags flags) {
    uintptr_t remote_addr = base_addr + remote_offset;
    wr::post_write(local_addr, size, qp, lkey, flags, rkey, remote_addr);
}


void Connection::read(void* local_addr, uint32_t size, uintptr_t remote_offset, Flags flags) {
    uintptr_t remote_addr = base_addr + remote_offset;
    wr::post_read(local_addr, size, qp, lkey, flags, rkey, remote_addr);
}

void Connection::prep_write(void* local_addr, uint32_t size, uintptr_t remote_offset, Flags flags) {
    uintptr_t remote_addr = base_addr + remote_offset;

    auto& entry = entries.emplace_back();
    entry.sge.addr = reinterpret_cast<uint64_t>(local_addr);
    entry.sge.length = size;
    entry.sge.lkey = lkey;

    entry.wr.opcode = IBV_WR_RDMA_WRITE;
    entry.wr.send_flags = flags;
    entry.wr.sg_list = &entry.sge;
    entry.wr.num_sge = 1;
    entry.wr.wr.rdma.rkey = rkey;
    entry.wr.wr.rdma.remote_addr = remote_addr;
    entry.wr.next = nullptr;
    if (entries.size() > 1) {
        entries[entries.size() - 2].wr.next = &entry.wr;
    }
}

void Connection::prep_read(void* local_addr, uint32_t size, uintptr_t remote_offset, Flags flags) {
    uintptr_t remote_addr = base_addr + remote_offset;

    auto& entry = entries.emplace_back();
    entry.sge.addr = reinterpret_cast<uint64_t>(local_addr);
    entry.sge.length = size;
    entry.sge.lkey = lkey;

    entry.wr.opcode = IBV_WR_RDMA_READ;
    entry.wr.send_flags = flags;
    entry.wr.sg_list = &entry.sge;
    entry.wr.num_sge = 1;
    entry.wr.wr.rdma.rkey = rkey;
    entry.wr.wr.rdma.remote_addr = remote_addr;
    entry.wr.next = nullptr;
    if (entries.size() > 1) {
        entries[entries.size() - 2].wr.next = &entry.wr;
    }
}

void Connection::flush_wrs() {
    ensure(entries.size() > 0);
    struct ibv_send_wr* bad_wr;
    check_zero(ibv_post_send(qp, &entries[0].wr, &bad_wr));
    entries.clear();
}


void Connection::cmp_swap(uint64_t* local_addr, uint64_t expected, uint64_t desired, uintptr_t remote_offset, Flags flags) {
    uintptr_t remote_addr = base_addr + remote_offset;
    wr::post_cmp_swap(local_addr, qp, lkey, flags, rkey, expected, desired, remote_addr);
}


void Connection::fetch_add(uint64_t* local_addr, uint64_t value, uintptr_t remote_offset, Flags flags) {
    uintptr_t remote_addr = base_addr + remote_offset;
    wr::post_fetch_add(local_addr, qp, lkey, flags, rkey, value, remote_addr);
}


void Connection::send(void* local_addr, uint32_t size, Flags flags) {
    wr::post_send(local_addr, size, qp, lkey, flags);
}

void Connection::recv(void* local_addr, uint32_t size) {
    wr::post_receive(local_addr, size, qp, lkey);
}


void Connection::sync_signaled(int num) {
    ibv_wc wc[num];
    int left = num;

    do {
        int nc = ibv_poll_cq(send_cq, left, wc);
        check_ret(nc);
        for (int i = 0; i < nc; ++i) {
            ensure(wc[i].status == IBV_WC_SUCCESS, [&] {
                return ibv_wc_status_str(wc[i].status);
            });
        }
        left -= nc;
    } while (left > 0);
}


int Connection::try_sync_signaled(int num) {
    ibv_wc wc[num];
    int left = num;

    int nc = ibv_poll_cq(send_cq, left, wc);
    check_ret(nc);
    for (int i = 0; i < nc; ++i) {
        ensure(wc[i].status == IBV_WC_SUCCESS, [&] {
            return ibv_wc_status_str(wc[i].status);
        });
    }

    return nc;
}

void Connection::sync_recv(int num) {
    ibv_wc wc[num];
    int left = num;

    do {
        int nc = ibv_poll_cq(recv_cq, left, wc);
        check_ret(nc);
        for (int i = 0; i < nc; ++i) {
            ensure(wc[i].status == IBV_WC_SUCCESS, [&] {
                return ibv_wc_status_str(wc[i].status);
            });
        }
        left -= nc;
    } while (left > 0);
}


int Connection::try_sync_recv(int num) {
    ibv_wc wc[num];

    int nc = ibv_poll_cq(recv_cq, num, wc);
    check_ret(nc);
    for (int i = 0; i < nc; ++i) {
        ensure(wc[i].status == IBV_WC_SUCCESS, [&] {
            return ibv_wc_status_str(wc[i].status);
        });
    }

    return nc;
}


} // namespace rdma
