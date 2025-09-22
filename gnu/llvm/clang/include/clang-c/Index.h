/*===-- clang-c/Index.h - Indexing Public C Interface -------------*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This header provides a public interface to a Clang library for extracting  *|
|* high-level symbol information from source files without exposing the full  *|
|* Clang C++ API.                                                             *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef LLVM_CLANG_C_INDEX_H
#define LLVM_CLANG_C_INDEX_H

#include "clang-c/BuildSystem.h"
#include "clang-c/CXDiagnostic.h"
#include "clang-c/CXErrorCode.h"
#include "clang-c/CXFile.h"
#include "clang-c/CXSourceLocation.h"
#include "clang-c/CXString.h"
#include "clang-c/ExternC.h"
#include "clang-c/Platform.h"

/**
 * The version constants for the libclang API.
 * CINDEX_VERSION_MINOR should increase when there are API additions.
 * CINDEX_VERSION_MAJOR is intended for "major" source/ABI breaking changes.
 *
 * The policy about the libclang API was always to keep it source and ABI
 * compatible, thus CINDEX_VERSION_MAJOR is expected to remain stable.
 */
#define CINDEX_VERSION_MAJOR 0
#define CINDEX_VERSION_MINOR 64

#define CINDEX_VERSION_ENCODE(major, minor) (((major)*10000) + ((minor)*1))

#define CINDEX_VERSION                                                         \
  CINDEX_VERSION_ENCODE(CINDEX_VERSION_MAJOR, CINDEX_VERSION_MINOR)

#define CINDEX_VERSION_STRINGIZE_(major, minor) #major "." #minor
#define CINDEX_VERSION_STRINGIZE(major, minor)                                 \
  CINDEX_VERSION_STRINGIZE_(major, minor)

#define CINDEX_VERSION_STRING                                                  \
  CINDEX_VERSION_STRINGIZE(CINDEX_VERSION_MAJOR, CINDEX_VERSION_MINOR)

#ifndef __has_feature
#define __has_feature(feature) 0
#endif

LLVM_CLANG_C_EXTERN_C_BEGIN

/** \defgroup CINDEX libclang: C Interface to Clang
 *
 * The C Interface to Clang provides a relatively small API that exposes
 * facilities for parsing source code into an abstract syntax tree (AST),
 * loading already-parsed ASTs, traversing the AST, associating
 * physical source locations with elements within the AST, and other
 * facilities that support Clang-based development tools.
 *
 * This C interface to Clang will never provide all of the information
 * representation stored in Clang's C++ AST, nor should it: the intent is to
 * maintain an API that is relatively stable from one release to the next,
 * providing only the basic functionality needed to support development tools.
 *
 * To avoid namespace pollution, data types are prefixed with "CX" and
 * functions are prefixed with "clang_".
 *
 * @{
 */

/**
 * An "index" that consists of a set of translation units that would
 * typically be linked together into an executable or library.
 */
typedef void *CXIndex;

/**
 * An opaque type representing target information for a given translation
 * unit.
 */
typedef struct CXTargetInfoImpl *CXTargetInfo;

/**
 * A single translation unit, which resides in an index.
 */
typedef struct CXTranslationUnitImpl *CXTranslationUnit;

/**
 * Opaque pointer representing client data that will be passed through
 * to various callbacks and visitors.
 */
typedef void *CXClientData;

/**
 * Provides the contents of a file that has not yet been saved to disk.
 *
 * Each CXUnsavedFile instance provides the name of a file on the
 * system along with the current contents of that file that have not
 * yet been saved to disk.
 */
struct CXUnsavedFile {
  /**
   * The file whose contents have not yet been saved.
   *
   * This file must already exist in the file system.
   */
  const char *Filename;

  /**
   * A buffer containing the unsaved contents of this file.
   */
  const char *Contents;

  /**
   * The length of the unsaved contents of this buffer.
   */
  unsigned long Length;
};

/**
 * Describes the availability of a particular entity, which indicates
 * whether the use of this entity will result in a warning or error due to
 * it being deprecated or unavailable.
 */
enum CXAvailabilityKind {
  /**
   * The entity is available.
   */
  CXAvailability_Available,
  /**
   * The entity is available, but has been deprecated (and its use is
   * not recommended).
   */
  CXAvailability_Deprecated,
  /**
   * The entity is not available; any use of it will be an error.
   */
  CXAvailability_NotAvailable,
  /**
   * The entity is available, but not accessible; any use of it will be
   * an error.
   */
  CXAvailability_NotAccessible
};

/**
 * Describes a version number of the form major.minor.subminor.
 */
typedef struct CXVersion {
  /**
   * The major version number, e.g., the '10' in '10.7.3'. A negative
   * value indicates that there is no version number at all.
   */
  int Major;
  /**
   * The minor version number, e.g., the '7' in '10.7.3'. This value
   * will be negative if no minor version number was provided, e.g., for
   * version '10'.
   */
  int Minor;
  /**
   * The subminor version number, e.g., the '3' in '10.7.3'. This value
   * will be negative if no minor or subminor version number was provided,
   * e.g., in version '10' or '10.7'.
   */
  int Subminor;
} CXVersion;

/**
 * Describes the exception specification of a cursor.
 *
 * A negative value indicates that the cursor is not a function declaration.
 */
enum CXCursor_ExceptionSpecificationKind {
  /**
   * The cursor has no exception specification.
   */
  CXCursor_ExceptionSpecificationKind_None,

  /**
   * The cursor has exception specification throw()
   */
  CXCursor_ExceptionSpecificationKind_DynamicNone,

  /**
   * The cursor has exception specification throw(T1, T2)
   */
  CXCursor_ExceptionSpecificationKind_Dynamic,

  /**
   * The cursor has exception specification throw(...).
   */
  CXCursor_ExceptionSpecificationKind_MSAny,

  /**
   * The cursor has exception specification basic noexcept.
   */
  CXCursor_ExceptionSpecificationKind_BasicNoexcept,

  /**
   * The cursor has exception specification computed noexcept.
   */
  CXCursor_ExceptionSpecificationKind_ComputedNoexcept,

  /**
   * The exception specification has not yet been evaluated.
   */
  CXCursor_ExceptionSpecificationKind_Unevaluated,

  /**
   * The exception specification has not yet been instantiated.
   */
  CXCursor_ExceptionSpecificationKind_Uninstantiated,

  /**
   * The exception specification has not been parsed yet.
   */
  CXCursor_ExceptionSpecificationKind_Unparsed,

  /**
   * The cursor has a __declspec(nothrow) exception specification.
   */
  CXCursor_ExceptionSpecificationKind_NoThrow
};

/**
 * Provides a shared context for creating translation units.
 *
 * It provides two options:
 *
 * - excludeDeclarationsFromPCH: When non-zero, allows enumeration of "local"
 * declarations (when loading any new translation units). A "local" declaration
 * is one that belongs in the translation unit itself and not in a precompiled
 * header that was used by the translation unit. If zero, all declarations
 * will be enumerated.
 *
 * Here is an example:
 *
 * \code
 *   // excludeDeclsFromPCH = 1, displayDiagnostics=1
 *   Idx = clang_createIndex(1, 1);
 *
 *   // IndexTest.pch was produced with the following command:
 *   // "clang -x c IndexTest.h -emit-ast -o IndexTest.pch"
 *   TU = clang_createTranslationUnit(Idx, "IndexTest.pch");
 *
 *   // This will load all the symbols from 'IndexTest.pch'
 *   clang_visitChildren(clang_getTranslationUnitCursor(TU),
 *                       TranslationUnitVisitor, 0);
 *   clang_disposeTranslationUnit(TU);
 *
 *   // This will load all the symbols from 'IndexTest.c', excluding symbols
 *   // from 'IndexTest.pch'.
 *   char *args[] = { "-Xclang", "-include-pch=IndexTest.pch" };
 *   TU = clang_createTranslationUnitFromSourceFile(Idx, "IndexTest.c", 2, args,
 *                                                  0, 0);
 *   clang_visitChildren(clang_getTranslationUnitCursor(TU),
 *                       TranslationUnitVisitor, 0);
 *   clang_disposeTranslationUnit(TU);
 * \endcode
 *
 * This process of creating the 'pch', loading it separately, and using it (via
 * -include-pch) allows 'excludeDeclsFromPCH' to remove redundant callbacks
 * (which gives the indexer the same performance benefit as the compiler).
 */
CINDEX_LINKAGE CXIndex clang_createIndex(int excludeDeclarationsFromPCH,
                                         int displayDiagnostics);

/**
 * Destroy the given index.
 *
 * The index must not be destroyed until all of the translation units created
 * within that index have been destroyed.
 */
CINDEX_LINKAGE void clang_disposeIndex(CXIndex index);

typedef enum {
  /**
   * Use the default value of an option that may depend on the process
   * environment.
   */
  CXChoice_Default = 0,
  /**
   * Enable the option.
   */
  CXChoice_Enabled = 1,
  /**
   * Disable the option.
   */
  CXChoice_Disabled = 2
} CXChoice;

typedef enum {
  /**
   * Used to indicate that no special CXIndex options are needed.
   */
  CXGlobalOpt_None = 0x0,

  /**
   * Used to indicate that threads that libclang creates for indexing
   * purposes should use background priority.
   *
   * Affects #clang_indexSourceFile, #clang_indexTranslationUnit,
   * #clang_parseTranslationUnit, #clang_saveTranslationUnit.
   */
  CXGlobalOpt_ThreadBackgroundPriorityForIndexing = 0x1,

  /**
   * Used to indicate that threads that libclang creates for editing
   * purposes should use background priority.
   *
   * Affects #clang_reparseTranslationUnit, #clang_codeCompleteAt,
   * #clang_annotateTokens
   */
  CXGlobalOpt_ThreadBackgroundPriorityForEditing = 0x2,

  /**
   * Used to indicate that all threads that libclang creates should use
   * background priority.
   */
  CXGlobalOpt_ThreadBackgroundPriorityForAll =
      CXGlobalOpt_ThreadBackgroundPriorityForIndexing |
      CXGlobalOpt_ThreadBackgroundPriorityForEditing

} CXGlobalOptFlags;

/**
 * Index initialization options.
 *
 * 0 is the default value of each member of this struct except for Size.
 * Initialize the struct in one of the following three ways to avoid adapting
 * code each time a new member is added to it:
 * \code
 * CXIndexOptions Opts;
 * memset(&Opts, 0, sizeof(Opts));
 * Opts.Size = sizeof(CXIndexOptions);
 * \endcode
 * or explicitly initialize the first data member and zero-initialize the rest:
 * \code
 * CXIndexOptions Opts = { sizeof(CXIndexOptions) };
 * \endcode
 * or to prevent the -Wmissing-field-initializers warning for the above version:
 * \code
 * CXIndexOptions Opts{};
 * Opts.Size = sizeof(CXIndexOptions);
 * \endcode
 */
typedef struct CXIndexOptions {
  /**
   * The size of struct CXIndexOptions used for option versioning.
   *
   * Always initialize this member to sizeof(CXIndexOptions), or assign
   * sizeof(CXIndexOptions) to it right after creating a CXIndexOptions object.
   */
  unsigned Size;
  /**
   * A CXChoice enumerator that specifies the indexing priority policy.
   * \sa CXGlobalOpt_ThreadBackgroundPriorityForIndexing
   */
  unsigned char ThreadBackgroundPriorityForIndexing;
  /**
   * A CXChoice enumerator that specifies the editing priority policy.
   * \sa CXGlobalOpt_ThreadBackgroundPriorityForEditing
   */
  unsigned char ThreadBackgroundPriorityForEditing;
  /**
   * \see clang_createIndex()
   */
  unsigned ExcludeDeclarationsFromPCH : 1;
  /**
   * \see clang_createIndex()
   */
  unsigned DisplayDiagnostics : 1;
  /**
   * Store PCH in memory. If zero, PCH are stored in temporary files.
   */
  unsigned StorePreamblesInMemory : 1;
  unsigned /*Reserved*/ : 13;

  /**
   * The path to a directory, in which to store temporary PCH files. If null or
   * empty, the default system temporary directory is used. These PCH files are
   * deleted on clean exit but stay on disk if the program crashes or is killed.
   *
   * This option is ignored if \a StorePreamblesInMemory is non-zero.
   *
   * Libclang does not create the directory at the specified path in the file
   * system. Therefore it must exist, or storing PCH files will fail.
   */
  const char *PreambleStoragePath;
  /**
   * Specifies a path which will contain log files for certain libclang
   * invocations. A null value implies that libclang invocations are not logged.
   */
  const char *InvocationEmissionPath;
} CXIndexOptions;

/**
 * Provides a shared context for creating translation units.
 *
 * Call this function instead of clang_createIndex() if you need to configure
 * the additional options in CXIndexOptions.
 *
 * \returns The created index or null in case of error, such as an unsupported
 * value of options->Size.
 *
 * For example:
 * \code
 * CXIndex createIndex(const char *ApplicationTemporaryPath) {
 *   const int ExcludeDeclarationsFromPCH = 1;
 *   const int DisplayDiagnostics = 1;
 *   CXIndex Idx;
 * #if CINDEX_VERSION_MINOR >= 64
 *   CXIndexOptions Opts;
 *   memset(&Opts, 0, sizeof(Opts));
 *   Opts.Size = sizeof(CXIndexOptions);
 *   Opts.ThreadBackgroundPriorityForIndexing = 1;
 *   Opts.ExcludeDeclarationsFromPCH = ExcludeDeclarationsFromPCH;
 *   Opts.DisplayDiagnostics = DisplayDiagnostics;
 *   Opts.PreambleStoragePath = ApplicationTemporaryPath;
 *   Idx = clang_createIndexWithOptions(&Opts);
 *   if (Idx)
 *     return Idx;
 *   fprintf(stderr,
 *           "clang_createIndexWithOptions() failed. "
 *           "CINDEX_VERSION_MINOR = %d, sizeof(CXIndexOptions) = %u\n",
 *           CINDEX_VERSION_MINOR, Opts.Size);
 * #else
 *   (void)ApplicationTemporaryPath;
 * #endif
 *   Idx = clang_createIndex(ExcludeDeclarationsFromPCH, DisplayDiagnostics);
 *   clang_CXIndex_setGlobalOptions(
 *       Idx, clang_CXIndex_getGlobalOptions(Idx) |
 *                CXGlobalOpt_ThreadBackgroundPriorityForIndexing);
 *   return Idx;
 * }
 * \endcode
 *
 * \sa clang_createIndex()
 */
CINDEX_LINKAGE CXIndex
clang_createIndexWithOptions(const CXIndexOptions *options);

/**
 * Sets general options associated with a CXIndex.
 *
 * This function is DEPRECATED. Set
 * CXIndexOptions::ThreadBackgroundPriorityForIndexing and/or
 * CXIndexOptions::ThreadBackgroundPriorityForEditing and call
 * clang_createIndexWithOptions() instead.
 *
 * For example:
 * \code
 * CXIndex idx = ...;
 * clang_CXIndex_setGlobalOptions(idx,
 *     clang_CXIndex_getGlobalOptions(idx) |
 *     CXGlobalOpt_ThreadBackgroundPriorityForIndexing);
 * \endcode
 *
 * \param options A bitmask of options, a bitwise OR of CXGlobalOpt_XXX flags.
 */
CINDEX_LINKAGE void clang_CXIndex_setGlobalOptions(CXIndex, unsigned options);

/**
 * Gets the general options associated with a CXIndex.
 *
 * This function allows to obtain the final option values used by libclang after
 * specifying the option policies via CXChoice enumerators.
 *
 * \returns A bitmask of options, a bitwise OR of CXGlobalOpt_XXX flags that
 * are associated with the given CXIndex object.
 */
CINDEX_LINKAGE unsigned clang_CXIndex_getGlobalOptions(CXIndex);

/**
 * Sets the invocation emission path option in a CXIndex.
 *
 * This function is DEPRECATED. Set CXIndexOptions::InvocationEmissionPath and
 * call clang_createIndexWithOptions() instead.
 *
 * The invocation emission path specifies a path which will contain log
 * files for certain libclang invocations. A null value (default) implies that
 * libclang invocations are not logged..
 */
CINDEX_LINKAGE void
clang_CXIndex_setInvocationEmissionPathOption(CXIndex, const char *Path);

/**
 * Determine whether the given header is guarded against
 * multiple inclusions, either with the conventional
 * \#ifndef/\#define/\#endif macro guards or with \#pragma once.
 */
CINDEX_LINKAGE unsigned clang_isFileMultipleIncludeGuarded(CXTranslationUnit tu,
                                                           CXFile file);

/**
 * Retrieve a file handle within the given translation unit.
 *
 * \param tu the translation unit
 *
 * \param file_name the name of the file.
 *
 * \returns the file handle for the named file in the translation unit \p tu,
 * or a NULL file handle if the file was not a part of this translation unit.
 */
CINDEX_LINKAGE CXFile clang_getFile(CXTranslationUnit tu,
                                    const char *file_name);

/**
 * Retrieve the buffer associated with the given file.
 *
 * \param tu the translation unit
 *
 * \param file the file for which to retrieve the buffer.
 *
 * \param size [out] if non-NULL, will be set to the size of the buffer.
 *
 * \returns a pointer to the buffer in memory that holds the contents of
 * \p file, or a NULL pointer when the file is not loaded.
 */
CINDEX_LINKAGE const char *clang_getFileContents(CXTranslationUnit tu,
                                                 CXFile file, size_t *size);

/**
 * Retrieves the source location associated with a given file/line/column
 * in a particular translation unit.
 */
CINDEX_LINKAGE CXSourceLocation clang_getLocation(CXTranslationUnit tu,
                                                  CXFile file, unsigned line,
                                                  unsigned column);
/**
 * Retrieves the source location associated with a given character offset
 * in a particular translation unit.
 */
CINDEX_LINKAGE CXSourceLocation clang_getLocationForOffset(CXTranslationUnit tu,
                                                           CXFile file,
                                                           unsigned offset);

/**
 * Retrieve all ranges that were skipped by the preprocessor.
 *
 * The preprocessor will skip lines when they are surrounded by an
 * if/ifdef/ifndef directive whose condition does not evaluate to true.
 */
CINDEX_LINKAGE CXSourceRangeList *clang_getSkippedRanges(CXTranslationUnit tu,
                                                         CXFile file);

/**
 * Retrieve all ranges from all files that were skipped by the
 * preprocessor.
 *
 * The preprocessor will skip lines when they are surrounded by an
 * if/ifdef/ifndef directive whose condition does not evaluate to true.
 */
CINDEX_LINKAGE CXSourceRangeList *
clang_getAllSkippedRanges(CXTranslationUnit tu);

/**
 * Determine the number of diagnostics produced for the given
 * translation unit.
 */
CINDEX_LINKAGE unsigned clang_getNumDiagnostics(CXTranslationUnit Unit);

/**
 * Retrieve a diagnostic associated with the given translation unit.
 *
 * \param Unit the translation unit to query.
 * \param Index the zero-based diagnostic number to retrieve.
 *
 * \returns the requested diagnostic. This diagnostic must be freed
 * via a call to \c clang_disposeDiagnostic().
 */
CINDEX_LINKAGE CXDiagnostic clang_getDiagnostic(CXTranslationUnit Unit,
                                                unsigned Index);

/**
 * Retrieve the complete set of diagnostics associated with a
 *        translation unit.
 *
 * \param Unit the translation unit to query.
 */
CINDEX_LINKAGE CXDiagnosticSet
clang_getDiagnosticSetFromTU(CXTranslationUnit Unit);

/**
 * \defgroup CINDEX_TRANSLATION_UNIT Translation unit manipulation
 *
 * The routines in this group provide the ability to create and destroy
 * translation units from files, either by parsing the contents of the files or
 * by reading in a serialized representation of a translation unit.
 *
 * @{
 */

/**
 * Get the original translation unit source file name.
 */
CINDEX_LINKAGE CXString
clang_getTranslationUnitSpelling(CXTranslationUnit CTUnit);

/**
 * Return the CXTranslationUnit for a given source file and the provided
 * command line arguments one would pass to the compiler.
 *
 * Note: The 'source_filename' argument is optional.  If the caller provides a
 * NULL pointer, the name of the source file is expected to reside in the
 * specified command line arguments.
 *
 * Note: When encountered in 'clang_command_line_args', the following options
 * are ignored:
 *
 *   '-c'
 *   '-emit-ast'
 *   '-fsyntax-only'
 *   '-o \<output file>'  (both '-o' and '\<output file>' are ignored)
 *
 * \param CIdx The index object with which the translation unit will be
 * associated.
 *
 * \param source_filename The name of the source file to load, or NULL if the
 * source file is included in \p clang_command_line_args.
 *
 * \param num_clang_command_line_args The number of command-line arguments in
 * \p clang_command_line_args.
 *
 * \param clang_command_line_args The command-line arguments that would be
 * passed to the \c clang executable if it were being invoked out-of-process.
 * These command-line options will be parsed and will affect how the translation
 * unit is parsed. Note that the following options are ignored: '-c',
 * '-emit-ast', '-fsyntax-only' (which is the default), and '-o \<output file>'.
 *
 * \param num_unsaved_files the number of unsaved file entries in \p
 * unsaved_files.
 *
 * \param unsaved_files the files that have not yet been saved to disk
 * but may be required for code completion, including the contents of
 * those files.  The contents and name of these files (as specified by
 * CXUnsavedFile) are copied when necessary, so the client only needs to
 * guarantee their validity until the call to this function returns.
 */
CINDEX_LINKAGE CXTranslationUnit clang_createTranslationUnitFromSourceFile(
    CXIndex CIdx, const char *source_filename, int num_clang_command_line_args,
    const char *const *clang_command_line_args, unsigned num_unsaved_files,
    struct CXUnsavedFile *unsaved_files);

/**
 * Same as \c clang_createTranslationUnit2, but returns
 * the \c CXTranslationUnit instead of an error code.  In case of an error this
 * routine returns a \c NULL \c CXTranslationUnit, without further detailed
 * error codes.
 */
CINDEX_LINKAGE CXTranslationUnit
clang_createTranslationUnit(CXIndex CIdx, const char *ast_filename);

/**
 * Create a translation unit from an AST file (\c -emit-ast).
 *
 * \param[out] out_TU A non-NULL pointer to store the created
 * \c CXTranslationUnit.
 *
 * \returns Zero on success, otherwise returns an error code.
 */
CINDEX_LINKAGE enum CXErrorCode
clang_createTranslationUnit2(CXIndex CIdx, const char *ast_filename,
                             CXTranslationUnit *out_TU);

/**
 * Flags that control the creation of translation units.
 *
 * The enumerators in this enumeration type are meant to be bitwise
 * ORed together to specify which options should be used when
 * constructing the translation unit.
 */
enum CXTranslationUnit_Flags {
  /**
   * Used to indicate that no special translation-unit options are
   * needed.
   */
  CXTranslationUnit_None = 0x0,

  /**
   * Used to indicate that the parser should construct a "detailed"
   * preprocessing record, including all macro definitions and instantiations.
   *
   * Constructing a detailed preprocessing record requires more memory
   * and time to parse, since the information contained in the record
   * is usually not retained. However, it can be useful for
   * applications that require more detailed information about the
   * behavior of the preprocessor.
   */
  CXTranslationUnit_DetailedPreprocessingRecord = 0x01,

  /**
   * Used to indicate that the translation unit is incomplete.
   *
   * When a translation unit is considered "incomplete", semantic
   * analysis that is typically performed at the end of the
   * translation unit will be suppressed. For example, this suppresses
   * the completion of tentative declarations in C and of
   * instantiation of implicitly-instantiation function templates in
   * C++. This option is typically used when parsing a header with the
   * intent of producing a precompiled header.
   */
  CXTranslationUnit_Incomplete = 0x02,

  /**
   * Used to indicate that the translation unit should be built with an
   * implicit precompiled header for the preamble.
   *
   * An implicit precompiled header is used as an optimization when a
   * particular translation unit is likely to be reparsed many times
   * when the sources aren't changing that often. In this case, an
   * implicit precompiled header will be built containing all of the
   * initial includes at the top of the main file (what we refer to as
   * the "preamble" of the file). In subsequent parses, if the
   * preamble or the files in it have not changed, \c
   * clang_reparseTranslationUnit() will re-use the implicit
   * precompiled header to improve parsing performance.
   */
  CXTranslationUnit_PrecompiledPreamble = 0x04,

  /**
   * Used to indicate that the translation unit should cache some
   * code-completion results with each reparse of the source file.
   *
   * Caching of code-completion results is a performance optimization that
   * introduces some overhead to reparsing but improves the performance of
   * code-completion operations.
   */
  CXTranslationUnit_CacheCompletionResults = 0x08,

  /**
   * Used to indicate that the translation unit will be serialized with
   * \c clang_saveTranslationUnit.
   *
   * This option is typically used when parsing a header with the intent of
   * producing a precompiled header.
   */
  CXTranslationUnit_ForSerialization = 0x10,

  /**
   * DEPRECATED: Enabled chained precompiled preambles in C++.
   *
   * Note: this is a *temporary* option that is available only while
   * we are testing C++ precompiled preamble support. It is deprecated.
   */
  CXTranslationUnit_CXXChainedPCH = 0x20,

  /**
   * Used to indicate that function/method bodies should be skipped while
   * parsing.
   *
   * This option can be used to search for declarations/definitions while
   * ignoring the usages.
   */
  CXTranslationUnit_SkipFunctionBodies = 0x40,

  /**
   * Used to indicate that brief documentation comments should be
   * included into the set of code completions returned from this translation
   * unit.
   */
  CXTranslationUnit_IncludeBriefCommentsInCodeCompletion = 0x80,

  /**
   * Used to indicate that the precompiled preamble should be created on
   * the first parse. Otherwise it will be created on the first reparse. This
   * trades runtime on the first parse (serializing the preamble takes time) for
   * reduced runtime on the second parse (can now reuse the preamble).
   */
  CXTranslationUnit_CreatePreambleOnFirstParse = 0x100,

  /**
   * Do not stop processing when fatal errors are encountered.
   *
   * When fatal errors are encountered while parsing a translation unit,
   * semantic analysis is typically stopped early when compiling code. A common
   * source for fatal errors are unresolvable include files. For the
   * purposes of an IDE, this is undesirable behavior and as much information
   * as possible should be reported. Use this flag to enable this behavior.
   */
  CXTranslationUnit_KeepGoing = 0x200,

  /**
   * Sets the preprocessor in a mode for parsing a single file only.
   */
  CXTranslationUnit_SingleFileParse = 0x400,

  /**
   * Used in combination with CXTranslationUnit_SkipFunctionBodies to
   * constrain the skipping of function bodies to the preamble.
   *
   * The function bodies of the main file are not skipped.
   */
  CXTranslationUnit_LimitSkipFunctionBodiesToPreamble = 0x800,

  /**
   * Used to indicate that attributed types should be included in CXType.
   */
  CXTranslationUnit_IncludeAttributedTypes = 0x1000,

  /**
   * Used to indicate that implicit attributes should be visited.
   */
  CXTranslationUnit_VisitImplicitAttributes = 0x2000,

  /**
   * Used to indicate that non-errors from included files should be ignored.
   *
   * If set, clang_getDiagnosticSetFromTU() will not report e.g. warnings from
   * included files anymore. This speeds up clang_getDiagnosticSetFromTU() for
   * the case where these warnings are not of interest, as for an IDE for
   * example, which typically shows only the diagnostics in the main file.
   */
  CXTranslationUnit_IgnoreNonErrorsFromIncludedFiles = 0x4000,

