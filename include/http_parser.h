#pragma once

#include <cstddef>
#include <map>
#include <string>

struct HttpRequest {
    std::string method;
    std::string path;
    std::string version;
    std::map<std::string, std::string> headers; // lower-cased keys
    std::string body;

    bool keep_alive = true;

    std::string header(const std::string& lower_key) const {
        auto it = headers.find(lower_key);
        return it == headers.end() ? std::string() : it->second;
    }
};

// Incremental HTTP/1.1 request parser. Feed it bytes as they arrive from
// the socket (possibly split across many epoll wakeups); it accumulates
// internal state across calls so partial reads are handled correctly.
class HttpParser {
public:
    enum class Status { NeedMore, Complete, Error };

    // Consumes any number of bytes, advancing internal parse state.
    // Returns Complete once a full request (request line + headers + body,
    // per Content-Length) has been parsed; call reset() before parsing
    // the next request on a keep-alive connection.
    Status feed(const char* data, size_t len);

    const HttpRequest& request() const { return request_; }

    // Resets parse state for the next request on a keep-alive connection.
    // Deliberately does NOT clear the internal buffer: if the peer
    // pipelined multiple requests into one TCP read, the bytes for the
    // next request are already sitting in it.
    void reset();

    // Attempts to parse a request out of whatever is already buffered
    // (e.g. pipelined requests read alongside a previous one), without
    // waiting for new socket data. Call after reset() to drain pipelined
    // requests before re-arming epoll for EPOLLIN.
    Status try_parse();

private:
    enum class State { RequestLine, Headers, Body, Done, Error };

    Status parse_buffered();
    bool parse_request_line(const std::string& line);
    bool parse_header_line(const std::string& line);

    static std::string to_lower(std::string s);
    static std::string trim(const std::string& s);

    State state_ = State::RequestLine;
    std::string buffer_;
    size_t content_length_ = 0;

    HttpRequest request_;
};
