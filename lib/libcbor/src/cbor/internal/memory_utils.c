/*
 * Copyright (c) 2014-2020 Pavel Kalvoda <me@pavelkalvoda.com>
 *
 * libcbor is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#include "memory_utils.h"
#include "cbor/common.h"

// TODO: Consider builtins
// (https://gcc.gnu.org/onlinedocs/gcc/Integer-Overflow-Builtins.html)

/** Highest on bit position */
size_t _cbor_highest_bit(size_t number) {
  size_t bit = 0;
  while (number != 0) {
    bit++;
    number >>= 1;
  }

  return bit;
}

bool _cbor_safe_to_multiply(size_t a, size_t b) {
  if (a <= 1 || b <= 1) return true;
  return _cbor_highest_bit(a) + _cbor_highest_bit(b) <= sizeof(size_t) * 8;
}

bool _cbor_safe_to_add(size_t a, size_t b) {
  // Unsigned integer overflow doesn't constitute UB
  size_t sum = a + b;
  return sum >= a && sum >= b;
}

size_t _cbor_safe_signaling_add(size_t a, size_t b) {
  if (a == 0 || b == 0) return 0;
  if (_cbor_safe_to_add(a, b)) return a + b;
  return 0;
}

void* _cbor_alloc_multiple(size_t item_size, size_t item_count) {
  if (_cbor_safe_to_multiply(item_size, item_count)) {
    return _cbor_malloc(item_size * item_count);
  } else {
    return NULL;
  }
}

void* _cbor_realloc_multiple(void* pointer, size_t item_size,
                             size_t item_count) {
  if (_cbor_safe_to_multiply(item_size, item_count)) {
    return _cbor_realloc(pointer, item_size * item_count);
  } else {
    return NULL;
  }
}
