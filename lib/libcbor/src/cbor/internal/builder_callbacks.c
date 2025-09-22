/*
 * Copyright (c) 2014-2020 Pavel Kalvoda <me@pavelkalvoda.com>
 *
 * libcbor is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#include "builder_callbacks.h"

#include <string.h>

#include "../arrays.h"
#include "../bytestrings.h"
#include "../common.h"
#include "../floats_ctrls.h"
#include "../ints.h"
#include "../maps.h"
#include "../strings.h"
#include "../tags.h"
#include "unicode.h"

// `_cbor_builder_append` takes ownership of `item`. If adding the item to
// parent container fails, `item` will be deallocated to prevent memory.
void _cbor_builder_append(cbor_item_t *item,
                          struct _cbor_decoder_context *ctx) {
  if (ctx->stack->size == 0) {
    /* Top level item */
    ctx->root = item;
    return;
  }
  /* Part of a bigger structure */
  switch (ctx->stack->top->item->type) {
    // Handle Arrays and Maps since they can contain subitems of any type.
    // Byte/string construction from chunks is handled in the respective chunk
    // handlers.
    case CBOR_TYPE_ARRAY: {
      if (cbor_array_is_definite(ctx->stack->top->item)) {
        // We don't need an explicit check for whether the item still belongs
        // into this array because if there are extra items, they will cause a
        // syntax error when decoded.
        CBOR_ASSERT(ctx->stack->top->subitems > 0);
        // This should never happen since the definite array should be
        // preallocated for the expected number of items.
        if (!cbor_array_push(ctx->stack->top->item, item)) {
          ctx->creation_failed = true;
          cbor_decref(&item);
          break;
        }
        cbor_decref(&item);
        ctx->stack->top->subitems--;
        if (ctx->stack->top->subitems == 0) {
          cbor_item_t *stack_item = ctx->stack->top->item;
          _cbor_stack_pop(ctx->stack);
          _cbor_builder_append(stack_item, ctx);
        }
      } else {
        /* Indefinite array, don't bother with subitems */
        if (!cbor_array_push(ctx->stack->top->item, item)) {
          ctx->creation_failed = true;
        }
        cbor_decref(&item);
      }
      break;
    }
    case CBOR_TYPE_MAP: {
      // Handle both definite and indefinite maps the same initially.
      // Note: We use 0 and 1 subitems to distinguish between keys and values in
      // indefinite items
      if (ctx->stack->top->subitems % 2) {
        /* Odd record, this is a value */
        if (!_cbor_map_add_value(ctx->stack->top->item, item)) {
          ctx->creation_failed = true;
          cbor_decref(&item);
          break;
        }
      } else {
        /* Even record, this is a key */
        if (!_cbor_map_add_key(ctx->stack->top->item, item)) {
          ctx->creation_failed = true;
          cbor_decref(&item);
          break;
        }
      }
      cbor_decref(&item);
      if (cbor_map_is_definite(ctx->stack->top->item)) {
        CBOR_ASSERT(ctx->stack->top->subitems > 0);
        ctx->stack->top->subitems--;
        if (ctx->stack->top->subitems == 0) {
          cbor_item_t *map_entry = ctx->stack->top->item;
          _cbor_stack_pop(ctx->stack);
          _cbor_builder_append(map_entry, ctx);
        }
      } else {
        ctx->stack->top->subitems ^=
            1; /* Flip the indicator for indefinite items */
      }
      break;
    }
    case CBOR_TYPE_TAG: {
      CBOR_ASSERT(ctx->stack->top->subitems == 1);
      cbor_tag_set_item(ctx->stack->top->item, item);
      cbor_decref(&item); /* Give up on our reference */
      cbor_item_t *tagged_item = ctx->stack->top->item;
      _cbor_stack_pop(ctx->stack);
      _cbor_builder_append(tagged_item, ctx);
      break;
    }
    // We have an item to append but nothing to append it to.
    default: {
      cbor_decref(&item);
      ctx->syntax_error = true;
    }
  }
}

