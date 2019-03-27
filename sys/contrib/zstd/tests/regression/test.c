/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#include <assert.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "data.h"
#include "method.h"

static int g_max_name_len = 0;

/** Check if a name contains a comma or is too long. */
static int is_name_bad(char const* name) {
    if (name == NULL)
        return 1;
    int const len = strlen(name);
    if (len > g_max_name_len)
        g_max_name_len = len;
    for (; *name != '\0'; ++name)
        if (*name == ',')
            return 1;
    return 0;
}

/** Check if any of the names contain a comma. */
static int are_names_bad() {
    for (size_t method = 0; methods[method] != NULL; ++method)
        if (is_name_bad(methods[method]->name)) {
            fprintf(stderr, "method name %s is bad\n", methods[method]->name);
            return 1;
        }
    for (size_t datum = 0; data[datum] != NULL; ++datum)
        if (is_name_bad(data[datum]->name)) {
            fprintf(stderr, "data name %s is bad\n", data[datum]->name);
            return 1;
        }
    for (size_t config = 0; configs[config] != NULL; ++config)
        if (is_name_bad(configs[config]->name)) {
            fprintf(stderr, "config name %s is bad\n", configs[config]->name);
            return 1;
        }
    return 0;
}

/**
 * Option parsing using getopt.
 * When you add a new option update: long_options, long_extras, and
 * short_options.
 */

/** Option variables filled by parse_args. */
static char const* g_output = NULL;
static char const* g_diff = NULL;
static char const* g_cache = NULL;
static char const* g_zstdcli = NULL;
static char const* g_config = NULL;
static char const* g_data = NULL;
static char const* g_method = NULL;

typedef enum {
    required_option,
    optional_option,
    help_option,
} option_type;

/**
 * Extra state that we need to keep per-option that we can't store in getopt.
 */
struct option_extra {
    int id; /**< The short option name, used as an id. */
    char const* help; /**< The help message. */
    option_type opt_type; /**< The option type: required, optional, or help. */
    char const** value; /**< The value to set or NULL if no_argument. */
};

/** The options. */
static struct option long_options[] = {
    {"cache", required_argument, NULL, 'c'},
    {"output", required_argument, NULL, 'o'},
    {"zstd", required_argument, NULL, 'z'},
    {"config", required_argument, NULL, 128},
    {"data", required_argument, NULL, 129},
    {"method", required_argument, NULL, 130},
    {"diff", required_argument, NULL, 'd'},
    {"help", no_argument, NULL, 'h'},
};

static size_t const nargs = sizeof(long_options) / sizeof(long_options[0]);

/** The extra info for the options. Must be in the same order as the options. */
static struct option_extra long_extras[] = {
    {'c', "the cache directory", required_option, &g_cache},
    {'o', "write the results here", required_option, &g_output},
    {'z', "zstd cli tool", required_option, &g_zstdcli},
    {128, "use this config", optional_option, &g_config},
    {129, "use this data", optional_option, &g_data},
    {130, "use this method", optional_option, &g_method},
    {'d', "compare the results to this file", optional_option, &g_diff},
    {'h', "display this message", help_option, NULL},
};

/** The short options. Must correspond to the options. */
static char const short_options[] = "c:d:ho:z:";

/** Return the help string for the option type. */
static char const* required_message(option_type opt_type) {
    switch (opt_type) {
        case required_option:
            return "[required]";
        case optional_option:
            return "[optional]";
        case help_option:
            return "";
        default:
            assert(0);
            return NULL;
    }
}

/** Print the help for the program. */
static void print_help(void) {
    fprintf(stderr, "regression test runner\n");
    size_t const nargs = sizeof(long_options) / sizeof(long_options[0]);
    for (size_t i = 0; i < nargs; ++i) {
        if (long_options[i].val < 128) {
            /* Long / short  - help [option type] */
            fprintf(
                stderr,
                "--%s / -%c \t- %s %s\n",
                long_options[i].name,
                long_options[i].val,
                long_extras[i].help,
                required_message(long_extras[i].opt_type));
        } else {
            /* Short / long  - help [option type] */
            fprintf(
                stderr,
                "--%s      \t- %s %s\n",
                long_options[i].name,
                long_extras[i].help,
                required_message(long_extras[i].opt_type));
        }
    }
}

