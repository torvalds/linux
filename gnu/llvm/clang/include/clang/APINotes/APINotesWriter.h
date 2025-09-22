//===-- APINotesWriter.h - API Notes Writer ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the \c APINotesWriter class that writes out source
// API notes data providing additional information about source code as
// a separate input, such as the non-nil/nilable annotations for
// method parameters.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_APINOTES_WRITER_H
#define LLVM_CLANG_APINOTES_WRITER_H

#include "clang/APINotes/Types.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/Support/raw_ostream.h"

#include <memory>

namespace clang {
class FileEntry;

namespace api_notes {

/// A class that writes API notes data to a binary representation that can be
/// read by the \c APINotesReader.
class APINotesWriter {
  class Implementation;
  std::unique_ptr<Implementation> Implementation;

public:
  /// Create a new API notes writer with the given module name and
  /// (optional) source file.
  APINotesWriter(llvm::StringRef ModuleName, const FileEntry *SF);
  ~APINotesWriter();

  APINotesWriter(const APINotesWriter &) = delete;
  APINotesWriter &operator=(const APINotesWriter &) = delete;

  void writeToStream(llvm::raw_ostream &OS);

  /// Add information about a specific Objective-C class or protocol or a C++
  /// namespace.
  ///
  /// \param Name The name of this class/protocol/namespace.
  /// \param Kind Whether this is a class, a protocol, or a namespace.
  /// \param Info Information about this class/protocol/namespace.
  ///
  /// \returns the ID of the class, protocol, or namespace, which can be used to
  /// add properties and methods to the class/protocol/namespace.
  ContextID addContext(std::optional<ContextID> ParentCtxID,
                       llvm::StringRef Name, ContextKind Kind,
                       const ContextInfo &Info,
                       llvm::VersionTuple SwiftVersion);

  /// Add information about a specific Objective-C property.
  ///
  /// \param CtxID The context in which this property resides.
  /// \param Name The name of this property.
  /// \param Info Information about this property.
  void addObjCProperty(ContextID CtxID, llvm::StringRef Name,
                       bool IsInstanceProperty, const ObjCPropertyInfo &Info,
                       llvm::VersionTuple SwiftVersion);

  /// Add information about a specific Objective-C method.
  ///
  /// \param CtxID The context in which this method resides.
  /// \param Selector The selector that names this method.
  /// \param IsInstanceMethod Whether this method is an instance method
  /// (vs. a class method).
  /// \param Info Information about this method.
  void addObjCMethod(ContextID CtxID, ObjCSelectorRef Selector,
                     bool IsInstanceMethod, const ObjCMethodInfo &Info,
                     llvm::VersionTuple SwiftVersion);

  /// Add information about a specific C++ method.
  ///
  /// \param CtxID The context in which this method resides, i.e. a C++ tag.
  /// \param Name The name of the method.
  /// \param Info Information about this method.
  void addCXXMethod(ContextID CtxID, llvm::StringRef Name,
                    const CXXMethodInfo &Info, llvm::VersionTuple SwiftVersion);

  /// Add information about a global variable.
  ///
  /// \param Name The name of this global variable.
  /// \param Info Information about this global variable.
  void addGlobalVariable(std::optional<Context> Ctx, llvm::StringRef Name,
                         const GlobalVariableInfo &Info,
                         llvm::VersionTuple SwiftVersion);

  /// Add information about a global function.
  ///
  /// \param Name The name of this global function.
  /// \param Info Information about this global function.
  void addGlobalFunction(std::optional<Context> Ctx, llvm::StringRef Name,
                         const GlobalFunctionInfo &Info,
                         llvm::VersionTuple SwiftVersion);

  /// Add information about an enumerator.
  ///
  /// \param Name The name of this enumerator.
  /// \param Info Information about this enumerator.
  void addEnumConstant(llvm::StringRef Name, const EnumConstantInfo &Info,
                       llvm::VersionTuple SwiftVersion);

  /// Add information about a tag (struct/union/enum/C++ class).
  ///
  /// \param Name The name of this tag.
  /// \param Info Information about this tag.
  void addTag(std::optional<Context> Ctx, llvm::StringRef Name,
              const TagInfo &Info, llvm::VersionTuple SwiftVersion);

  /// Add information about a typedef.
  ///
  /// \param Name The name of this typedef.
  /// \param Info Information about this typedef.
  void addTypedef(std::optional<Context> Ctx, llvm::StringRef Name,
                  const TypedefInfo &Info, llvm::VersionTuple SwiftVersion);
};
} // namespace api_notes
} // namespace clang

#endif // LLVM_CLANG_APINOTES_WRITER_H
