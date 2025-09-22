/*
 * Copyright (c) 2014-2020 Pavel Kalvoda <me@pavelkalvoda.com>
 *
 * libcbor is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#ifndef LIBCBOR_STACK_H
#define LIBCBOR_STACK_H

#include "cbor/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Simple stack record for the parser */
struct _cbor_stack_record {
  /** Pointer to the parent stack frame */
  struct _cbor_stack_record *lower;
  /** Item under construction */
  cbor_item_t *item;
  /**
   * How many outstanding subitems are expected.
   *
   * For example, when we see a new definite array, `subitems` is initialized to
   * the array length. With every item added, the counter is decreased. When it
   * reaches zero, the stack is popped and the complete item is propagated
   * upwards.
   */
  size_t subitems;
};

/** Stack handle - contents and size */
struct _cbor_stack {
  struct _cbor_stack_record *top;
  size_t size;
};

_CBOR_NODISCARD
struct _cbor_stack _cbor_stack_init(void);

void _cbor_stack_pop(struct _cbor_stack *);

_CBOR_NODISCARD
struct _cbor_stack_record *_cbor_stack_push(struct _cbor_stack *, cbor_item_t *,
                                            size_t);

#ifdef __cplusplus
}
#endif

#endif  // LIBCBOR_STACK_H
