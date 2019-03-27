/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

/*
    This program uses hard-coded data compressed with Zstd legacy versions
    and tests that the API decompresses them correctly
*/

/*===========================================
*   Dependencies
*==========================================*/
#include <stddef.h>     /* size_t */
#include <stdlib.h>     /* malloc, free */
#include <stdio.h>      /* fprintf */
#include <string.h>     /* strlen */
#include "zstd.h"
#include "zstd_errors.h"

/*===========================================
*   Macros
*==========================================*/
#define DISPLAY(...)          fprintf(stderr, __VA_ARGS__)

/*===========================================
*   Precompressed frames
*==========================================*/
const char* const COMPRESSED; /* content is at end of file */
size_t const COMPRESSED_SIZE = 917;
const char* const EXPECTED; /* content is at end of file */


static int testSimpleAPI(void)
{
    size_t const size = strlen(EXPECTED);
    char* const output = malloc(size);

    if (!output) {
        DISPLAY("ERROR: Not enough memory!\n");
        return 1;
    }

    {
        size_t const ret = ZSTD_decompress(output, size, COMPRESSED, COMPRESSED_SIZE);
        if (ZSTD_isError(ret)) {
            if (ret == ZSTD_error_prefix_unknown) {
                DISPLAY("ERROR: Invalid frame magic number, was this compiled "
                        "without legacy support?\n");
            } else {
                DISPLAY("ERROR: %s\n", ZSTD_getErrorName(ret));
            }
            return 1;
        }
        if (ret != size) {
            DISPLAY("ERROR: Wrong decoded size\n");
        }
    }
    if (memcmp(EXPECTED, output, size) != 0) {
        DISPLAY("ERROR: Wrong decoded output produced\n");
        return 1;
    }

    free(output);
    DISPLAY("Simple API OK\n");
    return 0;
}


static int testStreamingAPI(void)
{
    size_t const outBuffSize = ZSTD_DStreamOutSize();
    char* const outBuff = malloc(outBuffSize);
    ZSTD_DStream* const stream = ZSTD_createDStream();
    ZSTD_inBuffer input = { COMPRESSED, COMPRESSED_SIZE, 0 };
    size_t outputPos = 0;
    int needsInit = 1;

    if (outBuff == NULL) {
        DISPLAY("ERROR: Could not allocate memory\n");
        return 1;
    }
    if (stream == NULL) {
        DISPLAY("ERROR: Could not create dstream\n");
        return 1;
    }

    while (1) {
        ZSTD_outBuffer output = {outBuff, outBuffSize, 0};
        if (needsInit) {
            size_t const ret = ZSTD_initDStream(stream);
            if (ZSTD_isError(ret)) {
                DISPLAY("ERROR: ZSTD_initDStream: %s\n", ZSTD_getErrorName(ret));
                return 1;
        }   }

        {   size_t const ret = ZSTD_decompressStream(stream, &output, &input);
            if (ZSTD_isError(ret)) {
                DISPLAY("ERROR: ZSTD_decompressStream: %s\n", ZSTD_getErrorName(ret));
                return 1;
            }

            if (ret == 0) {
                needsInit = 1;
        }   }

        if (memcmp(outBuff, EXPECTED + outputPos, output.pos) != 0) {
            DISPLAY("ERROR: Wrong decoded output produced\n");
            return 1;
        }
        outputPos += output.pos;
        if (input.pos == input.size && output.pos < output.size) {
            break;
        }
    }

    free(outBuff);
    ZSTD_freeDStream(stream);
    DISPLAY("Streaming API OK\n");
    return 0;
}

int main(void)
{
    {   int const ret = testSimpleAPI();
        if (ret) return ret; }
    {   int const ret = testStreamingAPI();
        if (ret) return ret; }

    DISPLAY("OK\n");
    return 0;
}

