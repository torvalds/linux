/*===-- llvm-c/Remarks.h - Remarks Public C Interface -------------*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This header provides a public interface to a remark diagnostics library.   *|
|* LLVM provides an implementation of this interface.                         *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef LLVM_C_REMARKS_H
#define LLVM_C_REMARKS_H

#include "llvm-c/ExternC.h"
#include "llvm-c/Types.h"
#ifdef __cplusplus
#include <cstddef>
#else
#include <stddef.h>
#endif /* !defined(__cplusplus) */

LLVM_C_EXTERN_C_BEGIN

/**
 * @defgroup LLVMCREMARKS Remarks
 * @ingroup LLVMC
 *
 * @{
 */

// 0 -> 1: Bitstream remarks support.
#define REMARKS_API_VERSION 1

/**
 * The type of the emitted remark.
 */
enum LLVMRemarkType {
  LLVMRemarkTypeUnknown,
  LLVMRemarkTypePassed,
  LLVMRemarkTypeMissed,
  LLVMRemarkTypeAnalysis,
  LLVMRemarkTypeAnalysisFPCommute,
  LLVMRemarkTypeAnalysisAliasing,
  LLVMRemarkTypeFailure
};

/**
 * String containing a buffer and a length. The buffer is not guaranteed to be
 * zero-terminated.
 *
 * \since REMARKS_API_VERSION=0
 */
typedef struct LLVMRemarkOpaqueString *LLVMRemarkStringRef;

/**
 * Returns the buffer holding the string.
 *
 * \since REMARKS_API_VERSION=0
 */
extern const char *LLVMRemarkStringGetData(LLVMRemarkStringRef String);

/**
 * Returns the size of the string.
 *
 * \since REMARKS_API_VERSION=0
 */
extern uint32_t LLVMRemarkStringGetLen(LLVMRemarkStringRef String);

/**
 * DebugLoc containing File, Line and Column.
 *
 * \since REMARKS_API_VERSION=0
 */
typedef struct LLVMRemarkOpaqueDebugLoc *LLVMRemarkDebugLocRef;

/**
 * Return the path to the source file for a debug location.
 *
 * \since REMARKS_API_VERSION=0
 */
extern LLVMRemarkStringRef
LLVMRemarkDebugLocGetSourceFilePath(LLVMRemarkDebugLocRef DL);

/**
 * Return the line in the source file for a debug location.
 *
 * \since REMARKS_API_VERSION=0
 */
extern uint32_t LLVMRemarkDebugLocGetSourceLine(LLVMRemarkDebugLocRef DL);

/**
 * Return the column in the source file for a debug location.
 *
 * \since REMARKS_API_VERSION=0
 */
extern uint32_t LLVMRemarkDebugLocGetSourceColumn(LLVMRemarkDebugLocRef DL);

/**
 * Element of the "Args" list. The key might give more information about what
 * the semantics of the value are, e.g. "Callee" will tell you that the value
 * is a symbol that names a function.
 *
 * \since REMARKS_API_VERSION=0
 */
typedef struct LLVMRemarkOpaqueArg *LLVMRemarkArgRef;

/**
 * Returns the key of an argument. The key defines what the value is, and the
 * same key can appear multiple times in the list of arguments.
 *
 * \since REMARKS_API_VERSION=0
 */
extern LLVMRemarkStringRef LLVMRemarkArgGetKey(LLVMRemarkArgRef Arg);

/**
 * Returns the value of an argument. This is a string that can contain newlines.
 *
 * \since REMARKS_API_VERSION=0
 */
extern LLVMRemarkStringRef LLVMRemarkArgGetValue(LLVMRemarkArgRef Arg);

/**
 * Returns the debug location that is attached to the value of this argument.
 *
 * If there is no debug location, the return value will be `NULL`.
 *
 * \since REMARKS_API_VERSION=0
 */
extern LLVMRemarkDebugLocRef LLVMRemarkArgGetDebugLoc(LLVMRemarkArgRef Arg);

/**
 * A remark emitted by the compiler.
 *
 * \since REMARKS_API_VERSION=0
 */
typedef struct LLVMRemarkOpaqueEntry *LLVMRemarkEntryRef;

/**
 * Free the resources used by the remark entry.
 *
 * \since REMARKS_API_VERSION=0
 */
extern void LLVMRemarkEntryDispose(LLVMRemarkEntryRef Remark);

/**
 * The type of the remark. For example, it can allow users to only keep the
 * missed optimizations from the compiler.
 *
 * \since REMARKS_API_VERSION=0
 */
extern enum LLVMRemarkType LLVMRemarkEntryGetType(LLVMRemarkEntryRef Remark);

/**
 * Get the name of the pass that emitted this remark.
 *
 * \since REMARKS_API_VERSION=0
 */
extern LLVMRemarkStringRef
LLVMRemarkEntryGetPassName(LLVMRemarkEntryRef Remark);

/**
 * Get an identifier of the remark.
 *
 * \since REMARKS_API_VERSION=0
 */
extern LLVMRemarkStringRef
LLVMRemarkEntryGetRemarkName(LLVMRemarkEntryRef Remark);

/**
 * Get the name of the function being processed when the remark was emitted.
 *
 * \since REMARKS_API_VERSION=0
 */
extern LLVMRemarkStringRef
LLVMRemarkEntryGetFunctionName(LLVMRemarkEntryRef Remark);

