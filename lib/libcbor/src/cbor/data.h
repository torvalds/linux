/*
 * Copyright (c) 2014-2020 Pavel Kalvoda <me@pavelkalvoda.com>
 *
 * libcbor is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#ifndef LIBCBOR_DATA_H
#define LIBCBOR_DATA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef const unsigned char* cbor_data;
typedef unsigned char* cbor_mutable_data;

/** Specifies the Major type of ::cbor_item_t */
typedef enum cbor_type {
  CBOR_TYPE_UINT /** 0 - positive integers */
  ,
  CBOR_TYPE_NEGINT /** 1 - negative integers*/
  ,
  CBOR_TYPE_BYTESTRING /** 2 - byte strings */
  ,
  CBOR_TYPE_STRING /** 3 - strings */
  ,
  CBOR_TYPE_ARRAY /** 4 - arrays */
  ,
  CBOR_TYPE_MAP /** 5 - maps */
  ,
  CBOR_TYPE_TAG /** 6 - tags  */
  ,
  CBOR_TYPE_FLOAT_CTRL /** 7 - decimals and special values (true, false, nil,
                          ...) */
} cbor_type;

/** Possible decoding errors */
typedef enum {
  CBOR_ERR_NONE,
  CBOR_ERR_NOTENOUGHDATA,
  CBOR_ERR_NODATA,
  // TODO: Should be "malformed" or at least "malformatted". Retained for
  // backwards compatibility.
  CBOR_ERR_MALFORMATED,
  CBOR_ERR_MEMERROR /** Memory error - item allocation failed. Is it too big for
                       your allocator? */
  ,
  CBOR_ERR_SYNTAXERROR /** Stack parsing algorithm failed */
} cbor_error_code;

/** Possible widths of #CBOR_TYPE_UINT items */
typedef enum {
  CBOR_INT_8,
  CBOR_INT_16,
  CBOR_INT_32,
  CBOR_INT_64
} cbor_int_width;

/** Possible widths of #CBOR_TYPE_FLOAT_CTRL items */
typedef enum {
  CBOR_FLOAT_0 /** Internal use - ctrl and special values */
  ,
  CBOR_FLOAT_16 /** Half float */
  ,
  CBOR_FLOAT_32 /** Single float */
  ,
  CBOR_FLOAT_64 /** Double */
} cbor_float_width;

/** Metadata for dynamically sized types */
typedef enum {
  _CBOR_METADATA_DEFINITE,
  _CBOR_METADATA_INDEFINITE
} _cbor_dst_metadata;

/** Semantic mapping for CTRL simple values */
typedef enum {
  CBOR_CTRL_NONE = 0,
  CBOR_CTRL_FALSE = 20,
  CBOR_CTRL_TRUE = 21,
  CBOR_CTRL_NULL = 22,
  CBOR_CTRL_UNDEF = 23
} _cbor_ctrl;

// Metadata items use size_t (instead of uint64_t) because items in memory take
// up at least 1B per entry or string byte, so if size_t is narrower than
// uint64_t, we wouldn't be able to create them in the first place and can save
// some space.

/** Integers specific metadata */
struct _cbor_int_metadata {
  cbor_int_width width;
};

/** Bytestrings specific metadata */
struct _cbor_bytestring_metadata {
  size_t length;
  _cbor_dst_metadata type;
};

/** Strings specific metadata */
struct _cbor_string_metadata {
  size_t length;
  size_t codepoint_count; /* Sum of chunks' codepoint_counts for indefinite
                             strings */
  _cbor_dst_metadata type;
};

/** Arrays specific metadata */
struct _cbor_array_metadata {
  size_t allocated;
  size_t end_ptr;
  _cbor_dst_metadata type;
};

/** Maps specific metadata */
struct _cbor_map_metadata {
  size_t allocated;
  size_t end_ptr;
  _cbor_dst_metadata type;
};

/** Arrays specific metadata
 *
 * The pointer is included - cbor_item_metadata is
 * 2 * sizeof(size_t) + sizeof(_cbor_string_type_metadata),
 * lets use the space
 */
