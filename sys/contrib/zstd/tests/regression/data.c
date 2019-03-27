/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#include "data.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <sys/stat.h>

#include <curl/curl.h>

#include "mem.h"
#include "util.h"
#define XXH_STATIC_LINKING_ONLY
#include "xxhash.h"

/**
 * Data objects
 */

#define REGRESSION_RELEASE(x) \
    "https://github.com/facebook/zstd/releases/download/regression-data/" x

data_t silesia = {
    .name = "silesia",
    .type = data_type_dir,
    .data =
        {
            .url = REGRESSION_RELEASE("silesia.tar.zst"),
            .xxhash64 = 0x48a199f92f93e977LL,
        },
};

data_t silesia_tar = {
    .name = "silesia.tar",
    .type = data_type_file,
    .data =
        {
            .url = REGRESSION_RELEASE("silesia.tar.zst"),
            .xxhash64 = 0x48a199f92f93e977LL,
        },
};

data_t github = {
    .name = "github",
    .type = data_type_dir,
    .data =
        {
            .url = REGRESSION_RELEASE("github.tar.zst"),
            .xxhash64 = 0xa9b1b44b020df292LL,
        },
    .dict =
        {
            .url = REGRESSION_RELEASE("github.dict.zst"),
            .xxhash64 = 0x1eddc6f737d3cb53LL,

        },
};

static data_t* g_data[] = {
    &silesia,
    &silesia_tar,
    &github,
    NULL,
};

data_t const* const* data = (data_t const* const*)g_data;

/**
 * data helpers.
 */

int data_has_dict(data_t const* data) {
    return data->dict.url != NULL;
}

/**
 * data buffer helper functions (documented in header).
 */

data_buffer_t data_buffer_create(size_t const capacity) {
    data_buffer_t buffer = {};

    buffer.data = (uint8_t*)malloc(capacity);
    if (buffer.data == NULL)
        return buffer;
    buffer.capacity = capacity;
    return buffer;
}

data_buffer_t data_buffer_read(char const* filename) {
    data_buffer_t buffer = {};

    uint64_t const size = UTIL_getFileSize(filename);
    if (size == UTIL_FILESIZE_UNKNOWN) {
        fprintf(stderr, "unknown size for %s\n", filename);
        return buffer;
    }

    buffer.data = (uint8_t*)malloc(size);
    if (buffer.data == NULL) {
        fprintf(stderr, "malloc failed\n");
        return buffer;
    }
    buffer.capacity = size;

    FILE* file = fopen(filename, "rb");
    if (file == NULL) {
        fprintf(stderr, "file null\n");
        goto err;
    }
    buffer.size = fread(buffer.data, 1, buffer.capacity, file);
    fclose(file);
    if (buffer.size != buffer.capacity) {
        fprintf(stderr, "read %zu != %zu\n", buffer.size, buffer.capacity);
        goto err;
    }

    return buffer;
err:
    free(buffer.data);
    memset(&buffer, 0, sizeof(buffer));
    return buffer;
}

data_buffer_t data_buffer_get_data(data_t const* data) {
    data_buffer_t const kEmptyBuffer = {};

    if (data->type != data_type_file)
        return kEmptyBuffer;

    return data_buffer_read(data->data.path);
}

data_buffer_t data_buffer_get_dict(data_t const* data) {
    data_buffer_t const kEmptyBuffer = {};

    if (!data_has_dict(data))
        return kEmptyBuffer;

    return data_buffer_read(data->dict.path);
}

int data_buffer_compare(data_buffer_t buffer1, data_buffer_t buffer2) {
    size_t const size =
        buffer1.size < buffer2.size ? buffer1.size : buffer2.size;
    int const cmp = memcmp(buffer1.data, buffer2.data, size);
    if (cmp != 0)
        return cmp;
    if (buffer1.size < buffer2.size)
        return -1;
    if (buffer1.size == buffer2.size)
        return 0;
    assert(buffer1.size > buffer2.size);
    return 1;
}

void data_buffer_free(data_buffer_t buffer) {
    free(buffer.data);
}

/**
 * data filenames helpers.
 */