/** Parse the arguments. Teturn 0 on success. Print help on failure. */
static int parse_args(int argc, char** argv) {
    int option_index = 0;
    int c;

    while (1) {
        c = getopt_long(argc, argv, short_options, long_options, &option_index);
        if (c == -1)
            break;

        int found = 0;
        for (size_t i = 0; i < nargs; ++i) {
            if (c == long_extras[i].id && long_extras[i].value != NULL) {
                *long_extras[i].value = optarg;
                found = 1;
                break;
            }
        }
        if (found)
            continue;

        switch (c) {
            case 'h':
            case '?':
            default:
                print_help();
                return 1;
        }
    }

    int bad = 0;
    for (size_t i = 0; i < nargs; ++i) {
        if (long_extras[i].opt_type != required_option)
            continue;
        if (long_extras[i].value == NULL)
            continue;
        if (*long_extras[i].value != NULL)
            continue;
        fprintf(
            stderr,
            "--%s is a required argument but is not set\n",
            long_options[i].name);
        bad = 1;
    }
    if (bad) {
        fprintf(stderr, "\n");
        print_help();
        return 1;
    }

    return 0;
}

/** Helper macro to print to stderr and a file. */
#define tprintf(file, ...)            \
    do {                              \
        fprintf(file, __VA_ARGS__);   \
        fprintf(stderr, __VA_ARGS__); \
    } while (0)
/** Helper macro to flush stderr and a file. */
#define tflush(file)    \
    do {                \
        fflush(file);   \
        fflush(stderr); \
    } while (0)

void tprint_names(
    FILE* results,
    char const* data_name,
    char const* config_name,
    char const* method_name) {
    int const data_padding = g_max_name_len - strlen(data_name);
    int const config_padding = g_max_name_len - strlen(config_name);
    int const method_padding = g_max_name_len - strlen(method_name);

    tprintf(
        results,
        "%s, %*s%s, %*s%s, %*s",
        data_name,
        data_padding,
        "",
        config_name,
        config_padding,
        "",
        method_name,
        method_padding,
        "");
}

/**
 * Run all the regression tests and record the results table to results and
 * stderr progressively.
 */
static int run_all(FILE* results) {
    tprint_names(results, "Data", "Config", "Method");
    tprintf(results, "Total compressed size\n");
    for (size_t method = 0; methods[method] != NULL; ++method) {
        if (g_method != NULL && strcmp(methods[method]->name, g_method))
            continue;
        for (size_t datum = 0; data[datum] != NULL; ++datum) {
            if (g_data != NULL && strcmp(data[datum]->name, g_data))
                continue;
            /* Create the state common to all configs */
            method_state_t* state = methods[method]->create(data[datum]);
            for (size_t config = 0; configs[config] != NULL; ++config) {
                if (g_config != NULL && strcmp(configs[config]->name, g_config))
                    continue;
                if (config_skip_data(configs[config], data[datum]))
                    continue;
                /* Print the result for the (method, data, config) tuple. */
                result_t const result =
                    methods[method]->compress(state, configs[config]);
                if (result_is_skip(result))
                    continue;
                tprint_names(
                    results,
                    data[datum]->name,
                    configs[config]->name,
                    methods[method]->name);
                if (result_is_error(result)) {
                    tprintf(results, "%s\n", result_get_error_string(result));
                } else {
                    tprintf(
                        results,
                        "%llu\n",
                        (unsigned long long)result_get_data(result).total_size);
                }
                tflush(results);
            }
            methods[method]->destroy(state);
        }
    }
    return 0;
}

/** memcmp() the old results file and the new results file. */
static int diff_results(char const* actual_file, char const* expected_file) {
    data_buffer_t const actual = data_buffer_read(actual_file);
    data_buffer_t const expected = data_buffer_read(expected_file);
    int ret = 1;

    if (actual.data == NULL) {
        fprintf(stderr, "failed to open results '%s' for diff\n", actual_file);
        goto out;
    }
    if (expected.data == NULL) {
        fprintf(
            stderr,
            "failed to open previous results '%s' for diff\n",
            expected_file);
        goto out;
    }

    ret = data_buffer_compare(actual, expected);
    if (ret != 0) {
        fprintf(
            stderr,
            "actual results '%s' does not match expected results '%s'\n",
            actual_file,
            expected_file);
    } else {
        fprintf(stderr, "actual results match expected results\n");
    }
out:
    data_buffer_free(actual);
    data_buffer_free(expected);
    return ret;
}

int main(int argc, char** argv) {
    /* Parse args and validate modules. */
    int ret = parse_args(argc, argv);
    if (ret != 0)
        return ret;

    if (are_names_bad())
        return 1;

    /* Initialize modules. */
    method_set_zstdcli(g_zstdcli);
    ret = data_init(g_cache);
    if (ret != 0) {
        fprintf(stderr, "data_init() failed with error=%s\n", strerror(ret));
        return 1;
    }

    /* Run the regression tests. */
    ret = 1;
    FILE* results = fopen(g_output, "w");
    if (results == NULL) {
        fprintf(stderr, "Failed to open the output file\n");
        goto out;
    }
    ret = run_all(results);
    fclose(results);

    if (ret != 0)
        goto out;

    if (g_diff)
        /* Diff the new results with the previous results. */
        ret = diff_results(g_output, g_diff);

out:
    data_finish();
    return ret;
}