  /**
   * Tells the preprocessor not to skip excluded conditional blocks.
   */
  CXTranslationUnit_RetainExcludedConditionalBlocks = 0x8000
};

/**
 * Returns the set of flags that is suitable for parsing a translation
 * unit that is being edited.
 *
 * The set of flags returned provide options for \c clang_parseTranslationUnit()
 * to indicate that the translation unit is likely to be reparsed many times,
 * either explicitly (via \c clang_reparseTranslationUnit()) or implicitly
 * (e.g., by code completion (\c clang_codeCompletionAt())). The returned flag
 * set contains an unspecified set of optimizations (e.g., the precompiled
 * preamble) geared toward improving the performance of these routines. The
 * set of optimizations enabled may change from one version to the next.
 */
CINDEX_LINKAGE unsigned clang_defaultEditingTranslationUnitOptions(void);

/**
 * Same as \c clang_parseTranslationUnit2, but returns
 * the \c CXTranslationUnit instead of an error code.  In case of an error this
 * routine returns a \c NULL \c CXTranslationUnit, without further detailed
 * error codes.
 */
CINDEX_LINKAGE CXTranslationUnit clang_parseTranslationUnit(
    CXIndex CIdx, const char *source_filename,
    const char *const *command_line_args, int num_command_line_args,
    struct CXUnsavedFile *unsaved_files, unsigned num_unsaved_files,
    unsigned options);

/**
 * Parse the given source file and the translation unit corresponding
 * to that file.
 *
 * This routine is the main entry point for the Clang C API, providing the
 * ability to parse a source file into a translation unit that can then be
 * queried by other functions in the API. This routine accepts a set of
 * command-line arguments so that the compilation can be configured in the same
 * way that the compiler is configured on the command line.
 *
 * \param CIdx The index object with which the translation unit will be
 * associated.
 *
 * \param source_filename The name of the source file to load, or NULL if the
 * source file is included in \c command_line_args.
 *
 * \param command_line_args The command-line arguments that would be
 * passed to the \c clang executable if it were being invoked out-of-process.
 * These command-line options will be parsed and will affect how the translation
 * unit is parsed. Note that the following options are ignored: '-c',
 * '-emit-ast', '-fsyntax-only' (which is the default), and '-o \<output file>'.
 *
 * \param num_command_line_args The number of command-line arguments in
 * \c command_line_args.
 *
 * \param unsaved_files the files that have not yet been saved to disk
 * but may be required for parsing, including the contents of
 * those files.  The contents and name of these files (as specified by
 * CXUnsavedFile) are copied when necessary, so the client only needs to
 * guarantee their validity until the call to this function returns.
 *
 * \param num_unsaved_files the number of unsaved file entries in \p
 * unsaved_files.
 *
 * \param options A bitmask of options that affects how the translation unit
 * is managed but not its compilation. This should be a bitwise OR of the
 * CXTranslationUnit_XXX flags.
 *
 * \param[out] out_TU A non-NULL pointer to store the created
 * \c CXTranslationUnit, describing the parsed code and containing any
 * diagnostics produced by the compiler.
 *
 * \returns Zero on success, otherwise returns an error code.
 */
CINDEX_LINKAGE enum CXErrorCode clang_parseTranslationUnit2(
    CXIndex CIdx, const char *source_filename,
    const char *const *command_line_args, int num_command_line_args,
    struct CXUnsavedFile *unsaved_files, unsigned num_unsaved_files,
    unsigned options, CXTranslationUnit *out_TU);

/**
 * Same as clang_parseTranslationUnit2 but requires a full command line
 * for \c command_line_args including argv[0]. This is useful if the standard
 * library paths are relative to the binary.
 */
CINDEX_LINKAGE enum CXErrorCode clang_parseTranslationUnit2FullArgv(
    CXIndex CIdx, const char *source_filename,
    const char *const *command_line_args, int num_command_line_args,
    struct CXUnsavedFile *unsaved_files, unsigned num_unsaved_files,
    unsigned options, CXTranslationUnit *out_TU);

/**
 * Flags that control how translation units are saved.
 *
 * The enumerators in this enumeration type are meant to be bitwise
 * ORed together to specify which options should be used when
 * saving the translation unit.
 */
enum CXSaveTranslationUnit_Flags {
  /**
   * Used to indicate that no special saving options are needed.
   */
  CXSaveTranslationUnit_None = 0x0
};

/**
 * Returns the set of flags that is suitable for saving a translation
 * unit.
 *
 * The set of flags returned provide options for
 * \c clang_saveTranslationUnit() by default. The returned flag
 * set contains an unspecified set of options that save translation units with
 * the most commonly-requested data.
 */
CINDEX_LINKAGE unsigned clang_defaultSaveOptions(CXTranslationUnit TU);

/**
 * Describes the kind of error that occurred (if any) in a call to
 * \c clang_saveTranslationUnit().
 */
enum CXSaveError {
  /**
   * Indicates that no error occurred while saving a translation unit.
   */
  CXSaveError_None = 0,

  /**
   * Indicates that an unknown error occurred while attempting to save
   * the file.
   *
   * This error typically indicates that file I/O failed when attempting to
   * write the file.
   */
  CXSaveError_Unknown = 1,

  /**
   * Indicates that errors during translation prevented this attempt
   * to save the translation unit.
   *
   * Errors that prevent the translation unit from being saved can be
   * extracted using \c clang_getNumDiagnostics() and \c clang_getDiagnostic().
   */
  CXSaveError_TranslationErrors = 2,

  /**
   * Indicates that the translation unit to be saved was somehow
   * invalid (e.g., NULL).
   */
  CXSaveError_InvalidTU = 3
};

/**
 * Saves a translation unit into a serialized representation of
 * that translation unit on disk.
 *
 * Any translation unit that was parsed without error can be saved
 * into a file. The translation unit can then be deserialized into a
 * new \c CXTranslationUnit with \c clang_createTranslationUnit() or,
 * if it is an incomplete translation unit that corresponds to a
 * header, used as a precompiled header when parsing other translation
 * units.
 *
 * \param TU The translation unit to save.
 *
 * \param FileName The file to which the translation unit will be saved.
 *
 * \param options A bitmask of options that affects how the translation unit
 * is saved. This should be a bitwise OR of the
 * CXSaveTranslationUnit_XXX flags.
 *
 * \returns A value that will match one of the enumerators of the CXSaveError
 * enumeration. Zero (CXSaveError_None) indicates that the translation unit was
 * saved successfully, while a non-zero value indicates that a problem occurred.
 */
CINDEX_LINKAGE int clang_saveTranslationUnit(CXTranslationUnit TU,
                                             const char *FileName,
                                             unsigned options);

/**
 * Suspend a translation unit in order to free memory associated with it.
 *
 * A suspended translation unit uses significantly less memory but on the other
 * side does not support any other calls than \c clang_reparseTranslationUnit
 * to resume it or \c clang_disposeTranslationUnit to dispose it completely.
 */
CINDEX_LINKAGE unsigned clang_suspendTranslationUnit(CXTranslationUnit);

/**
 * Destroy the specified CXTranslationUnit object.
 */
CINDEX_LINKAGE void clang_disposeTranslationUnit(CXTranslationUnit);

/**
 * Flags that control the reparsing of translation units.
 *
 * The enumerators in this enumeration type are meant to be bitwise
 * ORed together to specify which options should be used when
 * reparsing the translation unit.
 */
enum CXReparse_Flags {
  /**
   * Used to indicate that no special reparsing options are needed.
   */
  CXReparse_None = 0x0
};

/**
 * Returns the set of flags that is suitable for reparsing a translation
 * unit.
 *
 * The set of flags returned provide options for
 * \c clang_reparseTranslationUnit() by default. The returned flag
 * set contains an unspecified set of optimizations geared toward common uses
 * of reparsing. The set of optimizations enabled may change from one version
 * to the next.
 */
CINDEX_LINKAGE unsigned clang_defaultReparseOptions(CXTranslationUnit TU);

/**
 * Reparse the source files that produced this translation unit.
 *
 * This routine can be used to re-parse the source files that originally
 * created the given translation unit, for example because those source files
 * have changed (either on disk or as passed via \p unsaved_files). The
 * source code will be reparsed with the same command-line options as it
 * was originally parsed.
 *
 * Reparsing a translation unit invalidates all cursors and source locations
 * that refer into that translation unit. This makes reparsing a translation
 * unit semantically equivalent to destroying the translation unit and then
 * creating a new translation unit with the same command-line arguments.
 * However, it may be more efficient to reparse a translation
 * unit using this routine.
 *
 * \param TU The translation unit whose contents will be re-parsed. The
 * translation unit must originally have been built with
 * \c clang_createTranslationUnitFromSourceFile().
 *
 * \param num_unsaved_files The number of unsaved file entries in \p
 * unsaved_files.
 *
 * \param unsaved_files The files that have not yet been saved to disk
 * but may be required for parsing, including the contents of
 * those files.  The contents and name of these files (as specified by
 * CXUnsavedFile) are copied when necessary, so the client only needs to
 * guarantee their validity until the call to this function returns.
 *
 * \param options A bitset of options composed of the flags in CXReparse_Flags.
 * The function \c clang_defaultReparseOptions() produces a default set of
 * options recommended for most uses, based on the translation unit.
 *
 * \returns 0 if the sources could be reparsed.  A non-zero error code will be
 * returned if reparsing was impossible, such that the translation unit is
 * invalid. In such cases, the only valid call for \c TU is
 * \c clang_disposeTranslationUnit(TU).  The error codes returned by this
 * routine are described by the \c CXErrorCode enum.
 */
CINDEX_LINKAGE int
clang_reparseTranslationUnit(CXTranslationUnit TU, unsigned num_unsaved_files,
                             struct CXUnsavedFile *unsaved_files,
                             unsigned options);

/**
 * Categorizes how memory is being used by a translation unit.
 */
enum CXTUResourceUsageKind {
  CXTUResourceUsage_AST = 1,
  CXTUResourceUsage_Identifiers = 2,
  CXTUResourceUsage_Selectors = 3,
  CXTUResourceUsage_GlobalCompletionResults = 4,
  CXTUResourceUsage_SourceManagerContentCache = 5,
  CXTUResourceUsage_AST_SideTables = 6,
  CXTUResourceUsage_SourceManager_Membuffer_Malloc = 7,
  CXTUResourceUsage_SourceManager_Membuffer_MMap = 8,
  CXTUResourceUsage_ExternalASTSource_Membuffer_Malloc = 9,
  CXTUResourceUsage_ExternalASTSource_Membuffer_MMap = 10,
  CXTUResourceUsage_Preprocessor = 11,
  CXTUResourceUsage_PreprocessingRecord = 12,
  CXTUResourceUsage_SourceManager_DataStructures = 13,
  CXTUResourceUsage_Preprocessor_HeaderSearch = 14,
  CXTUResourceUsage_MEMORY_IN_BYTES_BEGIN = CXTUResourceUsage_AST,
  CXTUResourceUsage_MEMORY_IN_BYTES_END =
      CXTUResourceUsage_Preprocessor_HeaderSearch,

  CXTUResourceUsage_First = CXTUResourceUsage_AST,
  CXTUResourceUsage_Last = CXTUResourceUsage_Preprocessor_HeaderSearch
};

/**
 * Returns the human-readable null-terminated C string that represents
 *  the name of the memory category.  This string should never be freed.
 */
CINDEX_LINKAGE
const char *clang_getTUResourceUsageName(enum CXTUResourceUsageKind kind);

typedef struct CXTUResourceUsageEntry {
  /* The memory usage category. */
  enum CXTUResourceUsageKind kind;
  /* Amount of resources used.
      The units will depend on the resource kind. */
  unsigned long amount;
} CXTUResourceUsageEntry;

/**
 * The memory usage of a CXTranslationUnit, broken into categories.
 */
typedef struct CXTUResourceUsage {
  /* Private data member, used for queries. */
  void *data;

  /* The number of entries in the 'entries' array. */
  unsigned numEntries;

  /* An array of key-value pairs, representing the breakdown of memory
            usage. */
  CXTUResourceUsageEntry *entries;

} CXTUResourceUsage;

/**
 * Return the memory usage of a translation unit.  This object
 *  should be released with clang_disposeCXTUResourceUsage().
 */
CINDEX_LINKAGE CXTUResourceUsage
clang_getCXTUResourceUsage(CXTranslationUnit TU);

CINDEX_LINKAGE void clang_disposeCXTUResourceUsage(CXTUResourceUsage usage);

/**
 * Get target information for this translation unit.
 *
 * The CXTargetInfo object cannot outlive the CXTranslationUnit object.
 */
CINDEX_LINKAGE CXTargetInfo
clang_getTranslationUnitTargetInfo(CXTranslationUnit CTUnit);

/**
 * Destroy the CXTargetInfo object.
 */
CINDEX_LINKAGE void clang_TargetInfo_dispose(CXTargetInfo Info);

/**
 * Get the normalized target triple as a string.
 *
 * Returns the empty string in case of any error.
 */
CINDEX_LINKAGE CXString clang_TargetInfo_getTriple(CXTargetInfo Info);

/**
 * Get the pointer width of the target in bits.
 *
 * Returns -1 in case of error.
 */
CINDEX_LINKAGE int clang_TargetInfo_getPointerWidth(CXTargetInfo Info);

/**
 * @}
 */

/**
 * Describes the kind of entity that a cursor refers to.
 */
enum CXCursorKind {
  /* Declarations */
  /**
   * A declaration whose specific kind is not exposed via this
   * interface.
   *
   * Unexposed declarations have the same operations as any other kind
   * of declaration; one can extract their location information,
   * spelling, find their definitions, etc. However, the specific kind
   * of the declaration is not reported.
   */
  CXCursor_UnexposedDecl = 1,
  /** A C or C++ struct. */
  CXCursor_StructDecl = 2,
  /** A C or C++ union. */
  CXCursor_UnionDecl = 3,
  /** A C++ class. */
  CXCursor_ClassDecl = 4,
  /** An enumeration. */
  CXCursor_EnumDecl = 5,
  /**
   * A field (in C) or non-static data member (in C++) in a
   * struct, union, or C++ class.
   */
  CXCursor_FieldDecl = 6,
  /** An enumerator constant. */
  CXCursor_EnumConstantDecl = 7,
  /** A function. */
  CXCursor_FunctionDecl = 8,
  /** A variable. */
  CXCursor_VarDecl = 9,
  /** A function or method parameter. */
  CXCursor_ParmDecl = 10,
  /** An Objective-C \@interface. */
  CXCursor_ObjCInterfaceDecl = 11,
  /** An Objective-C \@interface for a category. */
  CXCursor_ObjCCategoryDecl = 12,
  /** An Objective-C \@protocol declaration. */
  CXCursor_ObjCProtocolDecl = 13,
  /** An Objective-C \@property declaration. */
  CXCursor_ObjCPropertyDecl = 14,
  /** An Objective-C instance variable. */
  CXCursor_ObjCIvarDecl = 15,
  /** An Objective-C instance method. */
  CXCursor_ObjCInstanceMethodDecl = 16,
  /** An Objective-C class method. */
  CXCursor_ObjCClassMethodDecl = 17,
  /** An Objective-C \@implementation. */
  CXCursor_ObjCImplementationDecl = 18,
  /** An Objective-C \@implementation for a category. */
  CXCursor_ObjCCategoryImplDecl = 19,
  /** A typedef. */
  CXCursor_TypedefDecl = 20,
  /** A C++ class method. */
  CXCursor_CXXMethod = 21,
  /** A C++ namespace. */
  CXCursor_Namespace = 22,
  /** A linkage specification, e.g. 'extern "C"'. */
  CXCursor_LinkageSpec = 23,
  /** A C++ constructor. */
  CXCursor_Constructor = 24,
  /** A C++ destructor. */
  CXCursor_Destructor = 25,
  /** A C++ conversion function. */
  CXCursor_ConversionFunction = 26,
  /** A C++ template type parameter. */
  CXCursor_TemplateTypeParameter = 27,
  /** A C++ non-type template parameter. */
  CXCursor_NonTypeTemplateParameter = 28,
  /** A C++ template template parameter. */
  CXCursor_TemplateTemplateParameter = 29,
  /** A C++ function template. */
  CXCursor_FunctionTemplate = 30,
  /** A C++ class template. */
  CXCursor_ClassTemplate = 31,
  /** A C++ class template partial specialization. */
  CXCursor_ClassTemplatePartialSpecialization = 32,
  /** A C++ namespace alias declaration. */
  CXCursor_NamespaceAlias = 33,
  /** A C++ using directive. */
  CXCursor_UsingDirective = 34,
  /** A C++ using declaration. */
  CXCursor_UsingDeclaration = 35,
  /** A C++ alias declaration */
  CXCursor_TypeAliasDecl = 36,
  /** An Objective-C \@synthesize definition. */
  CXCursor_ObjCSynthesizeDecl = 37,
  /** An Objective-C \@dynamic definition. */
  CXCursor_ObjCDynamicDecl = 38,
  /** An access specifier. */
  CXCursor_CXXAccessSpecifier = 39,

  CXCursor_FirstDecl = CXCursor_UnexposedDecl,
  CXCursor_LastDecl = CXCursor_CXXAccessSpecifier,

  /* References */
  CXCursor_FirstRef = 40, /* Decl references */
  CXCursor_ObjCSuperClassRef = 40,
  CXCursor_ObjCProtocolRef = 41,
  CXCursor_ObjCClassRef = 42,
  /**
   * A reference to a type declaration.
   *
   * A type reference occurs anywhere where a type is named but not
   * declared. For example, given:
   *
   * \code
   * typedef unsigned size_type;
   * size_type size;
   * \endcode
   *
   * The typedef is a declaration of size_type (CXCursor_TypedefDecl),
   * while the type of the variable "size" is referenced. The cursor
   * referenced by the type of size is the typedef for size_type.
   */
  CXCursor_TypeRef = 43,
  CXCursor_CXXBaseSpecifier = 44,
  /**
   * A reference to a class template, function template, template
   * template parameter, or class template partial specialization.
   */
  CXCursor_TemplateRef = 45,
  /**
   * A reference to a namespace or namespace alias.
   */
  CXCursor_NamespaceRef = 46,
  /**
   * A reference to a member of a struct, union, or class that occurs in
   * some non-expression context, e.g., a designated initializer.
   */
  CXCursor_MemberRef = 47,
  /**
   * A reference to a labeled statement.
   *
   * This cursor kind is used to describe the jump to "start_over" in the
   * goto statement in the following example:
   *
   * \code
   *   start_over:
   *     ++counter;
   *
   *     goto start_over;
   * \endcode
   *
   * A label reference cursor refers to a label statement.
   */
  CXCursor_LabelRef = 48,

  /**
   * A reference to a set of overloaded functions or function templates
   * that has not yet been resolved to a specific function or function template.
   *
   * An overloaded declaration reference cursor occurs in C++ templates where
   * a dependent name refers to a function. For example:
   *
   * \code
   * template<typename T> void swap(T&, T&);
   *
   * struct X { ... };
   * void swap(X&, X&);
   *
   * template<typename T>
   * void reverse(T* first, T* last) {
   *   while (first < last - 1) {
   *     swap(*first, *--last);
   *     ++first;
   *   }
   * }
   *
   * struct Y { };
   * void swap(Y&, Y&);
   * \endcode
   *
   * Here, the identifier "swap" is associated with an overloaded declaration
   * reference. In the template definition, "swap" refers to either of the two
   * "swap" functions declared above, so both results will be available. At
   * instantiation time, "swap" may also refer to other functions found via
   * argument-dependent lookup (e.g., the "swap" function at the end of the
   * example).
   *
   * The functions \c clang_getNumOverloadedDecls() and
   * \c clang_getOverloadedDecl() can be used to retrieve the definitions
   * referenced by this cursor.
   */
  CXCursor_OverloadedDeclRef = 49,

  /**
   * A reference to a variable that occurs in some non-expression
   * context, e.g., a C++ lambda capture list.
   */
  CXCursor_VariableRef = 50,

  CXCursor_LastRef = CXCursor_VariableRef,

  /* Error conditions */
  CXCursor_FirstInvalid = 70,
  CXCursor_InvalidFile = 70,
  CXCursor_NoDeclFound = 71,
  CXCursor_NotImplemented = 72,
  CXCursor_InvalidCode = 73,
  CXCursor_LastInvalid = CXCursor_InvalidCode,

  /* Expressions */
  CXCursor_FirstExpr = 100,

  /**
   * An expression whose specific kind is not exposed via this
   * interface.
   *
   * Unexposed expressions have the same operations as any other kind
   * of expression; one can extract their location information,
   * spelling, children, etc. However, the specific kind of the
   * expression is not reported.
   */
  CXCursor_UnexposedExpr = 100,

  /**
   * An expression that refers to some value declaration, such
   * as a function, variable, or enumerator.
   */
  CXCursor_DeclRefExpr = 101,

  /**
   * An expression that refers to a member of a struct, union,
   * class, Objective-C class, etc.
   */
  CXCursor_MemberRefExpr = 102,

  /** An expression that calls a function. */
  CXCursor_CallExpr = 103,

  /** An expression that sends a message to an Objective-C
   object or class. */
  CXCursor_ObjCMessageExpr = 104,

  /** An expression that represents a block literal. */
  CXCursor_BlockExpr = 105,

  /** An integer literal.
   */
  CXCursor_IntegerLiteral = 106,

  /** A floating point number literal.
   */
  CXCursor_FloatingLiteral = 107,

  /** An imaginary number literal.
   */
  CXCursor_ImaginaryLiteral = 108,

  /** A string literal.
   */
  CXCursor_StringLiteral = 109,

  /** A character literal.
   */
  CXCursor_CharacterLiteral = 110,

  /** A parenthesized expression, e.g. "(1)".
   *
   * This AST node is only formed if full location information is requested.
   */
  CXCursor_ParenExpr = 111,

  /** This represents the unary-expression's (except sizeof and
   * alignof).
   */
  CXCursor_UnaryOperator = 112,

  /** [C99 6.5.2.1] Array Subscripting.
   */
  CXCursor_ArraySubscriptExpr = 113,

  /** A builtin binary operation expression such as "x + y" or
   * "x <= y".
   */
  CXCursor_BinaryOperator = 114,

  /** Compound assignment such as "+=".
   */
  CXCursor_CompoundAssignOperator = 115,

  /** The ?: ternary operator.
   */
  CXCursor_ConditionalOperator = 116,

  /** An explicit cast in C (C99 6.5.4) or a C-style cast in C++
   * (C++ [expr.cast]), which uses the syntax (Type)expr.
   *
   * For example: (int)f.
   */
  CXCursor_CStyleCastExpr = 117,

  /** [C99 6.5.2.5]
   */
  CXCursor_CompoundLiteralExpr = 118,

  /** Describes an C or C++ initializer list.
   */
  CXCursor_InitListExpr = 119,

  /** The GNU address of label extension, representing &&label.
   */
  CXCursor_AddrLabelExpr = 120,

  /** This is the GNU Statement Expression extension: ({int X=4; X;})
   */
  CXCursor_StmtExpr = 121,

  /** Represents a C11 generic selection.
   */
  CXCursor_GenericSelectionExpr = 122,

  /** Implements the GNU __null extension, which is a name for a null
   * pointer constant that has integral type (e.g., int or long) and is the same
   * size and alignment as a pointer.
   *
   * The __null extension is typically only used by system headers, which define
   * NULL as __null in C++ rather than using 0 (which is an integer that may not
   * match the size of a pointer).
   */
  CXCursor_GNUNullExpr = 123,

  /** C++'s static_cast<> expression.
   */
  CXCursor_CXXStaticCastExpr = 124,

  /** C++'s dynamic_cast<> expression.
   */
  CXCursor_CXXDynamicCastExpr = 125,

  /** C++'s reinterpret_cast<> expression.
   */
  CXCursor_CXXReinterpretCastExpr = 126,

  /** C++'s const_cast<> expression.
   */
  CXCursor_CXXConstCastExpr = 127,

  /** Represents an explicit C++ type conversion that uses "functional"
   * notion (C++ [expr.type.conv]).
   *
   * Example:
   * \code
   *   x = int(0.5);
   * \endcode
   */
  CXCursor_CXXFunctionalCastExpr = 128,

  /** A C++ typeid expression (C++ [expr.typeid]).
   */
  CXCursor_CXXTypeidExpr = 129,

  /** [C++ 2.13.5] C++ Boolean Literal.
   */
  CXCursor_CXXBoolLiteralExpr = 130,

  /** [C++0x 2.14.7] C++ Pointer Literal.
   */
  CXCursor_CXXNullPtrLiteralExpr = 131,

  /** Represents the "this" expression in C++
   */
  CXCursor_CXXThisExpr = 132,

  /** [C++ 15] C++ Throw Expression.
   *
   * This handles 'throw' and 'throw' assignment-expression. When
   * assignment-expression isn't present, Op will be null.
   */
  CXCursor_CXXThrowExpr = 133,

  /** A new expression for memory allocation and constructor calls, e.g:
   * "new CXXNewExpr(foo)".
   */
  CXCursor_CXXNewExpr = 134,

  /** A delete expression for memory deallocation and destructor calls,
   * e.g. "delete[] pArray".
   */
  CXCursor_CXXDeleteExpr = 135,

  /** A unary expression. (noexcept, sizeof, or other traits)
   */
  CXCursor_UnaryExpr = 136,

  /** An Objective-C string literal i.e. @"foo".
   */
  CXCursor_ObjCStringLiteral = 137,

  /** An Objective-C \@encode expression.
   */
  CXCursor_ObjCEncodeExpr = 138,

  /** An Objective-C \@selector expression.
   */
  CXCursor_ObjCSelectorExpr = 139,

  /** An Objective-C \@protocol expression.
   */
  CXCursor_ObjCProtocolExpr = 140,

  /** An Objective-C "bridged" cast expression, which casts between
   * Objective-C pointers and C pointers, transferring ownership in the process.
   *
   * \code
   *   NSString *str = (__bridge_transfer NSString *)CFCreateString();
   * \endcode
   */
  CXCursor_ObjCBridgedCastExpr = 141,

  /** Represents a C++0x pack expansion that produces a sequence of
   * expressions.
   *
   * A pack expansion expression contains a pattern (which itself is an
   * expression) followed by an ellipsis. For example:
   *
   * \code
   * template<typename F, typename ...Types>
   * void forward(F f, Types &&...args) {
   *  f(static_cast<Types&&>(args)...);
   * }
   * \endcode
   */
  CXCursor_PackExpansionExpr = 142,

  /** Represents an expression that computes the length of a parameter
   * pack.
   *
   * \code
   * template<typename ...Types>
   * struct count {
   *   static const unsigned value = sizeof...(Types);
   * };
   * \endcode
   */
  CXCursor_SizeOfPackExpr = 143,

  /* Represents a C++ lambda expression that produces a local function
   * object.
   *
   * \code
   * void abssort(float *x, unsigned N) {
   *   std::sort(x, x + N,
   *             [](float a, float b) {
   *               return std::abs(a) < std::abs(b);
   *             });
   * }
   * \endcode
   */
  CXCursor_LambdaExpr = 144,

  /** Objective-c Boolean Literal.
   */
  CXCursor_ObjCBoolLiteralExpr = 145,

  /** Represents the "self" expression in an Objective-C method.
   */
  CXCursor_ObjCSelfExpr = 146,

  /** OpenMP 5.0 [2.1.5, Array Section].
   * OpenACC 3.3 [2.7.1, Data Specification for Data Clauses (Sub Arrays)]
   */
  CXCursor_ArraySectionExpr = 147,

