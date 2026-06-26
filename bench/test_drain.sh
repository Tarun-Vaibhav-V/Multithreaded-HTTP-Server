#!/bin/bash
# Verifies graceful shutdown drains an in-flight slow request instead of
# dropping it: start a request, then send SIGTERM mid-flight, and confirm
# the client still gets a full response before the process exits.
set -e
cd "$(dirname "$0")/.."

rm -f /tmp/server.log
nohup setsid ./build/httpserver --port 8081 --root ./static > /tmp/server.log 2>&1 < /dev/null &
disown
sleep 1

# Big file to make the response take a moment to write/transfer.
head -c 5000000 /dev/urandom > ./static/big.bin

(curl -s -o /tmp/curl_out.bin -w 'HTTP_CODE:%{http_code} BYTES:%{size_download}\n' http://127.0.0.1:8081/big.bin > /tmp/curl_result.txt) &
CURL_PID=$!

sleep 0.05
PID=$(ps -C httpserver -o pid= | tr -d ' ')
echo "Sending SIGTERM to PID=$PID while curl (pid $CURL_PID) is in flight"
kill -TERM "$PID"

wait "$CURL_PID"
echo "--- curl result ---"
cat /tmp/curl_result.txt
echo "--- server log ---"
cat /tmp/server.log
rm -f ./static/big.bin
