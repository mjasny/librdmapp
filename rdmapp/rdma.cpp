#include "rdma.hpp"

#include "my_asserts.hpp"

#include <arpa/inet.h>
#include <array>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <netdb.h>
#include <rdma/rdma_cma.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unordered_map>


namespace rdma {

RDMA::RDMA(std::string ip, uint16_t port) : local_ip(ip), local_port(port) {
    setenv("MLX5_SINGLE_THREADED", "1", true);

    channel = rdma_create_event_channel();
    check_ptr(channel);


    int num_devices;
    struct ibv_context** list = rdma_get_devices(&num_devices);
    check_ptr(list);

    std::array<uint8_t, 12> IPV4_PREFIX{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff};

    bool found_ip = false;
    // std::cout << "Devices: " << num_devices << "\n";
    for (int i = 0; i < num_devices; ++i) {
        auto d = list[i]->device;
        // std::cout << d->name << " " << d->dev_name << " " << d->dev_path << " " << d->ibdev_path << "\n";
        // std::cout << "x: " << ibv_get_device_name(d) << "\n";

        struct ibv_device_attr device_attr;
        check_ret(ibv_query_device(list[i], &device_attr));

        // std::cout << "ports: " << +device_attr.phys_port_cnt << "\n";

        struct ibv_port_attr port_attr;
        check_ret(ibv_query_port(list[i], 1, &port_attr));

        int max_entries = port_attr.gid_tbl_len * device_attr.phys_port_cnt;

        struct ibv_gid_entry entries[max_entries];
        ssize_t n_entries = ibv_query_gid_table(list[i], entries, max_entries, 0);
        check_ret(n_entries);

        for (ssize_t i = 0; i < n_entries; ++i) {
            auto e = entries[i];


            // enum ibv_gid_type {
            //     IBV_GID_TYPE_IB,
            //     IBV_GID_TYPE_ROCE_V1,
            //     IBV_GID_TYPE_ROCE_V2,
            // };
            // std::cout << e.gid_index << " " << e.port_num << " " << e.gid_type << " " << e.ndev_ifindex << "\n";


            if (!std::equal(IPV4_PREFIX.begin(), IPV4_PREFIX.end(), e.gid.raw)) {
                continue;
            }

            std::stringstream ss;
            ss << +e.gid.raw[12] << "." << +e.gid.raw[13] << "." << +e.gid.raw[14] << "." << +e.gid.raw[15];
            auto ip = ss.str();

            if (ip == local_ip) {
                found_ip = true;
                break;
            }


            // struct ibv_gid_entry {
            //     union ibv_gid gid;
            //     uint32_t gid_index;
            //     uint32_t port_num;
            //     uint32_t gid_type; /* enum ibv_gid_type */
            //     uint32_t ndev_ifindex;
            // };
        }


        if (!found_ip) {
            // fallback
            try {
                std::string netdev = utils::ibdev2netdev(d->name);
                found_ip = utils::netdev_has_ip(netdev, local_ip);
            } catch (...) {
            }
        }

        if (found_ip) {
            context = list[i];
            // std::cout << "IP is on: " << context->device->name << "\n";
            pd = ibv_alloc_pd(context);
            check_ptr(pd);

            try {
                std::stringstream path;
                path << d->ibdev_path << "/device/numa_node";

                std::ifstream ifile(path.str());
                if (!ifile.good()) {
                    throw std::invalid_argument("Could not get socket for interface.");
                }
                ifile >> numa_socket;
                ifile.close();
            } catch (...) {
                numa_socket = -1;
            }
            break;
        }
    }
    ensure(found_ip, [&] {
        std::stringstream ss;
        ss << "Did not find interface that has local_ip: " << local_ip;
        return ss.str();
    });

    rdma_free_devices(list);
}

RDMA::~RDMA() {
    {
        const std::lock_guard<std::recursive_mutex> lock(mutex);
        for (auto& [id, ctx] : connections) {
            // cleanup?
            destroy_qp(ctx);
            delete ctx;
        }
    }

    if ((server_thread_flags & Flags::CLOSE_ON_DESTRUCT) || (server_thread_flags & Flags::CLOSE_ON_DTOR_AFTER_CONN)) {
        server_thread.request_stop();
    }

    if (server_thread.joinable()) {
        server_thread.join();
    }


    for (auto mr : mrs) {
        check_ret(ibv_dereg_mr(mr));
    }

    check_ret(ibv_dealloc_pd(pd));
    rdma_destroy_event_channel(channel);
}


size_t RDMA::register_memory(void* mem, size_t size) {
    // ensure(mr == nullptr, "some memory region is already registered.");

    auto mr = ibv_reg_mr(pd, mem, size, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC);
    check_ptr(mr);
    mrs.push_back(mr);

    if (mrs.size() == 1) { // keep init msg to first mr (TODO is this fine??)
        init_msg.meminfo.addr = mem;
        init_msg.meminfo.length = size;
        init_msg.meminfo.rkey = mr->rkey;
    }

    return mrs.size() - 1;
}


Connection* RDMA::connect_to(std::string ip, uint16_t port) {
    const std::lock_guard<std::mutex> lock(connect_to_mutex);


    auto ctx = new Connection(mrs);
    check_ret(rdma_create_id(channel, &ctx->id, nullptr, RDMA_PS_TCP));


    {
        struct addrinfo* res;
        int ret = getaddrinfo(local_ip.c_str(), nullptr, nullptr, &res);
        if (ret) {
            throw std::runtime_error("getaddrinfo failed");
        }

        struct sockaddr_storage addr;

        if (res->ai_family == PF_INET) {
            memcpy(&addr, res->ai_addr, sizeof(struct sockaddr_in));
        } else if (res->ai_family == PF_INET6) {
            memcpy(&addr, res->ai_addr, sizeof(struct sockaddr_in6));
        } else {
            throw std::runtime_error("Unexpected ai_family");
        }
        freeaddrinfo(res);

        // if (addr.ss_family == AF_INET) {
        //     ((struct sockaddr_in*)&addr)->sin_port = htons(local_port);
        // } else {
        //     ((struct sockaddr_in6*)&addr)->sin6_port = htons(local_port);
        // }

        check_ret(rdma_bind_addr(ctx->id, (struct sockaddr*)&addr));
    }


    {
        struct addrinfo* res;
        int ret = getaddrinfo(ip.c_str(), nullptr, nullptr, &res);
        if (ret) {
            throw std::runtime_error("getaddrinfo failed");
        }

        struct sockaddr_storage addr;

        if (res->ai_family == PF_INET) {
            memcpy(&addr, res->ai_addr, sizeof(struct sockaddr_in));
        } else if (res->ai_family == PF_INET6) {
            memcpy(&addr, res->ai_addr, sizeof(struct sockaddr_in6));
        } else {
            throw std::runtime_error("Unexpected ai_family");
        }
        freeaddrinfo(res);

        if (addr.ss_family == AF_INET) {
            ((struct sockaddr_in*)&addr)->sin_port = htons(port);
        } else {
            ((struct sockaddr_in6*)&addr)->sin6_port = htons(port);
        }

        check_ret(rdma_resolve_addr(ctx->id, nullptr, (struct sockaddr*)&addr, TIMEOUT));

        struct rdma_cm_event* event;
        check_ret(rdma_get_cm_event(channel, &event));
        ensure(event->id == ctx->id);
        ensure(event->event == RDMA_CM_EVENT_ADDR_RESOLVED, [&] {
            return rdma_event_str(event->event);
        });
        check_ret(rdma_ack_cm_event(event));

        check_ret(rdma_resolve_route(ctx->id, TIMEOUT));

        check_ret(rdma_get_cm_event(channel, &event));
        ensure(event->id == ctx->id);
        ensure(event->event == RDMA_CM_EVENT_ROUTE_RESOLVED, [&] {
            return rdma_event_str(event->event);
        });
        check_ret(rdma_ack_cm_event(event));
    }


    ctx->cq = ibv_create_cq(ctx->id->verbs, 16, nullptr, nullptr, 0); // 16 is the fixed value for now?
    check_ptr(ctx->cq);


    struct ibv_qp_init_attr init_attr {};
    init_attr.cap.max_send_wr = 8192; // 8192; //1024; //4096;
    init_attr.cap.max_recv_wr = 8192; // 8192; //1024; //4096;
    init_attr.cap.max_recv_sge = 1;
    init_attr.cap.max_send_sge = 1;
    // #ifdef USE_INLINE
    //     init_attr.cap.max_inline_data = INLINE_SIZE;
    // #endif
    init_attr.qp_type = IBV_QPT_RC;
    init_attr.send_cq = ctx->cq;
    init_attr.recv_cq = ctx->cq;
    // discussion here: https://linux-rdma.vger.kernel.narkive.com/8f5hoTKh/sharing-mr-between-multiple-connections
    check_ret(rdma_create_qp(ctx->id, pd /*nullptr*/, &init_attr));
    ctx->qp = ctx->id->qp;


    struct rdma_conn_param conn_param {};

    conn_param.responder_resources = RDMA_MAX_RESP_RES;
    conn_param.initiator_depth = RDMA_MAX_INIT_DEPTH;

    conn_param.retry_count = 0x07;     // necessary for NAK Hack for RDMA Reads
    conn_param.rnr_retry_count = 0x07; // necessary for NAK Hack for RDMA Reads
    // conn_param.responder_resources = 0x80;
    // conn_param.initiator_depth = 0x80;
    // conn_param.flow_control = 0x00;


    conn_param.private_data = &init_msg;
    conn_param.private_data_len = sizeof(InitMsg);

    // check_ret(rdma_connect(ctx->id, &conn_param));
    check_ret(rdma_connect(ctx->id, &conn_param));

    struct rdma_cm_event* event;
    check_ret(rdma_get_cm_event(channel, &event));
    ensure(event->id == ctx->id);
    ensure(event->event == RDMA_CM_EVENT_ESTABLISHED, [&] {
        // TODO handle more cases
        if (event->event == RDMA_CM_EVENT_REJECTED) {
            check_ret(rdma_ack_cm_event(event));
            rdma_destroy_qp(ctx->id);
            check_ret(ibv_destroy_cq(ctx->cq));
            check_ret(rdma_destroy_id(ctx->id));
        }
        return rdma_event_str(event->event);
    });

    auto& conn = event->param.conn;
    ensure(conn.private_data_len >= sizeof(InitMsg));
    std::memcpy(ctx->init_msg, conn.private_data, sizeof(InitMsg));

    check_ret(rdma_ack_cm_event(event));
    // std::cout << "connection established\n";

    ctx->state = Connection::State::OPEN;
    ctx->type = Connection::Type::OUTGOING;
    ctx->remote_ip = utils::sockaddr_to_ip(rdma_get_peer_addr(ctx->id));


    {
        const std::lock_guard<std::recursive_mutex> lock(mutex);
        connections[ctx->id] = ctx;
    }

    return ctx;
}

void RDMA::listen(Flags flags) {

    server_thread_flags = flags;

    struct rdma_event_channel* channel = rdma_create_event_channel();
    check_ptr(channel);


    auto sys_fcntl_modfl = [](int fd, int add, int rem) {
        int oldfl = fcntl(fd, F_GETFL);
        ensure(oldfl >= 0, [&] {
            std::stringstream ss;
            ss << "fcntl(fd=" << fd << ", F_GETFL) returned " << oldfl << ": " << std::strerror(errno) << "\n";
            return ss.str();
        });

        int ret = fcntl(fd, F_SETFL, (oldfl | add) & ~rem);
        ensure(ret >= 0, [&] {
            std::stringstream ss;
            ss << "fcntl(fd=" << fd << ", F_SETFL) returned " << ret << ": " << std::strerror(errno) << "\n";
            return ss.str();
        });
    };

    sys_fcntl_modfl(channel->fd, O_NONBLOCK, 0);


    struct rdma_cm_id* id;
    check_ret(rdma_create_id(channel, &id, nullptr, RDMA_PS_TCP));

    {
        struct addrinfo* res;
        int ret = getaddrinfo(local_ip.c_str(), nullptr, nullptr, &res);
        if (ret) {
            throw std::runtime_error("getaddrinfo failed");
        }

        struct sockaddr_storage addr;

        if (res->ai_family == PF_INET) {
            memcpy(&addr, res->ai_addr, sizeof(struct sockaddr_in));
        } else if (res->ai_family == PF_INET6) {
            memcpy(&addr, res->ai_addr, sizeof(struct sockaddr_in6));
        } else {
            throw std::runtime_error("Unexpected ai_family");
        }
        freeaddrinfo(res);

        ensure(local_port != 0);
        if (addr.ss_family == AF_INET) {
            ((struct sockaddr_in*)&addr)->sin_port = htons(local_port);
        } else {
            ((struct sockaddr_in6*)&addr)->sin6_port = htons(local_port);
        }

        check_ret(rdma_bind_addr(id, (struct sockaddr*)&addr));
    }

    check_ret(rdma_listen(id, 0));


    auto loop = [&, channel, id, flags](std::stop_token stop_token) {
        bool had_connections = false;
        while (true) {
            poll_event(channel);


            if (!had_connections && connections.size() > 0) {
                for (auto& [_, ctx] : connections) {
                    if (ctx->type == Connection::Type::INCOMING) {
                        had_connections = true;
                        break;
                    }
                }
            } else if (flags & Flags::CLOSE_AFTER_LAST) {
                if (had_connections && connections.size() == 0) {
                    break;
                }
            } else if (flags & Flags::CLOSE_ON_DTOR_AFTER_CONN) {
                if (had_connections && stop_token.stop_requested()) {
                    break;
                }
            } else if (flags & Flags::CLOSE_ON_DESTRUCT) {
                if (stop_token.stop_requested()) {
                    break;
                }
            }
        }

        check_ret(rdma_destroy_id(id));
        rdma_destroy_event_channel(channel);
    };

    if (flags & Flags::IN_BACKGROUND) {
        server_thread = std::jthread(loop);
    } else {
        std::stop_token stop_token;
        loop(stop_token);
    }
}

// private
void RDMA::poll_event(struct rdma_event_channel* channel) {
    // only run while-loop here, the setup is blocking
    // std::cout << "waiting for event, active connections: " << std::dec << connections.size() << "\n";
    struct rdma_cm_event* event;

    {
        int fd = channel->fd;
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);

        // struct timeval t = {1 /*seconds*/, 0 /*microseconds*/};
        struct timeval t = {0 /*seconds*/, 1000 /*microseconds*/};

        if (select(fd + 1, &fds, NULL, NULL, &t) != 1) {
            return;
        }
    }


