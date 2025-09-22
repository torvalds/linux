//===- ExtractAPI/Serialization/SymbolGraphSerializer.h ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the SymbolGraphSerializer class.
///
/// Implement an APISetVisitor to serialize the APISet into the Symbol Graph
/// format for ExtractAPI. See https://github.com/apple/swift-docc-symbolkit.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_EXTRACTAPI_SERIALIZATION_SYMBOLGRAPHSERIALIZER_H
#define LLVM_CLANG_EXTRACTAPI_SERIALIZATION_SYMBOLGRAPHSERIALIZER_H

#include "clang/ExtractAPI/API.h"
#include "clang/ExtractAPI/APIIgnoresList.h"
#include "clang/ExtractAPI/Serialization/APISetVisitor.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>

namespace clang {
namespace extractapi {

using namespace llvm::json;

/// Common options to customize the visitor output.
struct SymbolGraphSerializerOption {
  /// Do not include unnecessary whitespaces to save space.
  bool Compact = true;
  bool EmitSymbolLabelsForTesting = false;
};

/// A representation of the contents of a given module symbol graph
struct ExtendedModule {
  ExtendedModule() = default;
  ExtendedModule(ExtendedModule &&EM) = default;
  ExtendedModule &operator=(ExtendedModule &&EM) = default;
  // Copies are expensive so disable them.
  ExtendedModule(const ExtendedModule &EM) = delete;
  ExtendedModule &operator=(const ExtendedModule &EM) = delete;

  /// Add a symbol to the module, do not store the resulting pointer or use it
  /// across insertions.
  Object *addSymbol(Object &&Symbol);

  void addRelationship(Object &&Relationship);

  /// A JSON array of formatted symbols from an \c APISet.
  Array Symbols;

  /// A JSON array of formatted symbol relationships from an \c APISet.
  Array Relationships;
};

/// The visitor that organizes API information in the Symbol Graph format.
///
/// The Symbol Graph format (https://github.com/apple/swift-docc-symbolkit)
/// models an API set as a directed graph, where nodes are symbol declarations,
/// and edges are relationships between the connected symbols.
class SymbolGraphSerializer : public APISetVisitor<SymbolGraphSerializer> {
private:
  using Base = APISetVisitor<SymbolGraphSerializer>;
  /// The main symbol graph that contains symbols that are either top-level or a
  /// are related to symbols defined in this product/module.
  ExtendedModule MainModule;

  /// Additional symbol graphs that contain symbols that are related to symbols
  /// defined in another product/module. The key of this map is the module name
  /// of the extended module.
  llvm::StringMap<ExtendedModule> ExtendedModules;

  /// The Symbol Graph format version used by this serializer.
  static const VersionTuple FormatVersion;

  /// Indicates whether to take into account the extended module. This is only
  /// useful for \c serializeSingleSymbolSGF.
  bool ForceEmitToMainModule;

  // Stores the references required to construct path components for the
  // currently visited APIRecord.
  llvm::SmallVector<SymbolReference, 8> Hierarchy;

  /// The list of symbols to ignore.
  ///
  /// Note: This should be consulted before emitting a symbol.
  const APIIgnoresList &IgnoresList;

  const bool EmitSymbolLabelsForTesting = false;

  const bool SkipSymbolsInCategoriesToExternalTypes = false;

  /// The object instantiated by the last call to serializeAPIRecord.
  Object *CurrentSymbol = nullptr;

  /// The module to which \p CurrentSymbol belongs too.
  ExtendedModule *ModuleForCurrentSymbol = nullptr;

public:
  static void
  serializeMainSymbolGraph(raw_ostream &OS, const APISet &API,
                           const APIIgnoresList &IgnoresList,
                           SymbolGraphSerializerOption Options = {});

  static void serializeWithExtensionGraphs(
      raw_ostream &MainOutput, const APISet &API,
      const APIIgnoresList &IgnoresList,
      llvm::function_ref<
          std::unique_ptr<llvm::raw_pwrite_stream>(llvm::Twine BaseFileName)>
          CreateOutputStream,
      SymbolGraphSerializerOption Options = {});

  /// Serialize a single symbol SGF. This is primarily used for libclang.
  ///
  /// \returns an optional JSON Object representing the payload that libclang
  /// expects for providing symbol information for a single symbol. If this is
  /// not a known symbol returns \c std::nullopt.
  static std::optional<Object> serializeSingleSymbolSGF(StringRef USR,
                                                        const APISet &API);

private:
  /// The kind of a relationship between two symbols.
  enum RelationshipKind {
    /// The source symbol is a member of the target symbol.
    /// For example enum constants are members of the enum, class/instance
    /// methods are members of the class, etc.
    MemberOf,

    /// The source symbol is inherited from the target symbol.
    InheritsFrom,

    /// The source symbol conforms to the target symbol.
    /// For example Objective-C protocol conformances.
    ConformsTo,

    /// The source symbol is an extension to the target symbol.
    /// For example Objective-C categories extending an external type.
    ExtensionTo,
  };

  /// Serialize a single record.
  void serializeSingleRecord(const APIRecord *Record);

  /// Get the string representation of the relationship kind.
  static StringRef getRelationshipString(RelationshipKind Kind);

  void serializeRelationship(RelationshipKind Kind,
                             const SymbolReference &Source,
                             const SymbolReference &Target,
                             ExtendedModule &Into);

