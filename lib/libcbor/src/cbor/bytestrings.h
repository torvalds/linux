/*
 * Copyright (c) 2014-2020 Pavel Kalvoda <me@pavelkalvoda.com>
 *
 * libcbor is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#ifndef LIBCBOR_BYTESTRINGS_H
#define LIBCBOR_BYTESTRINGS_H

#include "cbor/cbor_export.h"
#include "cbor/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * Byte string manipulation
 * ============================================================================
 */

/** Returns the length of the binary data
 *
 * For definite byte strings only
 *
 * @param item[borrow] a definite bytestring
 * @return length of the binary data. Zero if no chunk has been attached yet
 */
_CBOR_NODISCARD
CBOR_EXPORT size_t cbor_bytestring_length(const cbor_item_t *item);

/** Is the byte string definite?
 *
 * @param item[borrow] a byte string
 * @return Is the byte string definite?
 */
_CBOR_NODISCARD
CBOR_EXPORT bool cbor_bytestring_is_definite(const cbor_item_t *item);

/** Is the byte string indefinite?
 *
 * @param item[borrow] a byte string
 * @return Is the byte string indefinite?
 */
_CBOR_NODISCARD
CBOR_EXPORT bool cbor_bytestring_is_indefinite(const cbor_item_t *item);

/** Get the handle to the binary data
 *
 * Definite items only. Modifying the data is allowed. In that case, the caller
 * takes responsibility for the effect on items this item might be a part of
 *
 * @param item[borrow] A definite byte string
 * @return The address of the binary data. `NULL` if no data have been assigned
 * yet.
 */
_CBOR_NODISCARD
CBOR_EXPORT cbor_mutable_data cbor_bytestring_handle(const cbor_item_t *item);

/** Set the handle to the binary data
 *
 * @param item[borrow] A definite byte string
 * @param data The memory block. The caller gives up the ownership of the block.
 * libcbor will deallocate it when appropriate using its free function
 * @param length Length of the data block
 */
CBOR_EXPORT void cbor_bytestring_set_handle(
    cbor_item_t *item, cbor_mutable_data CBOR_RESTRICT_POINTER data,
    size_t length);

/** Get the handle to the array of chunks
 *
 * Manipulations with the memory block (e.g. sorting it) are allowed, but the
 * validity and the number of chunks must be retained.
 *
 * @param item[borrow] A indefinite byte string
 * @return array of #cbor_bytestring_chunk_count definite bytestrings
 */
_CBOR_NODISCARD
CBOR_EXPORT cbor_item_t **cbor_bytestring_chunks_handle(
    const cbor_item_t *item);

/** Get the number of chunks this string consist of
 *
 * @param item[borrow] A indefinite bytestring
 * @return The chunk count. 0 for freshly created items.
 */
_CBOR_NODISCARD
CBOR_EXPORT size_t cbor_bytestring_chunk_count(const cbor_item_t *item);

/** Appends a chunk to the bytestring
 *
 * Indefinite byte strings only.
 *
 * May realloc the chunk storage.
 *
 * @param item[borrow] An indefinite byte string
 * @param item[incref] A definite byte string
 * @return true on success, false on realloc failure. In that case, the refcount
 * of `chunk` is not increased and the `item` is left intact.
 */
_CBOR_NODISCARD
CBOR_EXPORT bool cbor_bytestring_add_chunk(cbor_item_t *item,
                                           cbor_item_t *chunk);

/** Creates a new definite byte string
 *
 * The handle is initialized to `NULL` and length to 0
 *
 * @return **new** definite bytestring. `NULL` on malloc failure.
 */
_CBOR_NODISCARD
CBOR_EXPORT cbor_item_t *cbor_new_definite_bytestring(void);

/** Creates a new indefinite byte string
 *
 * The chunks array is initialized to `NULL` and chunk count to 0
 *
 * @return **new** indefinite bytestring. `NULL` on malloc failure.
 */
_CBOR_NODISCARD
CBOR_EXPORT cbor_item_t *cbor_new_indefinite_bytestring(void);

/** Creates a new byte string and initializes it
 *
 * The `handle` will be copied to a newly allocated block
 *
 * @param handle Block of binary data
 * @param length Length of `data`
 * @return A **new** byte string with content `handle`. `NULL` on malloc
 * failure.
 */
_CBOR_NODISCARD
CBOR_EXPORT cbor_item_t *cbor_build_bytestring(cbor_data handle, size_t length);

#ifdef __cplusplus
}
#endif

#endif  // LIBCBOR_BYTESTRINGS_H
