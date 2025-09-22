/*
 * Copyright (c) 2014-2020 Pavel Kalvoda <me@pavelkalvoda.com>
 *
 * libcbor is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#include "tags.h"

cbor_item_t *cbor_new_tag(uint64_t value) {
  cbor_item_t *item = _cbor_malloc(sizeof(cbor_item_t));
  _CBOR_NOTNULL(item);

  *item = (cbor_item_t){
      .refcount = 1,
      .type = CBOR_TYPE_TAG,
      .metadata = {.tag_metadata = {.value = value, .tagged_item = NULL}},
      .data = NULL /* Never used */
  };
  return item;
}

cbor_item_t *cbor_tag_item(const cbor_item_t *item) {
  CBOR_ASSERT(cbor_isa_tag(item));
  return cbor_incref(item->metadata.tag_metadata.tagged_item);
}

uint64_t cbor_tag_value(const cbor_item_t *item) {
  CBOR_ASSERT(cbor_isa_tag(item));
  return item->metadata.tag_metadata.value;
}

void cbor_tag_set_item(cbor_item_t *item, cbor_item_t *tagged_item) {
  CBOR_ASSERT(cbor_isa_tag(item));
  cbor_incref(tagged_item);
  item->metadata.tag_metadata.tagged_item = tagged_item;
}

cbor_item_t *cbor_build_tag(uint64_t value, cbor_item_t *item) {
  cbor_item_t *res = cbor_new_tag(value);
  if (res == NULL) {
    return NULL;
  }
  cbor_tag_set_item(res, item);
  return res;
}
