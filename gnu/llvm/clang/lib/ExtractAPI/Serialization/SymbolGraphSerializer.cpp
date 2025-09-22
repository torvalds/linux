//===- ExtractAPI/Serialization/SymbolGraphSerializer.cpp -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the SymbolGraphSerializer.
///
//===----------------------------------------------------------------------===//

#include "clang/ExtractAPI/Serialization/SymbolGraphSerializer.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/Version.h"
#include "clang/ExtractAPI/API.h"
#include "clang/ExtractAPI/DeclarationFragments.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/Support/raw_ostream.h"
#include <iterator>
#include <optional>
#include <type_traits>

using namespace clang;
using namespace clang::extractapi;
using namespace llvm;

namespace {

/// Helper function to inject a JSON object \p Obj into another object \p Paren
/// at position \p Key.
void serializeObject(Object &Paren, StringRef Key,
                     std::optional<Object> &&Obj) {
  if (Obj)
    Paren[Key] = std::move(*Obj);
}

/// Helper function to inject a JSON array \p Array into object \p Paren at
/// position \p Key.
void serializeArray(Object &Paren, StringRef Key,
                    std::optional<Array> &&Array) {
  if (Array)
    Paren[Key] = std::move(*Array);
}

/// Helper function to inject a JSON array composed of the values in \p C into
/// object \p Paren at position \p Key.
template <typename ContainerTy>
void serializeArray(Object &Paren, StringRef Key, ContainerTy &&C) {
  Paren[Key] = Array(C);
}

/// Serialize a \c VersionTuple \p V with the Symbol Graph semantic version
/// format.
///
/// A semantic version object contains three numeric fields, representing the
/// \c major, \c minor, and \c patch parts of the version tuple.
/// For example version tuple 1.0.3 is serialized as:
/// \code
///   {
///     "major" : 1,
///     "minor" : 0,
///     "patch" : 3
///   }
/// \endcode
///
/// \returns \c std::nullopt if the version \p V is empty, or an \c Object
/// containing the semantic version representation of \p V.
std::optional<Object> serializeSemanticVersion(const VersionTuple &V) {
  if (V.empty())
    return std::nullopt;

  Object Version;
  Version["major"] = V.getMajor();
  Version["minor"] = V.getMinor().value_or(0);
  Version["patch"] = V.getSubminor().value_or(0);
  return Version;
}

/// Serialize the OS information in the Symbol Graph platform property.
///
/// The OS information in Symbol Graph contains the \c name of the OS, and an
/// optional \c minimumVersion semantic version field.
Object serializeOperatingSystem(const Triple &T) {
  Object OS;
  OS["name"] = T.getOSTypeName(T.getOS());
  serializeObject(OS, "minimumVersion",
                  serializeSemanticVersion(T.getMinimumSupportedOSVersion()));
  return OS;
}

/// Serialize the platform information in the Symbol Graph module section.
///
/// The platform object describes a target platform triple in corresponding
/// three fields: \c architecture, \c vendor, and \c operatingSystem.
Object serializePlatform(const Triple &T) {
  Object Platform;
  Platform["architecture"] = T.getArchName();
  Platform["vendor"] = T.getVendorName();
  Platform["operatingSystem"] = serializeOperatingSystem(T);
  return Platform;
}

/// Serialize a source position.
Object serializeSourcePosition(const PresumedLoc &Loc) {
  assert(Loc.isValid() && "invalid source position");

  Object SourcePosition;
  SourcePosition["line"] = Loc.getLine() - 1;
  SourcePosition["character"] = Loc.getColumn() - 1;

  return SourcePosition;
}

/// Serialize a source location in file.
///
/// \param Loc The presumed location to serialize.
/// \param IncludeFileURI If true, include the file path of \p Loc as a URI.
/// Defaults to false.
Object serializeSourceLocation(const PresumedLoc &Loc,
                               bool IncludeFileURI = false) {
  Object SourceLocation;
  serializeObject(SourceLocation, "position", serializeSourcePosition(Loc));

  if (IncludeFileURI) {
    std::string FileURI = "file://";
    // Normalize file path to use forward slashes for the URI.
    FileURI += sys::path::convert_to_slash(Loc.getFilename());
    SourceLocation["uri"] = FileURI;
  }

  return SourceLocation;
}

/// Serialize a source range with begin and end locations.
Object serializeSourceRange(const PresumedLoc &BeginLoc,
                            const PresumedLoc &EndLoc) {
  Object SourceRange;
  serializeObject(SourceRange, "start", serializeSourcePosition(BeginLoc));
  serializeObject(SourceRange, "end", serializeSourcePosition(EndLoc));
  return SourceRange;
}

/// Serialize the availability attributes of a symbol.
///
/// Availability information contains the introduced, deprecated, and obsoleted
/// versions of the symbol as semantic versions, if not default.
/// Availability information also contains flags to indicate if the symbol is
/// unconditionally unavailable or deprecated,
/// i.e. \c __attribute__((unavailable)) and \c __attribute__((deprecated)).
///
/// \returns \c std::nullopt if the symbol has default availability attributes,
/// or an \c Array containing an object with the formatted availability
/// information.
std::optional<Array> serializeAvailability(const AvailabilityInfo &Avail) {
  if (Avail.isDefault())
    return std::nullopt;

  Array AvailabilityArray;

  if (Avail.isUnconditionallyDeprecated()) {
    Object UnconditionallyDeprecated;
    UnconditionallyDeprecated["domain"] = "*";
    UnconditionallyDeprecated["isUnconditionallyDeprecated"] = true;
    AvailabilityArray.emplace_back(std::move(UnconditionallyDeprecated));
  }
  Object Availability;

  Availability["domain"] = Avail.Domain;

  if (Avail.isUnavailable()) {
    Availability["isUnconditionallyUnavailable"] = true;
  } else {
    serializeObject(Availability, "introduced",
                    serializeSemanticVersion(Avail.Introduced));
    serializeObject(Availability, "deprecated",
                    serializeSemanticVersion(Avail.Deprecated));
    serializeObject(Availability, "obsoleted",
                    serializeSemanticVersion(Avail.Obsoleted));
  }

  AvailabilityArray.emplace_back(std::move(Availability));
  return AvailabilityArray;
}

/// Get the language name string for interface language references.
StringRef getLanguageName(Language Lang) {
  switch (Lang) {
  case Language::C:
    return "c";
  case Language::ObjC:
    return "objective-c";
  case Language::CXX:
    return "c++";
  case Language::ObjCXX:
    return "objective-c++";

  // Unsupported language currently
  case Language::OpenCL:
  case Language::OpenCLCXX:
  case Language::CUDA:
  case Language::RenderScript:
  case Language::HIP:
  case Language::HLSL:

  // Languages that the frontend cannot parse and compile
  case Language::Unknown:
  case Language::Asm:
  case Language::LLVM_IR:
  case Language::CIR:
    llvm_unreachable("Unsupported language kind");
  }

  llvm_unreachable("Unhandled language kind");
}

/// Serialize the identifier object as specified by the Symbol Graph format.
///
/// The identifier property of a symbol contains the USR for precise and unique
/// references, and the interface language name.
Object serializeIdentifier(const APIRecord &Record, Language Lang) {
  Object Identifier;
  Identifier["precise"] = Record.USR;
  Identifier["interfaceLanguage"] = getLanguageName(Lang);

  return Identifier;
}

/// Serialize the documentation comments attached to a symbol, as specified by
/// the Symbol Graph format.
///
/// The Symbol Graph \c docComment object contains an array of lines. Each line
/// represents one line of striped documentation comment, with source range
/// information.
/// e.g.
/// \code
///   /// This is a documentation comment
///       ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~'  First line.
///   ///     with multiple lines.
///       ^~~~~~~~~~~~~~~~~~~~~~~'         Second line.
/// \endcode
///
/// \returns \c std::nullopt if \p Comment is empty, or an \c Object containing
/// the formatted lines.
std::optional<Object> serializeDocComment(const DocComment &Comment) {
  if (Comment.empty())
    return std::nullopt;

  Object DocComment;

  Array LinesArray;
  for (const auto &CommentLine : Comment) {
    Object Line;
    Line["text"] = CommentLine.Text;
    serializeObject(Line, "range",
                    serializeSourceRange(CommentLine.Begin, CommentLine.End));
    LinesArray.emplace_back(std::move(Line));
  }

  serializeArray(DocComment, "lines", std::move(LinesArray));

  return DocComment;
}

/// Serialize the declaration fragments of a symbol.
///
/// The Symbol Graph declaration fragments is an array of tagged important
/// parts of a symbol's declaration. The fragments sequence can be joined to
/// form spans of declaration text, with attached information useful for
/// purposes like syntax-highlighting etc. For example:
/// \code
///   const int pi; -> "declarationFragments" : [
///                      {
///                        "kind" : "keyword",
///                        "spelling" : "const"
///                      },
///                      {
///                        "kind" : "text",
///                        "spelling" : " "
///                      },
///                      {
///                        "kind" : "typeIdentifier",
///                        "preciseIdentifier" : "c:I",
///                        "spelling" : "int"
///                      },
///                      {
///                        "kind" : "text",
///                        "spelling" : " "
///                      },
///                      {
///                        "kind" : "identifier",
///                        "spelling" : "pi"
///                      }
///                    ]
/// \endcode
///
/// \returns \c std::nullopt if \p DF is empty, or an \c Array containing the
/// formatted declaration fragments array.
std::optional<Array>
serializeDeclarationFragments(const DeclarationFragments &DF) {
  if (DF.getFragments().empty())
    return std::nullopt;

  Array Fragments;
  for (const auto &F : DF.getFragments()) {
    Object Fragment;
    Fragment["spelling"] = F.Spelling;
    Fragment["kind"] = DeclarationFragments::getFragmentKindString(F.Kind);
    if (!F.PreciseIdentifier.empty())
      Fragment["preciseIdentifier"] = F.PreciseIdentifier;
    Fragments.emplace_back(std::move(Fragment));
  }

  return Fragments;
}

/// Serialize the \c names field of a symbol as specified by the Symbol Graph
/// format.
///
/// The Symbol Graph names field contains multiple representations of a symbol
/// that can be used for different applications:
///   - \c title : The simple declared name of the symbol;
///   - \c subHeading : An array of declaration fragments that provides tags,
///     and potentially more tokens (for example the \c +/- symbol for
///     Objective-C methods). Can be used as sub-headings for documentation.
Object serializeNames(const APIRecord *Record) {
  Object Names;
  Names["title"] = Record->Name;

  serializeArray(Names, "subHeading",
                 serializeDeclarationFragments(Record->SubHeading));
  DeclarationFragments NavigatorFragments;
  NavigatorFragments.append(Record->Name,
                            DeclarationFragments::FragmentKind::Identifier,
                            /*PreciseIdentifier*/ "");
  serializeArray(Names, "navigator",
                 serializeDeclarationFragments(NavigatorFragments));

  return Names;
}

Object serializeSymbolKind(APIRecord::RecordKind RK, Language Lang) {
  auto AddLangPrefix = [&Lang](StringRef S) -> std::string {
    return (getLanguageName(Lang) + "." + S).str();
  };

  Object Kind;
  switch (RK) {
  case APIRecord::RK_Unknown:
    Kind["identifier"] = AddLangPrefix("unknown");
    Kind["displayName"] = "Unknown";
    break;
  case APIRecord::RK_Namespace:
    Kind["identifier"] = AddLangPrefix("namespace");
    Kind["displayName"] = "Namespace";
    break;
  case APIRecord::RK_GlobalFunction:
    Kind["identifier"] = AddLangPrefix("func");
    Kind["displayName"] = "Function";
    break;
  case APIRecord::RK_GlobalFunctionTemplate:
    Kind["identifier"] = AddLangPrefix("func");
    Kind["displayName"] = "Function Template";
    break;
  case APIRecord::RK_GlobalFunctionTemplateSpecialization:
    Kind["identifier"] = AddLangPrefix("func");
    Kind["displayName"] = "Function Template Specialization";
    break;
  case APIRecord::RK_GlobalVariableTemplate:
    Kind["identifier"] = AddLangPrefix("var");
    Kind["displayName"] = "Global Variable Template";
    break;
  case APIRecord::RK_GlobalVariableTemplateSpecialization:
    Kind["identifier"] = AddLangPrefix("var");
    Kind["displayName"] = "Global Variable Template Specialization";
    break;
  case APIRecord::RK_GlobalVariableTemplatePartialSpecialization:
    Kind["identifier"] = AddLangPrefix("var");
    Kind["displayName"] = "Global Variable Template Partial Specialization";
    break;
  case APIRecord::RK_GlobalVariable:
    Kind["identifier"] = AddLangPrefix("var");
    Kind["displayName"] = "Global Variable";
    break;
  case APIRecord::RK_EnumConstant:
    Kind["identifier"] = AddLangPrefix("enum.case");
    Kind["displayName"] = "Enumeration Case";
    break;
  case APIRecord::RK_Enum:
    Kind["identifier"] = AddLangPrefix("enum");
    Kind["displayName"] = "Enumeration";
    break;
  case APIRecord::RK_StructField:
    Kind["identifier"] = AddLangPrefix("property");
    Kind["displayName"] = "Instance Property";
    break;
  case APIRecord::RK_Struct:
    Kind["identifier"] = AddLangPrefix("struct");
    Kind["displayName"] = "Structure";
    break;
  case APIRecord::RK_UnionField:
    Kind["identifier"] = AddLangPrefix("property");
    Kind["displayName"] = "Instance Property";
    break;
  case APIRecord::RK_Union:
    Kind["identifier"] = AddLangPrefix("union");
    Kind["displayName"] = "Union";
    break;
  case APIRecord::RK_CXXField:
    Kind["identifier"] = AddLangPrefix("property");
    Kind["displayName"] = "Instance Property";
    break;
  case APIRecord::RK_StaticField:
    Kind["identifier"] = AddLangPrefix("type.property");
    Kind["displayName"] = "Type Property";
    break;
  case APIRecord::RK_ClassTemplate:
  case APIRecord::RK_ClassTemplateSpecialization:
  case APIRecord::RK_ClassTemplatePartialSpecialization:
  case APIRecord::RK_CXXClass:
    Kind["identifier"] = AddLangPrefix("class");
    Kind["displayName"] = "Class";
    break;
  case APIRecord::RK_CXXMethodTemplate:
    Kind["identifier"] = AddLangPrefix("method");
    Kind["displayName"] = "Method Template";
    break;
  case APIRecord::RK_CXXMethodTemplateSpecialization:
    Kind["identifier"] = AddLangPrefix("method");
    Kind["displayName"] = "Method Template Specialization";
    break;
  case APIRecord::RK_CXXFieldTemplate:
    Kind["identifier"] = AddLangPrefix("property");
    Kind["displayName"] = "Template Property";
    break;
  case APIRecord::RK_Concept:
    Kind["identifier"] = AddLangPrefix("concept");
    Kind["displayName"] = "Concept";
    break;
  case APIRecord::RK_CXXStaticMethod:
    Kind["identifier"] = AddLangPrefix("type.method");
    Kind["displayName"] = "Static Method";
    break;
  case APIRecord::RK_CXXInstanceMethod:
    Kind["identifier"] = AddLangPrefix("method");
    Kind["displayName"] = "Instance Method";
    break;
  case APIRecord::RK_CXXConstructorMethod:
    Kind["identifier"] = AddLangPrefix("method");
    Kind["displayName"] = "Constructor";
    break;
  case APIRecord::RK_CXXDestructorMethod:
    Kind["identifier"] = AddLangPrefix("method");
    Kind["displayName"] = "Destructor";
    break;
  case APIRecord::RK_ObjCIvar:
    Kind["identifier"] = AddLangPrefix("ivar");
    Kind["displayName"] = "Instance Variable";
    break;
  case APIRecord::RK_ObjCInstanceMethod:
    Kind["identifier"] = AddLangPrefix("method");
    Kind["displayName"] = "Instance Method";
    break;
  case APIRecord::RK_ObjCClassMethod:
    Kind["identifier"] = AddLangPrefix("type.method");
    Kind["displayName"] = "Type Method";
    break;
  case APIRecord::RK_ObjCInstanceProperty:
    Kind["identifier"] = AddLangPrefix("property");
    Kind["displayName"] = "Instance Property";
    break;
  case APIRecord::RK_ObjCClassProperty:
    Kind["identifier"] = AddLangPrefix("type.property");
    Kind["displayName"] = "Type Property";
    break;
  case APIRecord::RK_ObjCInterface:
    Kind["identifier"] = AddLangPrefix("class");
    Kind["displayName"] = "Class";
    break;
  case APIRecord::RK_ObjCCategory:
    Kind["identifier"] = AddLangPrefix("class.extension");
    Kind["displayName"] = "Class Extension";
    break;
  case APIRecord::RK_ObjCProtocol:
    Kind["identifier"] = AddLangPrefix("protocol");
    Kind["displayName"] = "Protocol";
    break;
  case APIRecord::RK_MacroDefinition:
    Kind["identifier"] = AddLangPrefix("macro");
    Kind["displayName"] = "Macro";
    break;
  case APIRecord::RK_Typedef:
    Kind["identifier"] = AddLangPrefix("typealias");
    Kind["displayName"] = "Type Alias";
    break;
  default:
    llvm_unreachable("API Record with uninstantiable kind");
  }

  return Kind;
}

/// Serialize the symbol kind information.
///
/// The Symbol Graph symbol kind property contains a shorthand \c identifier
/// which is prefixed by the source language name, useful for tooling to parse
/// the kind, and a \c displayName for rendering human-readable names.
Object serializeSymbolKind(const APIRecord &Record, Language Lang) {
  return serializeSymbolKind(Record.KindForDisplay, Lang);
}

/// Serialize the function signature field, as specified by the
/// Symbol Graph format.
///
/// The Symbol Graph function signature property contains two arrays.
///   - The \c returns array is the declaration fragments of the return type;
///   - The \c parameters array contains names and declaration fragments of the
///     parameters.
template <typename RecordTy>
void serializeFunctionSignatureMixin(Object &Paren, const RecordTy &Record) {
  const auto &FS = Record.Signature;
  if (FS.empty())
    return;

  Object Signature;
  serializeArray(Signature, "returns",
                 serializeDeclarationFragments(FS.getReturnType()));

  Array Parameters;
  for (const auto &P : FS.getParameters()) {
    Object Parameter;
    Parameter["name"] = P.Name;
    serializeArray(Parameter, "declarationFragments",
                   serializeDeclarationFragments(P.Fragments));
    Parameters.emplace_back(std::move(Parameter));
  }

  if (!Parameters.empty())
    Signature["parameters"] = std::move(Parameters);

  serializeObject(Paren, "functionSignature", std::move(Signature));
}

template <typename RecordTy>
void serializeTemplateMixin(Object &Paren, const RecordTy &Record) {
  const auto &Template = Record.Templ;
  if (Template.empty())
    return;

  Object Generics;
  Array GenericParameters;
  for (const auto &Param : Template.getParameters()) {
    Object Parameter;
    Parameter["name"] = Param.Name;
    Parameter["index"] = Param.Index;
    Parameter["depth"] = Param.Depth;
    GenericParameters.emplace_back(std::move(Parameter));
  }
  if (!GenericParameters.empty())
    Generics["parameters"] = std::move(GenericParameters);

  Array GenericConstraints;
  for (const auto &Constr : Template.getConstraints()) {
    Object Constraint;
    Constraint["kind"] = Constr.Kind;
    Constraint["lhs"] = Constr.LHS;
    Constraint["rhs"] = Constr.RHS;
    GenericConstraints.emplace_back(std::move(Constraint));
  }

  if (!GenericConstraints.empty())
    Generics["constraints"] = std::move(GenericConstraints);

  serializeObject(Paren, "swiftGenerics", Generics);
}

Array generateParentContexts(const SmallVectorImpl<SymbolReference> &Parents,
                             Language Lang) {
  Array ParentContexts;

  for (const auto &Parent : Parents) {
    Object Elem;
    Elem["usr"] = Parent.USR;
    Elem["name"] = Parent.Name;
    if (Parent.Record)
      Elem["kind"] = serializeSymbolKind(Parent.Record->KindForDisplay,
                                         Lang)["identifier"];
    else
      Elem["kind"] =
          serializeSymbolKind(APIRecord::RK_Unknown, Lang)["identifier"];
    ParentContexts.emplace_back(std::move(Elem));
  }

  return ParentContexts;
}

/// Walk the records parent information in reverse to generate a hierarchy
/// suitable for serialization.
SmallVector<SymbolReference, 8>
generateHierarchyFromRecord(const APIRecord *Record) {
  SmallVector<SymbolReference, 8> ReverseHierarchy;
  for (const auto *Current = Record; Current != nullptr;
       Current = Current->Parent.Record)
    ReverseHierarchy.emplace_back(Current);

  return SmallVector<SymbolReference, 8>(
      std::make_move_iterator(ReverseHierarchy.rbegin()),
      std::make_move_iterator(ReverseHierarchy.rend()));
}

SymbolReference getHierarchyReference(const APIRecord *Record,
                                      const APISet &API) {
  // If the parent is a category extended from internal module then we need to
  // pretend this belongs to the associated interface.
  if (auto *CategoryRecord = dyn_cast_or_null<ObjCCategoryRecord>(Record)) {
    return CategoryRecord->Interface;
    // FIXME: TODO generate path components correctly for categories extending
    // an external module.
  }

  return SymbolReference(Record);
}

} // namespace