  /** Represents an @available(...) check.
   */
  CXCursor_ObjCAvailabilityCheckExpr = 148,

  /**
   * Fixed point literal
   */
  CXCursor_FixedPointLiteral = 149,

  /** OpenMP 5.0 [2.1.4, Array Shaping].
   */
  CXCursor_OMPArrayShapingExpr = 150,

  /**
   * OpenMP 5.0 [2.1.6 Iterators]
   */
  CXCursor_OMPIteratorExpr = 151,

  /** OpenCL's addrspace_cast<> expression.
   */
  CXCursor_CXXAddrspaceCastExpr = 152,

  /**
   * Expression that references a C++20 concept.
   */
  CXCursor_ConceptSpecializationExpr = 153,

  /**
   * Expression that references a C++20 requires expression.
   */
  CXCursor_RequiresExpr = 154,

  /**
   * Expression that references a C++20 parenthesized list aggregate
   * initializer.
   */
  CXCursor_CXXParenListInitExpr = 155,

  /**
   *  Represents a C++26 pack indexing expression.
   */
  CXCursor_PackIndexingExpr = 156,

  CXCursor_LastExpr = CXCursor_PackIndexingExpr,

  /* Statements */
  CXCursor_FirstStmt = 200,
  /**
   * A statement whose specific kind is not exposed via this
   * interface.
   *
   * Unexposed statements have the same operations as any other kind of
   * statement; one can extract their location information, spelling,
   * children, etc. However, the specific kind of the statement is not
   * reported.
   */
  CXCursor_UnexposedStmt = 200,

  /** A labelled statement in a function.
   *
   * This cursor kind is used to describe the "start_over:" label statement in
   * the following example:
   *
   * \code
   *   start_over:
   *     ++counter;
   * \endcode
   *
   */
  CXCursor_LabelStmt = 201,

  /** A group of statements like { stmt stmt }.
   *
   * This cursor kind is used to describe compound statements, e.g. function
   * bodies.
   */
  CXCursor_CompoundStmt = 202,

  /** A case statement.
   */
  CXCursor_CaseStmt = 203,

  /** A default statement.
   */
  CXCursor_DefaultStmt = 204,

  /** An if statement
   */
  CXCursor_IfStmt = 205,

  /** A switch statement.
   */
  CXCursor_SwitchStmt = 206,

  /** A while statement.
   */
  CXCursor_WhileStmt = 207,

  /** A do statement.
   */
  CXCursor_DoStmt = 208,

  /** A for statement.
   */
  CXCursor_ForStmt = 209,

  /** A goto statement.
   */
  CXCursor_GotoStmt = 210,

  /** An indirect goto statement.
   */
  CXCursor_IndirectGotoStmt = 211,

  /** A continue statement.
   */
  CXCursor_ContinueStmt = 212,

  /** A break statement.
   */
  CXCursor_BreakStmt = 213,

  /** A return statement.
   */
  CXCursor_ReturnStmt = 214,

  /** A GCC inline assembly statement extension.
   */
  CXCursor_GCCAsmStmt = 215,
  CXCursor_AsmStmt = CXCursor_GCCAsmStmt,

  /** Objective-C's overall \@try-\@catch-\@finally statement.
   */
  CXCursor_ObjCAtTryStmt = 216,

  /** Objective-C's \@catch statement.
   */
  CXCursor_ObjCAtCatchStmt = 217,

  /** Objective-C's \@finally statement.
   */
  CXCursor_ObjCAtFinallyStmt = 218,

  /** Objective-C's \@throw statement.
   */
  CXCursor_ObjCAtThrowStmt = 219,

  /** Objective-C's \@synchronized statement.
   */
  CXCursor_ObjCAtSynchronizedStmt = 220,

  /** Objective-C's autorelease pool statement.
   */
  CXCursor_ObjCAutoreleasePoolStmt = 221,

  /** Objective-C's collection statement.
   */
  CXCursor_ObjCForCollectionStmt = 222,

  /** C++'s catch statement.
   */
  CXCursor_CXXCatchStmt = 223,

  /** C++'s try statement.
   */
  CXCursor_CXXTryStmt = 224,

  /** C++'s for (* : *) statement.
   */
  CXCursor_CXXForRangeStmt = 225,

  /** Windows Structured Exception Handling's try statement.
   */
  CXCursor_SEHTryStmt = 226,

  /** Windows Structured Exception Handling's except statement.
   */
  CXCursor_SEHExceptStmt = 227,

  /** Windows Structured Exception Handling's finally statement.
   */
  CXCursor_SEHFinallyStmt = 228,

  /** A MS inline assembly statement extension.
   */
  CXCursor_MSAsmStmt = 229,

  /** The null statement ";": C99 6.8.3p3.
   *
   * This cursor kind is used to describe the null statement.
   */
  CXCursor_NullStmt = 230,

  /** Adaptor class for mixing declarations with statements and
   * expressions.
   */
  CXCursor_DeclStmt = 231,

  /** OpenMP parallel directive.
   */
  CXCursor_OMPParallelDirective = 232,

  /** OpenMP SIMD directive.
   */
  CXCursor_OMPSimdDirective = 233,

  /** OpenMP for directive.
   */
  CXCursor_OMPForDirective = 234,

  /** OpenMP sections directive.
   */
  CXCursor_OMPSectionsDirective = 235,

  /** OpenMP section directive.
   */
  CXCursor_OMPSectionDirective = 236,

  /** OpenMP single directive.
   */
  CXCursor_OMPSingleDirective = 237,

  /** OpenMP parallel for directive.
   */
  CXCursor_OMPParallelForDirective = 238,

  /** OpenMP parallel sections directive.
   */
  CXCursor_OMPParallelSectionsDirective = 239,

  /** OpenMP task directive.
   */
  CXCursor_OMPTaskDirective = 240,

  /** OpenMP master directive.
   */
  CXCursor_OMPMasterDirective = 241,

  /** OpenMP critical directive.
   */
  CXCursor_OMPCriticalDirective = 242,

  /** OpenMP taskyield directive.
   */
  CXCursor_OMPTaskyieldDirective = 243,

  /** OpenMP barrier directive.
   */
  CXCursor_OMPBarrierDirective = 244,

  /** OpenMP taskwait directive.
   */
  CXCursor_OMPTaskwaitDirective = 245,

  /** OpenMP flush directive.
   */
  CXCursor_OMPFlushDirective = 246,

  /** Windows Structured Exception Handling's leave statement.
   */
  CXCursor_SEHLeaveStmt = 247,

  /** OpenMP ordered directive.
   */
  CXCursor_OMPOrderedDirective = 248,

  /** OpenMP atomic directive.
   */
  CXCursor_OMPAtomicDirective = 249,

  /** OpenMP for SIMD directive.
   */
  CXCursor_OMPForSimdDirective = 250,

  /** OpenMP parallel for SIMD directive.
   */
  CXCursor_OMPParallelForSimdDirective = 251,

  /** OpenMP target directive.
   */
  CXCursor_OMPTargetDirective = 252,

  /** OpenMP teams directive.
   */
  CXCursor_OMPTeamsDirective = 253,

  /** OpenMP taskgroup directive.
   */
  CXCursor_OMPTaskgroupDirective = 254,

  /** OpenMP cancellation point directive.
   */
  CXCursor_OMPCancellationPointDirective = 255,

  /** OpenMP cancel directive.
   */
  CXCursor_OMPCancelDirective = 256,

  /** OpenMP target data directive.
   */
  CXCursor_OMPTargetDataDirective = 257,

  /** OpenMP taskloop directive.
   */
  CXCursor_OMPTaskLoopDirective = 258,

  /** OpenMP taskloop simd directive.
   */
  CXCursor_OMPTaskLoopSimdDirective = 259,

  /** OpenMP distribute directive.
   */
  CXCursor_OMPDistributeDirective = 260,

  /** OpenMP target enter data directive.
   */
  CXCursor_OMPTargetEnterDataDirective = 261,

  /** OpenMP target exit data directive.
   */
  CXCursor_OMPTargetExitDataDirective = 262,

  /** OpenMP target parallel directive.
   */
  CXCursor_OMPTargetParallelDirective = 263,

  /** OpenMP target parallel for directive.
   */
  CXCursor_OMPTargetParallelForDirective = 264,

  /** OpenMP target update directive.
   */
  CXCursor_OMPTargetUpdateDirective = 265,

  /** OpenMP distribute parallel for directive.
   */
  CXCursor_OMPDistributeParallelForDirective = 266,

  /** OpenMP distribute parallel for simd directive.
   */
  CXCursor_OMPDistributeParallelForSimdDirective = 267,

  /** OpenMP distribute simd directive.
   */
  CXCursor_OMPDistributeSimdDirective = 268,

  /** OpenMP target parallel for simd directive.
   */
  CXCursor_OMPTargetParallelForSimdDirective = 269,

  /** OpenMP target simd directive.
   */
  CXCursor_OMPTargetSimdDirective = 270,

  /** OpenMP teams distribute directive.
   */
  CXCursor_OMPTeamsDistributeDirective = 271,

  /** OpenMP teams distribute simd directive.
   */
  CXCursor_OMPTeamsDistributeSimdDirective = 272,

  /** OpenMP teams distribute parallel for simd directive.
   */
  CXCursor_OMPTeamsDistributeParallelForSimdDirective = 273,

  /** OpenMP teams distribute parallel for directive.
   */
  CXCursor_OMPTeamsDistributeParallelForDirective = 274,

  /** OpenMP target teams directive.
   */
  CXCursor_OMPTargetTeamsDirective = 275,

  /** OpenMP target teams distribute directive.
   */
  CXCursor_OMPTargetTeamsDistributeDirective = 276,

  /** OpenMP target teams distribute parallel for directive.
   */
  CXCursor_OMPTargetTeamsDistributeParallelForDirective = 277,

  /** OpenMP target teams distribute parallel for simd directive.
   */
  CXCursor_OMPTargetTeamsDistributeParallelForSimdDirective = 278,

  /** OpenMP target teams distribute simd directive.
   */
  CXCursor_OMPTargetTeamsDistributeSimdDirective = 279,

  /** C++2a std::bit_cast expression.
   */
  CXCursor_BuiltinBitCastExpr = 280,

  /** OpenMP master taskloop directive.
   */
  CXCursor_OMPMasterTaskLoopDirective = 281,

  /** OpenMP parallel master taskloop directive.
   */
  CXCursor_OMPParallelMasterTaskLoopDirective = 282,

  /** OpenMP master taskloop simd directive.
   */
  CXCursor_OMPMasterTaskLoopSimdDirective = 283,

  /** OpenMP parallel master taskloop simd directive.
   */
  CXCursor_OMPParallelMasterTaskLoopSimdDirective = 284,

  /** OpenMP parallel master directive.
   */
  CXCursor_OMPParallelMasterDirective = 285,

  /** OpenMP depobj directive.
   */
  CXCursor_OMPDepobjDirective = 286,

  /** OpenMP scan directive.
   */
  CXCursor_OMPScanDirective = 287,

  /** OpenMP tile directive.
   */
  CXCursor_OMPTileDirective = 288,

  /** OpenMP canonical loop.
   */
  CXCursor_OMPCanonicalLoop = 289,

  /** OpenMP interop directive.
   */
  CXCursor_OMPInteropDirective = 290,

  /** OpenMP dispatch directive.
   */
  CXCursor_OMPDispatchDirective = 291,

  /** OpenMP masked directive.
   */
  CXCursor_OMPMaskedDirective = 292,

  /** OpenMP unroll directive.
   */
  CXCursor_OMPUnrollDirective = 293,

  /** OpenMP metadirective directive.
   */
  CXCursor_OMPMetaDirective = 294,

  /** OpenMP loop directive.
   */
  CXCursor_OMPGenericLoopDirective = 295,

  /** OpenMP teams loop directive.
   */
  CXCursor_OMPTeamsGenericLoopDirective = 296,

  /** OpenMP target teams loop directive.
   */
  CXCursor_OMPTargetTeamsGenericLoopDirective = 297,

  /** OpenMP parallel loop directive.
   */
  CXCursor_OMPParallelGenericLoopDirective = 298,

  /** OpenMP target parallel loop directive.
   */
  CXCursor_OMPTargetParallelGenericLoopDirective = 299,

  /** OpenMP parallel masked directive.
   */
  CXCursor_OMPParallelMaskedDirective = 300,

  /** OpenMP masked taskloop directive.
   */
  CXCursor_OMPMaskedTaskLoopDirective = 301,

  /** OpenMP masked taskloop simd directive.
   */
  CXCursor_OMPMaskedTaskLoopSimdDirective = 302,

  /** OpenMP parallel masked taskloop directive.
   */
  CXCursor_OMPParallelMaskedTaskLoopDirective = 303,

  /** OpenMP parallel masked taskloop simd directive.
   */
  CXCursor_OMPParallelMaskedTaskLoopSimdDirective = 304,

  /** OpenMP error directive.
   */
  CXCursor_OMPErrorDirective = 305,

  /** OpenMP scope directive.
   */
  CXCursor_OMPScopeDirective = 306,

  /** OpenMP reverse directive.
   */
  CXCursor_OMPReverseDirective = 307,

  /** OpenMP interchange directive.
   */
  CXCursor_OMPInterchangeDirective = 308,

  /** OpenACC Compute Construct.
   */
  CXCursor_OpenACCComputeConstruct = 320,

  /** OpenACC Loop Construct.
   */
  CXCursor_OpenACCLoopConstruct = 321,

  CXCursor_LastStmt = CXCursor_OpenACCLoopConstruct,

  /**
   * Cursor that represents the translation unit itself.
   *
   * The translation unit cursor exists primarily to act as the root
   * cursor for traversing the contents of a translation unit.
   */
  CXCursor_TranslationUnit = 350,

  /* Attributes */
  CXCursor_FirstAttr = 400,
  /**
   * An attribute whose specific kind is not exposed via this
   * interface.
   */
  CXCursor_UnexposedAttr = 400,

  CXCursor_IBActionAttr = 401,
  CXCursor_IBOutletAttr = 402,
  CXCursor_IBOutletCollectionAttr = 403,
  CXCursor_CXXFinalAttr = 404,
  CXCursor_CXXOverrideAttr = 405,
  CXCursor_AnnotateAttr = 406,
  CXCursor_AsmLabelAttr = 407,
  CXCursor_PackedAttr = 408,
  CXCursor_PureAttr = 409,
  CXCursor_ConstAttr = 410,
  CXCursor_NoDuplicateAttr = 411,
  CXCursor_CUDAConstantAttr = 412,
  CXCursor_CUDADeviceAttr = 413,
  CXCursor_CUDAGlobalAttr = 414,
  CXCursor_CUDAHostAttr = 415,
  CXCursor_CUDASharedAttr = 416,
  CXCursor_VisibilityAttr = 417,
  CXCursor_DLLExport = 418,
  CXCursor_DLLImport = 419,
  CXCursor_NSReturnsRetained = 420,
  CXCursor_NSReturnsNotRetained = 421,
  CXCursor_NSReturnsAutoreleased = 422,
  CXCursor_NSConsumesSelf = 423,
  CXCursor_NSConsumed = 424,
  CXCursor_ObjCException = 425,
  CXCursor_ObjCNSObject = 426,
  CXCursor_ObjCIndependentClass = 427,
  CXCursor_ObjCPreciseLifetime = 428,
  CXCursor_ObjCReturnsInnerPointer = 429,
  CXCursor_ObjCRequiresSuper = 430,
  CXCursor_ObjCRootClass = 431,
  CXCursor_ObjCSubclassingRestricted = 432,
  CXCursor_ObjCExplicitProtocolImpl = 433,
  CXCursor_ObjCDesignatedInitializer = 434,
  CXCursor_ObjCRuntimeVisible = 435,
  CXCursor_ObjCBoxable = 436,
  CXCursor_FlagEnum = 437,
  CXCursor_ConvergentAttr = 438,
  CXCursor_WarnUnusedAttr = 439,
  CXCursor_WarnUnusedResultAttr = 440,
  CXCursor_AlignedAttr = 441,
  CXCursor_LastAttr = CXCursor_AlignedAttr,

  /* Preprocessing */
  CXCursor_PreprocessingDirective = 500,
  CXCursor_MacroDefinition = 501,
  CXCursor_MacroExpansion = 502,
  CXCursor_MacroInstantiation = CXCursor_MacroExpansion,
  CXCursor_InclusionDirective = 503,
  CXCursor_FirstPreprocessing = CXCursor_PreprocessingDirective,
  CXCursor_LastPreprocessing = CXCursor_InclusionDirective,

  /* Extra Declarations */
  /**
   * A module import declaration.
   */
  CXCursor_ModuleImportDecl = 600,
  CXCursor_TypeAliasTemplateDecl = 601,
  /**
   * A static_assert or _Static_assert node
   */
  CXCursor_StaticAssert = 602,
  /**
   * a friend declaration.
   */
  CXCursor_FriendDecl = 603,
  /**
   * a concept declaration.
   */
  CXCursor_ConceptDecl = 604,

  CXCursor_FirstExtraDecl = CXCursor_ModuleImportDecl,
  CXCursor_LastExtraDecl = CXCursor_ConceptDecl,

  /**
   * A code completion overload candidate.
   */
  CXCursor_OverloadCandidate = 700
};

/**
 * A cursor representing some element in the abstract syntax tree for
 * a translation unit.
 *
 * The cursor abstraction unifies the different kinds of entities in a
 * program--declaration, statements, expressions, references to declarations,
 * etc.--under a single "cursor" abstraction with a common set of operations.
 * Common operation for a cursor include: getting the physical location in
 * a source file where the cursor points, getting the name associated with a
 * cursor, and retrieving cursors for any child nodes of a particular cursor.
 *
 * Cursors can be produced in two specific ways.
 * clang_getTranslationUnitCursor() produces a cursor for a translation unit,
 * from which one can use clang_visitChildren() to explore the rest of the
 * translation unit. clang_getCursor() maps from a physical source location
 * to the entity that resides at that location, allowing one to map from the
 * source code into the AST.
 */
typedef struct {
  enum CXCursorKind kind;
  int xdata;
  const void *data[3];
} CXCursor;

/**
 * \defgroup CINDEX_CURSOR_MANIP Cursor manipulations
 *
 * @{
 */

/**
 * Retrieve the NULL cursor, which represents no entity.
 */
CINDEX_LINKAGE CXCursor clang_getNullCursor(void);

/**
 * Retrieve the cursor that represents the given translation unit.
 *
 * The translation unit cursor can be used to start traversing the
 * various declarations within the given translation unit.
 */
CINDEX_LINKAGE CXCursor clang_getTranslationUnitCursor(CXTranslationUnit);

/**
 * Determine whether two cursors are equivalent.
 */
CINDEX_LINKAGE unsigned clang_equalCursors(CXCursor, CXCursor);

/**
 * Returns non-zero if \p cursor is null.
 */
CINDEX_LINKAGE int clang_Cursor_isNull(CXCursor cursor);

/**
 * Compute a hash value for the given cursor.
 */
CINDEX_LINKAGE unsigned clang_hashCursor(CXCursor);

/**
 * Retrieve the kind of the given cursor.
 */
CINDEX_LINKAGE enum CXCursorKind clang_getCursorKind(CXCursor);

/**
 * Determine whether the given cursor kind represents a declaration.
 */
CINDEX_LINKAGE unsigned clang_isDeclaration(enum CXCursorKind);

/**
 * Determine whether the given declaration is invalid.
 *
 * A declaration is invalid if it could not be parsed successfully.
 *
 * \returns non-zero if the cursor represents a declaration and it is
 * invalid, otherwise NULL.
 */
CINDEX_LINKAGE unsigned clang_isInvalidDeclaration(CXCursor);

/**
 * Determine whether the given cursor kind represents a simple
 * reference.
 *
 * Note that other kinds of cursors (such as expressions) can also refer to
 * other cursors. Use clang_getCursorReferenced() to determine whether a
 * particular cursor refers to another entity.
 */
CINDEX_LINKAGE unsigned clang_isReference(enum CXCursorKind);

/**
 * Determine whether the given cursor kind represents an expression.
 */
CINDEX_LINKAGE unsigned clang_isExpression(enum CXCursorKind);

/**
 * Determine whether the given cursor kind represents a statement.
 */
CINDEX_LINKAGE unsigned clang_isStatement(enum CXCursorKind);

/**
 * Determine whether the given cursor kind represents an attribute.
 */
CINDEX_LINKAGE unsigned clang_isAttribute(enum CXCursorKind);

/**
 * Determine whether the given cursor has any attributes.
 */
CINDEX_LINKAGE unsigned clang_Cursor_hasAttrs(CXCursor C);

/**
 * Determine whether the given cursor kind represents an invalid
 * cursor.
 */
CINDEX_LINKAGE unsigned clang_isInvalid(enum CXCursorKind);

/**
 * Determine whether the given cursor kind represents a translation
 * unit.
 */
CINDEX_LINKAGE unsigned clang_isTranslationUnit(enum CXCursorKind);

/***
 * Determine whether the given cursor represents a preprocessing
 * element, such as a preprocessor directive or macro instantiation.
 */
CINDEX_LINKAGE unsigned clang_isPreprocessing(enum CXCursorKind);

/***
 * Determine whether the given cursor represents a currently
 *  unexposed piece of the AST (e.g., CXCursor_UnexposedStmt).
 */
CINDEX_LINKAGE unsigned clang_isUnexposed(enum CXCursorKind);

/**
 * Describe the linkage of the entity referred to by a cursor.
 */
enum CXLinkageKind {
  /** This value indicates that no linkage information is available
   * for a provided CXCursor. */
  CXLinkage_Invalid,
  /**
   * This is the linkage for variables, parameters, and so on that
   *  have automatic storage.  This covers normal (non-extern) local variables.
   */
  CXLinkage_NoLinkage,
  /** This is the linkage for static variables and static functions. */
  CXLinkage_Internal,
  /** This is the linkage for entities with external linkage that live
   * in C++ anonymous namespaces.*/
  CXLinkage_UniqueExternal,
  /** This is the linkage for entities with true, external linkage. */
  CXLinkage_External
};

/**
 * Determine the linkage of the entity referred to by a given cursor.
 */
CINDEX_LINKAGE enum CXLinkageKind clang_getCursorLinkage(CXCursor cursor);

enum CXVisibilityKind {
  /** This value indicates that no visibility information is available
   * for a provided CXCursor. */
  CXVisibility_Invalid,

  /** Symbol not seen by the linker. */
  CXVisibility_Hidden,
  /** Symbol seen by the linker but resolves to a symbol inside this object. */
  CXVisibility_Protected,
  /** Symbol seen by the linker and acts like a normal symbol. */
  CXVisibility_Default
};

/**
 * Describe the visibility of the entity referred to by a cursor.
 *
 * This returns the default visibility if not explicitly specified by
 * a visibility attribute. The default visibility may be changed by
 * commandline arguments.
 *
 * \param cursor The cursor to query.
 *
 * \returns The visibility of the cursor.
 */
CINDEX_LINKAGE enum CXVisibilityKind clang_getCursorVisibility(CXCursor cursor);

/**
 * Determine the availability of the entity that this cursor refers to,
 * taking the current target platform into account.
 *
 * \param cursor The cursor to query.
 *
 * \returns The availability of the cursor.
 */
CINDEX_LINKAGE enum CXAvailabilityKind
clang_getCursorAvailability(CXCursor cursor);

/**
 * Describes the availability of a given entity on a particular platform, e.g.,
 * a particular class might only be available on Mac OS 10.7 or newer.
 */
typedef struct CXPlatformAvailability {
  /**
   * A string that describes the platform for which this structure
   * provides availability information.
   *
   * Possible values are "ios" or "macos".
   */
  CXString Platform;
  /**
   * The version number in which this entity was introduced.
   */
  CXVersion Introduced;
  /**
   * The version number in which this entity was deprecated (but is
   * still available).
   */
  CXVersion Deprecated;
  /**
   * The version number in which this entity was obsoleted, and therefore
   * is no longer available.
   */
  CXVersion Obsoleted;
  /**
   * Whether the entity is unconditionally unavailable on this platform.
   */
  int Unavailable;
  /**
   * An optional message to provide to a user of this API, e.g., to
   * suggest replacement APIs.
   */
  CXString Message;
} CXPlatformAvailability;

/**
 * Determine the availability of the entity that this cursor refers to
 * on any platforms for which availability information is known.
 *
 * \param cursor The cursor to query.
 *
 * \param always_deprecated If non-NULL, will be set to indicate whether the
 * entity is deprecated on all platforms.
 *
 * \param deprecated_message If non-NULL, will be set to the message text
 * provided along with the unconditional deprecation of this entity. The client
 * is responsible for deallocating this string.
 *
 * \param always_unavailable If non-NULL, will be set to indicate whether the
 * entity is unavailable on all platforms.
 *
 * \param unavailable_message If non-NULL, will be set to the message text
 * provided along with the unconditional unavailability of this entity. The
 * client is responsible for deallocating this string.
 *
 * \param availability If non-NULL, an array of CXPlatformAvailability instances
 * that will be populated with platform availability information, up to either
 * the number of platforms for which availability information is available (as
 * returned by this function) or \c availability_size, whichever is smaller.
 *
 * \param availability_size The number of elements available in the
 * \c availability array.
 *
 * \returns The number of platforms (N) for which availability information is
 * available (which is unrelated to \c availability_size).
 *
 * Note that the client is responsible for calling
 * \c clang_disposeCXPlatformAvailability to free each of the
 * platform-availability structures returned. There are
 * \c min(N, availability_size) such structures.
 */
CINDEX_LINKAGE int clang_getCursorPlatformAvailability(
    CXCursor cursor, int *always_deprecated, CXString *deprecated_message,
    int *always_unavailable, CXString *unavailable_message,
    CXPlatformAvailability *availability, int availability_size);

/**
 * Free the memory associated with a \c CXPlatformAvailability structure.
 */
CINDEX_LINKAGE void
clang_disposeCXPlatformAvailability(CXPlatformAvailability *availability);

/**
 * If cursor refers to a variable declaration and it has initializer returns
 * cursor referring to the initializer otherwise return null cursor.
 */
CINDEX_LINKAGE CXCursor clang_Cursor_getVarDeclInitializer(CXCursor cursor);

/**
 * If cursor refers to a variable declaration that has global storage returns 1.
 * If cursor refers to a variable declaration that doesn't have global storage
 * returns 0. Otherwise returns -1.
 */
CINDEX_LINKAGE int clang_Cursor_hasVarDeclGlobalStorage(CXCursor cursor);

/**
 * If cursor refers to a variable declaration that has external storage
 * returns 1. If cursor refers to a variable declaration that doesn't have
 * external storage returns 0. Otherwise returns -1.
 */
CINDEX_LINKAGE int clang_Cursor_hasVarDeclExternalStorage(CXCursor cursor);

/**
 * Describe the "language" of the entity referred to by a cursor.
 */
enum CXLanguageKind {
  CXLanguage_Invalid = 0,
  CXLanguage_C,
  CXLanguage_ObjC,
  CXLanguage_CPlusPlus
};

/**
 * Determine the "language" of the entity referred to by a given cursor.
 */
CINDEX_LINKAGE enum CXLanguageKind clang_getCursorLanguage(CXCursor cursor);

/**
 * Describe the "thread-local storage (TLS) kind" of the declaration
 * referred to by a cursor.
 */
enum CXTLSKind { CXTLS_None = 0, CXTLS_Dynamic, CXTLS_Static };

