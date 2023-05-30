#include "utils.hpp"

#include "my_asserts.hpp"

#include <arpa/inet.h>
#include <filesystem>
#include <fstream>
#include <ifaddrs.h>
#include <iostream>
#include <net/if.h>
#include <sstream>
#include <sys/socket.h>
#include <sys/types.h>

namespace rdma {
namespace utils {

std::string sockaddr_to_ip(struct sockaddr* sa) {
    // std::cout << "port=" << ((struct sockaddr_in*)sa)->sin_port << "\n";
    switch (sa->sa_family) {
        case AF_INET: {
            char buf[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(((struct sockaddr_in*)sa)->sin_addr), buf, INET_ADDRSTRLEN);
            return buf;
        }

        case AF_INET6: {
            char buf[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, &(((struct sockaddr_in6*)sa)->sin6_addr), buf, INET6_ADDRSTRLEN);
            return buf;
        }

        default:
            std::cout << sa->sa_family << "\n";
            throw std::invalid_argument("Unknown AF");
    }
}

std::string ibdev2netdev(std::string ibdev) {
    namespace fs = std::filesystem;
    using it = fs::recursive_directory_iterator;

    auto path = fs::path("/sys/class/infiniband/") / ibdev / "device/resource";
    std::ifstream f(path);
    std::string ib_res((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    for (const auto& iface_sys_path : it("/sys/class/net/")) {
        auto path = fs::path(iface_sys_path) / "device/resource";
        std::ifstream f(path);
        std::string net_res((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

        if (ib_res == net_res) {
            return fs::path(iface_sys_path).filename();
        }
    }

    std::stringstream ss;
    ss << "Could not find netdev for " << ibdev << "\n";
    throw std::runtime_error(ss.str());
}

bool netdev_has_ip(std::string netdev, std::string ip) {
    struct ifaddrs* ptr_ifaddrs = nullptr;

    check_ret(getifaddrs(&ptr_ifaddrs));

    bool found = false;
    for (auto* ptr_entry = ptr_ifaddrs; ptr_entry != nullptr; ptr_entry = ptr_entry->ifa_next) {

        if (ptr_entry->ifa_name != netdev) {
            continue;
        }

        // std::string interface_name = std::string(ptr_entry->ifa_name);
        std::string ipaddress_human_readable_form;
        sa_family_t address_family = ptr_entry->ifa_addr->sa_family;
        if (address_family == AF_INET) {
            // IPv4

            // Be aware that the `ifa_addr`, `ifa_netmask` and `ifa_data` fields might contain nullptr.
            // Dereferencing nullptr causes "Undefined behavior" problems.
            // So it is need to check these fields before dereferencing.
            if (ptr_entry->ifa_addr != nullptr) {
                char buffer[INET_ADDRSTRLEN]{};
                inet_ntop(
                    address_family,
                    &((struct sockaddr_in*)(ptr_entry->ifa_addr))->sin_addr,
                    buffer,
                    INET_ADDRSTRLEN);

                ipaddress_human_readable_form = std::string(buffer);
            }

            if (ipaddress_human_readable_form == ip) {
                found = true;
                break;
            }
        } else if (address_family == AF_INET6) {
            // IPv6
            if (ptr_entry->ifa_addr != nullptr) {
                char buffer[INET6_ADDRSTRLEN]{};
                inet_ntop(
                    address_family,
                    &((struct sockaddr_in6*)(ptr_entry->ifa_addr))->sin6_addr,
                    buffer,
                    INET6_ADDRSTRLEN);

                ipaddress_human_readable_form = std::string(buffer);
            }

            if (ipaddress_human_readable_form == ip) {
                found = true;
                break;
            }
        } else {
            // AF_UNIX, AF_UNSPEC, AF_PACKET etc.
            // If ignored, delete this section.
        }
    }

    freeifaddrs(ptr_ifaddrs);
    return found;
}

} // namespace utils
} // namespace rdma