  enum ConstraintKind { Conformance, ConditionalConformance };

  static StringRef getConstraintString(ConstraintKind Kind);

  /// Serialize the APIs in \c ExtendedModule.
  ///
  /// \returns a JSON object that contains the root of the formatted
  /// Symbol Graph.
  Object serializeGraph(StringRef ModuleName, ExtendedModule &&EM);

  /// Serialize the APIs in \c ExtendedModule in the Symbol Graph format and
  /// write them to the provide stream.
  void serializeGraphToStream(raw_ostream &OS,
                              SymbolGraphSerializerOption Options,
                              StringRef ModuleName, ExtendedModule &&EM);

  /// Synthesize the metadata section of the Symbol Graph format.
  ///
  /// The metadata section describes information about the Symbol Graph itself,
  /// including the format version and the generator information.
  Object serializeMetadata() const;

  /// Synthesize the module section of the Symbol Graph format.
  ///
  /// The module section contains information about the product that is defined
  /// by the given API set.
  /// Note that "module" here is not to be confused with the Clang/C++ module
  /// concept.
  Object serializeModuleObject(StringRef ModuleName) const;

  Array serializePathComponents(const APIRecord *Record) const;

  /// Determine if the given \p Record should be skipped during serialization.
  bool shouldSkip(const APIRecord *Record) const;

  ExtendedModule &getModuleForCurrentSymbol();

  /// Format the common API information for \p Record.
  ///
  /// This handles the shared information of all kinds of API records,
  /// for example identifier, source location and path components. The resulting
  /// object is then augmented with kind-specific symbol information in
  /// subsequent visit* methods by accessing the \p State member variable. This
  /// method also checks if the given \p Record should be skipped during
  /// serialization. This should be called only once per concrete APIRecord
  /// instance and the first visit* method to be called is responsible for
  /// calling this. This is normally visitAPIRecord unless a walkUpFromFoo
  /// method is implemented along the inheritance hierarchy in which case the
  /// visitFoo method needs to call this.
  ///
  /// \returns \c nullptr if this \p Record should be skipped, or a pointer to
  /// JSON object containing common symbol information of \p Record. Do not
  /// store the returned pointer only use it to augment the object with record
  /// specific information as it directly points to the object in the
  /// \p ExtendedModule, the pointer won't be valid as soon as another object is
  /// inserted into the module.
  void serializeAPIRecord(const APIRecord *Record);

public:
  // Handle if records should be skipped at this level of the traversal to
  // ensure that children of skipped records aren't serialized.
  bool traverseAPIRecord(const APIRecord *Record);

  bool visitAPIRecord(const APIRecord *Record);

  /// Visit a global function record.
  bool visitGlobalFunctionRecord(const GlobalFunctionRecord *Record);

  bool visitCXXClassRecord(const CXXClassRecord *Record);

  bool visitClassTemplateRecord(const ClassTemplateRecord *Record);

  bool visitClassTemplatePartialSpecializationRecord(
      const ClassTemplatePartialSpecializationRecord *Record);

  bool visitCXXMethodRecord(const CXXMethodRecord *Record);

  bool visitCXXMethodTemplateRecord(const CXXMethodTemplateRecord *Record);

  bool visitCXXFieldTemplateRecord(const CXXFieldTemplateRecord *Record);

  bool visitConceptRecord(const ConceptRecord *Record);

  bool
  visitGlobalVariableTemplateRecord(const GlobalVariableTemplateRecord *Record);

  bool visitGlobalVariableTemplatePartialSpecializationRecord(
      const GlobalVariableTemplatePartialSpecializationRecord *Record);

  bool
  visitGlobalFunctionTemplateRecord(const GlobalFunctionTemplateRecord *Record);

  bool visitObjCContainerRecord(const ObjCContainerRecord *Record);

  bool visitObjCInterfaceRecord(const ObjCInterfaceRecord *Record);

  bool traverseObjCCategoryRecord(const ObjCCategoryRecord *Record);
  bool walkUpFromObjCCategoryRecord(const ObjCCategoryRecord *Record);
  bool visitObjCCategoryRecord(const ObjCCategoryRecord *Record);

  bool visitObjCMethodRecord(const ObjCMethodRecord *Record);

  bool
  visitObjCInstanceVariableRecord(const ObjCInstanceVariableRecord *Record);

  bool walkUpFromTypedefRecord(const TypedefRecord *Record);
  bool visitTypedefRecord(const TypedefRecord *Record);

  SymbolGraphSerializer(const APISet &API, const APIIgnoresList &IgnoresList,
                        bool EmitSymbolLabelsForTesting = false,
                        bool ForceEmitToMainModule = false,
                        bool SkipSymbolsInCategoriesToExternalTypes = false)
      : Base(API), ForceEmitToMainModule(ForceEmitToMainModule),
        IgnoresList(IgnoresList),
        EmitSymbolLabelsForTesting(EmitSymbolLabelsForTesting),
        SkipSymbolsInCategoriesToExternalTypes(
            SkipSymbolsInCategoriesToExternalTypes) {}
};

} // namespace extractapi
} // namespace clang

#endif // LLVM_CLANG_EXTRACTAPI_SERIALIZATION_SYMBOLGRAPHSERIALIZER_H