Object *ExtendedModule::addSymbol(Object &&Symbol) {
  Symbols.emplace_back(std::move(Symbol));
  return Symbols.back().getAsObject();
}

void ExtendedModule::addRelationship(Object &&Relationship) {
  Relationships.emplace_back(std::move(Relationship));
}

/// Defines the format version emitted by SymbolGraphSerializer.
const VersionTuple SymbolGraphSerializer::FormatVersion{0, 5, 3};

Object SymbolGraphSerializer::serializeMetadata() const {
  Object Metadata;
  serializeObject(Metadata, "formatVersion",
                  serializeSemanticVersion(FormatVersion));
  Metadata["generator"] = clang::getClangFullVersion();
  return Metadata;
}

Object
SymbolGraphSerializer::serializeModuleObject(StringRef ModuleName) const {
  Object Module;
  Module["name"] = ModuleName;
  serializeObject(Module, "platform", serializePlatform(API.getTarget()));
  return Module;
}

bool SymbolGraphSerializer::shouldSkip(const APIRecord *Record) const {
  if (!Record)
    return true;

  // Skip unconditionally unavailable symbols
  if (Record->Availability.isUnconditionallyUnavailable())
    return true;

  // Filter out symbols without a name as we can generate correct symbol graphs
  // for them. In practice these are anonymous record types that aren't attached
  // to a declaration.
  if (auto *Tag = dyn_cast<TagRecord>(Record)) {
    if (Tag->IsEmbeddedInVarDeclarator)
      return true;
  }

  // Filter out symbols prefixed with an underscored as they are understood to
  // be symbols clients should not use.
  if (Record->Name.starts_with("_"))
    return true;

  // Skip explicitly ignored symbols.
  if (IgnoresList.shouldIgnore(Record->Name))
    return true;

  return false;
}

