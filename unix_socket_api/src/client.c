#define _GNU_SOURCE

#include <generic.h>
#include <logging.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>

#define EXPECTED_ARGC 4

#define PROGNOSE_LIMIT 256

#define CLIENT_CON_NUM 10000

#define MAX_SLEEP_DUR_SEC 3

#define RETRIES 10

enum client_type {
  STAR_CLIENT,
  ORDINARY_CLIENT,
  HARMFUL_CLIENT,
  CLIENT_TYPE_COUNT
};

static int star_client(int con_socket, size_t zodiac_ix) {
  char buf[ASTROLOG_BUF_LEN + 1];
  buf[ASTROLOG_BUF_LEN] = '\0';
  const char *zodiac = NULL;
  size_t pos = 0;
  ssize_t bytes;

  zodiac = get_zodiac(zodiac_ix);
  bytes = (ssize_t)strlen(STAR_REQUEST_TYPE);
  memcpy(buf + pos, STAR_REQUEST_TYPE, bytes);
  pos += (size_t)bytes;

  bytes = (ssize_t)strlen(zodiac);
  memcpy(buf + pos, zodiac, bytes);
  pos += (size_t)bytes;

  buf[pos] = DELIMITER;
  ++pos;

  if (send_all(con_socket, buf, pos, 0)) {
    LOG("Failed to send request");
    return EXIT_FAILURE;
  }

  buf[pos - 1] = '\0';
  LOG("Send request: %s", buf);

  bytes = sprintf(buf, "%d", rand() % PROGNOSE_LIMIT);

  if (bytes < 0) {
    perror("sprintf");
    return EXIT_FAILURE;
  }

  buf[bytes] = DELIMITER;
  pos = bytes + 1;

  if (send_all(con_socket, buf, pos, 0)) {
    LOG("Failed to send prognosis");
    return EXIT_FAILURE;
  }

  buf[pos - 1] = '\0';
  LOG("Send prognosis: %s", buf);

  bytes = recv_line(con_socket, buf, ASTROLOG_BUF_LEN, 0);

  if (bytes <= 0) {
    LOG("Failed to recv reply");
    return EXIT_FAILURE;
  }

  if (bytes == strlen(STAR_REPLY_FAILURE) &&
      !memcmp(buf, STAR_REPLY_FAILURE, strlen(STAR_REPLY_FAILURE))) {
    LOG("Star has been filtered");
  } else {
    buf[bytes - 1] = '\0';
    LOG("Recv reply: %s", buf);
  }

  return EXIT_SUCCESS;
}

static int ordinary_client(int con_socket, size_t zodiac_ix) {
  char buf[ASTROLOG_BUF_LEN + 1];
  buf[ASTROLOG_BUF_LEN] = '\0';
  const char *zodiac = NULL;
  size_t pos = 0;
  ssize_t bytes;

  zodiac = get_zodiac(zodiac_ix);
  bytes = (ssize_t)strlen(CLIENT_REQUEST_TYPE);
  memcpy(buf + pos, CLIENT_REQUEST_TYPE, bytes);
  pos += bytes;

  bytes = (ssize_t)strlen(zodiac);
  memcpy(buf + pos, zodiac, bytes);
  pos += bytes;

  buf[pos] = DELIMITER;
  ++pos;

  if (send_all(con_socket, buf, pos, 0)) {
    LOG("Failed to send request");
    return EXIT_FAILURE;
  }

  buf[pos - 1] = '\0';
  LOG("Send request: %s", buf);

  bytes = recv_line(con_socket, buf, ASTROLOG_BUF_LEN, 0);

  if (bytes <= 0) {
    LOG("Failed to recv reply");
    return EXIT_FAILURE;
  }

  buf[bytes - 1] = '\0';
  LOG("Recv reply: %s", buf);

  return EXIT_SUCCESS;
}

static int star_client_all(in_addr_t addr, in_port_t port) {
  int con_socket;
  struct sockaddr_in server_addr;
  size_t i = 0;
  int ret = EXIT_FAILURE;

  con_socket = socket(AF_INET, SOCK_STREAM, 0);

  if (con_socket < 0) {
    perror("socket");
    goto EXIT_LABEL;
  }

  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = addr;
  server_addr.sin_port = port;

  if (connect(con_socket, (const struct sockaddr *)&server_addr,
              sizeof(server_addr))) {
    perror("connect");
    goto CLOSE_CLIENT_SOCKET;
  }

  for (; i < ZODIAC_NUM; ++i) {
    ret = star_client(con_socket, i);

    if (ret) {
      break;
    }
  }

  if (i == ZODIAC_NUM) {
    LOG("Star work completed");
  }

CLOSE_CLIENT_SOCKET:
  close_fd(con_socket);

EXIT_LABEL:
  return ret;
}

