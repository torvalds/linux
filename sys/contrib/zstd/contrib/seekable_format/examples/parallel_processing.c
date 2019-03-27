/*
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 */

/*
 * A simple demo that sums up all the bytes in the file in parallel using
 * seekable decompression and the zstd thread pool
 */

#include <stdlib.h>    // malloc, exit
#include <stdio.h>     // fprintf, perror, feof
#include <string.h>    // strerror
#include <errno.h>     // errno
#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>      // presumes zstd library is installed
#include <zstd_errors.h>
#if defined(WIN32) || defined(_WIN32)
#  include <windows.h>
#  define SLEEP(x) Sleep(x)
#else
#  include <unistd.h>
#  define SLEEP(x) usleep(x * 1000)
#endif

#include "pool.h"      // use zstd thread pool for demo

#include "zstd_seekable.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))

static void* malloc_orDie(size_t size)
{
    void* const buff = malloc(size);
    if (buff) return buff;
    /* error */
    perror("malloc");
    exit(1);
}

static void* realloc_orDie(void* ptr, size_t size)
{
    ptr = realloc(ptr, size);
    if (ptr) return ptr;
    /* error */
    perror("realloc");
    exit(1);
}

static FILE* fopen_orDie(const char *filename, const char *instruction)
{
    FILE* const inFile = fopen(filename, instruction);
    if (inFile) return inFile;
    /* error */
    perror(filename);
    exit(3);
}

static size_t fread_orDie(void* buffer, size_t sizeToRead, FILE* file)
{
    size_t const readSize = fread(buffer, 1, sizeToRead, file);
    if (readSize == sizeToRead) return readSize;   /* good */
    if (feof(file)) return readSize;   /* good, reached end of file */
    /* error */
    perror("fread");
    exit(4);
}

static size_t fwrite_orDie(const void* buffer, size_t sizeToWrite, FILE* file)
{
    size_t const writtenSize = fwrite(buffer, 1, sizeToWrite, file);
    if (writtenSize == sizeToWrite) return sizeToWrite;   /* good */
    /* error */
    perror("fwrite");
    exit(5);
}

static size_t fclose_orDie(FILE* file)
{
    if (!fclose(file)) return 0;
    /* error */
    perror("fclose");
    exit(6);
}

static void fseek_orDie(FILE* file, long int offset, int origin) {
    if (!fseek(file, offset, origin)) {
        if (!fflush(file)) return;
    }
    /* error */
    perror("fseek");
    exit(7);
}

struct sum_job {
    const char* fname;
    unsigned long long sum;
    unsigned frameNb;
    int done;
};

static void sumFrame(void* opaque)
{
    struct sum_job* job = (struct sum_job*)opaque;
    job->done = 0;

    FILE* const fin = fopen_orDie(job->fname, "rb");

    ZSTD_seekable* const seekable = ZSTD_seekable_create();
    if (seekable==NULL) { fprintf(stderr, "ZSTD_seekable_create() error \n"); exit(10); }

    size_t const initResult = ZSTD_seekable_initFile(seekable, fin);
    if (ZSTD_isError(initResult)) { fprintf(stderr, "ZSTD_seekable_init() error : %s \n", ZSTD_getErrorName(initResult)); exit(11); }

    size_t const frameSize = ZSTD_seekable_getFrameDecompressedSize(seekable, job->frameNb);
    unsigned char* data = malloc_orDie(frameSize);

    size_t result = ZSTD_seekable_decompressFrame(seekable, data, frameSize, job->frameNb);
    if (ZSTD_isError(result)) { fprintf(stderr, "ZSTD_seekable_decompressFrame() error : %s \n", ZSTD_getErrorName(result)); exit(12); }

    unsigned long long sum = 0;
    size_t i;
    for (i = 0; i < frameSize; i++) {
        sum += data[i];
    }
    job->sum = sum;
    job->done = 1;

    fclose(fin);
    ZSTD_seekable_free(seekable);
    free(data);
}

static void sumFile_orDie(const char* fname, int nbThreads)
{
    POOL_ctx* pool = POOL_create(nbThreads, nbThreads);
    if (pool == NULL) { fprintf(stderr, "POOL_create() error \n"); exit(9); }

    FILE* const fin = fopen_orDie(fname, "rb");

    ZSTD_seekable* const seekable = ZSTD_seekable_create();
    if (seekable==NULL) { fprintf(stderr, "ZSTD_seekable_create() error \n"); exit(10); }

    size_t const initResult = ZSTD_seekable_initFile(seekable, fin);
    if (ZSTD_isError(initResult)) { fprintf(stderr, "ZSTD_seekable_init() error : %s \n", ZSTD_getErrorName(initResult)); exit(11); }

    unsigned const numFrames = ZSTD_seekable_getNumFrames(seekable);
    struct sum_job* jobs = (struct sum_job*)malloc(numFrames * sizeof(struct sum_job));

    unsigned fnb;
    for (fnb = 0; fnb < numFrames; fnb++) {
        jobs[fnb] = (struct sum_job){ fname, 0, fnb, 0 };
        POOL_add(pool, sumFrame, &jobs[fnb]);
    }

    unsigned long long total = 0;

    for (fnb = 0; fnb < numFrames; fnb++) {
        while (!jobs[fnb].done) SLEEP(5); /* wake up every 5 milliseconds to check */
        total += jobs[fnb].sum;
    }

    printf("Sum: %llu\n", total);

    POOL_free(pool);
    ZSTD_seekable_free(seekable);
    fclose(fin);
    free(jobs);
}


int main(int argc, const char** argv)
{
    const char* const exeName = argv[0];

    if (argc!=3) {
        fprintf(stderr, "wrong arguments\n");
        fprintf(stderr, "usage:\n");
        fprintf(stderr, "%s FILE NB_THREADS\n", exeName);
        return 1;
    }

    {
        const char* const inFilename = argv[1];
        int const nbThreads = atoi(argv[2]);
        sumFile_orDie(inFilename, nbThreads);
    }

    return 0;
}
