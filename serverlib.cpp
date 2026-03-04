#include "serverlib.h"
#include "debug.h"
#include "util.h"

int create_tcp_listener(uint16_t port) {
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
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    memset(addr.sin_zero, 0, 8);

    // Bind socket to address.
    rc = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
    DIE(rc < 0, "bind error");

    // Listen for connections.
    rc = listen(fd, SOMAXCONN);
    DIE(rc < 0, "listen error");

    return fd;
}

int create_udp_listener(uint16_t port) {
    // Create socket.
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    DIE(fd < 0, "socket error");

    // Set option to re-bind to the same port immediatly after process exit
    int opt = 1;
    int rc = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    DIE(rc < 0, "Error setsockopt");

    // Create server address.
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    memset(addr.sin_zero, 0, 8);

    // Bind socket to address.
    rc = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
    DIE(rc < 0, "bind error");

    return fd;
}

int epoll_add_fd(int epollfd, int fd, uint32_t flags) {
    struct epoll_event ev;

    ev.events = flags;
    ev.data.fd = fd;

    int rc = epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev);
    DIE(rc < 0, "epoll_ctl add error");

    return rc;
}

void handle_udp(std::unordered_map<std::string, client_t *> clients, int fd) {
    // Buff for recv
    char buf[BUFSIZ];

    // For recvfrom
    sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);

    // Recv datagram
    ssize_t len = recvfrom(fd, buf, sizeof(buf), 0, (sockaddr *)&addr, &addr_len);

    // Data to send
    udp_hdr hdr = {0};

    // Fill up header.
    hdr.topic_len = snprintf(hdr.topic, TOPIC_LEN, "%s", buf);
    DIE(hdr.topic_len < 0, "snprintf error");

    hdr.type = *((uint8_t *)(buf + TOPIC_LEN - 1)); // topic len is 51

    ssize_t off = TOPIC_LEN; // 51

    hdr.data_len = len - off;

    memcpy(hdr.data, buf + off, hdr.data_len);

    // AF_INET
    struct sockaddr_in *in = (struct sockaddr_in *)&addr;
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &in->sin_addr, ip, sizeof(ip));

    len = snprintf(hdr.ip, INET_ADDRSTRLEN, "%s", ip);
    DIE(len < 0, "snprintf error");

    hdr.port = ntohs(in->sin_port);

    dlog(LOG_INFO, "Filled in ip %s and port %u\n", hdr.ip, hdr.port);

    // Search for who to send it to
    for (auto &pair : clients) {
        client_t *cli = pair.second;
        if (cli->connected) {
            for (int i = 0; i < cli->subs.size(); i++) {
                if (match(hdr.topic, cli->subs[i])) {
                    send_payload(cli->fd, (void *)&hdr, sizeof(udp_hdr));
                    break;
                }
            }
        }
    }
}

void send_payload(int fd, void *buf, ssize_t len) {
    ssize_t total = 0;
    ssize_t sent = 0;

    do {
        sent = send(fd, (void *)((char *)buf + total) , len - total, 0);

        if (sent == 0) break;
        DIE(sent < 0, "send error");

        total += sent;
    } while (total < len);
}

command_t *recv_command(int fd) {
    char *buf = (char *)calloc(1, sizeof(command_t));
    DIE(buf == NULL, "Bro");
    memset(buf, 0, sizeof(command_t));

    ssize_t total = 0;
    ssize_t len = 0;

    do {
        len = recv(fd, (void *)(buf + total), sizeof(command_t) - total, 0);
        
        if (len == 0) return NULL;
        DIE(len < 0, "recv error");

        total += len;
    } while (total < sizeof(command_t));

    return (command_t *)buf;
}

void execute_comm(command_t *comm, std::unordered_map<std::string, client_t *> &clients, int epollfd, int clifd, std::unordered_map<int, char *> fd_to_ip) {
    uint8_t code = comm->code;

    switch(code) {
        case INIT: {
            if (clients.find(comm->id) == clients.end()) {
                // Client doesn't exist in db.
                client_t *client = (client_t *)calloc(1, sizeof(client_t));

                // Fill client
                client->connected = true;
                int len = snprintf(client->id, ID_LEN, "%s", comm->id);
                DIE(len < 0, "snprintf error");
                client->fd = clifd;

                // Add to db
                clients[client->id] = client;

                // Print of victory
                printf("New client %s connected from %s.\n", comm->id, fd_to_ip[clifd]);
            } else {
                if (!clients[comm->id]->connected) {
                    clients[comm->id]->connected = true;
                    clients[comm->id]->fd = clifd;
                    printf("New client %s connected from %s.\n", comm->id, fd_to_ip[clifd]);

                } else {
                    printf("Client %s already connected.\n", comm->id);
                    close(clifd);
                }
            }

            break;
        }
        case SUBSCRIBE: {
            char *buf = (char *)malloc(TOPIC_LEN);
            int len = snprintf(buf, TOPIC_LEN, "%s", comm->topic);

            clients[comm->id]->subs.push_back(buf);
            break;
        }
        case UNSUBSCRIBE: {
            client_t *c = clients[comm->id];
            char *ptr = NULL;

            for (int i = 0; i < c->subs.size(); i++) {
                if (strcmp(c->subs[i], comm->topic) == 0) {
                    ptr = c->subs[i];
                    c->subs.erase(c->subs.begin() + i);
                    break;
                }
            }

            if (ptr != NULL)
                free(ptr);

            break;
        }
        case EXIT: {
            break;
        }
        default:
            fprintf(stderr, "Unhandled command type!\n");
        break;
    }
}

