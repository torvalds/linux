/*===-- clang-c/CXSourceLocation.h - C Index Source Location ------*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This header provides the interface to C Index source locations.            *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef LLVM_CLANG_C_CXSOURCE_LOCATION_H
#define LLVM_CLANG_C_CXSOURCE_LOCATION_H

#include "clang-c/CXFile.h"
#include "clang-c/CXString.h"
#include "clang-c/ExternC.h"
#include "clang-c/Platform.h"

LLVM_CLANG_C_EXTERN_C_BEGIN

/**
 * \defgroup CINDEX_LOCATIONS Physical source locations
 *
 * Clang represents physical source locations in its abstract syntax tree in
 * great detail, with file, line, and column information for the majority of
 * the tokens parsed in the source code. These data types and functions are
 * used to represent source location information, either for a particular
 * point in the program or for a range of points in the program, and extract
 * specific location information from those data types.
 *
 * @{
 */

/**
 * Identifies a specific source location within a translation
 * unit.
 *
 * Use clang_getExpansionLocation() or clang_getSpellingLocation()
 * to map a source location to a particular file, line, and column.
 */
typedef struct {
  const void *ptr_data[2];
  unsigned int_data;
} CXSourceLocation;

/**
 * Identifies a half-open character range in the source code.
 *
 * Use clang_getRangeStart() and clang_getRangeEnd() to retrieve the
 * starting and end locations from a source range, respectively.
 */
typedef struct {
  const void *ptr_data[2];
  unsigned begin_int_data;
  unsigned end_int_data;
} CXSourceRange;

/**
 * Retrieve a NULL (invalid) source location.
 */
CINDEX_LINKAGE CXSourceLocation clang_getNullLocation(void);

/**
 * Determine whether two source locations, which must refer into
 * the same translation unit, refer to exactly the same point in the source
 * code.
 *
 * \returns non-zero if the source locations refer to the same location, zero
 * if they refer to different locations.
 */
CINDEX_LINKAGE unsigned clang_equalLocations(CXSourceLocation loc1,
                                             CXSourceLocation loc2);

/**
 * Returns non-zero if the given source location is in a system header.
 */
CINDEX_LINKAGE int clang_Location_isInSystemHeader(CXSourceLocation location);

/**
 * Returns non-zero if the given source location is in the main file of
 * the corresponding translation unit.
 */
CINDEX_LINKAGE int clang_Location_isFromMainFile(CXSourceLocation location);

/**
 * Retrieve a NULL (invalid) source range.
 */
CINDEX_LINKAGE CXSourceRange clang_getNullRange(void);

/**
 * Retrieve a source range given the beginning and ending source
 * locations.
 */
CINDEX_LINKAGE CXSourceRange clang_getRange(CXSourceLocation begin,
                                            CXSourceLocation end);

/**
 * Determine whether two ranges are equivalent.
 *
 * \returns non-zero if the ranges are the same, zero if they differ.
 */
CINDEX_LINKAGE unsigned clang_equalRanges(CXSourceRange range1,
                                          CXSourceRange range2);

/**
 * Returns non-zero if \p range is null.
 */
CINDEX_LINKAGE int clang_Range_isNull(CXSourceRange range);

/**
 * Retrieve the file, line, column, and offset represented by
 * the given source location.
 *
 * If the location refers into a macro expansion, retrieves the
 * location of the macro expansion.
 *
 * \param location the location within a source file that will be decomposed
 * into its parts.
 *
 * \param file [out] if non-NULL, will be set to the file to which the given
 * source location points.
 *
 * \param line [out] if non-NULL, will be set to the line to which the given
 * source location points.
 *
 * \param column [out] if non-NULL, will be set to the column to which the given
 * source location points.
 *
 * \param offset [out] if non-NULL, will be set to the offset into the
 * buffer to which the given source location points.
 */
CINDEX_LINKAGE void clang_getExpansionLocation(CXSourceLocation location,
                                               CXFile *file, unsigned *line,
                                               unsigned *column,
                                               unsigned *offset);

