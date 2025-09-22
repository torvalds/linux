//===- ExtractAPI/ExtractAPIActionBase.h -----------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the ExtractAPIActionBase class.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_EXTRACTAPI_ACTION_BASE_H
#define LLVM_CLANG_EXTRACTAPI_ACTION_BASE_H

#include "clang/ExtractAPI/API.h"
#include "clang/ExtractAPI/APIIgnoresList.h"
#include "clang/Frontend/CompilerInstance.h"
#include "llvm/Support/raw_ostream.h"

namespace clang {

/// Base class to be used by front end actions to generate ExtarctAPI info
///
/// Deriving from this class equips an action with all the necessary tools to
/// generate ExractAPI information in form of symbol-graphs
class ExtractAPIActionBase {
protected:
  /// A representation of the APIs this action extracts.
  std::unique_ptr<extractapi::APISet> API;

  /// A stream to the main output file of this action.
  std::unique_ptr<llvm::raw_pwrite_stream> OS;

  /// The product this action is extracting API information for.
  std::string ProductName;

  /// The synthesized input buffer that contains all the provided input header
  /// files.
  std::unique_ptr<llvm::MemoryBuffer> Buffer;

  /// The list of symbols to ignore during serialization
  extractapi::APIIgnoresList IgnoresList;

  /// Implements EndSourceFileAction for Symbol-Graph generation
  ///
  /// Use the serializer to generate output symbol graph files from
  /// the information gathered during the execution of Action.
  void ImplEndSourceFileAction(CompilerInstance &CI);
};

} // namespace clang

#endif // LLVM_CLANG_EXTRACTAPI_ACTION_BASE_H
