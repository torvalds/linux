/*
 * Copyright (c) 2014-2020 Pavel Kalvoda <me@pavelkalvoda.com>
 *
 * libcbor is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#include "ints.h"

cbor_int_width cbor_int_get_width(const cbor_item_t *item) {
  CBOR_ASSERT(cbor_is_int(item));
  return item->metadata.int_metadata.width;
}

uint8_t cbor_get_uint8(const cbor_item_t *item) {
  CBOR_ASSERT(cbor_is_int(item));
  CBOR_ASSERT(cbor_int_get_width(item) == CBOR_INT_8);
  return *item->data;
}

uint16_t cbor_get_uint16(const cbor_item_t *item) {
  CBOR_ASSERT(cbor_is_int(item));
  CBOR_ASSERT(cbor_int_get_width(item) == CBOR_INT_16);
  return *(uint16_t *)item->data;
}

uint32_t cbor_get_uint32(const cbor_item_t *item) {
  CBOR_ASSERT(cbor_is_int(item));
  CBOR_ASSERT(cbor_int_get_width(item) == CBOR_INT_32);
  return *(uint32_t *)item->data;
}

uint64_t cbor_get_uint64(const cbor_item_t *item) {
  CBOR_ASSERT(cbor_is_int(item));
  CBOR_ASSERT(cbor_int_get_width(item) == CBOR_INT_64);
  return *(uint64_t *)item->data;
}

uint64_t cbor_get_int(const cbor_item_t *item) {
  CBOR_ASSERT(cbor_is_int(item));
  // cppcheck-suppress missingReturn
  switch (cbor_int_get_width(item)) {
    case CBOR_INT_8:
      return cbor_get_uint8(item);
    case CBOR_INT_16:
      return cbor_get_uint16(item);
    case CBOR_INT_32:
      return cbor_get_uint32(item);
    case CBOR_INT_64:
      return cbor_get_uint64(item);
  }
}

void cbor_set_uint8(cbor_item_t *item, uint8_t value) {
  CBOR_ASSERT(cbor_is_int(item));
  CBOR_ASSERT(cbor_int_get_width(item) == CBOR_INT_8);
  *item->data = value;
}

void cbor_set_uint16(cbor_item_t *item, uint16_t value) {
  CBOR_ASSERT(cbor_is_int(item));
  CBOR_ASSERT(cbor_int_get_width(item) == CBOR_INT_16);
  *(uint16_t *)item->data = value;
}

void cbor_set_uint32(cbor_item_t *item, uint32_t value) {
  CBOR_ASSERT(cbor_is_int(item));
  CBOR_ASSERT(cbor_int_get_width(item) == CBOR_INT_32);
  *(uint32_t *)item->data = value;
}

void cbor_set_uint64(cbor_item_t *item, uint64_t value) {
  CBOR_ASSERT(cbor_is_int(item));
  CBOR_ASSERT(cbor_int_get_width(item) == CBOR_INT_64);
  *(uint64_t *)item->data = value;
}

void cbor_mark_uint(cbor_item_t *item) {
  CBOR_ASSERT(cbor_is_int(item));
  item->type = CBOR_TYPE_UINT;
}

void cbor_mark_negint(cbor_item_t *item) {
  CBOR_ASSERT(cbor_is_int(item));
  item->type = CBOR_TYPE_NEGINT;
}

cbor_item_t *cbor_new_int8(void) {
  cbor_item_t *item = _cbor_malloc(sizeof(cbor_item_t) + 1);
  _CBOR_NOTNULL(item);
  *item = (cbor_item_t){.data = (unsigned char *)item + sizeof(cbor_item_t),
                        .refcount = 1,
                        .metadata = {.int_metadata = {.width = CBOR_INT_8}},
                        .type = CBOR_TYPE_UINT};
  return item;
}

cbor_item_t *cbor_new_int16(void) {
  cbor_item_t *item = _cbor_malloc(sizeof(cbor_item_t) + 2);
  _CBOR_NOTNULL(item);
  *item = (cbor_item_t){.data = (unsigned char *)item + sizeof(cbor_item_t),
                        .refcount = 1,
                        .metadata = {.int_metadata = {.width = CBOR_INT_16}},
                        .type = CBOR_TYPE_UINT};
  return item;
}

cbor_item_t *cbor_new_int32(void) {
  cbor_item_t *item = _cbor_malloc(sizeof(cbor_item_t) + 4);
  _CBOR_NOTNULL(item);
  *item = (cbor_item_t){.data = (unsigned char *)item + sizeof(cbor_item_t),
                        .refcount = 1,
                        .metadata = {.int_metadata = {.width = CBOR_INT_32}},
                        .type = CBOR_TYPE_UINT};
  return item;
}

cbor_item_t *cbor_new_int64(void) {
  cbor_item_t *item = _cbor_malloc(sizeof(cbor_item_t) + 8);
  _CBOR_NOTNULL(item);
  *item = (cbor_item_t){.data = (unsigned char *)item + sizeof(cbor_item_t),
                        .refcount = 1,
                        .metadata = {.int_metadata = {.width = CBOR_INT_64}},
                        .type = CBOR_TYPE_UINT};
  return item;
}

cbor_item_t *cbor_build_uint8(uint8_t value) {
  cbor_item_t *item = cbor_new_int8();
  _CBOR_NOTNULL(item);
  cbor_set_uint8(item, value);
  cbor_mark_uint(item);
  return item;
}

cbor_item_t *cbor_build_uint16(uint16_t value) {
  cbor_item_t *item = cbor_new_int16();
  _CBOR_NOTNULL(item);
  cbor_set_uint16(item, value);
  cbor_mark_uint(item);
  return item;
}

cbor_item_t *cbor_build_uint32(uint32_t value) {
  cbor_item_t *item = cbor_new_int32();
  _CBOR_NOTNULL(item);
  cbor_set_uint32(item, value);
  cbor_mark_uint(item);
  return item;
}

cbor_item_t *cbor_build_uint64(uint64_t value) {
  cbor_item_t *item = cbor_new_int64();
  _CBOR_NOTNULL(item);
  cbor_set_uint64(item, value);
  cbor_mark_uint(item);
  return item;
}

cbor_item_t *cbor_build_negint8(uint8_t value) {
  cbor_item_t *item = cbor_new_int8();
  _CBOR_NOTNULL(item);
  cbor_set_uint8(item, value);
  cbor_mark_negint(item);
  return item;
}

cbor_item_t *cbor_build_negint16(uint16_t value) {
  cbor_item_t *item = cbor_new_int16();
  _CBOR_NOTNULL(item);
  cbor_set_uint16(item, value);
  cbor_mark_negint(item);
  return item;
}

cbor_item_t *cbor_build_negint32(uint32_t value) {
  cbor_item_t *item = cbor_new_int32();
  _CBOR_NOTNULL(item);
  cbor_set_uint32(item, value);
  cbor_mark_negint(item);
  return item;
}

cbor_item_t *cbor_build_negint64(uint64_t value) {
  cbor_item_t *item = cbor_new_int64();
  _CBOR_NOTNULL(item);
  cbor_set_uint64(item, value);
  cbor_mark_negint(item);
  return item;
}