ExtendedModule &SymbolGraphSerializer::getModuleForCurrentSymbol() {
  if (!ForceEmitToMainModule && ModuleForCurrentSymbol)
    return *ModuleForCurrentSymbol;

  return MainModule;
}

Array SymbolGraphSerializer::serializePathComponents(
    const APIRecord *Record) const {
  return Array(map_range(Hierarchy, [](auto Elt) { return Elt.Name; }));
}

StringRef SymbolGraphSerializer::getRelationshipString(RelationshipKind Kind) {
  switch (Kind) {
  case RelationshipKind::MemberOf:
    return "memberOf";
  case RelationshipKind::InheritsFrom:
    return "inheritsFrom";
  case RelationshipKind::ConformsTo:
    return "conformsTo";
  case RelationshipKind::ExtensionTo:
    return "extensionTo";
  }
  llvm_unreachable("Unhandled relationship kind");
}

void SymbolGraphSerializer::serializeRelationship(RelationshipKind Kind,
                                                  const SymbolReference &Source,
                                                  const SymbolReference &Target,
                                                  ExtendedModule &Into) {
  Object Relationship;
  SmallString<64> TestRelLabel;
  if (EmitSymbolLabelsForTesting) {
    llvm::raw_svector_ostream OS(TestRelLabel);
    OS << SymbolGraphSerializer::getRelationshipString(Kind) << " $ "
       << Source.USR << " $ ";
    if (Target.USR.empty())
      OS << Target.Name;
    else
      OS << Target.USR;
    Relationship["!testRelLabel"] = TestRelLabel;
  }
  Relationship["source"] = Source.USR;
  Relationship["target"] = Target.USR;
  Relationship["targetFallback"] = Target.Name;
  Relationship["kind"] = SymbolGraphSerializer::getRelationshipString(Kind);

  if (ForceEmitToMainModule)
    MainModule.addRelationship(std::move(Relationship));
  else
    Into.addRelationship(std::move(Relationship));
}

