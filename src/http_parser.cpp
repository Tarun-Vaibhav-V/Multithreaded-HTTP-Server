#include "http_parser.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

std::string HttpParser::to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                    [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::string HttpParser::trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r");
    return s.substr(start, end - start + 1);
}

void HttpParser::reset() {
    state_ = State::RequestLine;
    content_length_ = 0;
    request_ = HttpRequest();
    // buffer_ intentionally left intact -- see header comment.
}

HttpParser::Status HttpParser::feed(const char* data, size_t len) {
    buffer_.append(data, len);
    return parse_buffered();
}

HttpParser::Status HttpParser::try_parse() {
    return parse_buffered();
}

HttpParser::Status HttpParser::parse_buffered() {
    if (state_ == State::Error) return Status::Error;

    while (state_ == State::RequestLine || state_ == State::Headers) {
        size_t pos = buffer_.find("\r\n");
        if (pos == std::string::npos) {
            // Guard against an unbounded header line (no CRLF ever arriving).
            if (buffer_.size() > 8192) {
                state_ = State::Error;
                return Status::Error;
            }
            return Status::NeedMore;
        }

        std::string line = buffer_.substr(0, pos);
        buffer_.erase(0, pos + 2);

        if (state_ == State::RequestLine) {
            if (!parse_request_line(line)) {
                state_ = State::Error;
                return Status::Error;
            }
            state_ = State::Headers;
        } else { // Headers
            if (line.empty()) {
                // Blank line: end of headers.
                std::string cl = request_.header("content-length");
                content_length_ = cl.empty() ? 0 : static_cast<size_t>(std::strtoul(cl.c_str(), nullptr, 10));

                std::string conn = to_lower(request_.header("connection"));
                if (request_.version == "HTTP/1.0") {
                    request_.keep_alive = (conn == "keep-alive");
                } else {
                    request_.keep_alive = (conn != "close");
                }

                state_ = (content_length_ > 0) ? State::Body : State::Done;
                if (state_ == State::Done) return Status::Complete;
            } else if (!parse_header_line(line)) {
                state_ = State::Error;
                return Status::Error;
            }
        }
    }

    if (state_ == State::Body) {
        if (buffer_.size() < content_length_) return Status::NeedMore;
        request_.body = buffer_.substr(0, content_length_);
        buffer_.erase(0, content_length_);
        state_ = State::Done;
        return Status::Complete;
    }

    return Status::NeedMore;
}

bool HttpParser::parse_request_line(const std::string& line) {
    size_t sp1 = line.find(' ');
    if (sp1 == std::string::npos) return false;
    size_t sp2 = line.find(' ', sp1 + 1);
    if (sp2 == std::string::npos) return false;

    request_.method = line.substr(0, sp1);
    request_.path = line.substr(sp1 + 1, sp2 - sp1 - 1);
    request_.version = trim(line.substr(sp2 + 1));

    if (request_.method.empty() || request_.path.empty()) return false;
    if (request_.version != "HTTP/1.1" && request_.version != "HTTP/1.0") return false;

    return true;
}

bool HttpParser::parse_header_line(const std::string& line) {
    size_t colon = line.find(':');
    if (colon == std::string::npos) return false;

    std::string key = to_lower(trim(line.substr(0, colon)));
    std::string value = trim(line.substr(colon + 1));
    if (key.empty()) return false;

    request_.headers[key] = value;
    return true;
}
