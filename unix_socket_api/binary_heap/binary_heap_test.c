#define _GNU_SOURCE

#include <binary_heap.h>
#include <generic.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define UPDATE_TIME_STEP 10

struct testing_elem {
  struct timespec ts;
  size_t heap_ix;
};

static long active_allocations = 0;

static struct testing_elem *create_elem(size_t heap_ix, size_t limit) {
  struct testing_elem *ptr =
      (struct testing_elem *)malloc(sizeof(struct testing_elem));

  if (!ptr) {
    perror("malloc");
    return NULL;
  }

  ptr->heap_ix = heap_ix;
  ptr->ts.tv_sec = rand() % (int)limit;
  ++active_allocations;

  return ptr;
}

static void free_elem(struct testing_elem *e) {
  free(e);
  --active_allocations;
}

static int less_ts(const struct timespec *ptr_a, const struct timespec *ptr_b) {
  return ACCUM_TIME_NS(*ptr_a) >= ACCUM_TIME_NS(*ptr_b);
}

static int compare_elem(const void *lhs, const void *rhs) {
  const struct testing_elem **p_a = (const struct testing_elem **)lhs;
  const struct testing_elem **p_b = (const struct testing_elem **)rhs;

  return less_ts(&(*p_a)->ts, &(*p_b)->ts);
}

static void swap_elem(void *a, void *b) {
  struct testing_elem **elem_a = (struct testing_elem **)a;
  struct testing_elem **elem_b = (struct testing_elem **)b;
  struct testing_elem *tmp = NULL;
  size_t tmp_heap_ix;

  tmp = *elem_a;
  *elem_a = *elem_b;
  *elem_b = tmp;

  tmp_heap_ix = (*elem_a)->heap_ix;
  (*elem_a)->heap_ix = (*elem_b)->heap_ix;
  (*elem_b)->heap_ix = tmp_heap_ix;
}

static int test_heap_push(struct binary_heap *heap) {
  struct testing_elem *elem = NULL;

  for (size_t i = 0; i < heap->capacity; ++i) {
    elem = create_elem(i, heap->capacity);

    if (!elem) {
      return -1;
    }

    if (binary_heap_push(heap, &elem)) {
      free_elem(elem);
      return -1;
    }

    if (binary_heap_validate(heap)) {
      return -1;
    }
  }

  return 0;
}

static int test_heap_update(struct binary_heap *heap, size_t n) {
  struct testing_elem **elem_arr = (struct testing_elem **)heap->arr;
  size_t ix;

  for (size_t i = 0; i < n; ++i) {
    ix = rand() % heap->size;
    elem_arr[ix]->ts.tv_sec += UPDATE_TIME_STEP;

    binary_heap_update(heap, ix);

    if (binary_heap_validate(heap)) {
      return -1;
    }

    ix = rand() % heap->size;
    elem_arr[ix]->ts.tv_sec -= UPDATE_TIME_STEP;

    binary_heap_update(heap, ix);

    if (binary_heap_validate(heap)) {
      return -1;
    }
  }

  return 0;
}

static int test_heap_remove(struct binary_heap *heap, size_t n) {
  struct testing_elem *elem = NULL;
  size_t ix;

  for (size_t i = 0; i < n; ++i) {
    ix = rand() % heap->size;

    if (binary_heap_remove(heap, ix, &elem)) {
      return -1;
    }

    free_elem(elem);

    if (binary_heap_validate(heap)) {
      return -1;
    }
  }

  return 0;
}

static int test_heap_pop(struct binary_heap *heap,
                         struct testing_elem **elem_arr, size_t n) {
  struct testing_elem **p_elem = NULL;
  size_t i = 0;

  while (i < n) {
    if (binary_heap_top(heap, (const void **)&p_elem)) {
      goto FREE_POPPED_ELEMS_MEMORY;
    }

    if (binary_heap_pop(heap, &elem_arr[i])) {
      goto FREE_POPPED_ELEMS_MEMORY;
    }

    ++i;

    if (binary_heap_validate(heap)) {
      goto FREE_POPPED_ELEMS_MEMORY;
    }
  }

  return 0;

FREE_POPPED_ELEMS_MEMORY:
  while (i > 0) {
    i--;
    free_elem(elem_arr[i]);
  }

  return -1;
}