StringRef SymbolGraphSerializer::getConstraintString(ConstraintKind Kind) {
  switch (Kind) {
  case ConstraintKind::Conformance:
    return "conformance";
  case ConstraintKind::ConditionalConformance:
    return "conditionalConformance";
  }
  llvm_unreachable("Unhandled constraint kind");
}

void SymbolGraphSerializer::serializeAPIRecord(const APIRecord *Record) {
  Object Obj;

  // If we need symbol labels for testing emit the USR as the value and the key
  // starts with '!'' to ensure it ends up at the top of the object.
  if (EmitSymbolLabelsForTesting)
    Obj["!testLabel"] = Record->USR;

  serializeObject(Obj, "identifier",
                  serializeIdentifier(*Record, API.getLanguage()));
  serializeObject(Obj, "kind", serializeSymbolKind(*Record, API.getLanguage()));
  serializeObject(Obj, "names", serializeNames(Record));
  serializeObject(
      Obj, "location",
      serializeSourceLocation(Record->Location, /*IncludeFileURI=*/true));
  serializeArray(Obj, "availability",
                 serializeAvailability(Record->Availability));
  serializeObject(Obj, "docComment", serializeDocComment(Record->Comment));
  serializeArray(Obj, "declarationFragments",
                 serializeDeclarationFragments(Record->Declaration));

  Obj["pathComponents"] = serializePathComponents(Record);
  Obj["accessLevel"] = Record->Access.getAccess();

  ExtendedModule &Module = getModuleForCurrentSymbol();
  // If the hierarchy has at least one parent and child.
  if (Hierarchy.size() >= 2)
    serializeRelationship(MemberOf, Hierarchy.back(),
                          Hierarchy[Hierarchy.size() - 2], Module);

  CurrentSymbol = Module.addSymbol(std::move(Obj));
}

