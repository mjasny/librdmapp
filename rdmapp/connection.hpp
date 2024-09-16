#pragma once

#include "init_msg.hpp"
#include "static_vector.hpp"
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

    Connection(std::vector<struct ibv_mr*>& mrs);

    void add_remote_mr(MemInfo meminfo);

    void local_mr(size_t mr_id);
    void remote_mr(size_t mr_id);

private:
    // preselected
    uint32_t lkey;
    uint32_t rkey;
    uintptr_t base_addr;

    static constexpr size_t MAX_MR = 2; // Maximal Memory Regions

    struct RemoteMR {
        uintptr_t base_addr;
        uint32_t rkey;
    };

    utils::static_vector<uint32_t, MAX_MR> lkeys;

public:
    utils::static_vector<RemoteMR, MAX_MR> r_mrs;


public:
    enum class State {
        INVALID,
        OPEN,
        CLOSED,
    };

    enum class Type {
        INVALID,
        INCOMING,
        OUTGOING,
    };

    State state = State::INVALID;
    Type type = Type::INVALID;

    std::string remote_ip;
    InitMsg init_msg;

    struct Entry {
        struct ibv_sge sge;
        struct ibv_send_wr wr;
    };
    std::vector<Entry> entries;


public:
    void write(void* local_addr, uint32_t size, uintptr_t remote_offset, Flags flags);

    void read(void* local_addr, uint32_t size, uintptr_t remote_offset, Flags flags);

    void prep_write(void* local_addr, uint32_t size, uintptr_t remote_offset, Flags flags);
    void prep_read(void* local_addr, uint32_t size, uintptr_t remote_offset, Flags flags);
    void flush_wrs();

    // Fetch and add operation - existing (prior the operation) server-side value is written into local_addr
    void fetch_add(uint64_t* local_addr, uint64_t value, uintptr_t remote_offset, Flags flags);

    // Compare and swap operation - existing(prior the operation)  server-side value is written into local_addr
    void cmp_swap(uint64_t* local_addr, uint64_t expected, uint64_t desired, uintptr_t remote_offset, Flags flags);


    void send(void* local_addr, uint32_t size, Flags flags);
    void recv(void* local_addr, uint32_t size);

    // Synchronizes num prior signaled operations - num=0 --> poll for all prior signaled wr
    void sync_signaled(int num);
    int try_sync_signaled(int num);
    void sync_recv(int num);
    int try_sync_recv(int num);
};


// TODO ConnectionGuard ?


} // namespace rdma
