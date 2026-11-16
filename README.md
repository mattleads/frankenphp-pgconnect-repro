# SCRAM-SHA-256 connects serialize across the threads of one libpq process

A PHP application served by FrankenPHP opens a PostgreSQL connection per request. Across 8 threads it gets about 600 connects per second. The same script in 8 separate PHP processes gets about 1,700. With `md5` or `trust` authentication instead of `scram-sha-256`, the 8 threads get about 2,900.

The cost is libpq's client-side PBKDF2: `scram_SaltedPassword()` calls `pg_hmac_init()` 4,096 times per connect, and on OpenSSL 3 each `HMAC_Init_ex(ctx, key, len, EVP_sha256(), NULL)` does an implicit algorithm fetch that takes process-wide locks. Separate processes never contend. Threads of one process do, so a multithreaded libpq client gets single-process connect throughput however many threads it runs.

Everything below was measured on 16 November 2026 on an Apple M3 Max (Docker Desktop 29.4, 16 CPUs, aarch64), containers limited to 8 CPUs, PostgreSQL 16 in a container on the same Docker network.

- FrankenPHP 1.13.5, PHP 8.4.27 ZTS, `dunglas/frankenphp:1-php8.4` (Debian 13), pdo_pgsql and pgsql built by `install-php-extensions`
- libpq 17.12 (`libpq5 17.12-0+deb13u1`), OpenSSL 3.5.7 (`libssl3t64 3.5.7-1~deb13u3`)
- load: `wrk 4.2.0 -t4 -c32`, 6 to 8 seconds per row

## 1. PHP under FrankenPHP: one connect per request

`public/index.php` does one unit of work chosen by `?mode=` and returns. `NUM_THREADS` sets `num_threads` in the Caddyfile.

| mode (8 threads)              | req/s  | avg latency |
|-------------------------------|-------:|------------:|
| none (empty page)             | 41,482 | 1.5 ms |
| cpu (300k-iteration loop)     |  5,583 | 6.0 ms |
| sleep (usleep 4 ms)           |  1,436 | 22.2 ms |
| tcp (fsockopen redis:6379)    |  5,570 | 6.3 ms (16 threads) |
| pgsql (`pg_connect`, SCRAM)   |    543 | 74.8 ms |
| pdo (`new PDO`, SCRAM)        |    585 | 54.5 ms |

Thread scaling of `pdo` against the same server, by authentication method of the `app` role (`pg_hba.conf` of three otherwise identical `postgres:16-alpine` containers):

| threads | scram-sha-256 | md5   | trust |
|--------:|--------------:|------:|------:|
| 1       |   283 req/s   |   732 |   770 |
| 8       |   599         | 2,823 | 2,934 |
| 16      |   286         | 2,511 | 3,188 |

`sleep` on the same thread counts: 173, 1,435 and 2,867 req/s, so the threads themselves scale. Varying the connection did not move the SCRAM row: `sslmode=disable` 587, `gssencmode=disable` 563, both 596, host given as an IP 600.

Baseline with processes instead of threads, same image, same network, same 8-CPU limit: 8 PHP CLI processes each opening and closing 200 connections with `new PDO` reach 1,731 connects/s (`pg_connect`: 1,647).

## 2. C, no PHP: libpq's HMAC loop

`c/hmacbench.c` runs the 4,096-iteration HMAC-SHA-256 loop of `scram_SaltedPassword()` in N threads or N forked processes (`debian:trixie`, gcc, the same OpenSSL 3.5.7). Numbers are loops per second, i.e. "client-side SCRAM proofs per second".

| HMAC pattern                                                    | 1 thread | 8 threads | 16 threads | 8 procs | 16 procs |
|-----------------------------------------------------------------|---------:|----------:|-----------:|--------:|---------:|
| `legacy`: `HMAC_Init_ex(ctx, key, len, EVP_sha256(), NULL)` per iteration (libpq 17) | 519 | **547** | **252** | 3,397 | 3,091 |
| `evpmac`: `EVP_MAC_init(ctx, key, len, params{digest})` per iteration (libpq master since 2026-08) | 600 | **1,191** | **547** | 4,582 | 3,886 |
| `fetched-md`: `HMAC_Init_ex` with an `EVP_MD_fetch()`ed digest    | 1,150 | 5,684 | 9,832 | 6,366 | 11,363 |
| `legacy-reuse`: `HMAC_Init_ex(ctx, NULL, 0, NULL, NULL)` per iteration | 1,997 | 9,272 | 12,095 | 18,557 | 15,565 |
| `evpmac-reuse`: `EVP_MAC_init(ctx, NULL, 0, NULL)` per iteration | 1,085 | [EVPMAC_REUSE_8] | [EVPMAC_REUSE_16] | [EVPMAC_REUSE_P8] | [EVPMAC_REUSE_P16] |

The two patterns libpq uses are the two that do not scale with threads. Keeping the key and digest in the context between iterations removes the per-iteration fetch, is 2 to 4 times faster on one thread, and scales.

## 3. C, no PHP: `PQconnectdb()` in threads vs processes

`c/pqbench.c` opens and closes connections with libpq directly.

[PQBENCH]

## Reproduce

```sh
docker compose -f ../../demo/tenant-shared-demo/compose.yaml up -d database   # or any PostgreSQL with a scram role app/app and database app
docker build -t frankenphp-pgconnect .
docker run -d --name fp --network <network> --cpus 8 -e NUM_THREADS=8 -e PGHOST=<pg host> -p 8790:8080 frankenphp-pgconnect
wrk -t4 -c32 -d8s 'http://127.0.0.1:8790/?mode=pdo'
docker build -t pgconnect-c c/
docker run --rm --cpus 8 pgconnect-c hmacbench legacy 8 30 threads
docker run --rm --cpus 8 pgconnect-c hmacbench legacy 8 30 procs
docker run --rm --cpus 8 --network <network> pgconnect-c pqbench "host=<pg host> user=app password=app dbname=app" 8 60 threads
```

## Related

- pgsql-hackers, February 2025, "Optimize scram_SaltedPassword performance": proposed reusing the HMAC context across iterations (4 ms to 1 ms per proof); dropped after objections about the iteration count being a brute-force defence and about static state. Multithreaded clients were not discussed.
- PostgreSQL commit 1f3b9bb109 (10 August 2026), "Use EVP_MAC for HMAC (OpenSSL >= 3.0)": still re-initialises with the digest parameter on every iteration (the `evpmac` row).
- OpenSSL issue #19612 and BIND 9 MR !7375: implicit fetches in `EVP_DigestInit_ex` take locks; the fix is to fetch once or copy a prepared context.