/**
 * Determine the "thread-local storage (TLS) kind" of the declaration
 * referred to by a cursor.
 */
CINDEX_LINKAGE enum CXTLSKind clang_getCursorTLSKind(CXCursor cursor);

/**
 * Returns the translation unit that a cursor originated from.
 */
CINDEX_LINKAGE CXTranslationUnit clang_Cursor_getTranslationUnit(CXCursor);

/**
 * A fast container representing a set of CXCursors.
 */
typedef struct CXCursorSetImpl *CXCursorSet;

/**
 * Creates an empty CXCursorSet.
 */
CINDEX_LINKAGE CXCursorSet clang_createCXCursorSet(void);

/**
 * Disposes a CXCursorSet and releases its associated memory.
 */
CINDEX_LINKAGE void clang_disposeCXCursorSet(CXCursorSet cset);

/**
 * Queries a CXCursorSet to see if it contains a specific CXCursor.
 *
 * \returns non-zero if the set contains the specified cursor.
 */
CINDEX_LINKAGE unsigned clang_CXCursorSet_contains(CXCursorSet cset,
                                                   CXCursor cursor);

/**
 * Inserts a CXCursor into a CXCursorSet.
 *
 * \returns zero if the CXCursor was already in the set, and non-zero otherwise.
 */
CINDEX_LINKAGE unsigned clang_CXCursorSet_insert(CXCursorSet cset,
                                                 CXCursor cursor);

/**
 * Determine the semantic parent of the given cursor.
 *
 * The semantic parent of a cursor is the cursor that semantically contains
 * the given \p cursor. For many declarations, the lexical and semantic parents
 * are equivalent (the lexical parent is returned by
 * \c clang_getCursorLexicalParent()). They diverge when declarations or
 * definitions are provided out-of-line. For example:
 *
 * \code
 * class C {
 *  void f();
 * };
 *
 * void C::f() { }
 * \endcode
 *
 * In the out-of-line definition of \c C::f, the semantic parent is
 * the class \c C, of which this function is a member. The lexical parent is
 * the place where the declaration actually occurs in the source code; in this
 * case, the definition occurs in the translation unit. In general, the
 * lexical parent for a given entity can change without affecting the semantics
 * of the program, and the lexical parent of different declarations of the
 * same entity may be different. Changing the semantic parent of a declaration,
 * on the other hand, can have a major impact on semantics, and redeclarations
 * of a particular entity should all have the same semantic context.
 *
 * In the example above, both declarations of \c C::f have \c C as their
 * semantic context, while the lexical context of the first \c C::f is \c C
 * and the lexical context of the second \c C::f is the translation unit.
 *
 * For global declarations, the semantic parent is the translation unit.
 */
CINDEX_LINKAGE CXCursor clang_getCursorSemanticParent(CXCursor cursor);

/**
 * Determine the lexical parent of the given cursor.
 *
 * The lexical parent of a cursor is the cursor in which the given \p cursor
 * was actually written. For many declarations, the lexical and semantic parents
 * are equivalent (the semantic parent is returned by
 * \c clang_getCursorSemanticParent()). They diverge when declarations or
 * definitions are provided out-of-line. For example:
 *
 * \code
 * class C {
 *  void f();
 * };
 *
 * void C::f() { }
 * \endcode
 *
 * In the out-of-line definition of \c C::f, the semantic parent is
 * the class \c C, of which this function is a member. The lexical parent is
 * the place where the declaration actually occurs in the source code; in this
 * case, the definition occurs in the translation unit. In general, the
 * lexical parent for a given entity can change without affecting the semantics
 * of the program, and the lexical parent of different declarations of the
 * same entity may be different. Changing the semantic parent of a declaration,
 * on the other hand, can have a major impact on semantics, and redeclarations
 * of a particular entity should all have the same semantic context.
 *
 * In the example above, both declarations of \c C::f have \c C as their
 * semantic context, while the lexical context of the first \c C::f is \c C
 * and the lexical context of the second \c C::f is the translation unit.
 *
 * For declarations written in the global scope, the lexical parent is
 * the translation unit.
 */
CINDEX_LINKAGE CXCursor clang_getCursorLexicalParent(CXCursor cursor);

/**
 * Determine the set of methods that are overridden by the given
 * method.
 *
 * In both Objective-C and C++, a method (aka virtual member function,
 * in C++) can override a virtual method in a base class. For
 * Objective-C, a method is said to override any method in the class's
 * base class, its protocols, or its categories' protocols, that has the same
 * selector and is of the same kind (class or instance).
 * If no such method exists, the search continues to the class's superclass,
 * its protocols, and its categories, and so on. A method from an Objective-C
 * implementation is considered to override the same methods as its
 * corresponding method in the interface.
 *
 * For C++, a virtual member function overrides any virtual member
 * function with the same signature that occurs in its base
 * classes. With multiple inheritance, a virtual member function can
 * override several virtual member functions coming from different
 * base classes.
 *
 * In all cases, this function determines the immediate overridden
 * method, rather than all of the overridden methods. For example, if
 * a method is originally declared in a class A, then overridden in B
 * (which in inherits from A) and also in C (which inherited from B),
 * then the only overridden method returned from this function when
 * invoked on C's method will be B's method. The client may then
 * invoke this function again, given the previously-found overridden
 * methods, to map out the complete method-override set.
 *
 * \param cursor A cursor representing an Objective-C or C++
 * method. This routine will compute the set of methods that this
 * method overrides.
 *
 * \param overridden A pointer whose pointee will be replaced with a
 * pointer to an array of cursors, representing the set of overridden
 * methods. If there are no overridden methods, the pointee will be
 * set to NULL. The pointee must be freed via a call to
 * \c clang_disposeOverriddenCursors().
 *
 * \param num_overridden A pointer to the number of overridden
 * functions, will be set to the number of overridden functions in the
 * array pointed to by \p overridden.
 */
CINDEX_LINKAGE void clang_getOverriddenCursors(CXCursor cursor,
                                               CXCursor **overridden,
                                               unsigned *num_overridden);

/**
 * Free the set of overridden cursors returned by \c
 * clang_getOverriddenCursors().
 */
CINDEX_LINKAGE void clang_disposeOverriddenCursors(CXCursor *overridden);

/**
 * Retrieve the file that is included by the given inclusion directive
 * cursor.
 */
CINDEX_LINKAGE CXFile clang_getIncludedFile(CXCursor cursor);

/**
 * @}
 */

/**
 * \defgroup CINDEX_CURSOR_SOURCE Mapping between cursors and source code
 *
 * Cursors represent a location within the Abstract Syntax Tree (AST). These
 * routines help map between cursors and the physical locations where the
 * described entities occur in the source code. The mapping is provided in
 * both directions, so one can map from source code to the AST and back.
 *
 * @{
 */

/**
 * Map a source location to the cursor that describes the entity at that
 * location in the source code.
 *
 * clang_getCursor() maps an arbitrary source location within a translation
 * unit down to the most specific cursor that describes the entity at that
 * location. For example, given an expression \c x + y, invoking
 * clang_getCursor() with a source location pointing to "x" will return the
 * cursor for "x"; similarly for "y". If the cursor points anywhere between
 * "x" or "y" (e.g., on the + or the whitespace around it), clang_getCursor()
 * will return a cursor referring to the "+" expression.
 *
 * \returns a cursor representing the entity at the given source location, or
 * a NULL cursor if no such entity can be found.
 */
CINDEX_LINKAGE CXCursor clang_getCursor(CXTranslationUnit, CXSourceLocation);

/**
 * Retrieve the physical location of the source constructor referenced
 * by the given cursor.
 *
 * The location of a declaration is typically the location of the name of that
 * declaration, where the name of that declaration would occur if it is
 * unnamed, or some keyword that introduces that particular declaration.
 * The location of a reference is where that reference occurs within the
 * source code.
 */
CINDEX_LINKAGE CXSourceLocation clang_getCursorLocation(CXCursor);

/**
 * Retrieve the physical extent of the source construct referenced by
 * the given cursor.
 *
 * The extent of a cursor starts with the file/line/column pointing at the
 * first character within the source construct that the cursor refers to and
 * ends with the last character within that source construct. For a
 * declaration, the extent covers the declaration itself. For a reference,
 * the extent covers the location of the reference (e.g., where the referenced
 * entity was actually used).
 */
CINDEX_LINKAGE CXSourceRange clang_getCursorExtent(CXCursor);

/**
 * @}
 */

/**
 * \defgroup CINDEX_TYPES Type information for CXCursors
 *
 * @{
 */

/**
 * Describes the kind of type
 */
enum CXTypeKind {
  /**
   * Represents an invalid type (e.g., where no type is available).
   */
  CXType_Invalid = 0,

  /**
   * A type whose specific kind is not exposed via this
   * interface.
   */
  CXType_Unexposed = 1,

  /* Builtin types */
  CXType_Void = 2,
  CXType_Bool = 3,
  CXType_Char_U = 4,
  CXType_UChar = 5,
  CXType_Char16 = 6,
  CXType_Char32 = 7,
  CXType_UShort = 8,
  CXType_UInt = 9,
  CXType_ULong = 10,
  CXType_ULongLong = 11,
  CXType_UInt128 = 12,
  CXType_Char_S = 13,
  CXType_SChar = 14,
  CXType_WChar = 15,
  CXType_Short = 16,
  CXType_Int = 17,
  CXType_Long = 18,
  CXType_LongLong = 19,
  CXType_Int128 = 20,
  CXType_Float = 21,
  CXType_Double = 22,
  CXType_LongDouble = 23,
  CXType_NullPtr = 24,
  CXType_Overload = 25,
  CXType_Dependent = 26,
  CXType_ObjCId = 27,
  CXType_ObjCClass = 28,
  CXType_ObjCSel = 29,
  CXType_Float128 = 30,
  CXType_Half = 31,
  CXType_Float16 = 32,
  CXType_ShortAccum = 33,
  CXType_Accum = 34,
  CXType_LongAccum = 35,
  CXType_UShortAccum = 36,
  CXType_UAccum = 37,
  CXType_ULongAccum = 38,
  CXType_BFloat16 = 39,
  CXType_Ibm128 = 40,
  CXType_FirstBuiltin = CXType_Void,
  CXType_LastBuiltin = CXType_Ibm128,

  CXType_Complex = 100,
  CXType_Pointer = 101,
  CXType_BlockPointer = 102,
  CXType_LValueReference = 103,
  CXType_RValueReference = 104,
  CXType_Record = 105,
  CXType_Enum = 106,
  CXType_Typedef = 107,
  CXType_ObjCInterface = 108,
  CXType_ObjCObjectPointer = 109,
  CXType_FunctionNoProto = 110,
  CXType_FunctionProto = 111,
  CXType_ConstantArray = 112,
  CXType_Vector = 113,
  CXType_IncompleteArray = 114,
  CXType_VariableArray = 115,
  CXType_DependentSizedArray = 116,
  CXType_MemberPointer = 117,
  CXType_Auto = 118,

  /**
   * Represents a type that was referred to using an elaborated type keyword.
   *
   * E.g., struct S, or via a qualified name, e.g., N::M::type, or both.
   */
  CXType_Elaborated = 119,

  /* OpenCL PipeType. */
  CXType_Pipe = 120,

  /* OpenCL builtin types. */
  CXType_OCLImage1dRO = 121,
  CXType_OCLImage1dArrayRO = 122,
  CXType_OCLImage1dBufferRO = 123,
  CXType_OCLImage2dRO = 124,
  CXType_OCLImage2dArrayRO = 125,
  CXType_OCLImage2dDepthRO = 126,
  CXType_OCLImage2dArrayDepthRO = 127,
  CXType_OCLImage2dMSAARO = 128,
  CXType_OCLImage2dArrayMSAARO = 129,
  CXType_OCLImage2dMSAADepthRO = 130,
  CXType_OCLImage2dArrayMSAADepthRO = 131,
  CXType_OCLImage3dRO = 132,
  CXType_OCLImage1dWO = 133,
  CXType_OCLImage1dArrayWO = 134,
  CXType_OCLImage1dBufferWO = 135,
  CXType_OCLImage2dWO = 136,
  CXType_OCLImage2dArrayWO = 137,
  CXType_OCLImage2dDepthWO = 138,
  CXType_OCLImage2dArrayDepthWO = 139,
  CXType_OCLImage2dMSAAWO = 140,
  CXType_OCLImage2dArrayMSAAWO = 141,
  CXType_OCLImage2dMSAADepthWO = 142,
  CXType_OCLImage2dArrayMSAADepthWO = 143,
  CXType_OCLImage3dWO = 144,
  CXType_OCLImage1dRW = 145,
  CXType_OCLImage1dArrayRW = 146,
  CXType_OCLImage1dBufferRW = 147,
  CXType_OCLImage2dRW = 148,
  CXType_OCLImage2dArrayRW = 149,
  CXType_OCLImage2dDepthRW = 150,
  CXType_OCLImage2dArrayDepthRW = 151,
  CXType_OCLImage2dMSAARW = 152,
  CXType_OCLImage2dArrayMSAARW = 153,
  CXType_OCLImage2dMSAADepthRW = 154,
  CXType_OCLImage2dArrayMSAADepthRW = 155,
  CXType_OCLImage3dRW = 156,
  CXType_OCLSampler = 157,
  CXType_OCLEvent = 158,
  CXType_OCLQueue = 159,
  CXType_OCLReserveID = 160,

  CXType_ObjCObject = 161,
  CXType_ObjCTypeParam = 162,
  CXType_Attributed = 163,

  CXType_OCLIntelSubgroupAVCMcePayload = 164,
  CXType_OCLIntelSubgroupAVCImePayload = 165,
  CXType_OCLIntelSubgroupAVCRefPayload = 166,
  CXType_OCLIntelSubgroupAVCSicPayload = 167,
  CXType_OCLIntelSubgroupAVCMceResult = 168,
  CXType_OCLIntelSubgroupAVCImeResult = 169,
  CXType_OCLIntelSubgroupAVCRefResult = 170,
  CXType_OCLIntelSubgroupAVCSicResult = 171,
  CXType_OCLIntelSubgroupAVCImeResultSingleReferenceStreamout = 172,
  CXType_OCLIntelSubgroupAVCImeResultDualReferenceStreamout = 173,
  CXType_OCLIntelSubgroupAVCImeSingleReferenceStreamin = 174,
  CXType_OCLIntelSubgroupAVCImeDualReferenceStreamin = 175,

  /* Old aliases for AVC OpenCL extension types. */
  CXType_OCLIntelSubgroupAVCImeResultSingleRefStreamout = 172,
  CXType_OCLIntelSubgroupAVCImeResultDualRefStreamout = 173,
  CXType_OCLIntelSubgroupAVCImeSingleRefStreamin = 174,
  CXType_OCLIntelSubgroupAVCImeDualRefStreamin = 175,

  CXType_ExtVector = 176,
  CXType_Atomic = 177,
  CXType_BTFTagAttributed = 178
};

/**
 * Describes the calling convention of a function type
 */
enum CXCallingConv {
  CXCallingConv_Default = 0,
  CXCallingConv_C = 1,
  CXCallingConv_X86StdCall = 2,
  CXCallingConv_X86FastCall = 3,
  CXCallingConv_X86ThisCall = 4,
  CXCallingConv_X86Pascal = 5,
  CXCallingConv_AAPCS = 6,
  CXCallingConv_AAPCS_VFP = 7,
  CXCallingConv_X86RegCall = 8,
  CXCallingConv_IntelOclBicc = 9,
  CXCallingConv_Win64 = 10,
  /* Alias for compatibility with older versions of API. */
  CXCallingConv_X86_64Win64 = CXCallingConv_Win64,
  CXCallingConv_X86_64SysV = 11,
  CXCallingConv_X86VectorCall = 12,
  CXCallingConv_Swift = 13,
  CXCallingConv_PreserveMost = 14,
  CXCallingConv_PreserveAll = 15,
  CXCallingConv_AArch64VectorCall = 16,
  CXCallingConv_SwiftAsync = 17,
  CXCallingConv_AArch64SVEPCS = 18,
  CXCallingConv_M68kRTD = 19,
  CXCallingConv_PreserveNone = 20,
  CXCallingConv_RISCVVectorCall = 21,

  CXCallingConv_Invalid = 100,
  CXCallingConv_Unexposed = 200
};

/**
 * The type of an element in the abstract syntax tree.
 *
 */
typedef struct {
  enum CXTypeKind kind;
  void *data[2];
} CXType;

/**
 * Retrieve the type of a CXCursor (if any).
 */
CINDEX_LINKAGE CXType clang_getCursorType(CXCursor C);

/**
 * Pretty-print the underlying type using the rules of the
 * language of the translation unit from which it came.
 *
 * If the type is invalid, an empty string is returned.
 */
CINDEX_LINKAGE CXString clang_getTypeSpelling(CXType CT);

/**
 * Retrieve the underlying type of a typedef declaration.
 *
 * If the cursor does not reference a typedef declaration, an invalid type is
 * returned.
 */
CINDEX_LINKAGE CXType clang_getTypedefDeclUnderlyingType(CXCursor C);

/**
 * Retrieve the integer type of an enum declaration.
 *
 * If the cursor does not reference an enum declaration, an invalid type is
 * returned.
 */
CINDEX_LINKAGE CXType clang_getEnumDeclIntegerType(CXCursor C);

/**
 * Retrieve the integer value of an enum constant declaration as a signed
 *  long long.
 *
 * If the cursor does not reference an enum constant declaration, LLONG_MIN is
 * returned. Since this is also potentially a valid constant value, the kind of
 * the cursor must be verified before calling this function.
 */
CINDEX_LINKAGE long long clang_getEnumConstantDeclValue(CXCursor C);

/**
 * Retrieve the integer value of an enum constant declaration as an unsigned
 *  long long.
 *
 * If the cursor does not reference an enum constant declaration, ULLONG_MAX is
 * returned. Since this is also potentially a valid constant value, the kind of
 * the cursor must be verified before calling this function.
 */
CINDEX_LINKAGE unsigned long long
clang_getEnumConstantDeclUnsignedValue(CXCursor C);

/**
 * Returns non-zero if the cursor specifies a Record member that is a bit-field.
 */
CINDEX_LINKAGE unsigned clang_Cursor_isBitField(CXCursor C);

/**
 * Retrieve the bit width of a bit-field declaration as an integer.
 *
 * If the cursor does not reference a bit-field, or if the bit-field's width
 * expression cannot be evaluated, -1 is returned.
 *
 * For example:
 * \code
 * if (clang_Cursor_isBitField(Cursor)) {
 *   int Width = clang_getFieldDeclBitWidth(Cursor);
 *   if (Width != -1) {
 *     // The bit-field width is not value-dependent.
 *   }
 * }
 * \endcode
 */
CINDEX_LINKAGE int clang_getFieldDeclBitWidth(CXCursor C);

/**
 * Retrieve the number of non-variadic arguments associated with a given
 * cursor.
 *
 * The number of arguments can be determined for calls as well as for
 * declarations of functions or methods. For other cursors -1 is returned.
 */
CINDEX_LINKAGE int clang_Cursor_getNumArguments(CXCursor C);

/**
 * Retrieve the argument cursor of a function or method.
 *
 * The argument cursor can be determined for calls as well as for declarations
 * of functions or methods. For other cursors and for invalid indices, an
 * invalid cursor is returned.
 */
CINDEX_LINKAGE CXCursor clang_Cursor_getArgument(CXCursor C, unsigned i);

/**
 * Describes the kind of a template argument.
 *
 * See the definition of llvm::clang::TemplateArgument::ArgKind for full
 * element descriptions.
 */
enum CXTemplateArgumentKind {
  CXTemplateArgumentKind_Null,
  CXTemplateArgumentKind_Type,
  CXTemplateArgumentKind_Declaration,
  CXTemplateArgumentKind_NullPtr,
  CXTemplateArgumentKind_Integral,
  CXTemplateArgumentKind_Template,
  CXTemplateArgumentKind_TemplateExpansion,
  CXTemplateArgumentKind_Expression,
  CXTemplateArgumentKind_Pack,
  /* Indicates an error case, preventing the kind from being deduced. */
  CXTemplateArgumentKind_Invalid
};

/**
 * Returns the number of template args of a function, struct, or class decl
 * representing a template specialization.
 *
 * If the argument cursor cannot be converted into a template function
 * declaration, -1 is returned.
 *
 * For example, for the following declaration and specialization:
 *   template <typename T, int kInt, bool kBool>
 *   void foo() { ... }
 *
 *   template <>
 *   void foo<float, -7, true>();
 *
 * The value 3 would be returned from this call.
 */
CINDEX_LINKAGE int clang_Cursor_getNumTemplateArguments(CXCursor C);

/**
 * Retrieve the kind of the I'th template argument of the CXCursor C.
 *
 * If the argument CXCursor does not represent a FunctionDecl, StructDecl, or
 * ClassTemplatePartialSpecialization, an invalid template argument kind is
 * returned.
 *
 * For example, for the following declaration and specialization:
 *   template <typename T, int kInt, bool kBool>
 *   void foo() { ... }
 *
 *   template <>
 *   void foo<float, -7, true>();
 *
 * For I = 0, 1, and 2, Type, Integral, and Integral will be returned,
 * respectively.
 */
CINDEX_LINKAGE enum CXTemplateArgumentKind
clang_Cursor_getTemplateArgumentKind(CXCursor C, unsigned I);

/**
 * Retrieve a CXType representing the type of a TemplateArgument of a
 *  function decl representing a template specialization.
 *
 * If the argument CXCursor does not represent a FunctionDecl, StructDecl,
 * ClassDecl or ClassTemplatePartialSpecialization whose I'th template argument
 * has a kind of CXTemplateArgKind_Integral, an invalid type is returned.
 *
 * For example, for the following declaration and specialization:
 *   template <typename T, int kInt, bool kBool>
 *   void foo() { ... }
 *
 *   template <>
 *   void foo<float, -7, true>();
 *
 * If called with I = 0, "float", will be returned.
 * Invalid types will be returned for I == 1 or 2.
 */
CINDEX_LINKAGE CXType clang_Cursor_getTemplateArgumentType(CXCursor C,
                                                           unsigned I);

/**
 * Retrieve the value of an Integral TemplateArgument (of a function
 *  decl representing a template specialization) as a signed long long.
 *
 * It is undefined to call this function on a CXCursor that does not represent a
 * FunctionDecl, StructDecl, ClassDecl or ClassTemplatePartialSpecialization
 * whose I'th template argument is not an integral value.
 *
 * For example, for the following declaration and specialization:
 *   template <typename T, int kInt, bool kBool>
 *   void foo() { ... }
 *
 *   template <>
 *   void foo<float, -7, true>();
 *
 * If called with I = 1 or 2, -7 or true will be returned, respectively.
 * For I == 0, this function's behavior is undefined.
 */
CINDEX_LINKAGE long long clang_Cursor_getTemplateArgumentValue(CXCursor C,
                                                               unsigned I);

/**
 * Retrieve the value of an Integral TemplateArgument (of a function
 *  decl representing a template specialization) as an unsigned long long.
 *
 * It is undefined to call this function on a CXCursor that does not represent a
 * FunctionDecl, StructDecl, ClassDecl or ClassTemplatePartialSpecialization or
 * whose I'th template argument is not an integral value.
 *
 * For example, for the following declaration and specialization:
 *   template <typename T, int kInt, bool kBool>
 *   void foo() { ... }
 *
 *   template <>
 *   void foo<float, 2147483649, true>();
 *
 * If called with I = 1 or 2, 2147483649 or true will be returned, respectively.
 * For I == 0, this function's behavior is undefined.
 */
CINDEX_LINKAGE unsigned long long
clang_Cursor_getTemplateArgumentUnsignedValue(CXCursor C, unsigned I);

/**
 * Determine whether two CXTypes represent the same type.
 *
 * \returns non-zero if the CXTypes represent the same type and
 *          zero otherwise.
 */
CINDEX_LINKAGE unsigned clang_equalTypes(CXType A, CXType B);

/**
 * Return the canonical type for a CXType.
 *
 * Clang's type system explicitly models typedefs and all the ways
 * a specific type can be represented.  The canonical type is the underlying
 * type with all the "sugar" removed.  For example, if 'T' is a typedef
 * for 'int', the canonical type for 'T' would be 'int'.
 */
CINDEX_LINKAGE CXType clang_getCanonicalType(CXType T);

/**
 * Determine whether a CXType has the "const" qualifier set,
 * without looking through typedefs that may have added "const" at a
 * different level.
 */
CINDEX_LINKAGE unsigned clang_isConstQualifiedType(CXType T);

/**
 * Determine whether a  CXCursor that is a macro, is
 * function like.
 */
CINDEX_LINKAGE unsigned clang_Cursor_isMacroFunctionLike(CXCursor C);

/**
 * Determine whether a  CXCursor that is a macro, is a
 * builtin one.
 */
CINDEX_LINKAGE unsigned clang_Cursor_isMacroBuiltin(CXCursor C);

/**
 * Determine whether a  CXCursor that is a function declaration, is an
 * inline declaration.
 */
CINDEX_LINKAGE unsigned clang_Cursor_isFunctionInlined(CXCursor C);

/**
 * Determine whether a CXType has the "volatile" qualifier set,
 * without looking through typedefs that may have added "volatile" at
 * a different level.
 */
CINDEX_LINKAGE unsigned clang_isVolatileQualifiedType(CXType T);

/**
 * Determine whether a CXType has the "restrict" qualifier set,
 * without looking through typedefs that may have added "restrict" at a
 * different level.
 */
CINDEX_LINKAGE unsigned clang_isRestrictQualifiedType(CXType T);

/**
 * Returns the address space of the given type.
 */
CINDEX_LINKAGE unsigned clang_getAddressSpace(CXType T);

/**
 * Returns the typedef name of the given type.
 */
CINDEX_LINKAGE CXString clang_getTypedefName(CXType CT);

/**
 * For pointer types, returns the type of the pointee.
 */
CINDEX_LINKAGE CXType clang_getPointeeType(CXType T);

/**
 * Retrieve the unqualified variant of the given type, removing as
 * little sugar as possible.
 *
 * For example, given the following series of typedefs:
 *
 * \code
 * typedef int Integer;
 * typedef const Integer CInteger;
 * typedef CInteger DifferenceType;
 * \endcode
 *
 * Executing \c clang_getUnqualifiedType() on a \c CXType that
 * represents \c DifferenceType, will desugar to a type representing
 * \c Integer, that has no qualifiers.
 *
 * And, executing \c clang_getUnqualifiedType() on the type of the
 * first argument of the following function declaration:
 *
 * \code
 * void foo(const int);
 * \endcode
 *
 * Will return a type representing \c int, removing the \c const
 * qualifier.
 *
 * Sugar over array types is not desugared.
 *
 * A type can be checked for qualifiers with \c
 * clang_isConstQualifiedType(), \c clang_isVolatileQualifiedType()
 * and \c clang_isRestrictQualifiedType().
 *
 * A type that resulted from a call to \c clang_getUnqualifiedType
 * will return \c false for all of the above calls.
 */
CINDEX_LINKAGE CXType clang_getUnqualifiedType(CXType CT);

/**
 * For reference types (e.g., "const int&"), returns the type that the
 * reference refers to (e.g "const int").
 *
 * Otherwise, returns the type itself.
 *
 * A type that has kind \c CXType_LValueReference or
 * \c CXType_RValueReference is a reference type.
 */
CINDEX_LINKAGE CXType clang_getNonReferenceType(CXType CT);

/**
 * Return the cursor for the declaration of the given type.
 */
