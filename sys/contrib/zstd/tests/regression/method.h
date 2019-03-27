/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#ifndef METHOD_H
#define METHOD_H

#include <stddef.h>

#include "data.h"
#include "config.h"
#include "result.h"

/**
 * The base class for state that methods keep.
 * All derived method state classes must have a member of this type.
 */
typedef struct {
    data_t const* data;
} method_state_t;

/**
 * A method that compresses the data using config.
 */
typedef struct {
    char const* name;  /**< The identifier for this method in the results. */
    /**
     * Creates a state that must contain a member variable of method_state_t,
     * and returns a pointer to that member variable.
     *
     * This method can be used to do expensive work that only depends on the
     * data, like loading the data file into a buffer.
     */
    method_state_t* (*create)(data_t const* data);
    /**
     * Compresses the data in the state using the given config.
     *
     * @param state A pointer to the state returned by create().
     *
     * @returns The total compressed size on success, or an error code.
     */
    result_t (*compress)(method_state_t* state, config_t const* config);
    /**
     * Frees the state.
     */
    void (*destroy)(method_state_t* state);
} method_t;

/**
 * Set the zstd cli path. Must be called before any methods are used.
 */
void method_set_zstdcli(char const* zstdcli);

/**
 * A NULL-terminated list of methods.
 */
extern method_t const* const* methods;

#endif
