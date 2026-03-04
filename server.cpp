#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "serverlib.h"
#include "debug.h"
#include "util.h"

int main(int argc, char *argv[]) {
    setvbuf(stderr, NULL, _IONBF, 0);
     // Open or create a log file in the current directory
     int logfd = open("debug.log",
        O_CREAT | O_WRONLY | O_TRUNC,
        0644);
    if (logfd >= 0) {
    // Unbuffered so every dlog() line appears immediately
    // Redirect stderr to our file
    if (dup2(logfd, STDERR_FILENO) < 0) {
    perror("dup2");
    // optional: handle error
    }
    close(logfd);
    } else {
    perror("open debug.log");
    // optional: handle error
    }
    int rc;
    dlog(LOG_INFO, "Starting server...\n");

    // Deactivate buffered output.
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);
    
    // Check if a port is provided.
    DIE(argc != 2, "No port specified");

    // Convert the port argument to an unsinged short.
    uint16_t port = (uint16_t)atoi(argv[1]);
    DIE(port < 0, "atoi error");

    // Create sockets and listen.
    int tcp_fd = create_tcp_listener(port);
    int udp_fd = create_udp_listener(port);
    dlog(LOG_INFO, "Server listening for connections on port: %d\n", port);

    // Create epoll for multiplexing.
    int epollfd = epoll_create1(0);
    DIE(epollfd < 0, "epoll_create1 error");

    // Add fds to epoll for multiplexing.
    rc = epoll_add_fd(epollfd, tcp_fd, EPOLLIN);
    DIE(rc < 0, "epoll_add error");

    rc = epoll_add_fd(epollfd, udp_fd, EPOLLIN);
    DIE(rc < 0, "epoll_add error");

    // We also need to add STDIN fd to epoll so it wakes up when we type.
    rc = epoll_add_fd(epollfd, STDIN_FILENO, EPOLLIN);
    DIE(rc < 0, "epoll_add error");

    dlog(LOG_INFO, "Added events to epoll\n");
    
    struct epoll_event events[MAX_EVENTS];

    // Database of clients.
    std::unordered_map<std::string, client_t *> clients;

    // Map <fd, char *>
    std::unordered_map<int, char *> fd_to_ip;

    // Server loop.
    while (1) {
        // Waiting for events infinitely.
        dlog(LOG_INFO, "Waiting for clients or message...\n");
        int ready = epoll_wait(epollfd, events, MAX_EVENTS, -1);
        DIE(ready < 0, "epoll_wait error");

        // Iterate through ready events.
        for (int i = 0; i < ready; i++) {
            struct epoll_event ev = events[i];

            if (ev.data.fd == STDIN_FILENO) {
                // Get command.
                uint8_t code = read_host();

                // Execute command.
                if (code == EXIT) {
                    dlog(LOG_INFO, "Goodbye.\n");
                    // Graceful shutdown.
                    server_shutdown(clients);
                    return 0;
                } else {
                    fprintf(stderr, "Unknown host command!\n");
                    // Next event.
                    continue;
                }
            } else if (ev.data.fd == tcp_fd) {
                // Handle new TCP connection.
                dlog(LOG_INFO, "Got TCP new connection\n");
                connect_client(epollfd, tcp_fd, fd_to_ip);
            } else if (ev.data.fd == udp_fd) {
                // Handle UDP datagram.
                dlog(LOG_INFO, "Got UDP event\n");

                handle_udp(clients, ev.data.fd);
            } else if (ev.events & EPOLLIN) {
                /* We got TCP messages to send or to receive */
                dlog(LOG_INFO, "Receiving TCP message...\n");
                command_t *comm = recv_command(ev.data.fd);

                // End sent EOF.
                if (comm == NULL) {
                    client_t *cli = fd_to_cli(clients, ev.data.fd);
                    if (cli == NULL) {
                        fprintf(stderr, "Couldn't find client by fd in db.\n");
                        continue;
                    }

                    cli->connected = false;
                    rc = shutdown(cli->fd, SHUT_WR);
                    DIE(rc < 0, "shutdown error");
                    close(cli->fd);

                    // Print of shame
                    printf("Client %s disconnected.\n", cli->id);

                    // Go on with next event.
                    continue;
                }
                // Execute command.
                execute_comm(comm, clients, epollfd, ev.data.fd, fd_to_ip);
            } else {
                fprintf(stderr, "Unhandled event!\n");
                return 1;
            }
        }
    }
    return 0;
}