uint8_t read_host() {
    char buf[BUFSIZ];

    if (fgets(buf, sizeof(buf), stdin) != NULL) {
        // strip trailing newline
        buf[strcspn(buf, "\r\n")] = '\0';

        if (strcmp(buf, "exit") == 0)
            return EXIT;
        else return EMPTY;
    }

    return EMPTY;
}

void connect_client(int epollfd, int listenfd, std::unordered_map<int, char *> &fd_to_ip) {
    int rc;
    struct sockaddr_in cli;
    socklen_t cli_len = sizeof(cli);

    int fd = accept(listenfd, (struct sockaddr *)&cli, &cli_len);
    DIE(fd < 0, "accept error");

    // disable Nagle’s algorithm
    int flag = 1;
    if (setsockopt(fd,
               IPPROTO_TCP,       // at the TCP level
               TCP_NODELAY,       // disable Nagle
               &flag,
               sizeof(flag)) < 0) {
    perror("setsockopt TCP_NODELAY");
    }

    // make the socket non-blocking
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        perror("fcntl F_GETFL");
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("fcntl F_SETFL O_NONBLOCK");
    }

    rc = epoll_add_fd(epollfd, fd, EPOLLIN);

    char ip_char[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &cli.sin_addr, ip_char, sizeof(ip_char));

    char *buf = (char *)malloc(32);
    memset((void *)buf, '\0', 32);

    snprintf(buf, 32, "%s:%u", ip_char, ntohs(cli.sin_port));
    fd_to_ip[fd] = buf;
}

uint8_t parse_command(char *msg) {
    if (msg == NULL) return EMPTY;

    // Place \0 on first space
    msg[strcspn(msg, " ")] = '\0';

    if (strcmp(msg, "subscribe") == 0)
        return SUBSCRIBE;

    if (strcmp(msg, "unsubscribe") == 0)
        return UNSUBSCRIBE;

    if (strcmp(msg, "exit") == 0)
        return EXIT;

    return EMPTY;
}

void parse_topic(char *msg, char *dest) {
    char *p = strchr(msg, '\0');
    p += 1;
    ssize_t len = snprintf(dest, TOPIC_LEN, "%s", p);
    DIE(len < 0, "snprintf error");
}

client_t *fd_to_cli(std::unordered_map<std::string, client_t *> clients, int fd) {
    for (auto &pair : clients)
        if (pair.second->fd == fd)
            return pair.second;
    return NULL;
}

void server_shutdown(std::unordered_map<std::string, client_t *> clients) {
    for (auto &pair : clients)
        if (pair.second->connected) {
            shutdown(pair.second->fd, SHUT_WR);
            close(pair.second->fd);
        }
}

int match(char *t, char *s) {
    if (t == NULL || s == NULL) return -1;

    char tbuf[TOPIC_LEN], sbuf[TOPIC_LEN];
    snprintf(tbuf, TOPIC_LEN, "%s", t);
    snprintf(sbuf, TOPIC_LEN, "%s", s);

    char *save_t, *save_s;
    char *p1 = strtok_r(tbuf, DELIM, &save_t);
    char *p2 = strtok_r(sbuf, DELIM, &save_s);

    while (p2 != NULL) {
        if (strcmp(p2, "*") == 0) {
            char *next = strtok_r(NULL, DELIM, &save_s);
            if (next == NULL) {
                return 1;
            }
            while (p1 && strcmp(p1, next) != 0) {
                p1 = strtok_r(NULL, DELIM, &save_t);
            }
            if (p1 == NULL) {
                return 0;
            }
            p1 = strtok_r(NULL, DELIM, &save_t);
            p2 = strtok_r(NULL, DELIM, &save_s);
        } else if (strcmp(p2, "+") == 0) {
            if (p1 == NULL) return 0;
            p1 = strtok_r(NULL, DELIM, &save_t);
            p2 = strtok_r(NULL, DELIM, &save_s);
        } else {
            if (p1 == NULL || strcmp(p1, p2) != 0) {
                return 0;
            }
            p1 = strtok_r(NULL, DELIM, &save_t);
            p2 = strtok_r(NULL, DELIM, &save_s);
        }
    }

    if (p1 == NULL) {
        return 1;
    } else {
        return 0;
    }
}








