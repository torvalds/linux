/*
 * Copyright (c) 2014-2020 Pavel Kalvoda <me@pavelkalvoda.com>
 *
 * libcbor is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#include "serialization.h"
#include <string.h>
#include "cbor/arrays.h"
#include "cbor/bytestrings.h"
#include "cbor/floats_ctrls.h"
#include "cbor/ints.h"
#include "cbor/maps.h"
#include "cbor/strings.h"
#include "cbor/tags.h"
#include "encoding.h"
#include "internal/memory_utils.h"

size_t cbor_serialize(const cbor_item_t *item, unsigned char *buffer,
                      size_t buffer_size) {
  // cppcheck-suppress missingReturn
  switch (cbor_typeof(item)) {
    case CBOR_TYPE_UINT:
      return cbor_serialize_uint(item, buffer, buffer_size);
    case CBOR_TYPE_NEGINT:
      return cbor_serialize_negint(item, buffer, buffer_size);
    case CBOR_TYPE_BYTESTRING:
      return cbor_serialize_bytestring(item, buffer, buffer_size);
    case CBOR_TYPE_STRING:
      return cbor_serialize_string(item, buffer, buffer_size);
    case CBOR_TYPE_ARRAY:
      return cbor_serialize_array(item, buffer, buffer_size);
    case CBOR_TYPE_MAP:
      return cbor_serialize_map(item, buffer, buffer_size);
    case CBOR_TYPE_TAG:
      return cbor_serialize_tag(item, buffer, buffer_size);
    case CBOR_TYPE_FLOAT_CTRL:
      return cbor_serialize_float_ctrl(item, buffer, buffer_size);
  }
}

/** Largest integer that can be encoded as embedded in the item leading byte. */
const uint64_t kMaxEmbeddedInt = 23;

/** How many bytes will a tag for a nested item of a given `size` take when
 * encoded.*/
size_t _cbor_encoded_header_size(uint64_t size) {
  if (size <= kMaxEmbeddedInt)
    return 1;
  else if (size <= UINT8_MAX)
    return 2;
  else if (size <= UINT16_MAX)
    return 3;
  else if (size <= UINT32_MAX)
    return 5;
  else
    return 9;
}

size_t cbor_serialized_size(const cbor_item_t *item) {
  // cppcheck-suppress missingReturn
  switch (cbor_typeof(item)) {
    case CBOR_TYPE_UINT:
    case CBOR_TYPE_NEGINT:
      switch (cbor_int_get_width(item)) {
        case CBOR_INT_8:
          if (cbor_get_uint8(item) <= kMaxEmbeddedInt) return 1;
          return 2;
        case CBOR_INT_16:
          return 3;
        case CBOR_INT_32:
          return 5;
        case CBOR_INT_64:
          return 9;
      }
    // Note: We do not _cbor_safe_signaling_add zero-length definite strings,
    // they would cause zeroes to propagate. All other items are at least one
    // byte.
    case CBOR_TYPE_BYTESTRING: {
      if (cbor_bytestring_is_definite(item)) {
        size_t header_size =
            _cbor_encoded_header_size(cbor_bytestring_length(item));
        if (cbor_bytestring_length(item) == 0) return header_size;
        return _cbor_safe_signaling_add(header_size,
                                        cbor_bytestring_length(item));
      }
      size_t indef_bytestring_size = 2;  // Leading byte + break
      cbor_item_t **chunks = cbor_bytestring_chunks_handle(item);
      for (size_t i = 0; i < cbor_bytestring_chunk_count(item); i++) {
        indef_bytestring_size = _cbor_safe_signaling_add(
            indef_bytestring_size, cbor_serialized_size(chunks[i]));
      }
      return indef_bytestring_size;
    }
    case CBOR_TYPE_STRING: {
      if (cbor_string_is_definite(item)) {
        size_t header_size =
            _cbor_encoded_header_size(cbor_string_length(item));
        if (cbor_string_length(item) == 0) return header_size;
        return _cbor_safe_signaling_add(header_size, cbor_string_length(item));
      }
      size_t indef_string_size = 2;  // Leading byte + break
      cbor_item_t **chunks = cbor_string_chunks_handle(item);
      for (size_t i = 0; i < cbor_string_chunk_count(item); i++) {
        indef_string_size = _cbor_safe_signaling_add(
            indef_string_size, cbor_serialized_size(chunks[i]));
      }
      return indef_string_size;
    }
    case CBOR_TYPE_ARRAY: {
      size_t array_size = cbor_array_is_definite(item)
                              ? _cbor_encoded_header_size(cbor_array_size(item))
                              : 2;  // Leading byte + break
      cbor_item_t **items = cbor_array_handle(item);
      for (size_t i = 0; i < cbor_array_size(item); i++) {
        array_size = _cbor_safe_signaling_add(array_size,
                                              cbor_serialized_size(items[i]));
      }
      return array_size;
    }
    case CBOR_TYPE_MAP: {
      size_t map_size = cbor_map_is_definite(item)
                            ? _cbor_encoded_header_size(cbor_map_size(item))
                            : 2;  // Leading byte + break
      struct cbor_pair *items = cbor_map_handle(item);
      for (size_t i = 0; i < cbor_map_size(item); i++) {
        map_size = _cbor_safe_signaling_add(
            map_size,
            _cbor_safe_signaling_add(cbor_serialized_size(items[i].key),
                                     cbor_serialized_size(items[i].value)));
      }
      return map_size;
    }
    case CBOR_TYPE_TAG: {
      return _cbor_safe_signaling_add(
          _cbor_encoded_header_size(cbor_tag_value(item)),
          cbor_serialized_size(cbor_move(cbor_tag_item(item))));
    }
    case CBOR_TYPE_FLOAT_CTRL:
      switch (cbor_float_get_width(item)) {
        case CBOR_FLOAT_0:
          return _cbor_encoded_header_size(cbor_ctrl_value(item));
        case CBOR_FLOAT_16:
          return 3;
        case CBOR_FLOAT_32:
          return 5;
        case CBOR_FLOAT_64:
          return 9;
      }
  }
}

