#!/bin/bash
set -e
echo "===== Our server (thread pool + epoll), :8080 ====="
wrk -t4 -c100 -d10s --latency http://127.0.0.1:8080/bench.html
echo
echo "===== nginx baseline, :8082 ====="
wrk -t4 -c100 -d10s --latency http://127.0.0.1:8082/bench.html