static int ordinary_client_all(in_addr_t addr, in_port_t port) {
  int con_socket;
  struct sockaddr_in server_addr;
  size_t i = 0;
  int ret = EXIT_FAILURE;

  con_socket = socket(AF_INET, SOCK_STREAM, 0);

  if (con_socket < 0) {
    perror("socket");
    goto EXIT_LABEL;
  }

  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = addr;
  server_addr.sin_port = port;

  if (connect(con_socket, (const struct sockaddr *)&server_addr,
              sizeof(server_addr))) {
    perror("connect");
    goto CLOSE_CLIENT_SOCKET;
  }

  for (; i < ZODIAC_NUM; ++i) {
    ret = ordinary_client(con_socket, i);

    if (ret) {
      break;
    }
  }

  if (i == ZODIAC_NUM) {
    LOG("Ordinary work completed");
  }

CLOSE_CLIENT_SOCKET:
  close_fd(con_socket);

EXIT_LABEL:
  return ret;
}

static int harmful_client_child(in_addr_t addr, in_port_t port) {
  int con_socket;
  struct sockaddr_in server_addr;
  int ret = EXIT_FAILURE;
  unsigned int sleep_dur_us;

  con_socket = socket(AF_INET, SOCK_STREAM, 0);

  if (con_socket < 0) {
    perror("socket");
    return ret;
  }

  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = addr;
  server_addr.sin_port = port;

  if (connect(con_socket, (const struct sockaddr *)&server_addr,
              sizeof(server_addr))) {
    perror("connect");
    goto CLOSE_CLIENT_SOCKET;
  }

  for (size_t i = 0; i < RETRIES; ++i) {
    ret = ordinary_client(con_socket, rand() % ZODIAC_NUM);

    if (ret) {
      break;
    }

    sleep_dur_us = (rand() % MAX_SLEEP_DUR_SEC) * SEC_US_FACTOR;
    usleep(sleep_dur_us);
  }

CLOSE_CLIENT_SOCKET:
  close_fd(con_socket);

  return ret;
}

static int harmful_client(const in_addr_t addr, const in_port_t port) {
  int *child_pids = NULL;
  size_t i = 0;
  int ret = EXIT_FAILURE;
  int status;

  child_pids = (int *)malloc(CLIENT_CON_NUM * sizeof(int));

  if (!child_pids) {
    perror("malloc");
    free(child_pids);
    return ret;
  }

  for (; i < CLIENT_CON_NUM; ++i) {
    ret = fork();

    if (ret < 0) {
      perror("fork");
      ret = EXIT_FAILURE;
      break;
    }

    if (ret) {
      LOG("%d: Created child #%zu: %d", getpid(), i, ret);
      child_pids[i] = ret;
    } else {
      LOG("%d: I am child", getpid());
      free(child_pids);
      return harmful_client_child(addr, port);
    }
  }

  if (i == CLIENT_CON_NUM) {
    LOG("%d: Waiting child death", getpid());
    ret = wait(&status);
    LOG("%d: Child %d died with status %d", getpid(), ret, status);
    ret = EXIT_SUCCESS;
  }

  kill_children_procs(child_pids, i);
  free(child_pids);

  return ret;
}

int main(int argc, char *argv[]) {
  in_addr_t addr;
  in_port_t port;
  int type;

  if (argc != EXPECTED_ARGC) {
    LOG("Usage %s: <address> <port> <client_type>", argv[0]);
    return EXIT_FAILURE;
  }

  srand(time(NULL));

  // TODO: check conversion errors
  addr = inet_addr(argv[1]);
  port = htons(strtoul(argv[2], NULL, 10));
  type = strtoul(argv[3], NULL, 10);

  if (type >= CLIENT_TYPE_COUNT) {
    LOG("Wrong client type %d", type);
    return EXIT_FAILURE;
  }

  LOG("Working with addr %s port %s", argv[1], argv[2]);

  switch (type) {
  case STAR_CLIENT:
    return star_client_all(addr, port);
  case ORDINARY_CLIENT:
    return ordinary_client_all(addr, port);
  case HARMFUL_CLIENT:
    return harmful_client(addr, port);
  }
}