size_t cbor_serialize_alloc(const cbor_item_t *item, unsigned char **buffer,
                            size_t *buffer_size) {
  *buffer = NULL;
  size_t serialized_size = cbor_serialized_size(item);
  if (serialized_size == 0) {
    if (buffer_size != NULL) *buffer_size = 0;
    return 0;
  }
  *buffer = _cbor_malloc(serialized_size);
  if (*buffer == NULL) {
    if (buffer_size != NULL) *buffer_size = 0;
    return 0;
  }

  size_t written = cbor_serialize(item, *buffer, serialized_size);
  CBOR_ASSERT(written == serialized_size);
  if (buffer_size != NULL) *buffer_size = serialized_size;
  return written;
}

size_t cbor_serialize_uint(const cbor_item_t *item, unsigned char *buffer,
                           size_t buffer_size) {
  CBOR_ASSERT(cbor_isa_uint(item));
  // cppcheck-suppress missingReturn
  switch (cbor_int_get_width(item)) {
    case CBOR_INT_8:
      return cbor_encode_uint8(cbor_get_uint8(item), buffer, buffer_size);
    case CBOR_INT_16:
      return cbor_encode_uint16(cbor_get_uint16(item), buffer, buffer_size);
    case CBOR_INT_32:
      return cbor_encode_uint32(cbor_get_uint32(item), buffer, buffer_size);
    case CBOR_INT_64:
      return cbor_encode_uint64(cbor_get_uint64(item), buffer, buffer_size);
  }
}

size_t cbor_serialize_negint(const cbor_item_t *item, unsigned char *buffer,
                             size_t buffer_size) {
  CBOR_ASSERT(cbor_isa_negint(item));
  // cppcheck-suppress missingReturn
  switch (cbor_int_get_width(item)) {
    case CBOR_INT_8:
      return cbor_encode_negint8(cbor_get_uint8(item), buffer, buffer_size);
    case CBOR_INT_16:
      return cbor_encode_negint16(cbor_get_uint16(item), buffer, buffer_size);
    case CBOR_INT_32:
      return cbor_encode_negint32(cbor_get_uint32(item), buffer, buffer_size);
    case CBOR_INT_64:
      return cbor_encode_negint64(cbor_get_uint64(item), buffer, buffer_size);
  }
}

size_t cbor_serialize_bytestring(const cbor_item_t *item, unsigned char *buffer,
                                 size_t buffer_size) {
  CBOR_ASSERT(cbor_isa_bytestring(item));
  if (cbor_bytestring_is_definite(item)) {
    size_t length = cbor_bytestring_length(item);
    size_t written = cbor_encode_bytestring_start(length, buffer, buffer_size);
    if (written > 0 && (buffer_size - written >= length)) {
      memcpy(buffer + written, cbor_bytestring_handle(item), length);
      return written + length;
    }
    return 0;
  } else {
    CBOR_ASSERT(cbor_bytestring_is_indefinite(item));
    size_t chunk_count = cbor_bytestring_chunk_count(item);
    size_t written = cbor_encode_indef_bytestring_start(buffer, buffer_size);
    if (written == 0) return 0;

    cbor_item_t **chunks = cbor_bytestring_chunks_handle(item);
    for (size_t i = 0; i < chunk_count; i++) {
      size_t chunk_written = cbor_serialize_bytestring(
          chunks[i], buffer + written, buffer_size - written);
      if (chunk_written == 0) return 0;
      written += chunk_written;
    }

    size_t break_written =
        cbor_encode_break(buffer + written, buffer_size - written);
    if (break_written == 0) return 0;
    return written + break_written;
  }
}

