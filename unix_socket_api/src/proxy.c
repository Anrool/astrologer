#define _GNU_SOURCE

#include <generic.h>
#include <logging.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>

#define EXPECTED_ARGC 4

#define CON_QUEUE_SIZE 5

#define CHILD_PROC_NUM 2

enum disconnect_type { DISCONNECT_ASTROLOG, DISCONNECT_CLIENT };

enum proxy_state_type {
  STATE_RECV_REQUEST,
  STATE_SEND_DENIED,
  STATE_SEND_REQUEST,
  STATE_RECV_REPLY,
  STATE_SEND_REPLY,
  STATE_CLIENT_ERROR,
  STATE_ASTROLOG_ERROR,
  STATE_SUCCESS
};

struct proxy_state {
  int astrolog_fd;
  int con_fd;
  char buf[ASTROLOG_BUF_LEN + 1];
  size_t len;
  enum proxy_state_type state;
};

ATTR_UNUSED static const char *to_string(enum proxy_state_type type) {
  switch (type) {
  case STATE_RECV_REQUEST:
    return "STATE_RECV_REQUEST";
  case STATE_SEND_DENIED:
    return "STATE_SEND_DENIED";
  case STATE_SEND_REQUEST:
    return "STATE_SEND_REQUEST";
  case STATE_RECV_REPLY:
    return "STATE_RECV_REPLY";
  case STATE_SEND_REPLY:
    return "STATE_SEND_REPLY";
  case STATE_CLIENT_ERROR:
    return "STATE_CLIENT_ERROR";
  case STATE_ASTROLOG_ERROR:
    return "STATE_ASTROLOG_ERROR";
  case STATE_SUCCESS:
    return "STATE_SUCCESS";
  default:
    return "?";
  }
}

static void handle_recv_request(struct proxy_state *s) {
  ssize_t bytes;

  bytes = recv_line(s->con_fd, s->buf, ASTROLOG_BUF_LEN, 0);

  if (bytes < 0) {
    s->state = STATE_CLIENT_ERROR;
    return;
  }

  if (bytes == 0) {
    s->state = STATE_SUCCESS;
    return;
  }

  s->buf[bytes - 1] = '\0';

  if ((size_t)bytes > strlen(CLIENT_REQUEST_TYPE) &&
      !memcmp(s->buf, CLIENT_REQUEST_TYPE, strlen(CLIENT_REQUEST_TYPE))) {
    LOG("Recv request: %s", s->buf);
    s->buf[bytes - 1] = DELIMITER;

    s->len = bytes;
    s->state = STATE_SEND_REQUEST;
  } else {
    LOG("Filtering request: %s", s->buf);

    s->len = strlen(STAR_REPLY_FAILURE);
    memcpy(s->buf, STAR_REPLY_FAILURE, s->len);
    s->state = STATE_SEND_DENIED;
  }
}

static void handle_send_request(struct proxy_state *s) {
  if (send_all(s->astrolog_fd, s->buf, s->len, 0)) {
    LOG("Failed to send request");
    s->state = STATE_ASTROLOG_ERROR;
  }

  s->buf[s->len - 1] = '\0';
  LOG("Sent request: %s", s->buf);

  s->len = 0;
  s->state = STATE_RECV_REPLY;
}

static void handle_recv_reply(struct proxy_state *s) {
  ssize_t bytes;

  bytes = recv_line(s->astrolog_fd, s->buf, ASTROLOG_BUF_LEN, 0);

  if (bytes <= 0) {
    LOG("Failed to recv reply");
    s->state = STATE_ASTROLOG_ERROR;
    return;
  }

  s->buf[bytes - 1] = '\0';
  LOG("Recv reply: %s", s->buf);
  s->buf[bytes - 1] = DELIMITER;

  s->len = bytes;
  s->state = STATE_SEND_REPLY;
}

static void handle_send_reply(struct proxy_state *s) {
  if (send_all(s->con_fd, s->buf, s->len, 0)) {
    LOG("Failed to send reply");
    s->state = STATE_CLIENT_ERROR;
    return;
  }

  s->buf[s->len - 1] = '\0';
  LOG("Sent reply: %s", s->buf);

  s->len = 0;
  s->state = STATE_RECV_REQUEST;
}

static enum disconnect_type process_new_connection(int astrolog_fd,
                                                   int con_fd) {
  struct proxy_state s;

  s.astrolog_fd = astrolog_fd;
  s.con_fd = con_fd;
  s.buf[ASTROLOG_BUF_LEN] = '\0';
  s.len = 0;
  s.state = STATE_RECV_REQUEST;

  while (1) {
    switch (s.state) {
    case STATE_RECV_REQUEST:
      handle_recv_request(&s);
      break;
    case STATE_SEND_DENIED:
      handle_send_reply(&s);
      break;
    case STATE_SEND_REQUEST:
      handle_send_request(&s);
      break;
    case STATE_RECV_REPLY:
      handle_recv_reply(&s);
      break;
    case STATE_SEND_REPLY:
      handle_send_reply(&s);
      break;
    case STATE_CLIENT_ERROR:
      LOG("Failed to process client connection");
      return DISCONNECT_CLIENT;
    case STATE_ASTROLOG_ERROR:
      LOG("Failed to process Astrolog connection");
      return DISCONNECT_ASTROLOG;
    case STATE_SUCCESS:
      LOG("Succeeded to process connection");
      return DISCONNECT_CLIENT;
    }
  }
}

