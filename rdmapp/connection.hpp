#pragma once

#include "init_msg.hpp"
#include "utils.hpp"
#include "work_request.hpp"

#include <rdma/rdma_cma.h>
#include <string>
#include <vector>


namespace rdma {


class Connection { // TODO Consider rename ConnectionCtx
    friend class RDMA;

    struct rdma_cm_id* id;
    struct ibv_cq* send_cq;
    struct ibv_cq* recv_cq;

public: // TODO fix
    struct ibv_qp* qp;

    Connection(std::vector<struct ibv_mr*>& mrs) : mrs(mrs) {}

private:
    struct std::vector<struct ibv_mr*>& mrs;


    int outstanding_signaled_wr = 0;
    int outstanding_receive_wr = 0;

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
    void write(void* local_addr, uint32_t size, uintptr_t remote_offset, Flags flags = Flags(), size_t mr_id = 0);

    void read(void* local_addr, uint32_t size, uintptr_t remote_offset, Flags flags = Flags(), size_t mr_id = 0);

    // Fetch and add operation - existing (prior the operation) server-side value is written into local_addr
    void fetch_add(uint64_t* local_addr, uint64_t value, uintptr_t remote_offset, Flags flags = Flags(), size_t mr_id = 0);

    // Compare and swap operation - existing(prior the operation)  server-side value is written into local_addr
    void cmp_swap(uint64_t* local_addr, uint64_t expected, uint64_t desired, uintptr_t remote_offset, Flags flags = Flags(), size_t mr_id = 0);


    void send(void* local_addr, uint32_t size, Flags flags = Flags(), size_t mr_id = 0);
    void recv(void* local_addr, uint32_t size, size_t mr_id = 0);

    // Synchronizes num prior signaled operations - num=0 --> poll for all prior signaled wr
    void sync_signaled(int num = 0);
    void sync_recv(int num = 0);
    int try_sync_recv(int num = 0);
};


// TODO ConnectionGuard ?


} // namespace rdma
