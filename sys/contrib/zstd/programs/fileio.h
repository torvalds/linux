/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */


#ifndef FILEIO_H_23981798732
#define FILEIO_H_23981798732

#define ZSTD_STATIC_LINKING_ONLY   /* ZSTD_compressionParameters */
#include "zstd.h"                  /* ZSTD_* */

#if defined (__cplusplus)
extern "C" {
#endif


/* *************************************
*  Special i/o constants
**************************************/
#define stdinmark  "/*stdin*\\"
#define stdoutmark "/*stdout*\\"
#ifdef _WIN32
#  define nulmark "nul"
#else
#  define nulmark "/dev/null"
#endif
#define LZMA_EXTENSION  ".lzma"
#define XZ_EXTENSION    ".xz"
#define GZ_EXTENSION    ".gz"
#define ZSTD_EXTENSION  ".zst"
#define LZ4_EXTENSION   ".lz4"


/*-*************************************
*  Types
***************************************/
typedef enum { FIO_zstdCompression, FIO_gzipCompression, FIO_xzCompression, FIO_lzmaCompression, FIO_lz4Compression } FIO_compressionType_t;


/*-*************************************
*  Parameters
***************************************/
void FIO_setCompressionType(FIO_compressionType_t compressionType);
void FIO_overwriteMode(void);
void FIO_setAdaptiveMode(unsigned adapt);
void FIO_setAdaptMin(int minCLevel);
void FIO_setAdaptMax(int maxCLevel);
void FIO_setBlockSize(unsigned blockSize);
void FIO_setChecksumFlag(unsigned checksumFlag);
void FIO_setDictIDFlag(unsigned dictIDFlag);
void FIO_setLdmBucketSizeLog(unsigned ldmBucketSizeLog);
void FIO_setLdmFlag(unsigned ldmFlag);
void FIO_setLdmHashRateLog(unsigned ldmHashRateLog);
void FIO_setLdmHashLog(unsigned ldmHashLog);
void FIO_setLdmMinMatch(unsigned ldmMinMatch);
void FIO_setMemLimit(unsigned memLimit);
void FIO_setNbWorkers(unsigned nbWorkers);
void FIO_setNotificationLevel(unsigned level);
void FIO_setOverlapLog(unsigned overlapLog);
void FIO_setRemoveSrcFile(unsigned flag);
void FIO_setSparseWrite(unsigned sparse);  /**< 0: no sparse; 1: disable on stdout; 2: always enabled */
void FIO_setRsyncable(unsigned rsyncable);
void FIO_setNoProgress(unsigned noProgress);


/*-*************************************
*  Single File functions
***************************************/
/** FIO_compressFilename() :
    @return : 0 == ok;  1 == pb with src file. */
int FIO_compressFilename (const char* outfilename, const char* infilename, const char* dictFileName,
                          int compressionLevel, ZSTD_compressionParameters comprParams);

/** FIO_decompressFilename() :
    @return : 0 == ok;  1 == pb with src file. */
int FIO_decompressFilename (const char* outfilename, const char* infilename, const char* dictFileName);

int FIO_listMultipleFiles(unsigned numFiles, const char** filenameTable, int displayLevel);


/*-*************************************
*  Multiple File functions
***************************************/
/** FIO_compressMultipleFilenames() :
    @return : nb of missing files */
int FIO_compressMultipleFilenames(const char** srcNamesTable, unsigned nbFiles,
                                  const char* outFileName, const char* suffix,
                                  const char* dictFileName, int compressionLevel,
                                  ZSTD_compressionParameters comprParams);

/** FIO_decompressMultipleFilenames() :
    @return : nb of missing or skipped files */
int FIO_decompressMultipleFilenames(const char** srcNamesTable, unsigned nbFiles,
                                    const char* outFileName,
                                    const char* dictFileName);


/*-*************************************
*  Advanced stuff (should actually be hosted elsewhere)
***************************************/

/* custom crash signal handler */
void FIO_addAbortHandler(void);



#if defined (__cplusplus)
}
#endif

#endif  /* FILEIO_H_23981798732 */
