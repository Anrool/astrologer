#ifndef GENERIC_H
#define GENERIC_H

#include <sys/types.h>

#define ATTR_UNUSED __attribute__((unused))

#define ACCUM_TIME_NS(ts) ((ts).tv_sec * SEC_NS_FACTOR + (ts).tv_nsec)

#define MAX(a, b) ((a) < (b) ? (b) : (a))
#define MIN(a, b) ((a) > (b) ? (b) : (a))

#define SEC_NS_FACTOR 1000000000
#define SEC_US_FACTOR 1000000

#define STAR_REQUEST_TYPE "STARS SAY "
#define CLIENT_REQUEST_TYPE "HOROSCOPE "

#define STAR_REPLY_SUCCESS "THANKS!\n"
#define STAR_REPLY_FAILURE "DENIED!\n"

#define CLIENT_REPLY_FAILURE "YOU ARE OUT OF LUCK!\n"

#define ZODIAC_NUM (size_t)(3) // 12 in reality, but left 3 for simplicity

#define SIGN_ARIES "Aries"
#define SIGN_TAURUS "Taurus"
#define SIGN_GEMINI "Gemini"

#define ZODIAC_UNKOWN (size_t) - 1

#define DELIMITER '\n'

#define ASTROLOG_BUF_LEN (size_t)(80)

struct buffer {
  int fd;
  char data[ASTROLOG_BUF_LEN + 1];
  size_t pos;
  size_t len;
};

void close_fd(int fd);

void kill_children_procs(const int *children, size_t n);

ssize_t find_zodiac(const char *zodiac, size_t n);

const char *get_zodiac(size_t ix);

int send_all(int fd, const void *buf, size_t n, int flags);

// TODO: deprecate and only use buffered variant
ssize_t recv_line(int fd, void *buf, size_t n, int flags);

ssize_t buffered_recv(struct buffer *buf, void *dst, size_t len);

ssize_t buffered_recv_until_delim(struct buffer *buf, void *dst, size_t len,
                                  char delim);

#endif // GENERIC_H
