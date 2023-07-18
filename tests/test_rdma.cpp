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

        // uint64_t* value = reinterpret_cast<uint64_t*>(mem);
        // std::cout << "server value=0x" << std::hex << *value << std::dec << "\n";
    }
};

std::string IP = "172.18.94.20";

int main(int argc [[maybe_unused]], char** argv [[maybe_unused]]) {
    constexpr size_t size = 65536;
    void* s_mem = malloc(size);
    rdma::check_ptr(s_mem);

    rdma::RDMA server{IP, 7471};
    std::cout << "NIC located on socket: " << server.numa_socket << "\n";
    server.handler = std::make_unique<EventHandler>(s_mem);
    server.register_memory(s_mem, size);

    server.listen(rdma::RDMA::CLOSE_AFTER_LAST | rdma::RDMA::IN_BACKGROUND);

    // std::this_thread::sleep_for(std::chrono::milliseconds(100));

    {
        rdma::RDMA client{IP, 0};
        void* mem = malloc(size);
        rdma::check_ptr(mem);
        client.register_memory(mem, size); // use std::span?

        uint64_t* c_values = reinterpret_cast<uint64_t*>(mem);
        uint64_t* s_values = reinterpret_cast<uint64_t*>(s_mem);

        auto conn = client.connect_to(IP, 7471);
        // conn->init_msg.as<uint8_t>();

        *c_values = 0x1234567812345678;
        conn->write(c_values, sizeof(uint64_t), 0, rdma::Flags().signaled());
        conn->sync_signaled(1);
        rdma::ensure(s_values[0] == c_values[0]);

        uint64_t desired = 0;
        conn->cmp_swap(&c_values[1], 0x1234567812345678, desired, 0, rdma::Flags().signaled());
        conn->sync_signaled(1);
        rdma::ensure(s_values[0] == desired, "Server-side value does not match expected");
        rdma::ensure(c_values[1] == 0x1234567812345678, "Client-side value does not match expected");


        conn->fetch_add(c_values, 0x44445555, 0, rdma::Flags().signaled());
        conn->sync_signaled(1);
        rdma::ensure(s_values[0] == 0x44445555, "Server-side value does not match expected");

        c_values[0] = 0;
        conn->read(c_values, sizeof(uint64_t), 0, rdma::Flags().signaled());
        conn->sync_signaled(1);
        rdma::ensure(c_values[0] == 0x44445555, "Server-side value does not match expected");

        for (size_t i = 0; i < conn->max_wr - 1; ++i) {
            conn->fetch_add(c_values, 1, 0);
        }
        conn->fetch_add(c_values, 1, 0, rdma::Flags().signaled());
        conn->sync_signaled(1);
        rdma::ensure(s_values[0] == 0x44445555 + conn->max_wr, "Server-side value does not match expected");


        // async. operations examples
        *c_values = 0x1234567812345678;
        conn->write(c_values, sizeof(uint64_t), 0, rdma::Flags().signaled());  // op 0
        conn->write(c_values, sizeof(uint64_t), 8);                            // op 1
        conn->write(c_values, sizeof(uint64_t), 16, rdma::Flags().signaled()); // op 2
        conn->write(c_values, sizeof(uint64_t), 24);                           // op 3
        conn->sync_signaled(1);                                                // synchronizes on op 0
        conn->sync_signaled(1);                                                // synchronizes on op 2
        rdma::ensure(s_values[0] == c_values[0]);
        rdma::ensure(s_values[2] == c_values[0]);


        *c_values = 0xabcdef;
        conn->write(c_values, sizeof(uint64_t), 0, rdma::Flags().signaled());  // op 0
        conn->write(c_values, sizeof(uint64_t), 8);                            // op 1
        conn->write(c_values, sizeof(uint64_t), 16);                           // op 2
        conn->write(c_values, sizeof(uint64_t), 24, rdma::Flags().signaled()); // op 3
        conn->sync_signaled();                                                 // synchronizes on all signaled operations (i.e., 0 & 3)
        rdma::ensure(s_values[0] == c_values[0]);
        rdma::ensure(s_values[3] == c_values[0]);


        client.close(conn);
        free(mem);
    }

    server.wait();

    free(s_mem);

    std::cout << "Test concluded successfully!" << std::endl;

    return 0;
}