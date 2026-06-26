#pragma once

#include <map>
#include <string>

class HttpResponse {
public:
    explicit HttpResponse(int status_code = 200, std::string reason = "OK");

    void set_status(int code, std::string reason);
    void set_header(const std::string& key, const std::string& value);
    void set_body(std::string body, const std::string& content_type);

    // Serializes status line + headers + body into the wire format,
    // adding Content-Length and Connection automatically.
    std::string serialize(bool keep_alive) const;

    static HttpResponse make_error(int code, const std::string& reason);

private:
    int status_code_;
    std::string reason_;
    std::map<std::string, std::string> headers_;
    std::string body_;
};
