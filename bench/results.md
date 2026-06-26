# Benchmark Results

**Environment:** WSL2 Ubuntu 24.04, 8 logical cores, g++ 13.3.0 `-O2`, nginx 1.24.0,
`wrk` 4.1.0. Both servers serve the identical static file
(`static/bench.html`, ~1.4KB), keep-alive connections, same machine,
sequential runs (not concurrent with each other).

## 100 concurrent connections, 10s, 4 wrk threads

| Server | Req/sec | p50 latency | p99 latency | Errors |
|---|---|---|---|---|
| This server (thread pool + epoll) | 35,113 | 3.34ms | 5.89ms | 0 |
| nginx (baseline) | 119,527 | 0.54ms | 15.42ms | 0 |

## 500 concurrent connections, 10s, 8 wrk threads

| Server | Req/sec | p50 latency | p99 latency | Socket errors |
|---|---|---|---|---|
| This server | 60,295 | 7.09ms | 25.67ms | 0 |
| nginx | 101,068 | 2.47ms | 28.50ms | 443 timeouts |

## Takeaways

- This server sustains **35k-60k req/s** on a single 8-core machine with
  zero socket errors at up to 500 concurrent keep-alive connections,
  scaling from a fixed-size thread pool (`hardware_concurrency()` workers)
  driven by a single-threaded epoll event loop.
- nginx is ~2-3x faster in raw throughput, as expected from a decade of
  production-hardened event-loop tuning (multiple worker processes, sendfile,
  zero-copy paths, etc.) — the gap is the honest, expected baseline for a
  from-scratch implementation, not a bug.
- At 500 connections nginx's default config (`worker_connections 768` per
  worker) started timing out some sockets under this load; this server had
  zero errors, suggesting its connection-handling path degrades more
  gracefully near its limits even though peak throughput is lower.

## Reproducing

```bash
# one-time nginx setup (serves the same file from a world-readable path
# since nginx's worker runs as www-data and /root is not traversable by it)
mkdir -p /var/www/bench
cp static/bench.html /var/www/bench/bench.html
chmod -R a+rX /var/www/bench
cp bench/bench.conf /etc/nginx/sites-enabled/bench.conf
nginx -t && nginx -s reload

# start this server
bash bench/start_ours.sh

# run benchmarks
bash bench/compare.sh            # 100 connections
bash bench/compare_highconc.sh   # 500 connections
```