    check_ret(rdma_get_cm_event(channel, &event));
    // std::cout << "got: " << rdma_event_str(event->event) << "\n";

    switch (event->event) {
        case RDMA_CM_EVENT_CONNECT_REQUEST: {
            ensure(mrs.size() == 1, [&]() {
                if (mrs.size() == 0) {
                    return "No memory region was registered";
                }
                return "Multiple memory regions not implemented yet for remote side";
            });

            const std::lock_guard<std::recursive_mutex> lock(mutex);
            ensure(!connections.contains(event->id));
            auto ctx = new Connection(mrs);
            ctx->id = event->id;
            connections[ctx->id] = ctx;

            auto& conn = event->param.conn;
            ensure(conn.private_data_len >= sizeof(InitMsg));
            std::memcpy(ctx->init_msg, conn.private_data, sizeof(InitMsg));

            check_ret(rdma_ack_cm_event(event));

            ctx->cq = ibv_create_cq(ctx->id->verbs, 16, nullptr, nullptr, 0);
            check_ptr(ctx->cq);

            struct ibv_qp_init_attr init_attr {};
            // memset(&init_attr, 0, sizeof(init_attr));
            init_attr.cap.max_send_wr = 1; // 1024; //4096; // TODO take from config
            init_attr.cap.max_recv_wr = 1; // 1024; //4096; // TODO take from config
            init_attr.cap.max_recv_sge = 1;
            init_attr.cap.max_send_sge = 1;
            // #ifdef USE_INLINE
            //             init_attr.cap.max_inline_data = INLINE_SIZE;
            // #endif
            init_attr.qp_type = IBV_QPT_RC;
            init_attr.send_cq = ctx->cq;
            init_attr.recv_cq = ctx->cq;
            check_ret(rdma_create_qp(ctx->id, pd, &init_attr));
            ctx->qp = ctx->id->qp;

            struct rdma_conn_param conn_param {};
            conn_param.responder_resources = RDMA_MAX_RESP_RES;
            conn_param.initiator_depth = RDMA_MAX_INIT_DEPTH;
            conn_param.retry_count = 0x07;     // necessary for NAK Hack for RDMA Reads
            conn_param.rnr_retry_count = 0x07; // necessary for NAK Hack for RDMA Reads

            conn_param.private_data = &init_msg;
            conn_param.private_data_len = sizeof(InitMsg);

            check_ret(rdma_accept(ctx->id, &conn_param));
            break;
        }
        case RDMA_CM_EVENT_ESTABLISHED: {
            const std::lock_guard<std::recursive_mutex> lock(mutex);
            auto& ctx = connections.at(event->id);
            check_ret(rdma_ack_cm_event(event));

            ctx->state = Connection::State::OPEN;
            ctx->type = Connection::Type::INCOMING;
            ctx->remote_ip = utils::sockaddr_to_ip(rdma_get_peer_addr(ctx->id));

            // std::cout << "Connection established with: " << ctx->remote_ip << "\n";

            if (handler) {
                handler->on_connect(*ctx);
            }
            break;
        }

        case RDMA_CM_EVENT_DISCONNECTED: {
            const std::lock_guard<std::recursive_mutex> lock(mutex);
            auto& ctx = connections.at(event->id);
            check_ret(rdma_ack_cm_event(event));

            ctx->state = Connection::State::CLOSED;
            if (handler) {
                handler->on_disconnect(*ctx);
            }
            close(ctx);

            break;
        }

        default: {
            std::cerr << "Unhandled event:" << rdma_event_str(event->event) << "\n";
            check_ret(rdma_ack_cm_event(event));
            break;
        }
    }
}

