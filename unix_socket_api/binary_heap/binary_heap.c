#include <binary_heap.h>

#include <stdlib.h>
#include <string.h>

static int compare(const struct binary_heap *heap, size_t ix_a, size_t ix_b) {
  return heap->compare(heap->arr + ix_a * heap->elem_size,
                       heap->arr + ix_b * heap->elem_size);
}

static void swap(struct binary_heap *heap, size_t ix_a, size_t ix_b) {
  heap->swap(heap->arr + ix_a * heap->elem_size,
             heap->arr + ix_b * heap->elem_size);
}

static void heapify_up(struct binary_heap *heap, size_t ix) {
  while (ix > 0 && !compare(heap, ix, PARENT(ix))) {
    swap(heap, ix, PARENT(ix));
    ix = PARENT(ix);
  }
}

static void heapify_down(struct binary_heap *heap, size_t ix) {
  if (heap->size < 2) {
    return;
  }

  const size_t last_root_ix = PARENT(heap->size - 1);
  size_t child_ix;

  while (ix <= last_root_ix) {
    child_ix = LEFT_CHILD(ix);

    if (child_ix + 1 < heap->size && !compare(heap, child_ix + 1, child_ix)) {
      ++child_ix;
    }

    if (compare(heap, child_ix, ix)) {
      break;
    }

    swap(heap, ix, child_ix);
    ix = child_ix;
  }
}

int binary_heap_allocate(struct binary_heap *heap, size_t capacity,
                         size_t elem_size, compare_func compare,
                         swap_func swap) {
  heap->arr = malloc(capacity * elem_size);

  if (!heap->arr) {
    return -1;
  }

  heap->capacity = capacity;
  heap->size = 0;
  heap->elem_size = elem_size;
  heap->compare = compare;
  heap->swap = swap;

  return 0;
}

void binary_heap_free(struct binary_heap *heap) { free(heap->arr); }

int binary_heap_top(struct binary_heap *heap, const void **pp_elem) {
  if (heap->size == 0) {
    return -1;
  }

  *pp_elem = heap->arr;

  return 0;
}

int binary_heap_push(struct binary_heap *heap, const void *p_elem) {
  if (heap->size >= heap->capacity) {
    return -1;
  }

  memcpy(heap->arr + heap->size * heap->elem_size, p_elem, heap->elem_size);
  ++heap->size;
  heapify_up(heap, heap->size - 1);

  return 0;
}

int binary_heap_pop(struct binary_heap *heap, void *p_elem) {
  if (heap->size == 0) {
    return -1;
  }

  memcpy(p_elem, heap->arr, heap->elem_size);
  --heap->size;
  swap(heap, 0, heap->size);
  heapify_down(heap, 0);

  return 0;
}

void binary_heap_update(struct binary_heap *heap, size_t ix) {
  if (ix > 0 && !compare(heap, ix, PARENT(ix))) {
    heapify_up(heap, ix);
  } else {
    heapify_down(heap, ix);
  }
}

int binary_heap_remove(struct binary_heap *heap, size_t ix, void *p_elem) {
  if (ix >= heap->size) {
    return -1;
  }

  memcpy(p_elem, heap->arr + ix * heap->elem_size, heap->elem_size);
  --heap->size;

  if (ix != heap->size) {
    swap(heap, ix, heap->size);
    binary_heap_update(heap, ix);
  }

  return 0;
}

int binary_heap_validate(const struct binary_heap *heap) {
  if (heap->size < 2) {
    return 0;
  }

  const size_t last_root_ix = PARENT(heap->size - 1);
  size_t child_ix;

  for (size_t ix = 0; ix <= last_root_ix; ++ix) {
    child_ix = LEFT_CHILD(ix);

    if (child_ix + 1 < heap->size && !compare(heap, child_ix + 1, child_ix)) {
      ++child_ix;
    }

    if (!compare(heap, child_ix, ix)) {
      return -1;
    }
  }

  return 0;
}