bool SymbolGraphSerializer::traverseAPIRecord(const APIRecord *Record) {
  if (!Record)
    return true;
  if (shouldSkip(Record))
    return true;
  Hierarchy.push_back(getHierarchyReference(Record, API));
  // Defer traversal mechanics to APISetVisitor base implementation
  auto RetVal = Base::traverseAPIRecord(Record);
  Hierarchy.pop_back();
  return RetVal;
}

bool SymbolGraphSerializer::visitAPIRecord(const APIRecord *Record) {
  serializeAPIRecord(Record);
  return true;
}

bool SymbolGraphSerializer::visitGlobalFunctionRecord(
    const GlobalFunctionRecord *Record) {
  if (!CurrentSymbol)
    return true;

  serializeFunctionSignatureMixin(*CurrentSymbol, *Record);
  return true;
}

bool SymbolGraphSerializer::visitCXXClassRecord(const CXXClassRecord *Record) {
  if (!CurrentSymbol)
    return true;

  for (const auto &Base : Record->Bases)
    serializeRelationship(RelationshipKind::InheritsFrom, Record, Base,
                          getModuleForCurrentSymbol());
  return true;
}

bool SymbolGraphSerializer::visitClassTemplateRecord(
    const ClassTemplateRecord *Record) {
  if (!CurrentSymbol)
    return true;

  serializeTemplateMixin(*CurrentSymbol, *Record);
  return true;
}