CINDEX_LINKAGE CXCursor clang_getTypeDeclaration(CXType T);

/**
 * Returns the Objective-C type encoding for the specified declaration.
 */
CINDEX_LINKAGE CXString clang_getDeclObjCTypeEncoding(CXCursor C);

/**
 * Returns the Objective-C type encoding for the specified CXType.
 */
CINDEX_LINKAGE CXString clang_Type_getObjCEncoding(CXType type);

/**
 * Retrieve the spelling of a given CXTypeKind.
 */
CINDEX_LINKAGE CXString clang_getTypeKindSpelling(enum CXTypeKind K);

/**
 * Retrieve the calling convention associated with a function type.
 *
 * If a non-function type is passed in, CXCallingConv_Invalid is returned.
 */
CINDEX_LINKAGE enum CXCallingConv clang_getFunctionTypeCallingConv(CXType T);

/**
 * Retrieve the return type associated with a function type.
 *
 * If a non-function type is passed in, an invalid type is returned.
 */
CINDEX_LINKAGE CXType clang_getResultType(CXType T);

/**
 * Retrieve the exception specification type associated with a function type.
 * This is a value of type CXCursor_ExceptionSpecificationKind.
 *
 * If a non-function type is passed in, an error code of -1 is returned.
 */
CINDEX_LINKAGE int clang_getExceptionSpecificationType(CXType T);

/**
 * Retrieve the number of non-variadic parameters associated with a
 * function type.
 *
 * If a non-function type is passed in, -1 is returned.
 */
CINDEX_LINKAGE int clang_getNumArgTypes(CXType T);

/**
 * Retrieve the type of a parameter of a function type.
 *
 * If a non-function type is passed in or the function does not have enough
 * parameters, an invalid type is returned.
 */
CINDEX_LINKAGE CXType clang_getArgType(CXType T, unsigned i);

/**
 * Retrieves the base type of the ObjCObjectType.
 *
 * If the type is not an ObjC object, an invalid type is returned.
 */
CINDEX_LINKAGE CXType clang_Type_getObjCObjectBaseType(CXType T);

/**
 * Retrieve the number of protocol references associated with an ObjC object/id.
 *
 * If the type is not an ObjC object, 0 is returned.
 */
CINDEX_LINKAGE unsigned clang_Type_getNumObjCProtocolRefs(CXType T);

/**
 * Retrieve the decl for a protocol reference for an ObjC object/id.
 *
 * If the type is not an ObjC object or there are not enough protocol
 * references, an invalid cursor is returned.
 */
CINDEX_LINKAGE CXCursor clang_Type_getObjCProtocolDecl(CXType T, unsigned i);

/**
 * Retrieve the number of type arguments associated with an ObjC object.
 *
 * If the type is not an ObjC object, 0 is returned.
 */
CINDEX_LINKAGE unsigned clang_Type_getNumObjCTypeArgs(CXType T);

/**
 * Retrieve a type argument associated with an ObjC object.
 *
 * If the type is not an ObjC or the index is not valid,
 * an invalid type is returned.
 */
CINDEX_LINKAGE CXType clang_Type_getObjCTypeArg(CXType T, unsigned i);

/**
 * Return 1 if the CXType is a variadic function type, and 0 otherwise.
 */
CINDEX_LINKAGE unsigned clang_isFunctionTypeVariadic(CXType T);

/**
 * Retrieve the return type associated with a given cursor.
 *
 * This only returns a valid type if the cursor refers to a function or method.
 */
CINDEX_LINKAGE CXType clang_getCursorResultType(CXCursor C);

/**
 * Retrieve the exception specification type associated with a given cursor.
 * This is a value of type CXCursor_ExceptionSpecificationKind.
 *
 * This only returns a valid result if the cursor refers to a function or
 * method.
 */
CINDEX_LINKAGE int clang_getCursorExceptionSpecificationType(CXCursor C);

/**
 * Return 1 if the CXType is a POD (plain old data) type, and 0
 *  otherwise.
 */
CINDEX_LINKAGE unsigned clang_isPODType(CXType T);

/**
 * Return the element type of an array, complex, or vector type.
 *
 * If a type is passed in that is not an array, complex, or vector type,
 * an invalid type is returned.
 */
CINDEX_LINKAGE CXType clang_getElementType(CXType T);

/**
 * Return the number of elements of an array or vector type.
 *
 * If a type is passed in that is not an array or vector type,
 * -1 is returned.
 */
CINDEX_LINKAGE long long clang_getNumElements(CXType T);

/**
 * Return the element type of an array type.
 *
 * If a non-array type is passed in, an invalid type is returned.
 */
CINDEX_LINKAGE CXType clang_getArrayElementType(CXType T);

/**
 * Return the array size of a constant array.
 *
 * If a non-array type is passed in, -1 is returned.
 */
CINDEX_LINKAGE long long clang_getArraySize(CXType T);

/**
 * Retrieve the type named by the qualified-id.
 *
 * If a non-elaborated type is passed in, an invalid type is returned.
 */
CINDEX_LINKAGE CXType clang_Type_getNamedType(CXType T);

/**
 * Determine if a typedef is 'transparent' tag.
 *
 * A typedef is considered 'transparent' if it shares a name and spelling
 * location with its underlying tag type, as is the case with the NS_ENUM macro.
 *
 * \returns non-zero if transparent and zero otherwise.
 */
CINDEX_LINKAGE unsigned clang_Type_isTransparentTagTypedef(CXType T);

enum CXTypeNullabilityKind {
  /**
   * Values of this type can never be null.
   */
  CXTypeNullability_NonNull = 0,
  /**
   * Values of this type can be null.
   */
  CXTypeNullability_Nullable = 1,
  /**
   * Whether values of this type can be null is (explicitly)
   * unspecified. This captures a (fairly rare) case where we
   * can't conclude anything about the nullability of the type even
   * though it has been considered.
   */
  CXTypeNullability_Unspecified = 2,
  /**
   * Nullability is not applicable to this type.
   */
  CXTypeNullability_Invalid = 3,

  /**
   * Generally behaves like Nullable, except when used in a block parameter that
   * was imported into a swift async method. There, swift will assume that the
   * parameter can get null even if no error occurred. _Nullable parameters are
   * assumed to only get null on error.
   */
  CXTypeNullability_NullableResult = 4
};

/**
 * Retrieve the nullability kind of a pointer type.
 */
CINDEX_LINKAGE enum CXTypeNullabilityKind clang_Type_getNullability(CXType T);

/**
 * List the possible error codes for \c clang_Type_getSizeOf,
 *   \c clang_Type_getAlignOf, \c clang_Type_getOffsetOf and
 *   \c clang_Cursor_getOffsetOf.
 *
 * A value of this enumeration type can be returned if the target type is not
 * a valid argument to sizeof, alignof or offsetof.
 */
enum CXTypeLayoutError {
  /**
   * Type is of kind CXType_Invalid.
   */
  CXTypeLayoutError_Invalid = -1,
  /**
   * The type is an incomplete Type.
   */
  CXTypeLayoutError_Incomplete = -2,
  /**
   * The type is a dependent Type.
   */
  CXTypeLayoutError_Dependent = -3,
  /**
   * The type is not a constant size type.
   */
  CXTypeLayoutError_NotConstantSize = -4,
  /**
   * The Field name is not valid for this record.
   */
  CXTypeLayoutError_InvalidFieldName = -5,
  /**
   * The type is undeduced.
   */
  CXTypeLayoutError_Undeduced = -6
};

/**
 * Return the alignment of a type in bytes as per C++[expr.alignof]
 *   standard.
 *
 * If the type declaration is invalid, CXTypeLayoutError_Invalid is returned.
 * If the type declaration is an incomplete type, CXTypeLayoutError_Incomplete
 *   is returned.
 * If the type declaration is a dependent type, CXTypeLayoutError_Dependent is
 *   returned.
 * If the type declaration is not a constant size type,
 *   CXTypeLayoutError_NotConstantSize is returned.
 */
CINDEX_LINKAGE long long clang_Type_getAlignOf(CXType T);

/**
 * Return the class type of an member pointer type.
 *
 * If a non-member-pointer type is passed in, an invalid type is returned.
 */
CINDEX_LINKAGE CXType clang_Type_getClassType(CXType T);

/**
 * Return the size of a type in bytes as per C++[expr.sizeof] standard.
 *
 * If the type declaration is invalid, CXTypeLayoutError_Invalid is returned.
 * If the type declaration is an incomplete type, CXTypeLayoutError_Incomplete
 *   is returned.
 * If the type declaration is a dependent type, CXTypeLayoutError_Dependent is
 *   returned.
 */
CINDEX_LINKAGE long long clang_Type_getSizeOf(CXType T);

/**
 * Return the offset of a field named S in a record of type T in bits
 *   as it would be returned by __offsetof__ as per C++11[18.2p4]
 *
 * If the cursor is not a record field declaration, CXTypeLayoutError_Invalid
 *   is returned.
 * If the field's type declaration is an incomplete type,
 *   CXTypeLayoutError_Incomplete is returned.
 * If the field's type declaration is a dependent type,
 *   CXTypeLayoutError_Dependent is returned.
 * If the field's name S is not found,
 *   CXTypeLayoutError_InvalidFieldName is returned.
 */
CINDEX_LINKAGE long long clang_Type_getOffsetOf(CXType T, const char *S);

/**
 * Return the type that was modified by this attributed type.
 *
 * If the type is not an attributed type, an invalid type is returned.
 */
CINDEX_LINKAGE CXType clang_Type_getModifiedType(CXType T);

/**
 * Gets the type contained by this atomic type.
 *
 * If a non-atomic type is passed in, an invalid type is returned.
 */
CINDEX_LINKAGE CXType clang_Type_getValueType(CXType CT);

/**
 * Return the offset of the field represented by the Cursor.
 *
 * If the cursor is not a field declaration, -1 is returned.
 * If the cursor semantic parent is not a record field declaration,
 *   CXTypeLayoutError_Invalid is returned.
 * If the field's type declaration is an incomplete type,
 *   CXTypeLayoutError_Incomplete is returned.
 * If the field's type declaration is a dependent type,
 *   CXTypeLayoutError_Dependent is returned.
 * If the field's name S is not found,
 *   CXTypeLayoutError_InvalidFieldName is returned.
 */
CINDEX_LINKAGE long long clang_Cursor_getOffsetOfField(CXCursor C);

/**
 * Determine whether the given cursor represents an anonymous
 * tag or namespace
 */
CINDEX_LINKAGE unsigned clang_Cursor_isAnonymous(CXCursor C);

/**
 * Determine whether the given cursor represents an anonymous record
 * declaration.
 */
CINDEX_LINKAGE unsigned clang_Cursor_isAnonymousRecordDecl(CXCursor C);

/**
 * Determine whether the given cursor represents an inline namespace
 * declaration.
 */
CINDEX_LINKAGE unsigned clang_Cursor_isInlineNamespace(CXCursor C);

enum CXRefQualifierKind {
  /** No ref-qualifier was provided. */
  CXRefQualifier_None = 0,
  /** An lvalue ref-qualifier was provided (\c &). */
  CXRefQualifier_LValue,
  /** An rvalue ref-qualifier was provided (\c &&). */
  CXRefQualifier_RValue
};

/**
 * Returns the number of template arguments for given template
 * specialization, or -1 if type \c T is not a template specialization.
 */
CINDEX_LINKAGE int clang_Type_getNumTemplateArguments(CXType T);

/**
 * Returns the type template argument of a template class specialization
 * at given index.
 *
 * This function only returns template type arguments and does not handle
 * template template arguments or variadic packs.
 */
CINDEX_LINKAGE CXType clang_Type_getTemplateArgumentAsType(CXType T,
                                                           unsigned i);

/**
 * Retrieve the ref-qualifier kind of a function or method.
 *
 * The ref-qualifier is returned for C++ functions or methods. For other types
 * or non-C++ declarations, CXRefQualifier_None is returned.
 */
CINDEX_LINKAGE enum CXRefQualifierKind clang_Type_getCXXRefQualifier(CXType T);

/**
 * Returns 1 if the base class specified by the cursor with kind
 *   CX_CXXBaseSpecifier is virtual.
 */
CINDEX_LINKAGE unsigned clang_isVirtualBase(CXCursor);

/**
 * Represents the C++ access control level to a base class for a
 * cursor with kind CX_CXXBaseSpecifier.
 */
enum CX_CXXAccessSpecifier {
  CX_CXXInvalidAccessSpecifier,
  CX_CXXPublic,
  CX_CXXProtected,
  CX_CXXPrivate
};

/**
 * Returns the access control level for the referenced object.
 *
 * If the cursor refers to a C++ declaration, its access control level within
 * its parent scope is returned. Otherwise, if the cursor refers to a base
 * specifier or access specifier, the specifier itself is returned.
 */
CINDEX_LINKAGE enum CX_CXXAccessSpecifier clang_getCXXAccessSpecifier(CXCursor);

/**
 * Represents the storage classes as declared in the source. CX_SC_Invalid
 * was added for the case that the passed cursor in not a declaration.
 */
enum CX_StorageClass {
  CX_SC_Invalid,
  CX_SC_None,
  CX_SC_Extern,
  CX_SC_Static,
  CX_SC_PrivateExtern,
  CX_SC_OpenCLWorkGroupLocal,
  CX_SC_Auto,
  CX_SC_Register
};

/**
 * Represents a specific kind of binary operator which can appear at a cursor.
 */
enum CX_BinaryOperatorKind {
  CX_BO_Invalid = 0,
  CX_BO_PtrMemD = 1,
  CX_BO_PtrMemI = 2,
  CX_BO_Mul = 3,
  CX_BO_Div = 4,
  CX_BO_Rem = 5,
  CX_BO_Add = 6,
  CX_BO_Sub = 7,
  CX_BO_Shl = 8,
  CX_BO_Shr = 9,
  CX_BO_Cmp = 10,
  CX_BO_LT = 11,
  CX_BO_GT = 12,
  CX_BO_LE = 13,
  CX_BO_GE = 14,
  CX_BO_EQ = 15,
  CX_BO_NE = 16,
  CX_BO_And = 17,
  CX_BO_Xor = 18,
  CX_BO_Or = 19,
  CX_BO_LAnd = 20,
  CX_BO_LOr = 21,
  CX_BO_Assign = 22,
  CX_BO_MulAssign = 23,
  CX_BO_DivAssign = 24,
  CX_BO_RemAssign = 25,
  CX_BO_AddAssign = 26,
  CX_BO_SubAssign = 27,
  CX_BO_ShlAssign = 28,
  CX_BO_ShrAssign = 29,
  CX_BO_AndAssign = 30,
  CX_BO_XorAssign = 31,
  CX_BO_OrAssign = 32,
  CX_BO_Comma = 33,
  CX_BO_LAST = CX_BO_Comma
};

/**
 * \brief Returns the operator code for the binary operator.
 */
CINDEX_LINKAGE enum CX_BinaryOperatorKind
clang_Cursor_getBinaryOpcode(CXCursor C);

/**
 * \brief Returns a string containing the spelling of the binary operator.
 */
CINDEX_LINKAGE CXString
clang_Cursor_getBinaryOpcodeStr(enum CX_BinaryOperatorKind Op);

/**
 * Returns the storage class for a function or variable declaration.
 *
 * If the passed in Cursor is not a function or variable declaration,
 * CX_SC_Invalid is returned else the storage class.
 */
CINDEX_LINKAGE enum CX_StorageClass clang_Cursor_getStorageClass(CXCursor);

/**
 * Determine the number of overloaded declarations referenced by a
 * \c CXCursor_OverloadedDeclRef cursor.
 *
 * \param cursor The cursor whose overloaded declarations are being queried.
 *
 * \returns The number of overloaded declarations referenced by \c cursor. If it
 * is not a \c CXCursor_OverloadedDeclRef cursor, returns 0.
 */
CINDEX_LINKAGE unsigned clang_getNumOverloadedDecls(CXCursor cursor);

/**
 * Retrieve a cursor for one of the overloaded declarations referenced
 * by a \c CXCursor_OverloadedDeclRef cursor.
 *
 * \param cursor The cursor whose overloaded declarations are being queried.
 *
 * \param index The zero-based index into the set of overloaded declarations in
 * the cursor.
 *
 * \returns A cursor representing the declaration referenced by the given
 * \c cursor at the specified \c index. If the cursor does not have an
 * associated set of overloaded declarations, or if the index is out of bounds,
 * returns \c clang_getNullCursor();
 */
CINDEX_LINKAGE CXCursor clang_getOverloadedDecl(CXCursor cursor,
                                                unsigned index);

/**
 * @}
 */

/**
 * \defgroup CINDEX_ATTRIBUTES Information for attributes
 *
 * @{
 */

/**
 * For cursors representing an iboutletcollection attribute,
 *  this function returns the collection element type.
 *
 */
CINDEX_LINKAGE CXType clang_getIBOutletCollectionType(CXCursor);

/**
 * @}
 */

/**
 * \defgroup CINDEX_CURSOR_TRAVERSAL Traversing the AST with cursors
 *
 * These routines provide the ability to traverse the abstract syntax tree
 * using cursors.
 *
 * @{
 */

/**
 * Describes how the traversal of the children of a particular
 * cursor should proceed after visiting a particular child cursor.
 *
 * A value of this enumeration type should be returned by each
 * \c CXCursorVisitor to indicate how clang_visitChildren() proceed.
 */
enum CXChildVisitResult {
  /**
   * Terminates the cursor traversal.
   */
  CXChildVisit_Break,
  /**
   * Continues the cursor traversal with the next sibling of
   * the cursor just visited, without visiting its children.
   */
  CXChildVisit_Continue,
  /**
   * Recursively traverse the children of this cursor, using
   * the same visitor and client data.
   */
  CXChildVisit_Recurse
};

/**
 * Visitor invoked for each cursor found by a traversal.
 *
 * This visitor function will be invoked for each cursor found by
 * clang_visitCursorChildren(). Its first argument is the cursor being
 * visited, its second argument is the parent visitor for that cursor,
 * and its third argument is the client data provided to
 * clang_visitCursorChildren().
 *
 * The visitor should return one of the \c CXChildVisitResult values
 * to direct clang_visitCursorChildren().
 */
typedef enum CXChildVisitResult (*CXCursorVisitor)(CXCursor cursor,
                                                   CXCursor parent,
                                                   CXClientData client_data);

/**
 * Visit the children of a particular cursor.
 *
 * This function visits all the direct children of the given cursor,
 * invoking the given \p visitor function with the cursors of each
 * visited child. The traversal may be recursive, if the visitor returns
 * \c CXChildVisit_Recurse. The traversal may also be ended prematurely, if
 * the visitor returns \c CXChildVisit_Break.
 *
 * \param parent the cursor whose child may be visited. All kinds of
 * cursors can be visited, including invalid cursors (which, by
 * definition, have no children).
 *
 * \param visitor the visitor function that will be invoked for each
 * child of \p parent.
 *
 * \param client_data pointer data supplied by the client, which will
 * be passed to the visitor each time it is invoked.
 *
 * \returns a non-zero value if the traversal was terminated
 * prematurely by the visitor returning \c CXChildVisit_Break.
 */
CINDEX_LINKAGE unsigned clang_visitChildren(CXCursor parent,
                                            CXCursorVisitor visitor,
                                            CXClientData client_data);
/**
 * Visitor invoked for each cursor found by a traversal.
 *
 * This visitor block will be invoked for each cursor found by
 * clang_visitChildrenWithBlock(). Its first argument is the cursor being
 * visited, its second argument is the parent visitor for that cursor.
 *
 * The visitor should return one of the \c CXChildVisitResult values
 * to direct clang_visitChildrenWithBlock().
 */
#if __has_feature(blocks)
typedef enum CXChildVisitResult (^CXCursorVisitorBlock)(CXCursor cursor,
                                                        CXCursor parent);
#else
typedef struct _CXChildVisitResult *CXCursorVisitorBlock;
#endif

/**
 * Visits the children of a cursor using the specified block.  Behaves
 * identically to clang_visitChildren() in all other respects.
 */
CINDEX_LINKAGE unsigned
clang_visitChildrenWithBlock(CXCursor parent, CXCursorVisitorBlock block);

/**
 * @}
 */

/**
 * \defgroup CINDEX_CURSOR_XREF Cross-referencing in the AST
 *
 * These routines provide the ability to determine references within and
 * across translation units, by providing the names of the entities referenced
 * by cursors, follow reference cursors to the declarations they reference,
 * and associate declarations with their definitions.
 *
 * @{
 */

/**
 * Retrieve a Unified Symbol Resolution (USR) for the entity referenced
 * by the given cursor.
 *
 * A Unified Symbol Resolution (USR) is a string that identifies a particular
 * entity (function, class, variable, etc.) within a program. USRs can be
 * compared across translation units to determine, e.g., when references in
 * one translation refer to an entity defined in another translation unit.
 */
CINDEX_LINKAGE CXString clang_getCursorUSR(CXCursor);

/**
 * Construct a USR for a specified Objective-C class.
 */
CINDEX_LINKAGE CXString clang_constructUSR_ObjCClass(const char *class_name);

/**
 * Construct a USR for a specified Objective-C category.
 */
CINDEX_LINKAGE CXString clang_constructUSR_ObjCCategory(
    const char *class_name, const char *category_name);

/**
 * Construct a USR for a specified Objective-C protocol.
 */
CINDEX_LINKAGE CXString
clang_constructUSR_ObjCProtocol(const char *protocol_name);

/**
 * Construct a USR for a specified Objective-C instance variable and
 *   the USR for its containing class.
 */
CINDEX_LINKAGE CXString clang_constructUSR_ObjCIvar(const char *name,
                                                    CXString classUSR);

/**
 * Construct a USR for a specified Objective-C method and
 *   the USR for its containing class.
 */
CINDEX_LINKAGE CXString clang_constructUSR_ObjCMethod(const char *name,
                                                      unsigned isInstanceMethod,
                                                      CXString classUSR);

/**
 * Construct a USR for a specified Objective-C property and the USR
 *  for its containing class.
 */
CINDEX_LINKAGE CXString clang_constructUSR_ObjCProperty(const char *property,
                                                        CXString classUSR);

/**
 * Retrieve a name for the entity referenced by this cursor.
 */
CINDEX_LINKAGE CXString clang_getCursorSpelling(CXCursor);

/**
 * Retrieve a range for a piece that forms the cursors spelling name.
 * Most of the times there is only one range for the complete spelling but for
 * Objective-C methods and Objective-C message expressions, there are multiple
 * pieces for each selector identifier.
 *
 * \param pieceIndex the index of the spelling name piece. If this is greater
 * than the actual number of pieces, it will return a NULL (invalid) range.
 *
 * \param options Reserved.
 */
CINDEX_LINKAGE CXSourceRange clang_Cursor_getSpellingNameRange(
    CXCursor, unsigned pieceIndex, unsigned options);

/**
 * Opaque pointer representing a policy that controls pretty printing
 * for \c clang_getCursorPrettyPrinted.
 */
typedef void *CXPrintingPolicy;

/**
 * Properties for the printing policy.
 *
 * See \c clang::PrintingPolicy for more information.
 */
enum CXPrintingPolicyProperty {
  CXPrintingPolicy_Indentation,
  CXPrintingPolicy_SuppressSpecifiers,
  CXPrintingPolicy_SuppressTagKeyword,
  CXPrintingPolicy_IncludeTagDefinition,
  CXPrintingPolicy_SuppressScope,
  CXPrintingPolicy_SuppressUnwrittenScope,
  CXPrintingPolicy_SuppressInitializers,
  CXPrintingPolicy_ConstantArraySizeAsWritten,
  CXPrintingPolicy_AnonymousTagLocations,
  CXPrintingPolicy_SuppressStrongLifetime,
  CXPrintingPolicy_SuppressLifetimeQualifiers,
  CXPrintingPolicy_SuppressTemplateArgsInCXXConstructors,
  CXPrintingPolicy_Bool,
  CXPrintingPolicy_Restrict,
  CXPrintingPolicy_Alignof,
  CXPrintingPolicy_UnderscoreAlignof,
  CXPrintingPolicy_UseVoidForZeroParams,
  CXPrintingPolicy_TerseOutput,
  CXPrintingPolicy_PolishForDeclaration,
  CXPrintingPolicy_Half,
  CXPrintingPolicy_MSWChar,
  CXPrintingPolicy_IncludeNewlines,
  CXPrintingPolicy_MSVCFormatting,
  CXPrintingPolicy_ConstantsAsWritten,
  CXPrintingPolicy_SuppressImplicitBase,
  CXPrintingPolicy_FullyQualifiedName,

  CXPrintingPolicy_LastProperty = CXPrintingPolicy_FullyQualifiedName
};

/**
 * Get a property value for the given printing policy.
 */
CINDEX_LINKAGE unsigned
clang_PrintingPolicy_getProperty(CXPrintingPolicy Policy,
                                 enum CXPrintingPolicyProperty Property);

/**
 * Set a property value for the given printing policy.
 */
CINDEX_LINKAGE void
clang_PrintingPolicy_setProperty(CXPrintingPolicy Policy,
                                 enum CXPrintingPolicyProperty Property,
                                 unsigned Value);

/**
 * Retrieve the default policy for the cursor.
 *
 * The policy should be released after use with \c
 * clang_PrintingPolicy_dispose.
 */
CINDEX_LINKAGE CXPrintingPolicy clang_getCursorPrintingPolicy(CXCursor);

/**
 * Release a printing policy.
 */
CINDEX_LINKAGE void clang_PrintingPolicy_dispose(CXPrintingPolicy Policy);

/**
 * Pretty print declarations.
 *
 * \param Cursor The cursor representing a declaration.
 *
 * \param Policy The policy to control the entities being printed. If
 * NULL, a default policy is used.
 *
 * \returns The pretty printed declaration or the empty string for
 * other cursors.
 */
CINDEX_LINKAGE CXString clang_getCursorPrettyPrinted(CXCursor Cursor,
                                                     CXPrintingPolicy Policy);

/**
 * Retrieve the display name for the entity referenced by this cursor.
 *
 * The display name contains extra information that helps identify the cursor,
 * such as the parameters of a function or template or the arguments of a
 * class template specialization.
 */
CINDEX_LINKAGE CXString clang_getCursorDisplayName(CXCursor);

/** For a cursor that is a reference, retrieve a cursor representing the
 * entity that it references.
 *
 * Reference cursors refer to other entities in the AST. For example, an
 * Objective-C superclass reference cursor refers to an Objective-C class.
 * This function produces the cursor for the Objective-C class from the
 * cursor for the superclass reference. If the input cursor is a declaration or
 * definition, it returns that declaration or definition unchanged.
 * Otherwise, returns the NULL cursor.
 */
CINDEX_LINKAGE CXCursor clang_getCursorReferenced(CXCursor);

/**
 *  For a cursor that is either a reference to or a declaration
 *  of some entity, retrieve a cursor that describes the definition of
 *  that entity.
 *
 *  Some entities can be declared multiple times within a translation
 *  unit, but only one of those declarations can also be a
 *  definition. For example, given:
 *
 *  \code
 *  int f(int, int);
 *  int g(int x, int y) { return f(x, y); }
 *  int f(int a, int b) { return a + b; }
 *  int f(int, int);
 *  \endcode
 *
 *  there are three declarations of the function "f", but only the
 *  second one is a definition. The clang_getCursorDefinition()
 *  function will take any cursor pointing to a declaration of "f"
 *  (the first or fourth lines of the example) or a cursor referenced
 *  that uses "f" (the call to "f' inside "g") and will return a
 *  declaration cursor pointing to the definition (the second "f"
 *  declaration).
 *
 *  If given a cursor for which there is no corresponding definition,
 *  e.g., because there is no definition of that entity within this
 *  translation unit, returns a NULL cursor.
 */