static int is_sorted(struct testing_elem **arr, size_t n) {
  for (size_t i = 1; i < n; ++i) {
    if (!less_ts(&arr[i]->ts, &arr[i - 1]->ts)) {
      return -1;
    }
  }

  return 0;
}

ATTR_UNUSED static void print(const struct binary_heap *heap) {
  struct testing_elem **elem_arr = (struct testing_elem **)heap->arr;
  size_t del_ix = 0;

  printf("heap[%zu]:\n", heap->size);

  for (size_t i = 0; i < heap->size; ++i) {
    printf("(#%zu: %ld)", i, elem_arr[i]->ts.tv_sec);

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

static size_t parse_heap_size(const char *s) {
  char *endptr = NULL;
  size_t result;

  errno = 0;
  result = (size_t)strtoul(s, &endptr, 10);

  if (errno) {
    perror("strtoul");
    return 0;
  } else if (*endptr) {
    return 0;
  }

  return result;
}

int main(int argc, char *argv[]) {
  int result = EXIT_FAILURE;
  size_t elem_to_allocate;
  size_t elem_to_update;
  size_t elem_to_remove;
  size_t elem_to_pop;
  struct testing_elem *elem = NULL;
  struct testing_elem **popped_elems = NULL;
  struct binary_heap heap;

  if (argc != 2) {
    printf("Usage: %s <heap_size>\n", argv[0]);
    goto BAD_ARGS;
  }

  elem_to_allocate = parse_heap_size(argv[1]);

  if (elem_to_allocate == 0) {
    printf("Wrong heap size %s, conversion cannot be performed\n", argv[1]);
    goto BAD_ARGS;
  }

  elem_to_update = elem_to_allocate;
  elem_to_remove = elem_to_allocate / 2;
  elem_to_pop = elem_to_allocate - elem_to_remove;

  srand(time(NULL));

  if (binary_heap_allocate(&heap, elem_to_allocate,
                           sizeof(struct testing_elem *), &compare_elem,
                           &swap_elem)) {
    goto ALLOCATION_FAILURE;
  }

  if (test_heap_push(&heap)) {
    printf("Test heap push failed\n");
    goto FREE_HEAP_ELEMS;
  }

  if (!binary_heap_push(&heap, &elem)) {
    printf("Expected failure on push\n");
    goto FREE_HEAP_ELEMS;
  }

  if (test_heap_update(&heap, elem_to_update)) {
    printf("Test heap update failed\n");
    goto FREE_HEAP_ELEMS;
  }

  if (test_heap_remove(&heap, elem_to_remove)) {
    printf("Test heap remove failed\n");
    goto FREE_HEAP_ELEMS;
  }

  popped_elems = (struct testing_elem **)malloc(elem_to_pop *
                                                sizeof(struct testing_elem *));

  if (!popped_elems) {
    perror("malloc");
    goto FREE_HEAP_ELEMS;
  }

  if (test_heap_pop(&heap, popped_elems, elem_to_pop)) {
    printf("Test heap pop failed\n");
    goto FREE_POPPED;
  }

  if (!binary_heap_top(&heap, (const void **)&elem)) {
    printf("Expected failure on top\n");
    goto FREE_POPPED_ELEMS;
  }

  if (!binary_heap_pop(&heap, &elem)) {
    printf("Expected failure on pop\n");
    goto FREE_POPPED_ELEMS;
  }

  if (is_sorted(popped_elems, elem_to_pop)) {
    printf("Expected popped elements to be sorted\n");
    goto FREE_POPPED_ELEMS;
  }

  result = EXIT_SUCCESS;

FREE_POPPED_ELEMS:
  for (size_t i = 0; i < elem_to_pop; ++i) {
    free_elem(popped_elems[i]);
  }

FREE_POPPED:
  free(popped_elems);

FREE_HEAP_ELEMS:
  for (size_t i = 0; i < heap.size; ++i) {
    free_elem(((struct testing_elem **)heap.arr)[i]);
  }

  binary_heap_free(&heap);

  if (result == EXIT_SUCCESS && active_allocations) {
    printf("Memory leak count: %ld\n", active_allocations);
    result = EXIT_FAILURE;
  }

ALLOCATION_FAILURE:
  printf("Test result: %s\n", result == EXIT_SUCCESS ? "SUCCESS" : "FAILURE");

BAD_ARGS:
  return result;
}
