/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>

#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>

#include "data.h"

typedef struct {
    ZSTD_cParameter param;
    int value;
} param_value_t;

typedef struct {
    size_t size;
    param_value_t const* data;
} param_values_t;

/**
 * The config tells the compression method what options to use.
 */
typedef struct {
    const char* name;  /**< Identifies the config in the results table */
    /**
     * Optional arguments to pass to the CLI. If not set, CLI-based methods
     * will skip this config.
     */
    char const* cli_args;
    /**
     * Parameters to pass to the advanced API. If the advanced API isn't used,
     * the parameters will be derived from these.
     */
    param_values_t param_values;
    /**
     * Boolean parameter that says if we should use a dictionary. If the data
     * doesn't have a dictionary, this config is skipped. Defaults to no.
     */
    int use_dictionary;
    /**
     * Boolean parameter that says if we should pass the pledged source size
     * when the method allows it. Defaults to yes.
     */
    int no_pledged_src_size;
} config_t;

/**
 * Returns true if the config should skip this data.
 * For instance, if the config requires a dictionary but the data doesn't have
 * one.
 */
int config_skip_data(config_t const* config, data_t const* data);

#define CONFIG_NO_LEVEL (-ZSTD_TARGETLENGTH_MAX - 1)
/**
 * Returns the compression level specified by the config, or CONFIG_NO_LEVEL if
 * no level is specified. Note that 0 is a valid compression level, meaning
 * default.
 */
int config_get_level(config_t const* config);

/**
 * Returns the compression parameters specified by the config.
 */
ZSTD_parameters config_get_zstd_params(
    config_t const* config,
    uint64_t srcSize,
    size_t dictSize);

/**
 * The NULL-terminated list of configs.
 */
extern config_t const* const* configs;

#endif
