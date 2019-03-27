/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#ifndef DICTBUILDER_H_001
#define DICTBUILDER_H_001

#if defined (__cplusplus)
extern "C" {
#endif


/*======  Dependencies  ======*/
#include <stddef.h>  /* size_t */


/* =====   ZDICTLIB_API : control library symbols visibility   ===== */
#ifndef ZDICTLIB_VISIBILITY
#  if defined(__GNUC__) && (__GNUC__ >= 4)
#    define ZDICTLIB_VISIBILITY __attribute__ ((visibility ("default")))
#  else
#    define ZDICTLIB_VISIBILITY
#  endif
#endif
#if defined(ZSTD_DLL_EXPORT) && (ZSTD_DLL_EXPORT==1)
#  define ZDICTLIB_API __declspec(dllexport) ZDICTLIB_VISIBILITY
#elif defined(ZSTD_DLL_IMPORT) && (ZSTD_DLL_IMPORT==1)
#  define ZDICTLIB_API __declspec(dllimport) ZDICTLIB_VISIBILITY /* It isn't required but allows to generate better code, saving a function pointer load from the IAT and an indirect jump.*/
#else
#  define ZDICTLIB_API ZDICTLIB_VISIBILITY
#endif


/*! ZDICT_trainFromBuffer():
 *  Train a dictionary from an array of samples.
 *  Redirect towards ZDICT_optimizeTrainFromBuffer_fastCover() single-threaded, with d=8, steps=4,
 *  f=20, and accel=1.
 *  Samples must be stored concatenated in a single flat buffer `samplesBuffer`,
 *  supplied with an array of sizes `samplesSizes`, providing the size of each sample, in order.
 *  The resulting dictionary will be saved into `dictBuffer`.
 * @return: size of dictionary stored into `dictBuffer` (<= `dictBufferCapacity`)
 *          or an error code, which can be tested with ZDICT_isError().
 *  Note: ZDICT_trainFromBuffer() requires about 9 bytes of memory for each input byte.
 *  Tips: In general, a reasonable dictionary has a size of ~ 100 KB.
 *        It's possible to select smaller or larger size, just by specifying `dictBufferCapacity`.
 *        In general, it's recommended to provide a few thousands samples, though this can vary a lot.
 *        It's recommended that total size of all samples be about ~x100 times the target size of dictionary.
 */
ZDICTLIB_API size_t ZDICT_trainFromBuffer(void* dictBuffer, size_t dictBufferCapacity,
                                    const void* samplesBuffer,
                                    const size_t* samplesSizes, unsigned nbSamples);


/*======   Helper functions   ======*/
ZDICTLIB_API unsigned ZDICT_getDictID(const void* dictBuffer, size_t dictSize);  /**< extracts dictID; @return zero if error (not a valid dictionary) */
ZDICTLIB_API unsigned ZDICT_isError(size_t errorCode);
ZDICTLIB_API const char* ZDICT_getErrorName(size_t errorCode);



#ifdef ZDICT_STATIC_LINKING_ONLY

/* ====================================================================================
 * The definitions in this section are considered experimental.
 * They should never be used with a dynamic library, as they may change in the future.
 * They are provided for advanced usages.
 * Use them only in association with static linking.
 * ==================================================================================== */

typedef struct {
    int      compressionLevel;   /* optimize for a specific zstd compression level; 0 means default */
    unsigned notificationLevel;  /* Write log to stderr; 0 = none (default); 1 = errors; 2 = progression; 3 = details; 4 = debug; */
    unsigned dictID;             /* force dictID value; 0 means auto mode (32-bits random value) */
} ZDICT_params_t;

/*! ZDICT_cover_params_t:
 *  k and d are the only required parameters.
 *  For others, value 0 means default.
 */
typedef struct {
    unsigned k;                  /* Segment size : constraint: 0 < k : Reasonable range [16, 2048+] */
    unsigned d;                  /* dmer size : constraint: 0 < d <= k : Reasonable range [6, 16] */
    unsigned steps;              /* Number of steps : Only used for optimization : 0 means default (40) : Higher means more parameters checked */
    unsigned nbThreads;          /* Number of threads : constraint: 0 < nbThreads : 1 means single-threaded : Only used for optimization : Ignored if ZSTD_MULTITHREAD is not defined */
    double splitPoint;           /* Percentage of samples used for training: Only used for optimization : the first nbSamples * splitPoint samples will be used to training, the last nbSamples * (1 - splitPoint) samples will be used for testing, 0 means default (1.0), 1.0 when all samples are used for both training and testing */
    ZDICT_params_t zParams;
} ZDICT_cover_params_t;

typedef struct {
    unsigned k;                  /* Segment size : constraint: 0 < k : Reasonable range [16, 2048+] */
    unsigned d;                  /* dmer size : constraint: 0 < d <= k : Reasonable range [6, 16] */
    unsigned f;                  /* log of size of frequency array : constraint: 0 < f <= 31 : 1 means default(20)*/
    unsigned steps;              /* Number of steps : Only used for optimization : 0 means default (40) : Higher means more parameters checked */
    unsigned nbThreads;          /* Number of threads : constraint: 0 < nbThreads : 1 means single-threaded : Only used for optimization : Ignored if ZSTD_MULTITHREAD is not defined */
    double splitPoint;           /* Percentage of samples used for training: Only used for optimization : the first nbSamples * splitPoint samples will be used to training, the last nbSamples * (1 - splitPoint) samples will be used for testing, 0 means default (0.75), 1.0 when all samples are used for both training and testing */
    unsigned accel;              /* Acceleration level: constraint: 0 < accel <= 10, higher means faster and less accurate, 0 means default(1) */
    ZDICT_params_t zParams;
} ZDICT_fastCover_params_t;

/*! ZDICT_trainFromBuffer_cover():
 *  Train a dictionary from an array of samples using the COVER algorithm.
 *  Samples must be stored concatenated in a single flat buffer `samplesBuffer`,
 *  supplied with an array of sizes `samplesSizes`, providing the size of each sample, in order.
 *  The resulting dictionary will be saved into `dictBuffer`.
 * @return: size of dictionary stored into `dictBuffer` (<= `dictBufferCapacity`)
 *          or an error code, which can be tested with ZDICT_isError().
 *  Note: ZDICT_trainFromBuffer_cover() requires about 9 bytes of memory for each input byte.
 *  Tips: In general, a reasonable dictionary has a size of ~ 100 KB.
 *        It's possible to select smaller or larger size, just by specifying `dictBufferCapacity`.
 *        In general, it's recommended to provide a few thousands samples, though this can vary a lot.
 *        It's recommended that total size of all samples be about ~x100 times the target size of dictionary.
 */
ZDICTLIB_API size_t ZDICT_trainFromBuffer_cover(
          void *dictBuffer, size_t dictBufferCapacity,
    const void *samplesBuffer, const size_t *samplesSizes, unsigned nbSamples,
          ZDICT_cover_params_t parameters);

/*! ZDICT_optimizeTrainFromBuffer_cover():
 * The same requirements as above hold for all the parameters except `parameters`.
 * This function tries many parameter combinations and picks the best parameters.
 * `*parameters` is filled with the best parameters found,
 * dictionary constructed with those parameters is stored in `dictBuffer`.
 *
 * All of the parameters d, k, steps are optional.
 * If d is non-zero then we don't check multiple values of d, otherwise we check d = {6, 8}.
 * if steps is zero it defaults to its default value.
 * If k is non-zero then we don't check multiple values of k, otherwise we check steps values in [50, 2000].
 *
 * @return: size of dictionary stored into `dictBuffer` (<= `dictBufferCapacity`)
 *           or an error code, which can be tested with ZDICT_isError().
 *           On success `*parameters` contains the parameters selected.
 * Note: ZDICT_optimizeTrainFromBuffer_cover() requires about 8 bytes of memory for each input byte and additionally another 5 bytes of memory for each byte of memory for each thread.
 */
ZDICTLIB_API size_t ZDICT_optimizeTrainFromBuffer_cover(
          void* dictBuffer, size_t dictBufferCapacity,
    const void* samplesBuffer, const size_t* samplesSizes, unsigned nbSamples,
          ZDICT_cover_params_t* parameters);

/*! ZDICT_trainFromBuffer_fastCover():
 *  Train a dictionary from an array of samples using a modified version of COVER algorithm.
 *  Samples must be stored concatenated in a single flat buffer `samplesBuffer`,
 *  supplied with an array of sizes `samplesSizes`, providing the size of each sample, in order.
 *  d and k are required.
 *  All other parameters are optional, will use default values if not provided
 *  The resulting dictionary will be saved into `dictBuffer`.
 * @return: size of dictionary stored into `dictBuffer` (<= `dictBufferCapacity`)
 *          or an error code, which can be tested with ZDICT_isError().
 *  Note: ZDICT_trainFromBuffer_fastCover() requires about 1 bytes of memory for each input byte and additionally another 6 * 2^f bytes of memory .
 *  Tips: In general, a reasonable dictionary has a size of ~ 100 KB.
 *        It's possible to select smaller or larger size, just by specifying `dictBufferCapacity`.
 *        In general, it's recommended to provide a few thousands samples, though this can vary a lot.
 *        It's recommended that total size of all samples be about ~x100 times the target size of dictionary.
 */
ZDICTLIB_API size_t ZDICT_trainFromBuffer_fastCover(void *dictBuffer,
                    size_t dictBufferCapacity, const void *samplesBuffer,
                    const size_t *samplesSizes, unsigned nbSamples,
                    ZDICT_fastCover_params_t parameters);

/*! ZDICT_optimizeTrainFromBuffer_fastCover():
 * The same requirements as above hold for all the parameters except `parameters`.
 * This function tries many parameter combinations (specifically, k and d combinations)
 * and picks the best parameters. `*parameters` is filled with the best parameters found,
 * dictionary constructed with those parameters is stored in `dictBuffer`.
 * All of the parameters d, k, steps, f, and accel are optional.
 * If d is non-zero then we don't check multiple values of d, otherwise we check d = {6, 8}.
 * if steps is zero it defaults to its default value.
 * If k is non-zero then we don't check multiple values of k, otherwise we check steps values in [50, 2000].
 * If f is zero, default value of 20 is used.
 * If accel is zero, default value of 1 is used.
 *
 * @return: size of dictionary stored into `dictBuffer` (<= `dictBufferCapacity`)
 *           or an error code, which can be tested with ZDICT_isError().
 *           On success `*parameters` contains the parameters selected.
 * Note: ZDICT_optimizeTrainFromBuffer_fastCover() requires about 1 byte of memory for each input byte and additionally another 6 * 2^f bytes of memory for each thread.
 */
ZDICTLIB_API size_t ZDICT_optimizeTrainFromBuffer_fastCover(void* dictBuffer,
                    size_t dictBufferCapacity, const void* samplesBuffer,
                    const size_t* samplesSizes, unsigned nbSamples,
                    ZDICT_fastCover_params_t* parameters);

/*! ZDICT_finalizeDictionary():
 * Given a custom content as a basis for dictionary, and a set of samples,
 * finalize dictionary by adding headers and statistics.
 *
 * Samples must be stored concatenated in a flat buffer `samplesBuffer`,
 * supplied with an array of sizes `samplesSizes`, providing the size of each sample in order.
 *
 * dictContentSize must be >= ZDICT_CONTENTSIZE_MIN bytes.
 * maxDictSize must be >= dictContentSize, and must be >= ZDICT_DICTSIZE_MIN bytes.
 *
 * @return: size of dictionary stored into `dictBuffer` (<= `dictBufferCapacity`),
 *           or an error code, which can be tested by ZDICT_isError().
 * Note: ZDICT_finalizeDictionary() will push notifications into stderr if instructed to, using notificationLevel>0.
 * Note 2: dictBuffer and dictContent can overlap
 */
#define ZDICT_CONTENTSIZE_MIN 128
#define ZDICT_DICTSIZE_MIN    256
ZDICTLIB_API size_t ZDICT_finalizeDictionary(void* dictBuffer, size_t dictBufferCapacity,
                                const void* dictContent, size_t dictContentSize,
                                const void* samplesBuffer, const size_t* samplesSizes, unsigned nbSamples,
                                ZDICT_params_t parameters);

typedef struct {
    unsigned selectivityLevel;   /* 0 means default; larger => select more => larger dictionary */
    ZDICT_params_t zParams;
} ZDICT_legacy_params_t;

/*! ZDICT_trainFromBuffer_legacy():
 *  Train a dictionary from an array of samples.
 *  Samples must be stored concatenated in a single flat buffer `samplesBuffer`,
 *  supplied with an array of sizes `samplesSizes`, providing the size of each sample, in order.
 *  The resulting dictionary will be saved into `dictBuffer`.
 * `parameters` is optional and can be provided with values set to 0 to mean "default".
 * @return: size of dictionary stored into `dictBuffer` (<= `dictBufferCapacity`)
 *          or an error code, which can be tested with ZDICT_isError().
 *  Tips: In general, a reasonable dictionary has a size of ~ 100 KB.
 *        It's possible to select smaller or larger size, just by specifying `dictBufferCapacity`.
 *        In general, it's recommended to provide a few thousands samples, though this can vary a lot.
 *        It's recommended that total size of all samples be about ~x100 times the target size of dictionary.
 *  Note: ZDICT_trainFromBuffer_legacy() will send notifications into stderr if instructed to, using notificationLevel>0.
 */
ZDICTLIB_API size_t ZDICT_trainFromBuffer_legacy(
    void *dictBuffer, size_t dictBufferCapacity,
    const void *samplesBuffer, const size_t *samplesSizes, unsigned nbSamples,
    ZDICT_legacy_params_t parameters);

/* Deprecation warnings */
/* It is generally possible to disable deprecation warnings from compiler,
   for example with -Wno-deprecated-declarations for gcc
   or _CRT_SECURE_NO_WARNINGS in Visual.
   Otherwise, it's also possible to manually define ZDICT_DISABLE_DEPRECATE_WARNINGS */
#ifdef ZDICT_DISABLE_DEPRECATE_WARNINGS
#  define ZDICT_DEPRECATED(message) ZDICTLIB_API   /* disable deprecation warnings */
#else
#  define ZDICT_GCC_VERSION (__GNUC__ * 100 + __GNUC_MINOR__)
#  if defined (__cplusplus) && (__cplusplus >= 201402) /* C++14 or greater */
#    define ZDICT_DEPRECATED(message) [[deprecated(message)]] ZDICTLIB_API
#  elif (ZDICT_GCC_VERSION >= 405) || defined(__clang__)
#    define ZDICT_DEPRECATED(message) ZDICTLIB_API __attribute__((deprecated(message)))
#  elif (ZDICT_GCC_VERSION >= 301)
#    define ZDICT_DEPRECATED(message) ZDICTLIB_API __attribute__((deprecated))
#  elif defined(_MSC_VER)
#    define ZDICT_DEPRECATED(message) ZDICTLIB_API __declspec(deprecated(message))
#  else
#    pragma message("WARNING: You need to implement ZDICT_DEPRECATED for this compiler")
#    define ZDICT_DEPRECATED(message) ZDICTLIB_API
#  endif
#endif /* ZDICT_DISABLE_DEPRECATE_WARNINGS */

ZDICT_DEPRECATED("use ZDICT_finalizeDictionary() instead")
size_t ZDICT_addEntropyTablesFromBuffer(void* dictBuffer, size_t dictContentSize, size_t dictBufferCapacity,
                                  const void* samplesBuffer, const size_t* samplesSizes, unsigned nbSamples);


#endif   /* ZDICT_STATIC_LINKING_ONLY */

#if defined (__cplusplus)
}
#endif

#endif   /* DICTBUILDER_H_001 */
