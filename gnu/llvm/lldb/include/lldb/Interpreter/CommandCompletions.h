//===-- CommandCompletions.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INTERPRETER_COMMANDCOMPLETIONS_H
#define LLDB_INTERPRETER_COMMANDCOMPLETIONS_H

#include <set>

#include "lldb/Core/SearchFilter.h"
#include "lldb/Interpreter/Options.h"
#include "lldb/Utility/CompletionRequest.h"
#include "lldb/Utility/FileSpecList.h"
#include "lldb/Utility/RegularExpression.h"
#include "lldb/lldb-private.h"

#include "llvm/ADT/Twine.h"

namespace lldb_private {
class TildeExpressionResolver;
class CommandCompletions {
public:
  static bool InvokeCommonCompletionCallbacks(
      CommandInterpreter &interpreter, uint32_t completion_mask,
      lldb_private::CompletionRequest &request, SearchFilter *searcher);

  // These are the generic completer functions:
  static void DiskFiles(CommandInterpreter &interpreter,
                        CompletionRequest &request, SearchFilter *searcher);

  static void DiskFiles(const llvm::Twine &partial_file_name,
                        StringList &matches, TildeExpressionResolver &Resolver);

  static void DiskDirectories(CommandInterpreter &interpreter,
                              CompletionRequest &request,
                              SearchFilter *searcher);

  static void DiskDirectories(const llvm::Twine &partial_file_name,
                              StringList &matches,
                              TildeExpressionResolver &Resolver);

  static void RemoteDiskFiles(CommandInterpreter &interpreter,
                              CompletionRequest &request,
                              SearchFilter *searcher);

  static void RemoteDiskDirectories(CommandInterpreter &interpreter,
                                    CompletionRequest &request,
                                    SearchFilter *searcher);

  static void SourceFiles(CommandInterpreter &interpreter,
                          CompletionRequest &request, SearchFilter *searcher);

  static void Modules(CommandInterpreter &interpreter,
                      CompletionRequest &request, SearchFilter *searcher);

  static void ModuleUUIDs(CommandInterpreter &interpreter,
                          CompletionRequest &request, SearchFilter *searcher);

  static void Symbols(CommandInterpreter &interpreter,
                      CompletionRequest &request, SearchFilter *searcher);

  static void SettingsNames(CommandInterpreter &interpreter,
                            CompletionRequest &request, SearchFilter *searcher);

  static void PlatformPluginNames(CommandInterpreter &interpreter,
                                  CompletionRequest &request,
                                  SearchFilter *searcher);

  static void ArchitectureNames(CommandInterpreter &interpreter,
                                CompletionRequest &request,
                                SearchFilter *searcher);

  static void VariablePath(CommandInterpreter &interpreter,
                           CompletionRequest &request, SearchFilter *searcher);

  static void Registers(CommandInterpreter &interpreter,
                        CompletionRequest &request, SearchFilter *searcher);

  static void Breakpoints(CommandInterpreter &interpreter,
                          CompletionRequest &request, SearchFilter *searcher);

  static void BreakpointNames(CommandInterpreter &interpreter,
                              CompletionRequest &request,
                              SearchFilter *searcher);

  static void ProcessPluginNames(CommandInterpreter &interpreter,
                                 CompletionRequest &request,
                                 SearchFilter *searcher);

  static void ProcessIDs(CommandInterpreter &interpreter,
                         CompletionRequest &request, SearchFilter *searcher);

  static void ProcessNames(CommandInterpreter &interpreter,
                           CompletionRequest &request, SearchFilter *searcher);

  static void DisassemblyFlavors(CommandInterpreter &interpreter,
                                 CompletionRequest &request,
                                 SearchFilter *searcher);

  static void TypeLanguages(CommandInterpreter &interpreter,
                            CompletionRequest &request, SearchFilter *searcher);

  static void FrameIndexes(CommandInterpreter &interpreter,
                           CompletionRequest &request, SearchFilter *searcher);

  static void StopHookIDs(CommandInterpreter &interpreter,
                          CompletionRequest &request, SearchFilter *searcher);

  static void ThreadIndexes(CommandInterpreter &interpreter,
                            CompletionRequest &request, SearchFilter *searcher);

  static void WatchPointIDs(CommandInterpreter &interpreter,
                            CompletionRequest &request, SearchFilter *searcher);

  static void TypeCategoryNames(CommandInterpreter &interpreter,
                                CompletionRequest &request,
                                SearchFilter *searcher);

  static void ThreadIDs(CommandInterpreter &interpreter,
                        CompletionRequest &request, SearchFilter *searcher);

  /// This completer works for commands whose only arguments are a command path.
  /// It isn't tied to an argument type because it completes not on a single
  /// argument but on the sequence of arguments, so you have to invoke it by
  /// hand.
  static void
  CompleteModifiableCmdPathArgs(CommandInterpreter &interpreter,
                                CompletionRequest &request,
                                OptionElementVector &opt_element_vector);
};

} // namespace lldb_private

#endif // LLDB_INTERPRETER_COMMANDCOMPLETIONS_H