#define CHECK_RES(ctx, res)        \
  do {                             \
    if (res == NULL) {             \
      ctx->creation_failed = true; \
      return;                      \
    }                              \
  } while (0)

// Check that the length fits into size_t. If not, we cannot possibly allocate
// the required memory and should fail fast.
#define CHECK_LENGTH(ctx, length)  \
  do {                             \
    if (length > SIZE_MAX) {       \
      ctx->creation_failed = true; \
      return;                      \
    }                              \
  } while (0)

#define PUSH_CTX_STACK(ctx, res, subitems)                     \
  do {                                                         \
    if (_cbor_stack_push(ctx->stack, res, subitems) == NULL) { \
      cbor_decref(&res);                                       \
      ctx->creation_failed = true;                             \
    }                                                          \
  } while (0)

void cbor_builder_uint8_callback(void *context, uint8_t value) {
  struct _cbor_decoder_context *ctx = context;
  cbor_item_t *res = cbor_new_int8();
  CHECK_RES(ctx, res);
  cbor_mark_uint(res);
  cbor_set_uint8(res, value);
  _cbor_builder_append(res, ctx);
}

void cbor_builder_uint16_callback(void *context, uint16_t value) {
  struct _cbor_decoder_context *ctx = context;
  cbor_item_t *res = cbor_new_int16();
  CHECK_RES(ctx, res);
  cbor_mark_uint(res);
  cbor_set_uint16(res, value);
  _cbor_builder_append(res, ctx);
}

void cbor_builder_uint32_callback(void *context, uint32_t value) {
  struct _cbor_decoder_context *ctx = context;
  cbor_item_t *res = cbor_new_int32();
  CHECK_RES(ctx, res);
  cbor_mark_uint(res);
  cbor_set_uint32(res, value);
  _cbor_builder_append(res, ctx);
}

void cbor_builder_uint64_callback(void *context, uint64_t value) {
  struct _cbor_decoder_context *ctx = context;
  cbor_item_t *res = cbor_new_int64();
  CHECK_RES(ctx, res);
  cbor_mark_uint(res);
  cbor_set_uint64(res, value);
  _cbor_builder_append(res, ctx);
}

void cbor_builder_negint8_callback(void *context, uint8_t value) {
  struct _cbor_decoder_context *ctx = context;
  cbor_item_t *res = cbor_new_int8();
  CHECK_RES(ctx, res);
  cbor_mark_negint(res);
  cbor_set_uint8(res, value);
  _cbor_builder_append(res, ctx);
}

void cbor_builder_negint16_callback(void *context, uint16_t value) {
  struct _cbor_decoder_context *ctx = context;
  cbor_item_t *res = cbor_new_int16();
  CHECK_RES(ctx, res);
  cbor_mark_negint(res);
  cbor_set_uint16(res, value);
  _cbor_builder_append(res, ctx);
}

void cbor_builder_negint32_callback(void *context, uint32_t value) {
  struct _cbor_decoder_context *ctx = context;
  cbor_item_t *res = cbor_new_int32();
  CHECK_RES(ctx, res);
  cbor_mark_negint(res);
  cbor_set_uint32(res, value);
  _cbor_builder_append(res, ctx);
}

void cbor_builder_negint64_callback(void *context, uint64_t value) {
  struct _cbor_decoder_context *ctx = context;
  cbor_item_t *res = cbor_new_int64();
  CHECK_RES(ctx, res);
  cbor_mark_negint(res);
  cbor_set_uint64(res, value);
  _cbor_builder_append(res, ctx);
}

