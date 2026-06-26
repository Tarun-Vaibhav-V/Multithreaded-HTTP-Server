#pragma once

#include <string>

#include "http_parser.h"
#include "http_response.h"

// Serves static files from a document root, with MIME type detection and
// protection against path traversal escaping the root.
class Router {
public:
    explicit Router(std::string doc_root);

    HttpResponse handle(const HttpRequest& request) const;

private:
    HttpResponse serve_file(const std::string& url_path) const;
    static std::string mime_type_for(const std::string& path);

    // Resolves the request path against doc_root_, rejecting any path that
    // (after normalization) would escape the document root via "..".
    bool resolve_path(const std::string& url_path, std::string& out_fs_path) const;

    std::string doc_root_;
};