/**
 * Retrieve the file, line and column represented by the given source
 * location, as specified in a # line directive.
 *
 * Example: given the following source code in a file somefile.c
 *
 * \code
 * #123 "dummy.c" 1
 *
 * static int func(void)
 * {
 *     return 0;
 * }
 * \endcode
 *
 * the location information returned by this function would be
 *
 * File: dummy.c Line: 124 Column: 12
 *
 * whereas clang_getExpansionLocation would have returned
 *
 * File: somefile.c Line: 3 Column: 12
 *
 * \param location the location within a source file that will be decomposed
 * into its parts.
 *
 * \param filename [out] if non-NULL, will be set to the filename of the
 * source location. Note that filenames returned will be for "virtual" files,
 * which don't necessarily exist on the machine running clang - e.g. when
 * parsing preprocessed output obtained from a different environment. If
 * a non-NULL value is passed in, remember to dispose of the returned value
 * using \c clang_disposeString() once you've finished with it. For an invalid
 * source location, an empty string is returned.
 *
 * \param line [out] if non-NULL, will be set to the line number of the
 * source location. For an invalid source location, zero is returned.
 *
 * \param column [out] if non-NULL, will be set to the column number of the
 * source location. For an invalid source location, zero is returned.
 */
CINDEX_LINKAGE void clang_getPresumedLocation(CXSourceLocation location,
                                              CXString *filename,
                                              unsigned *line, unsigned *column);

/**
 * Legacy API to retrieve the file, line, column, and offset represented
 * by the given source location.
 *
 * This interface has been replaced by the newer interface
 * #clang_getExpansionLocation(). See that interface's documentation for
 * details.
 */
CINDEX_LINKAGE void clang_getInstantiationLocation(CXSourceLocation location,
                                                   CXFile *file, unsigned *line,
                                                   unsigned *column,
                                                   unsigned *offset);

/**
 * Retrieve the file, line, column, and offset represented by
 * the given source location.
 *
 * If the location refers into a macro instantiation, return where the
 * location was originally spelled in the source file.
 *
 * \param location the location within a source file that will be decomposed
 * into its parts.
 *
 * \param file [out] if non-NULL, will be set to the file to which the given
 * source location points.
 *
 * \param line [out] if non-NULL, will be set to the line to which the given
 * source location points.
 *
 * \param column [out] if non-NULL, will be set to the column to which the given
 * source location points.
 *
 * \param offset [out] if non-NULL, will be set to the offset into the
 * buffer to which the given source location points.
 */
CINDEX_LINKAGE void clang_getSpellingLocation(CXSourceLocation location,
                                              CXFile *file, unsigned *line,
                                              unsigned *column,
                                              unsigned *offset);

/**
 * Retrieve the file, line, column, and offset represented by
 * the given source location.
 *
 * If the location refers into a macro expansion, return where the macro was
 * expanded or where the macro argument was written, if the location points at
 * a macro argument.
 *
 * \param location the location within a source file that will be decomposed
 * into its parts.
 *
 * \param file [out] if non-NULL, will be set to the file to which the given
 * source location points.
 *
 * \param line [out] if non-NULL, will be set to the line to which the given
 * source location points.
 *
 * \param column [out] if non-NULL, will be set to the column to which the given
 * source location points.
 *
 * \param offset [out] if non-NULL, will be set to the offset into the
 * buffer to which the given source location points.
 */
CINDEX_LINKAGE void clang_getFileLocation(CXSourceLocation location,
                                          CXFile *file, unsigned *line,
                                          unsigned *column, unsigned *offset);

/**
 * Retrieve a source location representing the first character within a
 * source range.
 */
CINDEX_LINKAGE CXSourceLocation clang_getRangeStart(CXSourceRange range);

/**
 * Retrieve a source location representing the last character within a
 * source range.
 */
CINDEX_LINKAGE CXSourceLocation clang_getRangeEnd(CXSourceRange range);

/**
 * Identifies an array of ranges.
 */
typedef struct {
  /** The number of ranges in the \c ranges array. */
  unsigned count;
  /**
   * An array of \c CXSourceRanges.
   */
  CXSourceRange *ranges;
} CXSourceRangeList;

/**
 * Destroy the given \c CXSourceRangeList.
 */
CINDEX_LINKAGE void clang_disposeSourceRangeList(CXSourceRangeList *ranges);

/**
 * @}
 */

LLVM_CLANG_C_EXTERN_C_END

#endif
