/* PBKDF2-style HMAC-SHA-256 loop (4096 iterations, as libpq's scram_SaltedPassword) run by N threads or N processes.
 * Modes: legacy       HMAC_Init_ex(ctx, key, len, EVP_sha256(), NULL) every iteration   (libpq <= 17, OpenSSL 3)
 *        legacy-reuse HMAC_Init_ex(ctx, NULL, 0, NULL, NULL) every iteration            (reuses key and digest)
 *        fetched-md   HMAC_Init_ex(ctx, key, len, md, NULL) with md = EVP_MD_fetch() once
 *        evpmac       EVP_MAC_init(ctx, key, len, params{digest=SHA256}) every iteration (libpq master, 2026-08)
 *        evpmac-reuse EVP_MAC_init(ctx, NULL, 0, NULL) every iteration after one init with params
 */
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/core_names.h>
#include <openssl/params.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

static const char *mode;
static int rounds, iters = 4096;
static unsigned char key[8] = "password";

static double now(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec / 1e9; }

static void work(void)
{
    unsigned char buf[32] = {0}, out[64]; unsigned int len; size_t olen;
    for (int r = 0; r < rounds; r++) {
        if (!strcmp(mode, "legacy")) {
            HMAC_CTX *ctx = HMAC_CTX_new();
            for (int i = 0; i < iters; i++) { HMAC_Init_ex(ctx, key, 8, EVP_sha256(), NULL); HMAC_Update(ctx, buf, 32); HMAC_Final(ctx, out, &len); memcpy(buf, out, 32); }
            HMAC_CTX_free(ctx);
        } else if (!strcmp(mode, "legacy-reuse")) {
            HMAC_CTX *ctx = HMAC_CTX_new(); HMAC_Init_ex(ctx, key, 8, EVP_sha256(), NULL);
            for (int i = 0; i < iters; i++) { HMAC_Init_ex(ctx, NULL, 0, NULL, NULL); HMAC_Update(ctx, buf, 32); HMAC_Final(ctx, out, &len); memcpy(buf, out, 32); }
            HMAC_CTX_free(ctx);
        } else if (!strcmp(mode, "fetched-md")) {
            EVP_MD *md = EVP_MD_fetch(NULL, "SHA256", NULL); HMAC_CTX *ctx = HMAC_CTX_new();
            for (int i = 0; i < iters; i++) { HMAC_Init_ex(ctx, key, 8, md, NULL); HMAC_Update(ctx, buf, 32); HMAC_Final(ctx, out, &len); memcpy(buf, out, 32); }
            HMAC_CTX_free(ctx); EVP_MD_free(md);
        } else if (!strcmp(mode, "evpmac") || !strcmp(mode, "evpmac-reuse")) {
            EVP_MAC *mac = EVP_MAC_fetch(NULL, "HMAC", NULL); EVP_MAC_CTX *ctx = EVP_MAC_CTX_new(mac);
            OSSL_PARAM params[2] = { OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST, (char *) "SHA256", 0), OSSL_PARAM_construct_end() };
            int reuse = !strcmp(mode, "evpmac-reuse");
            if (reuse) EVP_MAC_init(ctx, key, 8, params);
            for (int i = 0; i < iters; i++) { if (reuse) EVP_MAC_init(ctx, NULL, 0, NULL); else EVP_MAC_init(ctx, key, 8, params); EVP_MAC_update(ctx, buf, 32); EVP_MAC_final(ctx, out, &olen, sizeof out); memcpy(buf, out, 32); }
            EVP_MAC_CTX_free(ctx); EVP_MAC_free(mac);
        } else { fprintf(stderr, "unknown mode %s\n", mode); exit(2); }
    }
}

static void *thr(void *a) { (void) a; work(); return NULL; }

int main(int argc, char **argv)
{
    if (argc < 5) { fprintf(stderr, "usage: %s <mode> <n> <rounds> threads|procs\n", argv[0]); return 2; }
    mode = argv[1]; int n = atoi(argv[2]); rounds = atoi(argv[3]); const char *how = argv[4];
    double t = now();
    if (!strcmp(how, "threads")) {
        pthread_t th[128];
        for (int i = 0; i < n; i++) pthread_create(&th[i], NULL, thr, NULL);
        for (int i = 0; i < n; i++) pthread_join(th[i], NULL);
    } else {
        for (int i = 0; i < n; i++) if (fork() == 0) { work(); _exit(0); }
        for (int i = 0; i < n; i++) wait(NULL);
    }
    double dt = now() - t;
    printf("%-13s %-7s n=%-3d %6.0f PBKDF2(4096)/s  (%.2f ms each)\n", mode, how, n, n * rounds / dt, dt * 1000 / rounds);
    return 0;
}
