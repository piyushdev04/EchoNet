// TCP Echo Client: connects to server, pipes STDIN to socket and socket to STDOUT.

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <host> <port>\nExample: %s 127.0.0.1 8080\n",
                argv[0], argv[0]);
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);

    // Resolve server
    struct addrinfo hints = {0}, *res = NULL, *rp = NULL;
    hints.ai_family   = AF_UNSPEC; // IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;

    int rc = getaddrinfo(argv[1], argv[2], &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
        return 1;
    }

    int sock = -1;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock == -1) continue;
        if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(sock);
        sock = -1;
    }
    freeaddrinfo(res);

    if (sock == -1) {
        perror("connect");
        return 1;
    }

    fprintf(stderr, "[*] Connected to %s:%s\n", argv[1], argv[2]);

    // Full-duplex: forward stdin->socket and socket->stdout using select()
    for (;;) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        FD_SET(sock, &rfds);
        int nfds = (sock > STDIN_FILENO ? sock : STDIN_FILENO) + 1;

        int ready = select(nfds, &rfds, NULL, NULL, NULL);
        if (ready < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        // Socket -> STDOUT
        if (FD_ISSET(sock, &rfds)) {
            char buf[4096];
            ssize_t n = recv(sock, buf, sizeof(buf), 0);
            if (n == 0) {
                fprintf(stderr, "[-] Server closed connection\n");
                break;
            }
            if (n < 0) {
                if (errno == EINTR) continue;
                perror("recv");
                break;
            }
            ssize_t w = 0;
            while (w < n) {
                ssize_t m = write(STDOUT_FILENO, buf + w, (size_t)(n - w));
                if (m < 0) {
                    if (errno == EINTR) continue;
                    perror("write");
                    break;
                }
                w += m;
            }
        }

        // STDIN -> Socket
        if (FD_ISSET(STDIN_FILENO, &rfds)) {
            char buf[4096];
            ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
            if (n == 0) {
                // EOF from stdin: shutdown write half, keep reading echoes until server closes
                shutdown(sock, SHUT_WR);
                // but continue loop to drain server data
                FD_CLR(STDIN_FILENO, &rfds);
            } else if (n < 0) {
                if (errno == EINTR) continue;
                perror("read");
                break;
            } else {
                ssize_t sent = 0;
                while (sent < n) {
                    ssize_t m = send(sock, buf + sent, (size_t)(n - sent), 0);
                    if (m < 0) {
                        if (errno == EINTR) continue;
                        perror("send");
                        goto done;
                    }
                    sent += m;
                }
            }
        }
    }

done:
    close(sock);
    return 0;
}

