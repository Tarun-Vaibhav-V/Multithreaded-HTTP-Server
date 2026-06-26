#include "router.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace fs = std::filesystem;

Router::Router(std::string doc_root) : doc_root_(std::move(doc_root)) {}

bool Router::resolve_path(const std::string& url_path, std::string& out_fs_path) const {
    std::string rel = url_path;
    if (!rel.empty() && rel[0] == '/') rel.erase(0, 1);
    if (rel.empty()) rel = "index.html";

    std::error_code ec;
    fs::path root = fs::weakly_canonical(fs::path(doc_root_), ec);
    if (ec) return false;

    fs::path candidate = fs::weakly_canonical(root / rel, ec);
    if (ec) return false;

    // Reject anything that normalizes outside the document root (".." escapes).
    auto rel_to_root = candidate.lexically_relative(root);
    if (rel_to_root.empty() || rel_to_root.native().substr(0, 2) == "..") return false;

    out_fs_path = candidate.string();
    return true;
}

std::string Router::mime_type_for(const std::string& path) {
    static const std::unordered_map<std::string, std::string> kMimeTypes = {
        {".html", "text/html"},      {".htm", "text/html"},
        {".css", "text/css"},        {".js", "application/javascript"},
        {".json", "application/json"}, {".txt", "text/plain"},
        {".png", "image/png"},       {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},     {".gif", "image/gif"},
        {".svg", "image/svg+xml"},   {".ico", "image/x-icon"},
        {".pdf", "application/pdf"}, {".woff", "font/woff"},
        {".woff2", "font/woff2"},
    };

    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return "application/octet-stream";
    auto it = kMimeTypes.find(path.substr(dot));
    return it == kMimeTypes.end() ? "application/octet-stream" : it->second;
}

HttpResponse Router::serve_file(const std::string& url_path) const {
    std::string fs_path;
    if (!resolve_path(url_path, fs_path)) {
        return HttpResponse::make_error(403, "Forbidden");
    }

    std::error_code ec;
    if (!fs::is_regular_file(fs_path, ec) || ec) {
        return HttpResponse::make_error(404, "Not Found");
    }

    std::ifstream file(fs_path, std::ios::binary);
    if (!file) {
        return HttpResponse::make_error(500, "Internal Server Error");
    }

    std::ostringstream contents;
    contents << file.rdbuf();

    HttpResponse resp(200, "OK");
    resp.set_body(contents.str(), mime_type_for(fs_path));
    return resp;
}

HttpResponse Router::handle(const HttpRequest& request) const {
    if (request.method == "GET" || request.method == "HEAD") {
        return serve_file(request.path);
    }
    if (request.method == "POST") {
        HttpResponse resp(200, "OK");
        std::ostringstream body;
        body << "Received " << request.body.size() << " bytes";
        resp.set_body(body.str(), "text/plain");
        return resp;
    }
    return HttpResponse::make_error(405, "Method Not Allowed");
}