bool SymbolGraphSerializer::visitClassTemplatePartialSpecializationRecord(
    const ClassTemplatePartialSpecializationRecord *Record) {
  if (!CurrentSymbol)
    return true;

  serializeTemplateMixin(*CurrentSymbol, *Record);
  return true;
}

bool SymbolGraphSerializer::visitCXXMethodRecord(
    const CXXMethodRecord *Record) {
  if (!CurrentSymbol)
    return true;

  serializeFunctionSignatureMixin(*CurrentSymbol, *Record);
  return true;
}

bool SymbolGraphSerializer::visitCXXMethodTemplateRecord(
    const CXXMethodTemplateRecord *Record) {
  if (!CurrentSymbol)
    return true;

  serializeTemplateMixin(*CurrentSymbol, *Record);
  return true;
}

bool SymbolGraphSerializer::visitCXXFieldTemplateRecord(
    const CXXFieldTemplateRecord *Record) {
  if (!CurrentSymbol)
    return true;

  serializeTemplateMixin(*CurrentSymbol, *Record);
  return true;
}

bool SymbolGraphSerializer::visitConceptRecord(const ConceptRecord *Record) {
  if (!CurrentSymbol)
    return true;

  serializeTemplateMixin(*CurrentSymbol, *Record);
  return true;
}

