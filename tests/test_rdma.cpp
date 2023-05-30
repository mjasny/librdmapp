#include "rdmapp/my_asserts.hpp"
#include "rdmapp/rdma.hpp"

#include <iostream>


struct EventHandler : public rdma::EventHandler {
    void* mem;

    EventHandler(void* mem) : mem(mem) {}

    void on_connect(rdma::Connection& conn [[maybe_unused]]) override {
        // conn contains info about remote
        std::cout << "on_connect: " << conn.remote_ip << "\n";
    }

    void on_disconnect(rdma::Connection& conn [[maybe_unused]]) override {
        // same info but state of conn is DISCONNECTED
        std::cout << "on_disconnect\n";

        uint64_t* value = reinterpret_cast<uint64_t*>(mem);
        std::cout << "server value=0x" << std::hex << *value << std::dec << "\n";
    }
};


int main(int argc [[maybe_unused]], char** argv [[maybe_unused]]) {
    constexpr size_t size = 65536;
    void* mem = malloc(size);
    rdma::check_ptr(mem);

    rdma::RDMA server{"192.168.101.208", 7471};
    std::cout << "NIC located on socket: " << server.numa_socket << "\n";
    server.handler = std::make_unique<EventHandler>(mem);
    server.register_memory(mem, size);

    server.listen(rdma::RDMA::CLOSE_AFTER_LAST | rdma::RDMA::IN_BACKGROUND);

    // std::this_thread::sleep_for(std::chrono::milliseconds(100));

    {
        rdma::RDMA client{"192.168.101.208", 7471};
        void* mem = malloc(size);
        rdma::check_ptr(mem);
        client.register_memory(mem, size); // use std::span?

        uint64_t* value = reinterpret_cast<uint64_t*>(mem);

        auto conn = client.connect_to("192.168.101.208", 7471);
        // conn->init_msg.as<uint8_t>();

        *value = 0x1234567812345678;
        conn->write_sync(value, sizeof(uint64_t), 0);


        conn->cmp_swap_sync(value, 0x1234567812345678, 0, 0);
        std::cout << "expected value=0x" << std::hex << *value << std::dec << "\n";


        conn->fetch_add_sync(value, 0x44445555, 0);
        std::cout << "expected value=0x" << std::hex << *value << std::dec << "\n";

        *value = 0;
        conn->read_sync(value, sizeof(uint64_t), 0);
        std::cout << "read value=0x" << std::hex << *value << std::dec << "\n";


        for (size_t i = 0; i < conn->max_wr; ++i) {
            conn->fetch_add(value, 1, 0);
        }
        conn->poll_cq(1);
        std::cout << "fetch_add value=0x" << std::hex << *value << std::dec << "\n";


        client.close(conn);
        free(mem);
    }

    server.wait();

    free(mem);

    return 0;
}