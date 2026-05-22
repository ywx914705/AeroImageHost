#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>

#define MAX_CONNS 500
#define MAX_EVENTS 512
#define REQ_BUF 8192
#define RESP_BUF 16384

typedef struct {
    int fd;
    int state;
    char req[REQ_BUF];
    int req_len;
    int req_sent;
    char resp[RESP_BUF];
    int resp_len;
    struct timespec start;
} conn_t;

static conn_t conns[MAX_CONNS];
static double latencies[200000];
static int lat_count = 0;
static int total_completed = 0;
static int total_errors = 0;
static int total_2xx = 0;
static int total_4xx = 0;
static int total_5xx = 0;
static int target_conns;
static int epoll_fd;
static struct sockaddr_in addr;

static double get_ms(struct timespec *s, struct timespec *e) {
    return (e->tv_sec - s->tv_sec) * 1000.0 + (e->tv_nsec - s->tv_nsec) / 1e6;
}

static void start_conn(int idx) {
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd < 0) return;
    conns[idx].fd = fd;
    conns[idx].state = 0;
    conns[idx].req_sent = 0;
    conns[idx].resp_len = 0;
    clock_gettime(CLOCK_MONOTONIC, &conns[idx].start);
    struct epoll_event ev;
    ev.events = EPOLLOUT;
    ev.data.u32 = idx;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev);
    connect(fd, (struct sockaddr *)&addr, sizeof(addr));
}

static void reset_conn(int idx) {
    if (conns[idx].fd > 0) {
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, conns[idx].fd, NULL);
        close(conns[idx].fd);
    }
    start_conn(idx);
}

static int cmp_double(const void *a, const void *b) {
    double da = *(const double *)a, db = *(const double *)b;
    return (da > db) - (da < db);
}

