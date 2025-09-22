/*
 * Copyright (c) 2014-2020 Pavel Kalvoda <me@pavelkalvoda.com>
 *
 * libcbor is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#include "arrays.h"
#include <string.h>
#include "internal/memory_utils.h"

size_t cbor_array_size(const cbor_item_t *item) {
  CBOR_ASSERT(cbor_isa_array(item));
  return item->metadata.array_metadata.end_ptr;
}

size_t cbor_array_allocated(const cbor_item_t *item) {
  CBOR_ASSERT(cbor_isa_array(item));
  return item->metadata.array_metadata.allocated;
}

cbor_item_t *cbor_array_get(const cbor_item_t *item, size_t index) {
  return cbor_incref(((cbor_item_t **)item->data)[index]);
}

bool cbor_array_set(cbor_item_t *item, size_t index, cbor_item_t *value) {
  if (index == item->metadata.array_metadata.end_ptr) {
    return cbor_array_push(item, value);
  } else if (index < item->metadata.array_metadata.end_ptr) {
    return cbor_array_replace(item, index, value);
  } else {
    return false;
  }
}

bool cbor_array_replace(cbor_item_t *item, size_t index, cbor_item_t *value) {
  if (index >= item->metadata.array_metadata.end_ptr) return false;
  /* We cannot use cbor_array_get as that would increase the refcount */
  cbor_intermediate_decref(((cbor_item_t **)item->data)[index]);
  ((cbor_item_t **)item->data)[index] = cbor_incref(value);
  return true;
}

bool cbor_array_push(cbor_item_t *array, cbor_item_t *pushee) {
  CBOR_ASSERT(cbor_isa_array(array));
  struct _cbor_array_metadata *metadata =
      (struct _cbor_array_metadata *)&array->metadata;
  cbor_item_t **data = (cbor_item_t **)array->data;
  if (cbor_array_is_definite(array)) {
    /* Do not reallocate definite arrays */
    if (metadata->end_ptr >= metadata->allocated) {
      return false;
    }
    data[metadata->end_ptr++] = pushee;
  } else {
    /* Exponential realloc */
    if (metadata->end_ptr >= metadata->allocated) {
      // Check for overflows first
      if (!_cbor_safe_to_multiply(CBOR_BUFFER_GROWTH, metadata->allocated)) {
        return false;
      }

      size_t new_allocation = metadata->allocated == 0
                                  ? 1
                                  : CBOR_BUFFER_GROWTH * metadata->allocated;

      unsigned char *new_data = _cbor_realloc_multiple(
          array->data, sizeof(cbor_item_t *), new_allocation);
      if (new_data == NULL) {
        return false;
      }

      array->data = new_data;
      metadata->allocated = new_allocation;
    }
    ((cbor_item_t **)array->data)[metadata->end_ptr++] = pushee;
  }
  cbor_incref(pushee);
  return true;
}

bool cbor_array_is_definite(const cbor_item_t *item) {
  CBOR_ASSERT(cbor_isa_array(item));
  return item->metadata.array_metadata.type == _CBOR_METADATA_DEFINITE;
}

bool cbor_array_is_indefinite(const cbor_item_t *item) {
  CBOR_ASSERT(cbor_isa_array(item));
  return item->metadata.array_metadata.type == _CBOR_METADATA_INDEFINITE;
}

cbor_item_t **cbor_array_handle(const cbor_item_t *item) {
  CBOR_ASSERT(cbor_isa_array(item));
  return (cbor_item_t **)item->data;
}

cbor_item_t *cbor_new_definite_array(size_t size) {
  cbor_item_t *item = _cbor_malloc(sizeof(cbor_item_t));
  _CBOR_NOTNULL(item);
  cbor_item_t **data = _cbor_alloc_multiple(sizeof(cbor_item_t *), size);
  _CBOR_DEPENDENT_NOTNULL(item, data);

  for (size_t i = 0; i < size; i++) {
    data[i] = NULL;
  }

  *item = (cbor_item_t){
      .refcount = 1,
      .type = CBOR_TYPE_ARRAY,
      .metadata = {.array_metadata = {.type = _CBOR_METADATA_DEFINITE,
                                      .allocated = size,
                                      .end_ptr = 0}},
      .data = (unsigned char *)data};

  return item;
}

cbor_item_t *cbor_new_indefinite_array(void) {
  cbor_item_t *item = _cbor_malloc(sizeof(cbor_item_t));
  _CBOR_NOTNULL(item);

  *item = (cbor_item_t){
      .refcount = 1,
      .type = CBOR_TYPE_ARRAY,
      .metadata = {.array_metadata = {.type = _CBOR_METADATA_INDEFINITE,
                                      .allocated = 0,
                                      .end_ptr = 0}},
      .data = NULL /* Can be safely realloc-ed */
  };
  return item;
}
