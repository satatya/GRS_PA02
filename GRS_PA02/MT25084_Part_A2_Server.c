// MT25084_Part_A2_Server.c
// A2: Multi-client server (one thread per client)
// Usage: ./MT25084_Part_A2_Server <port> <msg_size> <duration_sec> <num_clients>
// Example: ./MT25084_Part_A2_Server 9090 1024 20 4

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    int fd;
    int msg_size;
    int duration;
    struct timespec start_ts;
} worker_arg_t;

static double now_sec_monotonic(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void *client_worker(void *vp) {
    worker_arg_t *arg = (worker_arg_t *)vp;
    int fd = arg->fd;
    int msg_size = arg->msg_size;
    int duration = arg->duration;
    double t0 = (double)arg->start_ts.tv_sec + (double)arg->start_ts.tv_nsec / 1e9;

    // allocate a buffer and keep sending payload to the connected client
    char *buf = (char *)malloc((size_t)msg_size);
    if (!buf) {
        perror("malloc");
        close(fd);
        free(arg);
        return NULL;
    }
    memset(buf, 'A', (size_t)msg_size);

    // reduce chances of SIGPIPE killing thread if client closes
    signal(SIGPIPE, SIG_IGN);

    while (now_sec_monotonic() - t0 < (double)duration) {
        ssize_t sent = 0;
        while (sent < msg_size) {
            ssize_t n = send(fd, buf + sent, (size_t)(msg_size - sent), 0);
            if (n > 0) {
                sent += n;
                continue;
            }
            if (n == 0) {
                // peer closed
                goto done;
            }
            if (errno == EINTR)
                continue;
            // EPIPE/ECONNRESET etc => client went away
            goto done;
        }
    }

done:
    shutdown(fd, SHUT_RDWR);
    close(fd);
    free(buf);
    free(arg);
    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr,
                "Usage: %s <port> <msg_size> <duration_sec> <num_clients>\n",
                argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    int msg_size = atoi(argv[2]);
    int duration = atoi(argv[3]);
    int num_clients = atoi(argv[4]);

    if (port <= 0 || msg_size <= 0 || duration <= 0 || num_clients <= 0) {
        fprintf(stderr, "Invalid arguments.\n");
        return 1;
    }

    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt(SO_REUSEADDR)");
        close(sfd);
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sfd);
        return 1;
    }

    // backlog big enough
    if (listen(sfd, 128) < 0) {
        perror("listen");
        close(sfd);
        return 1;
    }

    printf("[A2 Server] listening on port %d | msg_size=%d | duration=%ds | clients=%d\n",
           port, msg_size, duration, num_clients);
    fflush(stdout);

    pthread_t *tids = (pthread_t *)calloc((size_t)num_clients, sizeof(pthread_t));
    if (!tids) {
        perror("calloc");
        close(sfd);
        return 1;
    }

    // Start time shared for all workers so everyone runs about the same window
    struct timespec start_ts;
    clock_gettime(CLOCK_MONOTONIC, &start_ts);

    // Accept exactly num_clients, then spawn one thread per client
    for (int i = 0; i < num_clients; i++) {
        struct sockaddr_in caddr;
        socklen_t clen = sizeof(caddr);
        int cfd;

        while (1) {
            cfd = accept(sfd, (struct sockaddr *)&caddr, &clen);
            if (cfd >= 0)
                break;
            if (errno == EINTR)
                continue;
            perror("accept");
            // if accept fails, stop early
            num_clients = i;
            goto join_and_exit;
        }

        // IMPORTANT: per-thread heap arg (no &cfd bug)
        worker_arg_t *arg = (worker_arg_t *)malloc(sizeof(worker_arg_t));
        if (!arg) {
            perror("malloc");
            close(cfd);
            num_clients = i;
            goto join_and_exit;
        }
        arg->fd = cfd;
        arg->msg_size = msg_size;
        arg->duration = duration;
        arg->start_ts = start_ts;

        int rc = pthread_create(&tids[i], NULL, client_worker, arg);
        if (rc != 0) {
            fprintf(stderr, "pthread_create failed: %s\n", strerror(rc));
            close(cfd);
            free(arg);
            num_clients = i;
            goto join_and_exit;
        }
    }

join_and_exit:
    // no more accepts needed
    close(sfd);

    for (int i = 0; i < num_clients; i++) {
        if (tids[i]) pthread_join(tids[i], NULL);
    }

    free(tids);
    return 0;
}
