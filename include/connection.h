#pragma once

#include <chrono>
#include <string>

#include "http_parser.h"

// Per-connection state owned by the event loop. Workers read/write the
// parser and write_buffer_ fields while a request for this connection is
// being processed; the event loop only touches a connection between
// dispatching it to the pool and the response being ready (no concurrent
// access to the same Connection from two threads at once).
struct Connection {
    int fd = -1;
    HttpParser parser;
    std::string write_buffer;   // pending bytes to flush to the socket
    size_t write_offset = 0;    // how much of write_buffer has been sent
    bool keep_alive = true;
    bool closing = false;

    std::chrono::steady_clock::time_point last_activity;

    void touch() { last_activity = std::chrono::steady_clock::now(); }

    bool idle_for(std::chrono::seconds timeout) const {
        return std::chrono::steady_clock::now() - last_activity > timeout;
    }
};