data_filenames_t data_filenames_get(data_t const* data) {
    data_filenames_t filenames = {.buffer = NULL, .size = 0};
    char const* path = data->data.path;

    filenames.filenames = UTIL_createFileList(
        &path,
        1,
        &filenames.buffer,
        &filenames.size,
        /* followLinks */ 0);
    return filenames;
}

void data_filenames_free(data_filenames_t filenames) {
    UTIL_freeFileList(filenames.filenames, filenames.buffer);
}

/**
 * data buffers helpers.
 */

data_buffers_t data_buffers_get(data_t const* data) {
    data_buffers_t buffers = {.size = 0};
    data_filenames_t filenames = data_filenames_get(data);
    if (filenames.size == 0)
        return buffers;

    data_buffer_t* buffersPtr =
        (data_buffer_t*)malloc(filenames.size * sizeof(data_buffer_t));
    if (buffersPtr == NULL)
        return buffers;
    buffers.buffers = (data_buffer_t const*)buffersPtr;
    buffers.size = filenames.size;

    for (size_t i = 0; i < filenames.size; ++i) {
        buffersPtr[i] = data_buffer_read(filenames.filenames[i]);
        if (buffersPtr[i].data == NULL) {
            data_buffers_t const kEmptyBuffer = {};
            data_buffers_free(buffers);
            return kEmptyBuffer;
        }
    }

    return buffers;
}

/**
 * Frees the data buffers.
 */
void data_buffers_free(data_buffers_t buffers) {
    free((data_buffer_t*)buffers.buffers);
}

/**
 * Initialization and download functions.
 */

static char* g_data_dir = NULL;

/* mkdir -p */
static int ensure_directory_exists(char const* indir) {
    char* const dir = strdup(indir);
    char* end = dir;
    int ret = 0;
    if (dir == NULL) {
        ret = EINVAL;
        goto out;
    }
    do {
        /* Find the next directory level. */
        for (++end; *end != '\0' && *end != '/'; ++end)
            ;
        /* End the string there, make the directory, and restore the string. */
        char const save = *end;
        *end = '\0';
        int const isdir = UTIL_isDirectory(dir);
        ret = mkdir(dir, S_IRWXU);
        *end = save;
        /* Its okay if the directory already exists. */
        if (ret == 0 || (errno == EEXIST && isdir))
            continue;
        ret = errno;
        fprintf(stderr, "mkdir() failed\n");
        goto out;
    } while (*end != '\0');

    ret = 0;
out:
    free(dir);
    return ret;
}

/** Concatenate 3 strings into a new buffer. */
static char* cat3(char const* str1, char const* str2, char const* str3) {
    size_t const size1 = strlen(str1);
    size_t const size2 = strlen(str2);
    size_t const size3 = str3 == NULL ? 0 : strlen(str3);
    size_t const size = size1 + size2 + size3 + 1;
    char* const dst = (char*)malloc(size);
    if (dst == NULL)
        return NULL;
    strcpy(dst, str1);
    strcpy(dst + size1, str2);
    if (str3 != NULL)
        strcpy(dst + size1 + size2, str3);
    assert(strlen(dst) == size1 + size2 + size3);
    return dst;
}

static char* cat2(char const* str1, char const* str2) {
    return cat3(str1, str2, NULL);
}

/**
 * State needed by the curl callback.
 * It takes data from curl, hashes it, and writes it to the file.
 */
typedef struct {
    FILE* file;
    XXH64_state_t xxhash64;
    int error;
} curl_data_t;

/** Create the curl state. */
static curl_data_t curl_data_create(
    data_resource_t const* resource,
    data_type_t type) {
    curl_data_t cdata = {};

    XXH64_reset(&cdata.xxhash64, 0);

    assert(UTIL_isDirectory(g_data_dir));

    if (type == data_type_file) {
        /* Decompress the resource and store to the path. */
        char* cmd = cat3("zstd -dqfo '", resource->path, "'");
        if (cmd == NULL) {
            cdata.error = ENOMEM;
            return cdata;
        }
        cdata.file = popen(cmd, "w");
        free(cmd);
    } else {
        /* Decompress and extract the resource to the cache directory. */
        char* cmd = cat3("zstd -dc | tar -x -C '", g_data_dir, "'");
        if (cmd == NULL) {
            cdata.error = ENOMEM;
            return cdata;
        }
        cdata.file = popen(cmd, "w");
        free(cmd);
    }
    if (cdata.file == NULL) {
        cdata.error = errno;
    }

    return cdata;
}

