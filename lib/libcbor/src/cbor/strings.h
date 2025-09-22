/*
 * Copyright (c) 2014-2020 Pavel Kalvoda <me@pavelkalvoda.com>
 *
 * libcbor is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#ifndef LIBCBOR_STRINGS_H
#define LIBCBOR_STRINGS_H

#include "cbor/cbor_export.h"
#include "cbor/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * String manipulation
 * ============================================================================
 */

/** Returns the length of the underlying string in bytes
 *
 * There can be fewer unicode character than bytes (see
 * `cbor_string_codepoint_count`). For definite strings only.
 *
 * @param item[borrow] a definite string
 * @return length of the string. Zero if no chunk has been attached yet
 */
_CBOR_NODISCARD CBOR_EXPORT size_t cbor_string_length(const cbor_item_t *item);

/** The number of codepoints in this string
 *
 * Might differ from length if there are multibyte ones
 *
 * @param item[borrow] A string
 * @return The number of codepoints in this string
 */
_CBOR_NODISCARD CBOR_EXPORT size_t
cbor_string_codepoint_count(const cbor_item_t *item);

/** Is the string definite?
 *
 * @param item[borrow] a string
 * @return Is the string definite?
 */
_CBOR_NODISCARD CBOR_EXPORT bool cbor_string_is_definite(
    const cbor_item_t *item);

/** Is the string indefinite?
 *
 * @param item[borrow] a string
 * @return Is the string indefinite?
 */
_CBOR_NODISCARD CBOR_EXPORT bool cbor_string_is_indefinite(
    const cbor_item_t *item);

/** Get the handle to the underlying string
 *
 * Definite items only. Modifying the data is allowed. In that case, the caller
 * takes responsibility for the effect on items this item might be a part of
 *
 * @param item[borrow] A definite string
 * @return The address of the underlying string. `NULL` if no data have been
 * assigned yet.
 */
_CBOR_NODISCARD CBOR_EXPORT cbor_mutable_data
cbor_string_handle(const cbor_item_t *item);

/** Set the handle to the underlying string
 *
 *
 * \rst
 * .. warning:: Using a pointer to a stack allocated constant is a common
 *  mistake. Lifetime of the string will expire when it goes out of scope and
 *  the CBOR item will be left inconsistent.
 * \endrst
 *
 * @param item[borrow] A definite string
 * @param data The memory block. The caller gives up the ownership of the block.
 * libcbor will deallocate it when appropriate using its free function
 * @param length Length of the data block
 */
CBOR_EXPORT void cbor_string_set_handle(
    cbor_item_t *item, cbor_mutable_data CBOR_RESTRICT_POINTER data,
    size_t length);

/** Get the handle to the array of chunks
 *
 * Manipulations with the memory block (e.g. sorting it) are allowed, but the
 * validity and the number of chunks must be retained.
 *
 * @param item[borrow] A indefinite string
 * @return array of #cbor_string_chunk_count definite strings
 */
_CBOR_NODISCARD CBOR_EXPORT cbor_item_t **cbor_string_chunks_handle(
    const cbor_item_t *item);

/** Get the number of chunks this string consist of
 *
 * @param item[borrow] A indefinite string
 * @return The chunk count. 0 for freshly created items.
 */
_CBOR_NODISCARD CBOR_EXPORT size_t
cbor_string_chunk_count(const cbor_item_t *item);

/** Appends a chunk to the string
 *
 * Indefinite strings only.
 *
 * May realloc the chunk storage.
 *
 * @param item[borrow] An indefinite string
 * @param item[incref] A definite string
 * @return true on success. false on realloc failure. In that case, the refcount
 * of `chunk` is not increased and the `item` is left intact.
 */
_CBOR_NODISCARD CBOR_EXPORT bool cbor_string_add_chunk(cbor_item_t *item,
                                                       cbor_item_t *chunk);

/** Creates a new definite string
 *
 * The handle is initialized to `NULL` and length to 0
 *
 * @return **new** definite string. `NULL` on malloc failure.
 */
_CBOR_NODISCARD CBOR_EXPORT cbor_item_t *cbor_new_definite_string(void);

/** Creates a new indefinite string
 *
 * The chunks array is initialized to `NULL` and chunkcount to 0
 *
 * @return **new** indefinite string. `NULL` on malloc failure.
 */
_CBOR_NODISCARD CBOR_EXPORT cbor_item_t *cbor_new_indefinite_string(void);

/** Creates a new string and initializes it
 *
 * The `val` will be copied to a newly allocated block
 *
 * @param val A null-terminated UTF-8 string
 * @return A **new** string with content `handle`. `NULL` on malloc failure.
 */
_CBOR_NODISCARD CBOR_EXPORT cbor_item_t *cbor_build_string(const char *val);

/** Creates a new string and initializes it
 *
 * The `handle` will be copied to a newly allocated block
 *
 * @param val A UTF-8 string, at least \p length long (excluding the null byte)
 * @return A **new** string with content `handle`. `NULL` on malloc failure.
 */
_CBOR_NODISCARD CBOR_EXPORT cbor_item_t *cbor_build_stringn(const char *val,
                                                            size_t length);

#ifdef __cplusplus
}
#endif

#endif  // LIBCBOR_STRINGS_H
