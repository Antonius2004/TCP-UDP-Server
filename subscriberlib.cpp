#include "subscriberlib.h"

void parse_arguments(char *argv[], char *id, uint32_t *ip,
                     uint16_t *port) {
    
    // Get id.
    int len = snprintf(id, ID_LEN, "%s", argv[1]);

    // Get server ip.
    struct in_addr addr;

    int rc = inet_pton(AF_INET, argv[2], &addr);
    DIE(rc < 0, "inet_pton error");

    *ip = ntohl(addr.s_addr);

    // Get port.
    *port = (uint16_t)atoi(argv[3]);
}

char *handle_user_in() {
    char *buf = (char *)malloc(INPUT_BUF_SIZE);

    // Get input.
    char *rc = fgets(buf, INPUT_BUF_SIZE, stdin);
    DIE(rc == NULL, "fgets error");

    // Strip of newline.
    buf[strcspn(buf, "\r\n")] = '\0';

    return buf;
}

void send_command(int fd, command_t *comm, ssize_t len) {
    // Send buffer.
    ssize_t total = 0;
    while (total < len) {
        ssize_t sent = send(fd, (void *)((char *)comm + total), len - total, 0);
        DIE(sent < 0, "send error");

        total += sent;
    }
}

udp_hdr *handle_server_message(int fd) {
    // Read message
    char *buf = (char *)calloc(1, sizeof(udp_header));
    DIE(buf == NULL, "calloc error");
    memset(buf, 0, sizeof(udp_hdr));

    ssize_t total = 0;
    ssize_t len = 0;

    dlog(LOG_INFO, "Receiving UDP message...\n");

    do {
        len = recv(fd, (void *)((char *)buf + total), sizeof(udp_header) - total, 0);
        dlog(LOG_INFO, "Received %ld bytes\n", len);

        if (len == 0) return NULL;
        DIE(len < 0, "recv error");

        total += len;
    } while (total < sizeof(udp_hdr));

    // Interpret message.
    dlog(LOG_INFO, "Received UDP message of %ld bytes. Should be %ld\n", total, sizeof(udp_header));

    return (udp_hdr *)buf;
}

void send_init_client(int fd, char *id) {
    // Get init command struct.
    command_t *comm = constr_comm(id, INIT, NULL);

    // Send it to server.
    send_command(fd, comm, sizeof(command_t));
}

command_t *constr_comm(char *id, uint8_t code, char *topic) {
    // Init struct.
    command_t *comm = (command_t *)calloc(1, sizeof(command_t));

    // Fill in fields.
    if (id != NULL) {
        int len = snprintf(comm->id, ID_LEN, "%s", id);
        DIE(len < 0, "snprintf error");
    } else {
        fprintf(stderr, "Given NULL id\n");
    }

    comm->code = code;

    if (topic != NULL) {
        int len = snprintf(comm->topic, TOPIC_LEN, "%s", topic);
        DIE(len < 0, "snprintf error");
    }

    // Return struct.
    return comm;
}

void read_udp_header(udp_hdr *hdr) {
    uint8_t type = hdr->type;
    dlog(LOG_INFO, "Received UDP message of type %d\n", type);

    switch (type) {
        case INT: {
            uint8_t *ptr = (uint8_t *)hdr->data;

            uint8_t sign = *ptr;

            uint32_t *nr_ptr = (uint32_t *)((char *)ptr + 1);

            uint32_t nr = ntohl(*nr_ptr);

            int n;
            if (sign == 0)
                n = 1;
            else n = -1;

            signed int ret = n * nr;

            // char topic[TOPIC_LEN];
            // snprintf(topic, TOPIC_LEN, "%s", hdr->topic);

            printf("%s:%hu - %s - INT - %d\n", hdr->ip, ntohs(hdr->port), hdr->topic, ret);
            break;
        }

        case SHORT_REAL: {
            uint16_t *ptr = (uint16_t *)hdr->data;
            uint16_t nr = ntohs(*ptr);

            double ret = (double)nr / 100.0;

            // char topic[TOPIC_LEN];
            // snprintf(topic, TOPIC_LEN, "%s", hdr->topic);

            printf("%s:%hu - %s - SHORT_REAL - %.2f\n",
                   hdr->ip, ntohs(hdr->port), hdr->topic, ret);

            break;
        }

        case FLOAT: {
            // Get sign.
            uint8_t *ptr = (uint8_t *)hdr->data;
            int sign = *ptr;

            // Get number in network order.
            uint32_t *nr_ptr = (uint32_t *)((char *)ptr + 1);
            double nr = ntohl(*nr_ptr);

            // Number of decimal places.
            uint8_t *dec_ptr = (uint8_t *)((char *)ptr + 5);
            int dec = *dec_ptr;
            while(dec > 0) {
                nr /= 10;
                dec--;
            }

            // Get sign.
            double ret = (double)nr;
            if (sign == 1)
                ret = -ret;

            char topic[TOPIC_LEN];
            snprintf(topic, TOPIC_LEN, "%s", hdr->topic);

            // Print.
            printf("%s:%hu - %s - FLOAT - %.*f\n",
                   hdr->ip, ntohs(hdr->port), hdr->topic, *dec_ptr, ret);

            break;
        }
        
        case STRING: {
            // Get string.
            char payload[hdr->data_len + 1];
            snprintf(payload, hdr->data_len + 1, "%s", hdr->data);

            // char topic[TOPIC_LEN];
            // snprintf(topic, TOPIC_LEN - 1, "%s", hdr->topic);

            printf("%s:%hu - %s - STRING - %s\n",
                   hdr->ip, ntohs(hdr->port), hdr->topic, hdr->data);

            break;
        }

        default:
            fprintf(stderr, "Wtf type is this?\n");
        break;
    }
}