/**
 * Returns the debug location that is attached to this remark.
 *
 * If there is no debug location, the return value will be `NULL`.
 *
 * \since REMARKS_API_VERSION=0
 */
extern LLVMRemarkDebugLocRef
LLVMRemarkEntryGetDebugLoc(LLVMRemarkEntryRef Remark);

/**
 * Return the hotness of the remark.
 *
 * A hotness of `0` means this value is not set.
 *
 * \since REMARKS_API_VERSION=0
 */
extern uint64_t LLVMRemarkEntryGetHotness(LLVMRemarkEntryRef Remark);

/**
 * The number of arguments the remark holds.
 *
 * \since REMARKS_API_VERSION=0
 */
extern uint32_t LLVMRemarkEntryGetNumArgs(LLVMRemarkEntryRef Remark);

/**
 * Get a new iterator to iterate over a remark's argument.
 *
 * If there are no arguments in \p Remark, the return value will be `NULL`.
 *
 * The lifetime of the returned value is bound to the lifetime of \p Remark.
 *
 * \since REMARKS_API_VERSION=0
 */
extern LLVMRemarkArgRef LLVMRemarkEntryGetFirstArg(LLVMRemarkEntryRef Remark);

/**
 * Get the next argument in \p Remark from the position of \p It.
 *
 * Returns `NULL` if there are no more arguments available.
 *
 * The lifetime of the returned value is bound to the lifetime of \p Remark.
 *
 * \since REMARKS_API_VERSION=0
 */
extern LLVMRemarkArgRef LLVMRemarkEntryGetNextArg(LLVMRemarkArgRef It,
                                                  LLVMRemarkEntryRef Remark);

typedef struct LLVMRemarkOpaqueParser *LLVMRemarkParserRef;

/**
 * Creates a remark parser that can be used to parse the buffer located in \p
 * Buf of size \p Size bytes.
 *
 * \p Buf cannot be `NULL`.
 *
 * This function should be paired with LLVMRemarkParserDispose() to avoid
 * leaking resources.
 *
 * \since REMARKS_API_VERSION=0
 */
extern LLVMRemarkParserRef LLVMRemarkParserCreateYAML(const void *Buf,
                                                      uint64_t Size);

/**
 * Creates a remark parser that can be used to parse the buffer located in \p
 * Buf of size \p Size bytes.
 *
 * \p Buf cannot be `NULL`.
 *
 * This function should be paired with LLVMRemarkParserDispose() to avoid
 * leaking resources.
 *
 * \since REMARKS_API_VERSION=1
 */
extern LLVMRemarkParserRef LLVMRemarkParserCreateBitstream(const void *Buf,
                                                           uint64_t Size);

/**
 * Returns the next remark in the file.
 *
 * The value pointed to by the return value needs to be disposed using a call to
 * LLVMRemarkEntryDispose().
 *
 * All the entries in the returned value that are of LLVMRemarkStringRef type
 * will become invalidated once a call to LLVMRemarkParserDispose is made.
 *
 * If the parser reaches the end of the buffer, the return value will be `NULL`.
 *
 * In the case of an error, the return value will be `NULL`, and:
 *
 * 1) LLVMRemarkParserHasError() will return `1`.
 *
 * 2) LLVMRemarkParserGetErrorMessage() will return a descriptive error
 *    message.
 *
 * An error may occur if:
 *
 * 1) An argument is invalid.
 *
 * 2) There is a parsing error. This can occur on things like malformed YAML.
 *
 * 3) There is a Remark semantic error. This can occur on well-formed files with
 *    missing or extra fields.
 *
 * Here is a quick example of the usage:
 *
 * ```
 * LLVMRemarkParserRef Parser = LLVMRemarkParserCreateYAML(Buf, Size);
 * LLVMRemarkEntryRef Remark = NULL;
 * while ((Remark = LLVMRemarkParserGetNext(Parser))) {
 *    // use Remark
 *    LLVMRemarkEntryDispose(Remark); // Release memory.
 * }
 * bool HasError = LLVMRemarkParserHasError(Parser);
 * LLVMRemarkParserDispose(Parser);
 * ```
 *
 * \since REMARKS_API_VERSION=0
 */
extern LLVMRemarkEntryRef LLVMRemarkParserGetNext(LLVMRemarkParserRef Parser);

/**
 * Returns `1` if the parser encountered an error while parsing the buffer.
 *
 * \since REMARKS_API_VERSION=0
 */
extern LLVMBool LLVMRemarkParserHasError(LLVMRemarkParserRef Parser);

/**
 * Returns a null-terminated string containing an error message.
 *
 * In case of no error, the result is `NULL`.
 *
 * The memory of the string is bound to the lifetime of \p Parser. If
 * LLVMRemarkParserDispose() is called, the memory of the string will be
 * released.
 *
 * \since REMARKS_API_VERSION=0
 */
extern const char *LLVMRemarkParserGetErrorMessage(LLVMRemarkParserRef Parser);

/**
 * Releases all the resources used by \p Parser.
 *
 * \since REMARKS_API_VERSION=0
 */
extern void LLVMRemarkParserDispose(LLVMRemarkParserRef Parser);

/**
 * Returns the version of the remarks library.
 *
 * \since REMARKS_API_VERSION=0
 */
extern uint32_t LLVMRemarkVersion(void);

/**
 * @} // endgoup LLVMCREMARKS
 */

LLVM_C_EXTERN_C_END

#endif /* LLVM_C_REMARKS_H */
