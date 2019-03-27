/*
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 */

#include <stdlib.h>    // malloc, free, exit, atoi
#include <stdio.h>     // fprintf, perror, feof, fopen, etc.
#include <string.h>    // strlen, memset, strcat
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

#define XXH_NAMESPACE ZSTD_
#include "xxhash.h"

#include "pool.h"      // use zstd thread pool for demo

#include "zstd_seekable.h"

static void* malloc_orDie(size_t size)
{
    void* const buff = malloc(size);
    if (buff) return buff;
    /* error */
    perror("malloc:");
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

static void fseek_orDie(FILE* file, long int offset, int origin)
{
    if (!fseek(file, offset, origin)) {
        if (!fflush(file)) return;
    }
    /* error */
    perror("fseek");
    exit(7);
}

static long int ftell_orDie(FILE* file)
{
    long int off = ftell(file);
    if (off != -1) return off;
    /* error */
    perror("ftell");
    exit(8);
}

struct job {
    const void* src;
    size_t srcSize;
    void* dst;
    size_t dstSize;

    unsigned checksum;

    int compressionLevel;
    int done;
};

static void compressFrame(void* opaque)
{
    struct job* job = opaque;

    job->checksum = XXH64(job->src, job->srcSize, 0);

    size_t ret = ZSTD_compress(job->dst, job->dstSize, job->src, job->srcSize, job->compressionLevel);
    if (ZSTD_isError(ret)) {
        fprintf(stderr, "ZSTD_compress() error : %s \n", ZSTD_getErrorName(ret));
        exit(20);
    }

    job->dstSize = ret;
    job->done = 1;
}

static void compressFile_orDie(const char* fname, const char* outName, int cLevel, unsigned frameSize, int nbThreads)
{
    POOL_ctx* pool = POOL_create(nbThreads, nbThreads);
    if (pool == NULL) { fprintf(stderr, "POOL_create() error \n"); exit(9); }

    FILE* const fin  = fopen_orDie(fname, "rb");
    FILE* const fout = fopen_orDie(outName, "wb");

    if (ZSTD_compressBound(frameSize) > 0xFFFFFFFFU) { fprintf(stderr, "Frame size too large \n"); exit(10); }
    unsigned dstSize = ZSTD_compressBound(frameSize);


    fseek_orDie(fin, 0, SEEK_END);
    long int length = ftell_orDie(fin);
    fseek_orDie(fin, 0, SEEK_SET);

    size_t numFrames = (length + frameSize - 1) / frameSize;

    struct job* jobs = malloc_orDie(sizeof(struct job) * numFrames);

    size_t i;
    for(i = 0; i < numFrames; i++) {
        void* in = malloc_orDie(frameSize);
        void* out = malloc_orDie(dstSize);

        size_t inSize = fread_orDie(in, frameSize, fin);

        jobs[i].src = in;
        jobs[i].srcSize = inSize;
        jobs[i].dst = out;
        jobs[i].dstSize = dstSize;
        jobs[i].compressionLevel = cLevel;
        jobs[i].done = 0;
        POOL_add(pool, compressFrame, &jobs[i]);
    }

    ZSTD_frameLog* fl = ZSTD_seekable_createFrameLog(1);
    if (fl == NULL) { fprintf(stderr, "ZSTD_seekable_createFrameLog() failed \n"); exit(11); }
    for (i = 0; i < numFrames; i++) {
        while (!jobs[i].done) SLEEP(5); /* wake up every 5 milliseconds to check */
        fwrite_orDie(jobs[i].dst, jobs[i].dstSize, fout);
        free((void*)jobs[i].src);
        free(jobs[i].dst);

        size_t ret = ZSTD_seekable_logFrame(fl, jobs[i].dstSize, jobs[i].srcSize, jobs[i].checksum);
        if (ZSTD_isError(ret)) { fprintf(stderr, "ZSTD_seekable_logFrame() error : %s \n", ZSTD_getErrorName(ret)); }
    }

    {   unsigned char seekTableBuff[1024];
        ZSTD_outBuffer out = {seekTableBuff, 1024, 0};
        while (ZSTD_seekable_writeSeekTable(fl, &out) != 0) {
            fwrite_orDie(seekTableBuff, out.pos, fout);
            out.pos = 0;
        }
        fwrite_orDie(seekTableBuff, out.pos, fout);
    }

    ZSTD_seekable_freeFrameLog(fl);
    free(jobs);
    fclose_orDie(fout);
    fclose_orDie(fin);
}

static const char* createOutFilename_orDie(const char* filename)
{
    size_t const inL = strlen(filename);
    size_t const outL = inL + 5;
    void* outSpace = malloc_orDie(outL);
    memset(outSpace, 0, outL);
    strcat(outSpace, filename);
    strcat(outSpace, ".zst");
    return (const char*)outSpace;
}

int main(int argc, const char** argv) {
    const char* const exeName = argv[0];
    if (argc!=4) {
        printf("wrong arguments\n");
        printf("usage:\n");
        printf("%s FILE FRAME_SIZE NB_THREADS\n", exeName);
        return 1;
    }

    {   const char* const inFileName = argv[1];
        unsigned const frameSize = (unsigned)atoi(argv[2]);
        int const nbThreads = atoi(argv[3]);

        const char* const outFileName = createOutFilename_orDie(inFileName);
        compressFile_orDie(inFileName, outFileName, 5, frameSize, nbThreads);
    }

    return 0;
}
