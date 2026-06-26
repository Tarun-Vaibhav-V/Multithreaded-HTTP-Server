#!/bin/bash
set -e
echo "===== Our server, 500 connections ====="
wrk -t8 -c500 -d10s --latency http://127.0.0.1:8080/bench.html
echo
echo "===== nginx, 500 connections ====="
wrk -t8 -c500 -d10s --latency http://127.0.0.1:8082/bench.html
