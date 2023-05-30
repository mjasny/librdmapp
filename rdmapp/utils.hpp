#pragma once

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <string>

namespace rdma {
namespace utils {

std::string sockaddr_to_ip(struct sockaddr* sa);

std::string ibdev2netdev(std::string ibdev);

bool netdev_has_ip(std::string netdev, std::string ip);


} // namespace utils
} // namespace rdma