size_t cbor_serialize_string(const cbor_item_t *item, unsigned char *buffer,
                             size_t buffer_size) {
  CBOR_ASSERT(cbor_isa_string(item));
  if (cbor_string_is_definite(item)) {
    size_t length = cbor_string_length(item);
    size_t written = cbor_encode_string_start(length, buffer, buffer_size);
    if (written && (buffer_size - written >= length)) {
      memcpy(buffer + written, cbor_string_handle(item), length);
      return written + length;
    }
    return 0;
  } else {
    CBOR_ASSERT(cbor_string_is_indefinite(item));
    size_t chunk_count = cbor_string_chunk_count(item);
    size_t written = cbor_encode_indef_string_start(buffer, buffer_size);
    if (written == 0) return 0;

    cbor_item_t **chunks = cbor_string_chunks_handle(item);
    for (size_t i = 0; i < chunk_count; i++) {
      size_t chunk_written = cbor_serialize_string(chunks[i], buffer + written,
                                                   buffer_size - written);
      if (chunk_written == 0) return 0;
      written += chunk_written;
    }

    size_t break_written =
        cbor_encode_break(buffer + written, buffer_size - written);
    if (break_written == 0) return 0;
    return written + break_written;
  }
}

size_t cbor_serialize_array(const cbor_item_t *item, unsigned char *buffer,
                            size_t buffer_size) {
  CBOR_ASSERT(cbor_isa_array(item));
  size_t size = cbor_array_size(item), written = 0;
  cbor_item_t **handle = cbor_array_handle(item);
  if (cbor_array_is_definite(item)) {
    written = cbor_encode_array_start(size, buffer, buffer_size);
  } else {
    CBOR_ASSERT(cbor_array_is_indefinite(item));
    written = cbor_encode_indef_array_start(buffer, buffer_size);
  }
  if (written == 0) return 0;

  for (size_t i = 0; i < size; i++) {
    size_t item_written =
        cbor_serialize(*(handle++), buffer + written, buffer_size - written);
    if (item_written == 0) return 0;
    written += item_written;
  }

  if (cbor_array_is_definite(item)) {
    return written;
  } else {
    CBOR_ASSERT(cbor_array_is_indefinite(item));
    size_t break_written =
        cbor_encode_break(buffer + written, buffer_size - written);
    if (break_written == 0) return 0;
    return written + break_written;
  }
}

size_t cbor_serialize_map(const cbor_item_t *item, unsigned char *buffer,
                          size_t buffer_size) {
  CBOR_ASSERT(cbor_isa_map(item));
  size_t size = cbor_map_size(item), written = 0;
  struct cbor_pair *handle = cbor_map_handle(item);

  if (cbor_map_is_definite(item)) {
    written = cbor_encode_map_start(size, buffer, buffer_size);
  } else {
    CBOR_ASSERT(cbor_map_is_indefinite(item));
    written = cbor_encode_indef_map_start(buffer, buffer_size);
  }
  if (written == 0) return 0;

  for (size_t i = 0; i < size; i++) {
    size_t item_written =
        cbor_serialize(handle->key, buffer + written, buffer_size - written);
    if (item_written == 0) {
      return 0;
    }
    written += item_written;
    item_written = cbor_serialize((handle++)->value, buffer + written,
                                  buffer_size - written);
    if (item_written == 0) return 0;
    written += item_written;
  }

  if (cbor_map_is_definite(item)) {
    return written;
  } else {
    CBOR_ASSERT(cbor_map_is_indefinite(item));
    size_t break_written =
        cbor_encode_break(buffer + written, buffer_size - written);
    if (break_written == 0) return 0;
    return written + break_written;
  }
}

size_t cbor_serialize_tag(const cbor_item_t *item, unsigned char *buffer,
                          size_t buffer_size) {
  CBOR_ASSERT(cbor_isa_tag(item));
  size_t written = cbor_encode_tag(cbor_tag_value(item), buffer, buffer_size);
  if (written == 0) return 0;

  size_t item_written = cbor_serialize(cbor_move(cbor_tag_item(item)),
                                       buffer + written, buffer_size - written);
  if (item_written == 0) return 0;
  return written + item_written;
}

size_t cbor_serialize_float_ctrl(const cbor_item_t *item, unsigned char *buffer,
                                 size_t buffer_size) {
  CBOR_ASSERT(cbor_isa_float_ctrl(item));
  // cppcheck-suppress missingReturn
  switch (cbor_float_get_width(item)) {
    case CBOR_FLOAT_0:
      /* CTRL - special treatment */
      return cbor_encode_ctrl(cbor_ctrl_value(item), buffer, buffer_size);
    case CBOR_FLOAT_16:
      return cbor_encode_half(cbor_float_get_float2(item), buffer, buffer_size);
    case CBOR_FLOAT_32:
      return cbor_encode_single(cbor_float_get_float4(item), buffer,
                                buffer_size);
    case CBOR_FLOAT_64:
      return cbor_encode_double(cbor_float_get_float8(item), buffer,
                                buffer_size);
  }
}