CINDEX_LINKAGE CXCursor clang_getCursorDefinition(CXCursor);

/**
 * Determine whether the declaration pointed to by this cursor
 * is also a definition of that entity.
 */
CINDEX_LINKAGE unsigned clang_isCursorDefinition(CXCursor);

/**
 * Retrieve the canonical cursor corresponding to the given cursor.
 *
 * In the C family of languages, many kinds of entities can be declared several
 * times within a single translation unit. For example, a structure type can
 * be forward-declared (possibly multiple times) and later defined:
 *
 * \code
 * struct X;
 * struct X;
 * struct X {
 *   int member;
 * };
 * \endcode
 *
 * The declarations and the definition of \c X are represented by three
 * different cursors, all of which are declarations of the same underlying
 * entity. One of these cursor is considered the "canonical" cursor, which
 * is effectively the representative for the underlying entity. One can
 * determine if two cursors are declarations of the same underlying entity by
 * comparing their canonical cursors.
 *
 * \returns The canonical cursor for the entity referred to by the given cursor.
 */
CINDEX_LINKAGE CXCursor clang_getCanonicalCursor(CXCursor);

/**
 * If the cursor points to a selector identifier in an Objective-C
 * method or message expression, this returns the selector index.
 *
 * After getting a cursor with #clang_getCursor, this can be called to
 * determine if the location points to a selector identifier.
 *
 * \returns The selector index if the cursor is an Objective-C method or message
 * expression and the cursor is pointing to a selector identifier, or -1
 * otherwise.
 */
CINDEX_LINKAGE int clang_Cursor_getObjCSelectorIndex(CXCursor);

/**
 * Given a cursor pointing to a C++ method call or an Objective-C
 * message, returns non-zero if the method/message is "dynamic", meaning:
 *
 * For a C++ method: the call is virtual.
 * For an Objective-C message: the receiver is an object instance, not 'super'
 * or a specific class.
 *
 * If the method/message is "static" or the cursor does not point to a
 * method/message, it will return zero.
 */
CINDEX_LINKAGE int clang_Cursor_isDynamicCall(CXCursor C);

/**
 * Given a cursor pointing to an Objective-C message or property
 * reference, or C++ method call, returns the CXType of the receiver.
 */
CINDEX_LINKAGE CXType clang_Cursor_getReceiverType(CXCursor C);

/**
 * Property attributes for a \c CXCursor_ObjCPropertyDecl.
 */
typedef enum {
  CXObjCPropertyAttr_noattr = 0x00,
  CXObjCPropertyAttr_readonly = 0x01,
  CXObjCPropertyAttr_getter = 0x02,
  CXObjCPropertyAttr_assign = 0x04,
  CXObjCPropertyAttr_readwrite = 0x08,
  CXObjCPropertyAttr_retain = 0x10,
  CXObjCPropertyAttr_copy = 0x20,
  CXObjCPropertyAttr_nonatomic = 0x40,
  CXObjCPropertyAttr_setter = 0x80,
  CXObjCPropertyAttr_atomic = 0x100,
  CXObjCPropertyAttr_weak = 0x200,
  CXObjCPropertyAttr_strong = 0x400,
  CXObjCPropertyAttr_unsafe_unretained = 0x800,
  CXObjCPropertyAttr_class = 0x1000
} CXObjCPropertyAttrKind;

/**
 * Given a cursor that represents a property declaration, return the
 * associated property attributes. The bits are formed from
 * \c CXObjCPropertyAttrKind.
 *
 * \param reserved Reserved for future use, pass 0.
 */
CINDEX_LINKAGE unsigned
clang_Cursor_getObjCPropertyAttributes(CXCursor C, unsigned reserved);

/**
 * Given a cursor that represents a property declaration, return the
 * name of the method that implements the getter.
 */
CINDEX_LINKAGE CXString clang_Cursor_getObjCPropertyGetterName(CXCursor C);

/**
 * Given a cursor that represents a property declaration, return the
 * name of the method that implements the setter, if any.
 */
CINDEX_LINKAGE CXString clang_Cursor_getObjCPropertySetterName(CXCursor C);

/**
 * 'Qualifiers' written next to the return and parameter types in
 * Objective-C method declarations.
 */
typedef enum {
  CXObjCDeclQualifier_None = 0x0,
  CXObjCDeclQualifier_In = 0x1,
  CXObjCDeclQualifier_Inout = 0x2,
  CXObjCDeclQualifier_Out = 0x4,
  CXObjCDeclQualifier_Bycopy = 0x8,
  CXObjCDeclQualifier_Byref = 0x10,
  CXObjCDeclQualifier_Oneway = 0x20
} CXObjCDeclQualifierKind;

/**
 * Given a cursor that represents an Objective-C method or parameter
 * declaration, return the associated Objective-C qualifiers for the return
 * type or the parameter respectively. The bits are formed from
 * CXObjCDeclQualifierKind.
 */
CINDEX_LINKAGE unsigned clang_Cursor_getObjCDeclQualifiers(CXCursor C);

/**
 * Given a cursor that represents an Objective-C method or property
 * declaration, return non-zero if the declaration was affected by "\@optional".
 * Returns zero if the cursor is not such a declaration or it is "\@required".
 */
CINDEX_LINKAGE unsigned clang_Cursor_isObjCOptional(CXCursor C);

/**
 * Returns non-zero if the given cursor is a variadic function or method.
 */
CINDEX_LINKAGE unsigned clang_Cursor_isVariadic(CXCursor C);

/**
 * Returns non-zero if the given cursor points to a symbol marked with
 * external_source_symbol attribute.
 *
 * \param language If non-NULL, and the attribute is present, will be set to
 * the 'language' string from the attribute.
 *
 * \param definedIn If non-NULL, and the attribute is present, will be set to
 * the 'definedIn' string from the attribute.
 *
 * \param isGenerated If non-NULL, and the attribute is present, will be set to
 * non-zero if the 'generated_declaration' is set in the attribute.
 */
CINDEX_LINKAGE unsigned clang_Cursor_isExternalSymbol(CXCursor C,
                                                      CXString *language,
                                                      CXString *definedIn,
                                                      unsigned *isGenerated);

/**
 * Given a cursor that represents a declaration, return the associated
 * comment's source range.  The range may include multiple consecutive comments
 * with whitespace in between.
 */
CINDEX_LINKAGE CXSourceRange clang_Cursor_getCommentRange(CXCursor C);

/**
 * Given a cursor that represents a declaration, return the associated
 * comment text, including comment markers.
 */
CINDEX_LINKAGE CXString clang_Cursor_getRawCommentText(CXCursor C);

/**
 * Given a cursor that represents a documentable entity (e.g.,
 * declaration), return the associated \paragraph; otherwise return the
 * first paragraph.
 */
CINDEX_LINKAGE CXString clang_Cursor_getBriefCommentText(CXCursor C);

/**
 * @}
 */

/** \defgroup CINDEX_MANGLE Name Mangling API Functions
 *
 * @{
 */

/**
 * Retrieve the CXString representing the mangled name of the cursor.
 */
CINDEX_LINKAGE CXString clang_Cursor_getMangling(CXCursor);

/**
 * Retrieve the CXStrings representing the mangled symbols of the C++
 * constructor or destructor at the cursor.
 */
CINDEX_LINKAGE CXStringSet *clang_Cursor_getCXXManglings(CXCursor);

/**
 * Retrieve the CXStrings representing the mangled symbols of the ObjC
 * class interface or implementation at the cursor.
 */
CINDEX_LINKAGE CXStringSet *clang_Cursor_getObjCManglings(CXCursor);

/**
 * @}
 */

/**
 * \defgroup CINDEX_MODULE Module introspection
 *
 * The functions in this group provide access to information about modules.
 *
 * @{
 */

typedef void *CXModule;

/**
 * Given a CXCursor_ModuleImportDecl cursor, return the associated module.
 */
CINDEX_LINKAGE CXModule clang_Cursor_getModule(CXCursor C);

/**
 * Given a CXFile header file, return the module that contains it, if one
 * exists.
 */
CINDEX_LINKAGE CXModule clang_getModuleForFile(CXTranslationUnit, CXFile);

/**
 * \param Module a module object.
 *
 * \returns the module file where the provided module object came from.
 */
CINDEX_LINKAGE CXFile clang_Module_getASTFile(CXModule Module);

/**
 * \param Module a module object.
 *
 * \returns the parent of a sub-module or NULL if the given module is top-level,
 * e.g. for 'std.vector' it will return the 'std' module.
 */
CINDEX_LINKAGE CXModule clang_Module_getParent(CXModule Module);

/**
 * \param Module a module object.
 *
 * \returns the name of the module, e.g. for the 'std.vector' sub-module it
 * will return "vector".
 */
CINDEX_LINKAGE CXString clang_Module_getName(CXModule Module);

/**
 * \param Module a module object.
 *
 * \returns the full name of the module, e.g. "std.vector".
 */
CINDEX_LINKAGE CXString clang_Module_getFullName(CXModule Module);

/**
 * \param Module a module object.
 *
 * \returns non-zero if the module is a system one.
 */
CINDEX_LINKAGE int clang_Module_isSystem(CXModule Module);

/**
 * \param Module a module object.
 *
 * \returns the number of top level headers associated with this module.
 */
CINDEX_LINKAGE unsigned clang_Module_getNumTopLevelHeaders(CXTranslationUnit,
                                                           CXModule Module);

/**
 * \param Module a module object.
 *
 * \param Index top level header index (zero-based).
 *
 * \returns the specified top level header associated with the module.
 */
CINDEX_LINKAGE
CXFile clang_Module_getTopLevelHeader(CXTranslationUnit, CXModule Module,
                                      unsigned Index);

/**
 * @}
 */

/**
 * \defgroup CINDEX_CPP C++ AST introspection
 *
 * The routines in this group provide access information in the ASTs specific
 * to C++ language features.
 *
 * @{
 */

/**
 * Determine if a C++ constructor is a converting constructor.
 */
CINDEX_LINKAGE unsigned
clang_CXXConstructor_isConvertingConstructor(CXCursor C);

/**
 * Determine if a C++ constructor is a copy constructor.
 */
CINDEX_LINKAGE unsigned clang_CXXConstructor_isCopyConstructor(CXCursor C);

/**
 * Determine if a C++ constructor is the default constructor.
 */
CINDEX_LINKAGE unsigned clang_CXXConstructor_isDefaultConstructor(CXCursor C);

/**
 * Determine if a C++ constructor is a move constructor.
 */
CINDEX_LINKAGE unsigned clang_CXXConstructor_isMoveConstructor(CXCursor C);

/**
 * Determine if a C++ field is declared 'mutable'.
 */
CINDEX_LINKAGE unsigned clang_CXXField_isMutable(CXCursor C);

/**
 * Determine if a C++ method is declared '= default'.
 */
CINDEX_LINKAGE unsigned clang_CXXMethod_isDefaulted(CXCursor C);

/**
 * Determine if a C++ method is declared '= delete'.
 */
CINDEX_LINKAGE unsigned clang_CXXMethod_isDeleted(CXCursor C);

/**
 * Determine if a C++ member function or member function template is
 * pure virtual.
 */
CINDEX_LINKAGE unsigned clang_CXXMethod_isPureVirtual(CXCursor C);

/**
 * Determine if a C++ member function or member function template is
 * declared 'static'.
 */
CINDEX_LINKAGE unsigned clang_CXXMethod_isStatic(CXCursor C);

/**
 * Determine if a C++ member function or member function template is
 * explicitly declared 'virtual' or if it overrides a virtual method from
 * one of the base classes.
 */
CINDEX_LINKAGE unsigned clang_CXXMethod_isVirtual(CXCursor C);

/**
 * Determine if a C++ member function is a copy-assignment operator,
 * returning 1 if such is the case and 0 otherwise.
 *
 * > A copy-assignment operator `X::operator=` is a non-static,
 * > non-template member function of _class_ `X` with exactly one
 * > parameter of type `X`, `X&`, `const X&`, `volatile X&` or `const
 * > volatile X&`.
 *
 * That is, for example, the `operator=` in:
 *
 *    class Foo {
 *        bool operator=(const volatile Foo&);
 *    };
 *
 * Is a copy-assignment operator, while the `operator=` in:
 *
 *    class Bar {
 *        bool operator=(const int&);
 *    };
 *
 * Is not.
 */
CINDEX_LINKAGE unsigned clang_CXXMethod_isCopyAssignmentOperator(CXCursor C);

/**
 * Determine if a C++ member function is a move-assignment operator,
 * returning 1 if such is the case and 0 otherwise.
 *
 * > A move-assignment operator `X::operator=` is a non-static,
 * > non-template member function of _class_ `X` with exactly one
 * > parameter of type `X&&`, `const X&&`, `volatile X&&` or `const
 * > volatile X&&`.
 *
 * That is, for example, the `operator=` in:
 *
 *    class Foo {
 *        bool operator=(const volatile Foo&&);
 *    };
 *
 * Is a move-assignment operator, while the `operator=` in:
 *
 *    class Bar {
 *        bool operator=(const int&&);
 *    };
 *
 * Is not.
 */
CINDEX_LINKAGE unsigned clang_CXXMethod_isMoveAssignmentOperator(CXCursor C);

/**
 * Determines if a C++ constructor or conversion function was declared
 * explicit, returning 1 if such is the case and 0 otherwise.
 *
 * Constructors or conversion functions are declared explicit through
 * the use of the explicit specifier.
 *
 * For example, the following constructor and conversion function are
 * not explicit as they lack the explicit specifier:
 *
 *     class Foo {
 *         Foo();
 *         operator int();
 *     };
 *
 * While the following constructor and conversion function are
 * explicit as they are declared with the explicit specifier.
 *
 *     class Foo {
 *         explicit Foo();
 *         explicit operator int();
 *     };
 *
 * This function will return 0 when given a cursor pointing to one of
 * the former declarations and it will return 1 for a cursor pointing
 * to the latter declarations.
 *
 * The explicit specifier allows the user to specify a
 * conditional compile-time expression whose value decides
 * whether the marked element is explicit or not.
 *
 * For example:
 *
 *     constexpr bool foo(int i) { return i % 2 == 0; }
 *
 *     class Foo {
 *          explicit(foo(1)) Foo();
 *          explicit(foo(2)) operator int();
 *     }
 *
 * This function will return 0 for the constructor and 1 for
 * the conversion function.
 */
CINDEX_LINKAGE unsigned clang_CXXMethod_isExplicit(CXCursor C);

/**
 * Determine if a C++ record is abstract, i.e. whether a class or struct
 * has a pure virtual member function.
 */
CINDEX_LINKAGE unsigned clang_CXXRecord_isAbstract(CXCursor C);

/**
 * Determine if an enum declaration refers to a scoped enum.
 */
CINDEX_LINKAGE unsigned clang_EnumDecl_isScoped(CXCursor C);

/**
 * Determine if a C++ member function or member function template is
 * declared 'const'.
 */
CINDEX_LINKAGE unsigned clang_CXXMethod_isConst(CXCursor C);

/**
 * Given a cursor that represents a template, determine
 * the cursor kind of the specializations would be generated by instantiating
 * the template.
 *
 * This routine can be used to determine what flavor of function template,
 * class template, or class template partial specialization is stored in the
 * cursor. For example, it can describe whether a class template cursor is
 * declared with "struct", "class" or "union".
 *
 * \param C The cursor to query. This cursor should represent a template
 * declaration.
 *
 * \returns The cursor kind of the specializations that would be generated
 * by instantiating the template \p C. If \p C is not a template, returns
 * \c CXCursor_NoDeclFound.
 */
CINDEX_LINKAGE enum CXCursorKind clang_getTemplateCursorKind(CXCursor C);

/**
 * Given a cursor that may represent a specialization or instantiation
 * of a template, retrieve the cursor that represents the template that it
 * specializes or from which it was instantiated.
 *
 * This routine determines the template involved both for explicit
 * specializations of templates and for implicit instantiations of the template,
 * both of which are referred to as "specializations". For a class template
 * specialization (e.g., \c std::vector<bool>), this routine will return
 * either the primary template (\c std::vector) or, if the specialization was
 * instantiated from a class template partial specialization, the class template
 * partial specialization. For a class template partial specialization and a
 * function template specialization (including instantiations), this
 * this routine will return the specialized template.
 *
 * For members of a class template (e.g., member functions, member classes, or
 * static data members), returns the specialized or instantiated member.
 * Although not strictly "templates" in the C++ language, members of class
 * templates have the same notions of specializations and instantiations that
 * templates do, so this routine treats them similarly.
 *
 * \param C A cursor that may be a specialization of a template or a member
 * of a template.
 *
 * \returns If the given cursor is a specialization or instantiation of a
 * template or a member thereof, the template or member that it specializes or
 * from which it was instantiated. Otherwise, returns a NULL cursor.
 */
CINDEX_LINKAGE CXCursor clang_getSpecializedCursorTemplate(CXCursor C);

/**
 * Given a cursor that references something else, return the source range
 * covering that reference.
 *
 * \param C A cursor pointing to a member reference, a declaration reference, or
 * an operator call.
 * \param NameFlags A bitset with three independent flags:
 * CXNameRange_WantQualifier, CXNameRange_WantTemplateArgs, and
 * CXNameRange_WantSinglePiece.
 * \param PieceIndex For contiguous names or when passing the flag
 * CXNameRange_WantSinglePiece, only one piece with index 0 is
 * available. When the CXNameRange_WantSinglePiece flag is not passed for a
 * non-contiguous names, this index can be used to retrieve the individual
 * pieces of the name. See also CXNameRange_WantSinglePiece.
 *
 * \returns The piece of the name pointed to by the given cursor. If there is no
 * name, or if the PieceIndex is out-of-range, a null-cursor will be returned.
 */
CINDEX_LINKAGE CXSourceRange clang_getCursorReferenceNameRange(
    CXCursor C, unsigned NameFlags, unsigned PieceIndex);

enum CXNameRefFlags {
  /**
   * Include the nested-name-specifier, e.g. Foo:: in x.Foo::y, in the
   * range.
   */
  CXNameRange_WantQualifier = 0x1,

  /**
   * Include the explicit template arguments, e.g. \<int> in x.f<int>,
   * in the range.
   */
  CXNameRange_WantTemplateArgs = 0x2,

  /**
   * If the name is non-contiguous, return the full spanning range.
   *
   * Non-contiguous names occur in Objective-C when a selector with two or more
   * parameters is used, or in C++ when using an operator:
   * \code
   * [object doSomething:here withValue:there]; // Objective-C
   * return some_vector[1]; // C++
   * \endcode
   */
  CXNameRange_WantSinglePiece = 0x4
};

/**
 * @}
 */

/**
 * \defgroup CINDEX_LEX Token extraction and manipulation
 *
 * The routines in this group provide access to the tokens within a
 * translation unit, along with a semantic mapping of those tokens to
 * their corresponding cursors.
 *
 * @{
 */

/**
 * Describes a kind of token.
 */
typedef enum CXTokenKind {
  /**
   * A token that contains some kind of punctuation.
   */
  CXToken_Punctuation,

  /**
   * A language keyword.
   */
  CXToken_Keyword,

  /**
   * An identifier (that is not a keyword).
   */
  CXToken_Identifier,

  /**
   * A numeric, string, or character literal.
   */
  CXToken_Literal,

  /**
   * A comment.
   */
  CXToken_Comment
} CXTokenKind;

/**
 * Describes a single preprocessing token.
 */
typedef struct {
  unsigned int_data[4];
  void *ptr_data;
} CXToken;

/**
 * Get the raw lexical token starting with the given location.
 *
 * \param TU the translation unit whose text is being tokenized.
 *
 * \param Location the source location with which the token starts.
 *
 * \returns The token starting with the given location or NULL if no such token
 * exist. The returned pointer must be freed with clang_disposeTokens before the
 * translation unit is destroyed.
 */
CINDEX_LINKAGE CXToken *clang_getToken(CXTranslationUnit TU,
                                       CXSourceLocation Location);

/**
 * Determine the kind of the given token.
 */
CINDEX_LINKAGE CXTokenKind clang_getTokenKind(CXToken);

/**
 * Determine the spelling of the given token.
 *
 * The spelling of a token is the textual representation of that token, e.g.,
 * the text of an identifier or keyword.
 */
CINDEX_LINKAGE CXString clang_getTokenSpelling(CXTranslationUnit, CXToken);

/**
 * Retrieve the source location of the given token.
 */
CINDEX_LINKAGE CXSourceLocation clang_getTokenLocation(CXTranslationUnit,
                                                       CXToken);

/**
 * Retrieve a source range that covers the given token.
 */
CINDEX_LINKAGE CXSourceRange clang_getTokenExtent(CXTranslationUnit, CXToken);

/**
 * Tokenize the source code described by the given range into raw
 * lexical tokens.
 *
 * \param TU the translation unit whose text is being tokenized.
 *
 * \param Range the source range in which text should be tokenized. All of the
 * tokens produced by tokenization will fall within this source range,
 *
 * \param Tokens this pointer will be set to point to the array of tokens
 * that occur within the given source range. The returned pointer must be
 * freed with clang_disposeTokens() before the translation unit is destroyed.
 *
 * \param NumTokens will be set to the number of tokens in the \c *Tokens
 * array.
 *
 */
CINDEX_LINKAGE void clang_tokenize(CXTranslationUnit TU, CXSourceRange Range,
                                   CXToken **Tokens, unsigned *NumTokens);

/**
 * Annotate the given set of tokens by providing cursors for each token
 * that can be mapped to a specific entity within the abstract syntax tree.
 *
 * This token-annotation routine is equivalent to invoking
 * clang_getCursor() for the source locations of each of the
 * tokens. The cursors provided are filtered, so that only those
 * cursors that have a direct correspondence to the token are
 * accepted. For example, given a function call \c f(x),
 * clang_getCursor() would provide the following cursors:
 *
 *   * when the cursor is over the 'f', a DeclRefExpr cursor referring to 'f'.
 *   * when the cursor is over the '(' or the ')', a CallExpr referring to 'f'.
 *   * when the cursor is over the 'x', a DeclRefExpr cursor referring to 'x'.
 *
 * Only the first and last of these cursors will occur within the
 * annotate, since the tokens "f" and "x' directly refer to a function
 * and a variable, respectively, but the parentheses are just a small
 * part of the full syntax of the function call expression, which is
 * not provided as an annotation.
 *
 * \param TU the translation unit that owns the given tokens.
 *
 * \param Tokens the set of tokens to annotate.
 *
 * \param NumTokens the number of tokens in \p Tokens.
 *
 * \param Cursors an array of \p NumTokens cursors, whose contents will be
 * replaced with the cursors corresponding to each token.
 */
CINDEX_LINKAGE void clang_annotateTokens(CXTranslationUnit TU, CXToken *Tokens,
                                         unsigned NumTokens, CXCursor *Cursors);

/**
 * Free the given set of tokens.
 */
CINDEX_LINKAGE void clang_disposeTokens(CXTranslationUnit TU, CXToken *Tokens,
                                        unsigned NumTokens);

/**
 * @}
 */

/**
 * \defgroup CINDEX_DEBUG Debugging facilities
 *
 * These routines are used for testing and debugging, only, and should not
 * be relied upon.
 *
 * @{
 */

/* for debug/testing */
CINDEX_LINKAGE CXString clang_getCursorKindSpelling(enum CXCursorKind Kind);
CINDEX_LINKAGE void clang_getDefinitionSpellingAndExtent(
    CXCursor, const char **startBuf, const char **endBuf, unsigned *startLine,
    unsigned *startColumn, unsigned *endLine, unsigned *endColumn);
CINDEX_LINKAGE void clang_enableStackTraces(void);
CINDEX_LINKAGE void clang_executeOnThread(void (*fn)(void *), void *user_data,
                                          unsigned stack_size);

/**
 * @}
 */

/**
 * \defgroup CINDEX_CODE_COMPLET Code completion
 *
 * Code completion involves taking an (incomplete) source file, along with
 * knowledge of where the user is actively editing that file, and suggesting
 * syntactically- and semantically-valid constructs that the user might want to
 * use at that particular point in the source code. These data structures and
 * routines provide support for code completion.
 *
 * @{
 */

/**
 * A semantic string that describes a code-completion result.
 *
 * A semantic string that describes the formatting of a code-completion
 * result as a single "template" of text that should be inserted into the
 * source buffer when a particular code-completion result is selected.
 * Each semantic string is made up of some number of "chunks", each of which
 * contains some text along with a description of what that text means, e.g.,
 * the name of the entity being referenced, whether the text chunk is part of
 * the template, or whether it is a "placeholder" that the user should replace
 * with actual code,of a specific kind. See \c CXCompletionChunkKind for a
 * description of the different kinds of chunks.
 */
typedef void *CXCompletionString;

/**
 * A single result of code completion.
 */
typedef struct {
  /**
   * The kind of entity that this completion refers to.
   *
   * The cursor kind will be a macro, keyword, or a declaration (one of the
   * *Decl cursor kinds), describing the entity that the completion is
   * referring to.
   *
   * \todo In the future, we would like to provide a full cursor, to allow
   * the client to extract additional information from declaration.
   */
  enum CXCursorKind CursorKind;

  /**
   * The code-completion string that describes how to insert this
   * code-completion result into the editing buffer.
   */
  CXCompletionString CompletionString;
} CXCompletionResult;

/**
 * Describes a single piece of text within a code-completion string.
 *
 * Each "chunk" within a code-completion string (\c CXCompletionString) is
 * either a piece of text with a specific "kind" that describes how that text
 * should be interpreted by the client or is another completion string.
 */
