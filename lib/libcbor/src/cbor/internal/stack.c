/*
 * Copyright (c) 2014-2020 Pavel Kalvoda <me@pavelkalvoda.com>
 *
 * libcbor is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#include "stack.h"

struct _cbor_stack _cbor_stack_init(void) {
  return (struct _cbor_stack){.top = NULL, .size = 0};
}

void _cbor_stack_pop(struct _cbor_stack *stack) {
  struct _cbor_stack_record *top = stack->top;
  stack->top = stack->top->lower;
  _cbor_free(top);
  stack->size--;
}

struct _cbor_stack_record *_cbor_stack_push(struct _cbor_stack *stack,
                                            cbor_item_t *item,
                                            size_t subitems) {
  if (stack->size == CBOR_MAX_STACK_SIZE) return NULL;
  struct _cbor_stack_record *new_top =
      _cbor_malloc(sizeof(struct _cbor_stack_record));
  if (new_top == NULL) return NULL;

  *new_top = (struct _cbor_stack_record){stack->top, item, subitems};
  stack->top = new_top;
  stack->size++;
  return new_top;
}
