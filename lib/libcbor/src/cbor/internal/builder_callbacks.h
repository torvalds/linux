/*
 * Copyright (c) 2014-2020 Pavel Kalvoda <me@pavelkalvoda.com>
 *
 * libcbor is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#ifndef LIBCBOR_BUILDER_CALLBACKS_H
#define LIBCBOR_BUILDER_CALLBACKS_H

#include "../callbacks.h"
#include "cbor/common.h"
#include "stack.h"

#ifdef __cplusplus
extern "C" {
#endif

/** High-level decoding context */
struct _cbor_decoder_context {
  /** Callback creating the last item has failed */
  bool creation_failed;
  /** Stack expectation mismatch */
  bool syntax_error;
  cbor_item_t *root;
  struct _cbor_stack *stack;
};

/** Internal helper: Append item to the top of the stack while handling errors.
 */
void _cbor_builder_append(cbor_item_t *item, struct _cbor_decoder_context *ctx);

void cbor_builder_uint8_callback(void *, uint8_t);

void cbor_builder_uint16_callback(void *, uint16_t);

void cbor_builder_uint32_callback(void *, uint32_t);

void cbor_builder_uint64_callback(void *, uint64_t);

void cbor_builder_negint8_callback(void *, uint8_t);

void cbor_builder_negint16_callback(void *, uint16_t);

void cbor_builder_negint32_callback(void *, uint32_t);

void cbor_builder_negint64_callback(void *, uint64_t);

void cbor_builder_string_callback(void *, cbor_data, uint64_t);

void cbor_builder_string_start_callback(void *);

void cbor_builder_byte_string_callback(void *, cbor_data, uint64_t);

void cbor_builder_byte_string_start_callback(void *);

void cbor_builder_array_start_callback(void *, uint64_t);

void cbor_builder_indef_array_start_callback(void *);

void cbor_builder_map_start_callback(void *, uint64_t);

void cbor_builder_indef_map_start_callback(void *);

void cbor_builder_tag_callback(void *, uint64_t);

void cbor_builder_float2_callback(void *, float);

void cbor_builder_float4_callback(void *, float);

void cbor_builder_float8_callback(void *, double);

void cbor_builder_null_callback(void *);

void cbor_builder_undefined_callback(void *);

void cbor_builder_boolean_callback(void *, bool);

void cbor_builder_indef_break_callback(void *);

#ifdef __cplusplus
}
#endif

#endif  // LIBCBOR_BUILDER_CALLBACKS_H