/** Free the curl state. */
static int curl_data_free(curl_data_t cdata) {
    return pclose(cdata.file);
}

/** curl callback. Updates the hash, and writes to the file. */
static size_t curl_write(void* data, size_t size, size_t count, void* ptr) {
    curl_data_t* cdata = (curl_data_t*)ptr;
    size_t const written = fwrite(data, size, count, cdata->file);
    XXH64_update(&cdata->xxhash64, data, written * size);
    return written;
}

static int curl_download_resource(
    CURL* curl,
    data_resource_t const* resource,
    data_type_t type) {
    curl_data_t cdata;
    /* Download the data. */
    if (curl_easy_setopt(curl, CURLOPT_URL, resource->url) != 0)
        return EINVAL;
    if (curl_easy_setopt(curl, CURLOPT_WRITEDATA, &cdata) != 0)
        return EINVAL;
    cdata = curl_data_create(resource, type);
    if (cdata.error != 0)
        return cdata.error;
    int const curl_err = curl_easy_perform(curl);
    int const close_err = curl_data_free(cdata);
    if (curl_err) {
        fprintf(
            stderr,
            "downloading '%s' for '%s' failed\n",
            resource->url,
            resource->path);
        return EIO;
    }
    if (close_err) {
        fprintf(stderr, "writing data to '%s' failed\n", resource->path);
        return EIO;
    }
    /* check that the file exists. */
    if (type == data_type_file && !UTIL_isRegularFile(resource->path)) {
        fprintf(stderr, "output file '%s' does not exist\n", resource->path);
        return EIO;
    }
    if (type == data_type_dir && !UTIL_isDirectory(resource->path)) {
        fprintf(
            stderr, "output directory '%s' does not exist\n", resource->path);
        return EIO;
    }
    /* Check that the hash matches. */
    if (XXH64_digest(&cdata.xxhash64) != resource->xxhash64) {
        fprintf(
            stderr,
            "checksum does not match: 0x%llxLL != 0x%llxLL\n",
            (unsigned long long)XXH64_digest(&cdata.xxhash64),
            (unsigned long long)resource->xxhash64);
        return EINVAL;
    }

    return 0;
}

/** Download a single data object. */
static int curl_download_datum(CURL* curl, data_t const* data) {
    int ret;
    ret = curl_download_resource(curl, &data->data, data->type);
    if (ret != 0)
        return ret;
    if (data_has_dict(data)) {
        ret = curl_download_resource(curl, &data->dict, data_type_file);
        if (ret != 0)
            return ret;
    }
    return ret;
}

/** Download all the data. */
static int curl_download_data(data_t const* const* data) {
    if (curl_global_init(CURL_GLOBAL_ALL) != 0)
        return EFAULT;

    curl_data_t cdata = {};
    CURL* curl = curl_easy_init();
    int err = EFAULT;

    if (curl == NULL)
        return EFAULT;

    if (curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L) != 0)
        goto out;
    if (curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L) != 0)
        goto out;
    if (curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write) != 0)
        goto out;

    assert(data != NULL);
    for (; *data != NULL; ++data) {
        if (curl_download_datum(curl, *data) != 0)
            goto out;
    }

    err = 0;
out:
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return err;
}

/** Fill the path member variable of the data objects. */
static int data_create_paths(data_t* const* data, char const* dir) {
    size_t const dirlen = strlen(dir);
    assert(data != NULL);
    for (; *data != NULL; ++data) {
        data_t* const datum = *data;
        datum->data.path = cat3(dir, "/", datum->name);
        if (datum->data.path == NULL)
            return ENOMEM;
        if (data_has_dict(datum)) {
            datum->dict.path = cat2(datum->data.path, ".dict");
            if (datum->dict.path == NULL)
                return ENOMEM;
        }
    }
    return 0;
}

/** Free the path member variable of the data objects. */
static void data_free_paths(data_t* const* data) {
    assert(data != NULL);
    for (; *data != NULL; ++data) {
        data_t* datum = *data;
        free((void*)datum->data.path);
        free((void*)datum->dict.path);
        datum->data.path = NULL;
        datum->dict.path = NULL;
    }
}

