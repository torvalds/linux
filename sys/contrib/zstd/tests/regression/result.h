/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#ifndef RESULT_H
#define RESULT_H

#include <stddef.h>

/**
 * The error type enum.
 */
typedef enum {
    result_error_ok,                   /**< No error. */
    result_error_skip,                 /**< This method was skipped. */
    result_error_system_error,         /**< Some internal error happened. */
    result_error_compression_error,    /**< Compression failed. */
    result_error_decompression_error,  /**< Decompression failed. */
    result_error_round_trip_error,     /**< Data failed to round trip. */
} result_error_t;

/**
 * The success type.
 */
typedef struct {
    size_t total_size;  /**< The total compressed size. */
} result_data_t;

/**
 * The result type.
 * Do not access the member variables directory, use the helper functions.
 */
typedef struct {
    result_error_t internal_error;
    result_data_t internal_data;
} result_t;

/**
 * Create a result of the error type.
 */
static result_t result_error(result_error_t error);
/**
 * Create a result of the success type.
 */
static result_t result_data(result_data_t data);

/**
 * Check if the result is an error or skip.
 */
static int result_is_error(result_t result);
/**
 * Check if the result error is skip.
 */
static int result_is_skip(result_t result);
/**
 * Get the result error or okay.
 */
static result_error_t result_get_error(result_t result);
/**
 * Get the result data. The result MUST be checked with result_is_error() first.
 */
static result_data_t result_get_data(result_t result);

static result_t result_error(result_error_t error) {
    result_t result = {
        .internal_error = error,
    };
    return result;
}

static result_t result_data(result_data_t data) {
    result_t result = {
        .internal_error = result_error_ok,
        .internal_data = data,
    };
    return result;
}

static int result_is_error(result_t result) {
    return result_get_error(result) != result_error_ok;
}

static int result_is_skip(result_t result) {
    return result_get_error(result) == result_error_skip;
}

static result_error_t result_get_error(result_t result) {
    return result.internal_error;
}

char const* result_get_error_string(result_t result);

static result_data_t result_get_data(result_t result) {
    return result.internal_data;
}

#endif
