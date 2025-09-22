/*
 * Copyright (c) 2014-2020 Pavel Kalvoda <me@pavelkalvoda.com>
 *
 * libcbor is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#ifndef LIBCBOR_SERIALIZATION_H
#define LIBCBOR_SERIALIZATION_H

#include "cbor/cbor_export.h"
#include "cbor/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * High level encoding
 * ============================================================================
 */

/** Serialize the given item
 *
 * @param item[borrow] A data item
 * @param buffer Buffer to serialize to
 * @param buffer_size Size of the \p buffer
 * @return Length of the result. 0 on failure.
 */
_CBOR_NODISCARD CBOR_EXPORT size_t cbor_serialize(const cbor_item_t *item,
                                                  cbor_mutable_data buffer,
                                                  size_t buffer_size);

/** Compute the length (in bytes) of the item when serialized using
 * `cbor_serialize`.
 *
 * Time complexity is proportional to the number of nested items.
 *
 * @param item[borrow] A data item
 * @return Length (>= 1) of the item when serialized. 0 if the length overflows
 * `size_t`.
 */
_CBOR_NODISCARD CBOR_EXPORT size_t
cbor_serialized_size(const cbor_item_t *item);

/** Serialize the given item, allocating buffers as needed
 *
 * Since libcbor v0.10, the return value is always the same as `buffer_size` (if
 * provided, see https://github.com/PJK/libcbor/pull/251/). New clients should
 * ignore the return value.
 *
 * \rst
 * .. warning:: It is the caller's responsibility to free the buffer using an
 *  appropriate ``free`` implementation.
 * \endrst
 *
 * @param item[borrow] A data item
 * @param buffer[out] Buffer containing the result
 * @param buffer_size[out] Size of the \p buffer, or ``NULL``
 * @return Length of the result. 0 on failure, in which case \p buffer is
 * ``NULL``.
 */
CBOR_EXPORT size_t cbor_serialize_alloc(const cbor_item_t *item,
                                        cbor_mutable_data *buffer,
                                        size_t *buffer_size);

/** Serialize an uint
 *
 * @param item[borrow] A uint
 * @param buffer Buffer to serialize to
 * @param buffer_size Size of the \p buffer
 * @return Length of the result. 0 on failure.
 */
_CBOR_NODISCARD CBOR_EXPORT size_t cbor_serialize_uint(const cbor_item_t *,
                                                       cbor_mutable_data,
                                                       size_t);

/** Serialize a negint
 *
 * @param item[borrow] A negint
 * @param buffer Buffer to serialize to
 * @param buffer_size Size of the \p buffer
 * @return Length of the result. 0 on failure.
 */
_CBOR_NODISCARD CBOR_EXPORT size_t cbor_serialize_negint(const cbor_item_t *,
                                                         cbor_mutable_data,
                                                         size_t);

/** Serialize a bytestring
 *
 * @param item[borrow] A bytestring
 * @param buffer Buffer to serialize to
 * @param buffer_size Size of the \p buffer
 * @return Length of the result. 0 on failure.
 */
_CBOR_NODISCARD CBOR_EXPORT size_t
cbor_serialize_bytestring(const cbor_item_t *, cbor_mutable_data, size_t);

/** Serialize a string
 *
 * @param item[borrow] A string
 * @param buffer Buffer to serialize to
 * @param buffer_size Size of the \p buffer
 * @return Length of the result. 0 on failure.
 */
_CBOR_NODISCARD CBOR_EXPORT size_t cbor_serialize_string(const cbor_item_t *,
                                                         cbor_mutable_data,
                                                         size_t);

/** Serialize an array
 *
 * @param item[borrow] An array
 * @param buffer Buffer to serialize to
 * @param buffer_size Size of the \p buffer
 * @return Length of the result. 0 on failure.
 */
_CBOR_NODISCARD CBOR_EXPORT size_t cbor_serialize_array(const cbor_item_t *,
                                                        cbor_mutable_data,
                                                        size_t);

/** Serialize a map
 *
 * @param item[borrow] A map
 * @param buffer Buffer to serialize to
 * @param buffer_size Size of the \p buffer
 * @return Length of the result. 0 on failure.
 */
_CBOR_NODISCARD CBOR_EXPORT size_t cbor_serialize_map(const cbor_item_t *,
                                                      cbor_mutable_data,
                                                      size_t);

/** Serialize a tag
 *
 * @param item[borrow] A tag
 * @param buffer Buffer to serialize to
 * @param buffer_size Size of the \p buffer
 * @return Length of the result. 0 on failure.
 */
_CBOR_NODISCARD CBOR_EXPORT size_t cbor_serialize_tag(const cbor_item_t *,
                                                      cbor_mutable_data,
                                                      size_t);

/** Serialize a
 *
 * @param item[borrow] A float or ctrl
 * @param buffer Buffer to serialize to
 * @param buffer_size Size of the \p buffer
 * @return Length of the result. 0 on failure.
 */
_CBOR_NODISCARD CBOR_EXPORT size_t
cbor_serialize_float_ctrl(const cbor_item_t *, cbor_mutable_data, size_t);

#ifdef __cplusplus
}
#endif

#endif  // LIBCBOR_SERIALIZATION_H
