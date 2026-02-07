// MT25084_Part_A3_Server.c
// A3: Multi-client server (one thread per client)
// Uses sendmsg() + MSG_ZEROCOPY if supported, otherwise falls back to send().
// Usage: ./MT25084_Part_A3_Server <port> <msg_size> <duration_sec> <num_clients>

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <linux/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    int fd;
    int msg_size;
    int duration;
    struct timespec start_ts;
    int try_zerocopy;
} worker_arg_t;

static double now_sec_monotonic(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static int send_payload(int fd, const char *buf, int len, int *use_zc) {
    if (*use_zc) {
        struct iovec iov;
        iov.iov_base = (void *)buf;
        iov.iov_len = (size_t)len;

        struct msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;

        ssize_t n = sendmsg(fd, &msg, MSG_ZEROCOPY);
        if (n >= 0) return (int)n;

        // If kernel doesn't support or disallows, fallback permanently
        if (errno == EINVAL || errno == EOPNOTSUPP || errno == ENOTSUP) {
            *use_zc = 0;
        } else if (errno == EPERM) {
            // sometimes restricted: fallback
            *use_zc = 0;
        } else if (errno == EINTR) {
            return -2; // retry
        } else {
            return -1;
        }
    }

    // fallback path
    ssize_t n = send(fd, buf, (size_t)len, 0);
    if (n >= 0) return (int)n;
    if (errno == EINTR) return -2;
    return -1;
}

static void *client_worker(void *vp) {
    worker_arg_t *arg = (worker_arg_t *)vp;
    int fd = arg->fd;
    int msg_size = arg->msg_size;
    int duration = arg->duration;
    int use_zc = arg->try_zerocopy;

    double t0 = (double)arg->start_ts.tv_sec + (double)arg->start_ts.tv_nsec / 1e9;

    char *buf = malloc((size_t)msg_size);
    if (!buf) {
        perror("malloc");
        close(fd);
        free(arg);
        return NULL;
    }
    memset(buf, 'Z', (size_t)msg_size);

    signal(SIGPIPE, SIG_IGN);

    while (now_sec_monotonic() - t0 < (double)duration) {
        int sent = 0;
        while (sent < msg_size) {
            int rc = send_payload(fd, buf + sent, msg_size - sent, &use_zc);
            if (rc > 0) {
                sent += rc;
                continue;
            }
            if (rc == -2) continue; // EINTR retry
            // other errors => stop
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
        fprintf(stderr, "Usage: %s <port> <msg_size> <duration_sec> <num_clients>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    int msg_size = atoi(argv[2]);
    int duration = atoi(argv[3]);
    int num_clients = atoi(argv[4]);

    if (port <= 0 || msg_size <= 0 || duration <= 0 || num_clients <= 0) {
        fprintf(stderr, "Invalid args.\n");
        return 1;
    }

    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

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
    if (listen(sfd, 128) < 0) {
        perror("listen");
        close(sfd);
        return 1;
    }

    printf("[A3 Server] listening on port %d | msg_size=%d | duration=%ds | clients=%d\n",
           port, msg_size, duration, num_clients);
    fflush(stdout);

    pthread_t *tids = calloc((size_t)num_clients, sizeof(pthread_t));
    if (!tids) { perror("calloc"); close(sfd); return 1; }

    struct timespec start_ts;
    clock_gettime(CLOCK_MONOTONIC, &start_ts);

    for (int i = 0; i < num_clients; i++) {
        struct sockaddr_in caddr;
        socklen_t clen = sizeof(caddr);

        int cfd;
        while (1) {
            cfd = accept(sfd, (struct sockaddr *)&caddr, &clen);
            if (cfd >= 0) break;
            if (errno == EINTR) continue;
            perror("accept");
            num_clients = i;
            goto join_and_exit;
        }

        worker_arg_t *arg = malloc(sizeof(*arg));
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
        arg->try_zerocopy = 1;

        int rc = pthread_create(&tids[i], NULL, client_worker, arg);
        if (rc != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(rc));
            close(cfd);
            free(arg);
            num_clients = i;
            goto join_and_exit;
        }
    }

join_and_exit:
    close(sfd);
    for (int i = 0; i < num_clients; i++) {
        if (tids[i]) pthread_join(tids[i], NULL);
    }
    free(tids);
    return 0;
}