bool SymbolGraphSerializer::visitGlobalVariableTemplateRecord(
    const GlobalVariableTemplateRecord *Record) {
  if (!CurrentSymbol)
    return true;

  serializeTemplateMixin(*CurrentSymbol, *Record);
  return true;
}

bool SymbolGraphSerializer::
    visitGlobalVariableTemplatePartialSpecializationRecord(
        const GlobalVariableTemplatePartialSpecializationRecord *Record) {
  if (!CurrentSymbol)
    return true;

  serializeTemplateMixin(*CurrentSymbol, *Record);
  return true;
}

bool SymbolGraphSerializer::visitGlobalFunctionTemplateRecord(
    const GlobalFunctionTemplateRecord *Record) {
  if (!CurrentSymbol)
    return true;

  serializeTemplateMixin(*CurrentSymbol, *Record);
  return true;
}

bool SymbolGraphSerializer::visitObjCContainerRecord(
    const ObjCContainerRecord *Record) {
  if (!CurrentSymbol)
    return true;

  for (const auto &Protocol : Record->Protocols)
    serializeRelationship(ConformsTo, Record, Protocol,
                          getModuleForCurrentSymbol());

  return true;
}

bool SymbolGraphSerializer::visitObjCInterfaceRecord(
    const ObjCInterfaceRecord *Record) {
  if (!CurrentSymbol)
    return true;

  if (!Record->SuperClass.empty())
    serializeRelationship(InheritsFrom, Record, Record->SuperClass,
                          getModuleForCurrentSymbol());
  return true;
}

bool SymbolGraphSerializer::traverseObjCCategoryRecord(
    const ObjCCategoryRecord *Record) {
  if (SkipSymbolsInCategoriesToExternalTypes &&
      !API.findRecordForUSR(Record->Interface.USR))
    return true;

  auto *CurrentModule = ModuleForCurrentSymbol;
  if (Record->isExtendingExternalModule())
    ModuleForCurrentSymbol = &ExtendedModules[Record->Interface.Source];

  if (!walkUpFromObjCCategoryRecord(Record))
    return false;

  bool RetVal = traverseRecordContext(Record);
  ModuleForCurrentSymbol = CurrentModule;
  return RetVal;
}

bool SymbolGraphSerializer::walkUpFromObjCCategoryRecord(
    const ObjCCategoryRecord *Record) {
  return visitObjCCategoryRecord(Record);
}

bool SymbolGraphSerializer::visitObjCCategoryRecord(
    const ObjCCategoryRecord *Record) {
  // If we need to create a record for the category in the future do so here,
  // otherwise everything is set up to pretend that the category is in fact the
  // interface it extends.
  for (const auto &Protocol : Record->Protocols)
    serializeRelationship(ConformsTo, Record->Interface, Protocol,
                          getModuleForCurrentSymbol());

  return true;
}

bool SymbolGraphSerializer::visitObjCMethodRecord(
    const ObjCMethodRecord *Record) {
  if (!CurrentSymbol)
    return true;

  serializeFunctionSignatureMixin(*CurrentSymbol, *Record);
  return true;
}

bool SymbolGraphSerializer::visitObjCInstanceVariableRecord(
    const ObjCInstanceVariableRecord *Record) {
  // FIXME: serialize ivar access control here.
  return true;
}

bool SymbolGraphSerializer::walkUpFromTypedefRecord(
    const TypedefRecord *Record) {
  // Short-circuit walking up the class hierarchy and handle creating typedef
  // symbol objects manually as there are additional symbol dropping rules to
  // respect.
  return visitTypedefRecord(Record);
}

bool SymbolGraphSerializer::visitTypedefRecord(const TypedefRecord *Record) {
  // Typedefs of anonymous types have their entries unified with the underlying
  // type.
  bool ShouldDrop = Record->UnderlyingType.Name.empty();
  // enums declared with `NS_OPTION` have a named enum and a named typedef, with
  // the same name
  ShouldDrop |= (Record->UnderlyingType.Name == Record->Name);
  if (ShouldDrop)
    return true;

  // Create the symbol record if the other symbol droppping rules permit it.
  serializeAPIRecord(Record);
  if (!CurrentSymbol)
    return true;

  (*CurrentSymbol)["type"] = Record->UnderlyingType.USR;

  return true;
}

void SymbolGraphSerializer::serializeSingleRecord(const APIRecord *Record) {
  switch (Record->getKind()) {
    // dispatch to the relevant walkUpFromMethod
#define CONCRETE_RECORD(CLASS, BASE, KIND)                                     \
  case APIRecord::KIND: {                                                      \
    walkUpFrom##CLASS(static_cast<const CLASS *>(Record));                     \
    break;                                                                     \
  }
#include "clang/ExtractAPI/APIRecords.inc"
  // otherwise fallback on the only behavior we can implement safely.
  case APIRecord::RK_Unknown:
    visitAPIRecord(Record);
    break;
  default:
    llvm_unreachable("API Record with uninstantiable kind");
  }
}

