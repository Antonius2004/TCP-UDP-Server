#ifndef SUBSCRIBERLIB_H
#define SUBSCRIBERLIB_H

#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
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

#define INT 0
#define SHORT_REAL 1
#define FLOAT 2
#define STRING 3

#define INPUT_BUF_SIZE 1024

void parse_arguments(char *argv[], char *id, uint32_t *ip,
                     uint16_t *port);

char *handle_user_in();
void send_command(int fd, command_t *comm, ssize_t len);
udp_hdr *handle_server_message(int fd);
void send_init_client(int fd, char *id);
command_t *constr_comm(char *id, uint8_t code, char *topic);
void read_udp_header(udp_hdr *hdr);

#endif // SUBSCRIBERLIB_H