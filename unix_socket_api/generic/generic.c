#define _GNU_SOURCE

#include <generic.h>

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>

void close_fd(int fd) {
  if (close(fd)) {
    perror("close");
  }
}

void kill_children_procs(const int *children, size_t n) {
  while (n > 0) {
    if (kill(*children, SIGTERM) < 0 && errno != ESRCH) {
      perror("kill");
    }

    n--;
    children++;
  }
}

static const char *zodiac_names[ZODIAC_NUM] ATTR_UNUSED = {
    SIGN_ARIES, SIGN_TAURUS, SIGN_GEMINI};

ssize_t find_zodiac(const char *zodiac, size_t n) {
  const char *zodiac_name = NULL;
  size_t len;

  for (size_t i = 0; i < ZODIAC_NUM; i++) {
    zodiac_name = zodiac_names[i];
    len = strlen(zodiac_name);

    if (n >= len && !memcmp(zodiac, zodiac_names[i], len)) {
      return (ssize_t)i;
    }
  }

  return -1;
}

const char *get_zodiac(size_t ix) {
  return ix < ZODIAC_NUM ? zodiac_names[ix] : NULL;
}

int send_all(int fd, const void *buf, size_t n, int flags) {
  ssize_t bytes;
  size_t pos = 0;

  while (pos < n) {
    bytes = send(fd, buf + pos, n - pos, flags);

    if (bytes < 0) {
      perror("send");
      return -1;
    }

    pos += (size_t)bytes;
  }

  return 0;
}

ssize_t recv_line(int fd, void *buf, size_t n, int flags) {
  ssize_t bytes;
  size_t pos = 0;
  const char *del_ptr = NULL;

  while (1) {
    bytes = recv(fd, buf + pos, n - pos, flags);

    if (bytes < 0) {
      perror("recv");
      break;
    } else if (bytes == 0) {
      return 0;
    }

    pos += (size_t)bytes;
    del_ptr = memchr(buf + pos - bytes, DELIMITER, bytes);

    if (del_ptr) {
      // NOTE: this is expected as we not using an intermediate buffer
      if (del_ptr != buf + pos - 1) {
        break;
      }

      return (ssize_t)pos;
    } else if (pos == n) {
      break;
    }
  }

  return -1;
}

ssize_t buffered_recv(struct buffer *buf, void *dst, size_t n) {
  ssize_t received;
  size_t size;

  if (buf->pos == buf->len) {
    buf->pos = buf->len = 0;

    received = recv(buf->fd, buf->data, ASTROLOG_BUF_LEN, 0);

    if (received <= 0) {
      return received;
    }

    buf->len = received;
  }

  size = buf->len - buf->pos;

  if (n < size) {
    size = n;
  }

  memcpy(dst, buf->data + buf->pos, size);
  buf->pos += size;

  return (ssize_t)size;
}

ssize_t buffered_recv_until_delim(struct buffer *buf, void *dst, size_t n,
                                  char delim) {
  ssize_t received;
  size_t size;
  const char *del_ptr = NULL;

  if (buf->pos == buf->len) {
    buf->pos = buf->len = 0;

    received = recv(buf->fd, buf->data, ASTROLOG_BUF_LEN, 0);

    if (received <= 0) {
      return received;
    }

    buf->len = received;
  }

  size = buf->len - buf->pos;

  if (n < size) {
    size = n;
  }

  del_ptr = memchr(buf->data + buf->pos, delim, size);

  if (del_ptr != NULL && del_ptr + 1 - buf->data - buf->pos < size) {
    size = del_ptr + 1 - buf->data - buf->pos;
  }

  memcpy(dst, buf->data + buf->pos, size);
  buf->pos += size;

  return (ssize_t)size;
}
