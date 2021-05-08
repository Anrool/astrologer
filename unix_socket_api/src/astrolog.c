#define _GNU_SOURCE

#include <binary_heap.h>
#include <generic.h>
#include <logging.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/resource.h>
#include <sys/socket.h>

#define EXPECTED_ARGC 3

#define CLIENT_CON_NUM 10000
#define MAX_NOFILE 2 * CLIENT_CON_NUM

#define CON_QUEUE_SIZE 5

#define RECV_TIMEOUT_SEC (5 * 60)
#define RECV_TIMEOUT_NSEC 0

#define SEND_TIMEOUT_SEC (3 * 60)
#define SEND_TIMEOUT_NSEC 0

enum server_state_type {
  STATE_RECV_FIRST_LINE,
  STATE_RECV_H,
  STATE_SEND_THANKS,
  STATE_SEND_H,
  STATE_ERROR,
  STATE_SUCCESS
};

enum fd_operation { OP_READ, OP_WRITE };

struct server_state {
  struct buffer interm_buf;
  char buf[ASTROLOG_BUF_LEN + 1];
  char *cur_pos;
  ssize_t rem;
  size_t zodiac_ix;
  enum server_state_type state;
  enum fd_operation op;
  struct timespec ts;
  size_t heap_ix;
};

static char zodiac_prognosises[ZODIAC_NUM][ASTROLOG_BUF_LEN + 1];

ATTR_UNUSED static const char *to_string(enum server_state_type type) {
  switch (type) {
  case STATE_RECV_FIRST_LINE:
    return "STATE_RECV_FIRST_LINE";
  case STATE_RECV_H:
    return "STATE_RECV_H";
  case STATE_SEND_THANKS:
    return "STATE_SEND_THANKS";
  case STATE_SEND_H:
    return "STATE_SEND_H";
  case STATE_ERROR:
    return "STATE_ERROR";
  case STATE_SUCCESS:
    return "STATE_SUCCESS";
  default:
    return "?";
  }
}

ATTR_UNUSED static void print(const struct binary_heap *heap) {
  const struct server_state **arr = (const struct server_state **)heap->arr;
  size_t del_ix = 0;

  printf("heap[%zu]:\n", heap->size);

  for (size_t i = 0; i < heap->size; i++) {
    printf("(#%zu: %ld)", i, ACCUM_TIME_NS(arr[i]->ts));

    if (i == del_ix) {
      printf("\n");
      del_ix = 2 * del_ix + 2;
    } else {
      printf(" ");
    }
  }

  if (heap->size - 1 != (del_ix - 2) / 2) {
    printf("\n");
  }
}

static int configure_epoll(int epfd, int op, int fd, uint32_t events,
                           void *ptr) {
  struct epoll_event ep_event;
  ep_event.events = events;
  ep_event.data.ptr = ptr;

  if (epoll_ctl(epfd, op, fd, &ep_event)) {
    perror("epoll_ctl");
    return -1;
  }

  return 0;
}

static void get_cur_time(struct timespec *ts) {
  if (clock_gettime(CLOCK_MONOTONIC, ts)) {
    perror("clock_gettime");
    exit(EXIT_FAILURE);
  }
}

static long get_cur_time_ns() {
  struct timespec ts;

  get_cur_time(&ts);
  return ACCUM_TIME_NS(ts);
}

static void update_ts(struct timespec *ts, enum fd_operation op) {
  get_cur_time(ts);

  if (op == OP_READ) {
    ts->tv_sec += RECV_TIMEOUT_SEC;
    ts->tv_nsec += RECV_TIMEOUT_NSEC;
  } else {
    ts->tv_sec += SEND_TIMEOUT_SEC;
    ts->tv_nsec += SEND_TIMEOUT_NSEC;
  }
}

static int less_ts(const struct timespec *ptr_a, const struct timespec *ptr_b) {
  return ACCUM_TIME_NS(*ptr_a) >= ACCUM_TIME_NS(*ptr_b);
}

static int compare_server_state(const void *lhs, const void *rhs) {
  const struct server_state **p_a = (const struct server_state **)lhs;
  const struct server_state **p_b = (const struct server_state **)rhs;

  return less_ts(&(*p_a)->ts, &(*p_b)->ts);
}