Object SymbolGraphSerializer::serializeGraph(StringRef ModuleName,
                                             ExtendedModule &&EM) {
  Object Root;
  serializeObject(Root, "metadata", serializeMetadata());
  serializeObject(Root, "module", serializeModuleObject(ModuleName));

  Root["symbols"] = std::move(EM.Symbols);
  Root["relationships"] = std::move(EM.Relationships);

  return Root;
}

void SymbolGraphSerializer::serializeGraphToStream(
    raw_ostream &OS, SymbolGraphSerializerOption Options, StringRef ModuleName,
    ExtendedModule &&EM) {
  Object Root = serializeGraph(ModuleName, std::move(EM));
  if (Options.Compact)
    OS << formatv("{0}", json::Value(std::move(Root))) << "\n";
  else
    OS << formatv("{0:2}", json::Value(std::move(Root))) << "\n";
}

void SymbolGraphSerializer::serializeMainSymbolGraph(
    raw_ostream &OS, const APISet &API, const APIIgnoresList &IgnoresList,
    SymbolGraphSerializerOption Options) {
  SymbolGraphSerializer Serializer(
      API, IgnoresList, Options.EmitSymbolLabelsForTesting,
      /*ForceEmitToMainModule=*/true,
      /*SkipSymbolsInCategoriesToExternalTypes=*/true);

  Serializer.traverseAPISet();
  Serializer.serializeGraphToStream(OS, Options, API.ProductName,
                                    std::move(Serializer.MainModule));
  // FIXME: TODO handle extended modules here
}

void SymbolGraphSerializer::serializeWithExtensionGraphs(
    raw_ostream &MainOutput, const APISet &API,
    const APIIgnoresList &IgnoresList,
    llvm::function_ref<std::unique_ptr<llvm::raw_pwrite_stream>(Twine BaseName)>
        CreateOutputStream,
    SymbolGraphSerializerOption Options) {
  SymbolGraphSerializer Serializer(API, IgnoresList,
                                   Options.EmitSymbolLabelsForTesting);
  Serializer.traverseAPISet();

  Serializer.serializeGraphToStream(MainOutput, Options, API.ProductName,
                                    std::move(Serializer.MainModule));

  for (auto &ExtensionSGF : Serializer.ExtendedModules) {
    if (auto ExtensionOS =
            CreateOutputStream(ExtensionSGF.getKey() + "@" + API.ProductName))
      Serializer.serializeGraphToStream(*ExtensionOS, Options,
                                        ExtensionSGF.getKey(),
                                        std::move(ExtensionSGF.getValue()));
  }
}

std::optional<Object>
SymbolGraphSerializer::serializeSingleSymbolSGF(StringRef USR,
                                                const APISet &API) {
  APIRecord *Record = API.findRecordForUSR(USR);
  if (!Record)
    return {};

  Object Root;
  APIIgnoresList EmptyIgnores;
  SymbolGraphSerializer Serializer(API, EmptyIgnores,
                                   /*EmitSymbolLabelsForTesting*/ false,
                                   /*ForceEmitToMainModule*/ true);

  // Set up serializer parent chain
  Serializer.Hierarchy = generateHierarchyFromRecord(Record);

  Serializer.serializeSingleRecord(Record);
  serializeObject(Root, "symbolGraph",
                  Serializer.serializeGraph(API.ProductName,
                                            std::move(Serializer.MainModule)));

  Language Lang = API.getLanguage();
  serializeArray(Root, "parentContexts",
                 generateParentContexts(Serializer.Hierarchy, Lang));

  Array RelatedSymbols;

  for (const auto &Fragment : Record->Declaration.getFragments()) {
    // If we don't have a USR there isn't much we can do.
    if (Fragment.PreciseIdentifier.empty())
      continue;

    APIRecord *RelatedRecord = API.findRecordForUSR(Fragment.PreciseIdentifier);

    // If we can't find the record let's skip.
    if (!RelatedRecord)
      continue;

    Object RelatedSymbol;
    RelatedSymbol["usr"] = RelatedRecord->USR;
    RelatedSymbol["declarationLanguage"] = getLanguageName(Lang);
    RelatedSymbol["accessLevel"] = RelatedRecord->Access.getAccess();
    RelatedSymbol["filePath"] = RelatedRecord->Location.getFilename();
    RelatedSymbol["moduleName"] = API.ProductName;
    RelatedSymbol["isSystem"] = RelatedRecord->IsFromSystemHeader;

    serializeArray(RelatedSymbol, "parentContexts",
                   generateParentContexts(
                       generateHierarchyFromRecord(RelatedRecord), Lang));

    RelatedSymbols.push_back(std::move(RelatedSymbol));
  }

  serializeArray(Root, "relatedSymbols", RelatedSymbols);
  return Root;
}