enum CXCompletionChunkKind {
  /**
   * A code-completion string that describes "optional" text that
   * could be a part of the template (but is not required).
   *
   * The Optional chunk is the only kind of chunk that has a code-completion
   * string for its representation, which is accessible via
   * \c clang_getCompletionChunkCompletionString(). The code-completion string
   * describes an additional part of the template that is completely optional.
   * For example, optional chunks can be used to describe the placeholders for
   * arguments that match up with defaulted function parameters, e.g. given:
   *
   * \code
   * void f(int x, float y = 3.14, double z = 2.71828);
   * \endcode
   *
   * The code-completion string for this function would contain:
   *   - a TypedText chunk for "f".
   *   - a LeftParen chunk for "(".
   *   - a Placeholder chunk for "int x"
   *   - an Optional chunk containing the remaining defaulted arguments, e.g.,
   *       - a Comma chunk for ","
   *       - a Placeholder chunk for "float y"
   *       - an Optional chunk containing the last defaulted argument:
   *           - a Comma chunk for ","
   *           - a Placeholder chunk for "double z"
   *   - a RightParen chunk for ")"
   *
   * There are many ways to handle Optional chunks. Two simple approaches are:
   *   - Completely ignore optional chunks, in which case the template for the
   *     function "f" would only include the first parameter ("int x").
   *   - Fully expand all optional chunks, in which case the template for the
   *     function "f" would have all of the parameters.
   */
  CXCompletionChunk_Optional,
  /**
   * Text that a user would be expected to type to get this
   * code-completion result.
   *
   * There will be exactly one "typed text" chunk in a semantic string, which
   * will typically provide the spelling of a keyword or the name of a
   * declaration that could be used at the current code point. Clients are
   * expected to filter the code-completion results based on the text in this
   * chunk.
   */
  CXCompletionChunk_TypedText,
  /**
   * Text that should be inserted as part of a code-completion result.
   *
   * A "text" chunk represents text that is part of the template to be
   * inserted into user code should this particular code-completion result
   * be selected.
   */
  CXCompletionChunk_Text,
  /**
   * Placeholder text that should be replaced by the user.
   *
   * A "placeholder" chunk marks a place where the user should insert text
   * into the code-completion template. For example, placeholders might mark
   * the function parameters for a function declaration, to indicate that the
   * user should provide arguments for each of those parameters. The actual
   * text in a placeholder is a suggestion for the text to display before
   * the user replaces the placeholder with real code.
   */
  CXCompletionChunk_Placeholder,
  /**
   * Informative text that should be displayed but never inserted as
   * part of the template.
   *
   * An "informative" chunk contains annotations that can be displayed to
   * help the user decide whether a particular code-completion result is the
   * right option, but which is not part of the actual template to be inserted
   * by code completion.
   */
  CXCompletionChunk_Informative,
  /**
   * Text that describes the current parameter when code-completion is
   * referring to function call, message send, or template specialization.
   *
   * A "current parameter" chunk occurs when code-completion is providing
   * information about a parameter corresponding to the argument at the
   * code-completion point. For example, given a function
   *
   * \code
   * int add(int x, int y);
   * \endcode
   *
   * and the source code \c add(, where the code-completion point is after the
   * "(", the code-completion string will contain a "current parameter" chunk
   * for "int x", indicating that the current argument will initialize that
   * parameter. After typing further, to \c add(17, (where the code-completion
   * point is after the ","), the code-completion string will contain a
   * "current parameter" chunk to "int y".
   */
  CXCompletionChunk_CurrentParameter,
  /**
   * A left parenthesis ('('), used to initiate a function call or
   * signal the beginning of a function parameter list.
   */
  CXCompletionChunk_LeftParen,
  /**
   * A right parenthesis (')'), used to finish a function call or
   * signal the end of a function parameter list.
   */
  CXCompletionChunk_RightParen,
  /**
   * A left bracket ('[').
   */
  CXCompletionChunk_LeftBracket,
  /**
   * A right bracket (']').
   */
  CXCompletionChunk_RightBracket,
  /**
   * A left brace ('{').
   */
  CXCompletionChunk_LeftBrace,
  /**
   * A right brace ('}').
   */
  CXCompletionChunk_RightBrace,
  /**
   * A left angle bracket ('<').
   */
  CXCompletionChunk_LeftAngle,
  /**
   * A right angle bracket ('>').
   */
  CXCompletionChunk_RightAngle,
  /**
   * A comma separator (',').
   */
  CXCompletionChunk_Comma,
  /**
   * Text that specifies the result type of a given result.
   *
   * This special kind of informative chunk is not meant to be inserted into
   * the text buffer. Rather, it is meant to illustrate the type that an
   * expression using the given completion string would have.
   */
  CXCompletionChunk_ResultType,
  /**
   * A colon (':').
   */
  CXCompletionChunk_Colon,
  /**
   * A semicolon (';').
   */
  CXCompletionChunk_SemiColon,
  /**
   * An '=' sign.
   */
  CXCompletionChunk_Equal,
  /**
   * Horizontal space (' ').
   */
  CXCompletionChunk_HorizontalSpace,
  /**
   * Vertical space ('\\n'), after which it is generally a good idea to
   * perform indentation.
   */
  CXCompletionChunk_VerticalSpace
};

/**
 * Determine the kind of a particular chunk within a completion string.
 *
 * \param completion_string the completion string to query.
 *
 * \param chunk_number the 0-based index of the chunk in the completion string.
 *
 * \returns the kind of the chunk at the index \c chunk_number.
 */
CINDEX_LINKAGE enum CXCompletionChunkKind
clang_getCompletionChunkKind(CXCompletionString completion_string,
                             unsigned chunk_number);

/**
 * Retrieve the text associated with a particular chunk within a
 * completion string.
 *
 * \param completion_string the completion string to query.
 *
 * \param chunk_number the 0-based index of the chunk in the completion string.
 *
 * \returns the text associated with the chunk at index \c chunk_number.
 */
CINDEX_LINKAGE CXString clang_getCompletionChunkText(
    CXCompletionString completion_string, unsigned chunk_number);

/**
 * Retrieve the completion string associated with a particular chunk
 * within a completion string.
 *
 * \param completion_string the completion string to query.
 *
 * \param chunk_number the 0-based index of the chunk in the completion string.
 *
 * \returns the completion string associated with the chunk at index
 * \c chunk_number.
 */
CINDEX_LINKAGE CXCompletionString clang_getCompletionChunkCompletionString(
    CXCompletionString completion_string, unsigned chunk_number);

/**
 * Retrieve the number of chunks in the given code-completion string.
 */
CINDEX_LINKAGE unsigned
clang_getNumCompletionChunks(CXCompletionString completion_string);

/**
 * Determine the priority of this code completion.
 *
 * The priority of a code completion indicates how likely it is that this
 * particular completion is the completion that the user will select. The
 * priority is selected by various internal heuristics.
 *
 * \param completion_string The completion string to query.
 *
 * \returns The priority of this completion string. Smaller values indicate
 * higher-priority (more likely) completions.
 */
CINDEX_LINKAGE unsigned
clang_getCompletionPriority(CXCompletionString completion_string);

/**
 * Determine the availability of the entity that this code-completion
 * string refers to.
 *
 * \param completion_string The completion string to query.
 *
 * \returns The availability of the completion string.
 */
CINDEX_LINKAGE enum CXAvailabilityKind
clang_getCompletionAvailability(CXCompletionString completion_string);

/**
 * Retrieve the number of annotations associated with the given
 * completion string.
 *
 * \param completion_string the completion string to query.
 *
 * \returns the number of annotations associated with the given completion
 * string.
 */
CINDEX_LINKAGE unsigned
clang_getCompletionNumAnnotations(CXCompletionString completion_string);

/**
 * Retrieve the annotation associated with the given completion string.
 *
 * \param completion_string the completion string to query.
 *
 * \param annotation_number the 0-based index of the annotation of the
 * completion string.
 *
 * \returns annotation string associated with the completion at index
 * \c annotation_number, or a NULL string if that annotation is not available.
 */
CINDEX_LINKAGE CXString clang_getCompletionAnnotation(
    CXCompletionString completion_string, unsigned annotation_number);

/**
 * Retrieve the parent context of the given completion string.
 *
 * The parent context of a completion string is the semantic parent of
 * the declaration (if any) that the code completion represents. For example,
 * a code completion for an Objective-C method would have the method's class
 * or protocol as its context.
 *
 * \param completion_string The code completion string whose parent is
 * being queried.
 *
 * \param kind DEPRECATED: always set to CXCursor_NotImplemented if non-NULL.
 *
 * \returns The name of the completion parent, e.g., "NSObject" if
 * the completion string represents a method in the NSObject class.
 */
CINDEX_LINKAGE CXString clang_getCompletionParent(
    CXCompletionString completion_string, enum CXCursorKind *kind);

/**
 * Retrieve the brief documentation comment attached to the declaration
 * that corresponds to the given completion string.
 */
CINDEX_LINKAGE CXString
clang_getCompletionBriefComment(CXCompletionString completion_string);

/**
 * Retrieve a completion string for an arbitrary declaration or macro
 * definition cursor.
 *
 * \param cursor The cursor to query.
 *
 * \returns A non-context-sensitive completion string for declaration and macro
 * definition cursors, or NULL for other kinds of cursors.
 */
CINDEX_LINKAGE CXCompletionString
clang_getCursorCompletionString(CXCursor cursor);

/**
 * Contains the results of code-completion.
 *
 * This data structure contains the results of code completion, as
 * produced by \c clang_codeCompleteAt(). Its contents must be freed by
 * \c clang_disposeCodeCompleteResults.
 */
typedef struct {
  /**
   * The code-completion results.
   */
  CXCompletionResult *Results;

  /**
   * The number of code-completion results stored in the
   * \c Results array.
   */
  unsigned NumResults;
} CXCodeCompleteResults;

/**
 * Retrieve the number of fix-its for the given completion index.
 *
 * Calling this makes sense only if CXCodeComplete_IncludeCompletionsWithFixIts
 * option was set.
 *
 * \param results The structure keeping all completion results
 *
 * \param completion_index The index of the completion
 *
 * \return The number of fix-its which must be applied before the completion at
 * completion_index can be applied
 */
CINDEX_LINKAGE unsigned
clang_getCompletionNumFixIts(CXCodeCompleteResults *results,
                             unsigned completion_index);

/**
 * Fix-its that *must* be applied before inserting the text for the
 * corresponding completion.
 *
 * By default, clang_codeCompleteAt() only returns completions with empty
 * fix-its. Extra completions with non-empty fix-its should be explicitly
 * requested by setting CXCodeComplete_IncludeCompletionsWithFixIts.
 *
 * For the clients to be able to compute position of the cursor after applying
 * fix-its, the following conditions are guaranteed to hold for
 * replacement_range of the stored fix-its:
 *  - Ranges in the fix-its are guaranteed to never contain the completion
 *  point (or identifier under completion point, if any) inside them, except
 *  at the start or at the end of the range.
 *  - If a fix-it range starts or ends with completion point (or starts or
 *  ends after the identifier under completion point), it will contain at
 *  least one character. It allows to unambiguously recompute completion
 *  point after applying the fix-it.
 *
 * The intuition is that provided fix-its change code around the identifier we
 * complete, but are not allowed to touch the identifier itself or the
 * completion point. One example of completions with corrections are the ones
 * replacing '.' with '->' and vice versa:
 *
 * std::unique_ptr<std::vector<int>> vec_ptr;
 * In 'vec_ptr.^', one of the completions is 'push_back', it requires
 * replacing '.' with '->'.
 * In 'vec_ptr->^', one of the completions is 'release', it requires
 * replacing '->' with '.'.
 *
 * \param results The structure keeping all completion results
 *
 * \param completion_index The index of the completion
 *
 * \param fixit_index The index of the fix-it for the completion at
 * completion_index
 *
 * \param replacement_range The fix-it range that must be replaced before the
 * completion at completion_index can be applied
 *
 * \returns The fix-it string that must replace the code at replacement_range
 * before the completion at completion_index can be applied
 */
CINDEX_LINKAGE CXString clang_getCompletionFixIt(
    CXCodeCompleteResults *results, unsigned completion_index,
    unsigned fixit_index, CXSourceRange *replacement_range);

/**
 * Flags that can be passed to \c clang_codeCompleteAt() to
 * modify its behavior.
 *
 * The enumerators in this enumeration can be bitwise-OR'd together to
 * provide multiple options to \c clang_codeCompleteAt().
 */
enum CXCodeComplete_Flags {
  /**
   * Whether to include macros within the set of code
   * completions returned.
   */
  CXCodeComplete_IncludeMacros = 0x01,

  /**
   * Whether to include code patterns for language constructs
   * within the set of code completions, e.g., for loops.
   */
  CXCodeComplete_IncludeCodePatterns = 0x02,

  /**
   * Whether to include brief documentation within the set of code
   * completions returned.
   */
  CXCodeComplete_IncludeBriefComments = 0x04,

  /**
   * Whether to speed up completion by omitting top- or namespace-level entities
   * defined in the preamble. There's no guarantee any particular entity is
   * omitted. This may be useful if the headers are indexed externally.
   */
  CXCodeComplete_SkipPreamble = 0x08,

  /**
   * Whether to include completions with small
   * fix-its, e.g. change '.' to '->' on member access, etc.
   */
  CXCodeComplete_IncludeCompletionsWithFixIts = 0x10
};

/**
 * Bits that represent the context under which completion is occurring.
 *
 * The enumerators in this enumeration may be bitwise-OR'd together if multiple
 * contexts are occurring simultaneously.
 */
enum CXCompletionContext {
  /**
   * The context for completions is unexposed, as only Clang results
   * should be included. (This is equivalent to having no context bits set.)
   */
  CXCompletionContext_Unexposed = 0,

  /**
   * Completions for any possible type should be included in the results.
   */
  CXCompletionContext_AnyType = 1 << 0,

  /**
   * Completions for any possible value (variables, function calls, etc.)
   * should be included in the results.
   */
  CXCompletionContext_AnyValue = 1 << 1,
  /**
   * Completions for values that resolve to an Objective-C object should
   * be included in the results.
   */
  CXCompletionContext_ObjCObjectValue = 1 << 2,
  /**
   * Completions for values that resolve to an Objective-C selector
   * should be included in the results.
   */
  CXCompletionContext_ObjCSelectorValue = 1 << 3,
  /**
   * Completions for values that resolve to a C++ class type should be
   * included in the results.
   */
  CXCompletionContext_CXXClassTypeValue = 1 << 4,

  /**
   * Completions for fields of the member being accessed using the dot
   * operator should be included in the results.
   */
  CXCompletionContext_DotMemberAccess = 1 << 5,
  /**
   * Completions for fields of the member being accessed using the arrow
   * operator should be included in the results.
   */
  CXCompletionContext_ArrowMemberAccess = 1 << 6,
  /**
   * Completions for properties of the Objective-C object being accessed
   * using the dot operator should be included in the results.
   */
  CXCompletionContext_ObjCPropertyAccess = 1 << 7,

  /**
   * Completions for enum tags should be included in the results.
   */
  CXCompletionContext_EnumTag = 1 << 8,
  /**
   * Completions for union tags should be included in the results.
   */
  CXCompletionContext_UnionTag = 1 << 9,
  /**
   * Completions for struct tags should be included in the results.
   */
  CXCompletionContext_StructTag = 1 << 10,

  /**
   * Completions for C++ class names should be included in the results.
   */
  CXCompletionContext_ClassTag = 1 << 11,
  /**
   * Completions for C++ namespaces and namespace aliases should be
   * included in the results.
   */
  CXCompletionContext_Namespace = 1 << 12,
  /**
   * Completions for C++ nested name specifiers should be included in
   * the results.
   */
  CXCompletionContext_NestedNameSpecifier = 1 << 13,

  /**
   * Completions for Objective-C interfaces (classes) should be included
   * in the results.
   */
  CXCompletionContext_ObjCInterface = 1 << 14,
  /**
   * Completions for Objective-C protocols should be included in
   * the results.
   */
  CXCompletionContext_ObjCProtocol = 1 << 15,
  /**
   * Completions for Objective-C categories should be included in
   * the results.
   */
  CXCompletionContext_ObjCCategory = 1 << 16,
  /**
   * Completions for Objective-C instance messages should be included
   * in the results.
   */
  CXCompletionContext_ObjCInstanceMessage = 1 << 17,
  /**
   * Completions for Objective-C class messages should be included in
   * the results.
   */
  CXCompletionContext_ObjCClassMessage = 1 << 18,
  /**
   * Completions for Objective-C selector names should be included in
   * the results.
   */
  CXCompletionContext_ObjCSelectorName = 1 << 19,

  /**
   * Completions for preprocessor macro names should be included in
   * the results.
   */
  CXCompletionContext_MacroName = 1 << 20,

  /**
   * Natural language completions should be included in the results.
   */
  CXCompletionContext_NaturalLanguage = 1 << 21,

  /**
   * #include file completions should be included in the results.
   */
  CXCompletionContext_IncludedFile = 1 << 22,

  /**
   * The current context is unknown, so set all contexts.
   */
  CXCompletionContext_Unknown = ((1 << 23) - 1)
};

/**
 * Returns a default set of code-completion options that can be
 * passed to\c clang_codeCompleteAt().
 */
CINDEX_LINKAGE unsigned clang_defaultCodeCompleteOptions(void);

/**
 * Perform code completion at a given location in a translation unit.
 *
 * This function performs code completion at a particular file, line, and
 * column within source code, providing results that suggest potential
 * code snippets based on the context of the completion. The basic model
 * for code completion is that Clang will parse a complete source file,
 * performing syntax checking up to the location where code-completion has
 * been requested. At that point, a special code-completion token is passed
 * to the parser, which recognizes this token and determines, based on the
 * current location in the C/Objective-C/C++ grammar and the state of
 * semantic analysis, what completions to provide. These completions are
 * returned via a new \c CXCodeCompleteResults structure.
 *
 * Code completion itself is meant to be triggered by the client when the
 * user types punctuation characters or whitespace, at which point the
 * code-completion location will coincide with the cursor. For example, if \c p
 * is a pointer, code-completion might be triggered after the "-" and then
 * after the ">" in \c p->. When the code-completion location is after the ">",
 * the completion results will provide, e.g., the members of the struct that
 * "p" points to. The client is responsible for placing the cursor at the
 * beginning of the token currently being typed, then filtering the results
 * based on the contents of the token. For example, when code-completing for
 * the expression \c p->get, the client should provide the location just after
 * the ">" (e.g., pointing at the "g") to this code-completion hook. Then, the
 * client can filter the results based on the current token text ("get"), only
 * showing those results that start with "get". The intent of this interface
 * is to separate the relatively high-latency acquisition of code-completion
 * results from the filtering of results on a per-character basis, which must
 * have a lower latency.
 *
 * \param TU The translation unit in which code-completion should
 * occur. The source files for this translation unit need not be
 * completely up-to-date (and the contents of those source files may
 * be overridden via \p unsaved_files). Cursors referring into the
 * translation unit may be invalidated by this invocation.
 *
 * \param complete_filename The name of the source file where code
 * completion should be performed. This filename may be any file
 * included in the translation unit.
 *
 * \param complete_line The line at which code-completion should occur.
 *
 * \param complete_column The column at which code-completion should occur.
 * Note that the column should point just after the syntactic construct that
 * initiated code completion, and not in the middle of a lexical token.
 *
 * \param unsaved_files the Files that have not yet been saved to disk
 * but may be required for parsing or code completion, including the
 * contents of those files.  The contents and name of these files (as
 * specified by CXUnsavedFile) are copied when necessary, so the
 * client only needs to guarantee their validity until the call to
 * this function returns.
 *
 * \param num_unsaved_files The number of unsaved file entries in \p
 * unsaved_files.
 *
 * \param options Extra options that control the behavior of code
 * completion, expressed as a bitwise OR of the enumerators of the
 * CXCodeComplete_Flags enumeration. The
 * \c clang_defaultCodeCompleteOptions() function returns a default set
 * of code-completion options.
 *
 * \returns If successful, a new \c CXCodeCompleteResults structure
 * containing code-completion results, which should eventually be
 * freed with \c clang_disposeCodeCompleteResults(). If code
 * completion fails, returns NULL.
 */
CINDEX_LINKAGE
CXCodeCompleteResults *
clang_codeCompleteAt(CXTranslationUnit TU, const char *complete_filename,
                     unsigned complete_line, unsigned complete_column,
                     struct CXUnsavedFile *unsaved_files,
                     unsigned num_unsaved_files, unsigned options);

/**
 * Sort the code-completion results in case-insensitive alphabetical
 * order.
 *
 * \param Results The set of results to sort.
 * \param NumResults The number of results in \p Results.
 */
CINDEX_LINKAGE
void clang_sortCodeCompletionResults(CXCompletionResult *Results,
                                     unsigned NumResults);

/**
 * Free the given set of code-completion results.
 */
CINDEX_LINKAGE
void clang_disposeCodeCompleteResults(CXCodeCompleteResults *Results);

/**
 * Determine the number of diagnostics produced prior to the
 * location where code completion was performed.
 */
CINDEX_LINKAGE
unsigned clang_codeCompleteGetNumDiagnostics(CXCodeCompleteResults *Results);

/**
 * Retrieve a diagnostic associated with the given code completion.
 *
 * \param Results the code completion results to query.
 * \param Index the zero-based diagnostic number to retrieve.
 *
 * \returns the requested diagnostic. This diagnostic must be freed
 * via a call to \c clang_disposeDiagnostic().
 */
CINDEX_LINKAGE
CXDiagnostic clang_codeCompleteGetDiagnostic(CXCodeCompleteResults *Results,
                                             unsigned Index);

/**
 * Determines what completions are appropriate for the context
 * the given code completion.
 *
 * \param Results the code completion results to query
 *
 * \returns the kinds of completions that are appropriate for use
 * along with the given code completion results.
 */
CINDEX_LINKAGE
unsigned long long
clang_codeCompleteGetContexts(CXCodeCompleteResults *Results);

/**
 * Returns the cursor kind for the container for the current code
 * completion context. The container is only guaranteed to be set for
 * contexts where a container exists (i.e. member accesses or Objective-C
 * message sends); if there is not a container, this function will return
 * CXCursor_InvalidCode.
 *
 * \param Results the code completion results to query
 *
 * \param IsIncomplete on return, this value will be false if Clang has complete
 * information about the container. If Clang does not have complete
 * information, this value will be true.
 *
 * \returns the container kind, or CXCursor_InvalidCode if there is not a
 * container
 */
CINDEX_LINKAGE
enum CXCursorKind
clang_codeCompleteGetContainerKind(CXCodeCompleteResults *Results,
                                   unsigned *IsIncomplete);

/**
 * Returns the USR for the container for the current code completion
 * context. If there is not a container for the current context, this
 * function will return the empty string.
 *
 * \param Results the code completion results to query
 *
 * \returns the USR for the container
 */
CINDEX_LINKAGE
CXString clang_codeCompleteGetContainerUSR(CXCodeCompleteResults *Results);

/**
 * Returns the currently-entered selector for an Objective-C message
 * send, formatted like "initWithFoo:bar:". Only guaranteed to return a
 * non-empty string for CXCompletionContext_ObjCInstanceMessage and
 * CXCompletionContext_ObjCClassMessage.
 *
 * \param Results the code completion results to query
 *
 * \returns the selector (or partial selector) that has been entered thus far
 * for an Objective-C message send.
 */
CINDEX_LINKAGE
CXString clang_codeCompleteGetObjCSelector(CXCodeCompleteResults *Results);

/**
 * @}
 */

/**
 * \defgroup CINDEX_MISC Miscellaneous utility functions
 *
 * @{
 */

/**
 * Return a version string, suitable for showing to a user, but not
 *        intended to be parsed (the format is not guaranteed to be stable).
 */
CINDEX_LINKAGE CXString clang_getClangVersion(void);

/**
 * Enable/disable crash recovery.
 *
 * \param isEnabled Flag to indicate if crash recovery is enabled.  A non-zero
 *        value enables crash recovery, while 0 disables it.
 */
CINDEX_LINKAGE void clang_toggleCrashRecovery(unsigned isEnabled);

/**
 * Visitor invoked for each file in a translation unit
 *        (used with clang_getInclusions()).
 *
 * This visitor function will be invoked by clang_getInclusions() for each
 * file included (either at the top-level or by \#include directives) within
 * a translation unit.  The first argument is the file being included, and
 * the second and third arguments provide the inclusion stack.  The
 * array is sorted in order of immediate inclusion.  For example,
 * the first element refers to the location that included 'included_file'.
 */
typedef void (*CXInclusionVisitor)(CXFile included_file,
                                   CXSourceLocation *inclusion_stack,
                                   unsigned include_len,
                                   CXClientData client_data);

/**
 * Visit the set of preprocessor inclusions in a translation unit.
 *   The visitor function is called with the provided data for every included
 *   file.  This does not include headers included by the PCH file (unless one
 *   is inspecting the inclusions in the PCH file itself).
 */
CINDEX_LINKAGE void clang_getInclusions(CXTranslationUnit tu,
                                        CXInclusionVisitor visitor,
                                        CXClientData client_data);

typedef enum {
  CXEval_Int = 1,
  CXEval_Float = 2,
  CXEval_ObjCStrLiteral = 3,
  CXEval_StrLiteral = 4,
  CXEval_CFStr = 5,
  CXEval_Other = 6,

  CXEval_UnExposed = 0

} CXEvalResultKind;

/**
 * Evaluation result of a cursor
 */
typedef void *CXEvalResult;

/**
 * If cursor is a statement declaration tries to evaluate the
 * statement and if its variable, tries to evaluate its initializer,
 * into its corresponding type.
 * If it's an expression, tries to evaluate the expression.
 */
CINDEX_LINKAGE CXEvalResult clang_Cursor_Evaluate(CXCursor C);

/**
 * Returns the kind of the evaluated result.
 */
CINDEX_LINKAGE CXEvalResultKind clang_EvalResult_getKind(CXEvalResult E);

/**
 * Returns the evaluation result as integer if the
 * kind is Int.
 */
CINDEX_LINKAGE int clang_EvalResult_getAsInt(CXEvalResult E);

/**
 * Returns the evaluation result as a long long integer if the
 * kind is Int. This prevents overflows that may happen if the result is
 * returned with clang_EvalResult_getAsInt.
 */
CINDEX_LINKAGE long long clang_EvalResult_getAsLongLong(CXEvalResult E);

/**
 * Returns a non-zero value if the kind is Int and the evaluation
 * result resulted in an unsigned integer.
 */
CINDEX_LINKAGE unsigned clang_EvalResult_isUnsignedInt(CXEvalResult E);

/**
 * Returns the evaluation result as an unsigned integer if
 * the kind is Int and clang_EvalResult_isUnsignedInt is non-zero.
 */
CINDEX_LINKAGE unsigned long long
clang_EvalResult_getAsUnsigned(CXEvalResult E);

/**
 * Returns the evaluation result as double if the
 * kind is double.
 */
CINDEX_LINKAGE double clang_EvalResult_getAsDouble(CXEvalResult E);

/**
 * Returns the evaluation result as a constant string if the
 * kind is other than Int or float. User must not free this pointer,
 * instead call clang_EvalResult_dispose on the CXEvalResult returned
 * by clang_Cursor_Evaluate.
 */
CINDEX_LINKAGE const char *clang_EvalResult_getAsStr(CXEvalResult E);

/**
 * Disposes the created Eval memory.
 */
CINDEX_LINKAGE void clang_EvalResult_dispose(CXEvalResult E);
/**
 * @}
 */

/** \defgroup CINDEX_REMAPPING Remapping functions
 *
 * @{
 */

/**
 * A remapping of original source files and their translated files.
 */
typedef void *CXRemapping;

/**
 * Retrieve a remapping.
 *
 * \param path the path that contains metadata about remappings.
 *
 * \returns the requested remapping. This remapping must be freed
 * via a call to \c clang_remap_dispose(). Can return NULL if an error occurred.
 */
CINDEX_LINKAGE CXRemapping clang_getRemappings(const char *path);

/**
 * Retrieve a remapping.
 *
 * \param filePaths pointer to an array of file paths containing remapping info.
 *
 * \param numFiles number of file paths.
 *
 * \returns the requested remapping. This remapping must be freed
 * via a call to \c clang_remap_dispose(). Can return NULL if an error occurred.
 */
CINDEX_LINKAGE
CXRemapping clang_getRemappingsFromFileList(const char **filePaths,
                                            unsigned numFiles);

/**
 * Determine the number of remappings.
 */
CINDEX_LINKAGE unsigned clang_remap_getNumFiles(CXRemapping);

/**
 * Get the original and the associated filename from the remapping.
 *
 * \param original If non-NULL, will be set to the original filename.
 *
 * \param transformed If non-NULL, will be set to the filename that the original
 * is associated with.
 */
CINDEX_LINKAGE void clang_remap_getFilenames(CXRemapping, unsigned index,
                                             CXString *original,
                                             CXString *transformed);

/**
 * Dispose the remapping.
 */
CINDEX_LINKAGE void clang_remap_dispose(CXRemapping);

/**
 * @}
 */

/** \defgroup CINDEX_HIGH Higher level API functions
 *
 * @{
 */

