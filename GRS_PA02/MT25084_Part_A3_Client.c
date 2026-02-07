// MT25084_Part_A3_Client.c
// A3 client: recv loop, prints SUMMARY
// Usage: ./MT25084_Part_A3_Client <server_ip> <port> <msg_size> <duration_sec>

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <server_ip> <port> <msg_size> <duration_sec>\n", argv[0]);
        return 1;
    }

    const char *ip = argv[1];
    int port = atoi(argv[2]);
    int msg_size = atoi(argv[3]);
    int duration = atoi(argv[4]);

    if (port <= 0 || msg_size <= 0 || duration <= 0) {
        fprintf(stderr, "Invalid args.\n");
        return 1;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 1; }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);

    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        fprintf(stderr, "inet_pton failed for %s\n", ip);
        close(fd);
        return 1;
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(fd);
        return 2;
    }

    char *buf = malloc((size_t)msg_size);
    if (!buf) { perror("malloc"); close(fd); return 1; }

    long long total_bytes = 0;
    long long total_msgs = 0;

    double t0 = now_sec();
    while (now_sec() - t0 < (double)duration) {
        ssize_t n = recv(fd, buf, (size_t)msg_size, 0);
        if (n > 0) {
            total_bytes += (long long)n;
            total_msgs += 1;
            continue;
        }
        if (n == 0) break;
        if (errno == EINTR) continue;
        perror("recv");
        break;
    }

    double elapsed = now_sec() - t0;
    double gbps = (elapsed > 0.0) ? ((double)total_bytes * 8.0) / (elapsed * 1e9) : 0.0;
    double avg_oneway_us = (total_msgs > 0) ? (elapsed / (double)total_msgs) * 1e6 : 0.0;

    printf("SUMMARY bytes=%lld seconds=%.6f gbps=%.6f msgs=%lld avg_oneway_us=%.3f\n",
           total_bytes, elapsed, gbps, total_msgs, avg_oneway_us);

    free(buf);
    shutdown(fd, SHUT_RDWR);
    close(fd);
    return 0;
}
