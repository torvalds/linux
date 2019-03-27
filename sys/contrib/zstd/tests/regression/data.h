/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#ifndef DATA_H
#define DATA_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    data_type_file = 1,  /**< This data is a file. *.zst */
    data_type_dir = 2,   /**< This data is a directory. *.tar.zst */
} data_type_t;

typedef struct {
    char const* url;   /**< Where to get this resource. */
    uint64_t xxhash64; /**< Hash of the url contents. */
    char const* path;  /**< The path of the unpacked resource (derived). */
} data_resource_t;

typedef struct {
    data_resource_t data;
    data_resource_t dict;
    data_type_t type;  /**< The type of the data. */
    char const* name;  /**< The logical name of the data (no extension). */
} data_t;

/**
 * The NULL-terminated list of data objects.
 */
extern data_t const* const* data;


int data_has_dict(data_t const* data);

/**
 * Initializes the data module and downloads the data necessary.
 * Caches the downloads in dir. We add a stamp file in the directory after
 * a successful download. If a stamp file already exists, and matches our
 * current data stamp, we will use the cached data without downloading.
 *
 * @param dir The directory to cache the downloaded data into.
 *
 * @returns 0 on success.
 */
int data_init(char const* dir);

/**
 * Must be called at exit to free resources allocated by data_init().
 */
void data_finish(void);

typedef struct {
    uint8_t* data;
    size_t size;
    size_t capacity;
} data_buffer_t;

/**
 * Read the file that data points to into a buffer.
 * NOTE: data must be a file, not a directory.
 *
 * @returns The buffer, which is NULL on failure.
 */
data_buffer_t data_buffer_get_data(data_t const* data);

/**
 * Read the dictionary that the data points to into a buffer.
 *
 * @returns The buffer, which is NULL on failure.
 */
data_buffer_t data_buffer_get_dict(data_t const* data);

/**
 * Read the contents of filename into a buffer.
 *
 * @returns The buffer, which is NULL on failure.
 */
data_buffer_t data_buffer_read(char const* filename);

/**
 * Create a buffer with the specified capacity.
 *
 * @returns The buffer, which is NULL on failure.
 */
data_buffer_t data_buffer_create(size_t capacity);

/**
 * Calls memcmp() on the contents [0, size) of both buffers.
 */
int data_buffer_compare(data_buffer_t buffer1, data_buffer_t buffer2);

/**
 * Frees an allocated buffer.
 */
void data_buffer_free(data_buffer_t buffer);

typedef struct {
    char* buffer;
    char const** filenames;
    unsigned size;
} data_filenames_t;

/**
 * Get a recursive list of filenames in the data object. If it is a file, it
 * will only contain one entry. If it is a directory, it will recursively walk
 * the directory.
 *
 * @returns The list of filenames, which has size 0 and NULL pointers on error.
 */
data_filenames_t data_filenames_get(data_t const* data);

/**
 * Frees the filenames table.
 */
void data_filenames_free(data_filenames_t filenames);

typedef struct {
    data_buffer_t const* buffers;
    size_t size;
} data_buffers_t;

/**
 * @returns a list of buffers for every file in data. It is zero sized on error.
 */
data_buffers_t data_buffers_get(data_t const* data);

/**
 * Frees the data buffers.
 */
void data_buffers_free(data_buffers_t buffers);

#endif