static void swap_server_state(void *a, void *b) {
  struct server_state **elem_a = (struct server_state **)a;
  struct server_state **elem_b = (struct server_state **)b;
  struct server_state *tmp = NULL;
  size_t tmp_heap_ix;

  tmp = *elem_a;
  *elem_a = *elem_b;
  *elem_b = tmp;

  tmp_heap_ix = (*elem_a)->heap_ix;
  (*elem_a)->heap_ix = (*elem_b)->heap_ix;
  (*elem_b)->heap_ix = tmp_heap_ix;
}

static void destroy_server_state(struct server_state *s) { free(s); }

static int create_server_state(int fd, struct binary_heap *heap, int epfd) {
  struct server_state *s = NULL;

  s = (struct server_state *)malloc(sizeof(struct server_state));

  if (!s) {
    perror("malloc");
    goto MALLOC_ERR;
  }

  s->interm_buf.fd = fd;
  s->interm_buf.data[ASTROLOG_BUF_LEN] = '\0';
  s->interm_buf.pos = 0;
  s->interm_buf.len = 0;
  s->buf[ASTROLOG_BUF_LEN] = '\0';
  s->cur_pos = s->buf;
  s->rem = ASTROLOG_BUF_LEN;
  s->zodiac_ix = ZODIAC_UNKOWN;
  s->state = STATE_RECV_FIRST_LINE;
  s->op = OP_READ;
  s->heap_ix = heap->size;

  update_ts(&s->ts, s->op);

  if (binary_heap_push(heap, &s)) {
    goto HEAP_PUSH_ERR;
  }

  if (configure_epoll(epfd, EPOLL_CTL_ADD, fd, EPOLLIN, s)) {
    perror("epoll_ctl add");
    goto EPOLL_ADD_ERR;
  }

  return 0;

EPOLL_ADD_ERR:
  if (binary_heap_remove(heap, s->heap_ix, &s)) {
    LOG("Failed to remove heap ix %zu", s->heap_ix);
  }

HEAP_PUSH_ERR:
  destroy_server_state(s);

MALLOC_ERR:
  return -1;
}

static void update_server_state_ts(struct server_state *s,
                                   struct binary_heap *heap) {
  update_ts(&s->ts, s->op);
  binary_heap_update(heap, s->heap_ix);
}

static void remove_server_state(struct server_state *s,
                                struct binary_heap *heap, int epfd) {
  const int fd = s->interm_buf.fd;

  (void)configure_epoll(epfd, EPOLL_CTL_DEL, fd, 0, NULL);

  if (binary_heap_remove(heap, s->heap_ix, &s)) {
    LOG("Failed to remove heap ix %zu", s->heap_ix);
  }

  destroy_server_state(s);
  close_fd(fd);
}

static void parse_star_first_line(struct server_state *s,
                                  struct binary_heap *heap) {
  const char *ptr = s->buf + strlen(STAR_REQUEST_TYPE);
  char *del_ptr = memchr(ptr, DELIMITER, s->cur_pos - ptr);
  ssize_t zodiac_ix;
  const char *zodiac = NULL;

  zodiac_ix = find_zodiac(ptr, del_ptr - ptr);

  if (zodiac_ix < 0) {
    s->state = STATE_ERROR;

    *del_ptr = '\0';
    LOG("Zodiac %s not found", ptr);
    return;
  }

  zodiac = get_zodiac(zodiac_ix);
  ptr += strlen(zodiac);

  LOG("Star is setting zodiac %s", zodiac);

  // has padding
  if (ptr != del_ptr) {
    *del_ptr = '\0';
    LOG("Request remainder %s", ptr);
  }

  s->cur_pos = s->buf;
  s->rem = ASTROLOG_BUF_LEN;
  s->zodiac_ix = zodiac_ix;
  s->state = STATE_RECV_H;
  s->op = OP_READ;

  update_server_state_ts(s, heap);
}

