#!/bin/bash
PID=$(ps -C httpserver -o pid= | tr -d ' ')
echo "PID=$PID"
if [ -z "$PID" ]; then echo "no server running"; exit 1; fi
kill -TERM "$PID"
sleep 1
echo "--- log ---"
cat /tmp/server.log
echo "--- still running? ---"
ps -C httpserver -o pid=,stat= || echo "process exited cleanly"