enum CXVisitorResult { CXVisit_Break, CXVisit_Continue };

typedef struct CXCursorAndRangeVisitor {
  void *context;
  enum CXVisitorResult (*visit)(void *context, CXCursor, CXSourceRange);
} CXCursorAndRangeVisitor;

typedef enum {
  /**
   * Function returned successfully.
   */
  CXResult_Success = 0,
  /**
   * One of the parameters was invalid for the function.
   */
  CXResult_Invalid = 1,
  /**
   * The function was terminated by a callback (e.g. it returned
   * CXVisit_Break)
   */
  CXResult_VisitBreak = 2

} CXResult;

/**
 * Find references of a declaration in a specific file.
 *
 * \param cursor pointing to a declaration or a reference of one.
 *
 * \param file to search for references.
 *
 * \param visitor callback that will receive pairs of CXCursor/CXSourceRange for
 * each reference found.
 * The CXSourceRange will point inside the file; if the reference is inside
 * a macro (and not a macro argument) the CXSourceRange will be invalid.
 *
 * \returns one of the CXResult enumerators.
 */
CINDEX_LINKAGE CXResult clang_findReferencesInFile(
    CXCursor cursor, CXFile file, CXCursorAndRangeVisitor visitor);

/**
 * Find #import/#include directives in a specific file.
 *
 * \param TU translation unit containing the file to query.
 *
 * \param file to search for #import/#include directives.
 *
 * \param visitor callback that will receive pairs of CXCursor/CXSourceRange for
 * each directive found.
 *
 * \returns one of the CXResult enumerators.
 */
CINDEX_LINKAGE CXResult clang_findIncludesInFile(
    CXTranslationUnit TU, CXFile file, CXCursorAndRangeVisitor visitor);

#if __has_feature(blocks)
typedef enum CXVisitorResult (^CXCursorAndRangeVisitorBlock)(CXCursor,
                                                             CXSourceRange);
#else
typedef struct _CXCursorAndRangeVisitorBlock *CXCursorAndRangeVisitorBlock;
#endif

CINDEX_LINKAGE
CXResult clang_findReferencesInFileWithBlock(CXCursor, CXFile,
                                             CXCursorAndRangeVisitorBlock);

CINDEX_LINKAGE
CXResult clang_findIncludesInFileWithBlock(CXTranslationUnit, CXFile,
                                           CXCursorAndRangeVisitorBlock);

/**
 * The client's data object that is associated with a CXFile.
 */
typedef void *CXIdxClientFile;

/**
 * The client's data object that is associated with a semantic entity.
 */
typedef void *CXIdxClientEntity;

/**
 * The client's data object that is associated with a semantic container
 * of entities.
 */
typedef void *CXIdxClientContainer;

/**
 * The client's data object that is associated with an AST file (PCH
 * or module).
 */
typedef void *CXIdxClientASTFile;

/**
 * Source location passed to index callbacks.
 */
typedef struct {
  void *ptr_data[2];
  unsigned int_data;
} CXIdxLoc;

/**
 * Data for ppIncludedFile callback.
 */
typedef struct {
  /**
   * Location of '#' in the \#include/\#import directive.
   */
  CXIdxLoc hashLoc;
  /**
   * Filename as written in the \#include/\#import directive.
   */
  const char *filename;
  /**
   * The actual file that the \#include/\#import directive resolved to.
   */
  CXFile file;
  int isImport;
  int isAngled;
  /**
   * Non-zero if the directive was automatically turned into a module
   * import.
   */
  int isModuleImport;
} CXIdxIncludedFileInfo;

/**
 * Data for IndexerCallbacks#importedASTFile.
 */
typedef struct {
  /**
   * Top level AST file containing the imported PCH, module or submodule.
   */
  CXFile file;
  /**
   * The imported module or NULL if the AST file is a PCH.
   */
  CXModule module;
  /**
   * Location where the file is imported. Applicable only for modules.
   */
  CXIdxLoc loc;
  /**
   * Non-zero if an inclusion directive was automatically turned into
   * a module import. Applicable only for modules.
   */
  int isImplicit;

} CXIdxImportedASTFileInfo;

typedef enum {
  CXIdxEntity_Unexposed = 0,
  CXIdxEntity_Typedef = 1,
  CXIdxEntity_Function = 2,
  CXIdxEntity_Variable = 3,
  CXIdxEntity_Field = 4,
  CXIdxEntity_EnumConstant = 5,

  CXIdxEntity_ObjCClass = 6,
  CXIdxEntity_ObjCProtocol = 7,
  CXIdxEntity_ObjCCategory = 8,

  CXIdxEntity_ObjCInstanceMethod = 9,
  CXIdxEntity_ObjCClassMethod = 10,
  CXIdxEntity_ObjCProperty = 11,
  CXIdxEntity_ObjCIvar = 12,

  CXIdxEntity_Enum = 13,
  CXIdxEntity_Struct = 14,
  CXIdxEntity_Union = 15,

  CXIdxEntity_CXXClass = 16,
  CXIdxEntity_CXXNamespace = 17,
  CXIdxEntity_CXXNamespaceAlias = 18,
  CXIdxEntity_CXXStaticVariable = 19,
  CXIdxEntity_CXXStaticMethod = 20,
  CXIdxEntity_CXXInstanceMethod = 21,
  CXIdxEntity_CXXConstructor = 22,
  CXIdxEntity_CXXDestructor = 23,
  CXIdxEntity_CXXConversionFunction = 24,
  CXIdxEntity_CXXTypeAlias = 25,
  CXIdxEntity_CXXInterface = 26,
  CXIdxEntity_CXXConcept = 27

} CXIdxEntityKind;

typedef enum {
  CXIdxEntityLang_None = 0,
  CXIdxEntityLang_C = 1,
  CXIdxEntityLang_ObjC = 2,
  CXIdxEntityLang_CXX = 3,
  CXIdxEntityLang_Swift = 4
} CXIdxEntityLanguage;

/**
 * Extra C++ template information for an entity. This can apply to:
 * CXIdxEntity_Function
 * CXIdxEntity_CXXClass
 * CXIdxEntity_CXXStaticMethod
 * CXIdxEntity_CXXInstanceMethod
 * CXIdxEntity_CXXConstructor
 * CXIdxEntity_CXXConversionFunction
 * CXIdxEntity_CXXTypeAlias
 */
typedef enum {
  CXIdxEntity_NonTemplate = 0,
  CXIdxEntity_Template = 1,
  CXIdxEntity_TemplatePartialSpecialization = 2,
  CXIdxEntity_TemplateSpecialization = 3
} CXIdxEntityCXXTemplateKind;

typedef enum {
  CXIdxAttr_Unexposed = 0,
  CXIdxAttr_IBAction = 1,
  CXIdxAttr_IBOutlet = 2,
  CXIdxAttr_IBOutletCollection = 3
} CXIdxAttrKind;

typedef struct {
  CXIdxAttrKind kind;
  CXCursor cursor;
  CXIdxLoc loc;
} CXIdxAttrInfo;

typedef struct {
  CXIdxEntityKind kind;
  CXIdxEntityCXXTemplateKind templateKind;
  CXIdxEntityLanguage lang;
  const char *name;
  const char *USR;
  CXCursor cursor;
  const CXIdxAttrInfo *const *attributes;
  unsigned numAttributes;
} CXIdxEntityInfo;

typedef struct {
  CXCursor cursor;
} CXIdxContainerInfo;

typedef struct {
  const CXIdxAttrInfo *attrInfo;
  const CXIdxEntityInfo *objcClass;
  CXCursor classCursor;
  CXIdxLoc classLoc;
} CXIdxIBOutletCollectionAttrInfo;

typedef enum { CXIdxDeclFlag_Skipped = 0x1 } CXIdxDeclInfoFlags;

typedef struct {
  const CXIdxEntityInfo *entityInfo;
  CXCursor cursor;
  CXIdxLoc loc;
  const CXIdxContainerInfo *semanticContainer;
  /**
   * Generally same as #semanticContainer but can be different in
   * cases like out-of-line C++ member functions.
   */
  const CXIdxContainerInfo *lexicalContainer;
  int isRedeclaration;
  int isDefinition;
  int isContainer;
  const CXIdxContainerInfo *declAsContainer;
  /**
   * Whether the declaration exists in code or was created implicitly
   * by the compiler, e.g. implicit Objective-C methods for properties.
   */
  int isImplicit;
  const CXIdxAttrInfo *const *attributes;
  unsigned numAttributes;

  unsigned flags;

} CXIdxDeclInfo;

typedef enum {
  CXIdxObjCContainer_ForwardRef = 0,
  CXIdxObjCContainer_Interface = 1,
  CXIdxObjCContainer_Implementation = 2
} CXIdxObjCContainerKind;

typedef struct {
  const CXIdxDeclInfo *declInfo;
  CXIdxObjCContainerKind kind;
} CXIdxObjCContainerDeclInfo;

typedef struct {
  const CXIdxEntityInfo *base;
  CXCursor cursor;
  CXIdxLoc loc;
} CXIdxBaseClassInfo;

typedef struct {
  const CXIdxEntityInfo *protocol;
  CXCursor cursor;
  CXIdxLoc loc;
} CXIdxObjCProtocolRefInfo;

typedef struct {
  const CXIdxObjCProtocolRefInfo *const *protocols;
  unsigned numProtocols;
} CXIdxObjCProtocolRefListInfo;

typedef struct {
  const CXIdxObjCContainerDeclInfo *containerInfo;
  const CXIdxBaseClassInfo *superInfo;
  const CXIdxObjCProtocolRefListInfo *protocols;
} CXIdxObjCInterfaceDeclInfo;

typedef struct {
  const CXIdxObjCContainerDeclInfo *containerInfo;
  const CXIdxEntityInfo *objcClass;
  CXCursor classCursor;
  CXIdxLoc classLoc;
  const CXIdxObjCProtocolRefListInfo *protocols;
} CXIdxObjCCategoryDeclInfo;

typedef struct {
  const CXIdxDeclInfo *declInfo;
  const CXIdxEntityInfo *getter;
  const CXIdxEntityInfo *setter;
} CXIdxObjCPropertyDeclInfo;

typedef struct {
  const CXIdxDeclInfo *declInfo;
  const CXIdxBaseClassInfo *const *bases;
  unsigned numBases;
} CXIdxCXXClassDeclInfo;

/**
 * Data for IndexerCallbacks#indexEntityReference.
 *
 * This may be deprecated in a future version as this duplicates
 * the \c CXSymbolRole_Implicit bit in \c CXSymbolRole.
 */
typedef enum {
  /**
   * The entity is referenced directly in user's code.
   */
  CXIdxEntityRef_Direct = 1,
  /**
   * An implicit reference, e.g. a reference of an Objective-C method
   * via the dot syntax.
   */
  CXIdxEntityRef_Implicit = 2
} CXIdxEntityRefKind;

/**
 * Roles that are attributed to symbol occurrences.
 *
 * Internal: this currently mirrors low 9 bits of clang::index::SymbolRole with
 * higher bits zeroed. These high bits may be exposed in the future.
 */
typedef enum {
  CXSymbolRole_None = 0,
  CXSymbolRole_Declaration = 1 << 0,
  CXSymbolRole_Definition = 1 << 1,
  CXSymbolRole_Reference = 1 << 2,
  CXSymbolRole_Read = 1 << 3,
  CXSymbolRole_Write = 1 << 4,
  CXSymbolRole_Call = 1 << 5,
  CXSymbolRole_Dynamic = 1 << 6,
  CXSymbolRole_AddressOf = 1 << 7,
  CXSymbolRole_Implicit = 1 << 8
} CXSymbolRole;

/**
 * Data for IndexerCallbacks#indexEntityReference.
 */
typedef struct {
  CXIdxEntityRefKind kind;
  /**
   * Reference cursor.
   */
  CXCursor cursor;
  CXIdxLoc loc;
  /**
   * The entity that gets referenced.
   */
  const CXIdxEntityInfo *referencedEntity;
  /**
   * Immediate "parent" of the reference. For example:
   *
   * \code
   * Foo *var;
   * \endcode
   *
   * The parent of reference of type 'Foo' is the variable 'var'.
   * For references inside statement bodies of functions/methods,
   * the parentEntity will be the function/method.
   */
  const CXIdxEntityInfo *parentEntity;
  /**
   * Lexical container context of the reference.
   */
  const CXIdxContainerInfo *container;
  /**
   * Sets of symbol roles of the reference.
   */
  CXSymbolRole role;
} CXIdxEntityRefInfo;

/**
 * A group of callbacks used by #clang_indexSourceFile and
 * #clang_indexTranslationUnit.
 */
typedef struct {
  /**
   * Called periodically to check whether indexing should be aborted.
   * Should return 0 to continue, and non-zero to abort.
   */
  int (*abortQuery)(CXClientData client_data, void *reserved);

  /**
   * Called at the end of indexing; passes the complete diagnostic set.
   */
  void (*diagnostic)(CXClientData client_data, CXDiagnosticSet, void *reserved);

  CXIdxClientFile (*enteredMainFile)(CXClientData client_data, CXFile mainFile,
                                     void *reserved);

  /**
   * Called when a file gets \#included/\#imported.
   */
  CXIdxClientFile (*ppIncludedFile)(CXClientData client_data,
                                    const CXIdxIncludedFileInfo *);

  /**
   * Called when a AST file (PCH or module) gets imported.
   *
   * AST files will not get indexed (there will not be callbacks to index all
   * the entities in an AST file). The recommended action is that, if the AST
   * file is not already indexed, to initiate a new indexing job specific to
   * the AST file.
   */
  CXIdxClientASTFile (*importedASTFile)(CXClientData client_data,
                                        const CXIdxImportedASTFileInfo *);

  /**
   * Called at the beginning of indexing a translation unit.
   */
  CXIdxClientContainer (*startedTranslationUnit)(CXClientData client_data,
                                                 void *reserved);

  void (*indexDeclaration)(CXClientData client_data, const CXIdxDeclInfo *);

  /**
   * Called to index a reference of an entity.
   */
  void (*indexEntityReference)(CXClientData client_data,
                               const CXIdxEntityRefInfo *);

} IndexerCallbacks;

CINDEX_LINKAGE int clang_index_isEntityObjCContainerKind(CXIdxEntityKind);
CINDEX_LINKAGE const CXIdxObjCContainerDeclInfo *
clang_index_getObjCContainerDeclInfo(const CXIdxDeclInfo *);

CINDEX_LINKAGE const CXIdxObjCInterfaceDeclInfo *
clang_index_getObjCInterfaceDeclInfo(const CXIdxDeclInfo *);

CINDEX_LINKAGE
const CXIdxObjCCategoryDeclInfo *
clang_index_getObjCCategoryDeclInfo(const CXIdxDeclInfo *);

CINDEX_LINKAGE const CXIdxObjCProtocolRefListInfo *
clang_index_getObjCProtocolRefListInfo(const CXIdxDeclInfo *);

CINDEX_LINKAGE const CXIdxObjCPropertyDeclInfo *
clang_index_getObjCPropertyDeclInfo(const CXIdxDeclInfo *);

CINDEX_LINKAGE const CXIdxIBOutletCollectionAttrInfo *
clang_index_getIBOutletCollectionAttrInfo(const CXIdxAttrInfo *);

CINDEX_LINKAGE const CXIdxCXXClassDeclInfo *
clang_index_getCXXClassDeclInfo(const CXIdxDeclInfo *);

/**
 * For retrieving a custom CXIdxClientContainer attached to a
 * container.
 */
CINDEX_LINKAGE CXIdxClientContainer
clang_index_getClientContainer(const CXIdxContainerInfo *);

/**
 * For setting a custom CXIdxClientContainer attached to a
 * container.
 */
CINDEX_LINKAGE void clang_index_setClientContainer(const CXIdxContainerInfo *,
                                                   CXIdxClientContainer);

/**
 * For retrieving a custom CXIdxClientEntity attached to an entity.
 */
CINDEX_LINKAGE CXIdxClientEntity
clang_index_getClientEntity(const CXIdxEntityInfo *);

/**
 * For setting a custom CXIdxClientEntity attached to an entity.
 */
CINDEX_LINKAGE void clang_index_setClientEntity(const CXIdxEntityInfo *,
                                                CXIdxClientEntity);

/**
 * An indexing action/session, to be applied to one or multiple
 * translation units.
 */
typedef void *CXIndexAction;

/**
 * An indexing action/session, to be applied to one or multiple
 * translation units.
 *
 * \param CIdx The index object with which the index action will be associated.
 */
CINDEX_LINKAGE CXIndexAction clang_IndexAction_create(CXIndex CIdx);

/**
 * Destroy the given index action.
 *
 * The index action must not be destroyed until all of the translation units
 * created within that index action have been destroyed.
 */
CINDEX_LINKAGE void clang_IndexAction_dispose(CXIndexAction);

typedef enum {
  /**
   * Used to indicate that no special indexing options are needed.
   */
  CXIndexOpt_None = 0x0,

  /**
   * Used to indicate that IndexerCallbacks#indexEntityReference should
   * be invoked for only one reference of an entity per source file that does
   * not also include a declaration/definition of the entity.
   */
  CXIndexOpt_SuppressRedundantRefs = 0x1,

  /**
   * Function-local symbols should be indexed. If this is not set
   * function-local symbols will be ignored.
   */
  CXIndexOpt_IndexFunctionLocalSymbols = 0x2,

  /**
   * Implicit function/class template instantiations should be indexed.
   * If this is not set, implicit instantiations will be ignored.
   */
  CXIndexOpt_IndexImplicitTemplateInstantiations = 0x4,

  /**
   * Suppress all compiler warnings when parsing for indexing.
   */
  CXIndexOpt_SuppressWarnings = 0x8,

  /**
   * Skip a function/method body that was already parsed during an
   * indexing session associated with a \c CXIndexAction object.
   * Bodies in system headers are always skipped.
   */
  CXIndexOpt_SkipParsedBodiesInSession = 0x10

} CXIndexOptFlags;

/**
 * Index the given source file and the translation unit corresponding
 * to that file via callbacks implemented through #IndexerCallbacks.
 *
 * \param client_data pointer data supplied by the client, which will
 * be passed to the invoked callbacks.
 *
 * \param index_callbacks Pointer to indexing callbacks that the client
 * implements.
 *
 * \param index_callbacks_size Size of #IndexerCallbacks structure that gets
 * passed in index_callbacks.
 *
 * \param index_options A bitmask of options that affects how indexing is
 * performed. This should be a bitwise OR of the CXIndexOpt_XXX flags.
 *
 * \param[out] out_TU pointer to store a \c CXTranslationUnit that can be
 * reused after indexing is finished. Set to \c NULL if you do not require it.
 *
 * \returns 0 on success or if there were errors from which the compiler could
 * recover.  If there is a failure from which there is no recovery, returns
 * a non-zero \c CXErrorCode.
 *
 * The rest of the parameters are the same as #clang_parseTranslationUnit.
 */
CINDEX_LINKAGE int clang_indexSourceFile(
    CXIndexAction, CXClientData client_data, IndexerCallbacks *index_callbacks,
    unsigned index_callbacks_size, unsigned index_options,
    const char *source_filename, const char *const *command_line_args,
    int num_command_line_args, struct CXUnsavedFile *unsaved_files,
    unsigned num_unsaved_files, CXTranslationUnit *out_TU, unsigned TU_options);

/**
 * Same as clang_indexSourceFile but requires a full command line
 * for \c command_line_args including argv[0]. This is useful if the standard
 * library paths are relative to the binary.
 */
CINDEX_LINKAGE int clang_indexSourceFileFullArgv(
    CXIndexAction, CXClientData client_data, IndexerCallbacks *index_callbacks,
    unsigned index_callbacks_size, unsigned index_options,
    const char *source_filename, const char *const *command_line_args,
    int num_command_line_args, struct CXUnsavedFile *unsaved_files,
    unsigned num_unsaved_files, CXTranslationUnit *out_TU, unsigned TU_options);

/**
 * Index the given translation unit via callbacks implemented through
 * #IndexerCallbacks.
 *
 * The order of callback invocations is not guaranteed to be the same as
 * when indexing a source file. The high level order will be:
 *
 *   -Preprocessor callbacks invocations
 *   -Declaration/reference callbacks invocations
 *   -Diagnostic callback invocations
 *
 * The parameters are the same as #clang_indexSourceFile.
 *
 * \returns If there is a failure from which there is no recovery, returns
 * non-zero, otherwise returns 0.
 */
CINDEX_LINKAGE int clang_indexTranslationUnit(
    CXIndexAction, CXClientData client_data, IndexerCallbacks *index_callbacks,
    unsigned index_callbacks_size, unsigned index_options, CXTranslationUnit);

/**
 * Retrieve the CXIdxFile, file, line, column, and offset represented by
 * the given CXIdxLoc.
 *
 * If the location refers into a macro expansion, retrieves the
 * location of the macro expansion and if it refers into a macro argument
 * retrieves the location of the argument.
 */
CINDEX_LINKAGE void clang_indexLoc_getFileLocation(CXIdxLoc loc,
                                                   CXIdxClientFile *indexFile,
                                                   CXFile *file, unsigned *line,
                                                   unsigned *column,
                                                   unsigned *offset);

/**
 * Retrieve the CXSourceLocation represented by the given CXIdxLoc.
 */
CINDEX_LINKAGE
CXSourceLocation clang_indexLoc_getCXSourceLocation(CXIdxLoc loc);

/**
 * Visitor invoked for each field found by a traversal.
 *
 * This visitor function will be invoked for each field found by
 * \c clang_Type_visitFields. Its first argument is the cursor being
 * visited, its second argument is the client data provided to
 * \c clang_Type_visitFields.
 *
 * The visitor should return one of the \c CXVisitorResult values
 * to direct \c clang_Type_visitFields.
 */
typedef enum CXVisitorResult (*CXFieldVisitor)(CXCursor C,
                                               CXClientData client_data);

/**
 * Visit the fields of a particular type.
 *
 * This function visits all the direct fields of the given cursor,
 * invoking the given \p visitor function with the cursors of each
 * visited field. The traversal may be ended prematurely, if
 * the visitor returns \c CXFieldVisit_Break.
 *
 * \param T the record type whose field may be visited.
 *
 * \param visitor the visitor function that will be invoked for each
 * field of \p T.
 *
 * \param client_data pointer data supplied by the client, which will
 * be passed to the visitor each time it is invoked.
 *
 * \returns a non-zero value if the traversal was terminated
 * prematurely by the visitor returning \c CXFieldVisit_Break.
 */
CINDEX_LINKAGE unsigned clang_Type_visitFields(CXType T, CXFieldVisitor visitor,
                                               CXClientData client_data);

/**
 * Describes the kind of binary operators.
 */
enum CXBinaryOperatorKind {
  /** This value describes cursors which are not binary operators. */
  CXBinaryOperator_Invalid,
  /** C++ Pointer - to - member operator. */
  CXBinaryOperator_PtrMemD,
  /** C++ Pointer - to - member operator. */
  CXBinaryOperator_PtrMemI,
  /** Multiplication operator. */
  CXBinaryOperator_Mul,
  /** Division operator. */
  CXBinaryOperator_Div,
  /** Remainder operator. */
  CXBinaryOperator_Rem,
  /** Addition operator. */
  CXBinaryOperator_Add,
  /** Subtraction operator. */
  CXBinaryOperator_Sub,
  /** Bitwise shift left operator. */
  CXBinaryOperator_Shl,
  /** Bitwise shift right operator. */
  CXBinaryOperator_Shr,
  /** C++ three-way comparison (spaceship) operator. */
  CXBinaryOperator_Cmp,
  /** Less than operator. */
  CXBinaryOperator_LT,
  /** Greater than operator. */
  CXBinaryOperator_GT,
  /** Less or equal operator. */
  CXBinaryOperator_LE,
  /** Greater or equal operator. */
  CXBinaryOperator_GE,
  /** Equal operator. */
  CXBinaryOperator_EQ,
  /** Not equal operator. */
  CXBinaryOperator_NE,
  /** Bitwise AND operator. */
  CXBinaryOperator_And,
  /** Bitwise XOR operator. */
  CXBinaryOperator_Xor,
  /** Bitwise OR operator. */
  CXBinaryOperator_Or,
  /** Logical AND operator. */
  CXBinaryOperator_LAnd,
  /** Logical OR operator. */
  CXBinaryOperator_LOr,
  /** Assignment operator. */
  CXBinaryOperator_Assign,
  /** Multiplication assignment operator. */
  CXBinaryOperator_MulAssign,
  /** Division assignment operator. */
  CXBinaryOperator_DivAssign,
  /** Remainder assignment operator. */
  CXBinaryOperator_RemAssign,
  /** Addition assignment operator. */
  CXBinaryOperator_AddAssign,
  /** Subtraction assignment operator. */
  CXBinaryOperator_SubAssign,
  /** Bitwise shift left assignment operator. */
  CXBinaryOperator_ShlAssign,
  /** Bitwise shift right assignment operator. */
  CXBinaryOperator_ShrAssign,
  /** Bitwise AND assignment operator. */
  CXBinaryOperator_AndAssign,
  /** Bitwise XOR assignment operator. */
  CXBinaryOperator_XorAssign,
  /** Bitwise OR assignment operator. */
  CXBinaryOperator_OrAssign,
  /** Comma operator. */
  CXBinaryOperator_Comma
};

/**
 * Retrieve the spelling of a given CXBinaryOperatorKind.
 */
CINDEX_LINKAGE CXString
clang_getBinaryOperatorKindSpelling(enum CXBinaryOperatorKind kind);

/**
 * Retrieve the binary operator kind of this cursor.
 *
 * If this cursor is not a binary operator then returns Invalid.
 */
CINDEX_LINKAGE enum CXBinaryOperatorKind
clang_getCursorBinaryOperatorKind(CXCursor cursor);

/**
 * Describes the kind of unary operators.
 */
enum CXUnaryOperatorKind {
  /** This value describes cursors which are not unary operators. */
  CXUnaryOperator_Invalid,
  /** Postfix increment operator. */
  CXUnaryOperator_PostInc,
  /** Postfix decrement operator. */
  CXUnaryOperator_PostDec,
  /** Prefix increment operator. */
  CXUnaryOperator_PreInc,
  /** Prefix decrement operator. */
  CXUnaryOperator_PreDec,
  /** Address of operator. */
  CXUnaryOperator_AddrOf,
  /** Dereference operator. */
  CXUnaryOperator_Deref,
  /** Plus operator. */
  CXUnaryOperator_Plus,
  /** Minus operator. */
  CXUnaryOperator_Minus,
  /** Not operator. */
  CXUnaryOperator_Not,
  /** LNot operator. */
  CXUnaryOperator_LNot,
  /** "__real expr" operator. */
  CXUnaryOperator_Real,
  /** "__imag expr" operator. */
  CXUnaryOperator_Imag,
  /** __extension__ marker operator. */
  CXUnaryOperator_Extension,
  /** C++ co_await operator. */
  CXUnaryOperator_Coawait
};

/**
 * Retrieve the spelling of a given CXUnaryOperatorKind.
 */
CINDEX_LINKAGE CXString
clang_getUnaryOperatorKindSpelling(enum CXUnaryOperatorKind kind);

/**
 * Retrieve the unary operator kind of this cursor.
 *
 * If this cursor is not a unary operator then returns Invalid.
 */
CINDEX_LINKAGE enum CXUnaryOperatorKind
clang_getCursorUnaryOperatorKind(CXCursor cursor);

/**
 * @}
 */

/**
 * @}
 */

LLVM_CLANG_C_EXTERN_C_END

#endif
