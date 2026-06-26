#include "server.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <poll.h>
#include <stdexcept>
#include <vector>

namespace {
constexpr int kMaxEvents = 256;
constexpr int kEpollWaitMs = 2000; // also drives idle-timeout sweep cadence
constexpr size_t kReadChunk = 16 * 1024;

void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// Blocks (via poll on just this fd) until all bytes are written or the
// connection errors out. Workers own the write for their own connection,
// so this is safe even though the socket is non-blocking.
bool write_all(int fd, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = ::write(fd, data + sent, len - sent);
        if (n > 0) {
            sent += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            pollfd pfd{fd, POLLOUT, 0};
            if (poll(&pfd, 1, 5000) <= 0) return false; // timed out or error
            continue;
        }
        return false; // real error or peer closed
    }
    return true;
}
} // namespace

Server::Server(int port, std::string doc_root, size_t num_threads,
               std::chrono::seconds idle_timeout)
    : port_(port),
      doc_root_(std::move(doc_root)),
      idle_timeout_(idle_timeout),
      router_(doc_root_),
      pool_(num_threads) {}

Server::~Server() {
    if (listen_fd_ >= 0) close(listen_fd_);
    if (epoll_fd_ >= 0) close(epoll_fd_);
}

void Server::request_shutdown() {
    shutdown_requested_.store(true, std::memory_order_relaxed);
}

void Server::setup_listen_socket() {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) throw std::runtime_error("socket() failed");

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port_));

    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        throw std::runtime_error("bind() failed: " + std::string(strerror(errno)));
    }
    if (listen(listen_fd_, SOMAXCONN) < 0) {
        throw std::runtime_error("listen() failed");
    }
    set_nonblocking(listen_fd_);

    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ < 0) throw std::runtime_error("epoll_create1() failed");

    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = listen_fd_;
    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &ev);
}

void Server::accept_connections() {
    for (;;) {
        sockaddr_in client_addr{};
        socklen_t len = sizeof(client_addr);
        int fd = accept(listen_fd_, reinterpret_cast<sockaddr*>(&client_addr), &len);
        if (fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            if (errno == EINTR) continue;
            return;
        }

        set_nonblocking(fd);
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        auto conn = std::make_unique<Connection>();
        conn->fd = fd;
        conn->touch();

        {
            std::lock_guard<std::mutex> lock(connections_mutex_);
            connections_[fd] = std::move(conn);
        }

        epoll_event ev{};
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = fd;
        epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev);
    }
}

void Server::handle_client_readable(int fd) {
    Connection* conn = nullptr;
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        auto it = connections_.find(fd);
        if (it == connections_.end()) return;
        conn = it->second.get();
    }

    std::array<char, kReadChunk> buf;
    for (;;) {
        ssize_t n = ::read(fd, buf.data(), buf.size());
        if (n > 0) {
            conn->touch();
            auto status = conn->parser.feed(buf.data(), static_cast<size_t>(n));
            if (status == HttpParser::Status::Complete) {
                // Stop watching this fd until the worker has written a
                // response and re-arms it (or closes it). Prevents the
                // event loop and a worker thread from touching the same
                // connection at the same time.
                epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
                dispatch_request(fd);
                return;
            }
            if (status == HttpParser::Status::Error) {
                close_connection(fd);
                return;
            }
            continue; // NeedMore: keep reading until EAGAIN
        }
        if (n == 0) {
            close_connection(fd); // peer closed
            return;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) return; // drained for now
        if (errno == EINTR) continue;
        close_connection(fd);
        return;
    }
}

void Server::dispatch_request(int fd) {
    pool_.submit([this, fd] {
        Connection* conn = nullptr;
        {
            std::lock_guard<std::mutex> lock(connections_mutex_);
            auto it = connections_.find(fd);
            if (it == connections_.end()) return;
            conn = it->second.get();
        }

        // Loop so pipelined requests already sitting in the parser's
        // buffer (read alongside this one) get handled immediately
        // instead of waiting for an epoll event that will never come.
        for (;;) {
            const HttpRequest& req = conn->parser.request();
            HttpResponse resp = router_.handle(req);
            std::string wire = resp.serialize(req.keep_alive);
            bool keep_alive = req.keep_alive;

            if (!write_all(fd, wire.data(), wire.size()) || !keep_alive) {
                close_connection(fd);
                return;
            }

            conn->parser.reset();
            conn->touch();

            auto status = conn->parser.try_parse();
            if (status == HttpParser::Status::Complete) continue;
            if (status == HttpParser::Status::Error) {
                close_connection(fd);
                return;
            }
            break; // NeedMore: re-arm epoll and wait for more bytes
        }

        epoll_event ev{};
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = fd;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
            close_connection(fd);
        }
    });
}

void Server::close_connection(int fd) {
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
    std::lock_guard<std::mutex> lock(connections_mutex_);
    connections_.erase(fd);
}

void Server::sweep_idle_connections() {
    std::vector<int> to_close;
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        for (auto& [fd, conn] : connections_) {
            if (conn->idle_for(idle_timeout_)) to_close.push_back(fd);
        }
    }
    for (int fd : to_close) close_connection(fd);
}

void Server::run() {
    setup_listen_socket();
    std::cout << "Listening on port " << port_ << " with " << "thread pool\n";

    std::array<epoll_event, kMaxEvents> events;

    while (!shutdown_requested_.load(std::memory_order_relaxed)) {
        int n = epoll_wait(epoll_fd_, events.data(), kMaxEvents, kEpollWaitMs);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            uint32_t flags = events[i].events;

            if (fd == listen_fd_) {
                accept_connections();
                continue;
            }
            if (flags & (EPOLLHUP | EPOLLERR)) {
                close_connection(fd);
                continue;
            }
            if (flags & EPOLLIN) {
                handle_client_readable(fd);
            }
        }
        sweep_idle_connections();
    }

    drain_and_shutdown();
}

void Server::drain_and_shutdown() {
    std::cout << "Shutting down: no longer accepting new connections\n";
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, listen_fd_, nullptr);
    close(listen_fd_);
    listen_fd_ = -1;

    std::cout << "Draining thread pool (" << connections_.size()
              << " tracked connections)...\n";
    pool_.shutdown(); // finishes queued/in-flight tasks, joins workers

    std::lock_guard<std::mutex> lock(connections_mutex_);
    for (auto& [fd, conn] : connections_) {
        close(fd);
    }
    connections_.clear();
    std::cout << "Shutdown complete\n";
}
