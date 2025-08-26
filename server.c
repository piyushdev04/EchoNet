// Simple TCP Echo Server: accepts one client at a time and echoes back whatever it receive.

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <netdb.h>    
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>


static volatile sig_atomic_t keep_running = 1;

static void on_sigint(int sig) {
    (void)sig;
    keep_running = 0;
}

static void print_peer(int fd) {
    struct sockaddr_storage addr;
    socklen_t len = sizeof(addr);
    if (getpeername(fd, (struct sockaddr*)&addr, &len) == -1) return;

    char host[NI_MAXHOST], serv[NI_MAXSERV];
    if (getnameinfo((struct sockaddr*)&addr, len, host, sizeof(host),
                    serv, sizeof(serv), NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
        fprintf(stderr, "[+] Client connected: %s:%s\n", host, serv);
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\nExample: %s 8080\n", argv[0], argv[0]);
        return 1;
    }

    struct sigaction sa = {0};
    sa.sa_handler = on_sigint;
    sigaction(SIGINT, &sa, NULL);
    signal(SIGPIPE, SIG_IGN); // avoid crash if client closes early

    // Prepare bind address (IPv6 + IPv4 via v6-mapped)
    struct addrinfo hints = {0}, *res = NULL, *rp = NULL;
    hints.ai_family   = AF_INET6;      // try IPv6 first, also supports IPv4-mapped
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_PASSIVE;    // listen on all interfaces
    int rc = getaddrinfo(NULL, argv[1], &hints, &res);
    if (rc != 0) {
        // fallback to IPv4 if IPv6 not supported
        hints.ai_family = AF_INET;
        rc = getaddrinfo(NULL, argv[1], &hints, &res);
        if (rc != 0) {
            fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
            return 1;
        }
    }

    int listen_fd = -1;
    int v6only = 0;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        listen_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (listen_fd == -1) continue;

        int yes = 1;
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#ifdef IPV6_V6ONLY
        if (rp->ai_family == AF_INET6) {
            setsockopt(listen_fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));
        }
#endif
        if (bind(listen_fd, rp->ai_addr, rp->ai_addrlen) == 0) break;

        close(listen_fd);
        listen_fd = -1;
    }
    freeaddrinfo(res);

    if (listen_fd == -1) {
        perror("bind/socket");
        return 1;
    }

    if (listen(listen_fd, SOMAXCONN) == -1) {
        perror("listen");
        close(listen_fd);
        return 1;
    }

    fprintf(stderr, "[*] Echo server listening on port %s … (Ctrl+C to stop)\n", argv[1]);

    while (keep_running) {
        struct sockaddr_storage cliaddr;
        socklen_t clilen = sizeof(cliaddr);
        int conn_fd = accept(listen_fd, (struct sockaddr*)&cliaddr, &clilen);
        if (conn_fd == -1) {
            if (errno == EINTR && !keep_running) break;
            perror("accept");
            continue;
        }

        print_peer(conn_fd);

        // Echo loop
        char buf[4096];
        for (;;) {
            ssize_t n = recv(conn_fd, buf, sizeof(buf), 0);
            if (n == 0) {
                fprintf(stderr, "[-] Client disconnected\n");
                break; // EOF
            }
            if (n < 0) {
                if (errno == EINTR) continue;
                perror("recv");
                break;
            }
            // Write back all bytes received
            ssize_t sent = 0;
            while (sent < n) {
                ssize_t m = send(conn_fd, buf + sent, (size_t)(n - sent), 0);
                if (m < 0) {
                    if (errno == EINTR) continue;
                    if (errno == EPIPE) {
                        fprintf(stderr, "[-] Client closed during send\n");
                        sent = n; // stop trying
                        break;
                    }
                    perror("send");
                    sent = n;
                    break;
                }
                sent += m;
            }
        }

        close(conn_fd);
    }

    close(listen_fd);
    fprintf(stderr, "[*] Server stopped.\n");
    return 0;
}