void cbor_builder_byte_string_callback(void *context, cbor_data data,
                                       uint64_t length) {
  struct _cbor_decoder_context *ctx = context;
  CHECK_LENGTH(ctx, length);
  unsigned char *new_handle = _cbor_malloc(length);
  if (new_handle == NULL) {
    ctx->creation_failed = true;
    return;
  }

  memcpy(new_handle, data, length);
  cbor_item_t *new_chunk = cbor_new_definite_bytestring();

  if (new_chunk == NULL) {
    _cbor_free(new_handle);
    ctx->creation_failed = true;
    return;
  }

  cbor_bytestring_set_handle(new_chunk, new_handle, length);

  // If an indef bytestring is on the stack, extend it (if it were closed, it
  // would have been popped). Handle any syntax errors upstream.
  if (ctx->stack->size > 0 && cbor_isa_bytestring(ctx->stack->top->item) &&
      cbor_bytestring_is_indefinite(ctx->stack->top->item)) {
    if (!cbor_bytestring_add_chunk(ctx->stack->top->item, new_chunk)) {
      ctx->creation_failed = true;
    }
    cbor_decref(&new_chunk);
  } else {
    _cbor_builder_append(new_chunk, ctx);
  }
}

void cbor_builder_byte_string_start_callback(void *context) {
  struct _cbor_decoder_context *ctx = context;
  cbor_item_t *res = cbor_new_indefinite_bytestring();
  CHECK_RES(ctx, res);
  PUSH_CTX_STACK(ctx, res, 0);
}

void cbor_builder_string_callback(void *context, cbor_data data,
                                  uint64_t length) {
  struct _cbor_decoder_context *ctx = context;
  CHECK_LENGTH(ctx, length);
  struct _cbor_unicode_status unicode_status;
  uint64_t codepoint_count =
      _cbor_unicode_codepoint_count(data, length, &unicode_status);

  if (unicode_status.status != _CBOR_UNICODE_OK) {
    ctx->syntax_error = true;
    return;
  }
  CBOR_ASSERT(codepoint_count <= length);

  unsigned char *new_handle = _cbor_malloc(length);

  if (new_handle == NULL) {
    ctx->creation_failed = true;
    return;
  }

  memcpy(new_handle, data, length);
  cbor_item_t *new_chunk = cbor_new_definite_string();
  if (new_chunk == NULL) {
    _cbor_free(new_handle);
    ctx->creation_failed = true;
    return;
  }
  cbor_string_set_handle(new_chunk, new_handle, length);
  new_chunk->metadata.string_metadata.codepoint_count = codepoint_count;

  // If an indef string is on the stack, extend it (if it were closed, it would
  // have been popped). Handle any syntax errors upstream.
  if (ctx->stack->size > 0 && cbor_isa_string(ctx->stack->top->item) &&
      cbor_string_is_indefinite(ctx->stack->top->item)) {
    if (!cbor_string_add_chunk(ctx->stack->top->item, new_chunk)) {
      ctx->creation_failed = true;
    }
    cbor_decref(&new_chunk);
  } else {
    _cbor_builder_append(new_chunk, ctx);
  }
}

void cbor_builder_string_start_callback(void *context) {
  struct _cbor_decoder_context *ctx = context;
  cbor_item_t *res = cbor_new_indefinite_string();
  CHECK_RES(ctx, res);
  PUSH_CTX_STACK(ctx, res, 0);
}

void cbor_builder_array_start_callback(void *context, uint64_t size) {
  struct _cbor_decoder_context *ctx = context;
  CHECK_LENGTH(ctx, size);
  cbor_item_t *res = cbor_new_definite_array(size);
  CHECK_RES(ctx, res);
  if (size > 0) {
    PUSH_CTX_STACK(ctx, res, size);
  } else {
    _cbor_builder_append(res, ctx);
  }
}

void cbor_builder_indef_array_start_callback(void *context) {
  struct _cbor_decoder_context *ctx = context;
  cbor_item_t *res = cbor_new_indefinite_array();
  CHECK_RES(ctx, res);
  PUSH_CTX_STACK(ctx, res, 0);
}

