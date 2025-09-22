/*
 * Copyright (c) 2014-2020 Pavel Kalvoda <me@pavelkalvoda.com>
 *
 * libcbor is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#ifndef LIBCBOR_FLOATS_CTRLS_H
#define LIBCBOR_FLOATS_CTRLS_H

#include "cbor/cbor_export.h"
#include "cbor/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * Float manipulation
 * ============================================================================
 */

/** Is this a ctrl value?
 *
 * @param item[borrow] A float or ctrl item
 * @return Is this a ctrl value?
 */
_CBOR_NODISCARD CBOR_EXPORT bool cbor_float_ctrl_is_ctrl(
    const cbor_item_t *item);

/** Get the float width
 *
 * @param item[borrow] A float or ctrl item
 * @return The width.
 */
_CBOR_NODISCARD CBOR_EXPORT cbor_float_width
cbor_float_get_width(const cbor_item_t *item);

/** Get a half precision float
 *
 * The item must have the corresponding width
 *
 * @param[borrow] A half precision float
 * @return half precision value
 */
_CBOR_NODISCARD CBOR_EXPORT float cbor_float_get_float2(
    const cbor_item_t *item);

/** Get a single precision float
 *
 * The item must have the corresponding width
 *
 * @param[borrow] A single precision float
 * @return single precision value
 */
_CBOR_NODISCARD CBOR_EXPORT float cbor_float_get_float4(
    const cbor_item_t *item);

/** Get a double precision float
 *
 * The item must have the corresponding width
 *
 * @param[borrow] A double precision float
 * @return double precision value
 */
_CBOR_NODISCARD CBOR_EXPORT double cbor_float_get_float8(
    const cbor_item_t *item);

/** Get the float value represented as double
 *
 * Can be used regardless of the width.
 *
 * @param[borrow] Any float
 * @return double precision value
 */
_CBOR_NODISCARD CBOR_EXPORT double cbor_float_get_float(
    const cbor_item_t *item);

/** Get value from a boolean ctrl item
 *
 * @param item[borrow] A ctrl item
 * @return boolean value
 */
_CBOR_NODISCARD CBOR_EXPORT bool cbor_get_bool(const cbor_item_t *item);

/** Constructs a new ctrl item
 *
 * The width cannot be changed once the item is created
 *
 * @return **new** 1B ctrl or `NULL` upon memory allocation failure
 */
_CBOR_NODISCARD CBOR_EXPORT cbor_item_t *cbor_new_ctrl(void);

/** Constructs a new float item
 *
 * The width cannot be changed once the item is created
 *
 * @return **new** 2B float or `NULL` upon memory allocation failure
 */
_CBOR_NODISCARD CBOR_EXPORT cbor_item_t *cbor_new_float2(void);

/** Constructs a new float item
 *
 * The width cannot be changed once the item is created
 *
 * @return **new** 4B float or `NULL` upon memory allocation failure
 */
_CBOR_NODISCARD CBOR_EXPORT cbor_item_t *cbor_new_float4(void);

/** Constructs a new float item
 *
 * The width cannot be changed once the item is created
 *
 * @return **new** 8B float or `NULL` upon memory allocation failure
 */
_CBOR_NODISCARD CBOR_EXPORT cbor_item_t *cbor_new_float8(void);

/** Constructs new null ctrl item
 *
 * @return **new** null ctrl item or `NULL` upon memory allocation failure
 */
_CBOR_NODISCARD CBOR_EXPORT cbor_item_t *cbor_new_null(void);

/** Constructs new undef ctrl item
 *
 * @return **new** undef ctrl item or `NULL` upon memory allocation failure
 */
_CBOR_NODISCARD CBOR_EXPORT cbor_item_t *cbor_new_undef(void);

/** Constructs new boolean ctrl item
 *
 * @param value The value to use
 * @return **new** boolean ctrl item or `NULL` upon memory allocation failure
 */
_CBOR_NODISCARD CBOR_EXPORT cbor_item_t *cbor_build_bool(bool value);

/** Assign a control value
 *
 * \rst
 * .. warning:: It is possible to produce an invalid CBOR value by assigning a
 *  invalid value using this mechanism. Please consult the standard before use.
 * \endrst
 *
 * @param item[borrow] A ctrl item
 * @param value The simple value to assign. Please consult the standard for
 * 	allowed values
 */
CBOR_EXPORT void cbor_set_ctrl(cbor_item_t *item, uint8_t value);

/** Assign a boolean value to a boolean ctrl item
 *
 * @param item[borrow] A ctrl item
 * @param value The simple value to assign.
 */
CBOR_EXPORT void cbor_set_bool(cbor_item_t *item, bool value);

/** Assigns a float value
 *
 * @param item[borrow] A half precision float
 * @param value The value to assign
 */
CBOR_EXPORT void cbor_set_float2(cbor_item_t *item, float value);

/** Assigns a float value
 *
 * @param item[borrow] A single precision float
 * @param value The value to assign
 */
CBOR_EXPORT void cbor_set_float4(cbor_item_t *item, float value);

/** Assigns a float value
 *
 * @param item[borrow] A double precision float
 * @param value The value to assign
 */
CBOR_EXPORT void cbor_set_float8(cbor_item_t *item, double value);

/** Reads the control value
 *
 * @param item[borrow] A ctrl item
 * @return the simple value
 */
_CBOR_NODISCARD CBOR_EXPORT uint8_t cbor_ctrl_value(const cbor_item_t *item);

/** Constructs a new float
 *
 * @param value the value to use
 * @return **new** float
 */
_CBOR_NODISCARD CBOR_EXPORT cbor_item_t *cbor_build_float2(float value);

/** Constructs a new float
 *
 * @param value the value to use
 * @return **new** float or `NULL` upon memory allocation failure
 */
_CBOR_NODISCARD CBOR_EXPORT cbor_item_t *cbor_build_float4(float value);

/** Constructs a new float
 *
 * @param value the value to use
 * @return **new** float or `NULL` upon memory allocation failure
 */
_CBOR_NODISCARD CBOR_EXPORT cbor_item_t *cbor_build_float8(double value);

/** Constructs a ctrl item
 *
 * @param value the value to use
 * @return **new** ctrl item or `NULL` upon memory allocation failure
 */
_CBOR_NODISCARD CBOR_EXPORT cbor_item_t *cbor_build_ctrl(uint8_t value);

#ifdef __cplusplus
}
#endif

#endif  // LIBCBOR_FLOATS_CTRLS_H
