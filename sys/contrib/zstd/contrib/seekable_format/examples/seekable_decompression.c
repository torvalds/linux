/*
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 */


#include <stdlib.h>    // malloc, exit
#include <stdio.h>     // fprintf, perror, feof
#include <string.h>    // strerror
#include <errno.h>     // errno
#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>      // presumes zstd library is installed
#include <zstd_errors.h>

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


static void decompressFile_orDie(const char* fname, off_t startOffset, off_t endOffset)
{
    FILE* const fin  = fopen_orDie(fname, "rb");
    FILE* const fout = stdout;
    size_t const buffOutSize = ZSTD_DStreamOutSize();  /* Guarantee to successfully flush at least one complete compressed block in all circumstances. */
    void*  const buffOut = malloc_orDie(buffOutSize);

    ZSTD_seekable* const seekable = ZSTD_seekable_create();
    if (seekable==NULL) { fprintf(stderr, "ZSTD_seekable_create() error \n"); exit(10); }

    size_t const initResult = ZSTD_seekable_initFile(seekable, fin);
    if (ZSTD_isError(initResult)) { fprintf(stderr, "ZSTD_seekable_init() error : %s \n", ZSTD_getErrorName(initResult)); exit(11); }

    while (startOffset < endOffset) {
        size_t const result = ZSTD_seekable_decompress(seekable, buffOut, MIN(endOffset - startOffset, buffOutSize), startOffset);

        if (ZSTD_isError(result)) {
            fprintf(stderr, "ZSTD_seekable_decompress() error : %s \n",
                    ZSTD_getErrorName(result));
            exit(12);
        }
        fwrite_orDie(buffOut, result, fout);
        startOffset += result;
    }

    ZSTD_seekable_free(seekable);
    fclose_orDie(fin);
    fclose_orDie(fout);
    free(buffOut);
}


int main(int argc, const char** argv)
{
    const char* const exeName = argv[0];

    if (argc!=4) {
        fprintf(stderr, "wrong arguments\n");
        fprintf(stderr, "usage:\n");
        fprintf(stderr, "%s FILE START END\n", exeName);
        return 1;
    }

    {
        const char* const inFilename = argv[1];
        off_t const startOffset = atoll(argv[2]);
        off_t const endOffset = atoll(argv[3]);
        decompressFile_orDie(inFilename, startOffset, endOffset);
    }

    return 0;
}