void cbor_builder_indef_map_start_callback(void *context) {
  struct _cbor_decoder_context *ctx = context;
  cbor_item_t *res = cbor_new_indefinite_map();
  CHECK_RES(ctx, res);
  PUSH_CTX_STACK(ctx, res, 0);
}

void cbor_builder_map_start_callback(void *context, uint64_t size) {
  struct _cbor_decoder_context *ctx = context;
  CHECK_LENGTH(ctx, size);
  cbor_item_t *res = cbor_new_definite_map(size);
  CHECK_RES(ctx, res);
  if (size > 0) {
    PUSH_CTX_STACK(ctx, res, size * 2);
  } else {
    _cbor_builder_append(res, ctx);
  }
}

/**
 * Is the (partially constructed) item indefinite?
 */
bool _cbor_is_indefinite(cbor_item_t *item) {
  switch (item->type) {
    case CBOR_TYPE_BYTESTRING:
      return cbor_bytestring_is_indefinite(item);
    case CBOR_TYPE_STRING:
      return cbor_string_is_indefinite(item);
    case CBOR_TYPE_ARRAY:
      return cbor_array_is_indefinite(item);
    case CBOR_TYPE_MAP:
      return cbor_map_is_indefinite(item);
    default:
      return false;
  }
}

void cbor_builder_indef_break_callback(void *context) {
  struct _cbor_decoder_context *ctx = context;
  /* There must be an item to break out of*/
  if (ctx->stack->size > 0) {
    cbor_item_t *item = ctx->stack->top->item;
    if (_cbor_is_indefinite(
            item) && /* Only indefinite items can be terminated by 0xFF */
        /* Special case: we cannot append up if an indefinite map is incomplete
           (we are expecting a value). */
        (item->type != CBOR_TYPE_MAP || ctx->stack->top->subitems % 2 == 0)) {
      _cbor_stack_pop(ctx->stack);
      _cbor_builder_append(item, ctx);
      return;
    }
  }

  ctx->syntax_error = true;
}

void cbor_builder_float2_callback(void *context, float value) {
  struct _cbor_decoder_context *ctx = context;
  cbor_item_t *res = cbor_new_float2();
  CHECK_RES(ctx, res);
  cbor_set_float2(res, value);
  _cbor_builder_append(res, ctx);
}

void cbor_builder_float4_callback(void *context, float value) {
  struct _cbor_decoder_context *ctx = context;
  cbor_item_t *res = cbor_new_float4();
  CHECK_RES(ctx, res);
  cbor_set_float4(res, value);
  _cbor_builder_append(res, ctx);
}

void cbor_builder_float8_callback(void *context, double value) {
  struct _cbor_decoder_context *ctx = context;
  cbor_item_t *res = cbor_new_float8();
  CHECK_RES(ctx, res);
  cbor_set_float8(res, value);
  _cbor_builder_append(res, ctx);
}

void cbor_builder_null_callback(void *context) {
  struct _cbor_decoder_context *ctx = context;
  cbor_item_t *res = cbor_new_null();
  CHECK_RES(ctx, res);
  _cbor_builder_append(res, ctx);
}

void cbor_builder_undefined_callback(void *context) {
  struct _cbor_decoder_context *ctx = context;
  cbor_item_t *res = cbor_new_undef();
  CHECK_RES(ctx, res);
  _cbor_builder_append(res, ctx);
}

void cbor_builder_boolean_callback(void *context, bool value) {
  struct _cbor_decoder_context *ctx = context;
  cbor_item_t *res = cbor_build_bool(value);
  CHECK_RES(ctx, res);
  _cbor_builder_append(res, ctx);
}

void cbor_builder_tag_callback(void *context, uint64_t value) {
  struct _cbor_decoder_context *ctx = context;
  cbor_item_t *res = cbor_new_tag(value);
  CHECK_RES(ctx, res);
  PUSH_CTX_STACK(ctx, res, 1);
}
