/******************************************************************************
 * gcov.h
 *
 * Coverage structures exported by Xen.
 * Structure is different from Gcc one.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Copyright (c) 2013, Citrix Systems R&D Ltd.
 */

#ifndef __XEN_PUBLIC_GCOV_H__
#define __XEN_PUBLIC_GCOV_H__ __XEN_PUBLIC_GCOV_H__

#define XENCOV_COUNTERS         5
#define XENCOV_TAG_BASE         0x58544300u
#define XENCOV_TAG_FILE         (XENCOV_TAG_BASE+0x46u)
#define XENCOV_TAG_FUNC         (XENCOV_TAG_BASE+0x66u)
#define XENCOV_TAG_COUNTER(n)   (XENCOV_TAG_BASE+0x30u+((n)&0xfu))
#define XENCOV_TAG_END          (XENCOV_TAG_BASE+0x2eu)
#define XENCOV_IS_TAG_COUNTER(n) \
    ((n) >= XENCOV_TAG_COUNTER(0) && (n) < XENCOV_TAG_COUNTER(XENCOV_COUNTERS))
#define XENCOV_COUNTER_NUM(n) ((n)-XENCOV_TAG_COUNTER(0))

/*
 * The main structure for the blob is
 * BLOB := FILE.. END
 * FILE := TAG_FILE VERSION STAMP FILENAME COUNTERS FUNCTIONS
 * FILENAME := LEN characters
 *   characters are padded to 32 bit
 * LEN := 32 bit value
 * COUNTERS := TAG_COUNTER(n) NUM COUNTER..
 * NUM := 32 bit valie
 * COUNTER := 64 bit value
 * FUNCTIONS := TAG_FUNC NUM FUNCTION..
 * FUNCTION := IDENT CHECKSUM NUM_COUNTERS
 *
 * All tagged structures are aligned to 8 bytes
 */

/**
 * File information
 * Prefixed with XENCOV_TAG_FILE and a string with filename
 * Aligned to 8 bytes
 */
struct xencov_file
{
    uint32_t tag; /* XENCOV_TAG_FILE */
    uint32_t version;
    uint32_t stamp;
    uint32_t fn_len;
    char filename[1];
};


/**
 * Counters information
 * Prefixed with XENCOV_TAG_COUNTER(n) where n is 0..(XENCOV_COUNTERS-1)
 * Aligned to 8 bytes
 */
struct xencov_counter
{
    uint32_t tag; /* XENCOV_TAG_COUNTER(n) */
    uint32_t num;
    uint64_t values[1];
};

/**
 * Information for each function
 * Number of counter is equal to the number of counter structures got before
 */
struct xencov_function
{
    uint32_t ident;
    uint32_t checksum;
    uint32_t num_counters[1];
};

/**
 * Information for all functions
 * Aligned to 8 bytes
 */
struct xencov_functions
{
    uint32_t tag; /* XENCOV_TAG_FUNC */
    uint32_t num;
    struct xencov_function xencov_function[1];
};

/**
 * Terminator
 */
struct xencov_end
{
    uint32_t tag; /* XENCOV_TAG_END */
};

#endif /* __XEN_PUBLIC_GCOV_H__ */