static void parse_client_first_line(struct server_state *s,
                                    struct binary_heap *heap) {
  char *ptr = s->buf + strlen(CLIENT_REQUEST_TYPE);
  char *del_ptr = memchr(ptr, DELIMITER, s->cur_pos - ptr);
  ssize_t zodiac_ix;
  const char *zodiac = NULL;

  zodiac_ix = find_zodiac(ptr, del_ptr - ptr);

  if (zodiac_ix < 0) {
    s->state = STATE_ERROR;

    *del_ptr = '\0';
    LOG("Zodiac %s not found", ptr);
    return;
  }

  zodiac = get_zodiac(zodiac_ix);
  ptr += strlen(zodiac);

  LOG("Client requests zodiac %s", zodiac);

  // has padding
  if (ptr != del_ptr) {
    *del_ptr = '\0';
    LOG("Request remainder %s", ptr);
  }

  ptr = zodiac_prognosises[zodiac_ix];
  del_ptr = memchr(ptr, DELIMITER, ASTROLOG_BUF_LEN);

  // prognosis is not set yet
  if (!del_ptr) {
    s->rem = strlen(CLIENT_REPLY_FAILURE);
    memcpy(s->buf, CLIENT_REPLY_FAILURE, s->rem);

    LOG("No prognosis for zodiac %s", zodiac);
  } else {
    s->rem = del_ptr + 1 - ptr;
    memcpy(s->buf, ptr, s->rem);

    LOG("Has prognosis for zodiac %s", zodiac);
  }

  s->cur_pos = s->buf;
  s->state = STATE_SEND_H;
  s->op = OP_WRITE;

  update_server_state_ts(s, heap);
}

static void handle_first_line(struct server_state *s,
                              struct binary_heap *heap) {
  ssize_t received;
  size_t len;

  received =
      buffered_recv_until_delim(&s->interm_buf, s->cur_pos, s->rem, DELIMITER);

  if (received < 0) {
    s->state = STATE_ERROR;
    perror("recv");
    return;
  }

  if (received == 0) {
    s->state = s->cur_pos == s->buf ? STATE_SUCCESS : STATE_ERROR;
    return;
  }

  s->cur_pos += received;
  s->rem -= received;
  len = ASTROLOG_BUF_LEN - s->rem;

  if (DELIMITER == s->cur_pos[-1]) {
    if (len > strlen(STAR_REQUEST_TYPE) &&
        !memcmp(s->buf, STAR_REQUEST_TYPE, strlen(STAR_REQUEST_TYPE))) {
      parse_star_first_line(s, heap);
    } else if (len > strlen(CLIENT_REQUEST_TYPE) &&
               !memcmp(s->buf, CLIENT_REQUEST_TYPE,
                       strlen(CLIENT_REQUEST_TYPE))) {
      parse_client_first_line(s, heap);
    } else {
      s->state = STATE_ERROR;

      s->cur_pos[-1] = '\0';
      LOG("Recv bad request type: %s", s->buf);
    }
  } else if (s->rem == 0) {
    s->state = STATE_ERROR;

    LOG("Recv first line without delimiter: %s", s->buf);
  }
}

static void handle_recv_h(struct server_state *s, struct binary_heap *heap) {
  ssize_t received;

  received =
      buffered_recv_until_delim(&s->interm_buf, s->cur_pos, s->rem, DELIMITER);

  if (received < 0) {
    s->state = STATE_ERROR;
    perror("recv");
    return;
  }

  if (received == 0) {
    s->state = STATE_ERROR;
    return;
  }

  s->cur_pos += received;
  s->rem -= received;

  if (DELIMITER == s->cur_pos[-1]) {
    memcpy(zodiac_prognosises[s->zodiac_ix], s->buf, ASTROLOG_BUF_LEN - s->rem);

    s->cur_pos[-1] = '\0';
    LOG("Recv prognosis: %s", s->buf);

    s->rem = strlen(STAR_REPLY_SUCCESS);
    memcpy(s->buf, STAR_REPLY_SUCCESS, s->rem);
    s->cur_pos = s->buf;
    s->zodiac_ix = ZODIAC_UNKOWN;
    s->state = STATE_SEND_THANKS;
    s->op = OP_WRITE;

    update_server_state_ts(s, heap);
  } else if (s->rem == 0) {
    s->state = STATE_ERROR;

    LOG("Recv prognosis without delimiter: %s", s->buf);
  }
}

