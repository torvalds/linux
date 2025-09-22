/*
 * Copyright (c) 2014-2020 Pavel Kalvoda <me@pavelkalvoda.com>
 *
 * libcbor is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#include "cbor/common.h"
#include "arrays.h"
#include "bytestrings.h"
#include "data.h"
#include "floats_ctrls.h"
#include "ints.h"
#include "maps.h"
#include "strings.h"
#include "tags.h"

#ifdef DEBUG
bool _cbor_enable_assert = true;
#endif

bool cbor_isa_uint(const cbor_item_t *item) {
  return item->type == CBOR_TYPE_UINT;
}

bool cbor_isa_negint(const cbor_item_t *item) {
  return item->type == CBOR_TYPE_NEGINT;
}

bool cbor_isa_bytestring(const cbor_item_t *item) {
  return item->type == CBOR_TYPE_BYTESTRING;
}

bool cbor_isa_string(const cbor_item_t *item) {
  return item->type == CBOR_TYPE_STRING;
}

bool cbor_isa_array(const cbor_item_t *item) {
  return item->type == CBOR_TYPE_ARRAY;
}

bool cbor_isa_map(const cbor_item_t *item) {
  return item->type == CBOR_TYPE_MAP;
}

bool cbor_isa_tag(const cbor_item_t *item) {
  return item->type == CBOR_TYPE_TAG;
}

bool cbor_isa_float_ctrl(const cbor_item_t *item) {
  return item->type == CBOR_TYPE_FLOAT_CTRL;
}

cbor_type cbor_typeof(const cbor_item_t *item) { return item->type; }

bool cbor_is_int(const cbor_item_t *item) {
  return cbor_isa_uint(item) || cbor_isa_negint(item);
}

bool cbor_is_bool(const cbor_item_t *item) {
  return cbor_isa_float_ctrl(item) &&
         (cbor_ctrl_value(item) == CBOR_CTRL_FALSE ||
          cbor_ctrl_value(item) == CBOR_CTRL_TRUE);
}

bool cbor_is_null(const cbor_item_t *item) {
  return cbor_isa_float_ctrl(item) && cbor_ctrl_value(item) == CBOR_CTRL_NULL;
}

bool cbor_is_undef(const cbor_item_t *item) {
  return cbor_isa_float_ctrl(item) && cbor_ctrl_value(item) == CBOR_CTRL_UNDEF;
}

bool cbor_is_float(const cbor_item_t *item) {
  return cbor_isa_float_ctrl(item) && !cbor_float_ctrl_is_ctrl(item);
}

cbor_item_t *cbor_incref(cbor_item_t *item) {
  item->refcount++;
  return item;
}

void cbor_decref(cbor_item_t **item_ref) {
  cbor_item_t *item = *item_ref;
  CBOR_ASSERT(item->refcount > 0);
  if (--item->refcount == 0) {
    switch (item->type) {
      case CBOR_TYPE_UINT:
        /* Fallthrough */
      case CBOR_TYPE_NEGINT:
        /* Combined allocation, freeing the item suffices */
        { break; }
      case CBOR_TYPE_BYTESTRING: {
        if (cbor_bytestring_is_definite(item)) {
          _cbor_free(item->data);
        } else {
          /* We need to decref all chunks */
          cbor_item_t **handle = cbor_bytestring_chunks_handle(item);
          for (size_t i = 0; i < cbor_bytestring_chunk_count(item); i++)
            cbor_decref(&handle[i]);
          _cbor_free(
              ((struct cbor_indefinite_string_data *)item->data)->chunks);
          _cbor_free(item->data);
        }
        break;
      }
      case CBOR_TYPE_STRING: {
        if (cbor_string_is_definite(item)) {
          _cbor_free(item->data);
        } else {
          /* We need to decref all chunks */
          cbor_item_t **handle = cbor_string_chunks_handle(item);
          for (size_t i = 0; i < cbor_string_chunk_count(item); i++)
            cbor_decref(&handle[i]);
          _cbor_free(
              ((struct cbor_indefinite_string_data *)item->data)->chunks);
          _cbor_free(item->data);
        }
        break;
      }
      case CBOR_TYPE_ARRAY: {
        /* Get all items and decref them */
        cbor_item_t **handle = cbor_array_handle(item);
        size_t size = cbor_array_size(item);
        for (size_t i = 0; i < size; i++)
          if (handle[i] != NULL) cbor_decref(&handle[i]);
        _cbor_free(item->data);
        break;
      }
      case CBOR_TYPE_MAP: {
        struct cbor_pair *handle = cbor_map_handle(item);
        for (size_t i = 0; i < item->metadata.map_metadata.end_ptr;
             i++, handle++) {
          cbor_decref(&handle->key);
          if (handle->value != NULL) cbor_decref(&handle->value);
        }
        _cbor_free(item->data);
        break;
      }
      case CBOR_TYPE_TAG: {
        if (item->metadata.tag_metadata.tagged_item != NULL)
          cbor_decref(&item->metadata.tag_metadata.tagged_item);
        _cbor_free(item->data);
        break;
      }
      case CBOR_TYPE_FLOAT_CTRL: {
        /* Floats have combined allocation */
        break;
      }
    }
    _cbor_free(item);
    *item_ref = NULL;
  }
}

void cbor_intermediate_decref(cbor_item_t *item) { cbor_decref(&item); }

size_t cbor_refcount(const cbor_item_t *item) { return item->refcount; }

cbor_item_t *cbor_move(cbor_item_t *item) {
  item->refcount--;
  return item;
}