void RDMA::wait() {
    if (server_thread.joinable()) {
        server_thread.join();
    }
}


void RDMA::close_all() {
    const std::lock_guard<std::recursive_mutex> lock(mutex);
    for (auto& [id, ctx] : connections) {
        destroy_qp(ctx);
        // ctx = nullptr;
        delete ctx;
    }
    connections.clear();
}

void RDMA::close(Connection* ctx) {
    destroy_qp(ctx);
    const std::lock_guard<std::recursive_mutex> lock(mutex);
    ensure(connections.contains(ctx->id));
    connections.erase(ctx->id);
    delete ctx;
    // ctx = nullptr;
}

void RDMA::destroy_qp(Connection* ctx) {
    rdma_destroy_qp(ctx->id);
    check_ret(ibv_destroy_cq(ctx->cq));
    check_ret(rdma_destroy_id(ctx->id));
}

void RDMA::print_qp_states() {
    const std::lock_guard<std::recursive_mutex> lock(mutex);
    size_t i = 0;
    std::stringstream ss;
    for (auto& [cm_id, conn] : connections) {
        struct ibv_qp_attr attr;
        struct ibv_qp_init_attr init_attr;
        int attr_mask = ibv_qp_attr_mask::IBV_QP_DEST_QPN | ibv_qp_attr_mask::IBV_QP_STATE | ibv_qp_attr_mask::IBV_QP_CUR_STATE;
        check_ret(ibv_query_qp(conn->qp, &attr, attr_mask, /*struct ibv_qp_init_attr * init_attr */ &init_attr));
        ss << i++ << ": dest_qp_num=" << attr.dest_qp_num << " qp_state=" << +attr.qp_state << " cur_qp_state=" << +attr.cur_qp_state << "\n";

        // 0 IBV_QPS_RESET - Reset state
        // 1 IBV_QPS_INIT - Initialized state
        // 2 IBV_QPS_RTR - Ready To Receive state
        // 3 IBV_QPS_RTS - Ready To Send state
        // 4 IBV_QPS_SQD - Send Queue Drain state
        // 5 IBV_QPS_SQE - Send Queue Error state
        // 6 IBV_QPS_ERR - Error state
    }
    std::cout << ss.rdbuf();
}

} // namespace rdma
