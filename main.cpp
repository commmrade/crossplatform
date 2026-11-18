// Build:
//   Linux/macOS: g++ -std=c++17 -Wall -Wextra http_get.cpp -o http_get
//   Windows MSVC: cl /EHsc http_get.cpp ws2_32.lib
//   Windows MinGW: g++ -std=c++17 http_get.cpp -o http_get -lws2_32

#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #ifdef _MSC_VER
        #pragma comment(lib, "ws2_32.lib")
    #endif
    using socket_t = SOCKET;
    static const socket_t INVALID_SOCK = INVALID_SOCKET;
    #define CLOSE_SOCKET(s) closesocket(s)
    #define SOCK_ERRNO() WSAGetLastError()
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <cerrno>
    using socket_t = int;
    static const socket_t INVALID_SOCK = -1;
    #define CLOSE_SOCKET(s) close(s)
    #define SOCK_ERRNO() errno
#endif

static bool net_init() {
#ifdef _WIN32
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
#else
    return true;
#endif
}

static void net_cleanup() {
#ifdef _WIN32
    WSACleanup();
#endif
}

// Resolves host and connects to the first address that works.
static socket_t connect_to(const char* host, const char* port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;      // IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;  // TCP

    addrinfo* res = nullptr;
    int rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0) {
        std::cerr << "getaddrinfo failed: ";
#ifdef _WIN32
        std::cerr << rc << "\n";
#else
        std::cerr << gai_strerror(rc) << "\n";
#endif
        return INVALID_SOCK;
    }

    socket_t sock = INVALID_SOCK;
    for (addrinfo* p = res; p != nullptr; p = p->ai_next) {
        sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock == INVALID_SOCK) continue;

        if (connect(sock, p->ai_addr, static_cast<int>(p->ai_addrlen)) == 0) break;

        CLOSE_SOCKET(sock);
        sock = INVALID_SOCK;
    }
    freeaddrinfo(res);

    if (sock == INVALID_SOCK) {
        std::cerr << "connect failed, error " << SOCK_ERRNO() << "\n";
    }
    return sock;
}

// send() may write fewer bytes than requested, so loop until everything is sent.
static bool send_all(socket_t sock, const std::string& data) {
    size_t total = 0;
    while (total < data.size()) {
        int n = send(sock, data.data() + total, static_cast<int>(data.size() - total), 0);
        if (n < 0) {
            std::cerr << "send failed, error " << SOCK_ERRNO() << "\n";
            return false;
        }
        total += static_cast<size_t>(n);
    }
    return true;
}

int main() {
    const char* host = "google.com";
    const char* port = "80";

    if (!net_init()) {
        std::cerr << "network init failed\n";
        return 1;
    }

    socket_t sock = connect_to(host, port);
    if (sock == INVALID_SOCK) {
        net_cleanup();
        return 1;
    }

    std::string request =
        "GET / HTTP/1.1\r\n"
        "Host: " + std::string(host) + "\r\n"
        "User-Agent: simple-cpp-client/1.0\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n"
        "\r\n";

    if (!send_all(sock, request)) {
        CLOSE_SOCKET(sock);
        net_cleanup();
        return 1;
    }

    // Read until the server closes the connection (recv returns 0).
    std::string response;
    char buf[4096];
    while (true) {
        int n = recv(sock, buf, sizeof(buf), 0);
        if (n > 0) {
            response.append(buf, static_cast<size_t>(n));
        } else if (n == 0) {
            break;  // connection closed by peer
        } else {
            std::cerr << "recv failed, error " << SOCK_ERRNO() << "\n";
            break;
        }
    }

    std::cout << response << std::endl;

    CLOSE_SOCKET(sock);
    net_cleanup();
    return 0;
}
