/*
 * Copyright (c) 2014-2020 Pavel Kalvoda <me@pavelkalvoda.com>
 *
 * libcbor is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#ifndef LIBCBOR_MAPS_H
#define LIBCBOR_MAPS_H

#include "cbor/cbor_export.h"
#include "cbor/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * Map manipulation
 * ============================================================================
 */

/** Get the number of pairs
 *
 * @param item[borrow] A map
 * @return The number of pairs
 */
_CBOR_NODISCARD CBOR_EXPORT size_t cbor_map_size(const cbor_item_t *item);

/** Get the size of the allocated storage
 *
 * @param item[borrow] A map
 * @return Allocated storage size (as the number of #cbor_pair items)
 */
_CBOR_NODISCARD CBOR_EXPORT size_t cbor_map_allocated(const cbor_item_t *item);

/** Create a new definite map
 *
 * @param size The number of slots to preallocate
 * @return **new** definite map. `NULL` on malloc failure.
 */
_CBOR_NODISCARD CBOR_EXPORT cbor_item_t *cbor_new_definite_map(size_t size);

/** Create a new indefinite map
 *
 * @return **new** indefinite map. `NULL` on malloc failure.
 */
_CBOR_NODISCARD CBOR_EXPORT cbor_item_t *cbor_new_indefinite_map(void);

/** Add a pair to the map
 *
 * For definite maps, items can only be added to the preallocated space. For
 * indefinite maps, the storage will be expanded as needed
 *
 * @param item[borrow] A map
 * @param pair[incref] The key-value pair to add (incref is member-wise)
 * @return `true` on success, `false` if either reallocation failed or the
 * preallocated storage is full
 */
_CBOR_NODISCARD CBOR_EXPORT bool cbor_map_add(cbor_item_t *item,
                                              struct cbor_pair pair);

/** Add a key to the map
 *
 * Sets the value to `NULL`. Internal API.
 *
 * @param item[borrow] A map
 * @param key[incref] The key
 * @return `true` on success, `false` if either reallocation failed or the
 * preallocated storage is full
 */
_CBOR_NODISCARD CBOR_EXPORT bool _cbor_map_add_key(cbor_item_t *item,
                                                   cbor_item_t *key);

/** Add a value to the map
 *
 * Assumes that #_cbor_map_add_key has been called. Internal API.
 *
 * @param item[borrow] A map
 * @param key[incref] The value
 * @return `true` on success, `false` if either reallocation failed or the
 * preallocated storage is full
 */
_CBOR_NODISCARD CBOR_EXPORT bool _cbor_map_add_value(cbor_item_t *item,
                                                     cbor_item_t *value);

/** Is this map definite?
 *
 * @param item[borrow] A map
 * @return Is this map definite?
 */
_CBOR_NODISCARD CBOR_EXPORT bool cbor_map_is_definite(const cbor_item_t *item);

/** Is this map indefinite?
 *
 * @param item[borrow] A map
 * @return Is this map indefinite?
 */
_CBOR_NODISCARD CBOR_EXPORT bool cbor_map_is_indefinite(
    const cbor_item_t *item);

/** Get the pairs storage
 *
 * @param item[borrow] A map
 * @return Array of #cbor_map_size pairs. Manipulation is possible as long as
 * references remain valid.
 */
_CBOR_NODISCARD CBOR_EXPORT struct cbor_pair *cbor_map_handle(
    const cbor_item_t *item);

#ifdef __cplusplus
}
#endif

#endif  // LIBCBOR_MAPS_H