/* Consists of the "EXPECTED" string compressed with default settings on
    - v0.4.3
    - v0.5.0
    - v0.6.0
    - v0.7.0
    - v0.8.0
*/
const char* const COMPRESSED =
    "\x24\xB5\x2F\xFD\x00\x00\x00\xBB\xB0\x02\xC0\x10\x00\x1E\xB0\x01"
    "\x02\x00\x00\x80\x00\xE8\x92\x34\x12\x97\xC8\xDF\xE9\xF3\xEF\x53"
    "\xEA\x1D\x27\x4F\x0C\x44\x90\x0C\x8D\xF1\xB4\x89\x17\x00\x18\x00"
    "\x18\x00\x3F\xE6\xE2\xE3\x74\xD6\xEC\xC9\x4A\xE0\x71\x71\x42\x3E"
    "\x64\x4F\x6A\x45\x4E\x78\xEC\x49\x03\x3F\xC6\x80\xAB\x8F\x75\x5E"
    "\x6F\x2E\x3E\x7E\xC6\xDC\x45\x69\x6C\xC5\xFD\xC7\x40\xB8\x84\x8A"
    "\x01\xEB\xA8\xD1\x40\x39\x90\x4C\x64\xF8\xEB\x53\xE6\x18\x0B\x67"
    "\x12\xAD\xB8\x99\xB3\x5A\x6F\x8A\x19\x03\x01\x50\x67\x56\xF5\x9F"
    "\x35\x84\x60\xA0\x60\x91\xC9\x0A\xDC\xAB\xAB\xE0\xE2\x81\xFA\xCF"
    "\xC6\xBA\x01\x0E\x00\x54\x00\x00\x19\x00\x00\x54\x14\x00\x24\x24"
    "\x04\xFE\x04\x84\x4E\x41\x00\x27\xE2\x02\xC4\xB1\x00\xD2\x51\x00"
    "\x79\x58\x41\x28\x00\xE0\x0C\x01\x68\x65\x00\x04\x13\x0C\xDA\x0C"
    "\x80\x22\x06\xC0\x00\x00\x25\xB5\x2F\xFD\x00\x00\x00\xAD\x12\xB0"
    "\x7D\x1E\xB0\x01\x02\x00\x00\x80\x00\xE8\x92\x34\x12\x97\xC8\xDF"
    "\xE9\xF3\xEF\x53\xEA\x1D\x27\x4F\x0C\x44\x90\x0C\x8D\xF1\xB4\x89"
    "\x03\x01\x50\x67\x56\xF5\x9F\x35\x84\x60\xA0\x60\x91\xC9\x0A\xDC"
    "\xAB\xAB\xE0\xE2\x81\xFA\xCF\xC6\xBA\xEB\xA8\xD1\x40\x39\x90\x4C"
    "\x64\xF8\xEB\x53\xE6\x18\x0B\x67\x12\xAD\xB8\x99\xB3\x5A\x6F\x8A"
    "\xF9\x63\x0C\xB8\xFA\x58\xE7\xF5\xE6\xE2\xE3\x67\xCC\x5D\x94\xC6"
    "\x56\xDC\x7F\x0C\x84\x4B\xA8\xF8\x63\x2E\x3E\x4E\x67\xCD\x9E\xAC"
    "\x04\x1E\x17\x27\xE4\x43\xF6\xA4\x56\xE4\x84\xC7\x9E\x34\x0E\x00"
    "\x00\x32\x40\x80\xA8\x00\x01\x49\x81\xE0\x3C\x01\x29\x1D\x00\x87"
    "\xCE\x80\x75\x08\x80\x72\x24\x00\x7B\x52\x00\x94\x00\x20\xCC\x01"
    "\x86\xD2\x00\x81\x09\x83\xC1\x34\xA0\x88\x01\xC0\x00\x00\x26\xB5"
    "\x2F\xFD\x42\xEF\x00\x00\xA6\x12\xB0\x7D\x1E\xB0\x01\x02\x00\x00"
    "\x54\xA0\xBA\x24\x8D\xC4\x25\xF2\x77\xFA\xFC\xFB\x94\x7A\xC7\xC9"
    "\x13\x03\x11\x24\x43\x63\x3C\x6D\x22\x03\x01\x50\x67\x56\xF5\x9F"
    "\x35\x84\x60\xA0\x60\x91\xC9\x0A\xDC\xAB\xAB\xE0\xE2\x81\xFA\xCF"
    "\xC6\xBA\xEB\xA8\xD1\x40\x39\x90\x4C\x64\xF8\xEB\x53\xE6\x18\x0B"
    "\x67\x12\xAD\xB8\x99\xB3\x5A\x6F\x8A\xF9\x63\x0C\xB8\xFA\x58\xE7"
    "\xF5\xE6\xE2\xE3\x67\xCC\x5D\x94\xC6\x56\xDC\x7F\x0C\x84\x4B\xA8"
    "\xF8\x63\x2E\x3E\x4E\x67\xCD\x9E\xAC\x04\x1E\x17\x27\xE4\x43\xF6"
    "\xA4\x56\xE4\x84\xC7\x9E\x34\x0E\x00\x35\x0B\x71\xB5\xC0\x2A\x5C"
    "\x26\x94\x22\x20\x8B\x4C\x8D\x13\x47\x58\x67\x15\x6C\xF1\x1C\x4B"
    "\x54\x10\x9D\x31\x50\x85\x4B\x54\x0E\x01\x4B\x3D\x01\xC0\x00\x00"
    "\x27\xB5\x2F\xFD\x20\xEF\x00\x00\xA6\x12\xE4\x84\x1F\xB0\x01\x10"
    "\x00\x00\x00\x35\x59\xA6\xE7\xA1\xEF\x7C\xFC\xBD\x3F\xFF\x9F\xEF"
    "\xEE\xEF\x61\xC3\xAA\x31\x1D\x34\x38\x22\x22\x04\x44\x21\x80\x32"
    "\xAD\x28\xF3\xD6\x28\x0C\x0A\x0E\xD6\x5C\xAC\x19\x8D\x20\x5F\x45"
    "\x02\x2E\x17\x50\x66\x6D\xAC\x8B\x9C\x6E\x07\x73\x46\xBB\x44\x14"
    "\xE7\x98\xC3\xB9\x17\x32\x6E\x33\x7C\x0E\x21\xB1\xDB\xCB\x89\x51"
    "\x23\x34\xAB\x9D\xBC\x6D\x20\xF5\x03\xA9\x91\x4C\x2E\x1F\x59\xDB"
    "\xD9\x35\x67\x4B\x0C\x95\x79\x10\x00\x85\xA6\x96\x95\x2E\xDF\x78"
    "\x7B\x4A\x5C\x09\x76\x97\xD1\x5C\x96\x12\x75\x35\xA3\x55\x4A\xD4"
    "\x0B\x00\x35\x0B\x71\xB5\xC0\x2A\x5C\xE6\x08\x45\xF1\x39\x43\xF1"
    "\x1C\x4B\x54\x10\x9D\x31\x50\x85\x4B\x54\x0E\x01\x4B\x3D\x01\xC0"
    "\x00\x00\x28\xB5\x2F\xFD\x24\xEF\x35\x05\x00\x92\x0B\x21\x1F\xB0"
    "\x01\x10\x00\x00\x00\x35\x59\xA6\xE7\xA1\xEF\x7C\xFC\xBD\x3F\xFF"
    "\x9F\xEF\xEE\xEF\x61\xC3\xAA\x31\x1D\x34\x38\x22\x22\x04\x44\x21"
    "\x80\x32\xAD\x28\xF3\xD6\x28\x0C\x0A\x0E\xD6\x5C\xAC\x19\x8D\x20"
    "\x5F\x45\x02\x2E\x17\x50\x66\x6D\xAC\x8B\x9C\x6E\x07\x73\x46\xBB"
    "\x44\x14\xE7\x98\xC3\xB9\x17\x32\x6E\x33\x7C\x0E\x21\xB1\xDB\xCB"
    "\x89\x51\x23\x34\xAB\x9D\xBC\x6D\x20\xF5\x03\xA9\x91\x4C\x2E\x1F"
    "\x59\xDB\xD9\x35\x67\x4B\x0C\x95\x79\x10\x00\x85\xA6\x96\x95\x2E"
    "\xDF\x78\x7B\x4A\x5C\x09\x76\x97\xD1\x5C\x96\x12\x75\x35\xA3\x55"
    "\x4A\xD4\x0B\x00\x35\x0B\x71\xB5\xC0\x2A\x5C\xE6\x08\x45\xF1\x39"
    "\x43\xF1\x1C\x4B\x54\x10\x9D\x31\x50\x85\x4B\x54\x0E\x01\x4B\x3D"
    "\x01\xD2\x2F\x21\x80";

const char* const EXPECTED =
    "snowden is snowed in / he's now then in his snow den / when does the snow end?\n"
    "goodbye little dog / you dug some holes in your day / they'll be hard to fill.\n"
    "when life shuts a door, / just open it. it’s a door. / that is how doors work.\n"

    "snowden is snowed in / he's now then in his snow den / when does the snow end?\n"
    "goodbye little dog / you dug some holes in your day / they'll be hard to fill.\n"
    "when life shuts a door, / just open it. it’s a door. / that is how doors work.\n"

    "snowden is snowed in / he's now then in his snow den / when does the snow end?\n"
    "goodbye little dog / you dug some holes in your day / they'll be hard to fill.\n"
    "when life shuts a door, / just open it. it’s a door. / that is how doors work.\n"

    "snowden is snowed in / he's now then in his snow den / when does the snow end?\n"
    "goodbye little dog / you dug some holes in your day / they'll be hard to fill.\n"
    "when life shuts a door, / just open it. it’s a door. / that is how doors work.\n"

    "snowden is snowed in / he's now then in his snow den / when does the snow end?\n"
    "goodbye little dog / you dug some holes in your day / they'll be hard to fill.\n"
    "when life shuts a door, / just open it. it’s a door. / that is how doors work.\n";
