#pragma once


#include "connection.hpp"
#include "event_handler.hpp"
#include "utils.hpp"

#include <cstddef>
#include <cstdint>
#include <infiniband/verbs.h>
#include <memory>
#include <mutex>
#include <rdma/rdma_cma.h>
#include <thread>
#include <unordered_map>
#include <vector>


namespace rdma {


class RDMA {
    static constexpr int TIMEOUT = 2000; // ms

    std::string local_ip;
    uint16_t local_port;

    struct ibv_context* context;
    struct rdma_event_channel* channel; // server listen has own channel
    struct ibv_pd* pd;
    std::vector<struct ibv_mr*> mrs;

    std::unordered_map<struct rdma_cm_id*, Connection*> connections;
    std::jthread server_thread;
    std::recursive_mutex mutex;
    std::mutex connect_to_mutex;

public:
    enum Flags {
        CLOSE_AFTER_LAST = 0b00000001,
        RUN_FOREVER = 0b00000000,
        IN_BACKGROUND = 0b00000010,
        CLOSE_ON_DESTRUCT = 0b00000100, // TODO consider logic flag operations (i.e. CLOSE_ON_DESCTRUCT | RUN_FOREVER doesnt make sense)
        CLOSE_ON_DTOR_AFTER_CONN = 0b00001000,
    };

    inline friend Flags operator|(Flags a, Flags b) {
        return static_cast<Flags>(static_cast<int>(a) | static_cast<int>(b));
    }

    std::unique_ptr<EventHandler> handler;
    InitMsg init_msg;
    int numa_socket;

public:
    RDMA(std::string ip, uint16_t port);
    ~RDMA();

    size_t register_memory(void* mem, size_t size); // returns an ID to memory region (in case of registering multiple memory regions)

    [[nodiscard]] Connection* connect_to(std::string ip, uint16_t port);

    void listen(Flags flags); // consider taking ip in constructor, because then: connect_to can be bound and alloc_pd might work earlier

    void close(Connection* ctx);

    void close_all();

    void wait(); // wait until all open connections are closed

    void print_qp_states();

private:
    void poll_event(struct rdma_event_channel* channel);

    void destroy_qp(Connection* ctx);

    Flags server_thread_flags = Flags::RUN_FOREVER; // default 0
};


} // namespace rdma
