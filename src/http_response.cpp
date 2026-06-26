#include "http_response.h"

#include <sstream>

HttpResponse::HttpResponse(int status_code, std::string reason)
    : status_code_(status_code), reason_(std::move(reason)) {}

void HttpResponse::set_status(int code, std::string reason) {
    status_code_ = code;
    reason_ = std::move(reason);
}

void HttpResponse::set_header(const std::string& key, const std::string& value) {
    headers_[key] = value;
}

void HttpResponse::set_body(std::string body, const std::string& content_type) {
    body_ = std::move(body);
    headers_["Content-Type"] = content_type;
}

std::string HttpResponse::serialize(bool keep_alive) const {
    std::ostringstream out;
    out << "HTTP/1.1 " << status_code_ << " " << reason_ << "\r\n";
    for (const auto& [key, value] : headers_) {
        out << key << ": " << value << "\r\n";
    }
    out << "Content-Length: " << body_.size() << "\r\n";
    out << "Connection: " << (keep_alive ? "keep-alive" : "close") << "\r\n";
    out << "\r\n";
    out << body_;
    return out.str();
}

HttpResponse HttpResponse::make_error(int code, const std::string& reason) {
    HttpResponse resp(code, reason);
    std::string body = std::to_string(code) + " " + reason;
    resp.set_body(body, "text/plain");
    return resp;
}
