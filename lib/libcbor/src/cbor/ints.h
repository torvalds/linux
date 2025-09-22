/*
 * Copyright (c) 2014-2020 Pavel Kalvoda <me@pavelkalvoda.com>
 *
 * libcbor is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#ifndef LIBCBOR_INTS_H
#define LIBCBOR_INTS_H

#include "cbor/cbor_export.h"
#include "cbor/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * Integer (uints and negints) manipulation
 * ============================================================================
 */

/** Extracts the integer value
 *
 * @param item[borrow] positive or negative integer
 * @return the value
 */
_CBOR_NODISCARD CBOR_EXPORT uint8_t cbor_get_uint8(const cbor_item_t *item);

/** Extracts the integer value
 *
 * @param item[borrow] positive or negative integer
 * @return the value
 */
_CBOR_NODISCARD CBOR_EXPORT uint16_t cbor_get_uint16(const cbor_item_t *item);

/** Extracts the integer value
 *
 * @param item[borrow] positive or negative integer
 * @return the value
 */
_CBOR_NODISCARD CBOR_EXPORT uint32_t cbor_get_uint32(const cbor_item_t *item);

/** Extracts the integer value
 *
 * @param item[borrow] positive or negative integer
 * @return the value
 */
_CBOR_NODISCARD CBOR_EXPORT uint64_t cbor_get_uint64(const cbor_item_t *item);

/** Extracts the integer value
 *
 * @param item[borrow] positive or negative integer
 * @return the value, extended to `uint64_t`
 */
_CBOR_NODISCARD CBOR_EXPORT uint64_t cbor_get_int(const cbor_item_t *item);

/** Assigns the integer value
 *
 * @param item[borrow] positive or negative integer item
 * @param value the value to assign. For negative integer, the logical value is
 * `-value - 1`
 */
CBOR_EXPORT void cbor_set_uint8(cbor_item_t *item, uint8_t value);

/** Assigns the integer value
 *
 * @param item[borrow] positive or negative integer item
 * @param value the value to assign. For negative integer, the logical value is
 * `-value - 1`
 */
CBOR_EXPORT void cbor_set_uint16(cbor_item_t *item, uint16_t value);

/** Assigns the integer value
 *
 * @param item[borrow] positive or negative integer item
 * @param value the value to assign. For negative integer, the logical value is
 * `-value - 1`
 */
CBOR_EXPORT void cbor_set_uint32(cbor_item_t *item, uint32_t value);

/** Assigns the integer value
 *
 * @param item[borrow] positive or negative integer item
 * @param value the value to assign. For negative integer, the logical value is
 * `-value - 1`
 */
CBOR_EXPORT void cbor_set_uint64(cbor_item_t *item, uint64_t value);

/** Queries the integer width
 *
 *  @param item[borrow] positive or negative integer item
 *  @return the width
 */
_CBOR_NODISCARD CBOR_EXPORT cbor_int_width
cbor_int_get_width(const cbor_item_t *item);

/** Marks the integer item as a positive integer
 *
 * The data value is not changed
 *
 * @param item[borrow] positive or negative integer item
 */
CBOR_EXPORT void cbor_mark_uint(cbor_item_t *item);

/** Marks the integer item as a negative integer
 *
 * The data value is not changed
 *
 * @param item[borrow] positive or negative integer item
 */
CBOR_EXPORT void cbor_mark_negint(cbor_item_t *item);

/** Allocates new integer with 1B width
 *
 * The width cannot be changed once allocated
 *
 * @return **new** positive integer or `NULL` on memory allocation failure. The
 * value is not initialized
 */
_CBOR_NODISCARD CBOR_EXPORT cbor_item_t *cbor_new_int8(void);

/** Allocates new integer with 2B width
 *
 * The width cannot be changed once allocated
 *
 * @return **new** positive integer or `NULL` on memory allocation failure. The
 * value is not initialized
 */
_CBOR_NODISCARD CBOR_EXPORT cbor_item_t *cbor_new_int16(void);

/** Allocates new integer with 4B width
 *
 * The width cannot be changed once allocated
 *
 * @return **new** positive integer or `NULL` on memory allocation failure. The
 * value is not initialized
 */
_CBOR_NODISCARD CBOR_EXPORT cbor_item_t *cbor_new_int32(void);

/** Allocates new integer with 8B width
 *
 * The width cannot be changed once allocated
 *
 * @return **new** positive integer or `NULL` on memory allocation failure. The
 * value is not initialized
 */
_CBOR_NODISCARD CBOR_EXPORT cbor_item_t *cbor_new_int64(void);

/** Constructs a new positive integer
 *
 * @param value the value to use
 * @return **new** positive integer or `NULL` on memory allocation failure
 */
_CBOR_NODISCARD CBOR_EXPORT cbor_item_t *cbor_build_uint8(uint8_t value);

/** Constructs a new positive integer
 *
 * @param value the value to use
 * @return **new** positive integer or `NULL` on memory allocation failure
 */
_CBOR_NODISCARD CBOR_EXPORT cbor_item_t *cbor_build_uint16(uint16_t value);

/** Constructs a new positive integer
 *
 * @param value the value to use
 * @return **new** positive integer or `NULL` on memory allocation failure
 */
_CBOR_NODISCARD CBOR_EXPORT cbor_item_t *cbor_build_uint32(uint32_t value);

/** Constructs a new positive integer
 *
 * @param value the value to use
 * @return **new** positive integer or `NULL` on memory allocation failure
 */
_CBOR_NODISCARD CBOR_EXPORT cbor_item_t *cbor_build_uint64(uint64_t value);

/** Constructs a new negative integer
 *
 * @param value the value to use
 * @return **new** negative integer or `NULL` on memory allocation failure
 */
_CBOR_NODISCARD CBOR_EXPORT cbor_item_t *cbor_build_negint8(uint8_t value);

/** Constructs a new negative integer
 *
 * @param value the value to use
 * @return **new** negative integer or `NULL` on memory allocation failure
 */
_CBOR_NODISCARD CBOR_EXPORT cbor_item_t *cbor_build_negint16(uint16_t value);

/** Constructs a new negative integer
 *
 * @param value the value to use
 * @return **new** negative integer or `NULL` on memory allocation failure
 */
_CBOR_NODISCARD CBOR_EXPORT cbor_item_t *cbor_build_negint32(uint32_t value);

/** Constructs a new negative integer
 *
 * @param value the value to use
 * @return **new** negative integer or `NULL` on memory allocation failure
 */
_CBOR_NODISCARD CBOR_EXPORT cbor_item_t *cbor_build_negint64(uint64_t value);

#ifdef __cplusplus
}
#endif

#endif  // LIBCBOR_INTS_H
