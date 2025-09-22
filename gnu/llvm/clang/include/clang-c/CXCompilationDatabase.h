/*===-- clang-c/CXCompilationDatabase.h - Compilation database  ---*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This header provides a public interface to use CompilationDatabase without *|
|* the full Clang C++ API.                                                    *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef LLVM_CLANG_C_CXCOMPILATIONDATABASE_H
#define LLVM_CLANG_C_CXCOMPILATIONDATABASE_H

#include "clang-c/CXString.h"
#include "clang-c/ExternC.h"
#include "clang-c/Platform.h"

LLVM_CLANG_C_EXTERN_C_BEGIN

/** \defgroup COMPILATIONDB CompilationDatabase functions
 * \ingroup CINDEX
 *
 * @{
 */

/**
 * A compilation database holds all information used to compile files in a
 * project. For each file in the database, it can be queried for the working
 * directory or the command line used for the compiler invocation.
 *
 * Must be freed by \c clang_CompilationDatabase_dispose
 */
typedef void * CXCompilationDatabase;

/**
 * Contains the results of a search in the compilation database
 *
 * When searching for the compile command for a file, the compilation db can
 * return several commands, as the file may have been compiled with
 * different options in different places of the project. This choice of compile
 * commands is wrapped in this opaque data structure. It must be freed by
 * \c clang_CompileCommands_dispose.
 */
typedef void * CXCompileCommands;

/**
 * Represents the command line invocation to compile a specific file.
 */
typedef void * CXCompileCommand;

/**
 * Error codes for Compilation Database
 */
typedef enum  {
  /*
   * No error occurred
   */
  CXCompilationDatabase_NoError = 0,

  /*
   * Database can not be loaded
   */
  CXCompilationDatabase_CanNotLoadDatabase = 1

} CXCompilationDatabase_Error;

/**
 * Creates a compilation database from the database found in directory
 * buildDir. For example, CMake can output a compile_commands.json which can
 * be used to build the database.
 *
 * It must be freed by \c clang_CompilationDatabase_dispose.
 */
CINDEX_LINKAGE CXCompilationDatabase
clang_CompilationDatabase_fromDirectory(const char *BuildDir,
                                        CXCompilationDatabase_Error *ErrorCode);

/**
 * Free the given compilation database
 */
CINDEX_LINKAGE void
clang_CompilationDatabase_dispose(CXCompilationDatabase);

/**
 * Find the compile commands used for a file. The compile commands
 * must be freed by \c clang_CompileCommands_dispose.
 */
CINDEX_LINKAGE CXCompileCommands
clang_CompilationDatabase_getCompileCommands(CXCompilationDatabase,
                                             const char *CompleteFileName);

/**
 * Get all the compile commands in the given compilation database.
 */
CINDEX_LINKAGE CXCompileCommands
clang_CompilationDatabase_getAllCompileCommands(CXCompilationDatabase);

/**
 * Free the given CompileCommands
 */
CINDEX_LINKAGE void clang_CompileCommands_dispose(CXCompileCommands);

/**
 * Get the number of CompileCommand we have for a file
 */
CINDEX_LINKAGE unsigned
clang_CompileCommands_getSize(CXCompileCommands);

/**
 * Get the I'th CompileCommand for a file
 *
 * Note : 0 <= i < clang_CompileCommands_getSize(CXCompileCommands)
 */
CINDEX_LINKAGE CXCompileCommand
clang_CompileCommands_getCommand(CXCompileCommands, unsigned I);

/**
 * Get the working directory where the CompileCommand was executed from
 */
CINDEX_LINKAGE CXString
clang_CompileCommand_getDirectory(CXCompileCommand);

/**
 * Get the filename associated with the CompileCommand.
 */
CINDEX_LINKAGE CXString
clang_CompileCommand_getFilename(CXCompileCommand);

/**
 * Get the number of arguments in the compiler invocation.
 *
 */
CINDEX_LINKAGE unsigned
clang_CompileCommand_getNumArgs(CXCompileCommand);

/**
 * Get the I'th argument value in the compiler invocations
 *
 * Invariant :
 *  - argument 0 is the compiler executable
 */
CINDEX_LINKAGE CXString
clang_CompileCommand_getArg(CXCompileCommand, unsigned I);

/**
 * Get the number of source mappings for the compiler invocation.
 */
CINDEX_LINKAGE unsigned
clang_CompileCommand_getNumMappedSources(CXCompileCommand);

/**
 * Get the I'th mapped source path for the compiler invocation.
 */
CINDEX_LINKAGE CXString
clang_CompileCommand_getMappedSourcePath(CXCompileCommand, unsigned I);

/**
 * Get the I'th mapped source content for the compiler invocation.
 */
CINDEX_LINKAGE CXString
clang_CompileCommand_getMappedSourceContent(CXCompileCommand, unsigned I);

/**
 * @}
 */

LLVM_CLANG_C_EXTERN_C_END

#endif

