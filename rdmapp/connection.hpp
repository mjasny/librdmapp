#pragma once

#include "init_msg.hpp"
#include "utils.hpp"

#include <rdma/rdma_cma.h>
#include <string>
#include <vector>


namespace rdma {


class Connection { // TODO Consider rename ConnectionCtx
    friend class RDMA;

    struct rdma_cm_id* id;
    struct ibv_cq* cq;

public: // TODO fix
    struct ibv_qp* qp;

    Connection(std::vector<struct ibv_mr*>& mrs) : mrs(mrs) {}

private:
    struct std::vector<struct ibv_mr*>& mrs;


    size_t outstanding_wr = 0;

public:
    size_t max_wr = 128;

    enum State {
        OPEN,
        CLOSED,
    };

    enum Type {
        INCOMING,
        OUTGOING,
    };

    State state;
    Type type;

    std::string remote_ip;
    InitMsg init_msg;

public:
    void write(void* local_addr, uint32_t size, uintptr_t remote_offset, size_t mr_id = 0);
    void write_sync(void* local_addr, uint32_t size, uintptr_t remote_offset, size_t mr_id = 0);
    void write_inlined(void* local_addr, uint32_t size, uintptr_t remote_offset, size_t mr_id = 0);

    void read(void* local_addr, uint32_t size, uintptr_t remote_offset, size_t mr_id = 0);
    void read_sync(void* local_addr, uint32_t size, uintptr_t remote_offset, size_t mr_id = 0);

    void fetch_add(uint64_t* local_addr, uint64_t value, uintptr_t remote_offset, size_t mr_id = 0);
    void fetch_add_sync(uint64_t* local_addr, uint64_t value, uintptr_t remote_offset, size_t mr_id = 0);

    void cmp_swap(uint64_t* local_addr, uint64_t expected, uint64_t desired, uintptr_t remote_offset, size_t mr_id = 0);
    void cmp_swap_sync(uint64_t* local_addr, uint64_t expected, uint64_t desired, uintptr_t remote_offset, size_t mr_id = 0);

    void poll_cq(int num);
};


// TODO ConnectionGuard


//  rdma_destroy_qp(id);
//  check_ret(ibv_destroy_cq(cq));
//  check_ret(rdma_destroy_id(id));

//  usleep(static_cast<__useconds_t>(random() % 100'000)); // "prevent" Switch CPU port incast


} // namespace rdma
