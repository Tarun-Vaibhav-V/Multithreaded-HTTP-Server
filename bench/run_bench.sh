#!/bin/bash
# Benchmarks this server against nginx serving the same static file,
# using wrk. Run from the repo root: bash bench/run_bench.sh
set -e
cd "$(dirname "$0")/.."

OURS_PORT=8080
NGINX_PORT=8082
DURATION=10s
THREADS=4
CONNS=100

echo "== Preparing test payload =="
head -c 1024 /dev/urandom | base64 > ./static/bench.html
# nginx doc root: reuse the same static dir.
cat > /etc/nginx/sites-enabled/bench.conf <<EOF
server {
    listen $NGINX_PORT;
    root $(pwd)/static;
    location / { try_files \$uri \$uri/ =404; }
}
EOF
nginx -t
nginx -s reload 2>/dev/null || nginx

echo "== Starting our server on :$OURS_PORT =="
pkill -f "build/httpserver" 2>/dev/null || true
sleep 0.3
nohup setsid ./build/httpserver --port $OURS_PORT --root ./static --threads "$(nproc)" \
    > /tmp/ours_bench.log 2>&1 < /dev/null &
disown
sleep 1

run_wrk() {
    local label=$1 url=$2
    echo
    echo "===== $label ====="
    wrk -t"$THREADS" -c"$CONNS" -d"$DURATION" --latency "$url"
}

run_wrk "Our server (thread pool + epoll)" "http://127.0.0.1:$OURS_PORT/bench.html"
run_wrk "nginx baseline"                    "http://127.0.0.1:$NGINX_PORT/bench.html"

echo
echo "== Cleanup =="
pkill -f "build/httpserver" 2>/dev/null || true
rm -f /etc/nginx/sites-enabled/bench.conf
nginx -s reload 2>/dev/null || true
rm -f ./static/bench.html
