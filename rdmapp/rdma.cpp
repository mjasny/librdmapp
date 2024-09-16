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
    // setenv("MLX5_SINGLE_THREADED", "1", true);

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

        // struct ibv_gid_entry {
        //     union ibv_gid gid;
        //     uint32_t gid_index;
        //     uint32_t port_num;
        //     uint32_t gid_type; /* enum ibv_gid_type */
        //     uint32_t ndev_ifindex;
        // };
        // auto ibv_query_gid_table = [](struct ibv_context* context,
        //                               struct ibv_gid_entry* entries,
        //                               size_t max_entries, uint32_t flags) -> ssize_t {
        // };


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
    uint32_t access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC | IBV_ACCESS_RELAXED_ORDERING;
    auto mr = ibv_reg_mr(pd, mem, size, access_flags);
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


    struct sockaddr source_sin;
    struct sockaddr* source_ptr = nullptr;

    {
        struct addrinfo* res;
        int ret = getaddrinfo(local_ip.c_str(), nullptr, nullptr, &res);
        if (ret) {
            throw std::runtime_error("getaddrinfo failed");
        }

        if (res->ai_family == PF_INET) {
            memcpy(&source_sin, res->ai_addr, sizeof(struct sockaddr_in));
        } else if (res->ai_family == PF_INET6) {
            memcpy(&source_sin, res->ai_addr, sizeof(struct sockaddr_in6));
        } else {
            throw std::runtime_error("Unexpected ai_family");
        }
        freeaddrinfo(res);

        // if (source_sin.ss_family == AF_INET) {
        //     ((struct sockaddr_in*)&source_sin)->sin_port = htons(local_port);
        // } else {
        //     ((struct sockaddr_in6*)&source_sin)->sin6_port = htons(local_port);
        // }

        source_ptr = (struct sockaddr*)&source_sin;
        // check_ret(rdma_bind_addr(ctx->id, (struct sockaddr*)&source_sin));
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

        check_ret(rdma_resolve_addr(ctx->id, source_ptr, (struct sockaddr*)&addr, TIMEOUT));

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


    ctx->send_cq = ibv_create_cq(ctx->id->verbs, 64, nullptr, nullptr, 0); // 16 is the fixed value for now?
    check_ptr(ctx->send_cq);
    ctx->recv_cq = ibv_create_cq(ctx->id->verbs, 64, nullptr, nullptr, 0); // 16 is the fixed value for now?
    check_ptr(ctx->recv_cq);

    struct ibv_qp_init_attr init_attr {};
    init_attr.cap.max_send_wr = 1024; // 8192; //1024; //4096;
    init_attr.cap.max_recv_wr = 1024; // 8192; //1024; //4096;
    init_attr.cap.max_recv_sge = 1;
    init_attr.cap.max_send_sge = 1;
    // #ifdef USE_INLINE
    //     init_attr.cap.max_inline_data = INLINE_SIZE;
    // #endif
    init_attr.cap.max_inline_data = 220;

    init_attr.qp_type = IBV_QPT_RC;
    init_attr.send_cq = ctx->send_cq;
    init_attr.recv_cq = ctx->recv_cq;
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

    check_ret(rdma_connect(ctx->id, &conn_param));


    struct rdma_cm_event* event;
    check_ret(rdma_get_cm_event(channel, &event));
    ensure(event->id == ctx->id);
    ensure(event->event == RDMA_CM_EVENT_ESTABLISHED, [&] {
        // TODO handle more cases
        std::string msg{rdma_event_str(event->event)};
        if (event->event == RDMA_CM_EVENT_REJECTED) {
            check_ret(rdma_ack_cm_event(event));
            rdma_destroy_qp(ctx->id);
            check_ret(ibv_destroy_cq(ctx->send_cq));
            check_ret(ibv_destroy_cq(ctx->recv_cq));
            check_ret(rdma_destroy_id(ctx->id));
            delete ctx;
        }
        return msg;
    });

    auto& conn = event->param.conn;
    ensure(conn.private_data_len >= sizeof(InitMsg));
    std::memcpy(ctx->init_msg, conn.private_data, sizeof(InitMsg));
    ctx->add_remote_mr(ctx->init_msg.meminfo);


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

            const std::lock_guard<std::recursive_mutex> lock(mutex); // for connections
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
            ensure(mrs.size() >= 1, [&]() {
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
            ctx->add_remote_mr(ctx->init_msg.meminfo);

            check_ret(rdma_ack_cm_event(event));

            ctx->send_cq = ibv_create_cq(ctx->id->verbs, 64, nullptr, nullptr, 0);
            check_ptr(ctx->send_cq);
            ctx->recv_cq = ibv_create_cq(ctx->id->verbs, 64, nullptr, nullptr, 0);
            check_ptr(ctx->recv_cq);

            struct ibv_qp_init_attr init_attr {};
            // memset(&init_attr, 0, sizeof(init_attr));
            init_attr.cap.max_send_wr = 1024; // 4096; // TODO take from config
            init_attr.cap.max_recv_wr = 1024; // 4096; // TODO take from config
            init_attr.cap.max_recv_sge = 1;
            init_attr.cap.max_send_sge = 1;
            // #ifdef USE_INLINE
            //             init_attr.cap.max_inline_data = INLINE_SIZE;
            // #endif
            init_attr.cap.max_inline_data = 220;
            init_attr.qp_type = IBV_QPT_RC;
            init_attr.send_cq = ctx->send_cq;
            init_attr.recv_cq = ctx->recv_cq;
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
    const std::lock_guard<std::recursive_mutex> lock(mutex);
    destroy_qp(ctx);
    ensure(connections.contains(ctx->id));
    connections.erase(ctx->id);
    delete ctx;
    // ctx = nullptr;
}

void RDMA::destroy_qp(Connection* ctx) {
    rdma_destroy_qp(ctx->id);
    check_ret(ibv_destroy_cq(ctx->send_cq));
    check_ret(ibv_destroy_cq(ctx->recv_cq));
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

        continue;
        // 0 IBV_QPS_RESET - Reset state
        // 1 IBV_QPS_INIT - Initialized state
        // 2 IBV_QPS_RTR - Ready To Receive state
        // 3 IBV_QPS_RTS - Ready To Send state
        // 4 IBV_QPS_SQD - Send Queue Drain state
        // 5 IBV_QPS_SQE - Send Queue Error state
        // 6 IBV_QPS_ERR - Error state

        // Lambda functions for conversion
        auto qp_state_to_string = [](enum ibv_qp_state state) -> const char* {
            switch (state) {
                case IBV_QPS_RESET:
                    return "RESET";
                case IBV_QPS_INIT:
                    return "INIT";
                case IBV_QPS_RTR:
                    return "RTR";
                case IBV_QPS_RTS:
                    return "RTS";
                case IBV_QPS_SQD:
                    return "SQD";
                case IBV_QPS_SQE:
                    return "SQE";
                case IBV_QPS_ERR:
                    return "ERR";
                default:
                    return "UNKNOWN";
            }
        };

        auto mtu_to_string = [](enum ibv_mtu mtu) -> const char* {
            switch (mtu) {
                case IBV_MTU_256:
                    return "256";
                case IBV_MTU_512:
                    return "512";
                case IBV_MTU_1024:
                    return "1024";
                case IBV_MTU_2048:
                    return "2048";
                case IBV_MTU_4096:
                    return "4096";
                default:
                    return "UNKNOWN";
            }
        };

        auto mig_state_to_string = [](enum ibv_mig_state state) -> const char* {
            switch (state) {
                case IBV_MIG_MIGRATED:
                    return "MIGRATED";
                case IBV_MIG_REARM:
                    return "REARM";
                case IBV_MIG_ARMED:
                    return "ARMED";
                default:
                    return "UNKNOWN";
            }
        };

        auto print_ah_attr = [&ss](const struct ibv_ah_attr& attr) {
            ss << "    ah_attr:" << "\n";
            ss << "      dlid: " << attr.dlid << "\n";
            ss << "      sl: " << static_cast<int>(attr.sl) << "\n";
            ss << "      src_path_bits: " << static_cast<int>(attr.src_path_bits) << "\n";
            ss << "      static_rate: " << static_cast<int>(attr.static_rate) << "\n";
            ss << "      is_global: " << static_cast<int>(attr.is_global) << "\n";
            ss << "      port_num: " << static_cast<int>(attr.port_num) << "\n";
            if (attr.is_global) {
                ss << "      grh:" << "\n";
                ss << "        dgid: ";
                for (int i = 0; i < 16; ++i) {
                    ss << std::hex << static_cast<int>(attr.grh.dgid.raw[i]) << " ";
                }
                ss << std::dec << "\n";
                ss << "        flow_label: " << attr.grh.flow_label << "\n";
                ss << "        sgid_index: " << static_cast<int>(attr.grh.sgid_index) << "\n";
                ss << "        hop_limit: " << static_cast<int>(attr.grh.hop_limit) << "\n";
                ss << "        traffic_class: " << static_cast<int>(attr.grh.traffic_class) << "\n";
            }
        };

        // attr_mask = IBV_QP_STATE | IBV_QP_CUR_STATE | IBV_QP_EN_SQD_ASYNC_NOTIFY | IBV_QP_CAP | IBV_QP_DEST_QPN | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_PATH_MIG_STATE | IBV_QP_QKEY | IBV_QP_RQ_PSN | IBV_QP_SQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MAX_QP_RD_ATOMIC | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_RNR_NAK_TIMEOUT | IBV_QP_MIN_RNR_TIMER | IBV_QP_FLOW_ENTROPY | IBV_QP_INIT_ATTR_MASK;
        attr_mask = IBV_QP_STATE |
                    IBV_QP_CUR_STATE |
                    IBV_QP_EN_SQD_ASYNC_NOTIFY |
                    IBV_QP_ACCESS_FLAGS |
                    IBV_QP_PKEY_INDEX |
                    IBV_QP_PORT |
                    IBV_QP_QKEY |
                    IBV_QP_AV |
                    IBV_QP_PATH_MTU |
                    IBV_QP_TIMEOUT |
                    IBV_QP_RETRY_CNT |
                    IBV_QP_RNR_RETRY |
                    IBV_QP_RQ_PSN |
                    IBV_QP_MAX_QP_RD_ATOMIC |
                    IBV_QP_ALT_PATH |
                    IBV_QP_MIN_RNR_TIMER |
                    IBV_QP_SQ_PSN |
                    IBV_QP_MAX_DEST_RD_ATOMIC |
                    IBV_QP_PATH_MIG_STATE |
                    IBV_QP_CAP |
                    IBV_QP_DEST_QPN | IBV_QP_RATE_LIMIT;
        check_ret(ibv_query_qp(conn->qp, &attr, attr_mask, &init_attr));

        ss << "QP attributes:" << "\n";
        ss << "  qp_state: " << qp_state_to_string(attr.qp_state) << "\n";
        ss << "  cur_qp_state: " << qp_state_to_string(attr.cur_qp_state) << "\n";
        ss << "  path_mtu: " << mtu_to_string(attr.path_mtu) << "\n";
        ss << "  path_mig_state: " << mig_state_to_string(attr.path_mig_state) << "\n";
        ss << "  qkey: " << attr.qkey << "\n";
        ss << "  rq_psn: " << attr.rq_psn << "\n";
        ss << "  sq_psn: " << attr.sq_psn << "\n";
        ss << "  dest_qp_num: " << attr.dest_qp_num << "\n";
        ss << "  qp_access_flags: " << attr.qp_access_flags << "\n";

        ss << "  cap:" << "\n";
        ss << "    max_send_wr: " << attr.cap.max_send_wr << "\n";
        ss << "    max_recv_wr: " << attr.cap.max_recv_wr << "\n";
        ss << "    max_send_sge: " << attr.cap.max_send_sge << "\n";
        ss << "    max_recv_sge: " << attr.cap.max_recv_sge << "\n";
        ss << "    max_inline_data: " << attr.cap.max_inline_data << "\n";

        ss << "  ah_attr:" << "\n";
        print_ah_attr(attr.ah_attr);

        ss << "  alt_ah_attr:" << "\n";
        print_ah_attr(attr.alt_ah_attr);

        ss << "  pkey_index: " << attr.pkey_index << "\n";
        ss << "  alt_pkey_index: " << attr.alt_pkey_index << "\n";
        ss << "  en_sqd_async_notify: " << static_cast<int>(attr.en_sqd_async_notify) << "\n";
        ss << "  sq_draining: " << static_cast<int>(attr.sq_draining) << "\n";
        ss << "  max_rd_atomic: " << static_cast<int>(attr.max_rd_atomic) << "\n";
        ss << "  max_dest_rd_atomic: " << static_cast<int>(attr.max_dest_rd_atomic) << "\n";
        ss << "  min_rnr_timer: " << static_cast<int>(attr.min_rnr_timer) << "\n";
        ss << "  port_num: " << static_cast<int>(attr.port_num) << "\n";
        ss << "  timeout: " << static_cast<int>(attr.timeout) << "\n";
        ss << "  retry_cnt: " << static_cast<int>(attr.retry_cnt) << "\n";
        ss << "  rnr_retry: " << static_cast<int>(attr.rnr_retry) << "\n";
        ss << "  alt_port_num: " << static_cast<int>(attr.alt_port_num) << "\n";
        ss << "  alt_timeout: " << static_cast<int>(attr.alt_timeout) << "\n";
        ss << "  rate_limit: " << attr.rate_limit << "\n";
    }


    std::cout << ss.rdbuf();
}

} // namespace rdma
