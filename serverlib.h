#ifndef SERVERLIB_H
#define SERVERLIB_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h> 
#include <vector>
#include <unordered_map>
#include <string>


#include "util.h"
#include "debug.h"

#define SEG_SIZE sizeof(struct udp_header)
#define TOPIC_LEN 51
#define UDP_TYPE_LEN sizeof(uint8_t)
#define UDP_DATA_LEN 1500
#define SERVER_IP 127.0.0.1
#define MAX_EVENTS 64

#define COMM_LEN 32
#define EXIT_COMM_LEN sizeof("exit")
#define SUB_COMM_LEN sizeof("subscribe")
#define UNSUB_COMM_LEN sizeof("unsubscribe")

#define EMPTY -1
#define INIT 0
#define SUBSCRIBE 1
#define UNSUBSCRIBE 2
#define EXIT 3
#define RECEIVE 4

#define ID_LEN 11
#define DELIM "/"

typedef struct udp_header {
    char topic[TOPIC_LEN];
    ssize_t topic_len;
    uint8_t type;
    char data[UDP_DATA_LEN];
    ssize_t data_len;
    char ip[INET_ADDRSTRLEN];
    uint16_t port;
} udp_hdr;

typedef struct msg_header {
    void *data;
    ssize_t max_len;
    ssize_t recv_len;
} msg_hdr;

typedef struct command {
    char id[ID_LEN];             // Id of client.
    uint8_t code;                // Code for command to be executed.
    char topic[TOPIC_LEN];       // Topic for sub / unsub.
} command_t;

typedef struct client {
    char id[ID_LEN];
    std::vector<char *> subs;
    bool connected;
    int fd;
} client_t;


int create_tcp_listener(uint16_t port);
int create_udp_listener(uint16_t port);
int epoll_add_fd(int epollfd, int fd, uint32_t flags);
void handle_udp(std::unordered_map<std::string, client_t *> clients, int fd);
void connect_client(int epollfd, int listenfd, std::unordered_map<int, char *> &fd_to_ip);
uint8_t parse_command(char *msg);
void parse_topic(char *msg, char *dest);
void execute_comm(command_t *comm, std::unordered_map<std::string, client_t *> &clients, int epollfd, int clifd, std::unordered_map<int, char *> fd_to_ip);
uint8_t read_host();
client_t *fd_to_cli(std::unordered_map<std::string, client_t *> clients, int fd);
void send_payload(int fd, void *buf, ssize_t len);
int match(char *t, char *s);
command_t *recv_command(int fd);
void server_shutdown(std::unordered_map<std::string, client_t *> clients);

#endif // SERVERLIB_H