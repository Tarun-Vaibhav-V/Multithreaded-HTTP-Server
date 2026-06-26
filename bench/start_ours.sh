#!/bin/bash
cd /root/projects/http-server
pkill -f 'build/httpserver' 2>/dev/null
sleep 0.3
nohup setsid ./build/httpserver --port 8080 --root ./static --threads "$(nproc)" \
    > /tmp/ours_bench.log 2>&1 < /dev/null &
disown
sleep 1
curl -s -o /dev/null -w 'ours: %{http_code}\n' http://127.0.0.1:8080/bench.html
curl -s -o /dev/null -w 'nginx: %{http_code}\n' http://127.0.0.1:8082/bench.html
