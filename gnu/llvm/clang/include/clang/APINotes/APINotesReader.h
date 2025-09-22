//===--- APINotesReader.h - API Notes Reader --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the \c APINotesReader class that reads source API notes
// data providing additional information about source code as a separate input,
// such as the non-nil/nilable annotations for method parameters.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_APINOTES_READER_H
#define LLVM_CLANG_APINOTES_READER_H

#include "clang/APINotes/Types.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/VersionTuple.h"
#include <memory>

namespace clang {
namespace api_notes {

/// A class that reads API notes data from a binary file that was written by
/// the \c APINotesWriter.
class APINotesReader {
  class Implementation;
  std::unique_ptr<Implementation> Implementation;

  APINotesReader(llvm::MemoryBuffer *InputBuffer,
                 llvm::VersionTuple SwiftVersion, bool &Failed);

public:
  /// Create a new API notes reader from the given member buffer, which
  /// contains the contents of a binary API notes file.
  ///
  /// \returns the new API notes reader, or null if an error occurred.
  static std::unique_ptr<APINotesReader>
  Create(std::unique_ptr<llvm::MemoryBuffer> InputBuffer,
         llvm::VersionTuple SwiftVersion);

  ~APINotesReader();

  APINotesReader(const APINotesReader &) = delete;
  APINotesReader &operator=(const APINotesReader &) = delete;

  /// Captures the completed versioned information for a particular part of
  /// API notes, including both unversioned API notes and each versioned API
  /// note for that particular entity.
  template <typename T> class VersionedInfo {
    /// The complete set of results.
    llvm::SmallVector<std::pair<llvm::VersionTuple, T>, 1> Results;

    /// The index of the result that is the "selected" set based on the desired
    /// Swift version, or null if nothing matched.
    std::optional<unsigned> Selected;

  public:
    /// Form an empty set of versioned information.
    VersionedInfo(std::nullopt_t) : Selected(std::nullopt) {}

    /// Form a versioned info set given the desired version and a set of
    /// results.
    VersionedInfo(
        llvm::VersionTuple Version,
        llvm::SmallVector<std::pair<llvm::VersionTuple, T>, 1> Results);

    /// Retrieve the selected index in the result set.
    std::optional<unsigned> getSelected() const { return Selected; }

    /// Return the number of versioned results we know about.
    unsigned size() const { return Results.size(); }

    /// Access all versioned results.
    const std::pair<llvm::VersionTuple, T> *begin() const {
      assert(!Results.empty());
      return Results.begin();
    }
    const std::pair<llvm::VersionTuple, T> *end() const {
      return Results.end();
    }

    /// Access a specific versioned result.
    const std::pair<llvm::VersionTuple, T> &operator[](unsigned index) const {
      assert(index < Results.size());
      return Results[index];
    }
  };

  /// Look for the context ID of the given Objective-C class.
  ///
  /// \param Name The name of the class we're looking for.
  ///
  /// \returns The ID, if known.
  std::optional<ContextID> lookupObjCClassID(llvm::StringRef Name);

  /// Look for information regarding the given Objective-C class.
  ///
  /// \param Name The name of the class we're looking for.
  ///
  /// \returns The information about the class, if known.
  VersionedInfo<ContextInfo> lookupObjCClassInfo(llvm::StringRef Name);

  /// Look for the context ID of the given Objective-C protocol.
  ///
  /// \param Name The name of the protocol we're looking for.
  ///
  /// \returns The ID of the protocol, if known.
  std::optional<ContextID> lookupObjCProtocolID(llvm::StringRef Name);

  /// Look for information regarding the given Objective-C protocol.
  ///
  /// \param Name The name of the protocol we're looking for.
  ///
  /// \returns The information about the protocol, if known.
  VersionedInfo<ContextInfo> lookupObjCProtocolInfo(llvm::StringRef Name);

  /// Look for information regarding the given Objective-C property in
  /// the given context.
  ///
  /// \param CtxID The ID that references the context we are looking for.
  /// \param Name The name of the property we're looking for.
  /// \param IsInstance Whether we are looking for an instance property (vs.
  /// a class property).
  ///
  /// \returns Information about the property, if known.
  VersionedInfo<ObjCPropertyInfo>
  lookupObjCProperty(ContextID CtxID, llvm::StringRef Name, bool IsInstance);

  /// Look for information regarding the given Objective-C method in
  /// the given context.
  ///
  /// \param CtxID The ID that references the context we are looking for.
  /// \param Selector The selector naming the method we're looking for.
  /// \param IsInstanceMethod Whether we are looking for an instance method.
  ///
  /// \returns Information about the method, if known.
  VersionedInfo<ObjCMethodInfo> lookupObjCMethod(ContextID CtxID,
                                                 ObjCSelectorRef Selector,
                                                 bool IsInstanceMethod);

  /// Look for information regarding the given C++ method in the given C++ tag
  /// context.
  ///
  /// \param CtxID The ID that references the parent context, i.e. a C++ tag.
  /// \param Name The name of the C++ method we're looking for.
  ///
  /// \returns Information about the method, if known.
  VersionedInfo<CXXMethodInfo> lookupCXXMethod(ContextID CtxID,
                                               llvm::StringRef Name);

  /// Look for information regarding the given global variable.
  ///
  /// \param Name The name of the global variable.
  ///
  /// \returns information about the global variable, if known.
  VersionedInfo<GlobalVariableInfo>
  lookupGlobalVariable(llvm::StringRef Name,
                       std::optional<Context> Ctx = std::nullopt);

  /// Look for information regarding the given global function.
  ///
  /// \param Name The name of the global function.
  ///
  /// \returns information about the global function, if known.
  VersionedInfo<GlobalFunctionInfo>
  lookupGlobalFunction(llvm::StringRef Name,
                       std::optional<Context> Ctx = std::nullopt);

  /// Look for information regarding the given enumerator.
  ///
  /// \param Name The name of the enumerator.
  ///
  /// \returns information about the enumerator, if known.
  VersionedInfo<EnumConstantInfo> lookupEnumConstant(llvm::StringRef Name);

  /// Look for the context ID of the given C++ tag.
  ///
  /// \param Name The name of the tag we're looking for.
  /// \param ParentCtx The context in which this tag is declared, e.g. a C++
  /// namespace.
  ///
  /// \returns The ID, if known.
  std::optional<ContextID>
  lookupTagID(llvm::StringRef Name,
              std::optional<Context> ParentCtx = std::nullopt);

  /// Look for information regarding the given tag
  /// (struct/union/enum/C++ class).
  ///
  /// \param Name The name of the tag.
  ///
  /// \returns information about the tag, if known.
  VersionedInfo<TagInfo> lookupTag(llvm::StringRef Name,
                                   std::optional<Context> Ctx = std::nullopt);

  /// Look for information regarding the given typedef.
  ///
  /// \param Name The name of the typedef.
  ///
  /// \returns information about the typedef, if known.
  VersionedInfo<TypedefInfo>
  lookupTypedef(llvm::StringRef Name,
                std::optional<Context> Ctx = std::nullopt);

  /// Look for the context ID of the given C++ namespace.
  ///
  /// \param Name The name of the class we're looking for.
  ///
  /// \returns The ID, if known.
  std::optional<ContextID>
  lookupNamespaceID(llvm::StringRef Name,
                    std::optional<ContextID> ParentNamespaceID = std::nullopt);
};

} // end namespace api_notes
} // end namespace clang

#endif // LLVM_CLANG_APINOTES_READER_H
