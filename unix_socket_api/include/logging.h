#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>

#define LOG(fmt, ...)                                                          \
  printf("%s(%d): " fmt "\n", __FUNCTION__, __LINE__, ##__VA_ARGS__)

#endif // UTILS_H
