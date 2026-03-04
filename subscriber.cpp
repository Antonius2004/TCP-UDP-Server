#include "subscriberlib.h"

int main(int argc, char *argv[]) {
     // Open or create a log file in the current directory
     int logfd = open("debug_client.log",
        O_CREAT | O_WRONLY | O_TRUNC,
        0644);
    if (logfd >= 0) {
    // Unbuffered so every dlog() line appears immediately
    setvbuf(stderr, NULL, _IONBF, 0);
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
    // Deactivate buffered output.
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    if (argc != 4) {
        fprintf(stderr, "Wrong numer of arguments provided!\n");
        return 0;
    }
    
    // Fields.
    char id[ID_LEN];
    uint32_t sv_ip;
    uint16_t sv_port;

    // Parse arguments.
    parse_arguments(argv, id, &sv_ip, &sv_port);

    // Create socket.
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    DIE(fd < 0, "socket error");

    // Set option to re-bind to the same port immediatly after process exit
    int opt = 1;
    int rc = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    DIE(rc < 0, "Error setsockopt");

    // Disable Nagle's algorithm
    rc = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
    DIE(rc < 0, "Error setsockopt TCP_NODELAY");
    
    // Create server address.
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(sv_port);
    addr.sin_addr.s_addr = htonl(sv_ip);
    memset(addr.sin_zero, 0, 8);
    
    // Connect to server.
    rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    DIE (rc < 0, "connect error");

    // Make socket non-blocking after connect
    int flags = fcntl(fd, F_GETFL, 0);
    DIE(flags < 0, "fcntl error");
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    // Send first message to initialize this client on the server.
    send_init_client(fd, id);

    // Create epoll.
    int epollfd = epoll_create1(0);
    DIE(epollfd < 0, "epoll_create1 error");

    // Add fd to epoll for reading from server.
    rc = epoll_add_fd(epollfd, fd, EPOLLIN);
    DIE(rc < 0, "epoll_add error");

    // Add STDIN fd to epoll for reading from host.
    rc = epoll_add_fd(epollfd, STDIN_FILENO, EPOLLIN);
    DIE(rc < 0, "epoll_add error");

    struct epoll_event events[MAX_EVENTS];
    // Client loop.
    while (1) {
        // Waiting for events infinitely.
        int ready = epoll_wait(epollfd, events, MAX_EVENTS, -1);
        DIE(ready < 0, "epoll_wait error");

        // Iterate through ready events.
        for (int i = 0; i < ready; i++) {
            struct epoll_event ev = events[i];

            if (ev.data.fd == STDIN_FILENO) {
                // Get user input.
                char *input = handle_user_in();
                dlog(LOG_INFO, "User input: %s\n", input);

                // Get command code.
                uint8_t code = parse_command(input);
                dlog(LOG_INFO, "Code is %d\n", code);
                if (code == EMPTY) {
                    fprintf(stderr, "Wtf did u type\n");
                    continue;
                }

                // Get topic.
                char topic[TOPIC_LEN];
                if (code != EXIT) {
                    parse_topic(input, topic);
                    dlog(LOG_INFO, "Topic is %s\n", topic);
                }
                
                // Construct command.
                command_t *comm = constr_comm(id, code, topic);

                // Send it to server.
                send_command(fd, comm, sizeof(command_t));

                // If we sent exit we need to close the fd.
                if (code == EXIT) {
                    shutdown(fd, SHUT_WR);
                    close(fd);
                    return 0;
                } else if (code == SUBSCRIBE) {
                    printf("Subscribed to topic %s\n", topic);
                } else if (code == UNSUBSCRIBE) {
                    printf("Unsubscribed from topic %s\n", topic);
                }
            } else if (ev.data.fd == fd) {
                dlog(LOG_INFO, "Receiving TCP message...\n");
                // Handle server message.
                udp_hdr *hdr = handle_server_message(fd);

                if (hdr == NULL) {
                    rc = shutdown(ev.data.fd, SHUT_WR);
                    DIE(rc < 0, "shutdown error");
                    close(ev.data.fd);
                    return 0;
                }

                read_udp_header(hdr);
            }
        }
    }


    return 0;
}