static char const kStampName[] = "STAMP";

static void xxh_update_le(XXH64_state_t* state, uint64_t data) {
    if (!MEM_isLittleEndian())
        data = MEM_swap64(data);
    XXH64_update(state, &data, sizeof(data));
}

/** Hash the data to create the stamp. */
static uint64_t stamp_hash(data_t const* const* data) {
    XXH64_state_t state;

    XXH64_reset(&state, 0);
    assert(data != NULL);
    for (; *data != NULL; ++data) {
        data_t const* datum = *data;
        /* We don't care about the URL that we fetch from. */
        /* The path is derived from the name. */
        XXH64_update(&state, datum->name, strlen(datum->name));
        xxh_update_le(&state, datum->data.xxhash64);
        xxh_update_le(&state, datum->dict.xxhash64);
        xxh_update_le(&state, datum->type);
    }
    return XXH64_digest(&state);
}

/** Check if the stamp matches the stamp in the cache directory. */
static int stamp_check(char const* dir, data_t const* const* data) {
    char* stamp = cat3(dir, "/", kStampName);
    uint64_t const expected = stamp_hash(data);
    XXH64_canonical_t actual;
    FILE* stampfile = NULL;
    int matches = 0;

    if (stamp == NULL)
        goto out;
    if (!UTIL_isRegularFile(stamp)) {
        fprintf(stderr, "stamp does not exist: recreating the data cache\n");
        goto out;
    }

    stampfile = fopen(stamp, "rb");
    if (stampfile == NULL) {
        fprintf(stderr, "could not open stamp: recreating the data cache\n");
        goto out;
    }

    size_t b;
    if ((b = fread(&actual, sizeof(actual), 1, stampfile)) != 1) {
        fprintf(stderr, "invalid stamp: recreating the data cache\n");
        goto out;
    }

    matches = (expected == XXH64_hashFromCanonical(&actual));
    if (matches)
        fprintf(stderr, "stamp matches: reusing the cached data\n");
    else
        fprintf(stderr, "stamp does not match: recreating the data cache\n");

out:
    free(stamp);
    if (stampfile != NULL)
        fclose(stampfile);
    return matches;
}

/** On success write a new stamp, on failure delete the old stamp. */
static int
stamp_write(char const* dir, data_t const* const* data, int const data_err) {
    char* stamp = cat3(dir, "/", kStampName);
    FILE* stampfile = NULL;
    int err = EIO;

    if (stamp == NULL)
        return ENOMEM;

    if (data_err != 0) {
        err = data_err;
        goto out;
    }
    XXH64_canonical_t hash;

    XXH64_canonicalFromHash(&hash, stamp_hash(data));

    stampfile = fopen(stamp, "wb");
    if (stampfile == NULL)
        goto out;
    if (fwrite(&hash, sizeof(hash), 1, stampfile) != 1)
        goto out;
    err = 0;
    fprintf(stderr, "stamped new data cache\n");
out:
    if (err != 0)
        /* Ignore errors. */
        unlink(stamp);
    free(stamp);
    if (stampfile != NULL)
        fclose(stampfile);
    return err;
}

int data_init(char const* dir) {
    int err;

    if (dir == NULL)
        return EINVAL;

    /* This must be first to simplify logic. */
    err = ensure_directory_exists(dir);
    if (err != 0)
        return err;

    /* Save the cache directory. */
    g_data_dir = strdup(dir);
    if (g_data_dir == NULL)
        return ENOMEM;

    err = data_create_paths(g_data, dir);
    if (err != 0)
        return err;

    /* If the stamp matches then we are good to go.
     * This must be called before any modifications to the data cache.
     * After this point, we MUST call stamp_write() to update the STAMP,
     * since we've updated the data cache.
     */
    if (stamp_check(dir, data))
        return 0;

    err = curl_download_data(data);
    if (err != 0)
        goto out;

out:
    /* This must be last, since it must know if data_init() succeeded. */
    stamp_write(dir, data, err);
    return err;
}

void data_finish(void) {
    data_free_paths(g_data);
    free(g_data_dir);
    g_data_dir = NULL;
}
