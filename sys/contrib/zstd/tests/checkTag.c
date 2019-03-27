/*
 * Copyright (c) 2018-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

/* checkTag : validation tool for libzstd
 * command :
 * $ ./checkTag tag
 * checkTag validates tags of following format : v[0-9].[0-9].[0-9]{any}
 * The tag is then compared to zstd version number.
 * They are compatible if first 3 digits are identical.
 * Anything beyond that is free, and doesn't impact validation.
 * Example : tag v1.8.1.2 is compatible with version 1.8.1
 * When tag and version are not compatible, program exits with error code 1.
 * When they are compatible, it exists with a code 0.
 * checkTag is intended to be used in automated testing environment.
 */

#include <stdio.h>   /* printf */
#include <string.h>  /* strlen, strncmp */
#include "zstd.h"    /* ZSTD_VERSION_STRING */


/*  validate() :
 * @return 1 if tag is compatible, 0 if not.
 */
static int validate(const char* const tag)
{
    size_t const tagLength = strlen(tag);
    size_t const verLength = strlen(ZSTD_VERSION_STRING);

    if (tagLength < 2) return 0;
    if (tag[0] != 'v') return 0;
    if (tagLength <= verLength) return 0;

    if (strncmp(ZSTD_VERSION_STRING, tag+1, verLength)) return 0;

    return 1;
}

int main(int argc, const char** argv)
{
    const char* const exeName = argv[0];
    const char* const tag = argv[1];
    if (argc!=2) {
        printf("incorrect usage : %s tag \n", exeName);
        return 2;
    }

    printf("Version : %s \n", ZSTD_VERSION_STRING);
    printf("Tag     : %s \n", tag);

    if (validate(tag)) {
        printf("OK : tag is compatible with zstd version \n");
        return 0;
    }

    printf("!! error : tag and versions are not compatible !! \n");
    return 1;
}
