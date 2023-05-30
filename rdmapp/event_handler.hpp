#pragma once


#include "connection.hpp"


namespace rdma {


struct EventHandler {
    virtual ~EventHandler() = default;

    virtual void on_connect(Connection& conn [[maybe_unused]]) {
        // conn contains info about remote
    }

    virtual void on_disconnect(Connection& conn [[maybe_unused]]) {
        // same info but state of conn is DISCONNECTED
    }
};


} // namespace rdma