int main(int argc, char *argv[]) {
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <host> <port> <path> <conns> [duration_s] [header]\n", argv[0]);
        return 1;
    }
    const char *host = argv[1];
    int port = atoi(argv[2]);
    const char *path = argv[3];
    target_conns = atoi(argv[4]);
    if (target_conns > MAX_CONNS) target_conns = MAX_CONNS;
    int duration = argc > 5 ? atoi(argv[5]) : 10;
    const char *extra_hdr = argc > 6 ? argv[6] : "";

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        struct hostent *he = gethostbyname(host);
        if (!he) { fprintf(stderr, "Cannot resolve %s\n", host); return 1; }
        memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
    }

    for (int i = 0; i < target_conns; i++) {
        if (extra_hdr[0])
            snprintf(conns[i].req, REQ_BUF,
                "GET %s HTTP/1.1\r\nHost: %s:%d\r\n%s\r\nConnection: keep-alive\r\n\r\n",
                path, host, port, extra_hdr);
        else
            snprintf(conns[i].req, REQ_BUF,
                "GET %s HTTP/1.1\r\nHost: %s:%d\r\nConnection: keep-alive\r\n\r\n",
                path, host, port);
        conns[i].req_len = strlen(conns[i].req);
        conns[i].fd = -1;
    }

    epoll_fd = epoll_create1(0);
    for (int i = 0; i < target_conns; i++) start_conn(i);

    struct timespec bench_start, now;
    clock_gettime(CLOCK_MONOTONIC, &bench_start);

    while (1) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (get_ms(&bench_start, &now) >= duration * 1000.0) break;

        struct epoll_event events[MAX_EVENTS];
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 100);

        for (int i = 0; i < nfds; i++) {
            int idx = events[i].data.u32;
            if (idx < 0 || idx >= target_conns) continue;
            int fd = conns[idx].fd;

            if (conns[idx].state == 0) {
                int err = 0; socklen_t len = sizeof(err);
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
                if (err) { total_errors++; reset_conn(idx); continue; }
                conns[idx].state = 1;
                conns[idx].req_sent = 0;
                struct epoll_event ev;
                ev.events = EPOLLOUT; ev.data.u32 = idx;
                epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev);
            }
            else if (conns[idx].state == 1) {
                int n = write(fd, conns[idx].req + conns[idx].req_sent,
                              conns[idx].req_len - conns[idx].req_sent);
                if (n <= 0) { total_errors++; reset_conn(idx); continue; }
                conns[idx].req_sent += n;
                if (conns[idx].req_sent >= conns[idx].req_len) {
                    conns[idx].state = 2;
                    conns[idx].resp_len = 0;
                    struct epoll_event ev;
                    ev.events = EPOLLIN; ev.data.u32 = idx;
                    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev);
                }
            }
            else if (conns[idx].state == 2) {
                int n = read(fd, conns[idx].resp + conns[idx].resp_len,
                             RESP_BUF - conns[idx].resp_len - 1);
                if (n <= 0) { total_errors++; reset_conn(idx); continue; }
                conns[idx].resp_len += n;
                conns[idx].resp[conns[idx].resp_len] = 0;

                char *hdr_end = strstr(conns[idx].resp, "\r\n\r\n");
                if (hdr_end) {
                    int hdr_len = hdr_end - conns[idx].resp + 4;
                    int content_len = 0;
                    char *cl = strstr(conns[idx].resp, "Content-Length:");
                    if (!cl) cl = strstr(conns[idx].resp, "content-length:");
                    if (cl) { while(*cl && !isdigit(*cl)) cl++; content_len = atoi(cl); }

                    int http_code = 0;
                    if (conns[idx].resp_len > 9) http_code = atoi(conns[idx].resp + 9);

                    int done = 0;
                    if (content_len > 0 && conns[idx].resp_len >= hdr_len + content_len) done = 1;
                    else if (content_len == 0 && conns[idx].resp_len > hdr_len && (http_code == 204 || http_code == 304)) done = 1;

                    if (done) {
                        struct timespec end;
                        clock_gettime(CLOCK_MONOTONIC, &end);
                        double lat = get_ms(&conns[idx].start, &end);
                        total_completed++;
                        if (http_code >= 200 && http_code < 300) total_2xx++;
                        else if (http_code >= 400 && http_code < 500) total_4xx++;
                        else if (http_code >= 500) total_5xx++;
                        if (lat_count < 200000) latencies[lat_count++] = lat;
                        conns[idx].resp_len = 0;
                        conns[idx].state = 1;
                        conns[idx].req_sent = 0;
                        clock_gettime(CLOCK_MONOTONIC, &conns[idx].start);
                        struct epoll_event ev;
                        ev.events = EPOLLOUT; ev.data.u32 = idx;
                        epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev);
                    }
                }
            }
        }
    }

    for (int i = 0; i < target_conns; i++) {
        if (conns[i].fd > 0) close(conns[i].fd);
    }
    close(epoll_fd);

    double total_sec = duration;
    double qps = total_completed / total_sec;

    printf("REQUESTS:      %d\n", total_completed);
    printf("QPS:           %.0f\n", qps);
    printf("DURATION:      %.1fs\n", total_sec);
    printf("CONNECTIONS:   %d\n", target_conns);
    printf("2xx:           %d\n", total_2xx);
    printf("4xx:           %d\n", total_4xx);
    printf("5xx:           %d\n", total_5xx);
    printf("ERRORS:        %d\n", total_errors);

    if (lat_count > 0) {
        qsort(latencies, lat_count, sizeof(double), cmp_double);
        double avg = 0;
        for (int i = 0; i < lat_count; i++) avg += latencies[i];
        avg /= lat_count;
        printf("LATENCY_AVG:   %.2f\n", avg);
        printf("LATENCY_P50:   %.2f\n", latencies[(int)(lat_count * 0.50)]);
        printf("LATENCY_P90:   %.2f\n", latencies[(int)(lat_count * 0.90)]);
        printf("LATENCY_P95:   %.2f\n", latencies[(int)(lat_count * 0.95)]);
        printf("LATENCY_P99:   %.2f\n", latencies[(int)(lat_count * 0.99)]);
        printf("LATENCY_MAX:   %.2f\n", latencies[lat_count - 1]);
    }

    return 0;
}
