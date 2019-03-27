/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

/* This library is designed for a single-threaded console application.
*  It exit() and printf() into stderr when it encounters an error condition. */

#ifndef DIBIO_H_003
#define DIBIO_H_003


/*-*************************************
*  Dependencies
***************************************/
#define ZDICT_STATIC_LINKING_ONLY
#include "zdict.h"     /* ZDICT_params_t */


/*-*************************************
*  Public functions
***************************************/
/*! DiB_trainFromFiles() :
    Train a dictionary from a set of files provided by `fileNamesTable`.
    Resulting dictionary is written into file `dictFileName`.
    `parameters` is optional and can be provided with values set to 0, meaning "default".
    @return : 0 == ok. Any other : error.
*/
int DiB_trainFromFiles(const char* dictFileName, unsigned maxDictSize,
                       const char** fileNamesTable, unsigned nbFiles, size_t chunkSize,
                       ZDICT_legacy_params_t* params, ZDICT_cover_params_t* coverParams,
                       ZDICT_fastCover_params_t* fastCoverParams, int optimize);

#endif
