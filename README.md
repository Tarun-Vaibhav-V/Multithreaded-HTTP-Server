# Multithreaded HTTP Server

A from-scratch HTTP/1.1 server in C++17, built around a fixed thread pool
and a single-threaded `epoll` event loop — not one-thread-per-connection.

## Architecture

- **Event loop** ([src/server.cpp](src/server.cpp)): one thread runs
  non-blocking, edge-triggered `epoll`. It accepts connections, reads
  available bytes, and feeds them into a per-connection incremental HTTP
  parser. Once a full request is parsed, the event loop deregisters that
  fd from `epoll` and hands the request off to the thread pool — this
  avoids the event loop and a worker thread touching the same connection
  at the same time.
- **Thread pool** ([include/thread_pool.h](include/thread_pool.h),
  [src/thread_pool.cpp](src/thread_pool.cpp)): fixed-size worker threads
  pulling from a `std::queue` guarded by a mutex and condition variable.
  Workers build the response and write it directly to the socket, then
  re-arm the fd in `epoll` for the next request (keep-alive) or close it.
- **HTTP/1.1 parser** ([include/http_parser.h](include/http_parser.h),
  [src/http_parser.cpp](src/http_parser.cpp)): an incremental state
  machine (request line → headers → body) that can be fed bytes across
  multiple `epoll` wakeups, since a request is not guaranteed to arrive
  in a single `read()`. Supports `GET`/`POST`, headers, `Content-Length`
  bodies, and keep-alive.
- **Static file serving** ([include/router.h](include/router.h),
  [src/router.cpp](src/router.cpp)): serves files from a configurable
  document root with MIME type detection by extension and path-traversal
  protection (`..` is rejected via canonical-path containment checks).
- **Graceful shutdown** ([src/main.cpp](src/main.cpp)): `SIGINT`/`SIGTERM`
  flips an atomic flag (signal-safe — no allocation in the handler). The
  event loop stops accepting new connections, the thread pool drains its
  queue and finishes in-flight requests, then all worker threads are
  joined before the process exits.

## A bug worth mentioning: pipelined requests were silently dropped

`HttpParser` keeps an internal buffer so it can be fed bytes incrementally
across `epoll` wakeups. The first version of `reset()` (called between
requests on a keep-alive connection) unconditionally cleared that buffer.

That's wrong when a client pipelines multiple requests into a single TCP
read — which `wrk` and any HTTP/1.1 keep-alive client routinely do. The
*first* request would parse and get a response, but the bytes for the
second and third requests (already sitting in the buffer from that same
`read()`) were thrown away by `reset()`, and since the event loop had
already moved on, no new `epoll` event would ever fire to deliver them.
The client would just hang waiting for responses that were never coming.

Found via a manual pipelining test (three `GET`s sent back-to-back over
one `nc` connection): only one `HTTP/1.1 200` came back instead of three.

**Fix:** `reset()` now leaves the buffer intact and only resets parser
state; a new `HttpParser::try_parse()` lets the caller attempt to parse
the next request out of whatever's already buffered. The worker loop in
`Server::dispatch_request` calls this after sending each response and
keeps handling requests in a tight loop as long as full requests are
already available, only re-arming `epoll` once the buffer is genuinely
drained. See [src/server.cpp](src/server.cpp) (`dispatch_request`) and
[src/http_parser.cpp](src/http_parser.cpp) (`reset`, `try_parse`).

## Building

Requires a Linux environment (`epoll` is Linux-only) — WSL2 Ubuntu works
fine and is what this was built/tested on.

```bash
sudo apt install build-essential cmake
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

## Running

```bash
./build/httpserver --port 8080 --threads 8 --root ./static --timeout 60
```

| Flag | Default | Meaning |
|---|---|---|
| `--port` | 8080 | Listen port |
| `--threads` | `hardware_concurrency()` | Worker thread pool size |
| `--root` | `./static` | Static file document root |
| `--timeout` | 60 | Idle connection timeout, seconds |

`Ctrl-C` (`SIGINT`) or `SIGTERM` triggers graceful shutdown.

## Benchmarks

See [bench/results.md](bench/results.md) for `wrk` numbers comparing this
server against nginx on the same machine, plus the scripts used to
reproduce them (`bench/compare.sh`, `bench/compare_highconc.sh`).

Headline numbers (8-core WSL2, 100 keep-alive connections, 10s):

| Server | Req/sec | p99 latency |
|---|---|---|
| This server | 35,113 | 5.89ms |
| nginx | 119,527 | 15.42ms |

At 500 connections this server held **zero socket errors** while nginx's
default config (`worker_connections 768`) started timing out — see
[bench/results.md](bench/results.md) for the full breakdown.

## Project layout

```
src/            implementation
include/        headers
static/         files served by the document root
bench/          wrk/nginx comparison scripts and results.md
```