static int proxy_child(int astrolog_socket, int listen_socket) {
  int con_socket;
  struct sockaddr_in client_addr;
  socklen_t client_size = sizeof(client_addr);
  enum disconnect_type disc_type;

  while (1) {
    con_socket = accept(listen_socket, (struct sockaddr *)&client_addr,
                        (socklen_t *)&client_size);

    if (con_socket < 0) {
      perror("accept");
      break;
    }

    LOG("%d: Connected to addr %s port %d", getpid(),
        inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

    disc_type = process_new_connection(astrolog_socket, con_socket);
    close_fd(con_socket);

    LOG("%d: Disconnected from addr %s port %d", getpid(),
        inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

    if (disc_type == DISCONNECT_ASTROLOG) {
      break;
    }
  }

  close_fd(listen_socket);
  close_fd(astrolog_socket);

  return 0;
}

int main(int argc, char *argv[]) {
  int ret = EXIT_FAILURE;
  in_addr_t addr;
  in_port_t astrolog_port;
  in_port_t proxy_port;
  int astrolog_socket;
  struct sockaddr_in astrolog_addr;
  int listen_socket;
  int reuseaddr_value = 1;
  struct sockaddr_in proxy_addr;
  int *child_pids = NULL;
  size_t i = 0;
  int status;

  if (argc != EXPECTED_ARGC) {
    LOG("Usage %s: <address> <astrolog_port> <proxy_port>", argv[0]);
    goto EXIT_LABEL;
  }

  // TODO: check conversion errors
  addr = inet_addr(argv[1]);
  astrolog_port = htons(strtoul(argv[2], NULL, 10));
  proxy_port = htons(strtoul(argv[3], NULL, 10));
  astrolog_socket = socket(AF_INET, SOCK_STREAM, 0);

  if (astrolog_socket < 0) {
    perror("socket");
    goto EXIT_LABEL;
  }

  astrolog_addr.sin_family = AF_INET;
  astrolog_addr.sin_addr.s_addr = addr;
  astrolog_addr.sin_port = astrolog_port;

  if (connect(astrolog_socket, (const struct sockaddr *)&astrolog_addr,
              sizeof(astrolog_addr))) {
    perror("connect");
    goto CLOSE_ASTROLOG_SOCKET;
  }

  LOG("%d: Working with Astrolog addr %s port %s", getpid(), argv[1], argv[2]);

  listen_socket = socket(AF_INET, SOCK_STREAM, 0);

  if (listen_socket < 0) {
    perror("socket");
    goto CLOSE_ASTROLOG_SOCKET;
  }

  if (setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR,
                 (const void *)&reuseaddr_value, sizeof(reuseaddr_value))) {
    perror("setsockopt");
    goto CLOSE_LISTEN_SOCKET;
  }

  proxy_addr.sin_family = AF_INET;
  proxy_addr.sin_addr.s_addr = addr;
  proxy_addr.sin_port = proxy_port;

  if (bind(listen_socket, (const struct sockaddr *)&proxy_addr,
           sizeof(proxy_addr))) {
    perror("bind");
    goto CLOSE_LISTEN_SOCKET;
  }

  if (listen(listen_socket, CON_QUEUE_SIZE)) {
    perror("listen");
    goto CLOSE_LISTEN_SOCKET;
  }

  child_pids = (int *)malloc(CHILD_PROC_NUM * sizeof(int));

  if (!child_pids) {
    perror("malloc");
    goto CLOSE_LISTEN_SOCKET;
  }

  for (; i < CHILD_PROC_NUM; i++) {
    ret = fork();

    if (ret < 0) {
      perror("fork");
      break;
    }

    if (ret) {
      LOG("%d: Created child #%zu: %d", getpid(), i, ret);
      child_pids[i] = ret;
    } else {
      LOG("%d: I am child", getpid());
      free(child_pids);
      return proxy_child(astrolog_socket, listen_socket);
    }
  }

  if (i == CHILD_PROC_NUM) {
    LOG("%d: Waiting child death", getpid());
    ret = wait(&status);
    LOG("%d: Child %d died with status %d", getpid(), ret, status);
    ret = EXIT_SUCCESS;
  }

  kill_children_procs(child_pids, i);
  free(child_pids);

CLOSE_LISTEN_SOCKET:
  close_fd(listen_socket);

CLOSE_ASTROLOG_SOCKET:
  close_fd(astrolog_socket);

EXIT_LABEL:
  return ret;
}
