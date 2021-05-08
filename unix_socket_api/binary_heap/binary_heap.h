#ifndef BINARY_HEAP_H
#define BINARY_HEAP_H

#include <stddef.h>

#define PARENT(ix) (((ix)-1) / 2)
#define LEFT_CHILD(ix) (2 * (ix) + 1)

typedef int (*compare_func)(const void *, const void *);
typedef void (*swap_func)(void *, void *);

struct binary_heap {
  void *arr;
  size_t capacity;
  size_t size;
  size_t elem_size;
  compare_func compare;
  swap_func swap;
};

int binary_heap_allocate(struct binary_heap *heap, size_t capacity,
                         size_t elem_size, compare_func compare,
                         swap_func swap);

void binary_heap_free(struct binary_heap *heap);

int binary_heap_top(struct binary_heap *heap, const void **pp_elem);

int binary_heap_push(struct binary_heap *heap, const void *p_elem);

int binary_heap_pop(struct binary_heap *heap, void *p_elem);

void binary_heap_update(struct binary_heap *heap, size_t ix);

int binary_heap_remove(struct binary_heap *heap, size_t ix, void *p_elem);

int binary_heap_validate(const struct binary_heap *heap);

#endif // BINARY_HEAP_H
