# TCP/UDP Message Broker

Client-server app for managing messages over TCP and UDP, written in C++.

UDP clients publish messages to a server. The server figures out which TCP subscribers care about each message (based on their topic subscriptions) and forwards it to them. No messages are stored — everything is forwarded on the spot.

## Build & Run

```
make          # produces ./server and ./subscriber
make clean
```

```
./server <PORT>
./subscriber <CLIENT_ID> <SERVER_IP> <SERVER_PORT>
```

`CLIENT_ID` can be up to 10 ASCII characters. Two clients can't be connected with the same ID at the same time — the server rejects the second one. Reconnecting with a previously used ID works fine; the old subscriptions are still there.

## Commands

**Subscriber (stdin):**

- `subscribe <TOPIC>` — subscribe to a topic (can include wildcards)
- `unsubscribe <TOPIC>` — unsubscribe
- `exit` — disconnect

**Server (stdin):**

- `exit` — shuts everything down, closes all TCP clients

## Output format

When a subscriber gets a message, it prints:

```
<UDP_CLIENT_IP>:<UDP_CLIENT_PORT> - <TOPIC> - <DATA_TYPE> - <VALUE>
```

Example: `1.2.3.4:4573 - UPB/precis/1/temperature - SHORT_REAL - 23.50`

## Wildcards

Topics are `/`-separated. Two wildcards are available in subscription patterns:

- `+` — matches one level. `a/+/b` matches `a/x/b` but not `a/x/y/b`.
- `*` — matches any number of levels (including zero). `a/*/b` matches `a/b`, `a/x/b`, `a/x/y/b`.

If a client has overlapping subscriptions that both match an incoming topic, the message is still only delivered once.

### How wildcard matching works

`match()` in `serverlib.cpp` splits both the actual topic and the subscription pattern into tokens on `/` (using `strtok_r`) and walks through them side by side:

- `*` — peek at the next pattern token. If there isn't one, the whole thing matches. Otherwise, skip forward through the topic tokens until that next token is found.
- `+` — advance both pointers by one level.
- anything else — the tokens must be identical, or it's not a match.

Both token lists must be fully consumed for a match to count.

## UDP datagram layout

| Field | Size | Content |
|---|---|---|
| topic | 50 bytes | null-terminated string |
| type | 1 byte | 0 = INT, 1 = SHORT_REAL, 2 = FLOAT, 3 = STRING |
| payload | ≤1500 bytes | depends on type (see below) |

**INT (0):** sign byte (0 or 1) + `uint32_t` in network byte order.

**SHORT_REAL (1):** `uint16_t` in network byte order. Value is the real number × 100.

**FLOAT (2):** sign byte + `uint32_t` in network byte order (the integer and fractional digits glued together) + `uint8_t` power (negative power of 10 to divide by).

**STRING (3):** up to 1500 characters, null-terminated or bounded by the datagram size.

## TCP protocol (application level)

Since TCP is a byte stream, two messages sent back-to-back might arrive merged in a single `recv()`, or one message might show up in pieces. The solution here is fixed-size structs on both directions — sender loops on `send()` until all bytes are out, receiver loops on `recv()` until the expected number of bytes are in.

### Subscriber → Server: `command_t` (63 bytes)

```c
struct command {
    char    id[11];     // client ID
    uint8_t code;       // 0=INIT, 1=SUBSCRIBE, 2=UNSUBSCRIBE, 3=EXIT
    char    topic[51];  // topic (for sub/unsub)
};
```

Right after connecting, the subscriber sends an INIT command with its ID. The server uses this to register the client (or reconnect it if the ID already existed). After that, it's just subscribe/unsubscribe/exit commands.

### Server → Subscriber: `udp_hdr`

```c
struct udp_header {
    char     topic[51];
    ssize_t  topic_len;
    uint8_t  type;
    char     data[1500];
    ssize_t  data_len;
    char     ip[INET_ADDRSTRLEN];
    uint16_t port;
};
```

When a UDP message comes in, the server parses it into this struct, iterates over all connected clients, checks subscription matches, and sends the whole struct over TCP to each matching client. The subscriber side reads exactly `sizeof(udp_hdr)` bytes and then decodes/prints the payload based on the `type` field.

## Implementation details

**I/O multiplexing:** `epoll` on both server and subscriber. The server watches the TCP listener, the UDP socket, and stdin. Each new TCP client fd gets added to epoll after `accept()`. The subscriber watches its server connection and stdin.

**Nagle's algorithm** is turned off (`TCP_NODELAY`) on all TCP sockets — both the listener and each accepted connection. This avoids the latency hit from TCP buffering small writes. It doesn't save you from message framing issues though; the send/recv loops still handle partial transfers.

**Non-blocking sockets:** accepted TCP connections are switched to `O_NONBLOCK` after `accept()` so a stalled client doesn't freeze the server loop.

**Output buffering** is disabled with `setvbuf(stdout, NULL, _IONBF, BUFSIZ)` in both programs.

**Client reconnection:** the server keeps a `std::unordered_map<std::string, client_t*>` of all clients that have ever connected. When a client disconnects, its entry stays (with `connected = false`), so reconnecting with the same ID restores the old subscriptions and just updates the fd.

**Logging:** stderr is redirected to `debug.log` / `debug_client.log` via `dup2`. The `dlog()` macro (from `debug.h`) only produces output when compiled with `-DDEBUG`. The Makefile has this on by default.

## Files

| File | What's in it |
|---|---|
| `server.cpp` | server main loop, epoll event dispatch |
| `serverlib.cpp` / `.h` | socket creation, UDP parsing, client management, command execution, wildcard matching |
| `subscriber.cpp` | subscriber main loop |
| `subscriberlib.cpp` / `.h` | argument parsing, send/recv wrappers, payload interpretation |
| `util.h` | `DIE()` macro |
| `debug.h` | `dlog()` macro |
