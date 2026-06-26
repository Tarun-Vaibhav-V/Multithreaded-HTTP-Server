#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "connection.h"
#include "router.h"
#include "thread_pool.h"

class Server {
public:
    Server(int port, std::string doc_root, size_t num_threads,
           std::chrono::seconds idle_timeout = std::chrono::seconds(60));
    ~Server();

    // Binds, listens, and runs the epoll loop until request_shutdown() is
    // called (typically from a signal handler) or an unrecoverable error
    // occurs. Blocks the calling thread.
    void run();

    // Signal-safe: only flips an atomic flag. Safe to call from a
    // signal handler.
    void request_shutdown();

private:
    void setup_listen_socket();
    void accept_connections();
    void handle_client_readable(int fd);
    void dispatch_request(int fd);
    void sweep_idle_connections();
    void close_connection(int fd);
    void drain_and_shutdown();

    int port_;
    std::string doc_root_;
    std::chrono::seconds idle_timeout_;

    int listen_fd_ = -1;
    int epoll_fd_ = -1;

    Router router_;
    ThreadPool pool_;

    std::mutex connections_mutex_;
    std::unordered_map<int, std::unique_ptr<Connection>> connections_;

    std::atomic<bool> shutdown_requested_{false};
};