struct _cbor_tag_metadata {
  struct cbor_item_t* tagged_item;
  uint64_t value;
};

/** Floats specific metadata - includes CTRL values */
struct _cbor_float_ctrl_metadata {
  cbor_float_width width;
  uint8_t ctrl;
};

/** Raw memory casts helper */
union _cbor_float_helper {
  float as_float;
  uint32_t as_uint;
};

/** Raw memory casts helper */
union _cbor_double_helper {
  double as_double;
  uint64_t as_uint;
};

/** Union of metadata across all possible types - discriminated in #cbor_item_t
 */
union cbor_item_metadata {
  struct _cbor_int_metadata int_metadata;
  struct _cbor_bytestring_metadata bytestring_metadata;
  struct _cbor_string_metadata string_metadata;
  struct _cbor_array_metadata array_metadata;
  struct _cbor_map_metadata map_metadata;
  struct _cbor_tag_metadata tag_metadata;
  struct _cbor_float_ctrl_metadata float_ctrl_metadata;
};

/** The item handle */
typedef struct cbor_item_t {
  /** Discriminated by type */
  union cbor_item_metadata metadata;
  /** Reference count - initialize to 0 */
  size_t refcount;
  /** Major type discriminator */
  cbor_type type;
  /** Raw data block - interpretation depends on metadata */
  unsigned char* data;
} cbor_item_t;

/** Defines cbor_item_t#data structure for indefinite strings and bytestrings
 *
 * Used to cast the raw representation for a sane manipulation
 */
struct cbor_indefinite_string_data {
  size_t chunk_count;
  size_t chunk_capacity;
  cbor_item_t** chunks;
};

/** High-level decoding error */
struct cbor_error {
  /** Approximate position */
  size_t position;
  /** Description */
  cbor_error_code code;
};

/** Simple pair of items for use in maps */
struct cbor_pair {
  cbor_item_t *key, *value;
};

/** High-level decoding result */
struct cbor_load_result {
  /** Error indicator */
  struct cbor_error error;
  /** Number of bytes read */
  size_t read;
};

/** Streaming decoder result - status */
enum cbor_decoder_status {
  /** Decoding finished successfully (a callback has been invoked)
   *
   * Note that this does *not* mean that the buffer has been fully decoded;
   * there may still be unread bytes for which no callback has been involved.
   */
  CBOR_DECODER_FINISHED,
  /** Not enough data to invoke a callback */
  // TODO: The name is inconsistent with CBOR_ERR_NOTENOUGHDATA. Retained for
  // backwards compatibility.
  CBOR_DECODER_NEDATA,
  /** Bad data (reserved MTB, malformed value, etc.)  */
  CBOR_DECODER_ERROR
};

/** Streaming decoder result */
struct cbor_decoder_result {
  /** Input bytes read/consumed
   *
   * If this is less than the size of input buffer, the client will likely
   * resume parsing starting at the next byte (e.g. `buffer + result.read`).
   *
   * Set to 0 if the #status is not #CBOR_DECODER_FINISHED.
   */
  size_t read;

  /** The decoding status */
  enum cbor_decoder_status status;

  /** Number of bytes in the input buffer needed to resume parsing
   *
   * Set to 0 unless the result status is #CBOR_DECODER_NEDATA. If it is, then:
   *  - If at least one byte was passed, #required will be set to the minimum
   *    number of bytes needed to invoke a decoded callback on the current
   *    prefix.
   *
   *    For example: Attempting to decode a 1B buffer containing `0x19` will
   *    set #required to 3 as `0x19` signals a 2B integer item, so we need at
   *    least 3B to continue (the `0x19` MTB byte and two bytes of data needed
   *    to invoke #cbor_callbacks.uint16).
   *
   *  - If there was no data at all, #read will always be set to 1
   */
  size_t required;
};

#ifdef __cplusplus
}
#endif

#endif  // LIBCBOR_DATA_H
