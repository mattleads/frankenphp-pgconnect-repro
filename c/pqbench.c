/* PQconnectdb + PQfinish in a loop, run by N threads or N processes. Prints connects per second. */
#include <libpq-fe.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

static const char *conninfo; static int rounds; static int failed;

static double now(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec / 1e9; }

static void work(void)
{
    for (int i = 0; i < rounds; i++) {
        PGconn *c = PQconnectdb(conninfo);
        if (PQstatus(c) != CONNECTION_OK) { if (!failed++) fprintf(stderr, "connect failed: %s", PQerrorMessage(c)); }
        PQfinish(c);
    }
}
static void *thr(void *a) { (void) a; work(); return NULL; }

int main(int argc, char **argv)
{
    if (argc < 5) { fprintf(stderr, "usage: %s <conninfo> <n> <rounds> threads|procs\n", argv[0]); return 2; }
    conninfo = argv[1]; int n = atoi(argv[2]); rounds = atoi(argv[3]); const char *how = argv[4];
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
    printf("%-7s n=%-3d %6.0f connects/s  (%.2f ms each per worker)%s\n", how, n, n * rounds / dt, dt * 1000 / rounds, failed ? "  [FAILURES]" : "");
    return 0;
}
