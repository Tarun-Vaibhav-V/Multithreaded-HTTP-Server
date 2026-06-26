#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include "server.h"

namespace {
Server* g_server = nullptr;

void handle_signal(int) {
    // Signal-safe: only flips an atomic flag, no allocation/IO here.
    if (g_server) g_server->request_shutdown();
}

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog
              << " [--port N] [--threads N] [--root DIR] [--timeout SECONDS]\n";
}
} // namespace

int main(int argc, char** argv) {
    int port = 8080;
    size_t threads = std::thread::hardware_concurrency();
    if (threads == 0) threads = 4;
    std::string doc_root = "./static";
    long idle_timeout = 60;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) { print_usage(argv[0]); std::exit(1); }
            return argv[++i];
        };

        if (arg == "--port") port = std::atoi(next().c_str());
        else if (arg == "--threads") threads = static_cast<size_t>(std::atoi(next().c_str()));
        else if (arg == "--root") doc_root = next();
        else if (arg == "--timeout") idle_timeout = std::atol(next().c_str());
        else { print_usage(argv[0]); return 1; }
    }

    Server server(port, doc_root, threads, std::chrono::seconds(idle_timeout));
    g_server = &server;

    struct sigaction sa{};
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    signal(SIGPIPE, SIG_IGN); // writes to a closed socket return EPIPE, not a signal

    std::cout << "Starting server: port=" << port << " threads=" << threads
              << " root=" << doc_root << " idle_timeout=" << idle_timeout << "s\n";

    server.run();
    return 0;
}
