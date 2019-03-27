/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#include "config.h"

/* Define a config for each fast level we want to test with. */
#define FAST_LEVEL(x)                                               \
    param_value_t const level_fast##x##_param_values[] = {          \
        {.param = ZSTD_c_compressionLevel, .value = -x},            \
    };                                                              \
    config_t const level_fast##x = {                                \
        .name = "level -" #x,                                       \
        .cli_args = "--fast=" #x,                                   \
        .param_values = PARAM_VALUES(level_fast##x##_param_values), \
    };                                                              \
    config_t const level_fast##x##_dict = {                         \
        .name = "level -" #x " with dict",                          \
        .cli_args = "--fast=" #x,                                   \
        .param_values = PARAM_VALUES(level_fast##x##_param_values), \
        .use_dictionary = 1,                                        \
    };

/* Define a config for each level we want to test with. */
#define LEVEL(x)                                                \
    param_value_t const level_##x##_param_values[] = {          \
        {.param = ZSTD_c_compressionLevel, .value = x},         \
    };                                                          \
    config_t const level_##x = {                                \
        .name = "level " #x,                                    \
        .cli_args = "-" #x,                                     \
        .param_values = PARAM_VALUES(level_##x##_param_values), \
    };                                                          \
    config_t const level_##x##_dict = {                         \
        .name = "level " #x " with dict",                       \
        .cli_args = "-" #x,                                     \
        .param_values = PARAM_VALUES(level_##x##_param_values), \
        .use_dictionary = 1,                                    \
    };

#define PARAM_VALUES(pv) \
    { .data = pv, .size = sizeof(pv) / sizeof((pv)[0]) }

#include "levels.h"

#undef LEVEL
#undef FAST_LEVEL

static config_t no_pledged_src_size = {
    .name = "no source size",
    .cli_args = "",
    .param_values = PARAM_VALUES(level_0_param_values),
    .no_pledged_src_size = 1,
};

static param_value_t const ldm_param_values[] = {
    {.param = ZSTD_c_enableLongDistanceMatching, .value = 1},
};

static config_t ldm = {
    .name = "long distance mode",
    .cli_args = "--long",
    .param_values = PARAM_VALUES(ldm_param_values),
};

static param_value_t const mt_param_values[] = {
    {.param = ZSTD_c_nbWorkers, .value = 2},
};

static config_t mt = {
    .name = "multithreaded",
    .cli_args = "-T2",
    .param_values = PARAM_VALUES(mt_param_values),
};

static param_value_t const mt_ldm_param_values[] = {
    {.param = ZSTD_c_nbWorkers, .value = 2},
    {.param = ZSTD_c_enableLongDistanceMatching, .value = 1},
};

static config_t mt_ldm = {
    .name = "multithreaded long distance mode",
    .cli_args = "-T2 --long",
    .param_values = PARAM_VALUES(mt_ldm_param_values),
};

static param_value_t const small_wlog_param_values[] = {
    {.param = ZSTD_c_windowLog, .value = 10},
};

static config_t small_wlog = {
    .name = "small window log",
    .cli_args = "--zstd=wlog=10",
    .param_values = PARAM_VALUES(small_wlog_param_values),
};

static param_value_t const small_hlog_param_values[] = {
    {.param = ZSTD_c_hashLog, .value = 6},
    {.param = ZSTD_c_strategy, .value = (int)ZSTD_btopt},
};

static config_t small_hlog = {
    .name = "small hash log",
    .cli_args = "--zstd=hlog=6,strat=7",
    .param_values = PARAM_VALUES(small_hlog_param_values),
};

static param_value_t const small_clog_param_values[] = {
    {.param = ZSTD_c_chainLog, .value = 6},
    {.param = ZSTD_c_strategy, .value = (int)ZSTD_btopt},
};

static config_t small_clog = {
    .name = "small chain log",
    .cli_args = "--zstd=clog=6,strat=7",
    .param_values = PARAM_VALUES(small_clog_param_values),
};

static param_value_t const explicit_params_param_values[] = {
    {.param = ZSTD_c_checksumFlag, .value = 1},
    {.param = ZSTD_c_contentSizeFlag, .value = 0},
    {.param = ZSTD_c_dictIDFlag, .value = 0},
    {.param = ZSTD_c_strategy, .value = (int)ZSTD_greedy},
    {.param = ZSTD_c_windowLog, .value = 18},
    {.param = ZSTD_c_hashLog, .value = 21},
    {.param = ZSTD_c_chainLog, .value = 21},
    {.param = ZSTD_c_targetLength, .value = 100},
};

static config_t explicit_params = {
    .name = "explicit params",
    .cli_args = "--no-check --no-dictID --zstd=strategy=3,wlog=18,hlog=21,clog=21,tlen=100",
    .param_values = PARAM_VALUES(explicit_params_param_values),
};

static config_t const* g_configs[] = {

#define FAST_LEVEL(x) &level_fast##x, &level_fast##x##_dict,
#define LEVEL(x) &level_##x, &level_##x##_dict,
#include "levels.h"
#undef LEVEL
#undef FAST_LEVEL

    &no_pledged_src_size,
    &ldm,
    &mt,
    &mt_ldm,
    &small_wlog,
    &small_hlog,
    &small_clog,
    &explicit_params,
    NULL,
};

config_t const* const* configs = g_configs;

int config_skip_data(config_t const* config, data_t const* data) {
    return config->use_dictionary && !data_has_dict(data);
}

int config_get_level(config_t const* config)
{
    param_values_t const params = config->param_values;
    size_t i;
    for (i = 0; i < params.size; ++i) {
        if (params.data[i].param == ZSTD_c_compressionLevel)
            return (int)params.data[i].value;
    }
    return CONFIG_NO_LEVEL;
}

ZSTD_parameters config_get_zstd_params(
    config_t const* config,
    uint64_t srcSize,
    size_t dictSize)
{
    ZSTD_parameters zparams = {};
    param_values_t const params = config->param_values;
    int level = config_get_level(config);
    if (level == CONFIG_NO_LEVEL)
        level = 3;
    zparams = ZSTD_getParams(
        level,
        config->no_pledged_src_size ? ZSTD_CONTENTSIZE_UNKNOWN : srcSize,
        dictSize);
    for (size_t i = 0; i < params.size; ++i) {
        unsigned const value = params.data[i].value;
        switch (params.data[i].param) {
            case ZSTD_c_contentSizeFlag:
                zparams.fParams.contentSizeFlag = value;
                break;
            case ZSTD_c_checksumFlag:
                zparams.fParams.checksumFlag = value;
                break;
            case ZSTD_c_dictIDFlag:
                zparams.fParams.noDictIDFlag = !value;
                break;
            case ZSTD_c_windowLog:
                zparams.cParams.windowLog = value;
                break;
            case ZSTD_c_chainLog:
                zparams.cParams.chainLog = value;
                break;
            case ZSTD_c_hashLog:
                zparams.cParams.hashLog = value;
                break;
            case ZSTD_c_searchLog:
                zparams.cParams.searchLog = value;
                break;
            case ZSTD_c_minMatch:
                zparams.cParams.minMatch = value;
                break;
            case ZSTD_c_targetLength:
                zparams.cParams.targetLength = value;
                break;
            case ZSTD_c_strategy:
                zparams.cParams.strategy = (ZSTD_strategy)value;
                break;
            default:
                break;
        }
    }
    return zparams;
}