static void handle_send_reply(struct server_state *s,
                              struct binary_heap *heap) {
  ssize_t bytes;

  bytes = send(s->interm_buf.fd, s->cur_pos, s->rem, 0);

  if (bytes < 0) {
    s->state = STATE_ERROR;
    perror("send");
    return;
  }

  s->cur_pos += bytes;
  s->rem -= bytes;

  if (s->rem == 0) {
    s->cur_pos[-1] = '\0';
    LOG("Sent reply: %s", s->buf);

    s->cur_pos = s->buf;
    s->rem = ASTROLOG_BUF_LEN;
    s->state = STATE_RECV_FIRST_LINE;
    s->op = OP_READ;

    update_server_state_ts(s, heap);
  }
}

static int update_state(struct server_state *s, struct binary_heap *heap,
                        int epfd) {
  const enum fd_operation init_op = s->op;

  do {
    switch (s->state) {
    case STATE_RECV_FIRST_LINE:
      handle_first_line(s, heap);
      break;
    case STATE_RECV_H:
      handle_recv_h(s, heap);
      break;
    case STATE_SEND_THANKS:
      handle_send_reply(s, heap);
      break;
    case STATE_SEND_H:
      handle_send_reply(s, heap);
      break;
    default:
      break;
    }
  } while (s->op == OP_READ && s->interm_buf.pos != s->interm_buf.len);

  if (s->state == STATE_ERROR || s->state == STATE_SUCCESS) {
    LOG("%s to process connection",
        s->state == STATE_ERROR ? "Failed" : "Succeeded");

    remove_server_state(s, heap, epfd);
    return 0;
  }

  if (init_op != s->op) {
    if (configure_epoll(epfd, EPOLL_CTL_MOD, s->interm_buf.fd,
                        s->op == OP_READ ? EPOLLIN : EPOLLOUT, s)) {
      perror("epoll_ctl mod");
      return -1;
    }
  }

  return 0;
}

static int change_nofile_rlimit(rlim_t limit) {
  struct rlimit r_lim;
  int resource = RLIMIT_NOFILE;

  r_lim.rlim_cur = limit;
  r_lim.rlim_max = limit;

  if (setrlimit(resource, &r_lim)) {
    perror("setrlimit");
    return -1;
  }

  return 0;
}

static int min_timeout(struct binary_heap *heap) {
  struct server_state **p_elem = NULL;
  long cur_ts_ns;
  long min_ts_ns;

  cur_ts_ns = get_cur_time_ns();

  if (!binary_heap_top(heap, (const void **)&p_elem)) {
    min_ts_ns = ACCUM_TIME_NS((*p_elem)->ts);

    if (min_ts_ns <= cur_ts_ns) {
      return 0;
    } else {
      return (min_ts_ns - cur_ts_ns) / SEC_US_FACTOR;
    }
  }

  return -1;
}

static int process_new_connection(int listen_socket, struct binary_heap *heap,
                                  int epfd) {
  int ret = -1;
  struct sockaddr_in client_addr;
  socklen_t client_size = sizeof(client_addr);
  int con_socket;

  con_socket = accept4(listen_socket, (struct sockaddr *)&client_addr,
                       &client_size, SOCK_NONBLOCK);

  if (con_socket < 0) {
    perror("accept4");
    goto ERROR;
  }

  if (heap->size >= heap->capacity) {
    LOG("Rejected addr %s port %d, reached connection limit %zu",
        inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port),
        heap->capacity);

    ret = 0;
    goto CLOSE_CONNECTION;
  } else if (create_server_state(con_socket, heap, epfd)) {
    goto CLOSE_CONNECTION;
  } else {
    LOG("Connected to addr %s port %d", inet_ntoa(client_addr.sin_addr),
        ntohs(client_addr.sin_port));
  }

  return 0;

CLOSE_CONNECTION:
  close_fd(con_socket);

ERROR:
  return ret;
}

static void discard_expired_connections(struct binary_heap *heap, int epfd) {
  struct server_state *elem = NULL;
  struct server_state **p_elem = NULL;
  long cur_ts_ns;

  cur_ts_ns = get_cur_time_ns();

  while (!binary_heap_top(heap, (const void **)&p_elem) &&
         ACCUM_TIME_NS((*p_elem)->ts) <= cur_ts_ns) {
    elem = *p_elem;
    elem->state = STATE_ERROR;
    (void)update_state(elem, heap, epfd);
  }
}

