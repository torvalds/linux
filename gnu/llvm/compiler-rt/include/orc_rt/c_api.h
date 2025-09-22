/*===- c_api.h - C API for the ORC runtime ------------------------*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This file defines the C API for the ORC runtime                            *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef ORC_RT_C_API_H
#define ORC_RT_C_API_H

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Helper to suppress strict prototype warnings. */
#ifdef __clang__
#define ORC_RT_C_STRICT_PROTOTYPES_BEGIN                                       \
  _Pragma("clang diagnostic push")                                             \
      _Pragma("clang diagnostic error \"-Wstrict-prototypes\"")
#define ORC_RT_C_STRICT_PROTOTYPES_END _Pragma("clang diagnostic pop")
#else
#define ORC_RT_C_STRICT_PROTOTYPES_BEGIN
#define ORC_RT_C_STRICT_PROTOTYPES_END
#endif

/* Helper to wrap C code for C++ */
#ifdef __cplusplus
#define ORC_RT_C_EXTERN_C_BEGIN                                                \
  extern "C" {                                                                 \
  ORC_RT_C_STRICT_PROTOTYPES_BEGIN
#define ORC_RT_C_EXTERN_C_END                                                  \
  ORC_RT_C_STRICT_PROTOTYPES_END                                               \
  }
#else
#define ORC_RT_C_EXTERN_C_BEGIN ORC_RT_C_STRICT_PROTOTYPES_BEGIN
#define ORC_RT_C_EXTERN_C_END ORC_RT_C_STRICT_PROTOTYPES_END
#endif

ORC_RT_C_EXTERN_C_BEGIN

typedef union {
  char *ValuePtr;
  char Value[sizeof(char *)];
} orc_rt_CWrapperFunctionResultDataUnion;

/**
 * orc_rt_CWrapperFunctionResult is a kind of C-SmallVector with an
 * out-of-band error state.
 *
 * If Size == 0 and Data.ValuePtr is non-zero then the value is in the
 * 'out-of-band error' state, and Data.ValuePtr points at a malloc-allocated,
 * null-terminated string error message.
 *
 * If Size <= sizeof(orc_rt_CWrapperFunctionResultData) then the value is in
 * the 'small' state and the content is held in the first Size bytes of
 * Data.Value.
 *
 * If Size > sizeof(OrtRTCWrapperFunctionResultData) then the value is in the
 * 'large' state and the content is held in the first Size bytes of the
 * memory pointed to by Data.ValuePtr. This memory must have been allocated by
 * malloc, and will be freed with free when this value is destroyed.
 */
typedef struct {
  orc_rt_CWrapperFunctionResultDataUnion Data;
  size_t Size;
} orc_rt_CWrapperFunctionResult;

/**
 * Zero-initialize an orc_rt_CWrapperFunctionResult.
 */
static inline void
orc_rt_CWrapperFunctionResultInit(orc_rt_CWrapperFunctionResult *R) {
  R->Size = 0;
  R->Data.ValuePtr = 0;
}

/**
 * Create an orc_rt_CWrapperFunctionResult with an uninitialized buffer of
 * size Size. The buffer is returned via the DataPtr argument.
 */
static inline orc_rt_CWrapperFunctionResult
orc_rt_CWrapperFunctionResultAllocate(size_t Size) {
  orc_rt_CWrapperFunctionResult R;
  R.Size = Size;
  // If Size is 0 ValuePtr must be 0 or it is considered an out-of-band error.
  R.Data.ValuePtr = 0;
  if (Size > sizeof(R.Data.Value))
    R.Data.ValuePtr = (char *)malloc(Size);
  return R;
}

/**
 * Create an orc_rt_WrapperFunctionResult from the given data range.
 */
static inline orc_rt_CWrapperFunctionResult
orc_rt_CreateCWrapperFunctionResultFromRange(const char *Data, size_t Size) {
  orc_rt_CWrapperFunctionResult R;
  R.Size = Size;
  if (R.Size > sizeof(R.Data.Value)) {
    char *Tmp = (char *)malloc(Size);
    memcpy(Tmp, Data, Size);
    R.Data.ValuePtr = Tmp;
  } else
    memcpy(R.Data.Value, Data, Size);
  return R;
}

/**
 * Create an orc_rt_CWrapperFunctionResult by copying the given string,
 * including the null-terminator.
 *
 * This function copies the input string. The client is responsible for freeing
 * the ErrMsg arg.
 */
static inline orc_rt_CWrapperFunctionResult
orc_rt_CreateCWrapperFunctionResultFromString(const char *Source) {
  return orc_rt_CreateCWrapperFunctionResultFromRange(Source,
                                                      strlen(Source) + 1);
}

/**
 * Create an orc_rt_CWrapperFunctionResult representing an out-of-band
 * error.
 *
 * This function copies the input string. The client is responsible for freeing
 * the ErrMsg arg.
 */
static inline orc_rt_CWrapperFunctionResult
orc_rt_CreateCWrapperFunctionResultFromOutOfBandError(const char *ErrMsg) {
  orc_rt_CWrapperFunctionResult R;
  R.Size = 0;
  char *Tmp = (char *)malloc(strlen(ErrMsg) + 1);
  strcpy(Tmp, ErrMsg);
  R.Data.ValuePtr = Tmp;
  return R;
}

/**
 * This should be called to destroy orc_rt_CWrapperFunctionResult values
 * regardless of their state.
 */
static inline void
orc_rt_DisposeCWrapperFunctionResult(orc_rt_CWrapperFunctionResult *R) {
  if (R->Size > sizeof(R->Data.Value) ||
      (R->Size == 0 && R->Data.ValuePtr))
    free(R->Data.ValuePtr);
}

/**
 * Get a pointer to the data contained in the given
 * orc_rt_CWrapperFunctionResult.
 */
static inline char *
orc_rt_CWrapperFunctionResultData(orc_rt_CWrapperFunctionResult *R) {
  assert((R->Size != 0 || R->Data.ValuePtr == NULL) &&
         "Cannot get data for out-of-band error value");
  return R->Size > sizeof(R->Data.Value) ? R->Data.ValuePtr : R->Data.Value;
}

/**
 * Safely get the size of the given orc_rt_CWrapperFunctionResult.
 *
 * Asserts that we're not trying to access the size of an error value.
 */
static inline size_t
orc_rt_CWrapperFunctionResultSize(const orc_rt_CWrapperFunctionResult *R) {
  assert((R->Size != 0 || R->Data.ValuePtr == NULL) &&
         "Cannot get size for out-of-band error value");
  return R->Size;
}

/**
 * Returns 1 if this value is equivalent to a value just initialized by
 * orc_rt_CWrapperFunctionResultInit, 0 otherwise.
 */
static inline size_t
orc_rt_CWrapperFunctionResultEmpty(const orc_rt_CWrapperFunctionResult *R) {
  return R->Size == 0 && R->Data.ValuePtr == 0;
}

/**
 * Returns a pointer to the out-of-band error string for this
 * orc_rt_CWrapperFunctionResult, or null if there is no error.
 *
 * The orc_rt_CWrapperFunctionResult retains ownership of the error
 * string, so it should be copied if the caller wishes to preserve it.
 */
static inline const char *orc_rt_CWrapperFunctionResultGetOutOfBandError(
    const orc_rt_CWrapperFunctionResult *R) {
  return R->Size == 0 ? R->Data.ValuePtr : 0;
}

ORC_RT_C_EXTERN_C_END

#endif /* ORC_RT_C_API_H */