int main(int argc, char *argv[]) {
  int ret = EXIT_FAILURE;
  in_addr_t addr;
  in_port_t port;
  int listen_socket;
  int reuseaddr_value = 1;
  struct sockaddr_in astrolog_addr;
  int epfd;
  struct epoll_event *ep_event_arr = NULL;
  struct binary_heap heap;
  struct server_state *elem = NULL;
  int ready_op_num;

  if (argc != EXPECTED_ARGC) {
    LOG("Usage %s: <address> <port>", argv[0]);
    goto EXIT_LABEL;
  }

  if (change_nofile_rlimit(MAX_NOFILE)) {
    goto EXIT_LABEL;
  }

  LOG("Connection limit: %d", CLIENT_CON_NUM);

  // TODO: check conversion errors
  addr = inet_addr(argv[1]);
  port = htons(strtoul(argv[2], NULL, 10));
  listen_socket = socket(AF_INET, SOCK_STREAM, 0);

  if (listen_socket < 0) {
    perror("socket");
    goto EXIT_LABEL;
  }

  if (setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR,
                 (const void *)&reuseaddr_value, sizeof(reuseaddr_value))) {
    perror("setsockopt");
    goto CLOSE_LISTEN_SOCKET;
  }

  astrolog_addr.sin_family = AF_INET;
  astrolog_addr.sin_addr.s_addr = addr;
  astrolog_addr.sin_port = port;

  if (bind(listen_socket, (const struct sockaddr *)&astrolog_addr,
           sizeof(astrolog_addr))) {
    perror("bind");
    goto CLOSE_LISTEN_SOCKET;
  }

  if (listen(listen_socket, CON_QUEUE_SIZE)) {
    perror("listen");
    goto CLOSE_LISTEN_SOCKET;
  }

  epfd = epoll_create1(0);

  if (epfd < 0) {
    perror("epoll_create1");
    goto CLOSE_LISTEN_SOCKET;
  }

  if (configure_epoll(epfd, EPOLL_CTL_ADD, listen_socket, EPOLLIN, NULL)) {
    perror("epoll_ctl add");
    goto CLOSE_EPOLL_INSTANCE;
  }

  ep_event_arr =
      (struct epoll_event *)malloc(sizeof(struct epoll_event) * CLIENT_CON_NUM);

  if (!ep_event_arr) {
    perror("malloc");
    goto CLOSE_EPOLL_INSTANCE;
  }

  if (binary_heap_allocate(&heap, CLIENT_CON_NUM, sizeof(struct server_state *),
                           &compare_server_state, &swap_server_state)) {
    goto FREE_EPOLL_ARRAY;
  }

  while (1) {
    ready_op_num =
        epoll_wait(epfd, ep_event_arr, CLIENT_CON_NUM, min_timeout(&heap));

    if (ready_op_num < 0) {
      perror("epoll_wait");
      goto FREE_HEAP_ELEMS;
    }

    if (ready_op_num == 0) {
      LOG("Wait timeout expired");

      discard_expired_connections(&heap, epfd);
      continue;
    }

    for (size_t i = 0; i < (size_t)ready_op_num; i++) {
      elem = (struct server_state *)ep_event_arr[i].data.ptr;

      if (!elem) {
        if (process_new_connection(listen_socket, &heap, epfd)) {
          goto FREE_HEAP_ELEMS;
        }
        continue;
      }

      if (update_state(elem, &heap, epfd)) {
        goto FREE_HEAP_ELEMS;
      }
    }
  }

  ret = EXIT_SUCCESS;

FREE_HEAP_ELEMS:
  while (heap.size > 0) {
    elem = ((struct server_state **)heap.arr)[heap.size - 1];
    remove_server_state(elem, &heap, epfd);
  }

  binary_heap_free(&heap);

FREE_EPOLL_ARRAY:
  free(ep_event_arr);

CLOSE_EPOLL_INSTANCE:
  close_fd(epfd);

CLOSE_LISTEN_SOCKET:
  close_fd(listen_socket);

EXIT_LABEL:
  return ret;
}
