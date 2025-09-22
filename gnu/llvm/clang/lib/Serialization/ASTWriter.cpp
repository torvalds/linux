//===- ASTWriter.cpp - AST File Writer ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ASTWriter class, which writes AST files.
//
//===----------------------------------------------------------------------===//

#include "ASTCommon.h"
#include "ASTReaderInternals.h"
#include "MultiOnDiskHashTable.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTUnresolvedSet.h"
#include "clang/AST/AbstractTypeWriter.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclContextInternals.h"
#include "clang/AST/DeclFriend.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/LambdaCapture.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/OpenACCClause.h"
#include "clang/AST/OpenMPClause.h"
#include "clang/AST/RawCommentList.h"
#include "clang/AST/TemplateName.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLocVisitor.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/FileSystemOptions.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/Lambda.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/Module.h"
#include "clang/Basic/ObjCRuntime.h"
#include "clang/Basic/OpenACCKinds.h"
#include "clang/Basic/OpenCLOptions.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/SourceManagerInternals.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "clang/Basic/Version.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/HeaderSearchOptions.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/ModuleMap.h"
#include "clang/Lex/PreprocessingRecord.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "clang/Lex/Token.h"
#include "clang/Sema/IdentifierResolver.h"
#include "clang/Sema/ObjCMethodList.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaCUDA.h"
#include "clang/Sema/SemaObjC.h"
#include "clang/Sema/Weak.h"
#include "clang/Serialization/ASTBitCodes.h"
#include "clang/Serialization/ASTReader.h"
#include "clang/Serialization/ASTRecordWriter.h"
#include "clang/Serialization/InMemoryModuleCache.h"
#include "clang/Serialization/ModuleFile.h"
#include "clang/Serialization/ModuleFileExtension.h"
#include "clang/Serialization/SerializationDiagnostic.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Bitstream/BitCodes.h"
#include "llvm/Bitstream/BitstreamWriter.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/DJB.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/OnDiskHashTable.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SHA1.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits>
#include <memory>
#include <optional>
#include <queue>
#include <tuple>
#include <utility>
#include <vector>

using namespace clang;
using namespace clang::serialization;

template <typename T, typename Allocator>
static StringRef bytes(const std::vector<T, Allocator> &v) {
  if (v.empty()) return StringRef();
  return StringRef(reinterpret_cast<const char*>(&v[0]),
                         sizeof(T) * v.size());
}

template <typename T>
static StringRef bytes(const SmallVectorImpl<T> &v) {
  return StringRef(reinterpret_cast<const char*>(v.data()),
                         sizeof(T) * v.size());
}

static std::string bytes(const std::vector<bool> &V) {
  std::string Str;
  Str.reserve(V.size() / 8);
  for (unsigned I = 0, E = V.size(); I < E;) {
    char Byte = 0;
    for (unsigned Bit = 0; Bit < 8 && I < E; ++Bit, ++I)
      Byte |= V[I] << Bit;
    Str += Byte;
  }
  return Str;
}

//===----------------------------------------------------------------------===//
// Type serialization
//===----------------------------------------------------------------------===//

static TypeCode getTypeCodeForTypeClass(Type::TypeClass id) {
  switch (id) {
#define TYPE_BIT_CODE(CLASS_ID, CODE_ID, CODE_VALUE) \
  case Type::CLASS_ID: return TYPE_##CODE_ID;
#include "clang/Serialization/TypeBitCodes.def"
  case Type::Builtin:
    llvm_unreachable("shouldn't be serializing a builtin type this way");
  }
  llvm_unreachable("bad type kind");
}

namespace {

std::optional<std::set<const FileEntry *>>
GetAffectingModuleMaps(const Preprocessor &PP, Module *RootModule) {
  if (!PP.getHeaderSearchInfo()
           .getHeaderSearchOpts()
           .ModulesPruneNonAffectingModuleMaps)
    return std::nullopt;

  const HeaderSearch &HS = PP.getHeaderSearchInfo();
  const ModuleMap &MM = HS.getModuleMap();

  std::set<const FileEntry *> ModuleMaps;
  std::set<const Module *> ProcessedModules;
  auto CollectModuleMapsForHierarchy = [&](const Module *M) {
    M = M->getTopLevelModule();

    if (!ProcessedModules.insert(M).second)
      return;

    std::queue<const Module *> Q;
    Q.push(M);
    while (!Q.empty()) {
      const Module *Mod = Q.front();
      Q.pop();

      // The containing module map is affecting, because it's being pointed
      // into by Module::DefinitionLoc.
      if (auto FE = MM.getContainingModuleMapFile(Mod))
        ModuleMaps.insert(*FE);
      // For inferred modules, the module map that allowed inferring is not
      // related to the virtual containing module map file. It did affect the
      // compilation, though.
      if (auto FE = MM.getModuleMapFileForUniquing(Mod))
        ModuleMaps.insert(*FE);

      for (auto *SubM : Mod->submodules())
        Q.push(SubM);
    }
  };

  // Handle all the affecting modules referenced from the root module.

  CollectModuleMapsForHierarchy(RootModule);

  std::queue<const Module *> Q;
  Q.push(RootModule);
  while (!Q.empty()) {
    const Module *CurrentModule = Q.front();
    Q.pop();

    for (const Module *ImportedModule : CurrentModule->Imports)
      CollectModuleMapsForHierarchy(ImportedModule);
    for (const Module *UndeclaredModule : CurrentModule->UndeclaredUses)
      CollectModuleMapsForHierarchy(UndeclaredModule);

    for (auto *M : CurrentModule->submodules())
      Q.push(M);
  }

  // Handle textually-included headers that belong to other modules.

  SmallVector<OptionalFileEntryRef, 16> FilesByUID;
  HS.getFileMgr().GetUniqueIDMapping(FilesByUID);

  if (FilesByUID.size() > HS.header_file_size())
    FilesByUID.resize(HS.header_file_size());

  for (unsigned UID = 0, LastUID = FilesByUID.size(); UID != LastUID; ++UID) {
    OptionalFileEntryRef File = FilesByUID[UID];
    if (!File)
      continue;

    const HeaderFileInfo *HFI = HS.getExistingLocalFileInfo(*File);
    if (!HFI)
      continue; // We have no information on this being a header file.
    if (!HFI->isCompilingModuleHeader && HFI->isModuleHeader)
      continue; // Modular header, handled in the above module-based loop.
    if (!HFI->isCompilingModuleHeader && !HFI->IsLocallyIncluded)
      continue; // Non-modular header not included locally is not affecting.

    for (const auto &KH : HS.findResolvedModulesForHeader(*File))
      if (const Module *M = KH.getModule())
        CollectModuleMapsForHierarchy(M);
  }

  // FIXME: This algorithm is not correct for module map hierarchies where
  // module map file defining a (sub)module of a top-level module X includes
  // a module map file that defines a (sub)module of another top-level module Y.
  // Whenever X is affecting and Y is not, "replaying" this PCM file will fail
  // when parsing module map files for X due to not knowing about the `extern`
  // module map for Y.
  //
  // We don't have a good way to fix it here. We could mark all children of
  // affecting module map files as being affecting as well, but that's
  // expensive. SourceManager does not model the edge from parent to child
  // SLocEntries, so instead, we would need to iterate over leaf module map
  // files, walk up their include hierarchy and check whether we arrive at an
  // affecting module map.
  //
  // Instead of complicating and slowing down this function, we should probably
  // just ban module map hierarchies where module map defining a (sub)module X
  // includes a module map defining a module that's not a submodule of X.

  return ModuleMaps;
}

class ASTTypeWriter {
  ASTWriter &Writer;
  ASTWriter::RecordData Record;
  ASTRecordWriter BasicWriter;

public:
  ASTTypeWriter(ASTWriter &Writer)
    : Writer(Writer), BasicWriter(Writer, Record) {}

  uint64_t write(QualType T) {
    if (T.hasLocalNonFastQualifiers()) {
      Qualifiers Qs = T.getLocalQualifiers();
      BasicWriter.writeQualType(T.getLocalUnqualifiedType());
      BasicWriter.writeQualifiers(Qs);
      return BasicWriter.Emit(TYPE_EXT_QUAL, Writer.getTypeExtQualAbbrev());
    }

    const Type *typePtr = T.getTypePtr();
    serialization::AbstractTypeWriter<ASTRecordWriter> atw(BasicWriter);
    atw.write(typePtr);
    return BasicWriter.Emit(getTypeCodeForTypeClass(typePtr->getTypeClass()),
                            /*abbrev*/ 0);
  }
};

class TypeLocWriter : public TypeLocVisitor<TypeLocWriter> {
  using LocSeq = SourceLocationSequence;

  ASTRecordWriter &Record;
  LocSeq *Seq;

  void addSourceLocation(SourceLocation Loc) {
    Record.AddSourceLocation(Loc, Seq);
  }
  void addSourceRange(SourceRange Range) { Record.AddSourceRange(Range, Seq); }

public:
  TypeLocWriter(ASTRecordWriter &Record, LocSeq *Seq)
      : Record(Record), Seq(Seq) {}

#define ABSTRACT_TYPELOC(CLASS, PARENT)
#define TYPELOC(CLASS, PARENT) \
    void Visit##CLASS##TypeLoc(CLASS##TypeLoc TyLoc);
#include "clang/AST/TypeLocNodes.def"

  void VisitArrayTypeLoc(ArrayTypeLoc TyLoc);
  void VisitFunctionTypeLoc(FunctionTypeLoc TyLoc);
};

} // namespace

void TypeLocWriter::VisitQualifiedTypeLoc(QualifiedTypeLoc TL) {
  // nothing to do
}

void TypeLocWriter::VisitBuiltinTypeLoc(BuiltinTypeLoc TL) {
  addSourceLocation(TL.getBuiltinLoc());
  if (TL.needsExtraLocalData()) {
    Record.push_back(TL.getWrittenTypeSpec());
    Record.push_back(static_cast<uint64_t>(TL.getWrittenSignSpec()));
    Record.push_back(static_cast<uint64_t>(TL.getWrittenWidthSpec()));
    Record.push_back(TL.hasModeAttr());
  }
}

void TypeLocWriter::VisitComplexTypeLoc(ComplexTypeLoc TL) {
  addSourceLocation(TL.getNameLoc());
}

void TypeLocWriter::VisitPointerTypeLoc(PointerTypeLoc TL) {
  addSourceLocation(TL.getStarLoc());
}

void TypeLocWriter::VisitDecayedTypeLoc(DecayedTypeLoc TL) {
  // nothing to do
}

void TypeLocWriter::VisitAdjustedTypeLoc(AdjustedTypeLoc TL) {
  // nothing to do
}

void TypeLocWriter::VisitArrayParameterTypeLoc(ArrayParameterTypeLoc TL) {
  // nothing to do
}

void TypeLocWriter::VisitBlockPointerTypeLoc(BlockPointerTypeLoc TL) {
  addSourceLocation(TL.getCaretLoc());
}

void TypeLocWriter::VisitLValueReferenceTypeLoc(LValueReferenceTypeLoc TL) {
  addSourceLocation(TL.getAmpLoc());
}

void TypeLocWriter::VisitRValueReferenceTypeLoc(RValueReferenceTypeLoc TL) {
  addSourceLocation(TL.getAmpAmpLoc());
}

void TypeLocWriter::VisitMemberPointerTypeLoc(MemberPointerTypeLoc TL) {
  addSourceLocation(TL.getStarLoc());
  Record.AddTypeSourceInfo(TL.getClassTInfo());
}

void TypeLocWriter::VisitArrayTypeLoc(ArrayTypeLoc TL) {
  addSourceLocation(TL.getLBracketLoc());
  addSourceLocation(TL.getRBracketLoc());
  Record.push_back(TL.getSizeExpr() ? 1 : 0);
  if (TL.getSizeExpr())
    Record.AddStmt(TL.getSizeExpr());
}

void TypeLocWriter::VisitConstantArrayTypeLoc(ConstantArrayTypeLoc TL) {
  VisitArrayTypeLoc(TL);
}

void TypeLocWriter::VisitIncompleteArrayTypeLoc(IncompleteArrayTypeLoc TL) {
  VisitArrayTypeLoc(TL);
}

void TypeLocWriter::VisitVariableArrayTypeLoc(VariableArrayTypeLoc TL) {
  VisitArrayTypeLoc(TL);
}

void TypeLocWriter::VisitDependentSizedArrayTypeLoc(
                                            DependentSizedArrayTypeLoc TL) {
  VisitArrayTypeLoc(TL);
}

void TypeLocWriter::VisitDependentAddressSpaceTypeLoc(
    DependentAddressSpaceTypeLoc TL) {
  addSourceLocation(TL.getAttrNameLoc());
  SourceRange range = TL.getAttrOperandParensRange();
  addSourceLocation(range.getBegin());
  addSourceLocation(range.getEnd());
  Record.AddStmt(TL.getAttrExprOperand());
}

void TypeLocWriter::VisitDependentSizedExtVectorTypeLoc(
                                        DependentSizedExtVectorTypeLoc TL) {
  addSourceLocation(TL.getNameLoc());
}

void TypeLocWriter::VisitVectorTypeLoc(VectorTypeLoc TL) {
  addSourceLocation(TL.getNameLoc());
}

void TypeLocWriter::VisitDependentVectorTypeLoc(
    DependentVectorTypeLoc TL) {
  addSourceLocation(TL.getNameLoc());
}

void TypeLocWriter::VisitExtVectorTypeLoc(ExtVectorTypeLoc TL) {
  addSourceLocation(TL.getNameLoc());
}

void TypeLocWriter::VisitConstantMatrixTypeLoc(ConstantMatrixTypeLoc TL) {
  addSourceLocation(TL.getAttrNameLoc());
  SourceRange range = TL.getAttrOperandParensRange();
  addSourceLocation(range.getBegin());
  addSourceLocation(range.getEnd());
  Record.AddStmt(TL.getAttrRowOperand());
  Record.AddStmt(TL.getAttrColumnOperand());
}

void TypeLocWriter::VisitDependentSizedMatrixTypeLoc(
    DependentSizedMatrixTypeLoc TL) {
  addSourceLocation(TL.getAttrNameLoc());
  SourceRange range = TL.getAttrOperandParensRange();
  addSourceLocation(range.getBegin());
  addSourceLocation(range.getEnd());
  Record.AddStmt(TL.getAttrRowOperand());
  Record.AddStmt(TL.getAttrColumnOperand());
}

void TypeLocWriter::VisitFunctionTypeLoc(FunctionTypeLoc TL) {
  addSourceLocation(TL.getLocalRangeBegin());
  addSourceLocation(TL.getLParenLoc());
  addSourceLocation(TL.getRParenLoc());
  addSourceRange(TL.getExceptionSpecRange());
  addSourceLocation(TL.getLocalRangeEnd());
  for (unsigned i = 0, e = TL.getNumParams(); i != e; ++i)
    Record.AddDeclRef(TL.getParam(i));
}

void TypeLocWriter::VisitFunctionProtoTypeLoc(FunctionProtoTypeLoc TL) {
  VisitFunctionTypeLoc(TL);
}

void TypeLocWriter::VisitFunctionNoProtoTypeLoc(FunctionNoProtoTypeLoc TL) {
  VisitFunctionTypeLoc(TL);
}

void TypeLocWriter::VisitUnresolvedUsingTypeLoc(UnresolvedUsingTypeLoc TL) {
  addSourceLocation(TL.getNameLoc());
}

void TypeLocWriter::VisitUsingTypeLoc(UsingTypeLoc TL) {
  addSourceLocation(TL.getNameLoc());
}

void TypeLocWriter::VisitTypedefTypeLoc(TypedefTypeLoc TL) {
  addSourceLocation(TL.getNameLoc());
}

void TypeLocWriter::VisitObjCTypeParamTypeLoc(ObjCTypeParamTypeLoc TL) {
  if (TL.getNumProtocols()) {
    addSourceLocation(TL.getProtocolLAngleLoc());
    addSourceLocation(TL.getProtocolRAngleLoc());
  }
  for (unsigned i = 0, e = TL.getNumProtocols(); i != e; ++i)
    addSourceLocation(TL.getProtocolLoc(i));
}

void TypeLocWriter::VisitTypeOfExprTypeLoc(TypeOfExprTypeLoc TL) {
  addSourceLocation(TL.getTypeofLoc());
  addSourceLocation(TL.getLParenLoc());
  addSourceLocation(TL.getRParenLoc());
}

void TypeLocWriter::VisitTypeOfTypeLoc(TypeOfTypeLoc TL) {
  addSourceLocation(TL.getTypeofLoc());
  addSourceLocation(TL.getLParenLoc());
  addSourceLocation(TL.getRParenLoc());
  Record.AddTypeSourceInfo(TL.getUnmodifiedTInfo());
}

void TypeLocWriter::VisitDecltypeTypeLoc(DecltypeTypeLoc TL) {
  addSourceLocation(TL.getDecltypeLoc());
  addSourceLocation(TL.getRParenLoc());
}

void TypeLocWriter::VisitUnaryTransformTypeLoc(UnaryTransformTypeLoc TL) {
  addSourceLocation(TL.getKWLoc());
  addSourceLocation(TL.getLParenLoc());
  addSourceLocation(TL.getRParenLoc());
  Record.AddTypeSourceInfo(TL.getUnderlyingTInfo());
}

void ASTRecordWriter::AddConceptReference(const ConceptReference *CR) {
  assert(CR);
  AddNestedNameSpecifierLoc(CR->getNestedNameSpecifierLoc());
  AddSourceLocation(CR->getTemplateKWLoc());
  AddDeclarationNameInfo(CR->getConceptNameInfo());
  AddDeclRef(CR->getFoundDecl());
  AddDeclRef(CR->getNamedConcept());
  push_back(CR->getTemplateArgsAsWritten() != nullptr);
  if (CR->getTemplateArgsAsWritten())
    AddASTTemplateArgumentListInfo(CR->getTemplateArgsAsWritten());
}

void TypeLocWriter::VisitPackIndexingTypeLoc(PackIndexingTypeLoc TL) {
  addSourceLocation(TL.getEllipsisLoc());
}

void TypeLocWriter::VisitAutoTypeLoc(AutoTypeLoc TL) {
  addSourceLocation(TL.getNameLoc());
  auto *CR = TL.getConceptReference();
  Record.push_back(TL.isConstrained() && CR);
  if (TL.isConstrained() && CR)
    Record.AddConceptReference(CR);
  Record.push_back(TL.isDecltypeAuto());
  if (TL.isDecltypeAuto())
    addSourceLocation(TL.getRParenLoc());
}

void TypeLocWriter::VisitDeducedTemplateSpecializationTypeLoc(
    DeducedTemplateSpecializationTypeLoc TL) {
  addSourceLocation(TL.getTemplateNameLoc());
}

void TypeLocWriter::VisitRecordTypeLoc(RecordTypeLoc TL) {
  addSourceLocation(TL.getNameLoc());
}

void TypeLocWriter::VisitEnumTypeLoc(EnumTypeLoc TL) {
  addSourceLocation(TL.getNameLoc());
}

void TypeLocWriter::VisitAttributedTypeLoc(AttributedTypeLoc TL) {
  Record.AddAttr(TL.getAttr());
}

void TypeLocWriter::VisitCountAttributedTypeLoc(CountAttributedTypeLoc TL) {
  // Nothing to do
}

void TypeLocWriter::VisitBTFTagAttributedTypeLoc(BTFTagAttributedTypeLoc TL) {
  // Nothing to do.
}

void TypeLocWriter::VisitTemplateTypeParmTypeLoc(TemplateTypeParmTypeLoc TL) {
  addSourceLocation(TL.getNameLoc());
}

void TypeLocWriter::VisitSubstTemplateTypeParmTypeLoc(
                                            SubstTemplateTypeParmTypeLoc TL) {
  addSourceLocation(TL.getNameLoc());
}

void TypeLocWriter::VisitSubstTemplateTypeParmPackTypeLoc(
                                          SubstTemplateTypeParmPackTypeLoc TL) {
  addSourceLocation(TL.getNameLoc());
}

void TypeLocWriter::VisitTemplateSpecializationTypeLoc(
                                           TemplateSpecializationTypeLoc TL) {
  addSourceLocation(TL.getTemplateKeywordLoc());
  addSourceLocation(TL.getTemplateNameLoc());
  addSourceLocation(TL.getLAngleLoc());
  addSourceLocation(TL.getRAngleLoc());
  for (unsigned i = 0, e = TL.getNumArgs(); i != e; ++i)
    Record.AddTemplateArgumentLocInfo(TL.getArgLoc(i).getArgument().getKind(),
                                      TL.getArgLoc(i).getLocInfo());
}

void TypeLocWriter::VisitParenTypeLoc(ParenTypeLoc TL) {
  addSourceLocation(TL.getLParenLoc());
  addSourceLocation(TL.getRParenLoc());
}

void TypeLocWriter::VisitMacroQualifiedTypeLoc(MacroQualifiedTypeLoc TL) {
  addSourceLocation(TL.getExpansionLoc());
}

void TypeLocWriter::VisitElaboratedTypeLoc(ElaboratedTypeLoc TL) {
  addSourceLocation(TL.getElaboratedKeywordLoc());
  Record.AddNestedNameSpecifierLoc(TL.getQualifierLoc());
}

void TypeLocWriter::VisitInjectedClassNameTypeLoc(InjectedClassNameTypeLoc TL) {
  addSourceLocation(TL.getNameLoc());
}

void TypeLocWriter::VisitDependentNameTypeLoc(DependentNameTypeLoc TL) {
  addSourceLocation(TL.getElaboratedKeywordLoc());
  Record.AddNestedNameSpecifierLoc(TL.getQualifierLoc());
  addSourceLocation(TL.getNameLoc());
}

void TypeLocWriter::VisitDependentTemplateSpecializationTypeLoc(
       DependentTemplateSpecializationTypeLoc TL) {
  addSourceLocation(TL.getElaboratedKeywordLoc());
  Record.AddNestedNameSpecifierLoc(TL.getQualifierLoc());
  addSourceLocation(TL.getTemplateKeywordLoc());
  addSourceLocation(TL.getTemplateNameLoc());
  addSourceLocation(TL.getLAngleLoc());
  addSourceLocation(TL.getRAngleLoc());
  for (unsigned I = 0, E = TL.getNumArgs(); I != E; ++I)
    Record.AddTemplateArgumentLocInfo(TL.getArgLoc(I).getArgument().getKind(),
                                      TL.getArgLoc(I).getLocInfo());
}

void TypeLocWriter::VisitPackExpansionTypeLoc(PackExpansionTypeLoc TL) {
  addSourceLocation(TL.getEllipsisLoc());
}

void TypeLocWriter::VisitObjCInterfaceTypeLoc(ObjCInterfaceTypeLoc TL) {
  addSourceLocation(TL.getNameLoc());
  addSourceLocation(TL.getNameEndLoc());
}

void TypeLocWriter::VisitObjCObjectTypeLoc(ObjCObjectTypeLoc TL) {
  Record.push_back(TL.hasBaseTypeAsWritten());
  addSourceLocation(TL.getTypeArgsLAngleLoc());
  addSourceLocation(TL.getTypeArgsRAngleLoc());
  for (unsigned i = 0, e = TL.getNumTypeArgs(); i != e; ++i)
    Record.AddTypeSourceInfo(TL.getTypeArgTInfo(i));
  addSourceLocation(TL.getProtocolLAngleLoc());
  addSourceLocation(TL.getProtocolRAngleLoc());
  for (unsigned i = 0, e = TL.getNumProtocols(); i != e; ++i)
    addSourceLocation(TL.getProtocolLoc(i));
}

void TypeLocWriter::VisitObjCObjectPointerTypeLoc(ObjCObjectPointerTypeLoc TL) {
  addSourceLocation(TL.getStarLoc());
}

void TypeLocWriter::VisitAtomicTypeLoc(AtomicTypeLoc TL) {
  addSourceLocation(TL.getKWLoc());
  addSourceLocation(TL.getLParenLoc());
  addSourceLocation(TL.getRParenLoc());
}

void TypeLocWriter::VisitPipeTypeLoc(PipeTypeLoc TL) {
  addSourceLocation(TL.getKWLoc());
}

void TypeLocWriter::VisitBitIntTypeLoc(clang::BitIntTypeLoc TL) {
  addSourceLocation(TL.getNameLoc());
}
void TypeLocWriter::VisitDependentBitIntTypeLoc(
    clang::DependentBitIntTypeLoc TL) {
  addSourceLocation(TL.getNameLoc());
}

void ASTWriter::WriteTypeAbbrevs() {
  using namespace llvm;

  std::shared_ptr<BitCodeAbbrev> Abv;

  // Abbreviation for TYPE_EXT_QUAL
  Abv = std::make_shared<BitCodeAbbrev>();
  Abv->Add(BitCodeAbbrevOp(serialization::TYPE_EXT_QUAL));
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // Type
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 3));   // Quals
  TypeExtQualAbbrev = Stream.EmitAbbrev(std::move(Abv));
}

//===----------------------------------------------------------------------===//
// ASTWriter Implementation
//===----------------------------------------------------------------------===//

static void EmitBlockID(unsigned ID, const char *Name,
                        llvm::BitstreamWriter &Stream,
                        ASTWriter::RecordDataImpl &Record) {
  Record.clear();
  Record.push_back(ID);
  Stream.EmitRecord(llvm::bitc::BLOCKINFO_CODE_SETBID, Record);

  // Emit the block name if present.
  if (!Name || Name[0] == 0)
    return;
  Record.clear();
  while (*Name)
    Record.push_back(*Name++);
  Stream.EmitRecord(llvm::bitc::BLOCKINFO_CODE_BLOCKNAME, Record);
}

static void EmitRecordID(unsigned ID, const char *Name,
                         llvm::BitstreamWriter &Stream,
                         ASTWriter::RecordDataImpl &Record) {
  Record.clear();
  Record.push_back(ID);
  while (*Name)
    Record.push_back(*Name++);
  Stream.EmitRecord(llvm::bitc::BLOCKINFO_CODE_SETRECORDNAME, Record);
}

static void AddStmtsExprs(llvm::BitstreamWriter &Stream,
                          ASTWriter::RecordDataImpl &Record) {
#define RECORD(X) EmitRecordID(X, #X, Stream, Record)
  RECORD(STMT_STOP);
  RECORD(STMT_NULL_PTR);
  RECORD(STMT_REF_PTR);
  RECORD(STMT_NULL);
  RECORD(STMT_COMPOUND);
  RECORD(STMT_CASE);
  RECORD(STMT_DEFAULT);
  RECORD(STMT_LABEL);
  RECORD(STMT_ATTRIBUTED);
  RECORD(STMT_IF);
  RECORD(STMT_SWITCH);
  RECORD(STMT_WHILE);
  RECORD(STMT_DO);
  RECORD(STMT_FOR);
  RECORD(STMT_GOTO);
  RECORD(STMT_INDIRECT_GOTO);
  RECORD(STMT_CONTINUE);
  RECORD(STMT_BREAK);
  RECORD(STMT_RETURN);
  RECORD(STMT_DECL);
  RECORD(STMT_GCCASM);
  RECORD(STMT_MSASM);
  RECORD(EXPR_PREDEFINED);
  RECORD(EXPR_DECL_REF);
  RECORD(EXPR_INTEGER_LITERAL);
  RECORD(EXPR_FIXEDPOINT_LITERAL);
  RECORD(EXPR_FLOATING_LITERAL);
  RECORD(EXPR_IMAGINARY_LITERAL);
  RECORD(EXPR_STRING_LITERAL);
  RECORD(EXPR_CHARACTER_LITERAL);
  RECORD(EXPR_PAREN);
  RECORD(EXPR_PAREN_LIST);
  RECORD(EXPR_UNARY_OPERATOR);
  RECORD(EXPR_SIZEOF_ALIGN_OF);
  RECORD(EXPR_ARRAY_SUBSCRIPT);
  RECORD(EXPR_CALL);
  RECORD(EXPR_MEMBER);
  RECORD(EXPR_BINARY_OPERATOR);
  RECORD(EXPR_COMPOUND_ASSIGN_OPERATOR);
  RECORD(EXPR_CONDITIONAL_OPERATOR);
  RECORD(EXPR_IMPLICIT_CAST);
  RECORD(EXPR_CSTYLE_CAST);
  RECORD(EXPR_COMPOUND_LITERAL);
  RECORD(EXPR_EXT_VECTOR_ELEMENT);
  RECORD(EXPR_INIT_LIST);
  RECORD(EXPR_DESIGNATED_INIT);
  RECORD(EXPR_DESIGNATED_INIT_UPDATE);
  RECORD(EXPR_IMPLICIT_VALUE_INIT);
  RECORD(EXPR_NO_INIT);
  RECORD(EXPR_VA_ARG);
  RECORD(EXPR_ADDR_LABEL);
  RECORD(EXPR_STMT);
  RECORD(EXPR_CHOOSE);
  RECORD(EXPR_GNU_NULL);
  RECORD(EXPR_SHUFFLE_VECTOR);
  RECORD(EXPR_BLOCK);
  RECORD(EXPR_GENERIC_SELECTION);
  RECORD(EXPR_OBJC_STRING_LITERAL);
  RECORD(EXPR_OBJC_BOXED_EXPRESSION);
  RECORD(EXPR_OBJC_ARRAY_LITERAL);
  RECORD(EXPR_OBJC_DICTIONARY_LITERAL);
  RECORD(EXPR_OBJC_ENCODE);
  RECORD(EXPR_OBJC_SELECTOR_EXPR);
  RECORD(EXPR_OBJC_PROTOCOL_EXPR);
  RECORD(EXPR_OBJC_IVAR_REF_EXPR);
  RECORD(EXPR_OBJC_PROPERTY_REF_EXPR);
  RECORD(EXPR_OBJC_KVC_REF_EXPR);
  RECORD(EXPR_OBJC_MESSAGE_EXPR);
  RECORD(STMT_OBJC_FOR_COLLECTION);
  RECORD(STMT_OBJC_CATCH);
  RECORD(STMT_OBJC_FINALLY);
  RECORD(STMT_OBJC_AT_TRY);
  RECORD(STMT_OBJC_AT_SYNCHRONIZED);
  RECORD(STMT_OBJC_AT_THROW);
  RECORD(EXPR_OBJC_BOOL_LITERAL);
  RECORD(STMT_CXX_CATCH);
  RECORD(STMT_CXX_TRY);
  RECORD(STMT_CXX_FOR_RANGE);
  RECORD(EXPR_CXX_OPERATOR_CALL);
  RECORD(EXPR_CXX_MEMBER_CALL);
  RECORD(EXPR_CXX_REWRITTEN_BINARY_OPERATOR);
  RECORD(EXPR_CXX_CONSTRUCT);
  RECORD(EXPR_CXX_TEMPORARY_OBJECT);
  RECORD(EXPR_CXX_STATIC_CAST);
  RECORD(EXPR_CXX_DYNAMIC_CAST);
  RECORD(EXPR_CXX_REINTERPRET_CAST);
  RECORD(EXPR_CXX_CONST_CAST);
  RECORD(EXPR_CXX_ADDRSPACE_CAST);
  RECORD(EXPR_CXX_FUNCTIONAL_CAST);
  RECORD(EXPR_USER_DEFINED_LITERAL);
  RECORD(EXPR_CXX_STD_INITIALIZER_LIST);
  RECORD(EXPR_CXX_BOOL_LITERAL);
  RECORD(EXPR_CXX_PAREN_LIST_INIT);
  RECORD(EXPR_CXX_NULL_PTR_LITERAL);
  RECORD(EXPR_CXX_TYPEID_EXPR);
  RECORD(EXPR_CXX_TYPEID_TYPE);
  RECORD(EXPR_CXX_THIS);
  RECORD(EXPR_CXX_THROW);
  RECORD(EXPR_CXX_DEFAULT_ARG);
  RECORD(EXPR_CXX_DEFAULT_INIT);
  RECORD(EXPR_CXX_BIND_TEMPORARY);
  RECORD(EXPR_CXX_SCALAR_VALUE_INIT);
  RECORD(EXPR_CXX_NEW);
  RECORD(EXPR_CXX_DELETE);
  RECORD(EXPR_CXX_PSEUDO_DESTRUCTOR);
  RECORD(EXPR_EXPR_WITH_CLEANUPS);
  RECORD(EXPR_CXX_DEPENDENT_SCOPE_MEMBER);
  RECORD(EXPR_CXX_DEPENDENT_SCOPE_DECL_REF);
  RECORD(EXPR_CXX_UNRESOLVED_CONSTRUCT);
  RECORD(EXPR_CXX_UNRESOLVED_MEMBER);
  RECORD(EXPR_CXX_UNRESOLVED_LOOKUP);
  RECORD(EXPR_CXX_EXPRESSION_TRAIT);
  RECORD(EXPR_CXX_NOEXCEPT);
  RECORD(EXPR_OPAQUE_VALUE);
  RECORD(EXPR_BINARY_CONDITIONAL_OPERATOR);
  RECORD(EXPR_TYPE_TRAIT);
  RECORD(EXPR_ARRAY_TYPE_TRAIT);
  RECORD(EXPR_PACK_EXPANSION);
  RECORD(EXPR_SIZEOF_PACK);
  RECORD(EXPR_PACK_INDEXING);
  RECORD(EXPR_SUBST_NON_TYPE_TEMPLATE_PARM);
  RECORD(EXPR_SUBST_NON_TYPE_TEMPLATE_PARM_PACK);
  RECORD(EXPR_FUNCTION_PARM_PACK);
  RECORD(EXPR_MATERIALIZE_TEMPORARY);
  RECORD(EXPR_CUDA_KERNEL_CALL);
  RECORD(EXPR_CXX_UUIDOF_EXPR);
  RECORD(EXPR_CXX_UUIDOF_TYPE);
  RECORD(EXPR_LAMBDA);
#undef RECORD
}

void ASTWriter::WriteBlockInfoBlock() {
  RecordData Record;
  Stream.EnterBlockInfoBlock();

#define BLOCK(X) EmitBlockID(X ## _ID, #X, Stream, Record)
#define RECORD(X) EmitRecordID(X, #X, Stream, Record)

  // Control Block.
  BLOCK(CONTROL_BLOCK);
  RECORD(METADATA);
  RECORD(MODULE_NAME);
  RECORD(MODULE_DIRECTORY);
  RECORD(MODULE_MAP_FILE);
  RECORD(IMPORTS);
  RECORD(ORIGINAL_FILE);
  RECORD(ORIGINAL_FILE_ID);
  RECORD(INPUT_FILE_OFFSETS);

  BLOCK(OPTIONS_BLOCK);
  RECORD(LANGUAGE_OPTIONS);
  RECORD(TARGET_OPTIONS);
  RECORD(FILE_SYSTEM_OPTIONS);
  RECORD(HEADER_SEARCH_OPTIONS);
  RECORD(PREPROCESSOR_OPTIONS);

  BLOCK(INPUT_FILES_BLOCK);
  RECORD(INPUT_FILE);
  RECORD(INPUT_FILE_HASH);

  // AST Top-Level Block.
  BLOCK(AST_BLOCK);
  RECORD(TYPE_OFFSET);
  RECORD(DECL_OFFSET);
  RECORD(IDENTIFIER_OFFSET);
  RECORD(IDENTIFIER_TABLE);
  RECORD(EAGERLY_DESERIALIZED_DECLS);
  RECORD(MODULAR_CODEGEN_DECLS);
  RECORD(SPECIAL_TYPES);
  RECORD(STATISTICS);
  RECORD(TENTATIVE_DEFINITIONS);
  RECORD(SELECTOR_OFFSETS);
  RECORD(METHOD_POOL);
  RECORD(PP_COUNTER_VALUE);
  RECORD(SOURCE_LOCATION_OFFSETS);
  RECORD(EXT_VECTOR_DECLS);
  RECORD(UNUSED_FILESCOPED_DECLS);
  RECORD(PPD_ENTITIES_OFFSETS);
  RECORD(VTABLE_USES);
  RECORD(PPD_SKIPPED_RANGES);
  RECORD(REFERENCED_SELECTOR_POOL);
  RECORD(TU_UPDATE_LEXICAL);
  RECORD(SEMA_DECL_REFS);
  RECORD(WEAK_UNDECLARED_IDENTIFIERS);
  RECORD(PENDING_IMPLICIT_INSTANTIATIONS);
  RECORD(UPDATE_VISIBLE);
  RECORD(DELAYED_NAMESPACE_LEXICAL_VISIBLE_RECORD);
  RECORD(DECL_UPDATE_OFFSETS);
  RECORD(DECL_UPDATES);
  RECORD(CUDA_SPECIAL_DECL_REFS);
  RECORD(HEADER_SEARCH_TABLE);
  RECORD(FP_PRAGMA_OPTIONS);
  RECORD(OPENCL_EXTENSIONS);
  RECORD(OPENCL_EXTENSION_TYPES);
  RECORD(OPENCL_EXTENSION_DECLS);
  RECORD(DELEGATING_CTORS);
  RECORD(KNOWN_NAMESPACES);
  RECORD(MODULE_OFFSET_MAP);
  RECORD(SOURCE_MANAGER_LINE_TABLE);
  RECORD(OBJC_CATEGORIES_MAP);
  RECORD(FILE_SORTED_DECLS);
  RECORD(IMPORTED_MODULES);
  RECORD(OBJC_CATEGORIES);
  RECORD(MACRO_OFFSET);
  RECORD(INTERESTING_IDENTIFIERS);
  RECORD(UNDEFINED_BUT_USED);
  RECORD(LATE_PARSED_TEMPLATE);
  RECORD(OPTIMIZE_PRAGMA_OPTIONS);
  RECORD(MSSTRUCT_PRAGMA_OPTIONS);
  RECORD(POINTERS_TO_MEMBERS_PRAGMA_OPTIONS);
  RECORD(UNUSED_LOCAL_TYPEDEF_NAME_CANDIDATES);
  RECORD(DELETE_EXPRS_TO_ANALYZE);
  RECORD(CUDA_PRAGMA_FORCE_HOST_DEVICE_DEPTH);
  RECORD(PP_CONDITIONAL_STACK);
  RECORD(DECLS_TO_CHECK_FOR_DEFERRED_DIAGS);
  RECORD(PP_ASSUME_NONNULL_LOC);
  RECORD(PP_UNSAFE_BUFFER_USAGE);
  RECORD(VTABLES_TO_EMIT);

  // SourceManager Block.
  BLOCK(SOURCE_MANAGER_BLOCK);
  RECORD(SM_SLOC_FILE_ENTRY);
  RECORD(SM_SLOC_BUFFER_ENTRY);
  RECORD(SM_SLOC_BUFFER_BLOB);
  RECORD(SM_SLOC_BUFFER_BLOB_COMPRESSED);
  RECORD(SM_SLOC_EXPANSION_ENTRY);

  // Preprocessor Block.
  BLOCK(PREPROCESSOR_BLOCK);
  RECORD(PP_MACRO_DIRECTIVE_HISTORY);
  RECORD(PP_MACRO_FUNCTION_LIKE);
  RECORD(PP_MACRO_OBJECT_LIKE);
  RECORD(PP_MODULE_MACRO);
  RECORD(PP_TOKEN);

  // Submodule Block.
  BLOCK(SUBMODULE_BLOCK);
  RECORD(SUBMODULE_METADATA);
  RECORD(SUBMODULE_DEFINITION);
  RECORD(SUBMODULE_UMBRELLA_HEADER);
  RECORD(SUBMODULE_HEADER);
  RECORD(SUBMODULE_TOPHEADER);
  RECORD(SUBMODULE_UMBRELLA_DIR);
  RECORD(SUBMODULE_IMPORTS);
  RECORD(SUBMODULE_AFFECTING_MODULES);
  RECORD(SUBMODULE_EXPORTS);
  RECORD(SUBMODULE_REQUIRES);
  RECORD(SUBMODULE_EXCLUDED_HEADER);
  RECORD(SUBMODULE_LINK_LIBRARY);
  RECORD(SUBMODULE_CONFIG_MACRO);
  RECORD(SUBMODULE_CONFLICT);
  RECORD(SUBMODULE_PRIVATE_HEADER);
  RECORD(SUBMODULE_TEXTUAL_HEADER);
  RECORD(SUBMODULE_PRIVATE_TEXTUAL_HEADER);
  RECORD(SUBMODULE_INITIALIZERS);
  RECORD(SUBMODULE_EXPORT_AS);

  // Comments Block.
  BLOCK(COMMENTS_BLOCK);
  RECORD(COMMENTS_RAW_COMMENT);

  // Decls and Types block.
  BLOCK(DECLTYPES_BLOCK);
  RECORD(TYPE_EXT_QUAL);
  RECORD(TYPE_COMPLEX);
  RECORD(TYPE_POINTER);
  RECORD(TYPE_BLOCK_POINTER);
  RECORD(TYPE_LVALUE_REFERENCE);
  RECORD(TYPE_RVALUE_REFERENCE);
  RECORD(TYPE_MEMBER_POINTER);
  RECORD(TYPE_CONSTANT_ARRAY);
  RECORD(TYPE_INCOMPLETE_ARRAY);
  RECORD(TYPE_VARIABLE_ARRAY);
  RECORD(TYPE_VECTOR);
  RECORD(TYPE_EXT_VECTOR);
  RECORD(TYPE_FUNCTION_NO_PROTO);
  RECORD(TYPE_FUNCTION_PROTO);
  RECORD(TYPE_TYPEDEF);
  RECORD(TYPE_TYPEOF_EXPR);
  RECORD(TYPE_TYPEOF);
  RECORD(TYPE_RECORD);
  RECORD(TYPE_ENUM);
  RECORD(TYPE_OBJC_INTERFACE);
  RECORD(TYPE_OBJC_OBJECT_POINTER);
  RECORD(TYPE_DECLTYPE);
  RECORD(TYPE_ELABORATED);
  RECORD(TYPE_SUBST_TEMPLATE_TYPE_PARM);
  RECORD(TYPE_UNRESOLVED_USING);
  RECORD(TYPE_INJECTED_CLASS_NAME);
  RECORD(TYPE_OBJC_OBJECT);
  RECORD(TYPE_TEMPLATE_TYPE_PARM);
  RECORD(TYPE_TEMPLATE_SPECIALIZATION);
  RECORD(TYPE_DEPENDENT_NAME);
  RECORD(TYPE_DEPENDENT_TEMPLATE_SPECIALIZATION);
  RECORD(TYPE_DEPENDENT_SIZED_ARRAY);
  RECORD(TYPE_PAREN);
  RECORD(TYPE_MACRO_QUALIFIED);
  RECORD(TYPE_PACK_EXPANSION);
  RECORD(TYPE_ATTRIBUTED);
  RECORD(TYPE_SUBST_TEMPLATE_TYPE_PARM_PACK);
  RECORD(TYPE_AUTO);
  RECORD(TYPE_UNARY_TRANSFORM);
  RECORD(TYPE_ATOMIC);
  RECORD(TYPE_DECAYED);
  RECORD(TYPE_ADJUSTED);
  RECORD(TYPE_OBJC_TYPE_PARAM);
  RECORD(LOCAL_REDECLARATIONS);
  RECORD(DECL_TYPEDEF);
  RECORD(DECL_TYPEALIAS);
  RECORD(DECL_ENUM);
  RECORD(DECL_RECORD);
  RECORD(DECL_ENUM_CONSTANT);
  RECORD(DECL_FUNCTION);
  RECORD(DECL_OBJC_METHOD);
  RECORD(DECL_OBJC_INTERFACE);
  RECORD(DECL_OBJC_PROTOCOL);
  RECORD(DECL_OBJC_IVAR);
  RECORD(DECL_OBJC_AT_DEFS_FIELD);
  RECORD(DECL_OBJC_CATEGORY);
  RECORD(DECL_OBJC_CATEGORY_IMPL);
  RECORD(DECL_OBJC_IMPLEMENTATION);
  RECORD(DECL_OBJC_COMPATIBLE_ALIAS);
  RECORD(DECL_OBJC_PROPERTY);
  RECORD(DECL_OBJC_PROPERTY_IMPL);
  RECORD(DECL_FIELD);
  RECORD(DECL_MS_PROPERTY);
  RECORD(DECL_VAR);
  RECORD(DECL_IMPLICIT_PARAM);
  RECORD(DECL_PARM_VAR);
  RECORD(DECL_FILE_SCOPE_ASM);
  RECORD(DECL_BLOCK);
  RECORD(DECL_CONTEXT_LEXICAL);
  RECORD(DECL_CONTEXT_VISIBLE);
  RECORD(DECL_NAMESPACE);
  RECORD(DECL_NAMESPACE_ALIAS);
  RECORD(DECL_USING);
  RECORD(DECL_USING_SHADOW);
  RECORD(DECL_USING_DIRECTIVE);
  RECORD(DECL_UNRESOLVED_USING_VALUE);
  RECORD(DECL_UNRESOLVED_USING_TYPENAME);
  RECORD(DECL_LINKAGE_SPEC);
  RECORD(DECL_EXPORT);
  RECORD(DECL_CXX_RECORD);
  RECORD(DECL_CXX_METHOD);
  RECORD(DECL_CXX_CONSTRUCTOR);
  RECORD(DECL_CXX_DESTRUCTOR);
  RECORD(DECL_CXX_CONVERSION);
  RECORD(DECL_ACCESS_SPEC);
  RECORD(DECL_FRIEND);
  RECORD(DECL_FRIEND_TEMPLATE);
  RECORD(DECL_CLASS_TEMPLATE);
  RECORD(DECL_CLASS_TEMPLATE_SPECIALIZATION);
  RECORD(DECL_CLASS_TEMPLATE_PARTIAL_SPECIALIZATION);
  RECORD(DECL_VAR_TEMPLATE);
  RECORD(DECL_VAR_TEMPLATE_SPECIALIZATION);
  RECORD(DECL_VAR_TEMPLATE_PARTIAL_SPECIALIZATION);
  RECORD(DECL_FUNCTION_TEMPLATE);
  RECORD(DECL_TEMPLATE_TYPE_PARM);
  RECORD(DECL_NON_TYPE_TEMPLATE_PARM);
  RECORD(DECL_TEMPLATE_TEMPLATE_PARM);
  RECORD(DECL_CONCEPT);
  RECORD(DECL_REQUIRES_EXPR_BODY);
  RECORD(DECL_TYPE_ALIAS_TEMPLATE);
  RECORD(DECL_STATIC_ASSERT);
  RECORD(DECL_CXX_BASE_SPECIFIERS);
  RECORD(DECL_CXX_CTOR_INITIALIZERS);
  RECORD(DECL_INDIRECTFIELD);
  RECORD(DECL_EXPANDED_NON_TYPE_TEMPLATE_PARM_PACK);
  RECORD(DECL_EXPANDED_TEMPLATE_TEMPLATE_PARM_PACK);
  RECORD(DECL_IMPORT);
  RECORD(DECL_OMP_THREADPRIVATE);
  RECORD(DECL_EMPTY);
  RECORD(DECL_OBJC_TYPE_PARAM);
  RECORD(DECL_OMP_CAPTUREDEXPR);
  RECORD(DECL_PRAGMA_COMMENT);
  RECORD(DECL_PRAGMA_DETECT_MISMATCH);
  RECORD(DECL_OMP_DECLARE_REDUCTION);
  RECORD(DECL_OMP_ALLOCATE);
  RECORD(DECL_HLSL_BUFFER);

  // Statements and Exprs can occur in the Decls and Types block.
  AddStmtsExprs(Stream, Record);

  BLOCK(PREPROCESSOR_DETAIL_BLOCK);
  RECORD(PPD_MACRO_EXPANSION);
  RECORD(PPD_MACRO_DEFINITION);
  RECORD(PPD_INCLUSION_DIRECTIVE);

  // Decls and Types block.
  BLOCK(EXTENSION_BLOCK);
  RECORD(EXTENSION_METADATA);

  BLOCK(UNHASHED_CONTROL_BLOCK);
  RECORD(SIGNATURE);
  RECORD(AST_BLOCK_HASH);
  RECORD(DIAGNOSTIC_OPTIONS);
  RECORD(HEADER_SEARCH_PATHS);
  RECORD(DIAG_PRAGMA_MAPPINGS);

#undef RECORD
#undef BLOCK
  Stream.ExitBlock();
}

/// Prepares a path for being written to an AST file by converting it
/// to an absolute path and removing nested './'s.
///
/// \return \c true if the path was changed.
static bool cleanPathForOutput(FileManager &FileMgr,
                               SmallVectorImpl<char> &Path) {
  bool Changed = FileMgr.makeAbsolutePath(Path);
  return Changed | llvm::sys::path::remove_dots(Path);
}

/// Adjusts the given filename to only write out the portion of the
/// filename that is not part of the system root directory.
///
/// \param Filename the file name to adjust.
///
/// \param BaseDir When non-NULL, the PCH file is a relocatable AST file and
/// the returned filename will be adjusted by this root directory.
///
/// \returns either the original filename (if it needs no adjustment) or the
/// adjusted filename (which points into the @p Filename parameter).
static const char *
adjustFilenameForRelocatableAST(const char *Filename, StringRef BaseDir) {
  assert(Filename && "No file name to adjust?");

  if (BaseDir.empty())
    return Filename;

  // Verify that the filename and the system root have the same prefix.
  unsigned Pos = 0;
  for (; Filename[Pos] && Pos < BaseDir.size(); ++Pos)
    if (Filename[Pos] != BaseDir[Pos])
      return Filename; // Prefixes don't match.

  // We hit the end of the filename before we hit the end of the system root.
  if (!Filename[Pos])
    return Filename;

  // If there's not a path separator at the end of the base directory nor
  // immediately after it, then this isn't within the base directory.
  if (!llvm::sys::path::is_separator(Filename[Pos])) {
    if (!llvm::sys::path::is_separator(BaseDir.back()))
      return Filename;
  } else {
    // If the file name has a '/' at the current position, skip over the '/'.
    // We distinguish relative paths from absolute paths by the
    // absence of '/' at the beginning of relative paths.
    //
    // FIXME: This is wrong. We distinguish them by asking if the path is
    // absolute, which isn't the same thing. And there might be multiple '/'s
    // in a row. Use a better mechanism to indicate whether we have emitted an
    // absolute or relative path.
    ++Pos;
  }

  return Filename + Pos;
}

std::pair<ASTFileSignature, ASTFileSignature>
ASTWriter::createSignature() const {
  StringRef AllBytes(Buffer.data(), Buffer.size());

  llvm::SHA1 Hasher;
  Hasher.update(AllBytes.slice(ASTBlockRange.first, ASTBlockRange.second));
  ASTFileSignature ASTBlockHash = ASTFileSignature::create(Hasher.result());

  // Add the remaining bytes:
  //  1. Before the unhashed control block.
  Hasher.update(AllBytes.slice(0, UnhashedControlBlockRange.first));
  //  2. Between the unhashed control block and the AST block.
  Hasher.update(
      AllBytes.slice(UnhashedControlBlockRange.second, ASTBlockRange.first));
  //  3. After the AST block.
  Hasher.update(AllBytes.slice(ASTBlockRange.second, StringRef::npos));
  ASTFileSignature Signature = ASTFileSignature::create(Hasher.result());

  return std::make_pair(ASTBlockHash, Signature);
}

ASTFileSignature ASTWriter::createSignatureForNamedModule() const {
  llvm::SHA1 Hasher;
  Hasher.update(StringRef(Buffer.data(), Buffer.size()));

  assert(WritingModule);
  assert(WritingModule->isNamedModule());

  // We need to combine all the export imported modules no matter
  // we used it or not.
  for (auto [ExportImported, _] : WritingModule->Exports)
    Hasher.update(ExportImported->Signature);

  // We combine all the used modules to make sure the signature is precise.
  // Consider the case like:
  //
  // // a.cppm
  // export module a;
  // export inline int a() { ... }
  //
  // // b.cppm
  // export module b;
  // import a;
  // export inline int b() { return a(); }
  //
  // Since both `a()` and `b()` are inline, we need to make sure the BMI of
  // `b.pcm` will change after the implementation of `a()` changes. We can't
  // get that naturally since we won't record the body of `a()` during the
  // writing process. We can't reuse ODRHash here since ODRHash won't calculate
  // the called function recursively. So ODRHash will be problematic if `a()`
  // calls other inline functions.
  //
  // Probably we can solve this by a new hash mechanism. But the safety and
  // efficiency may a problem too. Here we just combine the hash value of the
  // used modules conservatively.
  for (Module *M : TouchedTopLevelModules)
    Hasher.update(M->Signature);

  return ASTFileSignature::create(Hasher.result());
}

static void BackpatchSignatureAt(llvm::BitstreamWriter &Stream,
                                 const ASTFileSignature &S, uint64_t BitNo) {
  for (uint8_t Byte : S) {
    Stream.BackpatchByte(BitNo, Byte);
    BitNo += 8;
  }
}

ASTFileSignature ASTWriter::backpatchSignature() {
  if (isWritingStdCXXNamedModules()) {
    ASTFileSignature Signature = createSignatureForNamedModule();
    BackpatchSignatureAt(Stream, Signature, SignatureOffset);
    return Signature;
  }

  if (!WritingModule ||
      !PP->getHeaderSearchInfo().getHeaderSearchOpts().ModulesHashContent)
    return {};

  // For implicit modules, write the hash of the PCM as its signature.
  ASTFileSignature ASTBlockHash;
  ASTFileSignature Signature;
  std::tie(ASTBlockHash, Signature) = createSignature();

  BackpatchSignatureAt(Stream, ASTBlockHash, ASTBlockHashOffset);
  BackpatchSignatureAt(Stream, Signature, SignatureOffset);

  return Signature;
}

void ASTWriter::writeUnhashedControlBlock(Preprocessor &PP,
                                          ASTContext &Context) {
  using namespace llvm;

  // Flush first to prepare the PCM hash (signature).
  Stream.FlushToWord();
  UnhashedControlBlockRange.first = Stream.GetCurrentBitNo() >> 3;

  // Enter the block and prepare to write records.
  RecordData Record;
  Stream.EnterSubblock(UNHASHED_CONTROL_BLOCK_ID, 5);

  // For implicit modules and C++20 named modules, write the hash of the PCM as
  // its signature.
  if (isWritingStdCXXNamedModules() ||
      (WritingModule &&
       PP.getHeaderSearchInfo().getHeaderSearchOpts().ModulesHashContent)) {
    // At this point, we don't know the actual signature of the file or the AST
    // block - we're only able to compute those at the end of the serialization
    // process. Let's store dummy signatures for now, and replace them with the
    // real ones later on.
    // The bitstream VBR-encodes record elements, which makes backpatching them
    // really difficult. Let's store the signatures as blobs instead - they are
    // guaranteed to be word-aligned, and we control their format/encoding.
    auto Dummy = ASTFileSignature::createDummy();
    SmallString<128> Blob{Dummy.begin(), Dummy.end()};

    // We don't need AST Block hash in named modules.
    if (!isWritingStdCXXNamedModules()) {
      auto Abbrev = std::make_shared<BitCodeAbbrev>();
      Abbrev->Add(BitCodeAbbrevOp(AST_BLOCK_HASH));
      Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));
      unsigned ASTBlockHashAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

      Record.push_back(AST_BLOCK_HASH);
      Stream.EmitRecordWithBlob(ASTBlockHashAbbrev, Record, Blob);
      ASTBlockHashOffset = Stream.GetCurrentBitNo() - Blob.size() * 8;
      Record.clear();
    }

    auto Abbrev = std::make_shared<BitCodeAbbrev>();
    Abbrev->Add(BitCodeAbbrevOp(SIGNATURE));
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));
    unsigned SignatureAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

    Record.push_back(SIGNATURE);
    Stream.EmitRecordWithBlob(SignatureAbbrev, Record, Blob);
    SignatureOffset = Stream.GetCurrentBitNo() - Blob.size() * 8;
    Record.clear();
  }

  const auto &HSOpts = PP.getHeaderSearchInfo().getHeaderSearchOpts();

  // Diagnostic options.
  const auto &Diags = Context.getDiagnostics();
  const DiagnosticOptions &DiagOpts = Diags.getDiagnosticOptions();
  if (!HSOpts.ModulesSkipDiagnosticOptions) {
#define DIAGOPT(Name, Bits, Default) Record.push_back(DiagOpts.Name);
#define ENUM_DIAGOPT(Name, Type, Bits, Default)                                \
  Record.push_back(static_cast<unsigned>(DiagOpts.get##Name()));
#include "clang/Basic/DiagnosticOptions.def"
    Record.push_back(DiagOpts.Warnings.size());
    for (unsigned I = 0, N = DiagOpts.Warnings.size(); I != N; ++I)
      AddString(DiagOpts.Warnings[I], Record);
    Record.push_back(DiagOpts.Remarks.size());
    for (unsigned I = 0, N = DiagOpts.Remarks.size(); I != N; ++I)
      AddString(DiagOpts.Remarks[I], Record);
    // Note: we don't serialize the log or serialization file names, because
    // they are generally transient files and will almost always be overridden.
    Stream.EmitRecord(DIAGNOSTIC_OPTIONS, Record);
    Record.clear();
  }

  // Header search paths.
  if (!HSOpts.ModulesSkipHeaderSearchPaths) {
    // Include entries.
    Record.push_back(HSOpts.UserEntries.size());
    for (unsigned I = 0, N = HSOpts.UserEntries.size(); I != N; ++I) {
      const HeaderSearchOptions::Entry &Entry = HSOpts.UserEntries[I];
      AddString(Entry.Path, Record);
      Record.push_back(static_cast<unsigned>(Entry.Group));
      Record.push_back(Entry.IsFramework);
      Record.push_back(Entry.IgnoreSysRoot);
    }

    // System header prefixes.
    Record.push_back(HSOpts.SystemHeaderPrefixes.size());
    for (unsigned I = 0, N = HSOpts.SystemHeaderPrefixes.size(); I != N; ++I) {
      AddString(HSOpts.SystemHeaderPrefixes[I].Prefix, Record);
      Record.push_back(HSOpts.SystemHeaderPrefixes[I].IsSystemHeader);
    }

    // VFS overlay files.
    Record.push_back(HSOpts.VFSOverlayFiles.size());
    for (StringRef VFSOverlayFile : HSOpts.VFSOverlayFiles)
      AddString(VFSOverlayFile, Record);

    Stream.EmitRecord(HEADER_SEARCH_PATHS, Record);
  }

  if (!HSOpts.ModulesSkipPragmaDiagnosticMappings)
    WritePragmaDiagnosticMappings(Diags, /* isModule = */ WritingModule);

  // Header search entry usage.
  {
    auto HSEntryUsage = PP.getHeaderSearchInfo().computeUserEntryUsage();
    auto Abbrev = std::make_shared<BitCodeAbbrev>();
    Abbrev->Add(BitCodeAbbrevOp(HEADER_SEARCH_ENTRY_USAGE));
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32)); // Number of bits.
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));      // Bit vector.
    unsigned HSUsageAbbrevCode = Stream.EmitAbbrev(std::move(Abbrev));
    RecordData::value_type Record[] = {HEADER_SEARCH_ENTRY_USAGE,
                                       HSEntryUsage.size()};
    Stream.EmitRecordWithBlob(HSUsageAbbrevCode, Record, bytes(HSEntryUsage));
  }

  // VFS usage.
  {
    auto VFSUsage = PP.getHeaderSearchInfo().collectVFSUsageAndClear();
    auto Abbrev = std::make_shared<BitCodeAbbrev>();
    Abbrev->Add(BitCodeAbbrevOp(VFS_USAGE));
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32)); // Number of bits.
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));      // Bit vector.
    unsigned VFSUsageAbbrevCode = Stream.EmitAbbrev(std::move(Abbrev));
    RecordData::value_type Record[] = {VFS_USAGE, VFSUsage.size()};
    Stream.EmitRecordWithBlob(VFSUsageAbbrevCode, Record, bytes(VFSUsage));
  }

  // Leave the options block.
  Stream.ExitBlock();
  UnhashedControlBlockRange.second = Stream.GetCurrentBitNo() >> 3;
}

/// Write the control block.
void ASTWriter::WriteControlBlock(Preprocessor &PP, ASTContext &Context,
                                  StringRef isysroot) {
  using namespace llvm;

  Stream.EnterSubblock(CONTROL_BLOCK_ID, 5);
  RecordData Record;

  // Metadata
  auto MetadataAbbrev = std::make_shared<BitCodeAbbrev>();
  MetadataAbbrev->Add(BitCodeAbbrevOp(METADATA));
  MetadataAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 16)); // Major
  MetadataAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 16)); // Minor
  MetadataAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 16)); // Clang maj.
  MetadataAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 16)); // Clang min.
  MetadataAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // Relocatable
  // Standard C++ module
  MetadataAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1));
  MetadataAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // Timestamps
  MetadataAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // Errors
  MetadataAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)); // SVN branch/tag
  unsigned MetadataAbbrevCode = Stream.EmitAbbrev(std::move(MetadataAbbrev));
  assert((!WritingModule || isysroot.empty()) &&
         "writing module as a relocatable PCH?");
  {
    RecordData::value_type Record[] = {METADATA,
                                       VERSION_MAJOR,
                                       VERSION_MINOR,
                                       CLANG_VERSION_MAJOR,
                                       CLANG_VERSION_MINOR,
                                       !isysroot.empty(),
                                       isWritingStdCXXNamedModules(),
                                       IncludeTimestamps,
                                       ASTHasCompilerErrors};
    Stream.EmitRecordWithBlob(MetadataAbbrevCode, Record,
                              getClangFullRepositoryVersion());
  }

  if (WritingModule) {
    // Module name
    auto Abbrev = std::make_shared<BitCodeAbbrev>();
    Abbrev->Add(BitCodeAbbrevOp(MODULE_NAME));
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)); // Name
    unsigned AbbrevCode = Stream.EmitAbbrev(std::move(Abbrev));
    RecordData::value_type Record[] = {MODULE_NAME};
    Stream.EmitRecordWithBlob(AbbrevCode, Record, WritingModule->Name);
  }

  if (WritingModule && WritingModule->Directory) {
    SmallString<128> BaseDir;
    if (PP.getHeaderSearchInfo().getHeaderSearchOpts().ModuleFileHomeIsCwd) {
      // Use the current working directory as the base path for all inputs.
      auto CWD =
          Context.getSourceManager().getFileManager().getOptionalDirectoryRef(
              ".");
      BaseDir.assign(CWD->getName());
    } else {
      BaseDir.assign(WritingModule->Directory->getName());
    }
    cleanPathForOutput(Context.getSourceManager().getFileManager(), BaseDir);

    // If the home of the module is the current working directory, then we
    // want to pick up the cwd of the build process loading the module, not
    // our cwd, when we load this module.
    if (!PP.getHeaderSearchInfo().getHeaderSearchOpts().ModuleFileHomeIsCwd &&
        (!PP.getHeaderSearchInfo()
              .getHeaderSearchOpts()
              .ModuleMapFileHomeIsCwd ||
         WritingModule->Directory->getName() != ".")) {
      // Module directory.
      auto Abbrev = std::make_shared<BitCodeAbbrev>();
      Abbrev->Add(BitCodeAbbrevOp(MODULE_DIRECTORY));
      Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)); // Directory
      unsigned AbbrevCode = Stream.EmitAbbrev(std::move(Abbrev));

      RecordData::value_type Record[] = {MODULE_DIRECTORY};
      Stream.EmitRecordWithBlob(AbbrevCode, Record, BaseDir);
    }

    // Write out all other paths relative to the base directory if possible.
    BaseDirectory.assign(BaseDir.begin(), BaseDir.end());
  } else if (!isysroot.empty()) {
    // Write out paths relative to the sysroot if possible.
    BaseDirectory = std::string(isysroot);
  }

  // Module map file
  if (WritingModule && WritingModule->Kind == Module::ModuleMapModule) {
    Record.clear();

    auto &Map = PP.getHeaderSearchInfo().getModuleMap();
    AddPath(WritingModule->PresumedModuleMapFile.empty()
                ? Map.getModuleMapFileForUniquing(WritingModule)
                      ->getNameAsRequested()
                : StringRef(WritingModule->PresumedModuleMapFile),
            Record);

    // Additional module map files.
    if (auto *AdditionalModMaps =
            Map.getAdditionalModuleMapFiles(WritingModule)) {
      Record.push_back(AdditionalModMaps->size());
      SmallVector<FileEntryRef, 1> ModMaps(AdditionalModMaps->begin(),
                                           AdditionalModMaps->end());
      llvm::sort(ModMaps, [](FileEntryRef A, FileEntryRef B) {
        return A.getName() < B.getName();
      });
      for (FileEntryRef F : ModMaps)
        AddPath(F.getName(), Record);
    } else {
      Record.push_back(0);
    }

    Stream.EmitRecord(MODULE_MAP_FILE, Record);
  }

  // Imports
  if (Chain) {
    serialization::ModuleManager &Mgr = Chain->getModuleManager();
    Record.clear();

    for (ModuleFile &M : Mgr) {
      // Skip modules that weren't directly imported.
      if (!M.isDirectlyImported())
        continue;

      Record.push_back((unsigned)M.Kind); // FIXME: Stable encoding
      Record.push_back(M.StandardCXXModule);
      AddSourceLocation(M.ImportLoc, Record);

      // We don't want to hard code the information about imported modules
      // in the C++20 named modules.
      if (!M.StandardCXXModule) {
        // If we have calculated signature, there is no need to store
        // the size or timestamp.
        Record.push_back(M.Signature ? 0 : M.File.getSize());
        Record.push_back(M.Signature ? 0 : getTimestampForOutput(M.File));
        llvm::append_range(Record, M.Signature);
      }

      AddString(M.ModuleName, Record);

      if (!M.StandardCXXModule)
        AddPath(M.FileName, Record);
    }
    Stream.EmitRecord(IMPORTS, Record);
  }

  // Write the options block.
  Stream.EnterSubblock(OPTIONS_BLOCK_ID, 4);

  // Language options.
  Record.clear();
  const LangOptions &LangOpts = Context.getLangOpts();
#define LANGOPT(Name, Bits, Default, Description) \
  Record.push_back(LangOpts.Name);
#define ENUM_LANGOPT(Name, Type, Bits, Default, Description) \
  Record.push_back(static_cast<unsigned>(LangOpts.get##Name()));
#include "clang/Basic/LangOptions.def"
#define SANITIZER(NAME, ID)                                                    \
  Record.push_back(LangOpts.Sanitize.has(SanitizerKind::ID));
#include "clang/Basic/Sanitizers.def"

  Record.push_back(LangOpts.ModuleFeatures.size());
  for (StringRef Feature : LangOpts.ModuleFeatures)
    AddString(Feature, Record);

  Record.push_back((unsigned) LangOpts.ObjCRuntime.getKind());
  AddVersionTuple(LangOpts.ObjCRuntime.getVersion(), Record);

  AddString(LangOpts.CurrentModule, Record);

  // Comment options.
  Record.push_back(LangOpts.CommentOpts.BlockCommandNames.size());
  for (const auto &I : LangOpts.CommentOpts.BlockCommandNames) {
    AddString(I, Record);
  }
  Record.push_back(LangOpts.CommentOpts.ParseAllComments);

  // OpenMP offloading options.
  Record.push_back(LangOpts.OMPTargetTriples.size());
  for (auto &T : LangOpts.OMPTargetTriples)
    AddString(T.getTriple(), Record);

  AddString(LangOpts.OMPHostIRFile, Record);

  Stream.EmitRecord(LANGUAGE_OPTIONS, Record);

  // Target options.
  Record.clear();
  const TargetInfo &Target = Context.getTargetInfo();
  const TargetOptions &TargetOpts = Target.getTargetOpts();
  AddString(TargetOpts.Triple, Record);
  AddString(TargetOpts.CPU, Record);
  AddString(TargetOpts.TuneCPU, Record);
  AddString(TargetOpts.ABI, Record);
  Record.push_back(TargetOpts.FeaturesAsWritten.size());
  for (unsigned I = 0, N = TargetOpts.FeaturesAsWritten.size(); I != N; ++I) {
    AddString(TargetOpts.FeaturesAsWritten[I], Record);
  }
  Record.push_back(TargetOpts.Features.size());
  for (unsigned I = 0, N = TargetOpts.Features.size(); I != N; ++I) {
    AddString(TargetOpts.Features[I], Record);
  }
  Stream.EmitRecord(TARGET_OPTIONS, Record);

  // File system options.
  Record.clear();
  const FileSystemOptions &FSOpts =
      Context.getSourceManager().getFileManager().getFileSystemOpts();
  AddString(FSOpts.WorkingDir, Record);
  Stream.EmitRecord(FILE_SYSTEM_OPTIONS, Record);

  // Header search options.
  Record.clear();
  const HeaderSearchOptions &HSOpts =
      PP.getHeaderSearchInfo().getHeaderSearchOpts();

  AddString(HSOpts.Sysroot, Record);
  AddString(HSOpts.ResourceDir, Record);
  AddString(HSOpts.ModuleCachePath, Record);
  AddString(HSOpts.ModuleUserBuildPath, Record);
  Record.push_back(HSOpts.DisableModuleHash);
  Record.push_back(HSOpts.ImplicitModuleMaps);
  Record.push_back(HSOpts.ModuleMapFileHomeIsCwd);
  Record.push_back(HSOpts.EnablePrebuiltImplicitModules);
  Record.push_back(HSOpts.UseBuiltinIncludes);
  Record.push_back(HSOpts.UseStandardSystemIncludes);
  Record.push_back(HSOpts.UseStandardCXXIncludes);
  Record.push_back(HSOpts.UseLibcxx);
  // Write out the specific module cache path that contains the module files.
  AddString(PP.getHeaderSearchInfo().getModuleCachePath(), Record);
  Stream.EmitRecord(HEADER_SEARCH_OPTIONS, Record);

  // Preprocessor options.
  Record.clear();
  const PreprocessorOptions &PPOpts = PP.getPreprocessorOpts();

  // If we're building an implicit module with a context hash, the importer is
  // guaranteed to have the same macros defined on the command line. Skip
  // writing them.
  bool SkipMacros = BuildingImplicitModule && !HSOpts.DisableModuleHash;
  bool WriteMacros = !SkipMacros;
  Record.push_back(WriteMacros);
  if (WriteMacros) {
    // Macro definitions.
    Record.push_back(PPOpts.Macros.size());
    for (unsigned I = 0, N = PPOpts.Macros.size(); I != N; ++I) {
      AddString(PPOpts.Macros[I].first, Record);
      Record.push_back(PPOpts.Macros[I].second);
    }
  }

  // Includes
  Record.push_back(PPOpts.Includes.size());
  for (unsigned I = 0, N = PPOpts.Includes.size(); I != N; ++I)
    AddString(PPOpts.Includes[I], Record);

  // Macro includes
  Record.push_back(PPOpts.MacroIncludes.size());
  for (unsigned I = 0, N = PPOpts.MacroIncludes.size(); I != N; ++I)
    AddString(PPOpts.MacroIncludes[I], Record);

  Record.push_back(PPOpts.UsePredefines);
  // Detailed record is important since it is used for the module cache hash.
  Record.push_back(PPOpts.DetailedRecord);
  AddString(PPOpts.ImplicitPCHInclude, Record);
  Record.push_back(static_cast<unsigned>(PPOpts.ObjCXXARCStandardLibrary));
  Stream.EmitRecord(PREPROCESSOR_OPTIONS, Record);

  // Leave the options block.
  Stream.ExitBlock();

  // Original file name and file ID
  SourceManager &SM = Context.getSourceManager();
  if (auto MainFile = SM.getFileEntryRefForID(SM.getMainFileID())) {
    auto FileAbbrev = std::make_shared<BitCodeAbbrev>();
    FileAbbrev->Add(BitCodeAbbrevOp(ORIGINAL_FILE));
    FileAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // File ID
    FileAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)); // File name
    unsigned FileAbbrevCode = Stream.EmitAbbrev(std::move(FileAbbrev));

    Record.clear();
    Record.push_back(ORIGINAL_FILE);
    AddFileID(SM.getMainFileID(), Record);
    EmitRecordWithPath(FileAbbrevCode, Record, MainFile->getName());
  }

  Record.clear();
  AddFileID(SM.getMainFileID(), Record);
  Stream.EmitRecord(ORIGINAL_FILE_ID, Record);

  WriteInputFiles(Context.SourceMgr,
                  PP.getHeaderSearchInfo().getHeaderSearchOpts());
  Stream.ExitBlock();
}

namespace  {

/// An input file.
struct InputFileEntry {
  FileEntryRef File;
  bool IsSystemFile;
  bool IsTransient;
  bool BufferOverridden;
  bool IsTopLevel;
  bool IsModuleMap;
  uint32_t ContentHash[2];

  InputFileEntry(FileEntryRef File) : File(File) {}
};

} // namespace

SourceLocation ASTWriter::getAffectingIncludeLoc(const SourceManager &SourceMgr,
                                                 const SrcMgr::FileInfo &File) {
  SourceLocation IncludeLoc = File.getIncludeLoc();
  if (IncludeLoc.isValid()) {
    FileID IncludeFID = SourceMgr.getFileID(IncludeLoc);
    assert(IncludeFID.isValid() && "IncludeLoc in invalid file");
    if (!IsSLocAffecting[IncludeFID.ID])
      IncludeLoc = SourceLocation();
  }
  return IncludeLoc;
}

void ASTWriter::WriteInputFiles(SourceManager &SourceMgr,
                                HeaderSearchOptions &HSOpts) {
  using namespace llvm;

  Stream.EnterSubblock(INPUT_FILES_BLOCK_ID, 4);

  // Create input-file abbreviation.
  auto IFAbbrev = std::make_shared<BitCodeAbbrev>();
  IFAbbrev->Add(BitCodeAbbrevOp(INPUT_FILE));
  IFAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // ID
  IFAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 12)); // Size
  IFAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 32)); // Modification time
  IFAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // Overridden
  IFAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // Transient
  IFAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // Top-level
  IFAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // Module map
  IFAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 16)); // Name as req. len
  IFAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)); // Name as req. + name
  unsigned IFAbbrevCode = Stream.EmitAbbrev(std::move(IFAbbrev));

  // Create input file hash abbreviation.
  auto IFHAbbrev = std::make_shared<BitCodeAbbrev>();
  IFHAbbrev->Add(BitCodeAbbrevOp(INPUT_FILE_HASH));
  IFHAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32));
  IFHAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32));
  unsigned IFHAbbrevCode = Stream.EmitAbbrev(std::move(IFHAbbrev));

  uint64_t InputFilesOffsetBase = Stream.GetCurrentBitNo();

  // Get all ContentCache objects for files.
  std::vector<InputFileEntry> UserFiles;
  std::vector<InputFileEntry> SystemFiles;
  for (unsigned I = 1, N = SourceMgr.local_sloc_entry_size(); I != N; ++I) {
    // Get this source location entry.
    const SrcMgr::SLocEntry *SLoc = &SourceMgr.getLocalSLocEntry(I);
    assert(&SourceMgr.getSLocEntry(FileID::get(I)) == SLoc);

    // We only care about file entries that were not overridden.
    if (!SLoc->isFile())
      continue;
    const SrcMgr::FileInfo &File = SLoc->getFile();
    const SrcMgr::ContentCache *Cache = &File.getContentCache();
    if (!Cache->OrigEntry)
      continue;

    // Do not emit input files that do not affect current module.
    if (!IsSLocAffecting[I])
      continue;

    InputFileEntry Entry(*Cache->OrigEntry);
    Entry.IsSystemFile = isSystem(File.getFileCharacteristic());
    Entry.IsTransient = Cache->IsTransient;
    Entry.BufferOverridden = Cache->BufferOverridden;
    Entry.IsTopLevel = getAffectingIncludeLoc(SourceMgr, File).isInvalid();
    Entry.IsModuleMap = isModuleMap(File.getFileCharacteristic());

    uint64_t ContentHash = 0;
    if (PP->getHeaderSearchInfo()
            .getHeaderSearchOpts()
            .ValidateASTInputFilesContent) {
      auto MemBuff = Cache->getBufferIfLoaded();
      if (MemBuff)
        ContentHash = xxh3_64bits(MemBuff->getBuffer());
      else
        PP->Diag(SourceLocation(), diag::err_module_unable_to_hash_content)
            << Entry.File.getName();
    }
    Entry.ContentHash[0] = uint32_t(ContentHash);
    Entry.ContentHash[1] = uint32_t(ContentHash >> 32);
    if (Entry.IsSystemFile)
      SystemFiles.push_back(Entry);
    else
      UserFiles.push_back(Entry);
  }

  // User files go at the front, system files at the back.
  auto SortedFiles = llvm::concat<InputFileEntry>(std::move(UserFiles),
                                                  std::move(SystemFiles));

  unsigned UserFilesNum = 0;
  // Write out all of the input files.
  std::vector<uint64_t> InputFileOffsets;
  for (const auto &Entry : SortedFiles) {
    uint32_t &InputFileID = InputFileIDs[Entry.File];
    if (InputFileID != 0)
      continue; // already recorded this file.

    // Record this entry's offset.
    InputFileOffsets.push_back(Stream.GetCurrentBitNo() - InputFilesOffsetBase);

    InputFileID = InputFileOffsets.size();

    if (!Entry.IsSystemFile)
      ++UserFilesNum;

    // Emit size/modification time for this file.
    // And whether this file was overridden.
    {
      SmallString<128> NameAsRequested = Entry.File.getNameAsRequested();
      SmallString<128> Name = Entry.File.getName();

      PreparePathForOutput(NameAsRequested);
      PreparePathForOutput(Name);

      if (Name == NameAsRequested)
        Name.clear();

      RecordData::value_type Record[] = {
          INPUT_FILE,
          InputFileOffsets.size(),
          (uint64_t)Entry.File.getSize(),
          (uint64_t)getTimestampForOutput(Entry.File),
          Entry.BufferOverridden,
          Entry.IsTransient,
          Entry.IsTopLevel,
          Entry.IsModuleMap,
          NameAsRequested.size()};

      Stream.EmitRecordWithBlob(IFAbbrevCode, Record,
                                (NameAsRequested + Name).str());
    }

    // Emit content hash for this file.
    {
      RecordData::value_type Record[] = {INPUT_FILE_HASH, Entry.ContentHash[0],
                                         Entry.ContentHash[1]};
      Stream.EmitRecordWithAbbrev(IFHAbbrevCode, Record);
    }
  }

  Stream.ExitBlock();

  // Create input file offsets abbreviation.
  auto OffsetsAbbrev = std::make_shared<BitCodeAbbrev>();
  OffsetsAbbrev->Add(BitCodeAbbrevOp(INPUT_FILE_OFFSETS));
  OffsetsAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // # input files
  OffsetsAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // # non-system
                                                                //   input files
  OffsetsAbbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));   // Array
  unsigned OffsetsAbbrevCode = Stream.EmitAbbrev(std::move(OffsetsAbbrev));

  // Write input file offsets.
  RecordData::value_type Record[] = {INPUT_FILE_OFFSETS,
                                     InputFileOffsets.size(), UserFilesNum};
  Stream.EmitRecordWithBlob(OffsetsAbbrevCode, Record, bytes(InputFileOffsets));
}

//===----------------------------------------------------------------------===//
// Source Manager Serialization
//===----------------------------------------------------------------------===//

/// Create an abbreviation for the SLocEntry that refers to a
/// file.
static unsigned CreateSLocFileAbbrev(llvm::BitstreamWriter &Stream) {
  using namespace llvm;

  auto Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(SM_SLOC_FILE_ENTRY));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8)); // Offset
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8)); // Include location
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 3)); // Characteristic
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // Line directives
  // FileEntry fields.
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Input File ID
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8)); // NumCreatedFIDs
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 24)); // FirstDeclIndex
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8)); // NumDecls
  return Stream.EmitAbbrev(std::move(Abbrev));
}

/// Create an abbreviation for the SLocEntry that refers to a
/// buffer.
static unsigned CreateSLocBufferAbbrev(llvm::BitstreamWriter &Stream) {
  using namespace llvm;

  auto Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(SM_SLOC_BUFFER_ENTRY));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8)); // Offset
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8)); // Include location
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 3)); // Characteristic
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // Line directives
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)); // Buffer name blob
  return Stream.EmitAbbrev(std::move(Abbrev));
}

/// Create an abbreviation for the SLocEntry that refers to a
/// buffer's blob.
static unsigned CreateSLocBufferBlobAbbrev(llvm::BitstreamWriter &Stream,
                                           bool Compressed) {
  using namespace llvm;

  auto Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(Compressed ? SM_SLOC_BUFFER_BLOB_COMPRESSED
                                         : SM_SLOC_BUFFER_BLOB));
  if (Compressed)
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8)); // Uncompressed size
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)); // Blob
  return Stream.EmitAbbrev(std::move(Abbrev));
}

/// Create an abbreviation for the SLocEntry that refers to a macro
/// expansion.
static unsigned CreateSLocExpansionAbbrev(llvm::BitstreamWriter &Stream) {
  using namespace llvm;

  auto Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(SM_SLOC_EXPANSION_ENTRY));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8)); // Offset
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8)); // Spelling location
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Start location
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // End location
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // Is token range
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Token length
  return Stream.EmitAbbrev(std::move(Abbrev));
}

/// Emit key length and data length as ULEB-encoded data, and return them as a
/// pair.
static std::pair<unsigned, unsigned>
emitULEBKeyDataLength(unsigned KeyLen, unsigned DataLen, raw_ostream &Out) {
  llvm::encodeULEB128(KeyLen, Out);
  llvm::encodeULEB128(DataLen, Out);
  return std::make_pair(KeyLen, DataLen);
}

namespace {

  // Trait used for the on-disk hash table of header search information.
  class HeaderFileInfoTrait {
    ASTWriter &Writer;

    // Keep track of the framework names we've used during serialization.
    SmallString<128> FrameworkStringData;
    llvm::StringMap<unsigned> FrameworkNameOffset;

  public:
    HeaderFileInfoTrait(ASTWriter &Writer) : Writer(Writer) {}

    struct key_type {
      StringRef Filename;
      off_t Size;
      time_t ModTime;
    };
    using key_type_ref = const key_type &;

    using UnresolvedModule =
        llvm::PointerIntPair<Module *, 2, ModuleMap::ModuleHeaderRole>;

    struct data_type {
      data_type(const HeaderFileInfo &HFI, bool AlreadyIncluded,
                ArrayRef<ModuleMap::KnownHeader> KnownHeaders,
                UnresolvedModule Unresolved)
          : HFI(HFI), AlreadyIncluded(AlreadyIncluded),
            KnownHeaders(KnownHeaders), Unresolved(Unresolved) {}

      HeaderFileInfo HFI;
      bool AlreadyIncluded;
      SmallVector<ModuleMap::KnownHeader, 1> KnownHeaders;
      UnresolvedModule Unresolved;
    };
    using data_type_ref = const data_type &;

    using hash_value_type = unsigned;
    using offset_type = unsigned;

    hash_value_type ComputeHash(key_type_ref key) {
      // The hash is based only on size/time of the file, so that the reader can
      // match even when symlinking or excess path elements ("foo/../", "../")
      // change the form of the name. However, complete path is still the key.
      uint8_t buf[sizeof(key.Size) + sizeof(key.ModTime)];
      memcpy(buf, &key.Size, sizeof(key.Size));
      memcpy(buf + sizeof(key.Size), &key.ModTime, sizeof(key.ModTime));
      return llvm::xxh3_64bits(buf);
    }

    std::pair<unsigned, unsigned>
    EmitKeyDataLength(raw_ostream& Out, key_type_ref key, data_type_ref Data) {
      unsigned KeyLen = key.Filename.size() + 1 + 8 + 8;
      unsigned DataLen = 1 + sizeof(IdentifierID) + 4;
      for (auto ModInfo : Data.KnownHeaders)
        if (Writer.getLocalOrImportedSubmoduleID(ModInfo.getModule()))
          DataLen += 4;
      if (Data.Unresolved.getPointer())
        DataLen += 4;
      return emitULEBKeyDataLength(KeyLen, DataLen, Out);
    }

    void EmitKey(raw_ostream& Out, key_type_ref key, unsigned KeyLen) {
      using namespace llvm::support;

      endian::Writer LE(Out, llvm::endianness::little);
      LE.write<uint64_t>(key.Size);
      KeyLen -= 8;
      LE.write<uint64_t>(key.ModTime);
      KeyLen -= 8;
      Out.write(key.Filename.data(), KeyLen);
    }

    void EmitData(raw_ostream &Out, key_type_ref key,
                  data_type_ref Data, unsigned DataLen) {
      using namespace llvm::support;

      endian::Writer LE(Out, llvm::endianness::little);
      uint64_t Start = Out.tell(); (void)Start;

      unsigned char Flags = (Data.AlreadyIncluded << 6)
                          | (Data.HFI.isImport << 5)
                          | (Writer.isWritingStdCXXNamedModules() ? 0 :
                             Data.HFI.isPragmaOnce << 4)
                          | (Data.HFI.DirInfo << 1)
                          | Data.HFI.IndexHeaderMapHeader;
      LE.write<uint8_t>(Flags);

      if (Data.HFI.LazyControllingMacro.isID())
        LE.write<IdentifierID>(Data.HFI.LazyControllingMacro.getID());
      else
        LE.write<IdentifierID>(
            Writer.getIdentifierRef(Data.HFI.LazyControllingMacro.getPtr()));

      unsigned Offset = 0;
      if (!Data.HFI.Framework.empty()) {
        // If this header refers into a framework, save the framework name.
        llvm::StringMap<unsigned>::iterator Pos
          = FrameworkNameOffset.find(Data.HFI.Framework);
        if (Pos == FrameworkNameOffset.end()) {
          Offset = FrameworkStringData.size() + 1;
          FrameworkStringData.append(Data.HFI.Framework);
          FrameworkStringData.push_back(0);

          FrameworkNameOffset[Data.HFI.Framework] = Offset;
        } else
          Offset = Pos->second;
      }
      LE.write<uint32_t>(Offset);

      auto EmitModule = [&](Module *M, ModuleMap::ModuleHeaderRole Role) {
        if (uint32_t ModID = Writer.getLocalOrImportedSubmoduleID(M)) {
          uint32_t Value = (ModID << 3) | (unsigned)Role;
          assert((Value >> 3) == ModID && "overflow in header module info");
          LE.write<uint32_t>(Value);
        }
      };

      for (auto ModInfo : Data.KnownHeaders)
        EmitModule(ModInfo.getModule(), ModInfo.getRole());
      if (Data.Unresolved.getPointer())
        EmitModule(Data.Unresolved.getPointer(), Data.Unresolved.getInt());

      assert(Out.tell() - Start == DataLen && "Wrong data length");
    }

    const char *strings_begin() const { return FrameworkStringData.begin(); }
    const char *strings_end() const { return FrameworkStringData.end(); }
  };

} // namespace

/// Write the header search block for the list of files that
///
/// \param HS The header search structure to save.
void ASTWriter::WriteHeaderSearch(const HeaderSearch &HS) {
  HeaderFileInfoTrait GeneratorTrait(*this);
  llvm::OnDiskChainedHashTableGenerator<HeaderFileInfoTrait> Generator;
  SmallVector<const char *, 4> SavedStrings;
  unsigned NumHeaderSearchEntries = 0;

  // Find all unresolved headers for the current module. We generally will
  // have resolved them before we get here, but not necessarily: we might be
  // compiling a preprocessed module, where there is no requirement for the
  // original files to exist any more.
  const HeaderFileInfo Empty; // So we can take a reference.
  if (WritingModule) {
    llvm::SmallVector<Module *, 16> Worklist(1, WritingModule);
    while (!Worklist.empty()) {
      Module *M = Worklist.pop_back_val();
      // We don't care about headers in unimportable submodules.
      if (M->isUnimportable())
        continue;

      // Map to disk files where possible, to pick up any missing stat
      // information. This also means we don't need to check the unresolved
      // headers list when emitting resolved headers in the first loop below.
      // FIXME: It'd be preferable to avoid doing this if we were given
      // sufficient stat information in the module map.
      HS.getModuleMap().resolveHeaderDirectives(M, /*File=*/std::nullopt);

      // If the file didn't exist, we can still create a module if we were given
      // enough information in the module map.
      for (const auto &U : M->MissingHeaders) {
        // Check that we were given enough information to build a module
        // without this file existing on disk.
        if (!U.Size || (!U.ModTime && IncludeTimestamps)) {
          PP->Diag(U.FileNameLoc, diag::err_module_no_size_mtime_for_header)
              << WritingModule->getFullModuleName() << U.Size.has_value()
              << U.FileName;
          continue;
        }

        // Form the effective relative pathname for the file.
        SmallString<128> Filename(M->Directory->getName());
        llvm::sys::path::append(Filename, U.FileName);
        PreparePathForOutput(Filename);

        StringRef FilenameDup = strdup(Filename.c_str());
        SavedStrings.push_back(FilenameDup.data());

        HeaderFileInfoTrait::key_type Key = {
            FilenameDup, *U.Size, IncludeTimestamps ? *U.ModTime : 0};
        HeaderFileInfoTrait::data_type Data = {
            Empty, false, {}, {M, ModuleMap::headerKindToRole(U.Kind)}};
        // FIXME: Deal with cases where there are multiple unresolved header
        // directives in different submodules for the same header.
        Generator.insert(Key, Data, GeneratorTrait);
        ++NumHeaderSearchEntries;
      }
      auto SubmodulesRange = M->submodules();
      Worklist.append(SubmodulesRange.begin(), SubmodulesRange.end());
    }
  }

  SmallVector<OptionalFileEntryRef, 16> FilesByUID;
  HS.getFileMgr().GetUniqueIDMapping(FilesByUID);

  if (FilesByUID.size() > HS.header_file_size())
    FilesByUID.resize(HS.header_file_size());

  for (unsigned UID = 0, LastUID = FilesByUID.size(); UID != LastUID; ++UID) {
    OptionalFileEntryRef File = FilesByUID[UID];
    if (!File)
      continue;

    const HeaderFileInfo *HFI = HS.getExistingLocalFileInfo(*File);
    if (!HFI)
      continue; // We have no information on this being a header file.
    if (!HFI->isCompilingModuleHeader && HFI->isModuleHeader)
      continue; // Header file info is tracked by the owning module file.
    if (!HFI->isCompilingModuleHeader && !PP->alreadyIncluded(*File))
      continue; // Non-modular header not included is not needed.

    // Massage the file path into an appropriate form.
    StringRef Filename = File->getName();
    SmallString<128> FilenameTmp(Filename);
    if (PreparePathForOutput(FilenameTmp)) {
      // If we performed any translation on the file name at all, we need to
      // save this string, since the generator will refer to it later.
      Filename = StringRef(strdup(FilenameTmp.c_str()));
      SavedStrings.push_back(Filename.data());
    }

    bool Included = PP->alreadyIncluded(*File);

    HeaderFileInfoTrait::key_type Key = {
      Filename, File->getSize(), getTimestampForOutput(*File)
    };
    HeaderFileInfoTrait::data_type Data = {
      *HFI, Included, HS.getModuleMap().findResolvedModulesForHeader(*File), {}
    };
    Generator.insert(Key, Data, GeneratorTrait);
    ++NumHeaderSearchEntries;
  }

  // Create the on-disk hash table in a buffer.
  SmallString<4096> TableData;
  uint32_t BucketOffset;
  {
    using namespace llvm::support;

    llvm::raw_svector_ostream Out(TableData);
    // Make sure that no bucket is at offset 0
    endian::write<uint32_t>(Out, 0, llvm::endianness::little);
    BucketOffset = Generator.Emit(Out, GeneratorTrait);
  }

  // Create a blob abbreviation
  using namespace llvm;

  auto Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(HEADER_SEARCH_TABLE));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));
  unsigned TableAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

  // Write the header search table
  RecordData::value_type Record[] = {HEADER_SEARCH_TABLE, BucketOffset,
                                     NumHeaderSearchEntries, TableData.size()};
  TableData.append(GeneratorTrait.strings_begin(),GeneratorTrait.strings_end());
  Stream.EmitRecordWithBlob(TableAbbrev, Record, TableData);

  // Free all of the strings we had to duplicate.
  for (unsigned I = 0, N = SavedStrings.size(); I != N; ++I)
    free(const_cast<char *>(SavedStrings[I]));
}

static void emitBlob(llvm::BitstreamWriter &Stream, StringRef Blob,
                     unsigned SLocBufferBlobCompressedAbbrv,
                     unsigned SLocBufferBlobAbbrv) {
  using RecordDataType = ASTWriter::RecordData::value_type;

  // Compress the buffer if possible. We expect that almost all PCM
  // consumers will not want its contents.
  SmallVector<uint8_t, 0> CompressedBuffer;
  if (llvm::compression::zstd::isAvailable()) {
    llvm::compression::zstd::compress(
        llvm::arrayRefFromStringRef(Blob.drop_back(1)), CompressedBuffer, 9);
    RecordDataType Record[] = {SM_SLOC_BUFFER_BLOB_COMPRESSED, Blob.size() - 1};
    Stream.EmitRecordWithBlob(SLocBufferBlobCompressedAbbrv, Record,
                              llvm::toStringRef(CompressedBuffer));
    return;
  }
  if (llvm::compression::zlib::isAvailable()) {
    llvm::compression::zlib::compress(
        llvm::arrayRefFromStringRef(Blob.drop_back(1)), CompressedBuffer);
    RecordDataType Record[] = {SM_SLOC_BUFFER_BLOB_COMPRESSED, Blob.size() - 1};
    Stream.EmitRecordWithBlob(SLocBufferBlobCompressedAbbrv, Record,
                              llvm::toStringRef(CompressedBuffer));
    return;
  }

  RecordDataType Record[] = {SM_SLOC_BUFFER_BLOB};
  Stream.EmitRecordWithBlob(SLocBufferBlobAbbrv, Record, Blob);
}

/// Writes the block containing the serialized form of the
/// source manager.
///
/// TODO: We should probably use an on-disk hash table (stored in a
/// blob), indexed based on the file name, so that we only create
/// entries for files that we actually need. In the common case (no
/// errors), we probably won't have to create file entries for any of
/// the files in the AST.
void ASTWriter::WriteSourceManagerBlock(SourceManager &SourceMgr,
                                        const Preprocessor &PP) {
  RecordData Record;

  // Enter the source manager block.
  Stream.EnterSubblock(SOURCE_MANAGER_BLOCK_ID, 4);
  const uint64_t SourceManagerBlockOffset = Stream.GetCurrentBitNo();

  // Abbreviations for the various kinds of source-location entries.
  unsigned SLocFileAbbrv = CreateSLocFileAbbrev(Stream);
  unsigned SLocBufferAbbrv = CreateSLocBufferAbbrev(Stream);
  unsigned SLocBufferBlobAbbrv = CreateSLocBufferBlobAbbrev(Stream, false);
  unsigned SLocBufferBlobCompressedAbbrv =
      CreateSLocBufferBlobAbbrev(Stream, true);
  unsigned SLocExpansionAbbrv = CreateSLocExpansionAbbrev(Stream);

  // Write out the source location entry table. We skip the first
  // entry, which is always the same dummy entry.
  std::vector<uint32_t> SLocEntryOffsets;
  uint64_t SLocEntryOffsetsBase = Stream.GetCurrentBitNo();
  SLocEntryOffsets.reserve(SourceMgr.local_sloc_entry_size() - 1);
  for (unsigned I = 1, N = SourceMgr.local_sloc_entry_size();
       I != N; ++I) {
    // Get this source location entry.
    const SrcMgr::SLocEntry *SLoc = &SourceMgr.getLocalSLocEntry(I);
    FileID FID = FileID::get(I);
    assert(&SourceMgr.getSLocEntry(FID) == SLoc);

    // Record the offset of this source-location entry.
    uint64_t Offset = Stream.GetCurrentBitNo() - SLocEntryOffsetsBase;
    assert((Offset >> 32) == 0 && "SLocEntry offset too large");

    // Figure out which record code to use.
    unsigned Code;
    if (SLoc->isFile()) {
      const SrcMgr::ContentCache *Cache = &SLoc->getFile().getContentCache();
      if (Cache->OrigEntry) {
        Code = SM_SLOC_FILE_ENTRY;
      } else
        Code = SM_SLOC_BUFFER_ENTRY;
    } else
      Code = SM_SLOC_EXPANSION_ENTRY;
    Record.clear();
    Record.push_back(Code);

    if (SLoc->isFile()) {
      const SrcMgr::FileInfo &File = SLoc->getFile();
      const SrcMgr::ContentCache *Content = &File.getContentCache();
      // Do not emit files that were not listed as inputs.
      if (!IsSLocAffecting[I])
        continue;
      SLocEntryOffsets.push_back(Offset);
      // Starting offset of this entry within this module, so skip the dummy.
      Record.push_back(getAdjustedOffset(SLoc->getOffset()) - 2);
      AddSourceLocation(getAffectingIncludeLoc(SourceMgr, File), Record);
      Record.push_back(File.getFileCharacteristic()); // FIXME: stable encoding
      Record.push_back(File.hasLineDirectives());

      bool EmitBlob = false;
      if (Content->OrigEntry) {
        assert(Content->OrigEntry == Content->ContentsEntry &&
               "Writing to AST an overridden file is not supported");

        // The source location entry is a file. Emit input file ID.
        assert(InputFileIDs[*Content->OrigEntry] != 0 && "Missed file entry");
        Record.push_back(InputFileIDs[*Content->OrigEntry]);

        Record.push_back(getAdjustedNumCreatedFIDs(FID));

        FileDeclIDsTy::iterator FDI = FileDeclIDs.find(FID);
        if (FDI != FileDeclIDs.end()) {
          Record.push_back(FDI->second->FirstDeclIndex);
          Record.push_back(FDI->second->DeclIDs.size());
        } else {
          Record.push_back(0);
          Record.push_back(0);
        }

        Stream.EmitRecordWithAbbrev(SLocFileAbbrv, Record);

        if (Content->BufferOverridden || Content->IsTransient)
          EmitBlob = true;
      } else {
        // The source location entry is a buffer. The blob associated
        // with this entry contains the contents of the buffer.

        // We add one to the size so that we capture the trailing NULL
        // that is required by llvm::MemoryBuffer::getMemBuffer (on
        // the reader side).
        std::optional<llvm::MemoryBufferRef> Buffer =
            Content->getBufferOrNone(PP.getDiagnostics(), PP.getFileManager());
        StringRef Name = Buffer ? Buffer->getBufferIdentifier() : "";
        Stream.EmitRecordWithBlob(SLocBufferAbbrv, Record,
                                  StringRef(Name.data(), Name.size() + 1));
        EmitBlob = true;
      }

      if (EmitBlob) {
        // Include the implicit terminating null character in the on-disk buffer
        // if we're writing it uncompressed.
        std::optional<llvm::MemoryBufferRef> Buffer =
            Content->getBufferOrNone(PP.getDiagnostics(), PP.getFileManager());
        if (!Buffer)
          Buffer = llvm::MemoryBufferRef("<<<INVALID BUFFER>>>", "");
        StringRef Blob(Buffer->getBufferStart(), Buffer->getBufferSize() + 1);
        emitBlob(Stream, Blob, SLocBufferBlobCompressedAbbrv,
                 SLocBufferBlobAbbrv);
      }
    } else {
      // The source location entry is a macro expansion.
      const SrcMgr::ExpansionInfo &Expansion = SLoc->getExpansion();
      SLocEntryOffsets.push_back(Offset);
      // Starting offset of this entry within this module, so skip the dummy.
      Record.push_back(getAdjustedOffset(SLoc->getOffset()) - 2);
      LocSeq::State Seq;
      AddSourceLocation(Expansion.getSpellingLoc(), Record, Seq);
      AddSourceLocation(Expansion.getExpansionLocStart(), Record, Seq);
      AddSourceLocation(Expansion.isMacroArgExpansion()
                            ? SourceLocation()
                            : Expansion.getExpansionLocEnd(),
                        Record, Seq);
      Record.push_back(Expansion.isExpansionTokenRange());

      // Compute the token length for this macro expansion.
      SourceLocation::UIntTy NextOffset = SourceMgr.getNextLocalOffset();
      if (I + 1 != N)
        NextOffset = SourceMgr.getLocalSLocEntry(I + 1).getOffset();
      Record.push_back(getAdjustedOffset(NextOffset - SLoc->getOffset()) - 1);
      Stream.EmitRecordWithAbbrev(SLocExpansionAbbrv, Record);
    }
  }

  Stream.ExitBlock();

  if (SLocEntryOffsets.empty())
    return;

  // Write the source-location offsets table into the AST block. This
  // table is used for lazily loading source-location information.
  using namespace llvm;

  auto Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(SOURCE_LOCATION_OFFSETS));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 16)); // # of slocs
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 16)); // total size
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 32)); // base offset
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)); // offsets
  unsigned SLocOffsetsAbbrev = Stream.EmitAbbrev(std::move(Abbrev));
  {
    RecordData::value_type Record[] = {
        SOURCE_LOCATION_OFFSETS, SLocEntryOffsets.size(),
        getAdjustedOffset(SourceMgr.getNextLocalOffset()) - 1 /* skip dummy */,
        SLocEntryOffsetsBase - SourceManagerBlockOffset};
    Stream.EmitRecordWithBlob(SLocOffsetsAbbrev, Record,
                              bytes(SLocEntryOffsets));
  }

  // Write the line table. It depends on remapping working, so it must come
  // after the source location offsets.
  if (SourceMgr.hasLineTable()) {
    LineTableInfo &LineTable = SourceMgr.getLineTable();

    Record.clear();

    // Emit the needed file names.
    llvm::DenseMap<int, int> FilenameMap;
    FilenameMap[-1] = -1; // For unspecified filenames.
    for (const auto &L : LineTable) {
      if (L.first.ID < 0)
        continue;
      for (auto &LE : L.second) {
        if (FilenameMap.insert(std::make_pair(LE.FilenameID,
                                              FilenameMap.size() - 1)).second)
          AddPath(LineTable.getFilename(LE.FilenameID), Record);
      }
    }
    Record.push_back(0);

    // Emit the line entries
    for (const auto &L : LineTable) {
      // Only emit entries for local files.
      if (L.first.ID < 0)
        continue;

      AddFileID(L.first, Record);

      // Emit the line entries
      Record.push_back(L.second.size());
      for (const auto &LE : L.second) {
        Record.push_back(LE.FileOffset);
        Record.push_back(LE.LineNo);
        Record.push_back(FilenameMap[LE.FilenameID]);
        Record.push_back((unsigned)LE.FileKind);
        Record.push_back(LE.IncludeOffset);
      }
    }

    Stream.EmitRecord(SOURCE_MANAGER_LINE_TABLE, Record);
  }
}

//===----------------------------------------------------------------------===//
// Preprocessor Serialization
//===----------------------------------------------------------------------===//

static bool shouldIgnoreMacro(MacroDirective *MD, bool IsModule,
                              const Preprocessor &PP) {
  if (MacroInfo *MI = MD->getMacroInfo())
    if (MI->isBuiltinMacro())
      return true;

  if (IsModule) {
    SourceLocation Loc = MD->getLocation();
    if (Loc.isInvalid())
      return true;
    if (PP.getSourceManager().getFileID(Loc) == PP.getPredefinesFileID())
      return true;
  }

  return false;
}

/// Writes the block containing the serialized form of the
/// preprocessor.
void ASTWriter::WritePreprocessor(const Preprocessor &PP, bool IsModule) {
  uint64_t MacroOffsetsBase = Stream.GetCurrentBitNo();

  PreprocessingRecord *PPRec = PP.getPreprocessingRecord();
  if (PPRec)
    WritePreprocessorDetail(*PPRec, MacroOffsetsBase);

  RecordData Record;
  RecordData ModuleMacroRecord;

  // If the preprocessor __COUNTER__ value has been bumped, remember it.
  if (PP.getCounterValue() != 0) {
    RecordData::value_type Record[] = {PP.getCounterValue()};
    Stream.EmitRecord(PP_COUNTER_VALUE, Record);
  }

  // If we have a recorded #pragma assume_nonnull, remember it so it can be
  // replayed when the preamble terminates into the main file.
  SourceLocation AssumeNonNullLoc =
      PP.getPreambleRecordedPragmaAssumeNonNullLoc();
  if (AssumeNonNullLoc.isValid()) {
    assert(PP.isRecordingPreamble());
    AddSourceLocation(AssumeNonNullLoc, Record);
    Stream.EmitRecord(PP_ASSUME_NONNULL_LOC, Record);
    Record.clear();
  }

  if (PP.isRecordingPreamble() && PP.hasRecordedPreamble()) {
    assert(!IsModule);
    auto SkipInfo = PP.getPreambleSkipInfo();
    if (SkipInfo) {
      Record.push_back(true);
      AddSourceLocation(SkipInfo->HashTokenLoc, Record);
      AddSourceLocation(SkipInfo->IfTokenLoc, Record);
      Record.push_back(SkipInfo->FoundNonSkipPortion);
      Record.push_back(SkipInfo->FoundElse);
      AddSourceLocation(SkipInfo->ElseLoc, Record);
    } else {
      Record.push_back(false);
    }
    for (const auto &Cond : PP.getPreambleConditionalStack()) {
      AddSourceLocation(Cond.IfLoc, Record);
      Record.push_back(Cond.WasSkipping);
      Record.push_back(Cond.FoundNonSkip);
      Record.push_back(Cond.FoundElse);
    }
    Stream.EmitRecord(PP_CONDITIONAL_STACK, Record);
    Record.clear();
  }

  // Write the safe buffer opt-out region map in PP
  for (SourceLocation &S : PP.serializeSafeBufferOptOutMap())
    AddSourceLocation(S, Record);
  Stream.EmitRecord(PP_UNSAFE_BUFFER_USAGE, Record);
  Record.clear();

  // Enter the preprocessor block.
  Stream.EnterSubblock(PREPROCESSOR_BLOCK_ID, 3);

  // If the AST file contains __DATE__ or __TIME__ emit a warning about this.
  // FIXME: Include a location for the use, and say which one was used.
  if (PP.SawDateOrTime())
    PP.Diag(SourceLocation(), diag::warn_module_uses_date_time) << IsModule;

  // Loop over all the macro directives that are live at the end of the file,
  // emitting each to the PP section.

  // Construct the list of identifiers with macro directives that need to be
  // serialized.
  SmallVector<const IdentifierInfo *, 128> MacroIdentifiers;
  // It is meaningless to emit macros for named modules. It only wastes times
  // and spaces.
  if (!isWritingStdCXXNamedModules())
    for (auto &Id : PP.getIdentifierTable())
      if (Id.second->hadMacroDefinition() &&
          (!Id.second->isFromAST() ||
          Id.second->hasChangedSinceDeserialization()))
        MacroIdentifiers.push_back(Id.second);
  // Sort the set of macro definitions that need to be serialized by the
  // name of the macro, to provide a stable ordering.
  llvm::sort(MacroIdentifiers, llvm::deref<std::less<>>());

  // Emit the macro directives as a list and associate the offset with the
  // identifier they belong to.
  for (const IdentifierInfo *Name : MacroIdentifiers) {
    MacroDirective *MD = PP.getLocalMacroDirectiveHistory(Name);
    uint64_t StartOffset = Stream.GetCurrentBitNo() - MacroOffsetsBase;
    assert((StartOffset >> 32) == 0 && "Macro identifiers offset too large");

    // Write out any exported module macros.
    bool EmittedModuleMacros = false;
    // C+=20 Header Units are compiled module interfaces, but they preserve
    // macros that are live (i.e. have a defined value) at the end of the
    // compilation.  So when writing a header unit, we preserve only the final
    // value of each macro (and discard any that are undefined).  Header units
    // do not have sub-modules (although they might import other header units).
    // PCH files, conversely, retain the history of each macro's define/undef
    // and of leaf macros in sub modules.
    if (IsModule && WritingModule->isHeaderUnit()) {
      // This is for the main TU when it is a C++20 header unit.
      // We preserve the final state of defined macros, and we do not emit ones
      // that are undefined.
      if (!MD || shouldIgnoreMacro(MD, IsModule, PP) ||
          MD->getKind() == MacroDirective::MD_Undefine)
        continue;
      AddSourceLocation(MD->getLocation(), Record);
      Record.push_back(MD->getKind());
      if (auto *DefMD = dyn_cast<DefMacroDirective>(MD)) {
        Record.push_back(getMacroRef(DefMD->getInfo(), Name));
      } else if (auto *VisMD = dyn_cast<VisibilityMacroDirective>(MD)) {
        Record.push_back(VisMD->isPublic());
      }
      ModuleMacroRecord.push_back(getSubmoduleID(WritingModule));
      ModuleMacroRecord.push_back(getMacroRef(MD->getMacroInfo(), Name));
      Stream.EmitRecord(PP_MODULE_MACRO, ModuleMacroRecord);
      ModuleMacroRecord.clear();
      EmittedModuleMacros = true;
    } else {
      // Emit the macro directives in reverse source order.
      for (; MD; MD = MD->getPrevious()) {
        // Once we hit an ignored macro, we're done: the rest of the chain
        // will all be ignored macros.
        if (shouldIgnoreMacro(MD, IsModule, PP))
          break;
        AddSourceLocation(MD->getLocation(), Record);
        Record.push_back(MD->getKind());
        if (auto *DefMD = dyn_cast<DefMacroDirective>(MD)) {
          Record.push_back(getMacroRef(DefMD->getInfo(), Name));
        } else if (auto *VisMD = dyn_cast<VisibilityMacroDirective>(MD)) {
          Record.push_back(VisMD->isPublic());
        }
      }

      // We write out exported module macros for PCH as well.
      auto Leafs = PP.getLeafModuleMacros(Name);
      SmallVector<ModuleMacro *, 8> Worklist(Leafs.begin(), Leafs.end());
      llvm::DenseMap<ModuleMacro *, unsigned> Visits;
      while (!Worklist.empty()) {
        auto *Macro = Worklist.pop_back_val();

        // Emit a record indicating this submodule exports this macro.
        ModuleMacroRecord.push_back(getSubmoduleID(Macro->getOwningModule()));
        ModuleMacroRecord.push_back(getMacroRef(Macro->getMacroInfo(), Name));
        for (auto *M : Macro->overrides())
          ModuleMacroRecord.push_back(getSubmoduleID(M->getOwningModule()));

        Stream.EmitRecord(PP_MODULE_MACRO, ModuleMacroRecord);
        ModuleMacroRecord.clear();

        // Enqueue overridden macros once we've visited all their ancestors.
        for (auto *M : Macro->overrides())
          if (++Visits[M] == M->getNumOverridingMacros())
            Worklist.push_back(M);

        EmittedModuleMacros = true;
      }
    }
    if (Record.empty() && !EmittedModuleMacros)
      continue;

    IdentMacroDirectivesOffsetMap[Name] = StartOffset;
    Stream.EmitRecord(PP_MACRO_DIRECTIVE_HISTORY, Record);
    Record.clear();
  }

  /// Offsets of each of the macros into the bitstream, indexed by
  /// the local macro ID
  ///
  /// For each identifier that is associated with a macro, this map
  /// provides the offset into the bitstream where that macro is
  /// defined.
  std::vector<uint32_t> MacroOffsets;

  for (unsigned I = 0, N = MacroInfosToEmit.size(); I != N; ++I) {
    const IdentifierInfo *Name = MacroInfosToEmit[I].Name;
    MacroInfo *MI = MacroInfosToEmit[I].MI;
    MacroID ID = MacroInfosToEmit[I].ID;

    if (ID < FirstMacroID) {
      assert(0 && "Loaded MacroInfo entered MacroInfosToEmit ?");
      continue;
    }

    // Record the local offset of this macro.
    unsigned Index = ID - FirstMacroID;
    if (Index >= MacroOffsets.size())
      MacroOffsets.resize(Index + 1);

    uint64_t Offset = Stream.GetCurrentBitNo() - MacroOffsetsBase;
    assert((Offset >> 32) == 0 && "Macro offset too large");
    MacroOffsets[Index] = Offset;

    AddIdentifierRef(Name, Record);
    AddSourceLocation(MI->getDefinitionLoc(), Record);
    AddSourceLocation(MI->getDefinitionEndLoc(), Record);
    Record.push_back(MI->isUsed());
    Record.push_back(MI->isUsedForHeaderGuard());
    Record.push_back(MI->getNumTokens());
    unsigned Code;
    if (MI->isObjectLike()) {
      Code = PP_MACRO_OBJECT_LIKE;
    } else {
      Code = PP_MACRO_FUNCTION_LIKE;

      Record.push_back(MI->isC99Varargs());
      Record.push_back(MI->isGNUVarargs());
      Record.push_back(MI->hasCommaPasting());
      Record.push_back(MI->getNumParams());
      for (const IdentifierInfo *Param : MI->params())
        AddIdentifierRef(Param, Record);
    }

    // If we have a detailed preprocessing record, record the macro definition
    // ID that corresponds to this macro.
    if (PPRec)
      Record.push_back(MacroDefinitions[PPRec->findMacroDefinition(MI)]);

    Stream.EmitRecord(Code, Record);
    Record.clear();

    // Emit the tokens array.
    for (unsigned TokNo = 0, e = MI->getNumTokens(); TokNo != e; ++TokNo) {
      // Note that we know that the preprocessor does not have any annotation
      // tokens in it because they are created by the parser, and thus can't
      // be in a macro definition.
      const Token &Tok = MI->getReplacementToken(TokNo);
      AddToken(Tok, Record);
      Stream.EmitRecord(PP_TOKEN, Record);
      Record.clear();
    }
    ++NumMacros;
  }

  Stream.ExitBlock();

  // Write the offsets table for macro IDs.
  using namespace llvm;

  auto Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(MACRO_OFFSET));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32)); // # of macros
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32)); // first ID
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 32));   // base offset
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));

  unsigned MacroOffsetAbbrev = Stream.EmitAbbrev(std::move(Abbrev));
  {
    RecordData::value_type Record[] = {MACRO_OFFSET, MacroOffsets.size(),
                                       FirstMacroID - NUM_PREDEF_MACRO_IDS,
                                       MacroOffsetsBase - ASTBlockStartOffset};
    Stream.EmitRecordWithBlob(MacroOffsetAbbrev, Record, bytes(MacroOffsets));
  }
}

void ASTWriter::WritePreprocessorDetail(PreprocessingRecord &PPRec,
                                        uint64_t MacroOffsetsBase) {
  if (PPRec.local_begin() == PPRec.local_end())
    return;

  SmallVector<PPEntityOffset, 64> PreprocessedEntityOffsets;

  // Enter the preprocessor block.
  Stream.EnterSubblock(PREPROCESSOR_DETAIL_BLOCK_ID, 3);

  // If the preprocessor has a preprocessing record, emit it.
  unsigned NumPreprocessingRecords = 0;
  using namespace llvm;

  // Set up the abbreviation for
  unsigned InclusionAbbrev = 0;
  {
    auto Abbrev = std::make_shared<BitCodeAbbrev>();
    Abbrev->Add(BitCodeAbbrevOp(PPD_INCLUSION_DIRECTIVE));
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32)); // filename length
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // in quotes
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 2)); // kind
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // imported module
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));
    InclusionAbbrev = Stream.EmitAbbrev(std::move(Abbrev));
  }

  unsigned FirstPreprocessorEntityID
    = (Chain ? PPRec.getNumLoadedPreprocessedEntities() : 0)
    + NUM_PREDEF_PP_ENTITY_IDS;
  unsigned NextPreprocessorEntityID = FirstPreprocessorEntityID;
  RecordData Record;
  for (PreprocessingRecord::iterator E = PPRec.local_begin(),
                                  EEnd = PPRec.local_end();
       E != EEnd;
       (void)++E, ++NumPreprocessingRecords, ++NextPreprocessorEntityID) {
    Record.clear();

    uint64_t Offset = Stream.GetCurrentBitNo() - MacroOffsetsBase;
    assert((Offset >> 32) == 0 && "Preprocessed entity offset too large");
    SourceRange R = getAdjustedRange((*E)->getSourceRange());
    PreprocessedEntityOffsets.emplace_back(
        getRawSourceLocationEncoding(R.getBegin()),
        getRawSourceLocationEncoding(R.getEnd()), Offset);

    if (auto *MD = dyn_cast<MacroDefinitionRecord>(*E)) {
      // Record this macro definition's ID.
      MacroDefinitions[MD] = NextPreprocessorEntityID;

      AddIdentifierRef(MD->getName(), Record);
      Stream.EmitRecord(PPD_MACRO_DEFINITION, Record);
      continue;
    }

    if (auto *ME = dyn_cast<MacroExpansion>(*E)) {
      Record.push_back(ME->isBuiltinMacro());
      if (ME->isBuiltinMacro())
        AddIdentifierRef(ME->getName(), Record);
      else
        Record.push_back(MacroDefinitions[ME->getDefinition()]);
      Stream.EmitRecord(PPD_MACRO_EXPANSION, Record);
      continue;
    }

    if (auto *ID = dyn_cast<InclusionDirective>(*E)) {
      Record.push_back(PPD_INCLUSION_DIRECTIVE);
      Record.push_back(ID->getFileName().size());
      Record.push_back(ID->wasInQuotes());
      Record.push_back(static_cast<unsigned>(ID->getKind()));
      Record.push_back(ID->importedModule());
      SmallString<64> Buffer;
      Buffer += ID->getFileName();
      // Check that the FileEntry is not null because it was not resolved and
      // we create a PCH even with compiler errors.
      if (ID->getFile())
        Buffer += ID->getFile()->getName();
      Stream.EmitRecordWithBlob(InclusionAbbrev, Record, Buffer);
      continue;
    }

    llvm_unreachable("Unhandled PreprocessedEntity in ASTWriter");
  }
  Stream.ExitBlock();

  // Write the offsets table for the preprocessing record.
  if (NumPreprocessingRecords > 0) {
    assert(PreprocessedEntityOffsets.size() == NumPreprocessingRecords);

    // Write the offsets table for identifier IDs.
    using namespace llvm;

    auto Abbrev = std::make_shared<BitCodeAbbrev>();
    Abbrev->Add(BitCodeAbbrevOp(PPD_ENTITIES_OFFSETS));
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32)); // first pp entity
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));
    unsigned PPEOffsetAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

    RecordData::value_type Record[] = {PPD_ENTITIES_OFFSETS,
                                       FirstPreprocessorEntityID -
                                           NUM_PREDEF_PP_ENTITY_IDS};
    Stream.EmitRecordWithBlob(PPEOffsetAbbrev, Record,
                              bytes(PreprocessedEntityOffsets));
  }

  // Write the skipped region table for the preprocessing record.
  ArrayRef<SourceRange> SkippedRanges = PPRec.getSkippedRanges();
  if (SkippedRanges.size() > 0) {
    std::vector<PPSkippedRange> SerializedSkippedRanges;
    SerializedSkippedRanges.reserve(SkippedRanges.size());
    for (auto const& Range : SkippedRanges)
      SerializedSkippedRanges.emplace_back(
          getRawSourceLocationEncoding(Range.getBegin()),
          getRawSourceLocationEncoding(Range.getEnd()));

    using namespace llvm;
    auto Abbrev = std::make_shared<BitCodeAbbrev>();
    Abbrev->Add(BitCodeAbbrevOp(PPD_SKIPPED_RANGES));
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));
    unsigned PPESkippedRangeAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

    Record.clear();
    Record.push_back(PPD_SKIPPED_RANGES);
    Stream.EmitRecordWithBlob(PPESkippedRangeAbbrev, Record,
                              bytes(SerializedSkippedRanges));
  }
}

unsigned ASTWriter::getLocalOrImportedSubmoduleID(const Module *Mod) {
  if (!Mod)
    return 0;

  auto Known = SubmoduleIDs.find(Mod);
  if (Known != SubmoduleIDs.end())
    return Known->second;

  auto *Top = Mod->getTopLevelModule();
  if (Top != WritingModule &&
      (getLangOpts().CompilingPCH ||
       !Top->fullModuleNameIs(StringRef(getLangOpts().CurrentModule))))
    return 0;

  return SubmoduleIDs[Mod] = NextSubmoduleID++;
}

unsigned ASTWriter::getSubmoduleID(Module *Mod) {
  unsigned ID = getLocalOrImportedSubmoduleID(Mod);
  // FIXME: This can easily happen, if we have a reference to a submodule that
  // did not result in us loading a module file for that submodule. For
  // instance, a cross-top-level-module 'conflict' declaration will hit this.
  // assert((ID || !Mod) &&
  //        "asked for module ID for non-local, non-imported module");
  return ID;
}

/// Compute the number of modules within the given tree (including the
/// given module).
static unsigned getNumberOfModules(Module *Mod) {
  unsigned ChildModules = 0;
  for (auto *Submodule : Mod->submodules())
    ChildModules += getNumberOfModules(Submodule);

  return ChildModules + 1;
}

void ASTWriter::WriteSubmodules(Module *WritingModule) {
  // Enter the submodule description block.
  Stream.EnterSubblock(SUBMODULE_BLOCK_ID, /*bits for abbreviations*/5);

  // Write the abbreviations needed for the submodules block.
  using namespace llvm;

  auto Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(SUBMODULE_DEFINITION));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // ID
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Parent
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 4)); // Kind
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 8)); // Definition location
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // IsFramework
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // IsExplicit
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // IsSystem
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // IsExternC
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // InferSubmodules...
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // InferExplicit...
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // InferExportWild...
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // ConfigMacrosExh...
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // ModuleMapIsPriv...
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // NamedModuleHasN...
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)); // Name
  unsigned DefinitionAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

  Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(SUBMODULE_UMBRELLA_HEADER));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)); // Name
  unsigned UmbrellaAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

  Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(SUBMODULE_HEADER));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)); // Name
  unsigned HeaderAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

  Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(SUBMODULE_TOPHEADER));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)); // Name
  unsigned TopHeaderAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

  Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(SUBMODULE_UMBRELLA_DIR));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)); // Name
  unsigned UmbrellaDirAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

  Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(SUBMODULE_REQUIRES));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // State
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));     // Feature
  unsigned RequiresAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

  Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(SUBMODULE_EXCLUDED_HEADER));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)); // Name
  unsigned ExcludedHeaderAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

  Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(SUBMODULE_TEXTUAL_HEADER));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)); // Name
  unsigned TextualHeaderAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

  Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(SUBMODULE_PRIVATE_HEADER));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)); // Name
  unsigned PrivateHeaderAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

  Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(SUBMODULE_PRIVATE_TEXTUAL_HEADER));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)); // Name
  unsigned PrivateTextualHeaderAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

  Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(SUBMODULE_LINK_LIBRARY));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // IsFramework
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));     // Name
  unsigned LinkLibraryAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

  Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(SUBMODULE_CONFIG_MACRO));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));    // Macro name
  unsigned ConfigMacroAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

  Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(SUBMODULE_CONFLICT));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));  // Other module
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));    // Message
  unsigned ConflictAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

  Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(SUBMODULE_EXPORT_AS));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));    // Macro name
  unsigned ExportAsAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

  // Write the submodule metadata block.
  RecordData::value_type Record[] = {
      getNumberOfModules(WritingModule),
      FirstSubmoduleID - NUM_PREDEF_SUBMODULE_IDS};
  Stream.EmitRecord(SUBMODULE_METADATA, Record);

  // Write all of the submodules.
  std::queue<Module *> Q;
  Q.push(WritingModule);
  while (!Q.empty()) {
    Module *Mod = Q.front();
    Q.pop();
    unsigned ID = getSubmoduleID(Mod);

    uint64_t ParentID = 0;
    if (Mod->Parent) {
      assert(SubmoduleIDs[Mod->Parent] && "Submodule parent not written?");
      ParentID = SubmoduleIDs[Mod->Parent];
    }

    SourceLocationEncoding::RawLocEncoding DefinitionLoc =
        getRawSourceLocationEncoding(getAdjustedLocation(Mod->DefinitionLoc));

    // Emit the definition of the block.
    {
      RecordData::value_type Record[] = {SUBMODULE_DEFINITION,
                                         ID,
                                         ParentID,
                                         (RecordData::value_type)Mod->Kind,
                                         DefinitionLoc,
                                         Mod->IsFramework,
                                         Mod->IsExplicit,
                                         Mod->IsSystem,
                                         Mod->IsExternC,
                                         Mod->InferSubmodules,
                                         Mod->InferExplicitSubmodules,
                                         Mod->InferExportWildcard,
                                         Mod->ConfigMacrosExhaustive,
                                         Mod->ModuleMapIsPrivate,
                                         Mod->NamedModuleHasInit};
      Stream.EmitRecordWithBlob(DefinitionAbbrev, Record, Mod->Name);
    }

    // Emit the requirements.
    for (const auto &R : Mod->Requirements) {
      RecordData::value_type Record[] = {SUBMODULE_REQUIRES, R.RequiredState};
      Stream.EmitRecordWithBlob(RequiresAbbrev, Record, R.FeatureName);
    }

    // Emit the umbrella header, if there is one.
    if (std::optional<Module::Header> UmbrellaHeader =
            Mod->getUmbrellaHeaderAsWritten()) {
      RecordData::value_type Record[] = {SUBMODULE_UMBRELLA_HEADER};
      Stream.EmitRecordWithBlob(UmbrellaAbbrev, Record,
                                UmbrellaHeader->NameAsWritten);
    } else if (std::optional<Module::DirectoryName> UmbrellaDir =
                   Mod->getUmbrellaDirAsWritten()) {
      RecordData::value_type Record[] = {SUBMODULE_UMBRELLA_DIR};
      Stream.EmitRecordWithBlob(UmbrellaDirAbbrev, Record,
                                UmbrellaDir->NameAsWritten);
    }

    // Emit the headers.
    struct {
      unsigned RecordKind;
      unsigned Abbrev;
      Module::HeaderKind HeaderKind;
    } HeaderLists[] = {
      {SUBMODULE_HEADER, HeaderAbbrev, Module::HK_Normal},
      {SUBMODULE_TEXTUAL_HEADER, TextualHeaderAbbrev, Module::HK_Textual},
      {SUBMODULE_PRIVATE_HEADER, PrivateHeaderAbbrev, Module::HK_Private},
      {SUBMODULE_PRIVATE_TEXTUAL_HEADER, PrivateTextualHeaderAbbrev,
        Module::HK_PrivateTextual},
      {SUBMODULE_EXCLUDED_HEADER, ExcludedHeaderAbbrev, Module::HK_Excluded}
    };
    for (auto &HL : HeaderLists) {
      RecordData::value_type Record[] = {HL.RecordKind};
      for (auto &H : Mod->Headers[HL.HeaderKind])
        Stream.EmitRecordWithBlob(HL.Abbrev, Record, H.NameAsWritten);
    }

    // Emit the top headers.
    {
      RecordData::value_type Record[] = {SUBMODULE_TOPHEADER};
      for (FileEntryRef H : Mod->getTopHeaders(PP->getFileManager())) {
        SmallString<128> HeaderName(H.getName());
        PreparePathForOutput(HeaderName);
        Stream.EmitRecordWithBlob(TopHeaderAbbrev, Record, HeaderName);
      }
    }

    // Emit the imports.
    if (!Mod->Imports.empty()) {
      RecordData Record;
      for (auto *I : Mod->Imports)
        Record.push_back(getSubmoduleID(I));
      Stream.EmitRecord(SUBMODULE_IMPORTS, Record);
    }

    // Emit the modules affecting compilation that were not imported.
    if (!Mod->AffectingClangModules.empty()) {
      RecordData Record;
      for (auto *I : Mod->AffectingClangModules)
        Record.push_back(getSubmoduleID(I));
      Stream.EmitRecord(SUBMODULE_AFFECTING_MODULES, Record);
    }

    // Emit the exports.
    if (!Mod->Exports.empty()) {
      RecordData Record;
      for (const auto &E : Mod->Exports) {
        // FIXME: This may fail; we don't require that all exported modules
        // are local or imported.
        Record.push_back(getSubmoduleID(E.getPointer()));
        Record.push_back(E.getInt());
      }
      Stream.EmitRecord(SUBMODULE_EXPORTS, Record);
    }

    //FIXME: How do we emit the 'use'd modules?  They may not be submodules.
    // Might be unnecessary as use declarations are only used to build the
    // module itself.

    // TODO: Consider serializing undeclared uses of modules.

    // Emit the link libraries.
    for (const auto &LL : Mod->LinkLibraries) {
      RecordData::value_type Record[] = {SUBMODULE_LINK_LIBRARY,
                                         LL.IsFramework};
      Stream.EmitRecordWithBlob(LinkLibraryAbbrev, Record, LL.Library);
    }

    // Emit the conflicts.
    for (const auto &C : Mod->Conflicts) {
      // FIXME: This may fail; we don't require that all conflicting modules
      // are local or imported.
      RecordData::value_type Record[] = {SUBMODULE_CONFLICT,
                                         getSubmoduleID(C.Other)};
      Stream.EmitRecordWithBlob(ConflictAbbrev, Record, C.Message);
    }

    // Emit the configuration macros.
    for (const auto &CM : Mod->ConfigMacros) {
      RecordData::value_type Record[] = {SUBMODULE_CONFIG_MACRO};
      Stream.EmitRecordWithBlob(ConfigMacroAbbrev, Record, CM);
    }

    // Emit the reachable initializers.
    // The initializer may only be unreachable in reduced BMI.
    RecordData Inits;
    for (Decl *D : Context->getModuleInitializers(Mod))
      if (wasDeclEmitted(D))
        AddDeclRef(D, Inits);
    if (!Inits.empty())
      Stream.EmitRecord(SUBMODULE_INITIALIZERS, Inits);

    // Emit the name of the re-exported module, if any.
    if (!Mod->ExportAsModule.empty()) {
      RecordData::value_type Record[] = {SUBMODULE_EXPORT_AS};
      Stream.EmitRecordWithBlob(ExportAsAbbrev, Record, Mod->ExportAsModule);
    }

    // Queue up the submodules of this module.
    for (auto *M : Mod->submodules())
      Q.push(M);
  }

  Stream.ExitBlock();

  assert((NextSubmoduleID - FirstSubmoduleID ==
          getNumberOfModules(WritingModule)) &&
         "Wrong # of submodules; found a reference to a non-local, "
         "non-imported submodule?");
}

void ASTWriter::WritePragmaDiagnosticMappings(const DiagnosticsEngine &Diag,
                                              bool isModule) {
  llvm::SmallDenseMap<const DiagnosticsEngine::DiagState *, unsigned, 64>
      DiagStateIDMap;
  unsigned CurrID = 0;
  RecordData Record;

  auto EncodeDiagStateFlags =
      [](const DiagnosticsEngine::DiagState *DS) -> unsigned {
    unsigned Result = (unsigned)DS->ExtBehavior;
    for (unsigned Val :
         {(unsigned)DS->IgnoreAllWarnings, (unsigned)DS->EnableAllWarnings,
          (unsigned)DS->WarningsAsErrors, (unsigned)DS->ErrorsAsFatal,
          (unsigned)DS->SuppressSystemWarnings})
      Result = (Result << 1) | Val;
    return Result;
  };

  unsigned Flags = EncodeDiagStateFlags(Diag.DiagStatesByLoc.FirstDiagState);
  Record.push_back(Flags);

  auto AddDiagState = [&](const DiagnosticsEngine::DiagState *State,
                          bool IncludeNonPragmaStates) {
    // Ensure that the diagnostic state wasn't modified since it was created.
    // We will not correctly round-trip this information otherwise.
    assert(Flags == EncodeDiagStateFlags(State) &&
           "diag state flags vary in single AST file");

    // If we ever serialize non-pragma mappings outside the initial state, the
    // code below will need to consider more than getDefaultMapping.
    assert(!IncludeNonPragmaStates ||
           State == Diag.DiagStatesByLoc.FirstDiagState);

    unsigned &DiagStateID = DiagStateIDMap[State];
    Record.push_back(DiagStateID);

    if (DiagStateID == 0) {
      DiagStateID = ++CurrID;
      SmallVector<std::pair<unsigned, DiagnosticMapping>> Mappings;

      // Add a placeholder for the number of mappings.
      auto SizeIdx = Record.size();
      Record.emplace_back();
      for (const auto &I : *State) {
        // Maybe skip non-pragmas.
        if (!I.second.isPragma() && !IncludeNonPragmaStates)
          continue;
        // Skip default mappings. We have a mapping for every diagnostic ever
        // emitted, regardless of whether it was customized.
        if (!I.second.isPragma() &&
            I.second == DiagnosticIDs::getDefaultMapping(I.first))
          continue;
        Mappings.push_back(I);
      }

      // Sort by diag::kind for deterministic output.
      llvm::sort(Mappings, llvm::less_first());

      for (const auto &I : Mappings) {
        Record.push_back(I.first);
        Record.push_back(I.second.serialize());
      }
      // Update the placeholder.
      Record[SizeIdx] = (Record.size() - SizeIdx) / 2;
    }
  };

  AddDiagState(Diag.DiagStatesByLoc.FirstDiagState, isModule);

  // Reserve a spot for the number of locations with state transitions.
  auto NumLocationsIdx = Record.size();
  Record.emplace_back();

  // Emit the state transitions.
  unsigned NumLocations = 0;
  for (auto &FileIDAndFile : Diag.DiagStatesByLoc.Files) {
    if (!FileIDAndFile.first.isValid() ||
        !FileIDAndFile.second.HasLocalTransitions)
      continue;
    ++NumLocations;

    AddFileID(FileIDAndFile.first, Record);

    Record.push_back(FileIDAndFile.second.StateTransitions.size());
    for (auto &StatePoint : FileIDAndFile.second.StateTransitions) {
      Record.push_back(getAdjustedOffset(StatePoint.Offset));
      AddDiagState(StatePoint.State, false);
    }
  }

  // Backpatch the number of locations.
  Record[NumLocationsIdx] = NumLocations;

  // Emit CurDiagStateLoc.  Do it last in order to match source order.
  //
  // This also protects against a hypothetical corner case with simulating
  // -Werror settings for implicit modules in the ASTReader, where reading
  // CurDiagState out of context could change whether warning pragmas are
  // treated as errors.
  AddSourceLocation(Diag.DiagStatesByLoc.CurDiagStateLoc, Record);
  AddDiagState(Diag.DiagStatesByLoc.CurDiagState, false);

  Stream.EmitRecord(DIAG_PRAGMA_MAPPINGS, Record);
}

//===----------------------------------------------------------------------===//
// Type Serialization
//===----------------------------------------------------------------------===//

/// Write the representation of a type to the AST stream.
void ASTWriter::WriteType(QualType T) {
  TypeIdx &IdxRef = TypeIdxs[T];
  if (IdxRef.getValue() == 0) // we haven't seen this type before.
    IdxRef = TypeIdx(0, NextTypeID++);
  TypeIdx Idx = IdxRef;

  assert(Idx.getModuleFileIndex() == 0 && "Re-writing a type from a prior AST");
  assert(Idx.getValue() >= FirstTypeID && "Writing predefined type");

  // Emit the type's representation.
  uint64_t Offset = ASTTypeWriter(*this).write(T) - DeclTypesBlockStartOffset;

  // Record the offset for this type.
  uint64_t Index = Idx.getValue() - FirstTypeID;
  if (TypeOffsets.size() == Index)
    TypeOffsets.emplace_back(Offset);
  else if (TypeOffsets.size() < Index) {
    TypeOffsets.resize(Index + 1);
    TypeOffsets[Index].set(Offset);
  } else {
    llvm_unreachable("Types emitted in wrong order");
  }
}

//===----------------------------------------------------------------------===//
// Declaration Serialization
//===----------------------------------------------------------------------===//

static bool IsInternalDeclFromFileContext(const Decl *D) {
  auto *ND = dyn_cast<NamedDecl>(D);
  if (!ND)
    return false;

  if (!D->getDeclContext()->getRedeclContext()->isFileContext())
    return false;

  return ND->getFormalLinkage() == Linkage::Internal;
}

/// Write the block containing all of the declaration IDs
/// lexically declared within the given DeclContext.
///
/// \returns the offset of the DECL_CONTEXT_LEXICAL block within the
/// bitstream, or 0 if no block was written.
uint64_t ASTWriter::WriteDeclContextLexicalBlock(ASTContext &Context,
                                                 const DeclContext *DC) {
  if (DC->decls_empty())
    return 0;

  // In reduced BMI, we don't care the declarations in functions.
  if (GeneratingReducedBMI && DC->isFunctionOrMethod())
    return 0;

  uint64_t Offset = Stream.GetCurrentBitNo();
  SmallVector<DeclID, 128> KindDeclPairs;
  for (const auto *D : DC->decls()) {
    if (DoneWritingDeclsAndTypes && !wasDeclEmitted(D))
      continue;

    // We don't need to write decls with internal linkage into reduced BMI.
    // If such decls gets emitted due to it get used from inline functions,
    // the program illegal. However, there are too many use of static inline
    // functions in the global module fragment and it will be breaking change
    // to forbid that. So we have to allow to emit such declarations from GMF.
    if (GeneratingReducedBMI && !D->isFromExplicitGlobalModule() &&
        IsInternalDeclFromFileContext(D))
      continue;

    KindDeclPairs.push_back(D->getKind());
    KindDeclPairs.push_back(GetDeclRef(D).getRawValue());
  }

  ++NumLexicalDeclContexts;
  RecordData::value_type Record[] = {DECL_CONTEXT_LEXICAL};
  Stream.EmitRecordWithBlob(DeclContextLexicalAbbrev, Record,
                            bytes(KindDeclPairs));
  return Offset;
}

void ASTWriter::WriteTypeDeclOffsets() {
  using namespace llvm;

  // Write the type offsets array
  auto Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(TYPE_OFFSET));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32)); // # of types
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)); // types block
  unsigned TypeOffsetAbbrev = Stream.EmitAbbrev(std::move(Abbrev));
  {
    RecordData::value_type Record[] = {TYPE_OFFSET, TypeOffsets.size()};
    Stream.EmitRecordWithBlob(TypeOffsetAbbrev, Record, bytes(TypeOffsets));
  }

  // Write the declaration offsets array
  Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(DECL_OFFSET));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32)); // # of declarations
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)); // declarations block
  unsigned DeclOffsetAbbrev = Stream.EmitAbbrev(std::move(Abbrev));
  {
    RecordData::value_type Record[] = {DECL_OFFSET, DeclOffsets.size()};
    Stream.EmitRecordWithBlob(DeclOffsetAbbrev, Record, bytes(DeclOffsets));
  }
}

void ASTWriter::WriteFileDeclIDsMap() {
  using namespace llvm;

  SmallVector<std::pair<FileID, DeclIDInFileInfo *>, 64> SortedFileDeclIDs;
  SortedFileDeclIDs.reserve(FileDeclIDs.size());
  for (const auto &P : FileDeclIDs)
    SortedFileDeclIDs.push_back(std::make_pair(P.first, P.second.get()));
  llvm::sort(SortedFileDeclIDs, llvm::less_first());

  // Join the vectors of DeclIDs from all files.
  SmallVector<DeclID, 256> FileGroupedDeclIDs;
  for (auto &FileDeclEntry : SortedFileDeclIDs) {
    DeclIDInFileInfo &Info = *FileDeclEntry.second;
    Info.FirstDeclIndex = FileGroupedDeclIDs.size();
    llvm::stable_sort(Info.DeclIDs);
    for (auto &LocDeclEntry : Info.DeclIDs)
      FileGroupedDeclIDs.push_back(LocDeclEntry.second.getRawValue());
  }

  auto Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(FILE_SORTED_DECLS));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));
  unsigned AbbrevCode = Stream.EmitAbbrev(std::move(Abbrev));
  RecordData::value_type Record[] = {FILE_SORTED_DECLS,
                                     FileGroupedDeclIDs.size()};
  Stream.EmitRecordWithBlob(AbbrevCode, Record, bytes(FileGroupedDeclIDs));
}

void ASTWriter::WriteComments() {
  Stream.EnterSubblock(COMMENTS_BLOCK_ID, 3);
  auto _ = llvm::make_scope_exit([this] { Stream.ExitBlock(); });
  if (!PP->getPreprocessorOpts().WriteCommentListToPCH)
    return;

  // Don't write comments to BMI to reduce the size of BMI.
  // If language services (e.g., clangd) want such abilities,
  // we can offer a special option then.
  if (isWritingStdCXXNamedModules())
    return;

  RecordData Record;
  for (const auto &FO : Context->Comments.OrderedComments) {
    for (const auto &OC : FO.second) {
      const RawComment *I = OC.second;
      Record.clear();
      AddSourceRange(I->getSourceRange(), Record);
      Record.push_back(I->getKind());
      Record.push_back(I->isTrailingComment());
      Record.push_back(I->isAlmostTrailingComment());
      Stream.EmitRecord(COMMENTS_RAW_COMMENT, Record);
    }
  }
}

//===----------------------------------------------------------------------===//
// Global Method Pool and Selector Serialization
//===----------------------------------------------------------------------===//

namespace {

// Trait used for the on-disk hash table used in the method pool.
class ASTMethodPoolTrait {
  ASTWriter &Writer;

public:
  using key_type = Selector;
  using key_type_ref = key_type;

  struct data_type {
    SelectorID ID;
    ObjCMethodList Instance, Factory;
  };
  using data_type_ref = const data_type &;

  using hash_value_type = unsigned;
  using offset_type = unsigned;

  explicit ASTMethodPoolTrait(ASTWriter &Writer) : Writer(Writer) {}

  static hash_value_type ComputeHash(Selector Sel) {
    return serialization::ComputeHash(Sel);
  }

  std::pair<unsigned, unsigned>
    EmitKeyDataLength(raw_ostream& Out, Selector Sel,
                      data_type_ref Methods) {
    unsigned KeyLen =
        2 + (Sel.getNumArgs() ? Sel.getNumArgs() * sizeof(IdentifierID)
                              : sizeof(IdentifierID));
    unsigned DataLen = 4 + 2 + 2; // 2 bytes for each of the method counts
    for (const ObjCMethodList *Method = &Methods.Instance; Method;
         Method = Method->getNext())
      if (ShouldWriteMethodListNode(Method))
        DataLen += sizeof(DeclID);
    for (const ObjCMethodList *Method = &Methods.Factory; Method;
         Method = Method->getNext())
      if (ShouldWriteMethodListNode(Method))
        DataLen += sizeof(DeclID);
    return emitULEBKeyDataLength(KeyLen, DataLen, Out);
  }

  void EmitKey(raw_ostream& Out, Selector Sel, unsigned) {
    using namespace llvm::support;

    endian::Writer LE(Out, llvm::endianness::little);
    uint64_t Start = Out.tell();
    assert((Start >> 32) == 0 && "Selector key offset too large");
    Writer.SetSelectorOffset(Sel, Start);
    unsigned N = Sel.getNumArgs();
    LE.write<uint16_t>(N);
    if (N == 0)
      N = 1;
    for (unsigned I = 0; I != N; ++I)
      LE.write<IdentifierID>(
          Writer.getIdentifierRef(Sel.getIdentifierInfoForSlot(I)));
  }

  void EmitData(raw_ostream& Out, key_type_ref,
                data_type_ref Methods, unsigned DataLen) {
    using namespace llvm::support;

    endian::Writer LE(Out, llvm::endianness::little);
    uint64_t Start = Out.tell(); (void)Start;
    LE.write<uint32_t>(Methods.ID);
    unsigned NumInstanceMethods = 0;
    for (const ObjCMethodList *Method = &Methods.Instance; Method;
         Method = Method->getNext())
      if (ShouldWriteMethodListNode(Method))
        ++NumInstanceMethods;

    unsigned NumFactoryMethods = 0;
    for (const ObjCMethodList *Method = &Methods.Factory; Method;
         Method = Method->getNext())
      if (ShouldWriteMethodListNode(Method))
        ++NumFactoryMethods;

    unsigned InstanceBits = Methods.Instance.getBits();
    assert(InstanceBits < 4);
    unsigned InstanceHasMoreThanOneDeclBit =
        Methods.Instance.hasMoreThanOneDecl();
    unsigned FullInstanceBits = (NumInstanceMethods << 3) |
                                (InstanceHasMoreThanOneDeclBit << 2) |
                                InstanceBits;
    unsigned FactoryBits = Methods.Factory.getBits();
    assert(FactoryBits < 4);
    unsigned FactoryHasMoreThanOneDeclBit =
        Methods.Factory.hasMoreThanOneDecl();
    unsigned FullFactoryBits = (NumFactoryMethods << 3) |
                               (FactoryHasMoreThanOneDeclBit << 2) |
                               FactoryBits;
    LE.write<uint16_t>(FullInstanceBits);
    LE.write<uint16_t>(FullFactoryBits);
    for (const ObjCMethodList *Method = &Methods.Instance; Method;
         Method = Method->getNext())
      if (ShouldWriteMethodListNode(Method))
        LE.write<DeclID>((DeclID)Writer.getDeclID(Method->getMethod()));
    for (const ObjCMethodList *Method = &Methods.Factory; Method;
         Method = Method->getNext())
      if (ShouldWriteMethodListNode(Method))
        LE.write<DeclID>((DeclID)Writer.getDeclID(Method->getMethod()));

    assert(Out.tell() - Start == DataLen && "Data length is wrong");
  }

private:
  static bool ShouldWriteMethodListNode(const ObjCMethodList *Node) {
    return (Node->getMethod() && !Node->getMethod()->isFromASTFile());
  }
};

} // namespace

/// Write ObjC data: selectors and the method pool.
///
/// The method pool contains both instance and factory methods, stored
/// in an on-disk hash table indexed by the selector. The hash table also
/// contains an empty entry for every other selector known to Sema.
void ASTWriter::WriteSelectors(Sema &SemaRef) {
  using namespace llvm;

  // Do we have to do anything at all?
  if (SemaRef.ObjC().MethodPool.empty() && SelectorIDs.empty())
    return;
  unsigned NumTableEntries = 0;
  // Create and write out the blob that contains selectors and the method pool.
  {
    llvm::OnDiskChainedHashTableGenerator<ASTMethodPoolTrait> Generator;
    ASTMethodPoolTrait Trait(*this);

    // Create the on-disk hash table representation. We walk through every
    // selector we've seen and look it up in the method pool.
    SelectorOffsets.resize(NextSelectorID - FirstSelectorID);
    for (auto &SelectorAndID : SelectorIDs) {
      Selector S = SelectorAndID.first;
      SelectorID ID = SelectorAndID.second;
      SemaObjC::GlobalMethodPool::iterator F =
          SemaRef.ObjC().MethodPool.find(S);
      ASTMethodPoolTrait::data_type Data = {
        ID,
        ObjCMethodList(),
        ObjCMethodList()
      };
      if (F != SemaRef.ObjC().MethodPool.end()) {
        Data.Instance = F->second.first;
        Data.Factory = F->second.second;
      }
      // Only write this selector if it's not in an existing AST or something
      // changed.
      if (Chain && ID < FirstSelectorID) {
        // Selector already exists. Did it change?
        bool changed = false;
        for (ObjCMethodList *M = &Data.Instance; M && M->getMethod();
             M = M->getNext()) {
          if (!M->getMethod()->isFromASTFile()) {
            changed = true;
            Data.Instance = *M;
            break;
          }
        }
        for (ObjCMethodList *M = &Data.Factory; M && M->getMethod();
             M = M->getNext()) {
          if (!M->getMethod()->isFromASTFile()) {
            changed = true;
            Data.Factory = *M;
            break;
          }
        }
        if (!changed)
          continue;
      } else if (Data.Instance.getMethod() || Data.Factory.getMethod()) {
        // A new method pool entry.
        ++NumTableEntries;
      }
      Generator.insert(S, Data, Trait);
    }

    // Create the on-disk hash table in a buffer.
    SmallString<4096> MethodPool;
    uint32_t BucketOffset;
    {
      using namespace llvm::support;

      ASTMethodPoolTrait Trait(*this);
      llvm::raw_svector_ostream Out(MethodPool);
      // Make sure that no bucket is at offset 0
      endian::write<uint32_t>(Out, 0, llvm::endianness::little);
      BucketOffset = Generator.Emit(Out, Trait);
    }

    // Create a blob abbreviation
    auto Abbrev = std::make_shared<BitCodeAbbrev>();
    Abbrev->Add(BitCodeAbbrevOp(METHOD_POOL));
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32));
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32));
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));
    unsigned MethodPoolAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

    // Write the method pool
    {
      RecordData::value_type Record[] = {METHOD_POOL, BucketOffset,
                                         NumTableEntries};
      Stream.EmitRecordWithBlob(MethodPoolAbbrev, Record, MethodPool);
    }

    // Create a blob abbreviation for the selector table offsets.
    Abbrev = std::make_shared<BitCodeAbbrev>();
    Abbrev->Add(BitCodeAbbrevOp(SELECTOR_OFFSETS));
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32)); // size
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32)); // first ID
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));
    unsigned SelectorOffsetAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

    // Write the selector offsets table.
    {
      RecordData::value_type Record[] = {
          SELECTOR_OFFSETS, SelectorOffsets.size(),
          FirstSelectorID - NUM_PREDEF_SELECTOR_IDS};
      Stream.EmitRecordWithBlob(SelectorOffsetAbbrev, Record,
                                bytes(SelectorOffsets));
    }
  }
}

/// Write the selectors referenced in @selector expression into AST file.
void ASTWriter::WriteReferencedSelectorsPool(Sema &SemaRef) {
  using namespace llvm;

  if (SemaRef.ObjC().ReferencedSelectors.empty())
    return;

  RecordData Record;
  ASTRecordWriter Writer(*this, Record);

  // Note: this writes out all references even for a dependent AST. But it is
  // very tricky to fix, and given that @selector shouldn't really appear in
  // headers, probably not worth it. It's not a correctness issue.
  for (auto &SelectorAndLocation : SemaRef.ObjC().ReferencedSelectors) {
    Selector Sel = SelectorAndLocation.first;
    SourceLocation Loc = SelectorAndLocation.second;
    Writer.AddSelectorRef(Sel);
    Writer.AddSourceLocation(Loc);
  }
  Writer.Emit(REFERENCED_SELECTOR_POOL);
}

//===----------------------------------------------------------------------===//
// Identifier Table Serialization
//===----------------------------------------------------------------------===//

/// Determine the declaration that should be put into the name lookup table to
/// represent the given declaration in this module. This is usually D itself,
/// but if D was imported and merged into a local declaration, we want the most
/// recent local declaration instead. The chosen declaration will be the most
/// recent declaration in any module that imports this one.
static NamedDecl *getDeclForLocalLookup(const LangOptions &LangOpts,
                                        NamedDecl *D) {
  if (!LangOpts.Modules || !D->isFromASTFile())
    return D;

  if (Decl *Redecl = D->getPreviousDecl()) {
    // For Redeclarable decls, a prior declaration might be local.
    for (; Redecl; Redecl = Redecl->getPreviousDecl()) {
      // If we find a local decl, we're done.
      if (!Redecl->isFromASTFile()) {
        // Exception: in very rare cases (for injected-class-names), not all
        // redeclarations are in the same semantic context. Skip ones in a
        // different context. They don't go in this lookup table at all.
        if (!Redecl->getDeclContext()->getRedeclContext()->Equals(
                D->getDeclContext()->getRedeclContext()))
          continue;
        return cast<NamedDecl>(Redecl);
      }

      // If we find a decl from a (chained-)PCH stop since we won't find a
      // local one.
      if (Redecl->getOwningModuleID() == 0)
        break;
    }
  } else if (Decl *First = D->getCanonicalDecl()) {
    // For Mergeable decls, the first decl might be local.
    if (!First->isFromASTFile())
      return cast<NamedDecl>(First);
  }

  // All declarations are imported. Our most recent declaration will also be
  // the most recent one in anyone who imports us.
  return D;
}

namespace {

bool IsInterestingIdentifier(const IdentifierInfo *II, uint64_t MacroOffset,
                             bool IsModule, bool IsCPlusPlus) {
  bool NeedDecls = !IsModule || !IsCPlusPlus;

  bool IsInteresting =
      II->getNotableIdentifierID() != tok::NotableIdentifierKind::not_notable ||
      II->getBuiltinID() != Builtin::ID::NotBuiltin ||
      II->getObjCKeywordID() != tok::ObjCKeywordKind::objc_not_keyword;
  if (MacroOffset || II->isPoisoned() || (!IsModule && IsInteresting) ||
      II->hasRevertedTokenIDToIdentifier() ||
      (NeedDecls && II->getFETokenInfo()))
    return true;

  return false;
}

bool IsInterestingNonMacroIdentifier(const IdentifierInfo *II,
                                     ASTWriter &Writer) {
  bool IsModule = Writer.isWritingModule();
  bool IsCPlusPlus = Writer.getLangOpts().CPlusPlus;
  return IsInterestingIdentifier(II, /*MacroOffset=*/0, IsModule, IsCPlusPlus);
}

class ASTIdentifierTableTrait {
  ASTWriter &Writer;
  Preprocessor &PP;
  IdentifierResolver &IdResolver;
  bool IsModule;
  bool NeedDecls;
  ASTWriter::RecordData *InterestingIdentifierOffsets;

  /// Determines whether this is an "interesting" identifier that needs a
  /// full IdentifierInfo structure written into the hash table. Notably, this
  /// doesn't check whether the name has macros defined; use PublicMacroIterator
  /// to check that.
  bool isInterestingIdentifier(const IdentifierInfo *II, uint64_t MacroOffset) {
    return IsInterestingIdentifier(II, MacroOffset, IsModule,
                                   Writer.getLangOpts().CPlusPlus);
  }

public:
  using key_type = const IdentifierInfo *;
  using key_type_ref = key_type;

  using data_type = IdentifierID;
  using data_type_ref = data_type;

  using hash_value_type = unsigned;
  using offset_type = unsigned;

  ASTIdentifierTableTrait(ASTWriter &Writer, Preprocessor &PP,
                          IdentifierResolver &IdResolver, bool IsModule,
                          ASTWriter::RecordData *InterestingIdentifierOffsets)
      : Writer(Writer), PP(PP), IdResolver(IdResolver), IsModule(IsModule),
        NeedDecls(!IsModule || !Writer.getLangOpts().CPlusPlus),
        InterestingIdentifierOffsets(InterestingIdentifierOffsets) {}

  bool needDecls() const { return NeedDecls; }

  static hash_value_type ComputeHash(const IdentifierInfo* II) {
    return llvm::djbHash(II->getName());
  }

  bool isInterestingIdentifier(const IdentifierInfo *II) {
    auto MacroOffset = Writer.getMacroDirectivesOffset(II);
    return isInterestingIdentifier(II, MacroOffset);
  }

  std::pair<unsigned, unsigned>
  EmitKeyDataLength(raw_ostream &Out, const IdentifierInfo *II, IdentifierID ID) {
    // Record the location of the identifier data. This is used when generating
    // the mapping from persistent IDs to strings.
    Writer.SetIdentifierOffset(II, Out.tell());

    auto MacroOffset = Writer.getMacroDirectivesOffset(II);

    // Emit the offset of the key/data length information to the interesting
    // identifiers table if necessary.
    if (InterestingIdentifierOffsets &&
        isInterestingIdentifier(II, MacroOffset))
      InterestingIdentifierOffsets->push_back(Out.tell());

    unsigned KeyLen = II->getLength() + 1;
    unsigned DataLen = sizeof(IdentifierID); // bytes for the persistent ID << 1
    if (isInterestingIdentifier(II, MacroOffset)) {
      DataLen += 2; // 2 bytes for builtin ID
      DataLen += 2; // 2 bytes for flags
      if (MacroOffset)
        DataLen += 4; // MacroDirectives offset.

      if (NeedDecls)
        DataLen += std::distance(IdResolver.begin(II), IdResolver.end()) *
                   sizeof(DeclID);
    }
    return emitULEBKeyDataLength(KeyLen, DataLen, Out);
  }

  void EmitKey(raw_ostream &Out, const IdentifierInfo *II, unsigned KeyLen) {
    Out.write(II->getNameStart(), KeyLen);
  }

  void EmitData(raw_ostream &Out, const IdentifierInfo *II, IdentifierID ID,
                unsigned) {
    using namespace llvm::support;

    endian::Writer LE(Out, llvm::endianness::little);

    auto MacroOffset = Writer.getMacroDirectivesOffset(II);
    if (!isInterestingIdentifier(II, MacroOffset)) {
      LE.write<IdentifierID>(ID << 1);
      return;
    }

    LE.write<IdentifierID>((ID << 1) | 0x01);
    uint32_t Bits = (uint32_t)II->getObjCOrBuiltinID();
    assert((Bits & 0xffff) == Bits && "ObjCOrBuiltinID too big for ASTReader.");
    LE.write<uint16_t>(Bits);
    Bits = 0;
    bool HadMacroDefinition = MacroOffset != 0;
    Bits = (Bits << 1) | unsigned(HadMacroDefinition);
    Bits = (Bits << 1) | unsigned(II->isExtensionToken());
    Bits = (Bits << 1) | unsigned(II->isPoisoned());
    Bits = (Bits << 1) | unsigned(II->hasRevertedTokenIDToIdentifier());
    Bits = (Bits << 1) | unsigned(II->isCPlusPlusOperatorKeyword());
    LE.write<uint16_t>(Bits);

    if (HadMacroDefinition)
      LE.write<uint32_t>(MacroOffset);

    if (NeedDecls) {
      // Emit the declaration IDs in reverse order, because the
      // IdentifierResolver provides the declarations as they would be
      // visible (e.g., the function "stat" would come before the struct
      // "stat"), but the ASTReader adds declarations to the end of the list
      // (so we need to see the struct "stat" before the function "stat").
      // Only emit declarations that aren't from a chained PCH, though.
      SmallVector<NamedDecl *, 16> Decls(IdResolver.decls(II));
      for (NamedDecl *D : llvm::reverse(Decls))
        LE.write<DeclID>((DeclID)Writer.getDeclID(
            getDeclForLocalLookup(PP.getLangOpts(), D)));
    }
  }
};

} // namespace

/// If the \param IdentifierID ID is a local Identifier ID. If the higher
/// bits of ID is 0, it implies that the ID doesn't come from AST files.
static bool isLocalIdentifierID(IdentifierID ID) { return !(ID >> 32); }

/// Write the identifier table into the AST file.
///
/// The identifier table consists of a blob containing string data
/// (the actual identifiers themselves) and a separate "offsets" index
/// that maps identifier IDs to locations within the blob.
void ASTWriter::WriteIdentifierTable(Preprocessor &PP,
                                     IdentifierResolver &IdResolver,
                                     bool IsModule) {
  using namespace llvm;

  RecordData InterestingIdents;

  // Create and write out the blob that contains the identifier
  // strings.
  {
    llvm::OnDiskChainedHashTableGenerator<ASTIdentifierTableTrait> Generator;
    ASTIdentifierTableTrait Trait(*this, PP, IdResolver, IsModule,
                                  IsModule ? &InterestingIdents : nullptr);

    // Create the on-disk hash table representation. We only store offsets
    // for identifiers that appear here for the first time.
    IdentifierOffsets.resize(NextIdentID - FirstIdentID);
    for (auto IdentIDPair : IdentifierIDs) {
      const IdentifierInfo *II = IdentIDPair.first;
      IdentifierID ID = IdentIDPair.second;
      assert(II && "NULL identifier in identifier table");

      // Write out identifiers if either the ID is local or the identifier has
      // changed since it was loaded.
      if (isLocalIdentifierID(ID) || II->hasChangedSinceDeserialization() ||
          (Trait.needDecls() &&
           II->hasFETokenInfoChangedSinceDeserialization()))
        Generator.insert(II, ID, Trait);
    }

    // Create the on-disk hash table in a buffer.
    SmallString<4096> IdentifierTable;
    uint32_t BucketOffset;
    {
      using namespace llvm::support;

      llvm::raw_svector_ostream Out(IdentifierTable);
      // Make sure that no bucket is at offset 0
      endian::write<uint32_t>(Out, 0, llvm::endianness::little);
      BucketOffset = Generator.Emit(Out, Trait);
    }

    // Create a blob abbreviation
    auto Abbrev = std::make_shared<BitCodeAbbrev>();
    Abbrev->Add(BitCodeAbbrevOp(IDENTIFIER_TABLE));
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32));
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));
    unsigned IDTableAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

    // Write the identifier table
    RecordData::value_type Record[] = {IDENTIFIER_TABLE, BucketOffset};
    Stream.EmitRecordWithBlob(IDTableAbbrev, Record, IdentifierTable);
  }

  // Write the offsets table for identifier IDs.
  auto Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(IDENTIFIER_OFFSET));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32)); // # of identifiers
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));
  unsigned IdentifierOffsetAbbrev = Stream.EmitAbbrev(std::move(Abbrev));

#ifndef NDEBUG
  for (unsigned I = 0, N = IdentifierOffsets.size(); I != N; ++I)
    assert(IdentifierOffsets[I] && "Missing identifier offset?");
#endif

  RecordData::value_type Record[] = {IDENTIFIER_OFFSET,
                                     IdentifierOffsets.size()};
  Stream.EmitRecordWithBlob(IdentifierOffsetAbbrev, Record,
                            bytes(IdentifierOffsets));

  // In C++, write the list of interesting identifiers (those that are
  // defined as macros, poisoned, or similar unusual things).
  if (!InterestingIdents.empty())
    Stream.EmitRecord(INTERESTING_IDENTIFIERS, InterestingIdents);
}

void ASTWriter::handleVTable(CXXRecordDecl *RD) {
  if (!RD->isInNamedModule())
    return;

  PendingEmittingVTables.push_back(RD);
}

//===----------------------------------------------------------------------===//
// DeclContext's Name Lookup Table Serialization
//===----------------------------------------------------------------------===//

namespace {

// Trait used for the on-disk hash table used in the method pool.
class ASTDeclContextNameLookupTrait {
  ASTWriter &Writer;
  llvm::SmallVector<LocalDeclID, 64> DeclIDs;

public:
  using key_type = DeclarationNameKey;
  using key_type_ref = key_type;

  /// A start and end index into DeclIDs, representing a sequence of decls.
  using data_type = std::pair<unsigned, unsigned>;
  using data_type_ref = const data_type &;

  using hash_value_type = unsigned;
  using offset_type = unsigned;

  explicit ASTDeclContextNameLookupTrait(ASTWriter &Writer) : Writer(Writer) {}

  template<typename Coll>
  data_type getData(const Coll &Decls) {
    unsigned Start = DeclIDs.size();
    for (NamedDecl *D : Decls) {
      NamedDecl *DeclForLocalLookup =
          getDeclForLocalLookup(Writer.getLangOpts(), D);

      if (Writer.getDoneWritingDeclsAndTypes() &&
          !Writer.wasDeclEmitted(DeclForLocalLookup))
        continue;

      // Try to avoid writing internal decls to reduced BMI.
      // See comments in ASTWriter::WriteDeclContextLexicalBlock for details.
      if (Writer.isGeneratingReducedBMI() &&
          !DeclForLocalLookup->isFromExplicitGlobalModule() &&
          IsInternalDeclFromFileContext(DeclForLocalLookup))
        continue;

      DeclIDs.push_back(Writer.GetDeclRef(DeclForLocalLookup));
    }
    return std::make_pair(Start, DeclIDs.size());
  }

  data_type ImportData(const reader::ASTDeclContextNameLookupTrait::data_type &FromReader) {
    unsigned Start = DeclIDs.size();
    DeclIDs.insert(
        DeclIDs.end(),
        DeclIDIterator<GlobalDeclID, LocalDeclID>(FromReader.begin()),
        DeclIDIterator<GlobalDeclID, LocalDeclID>(FromReader.end()));
    return std::make_pair(Start, DeclIDs.size());
  }

  static bool EqualKey(key_type_ref a, key_type_ref b) {
    return a == b;
  }

  hash_value_type ComputeHash(DeclarationNameKey Name) {
    return Name.getHash();
  }

  void EmitFileRef(raw_ostream &Out, ModuleFile *F) const {
    assert(Writer.hasChain() &&
           "have reference to loaded module file but no chain?");

    using namespace llvm::support;

    endian::write<uint32_t>(Out, Writer.getChain()->getModuleFileID(F),
                            llvm::endianness::little);
  }

  std::pair<unsigned, unsigned> EmitKeyDataLength(raw_ostream &Out,
                                                  DeclarationNameKey Name,
                                                  data_type_ref Lookup) {
    unsigned KeyLen = 1;
    switch (Name.getKind()) {
    case DeclarationName::Identifier:
    case DeclarationName::CXXLiteralOperatorName:
    case DeclarationName::CXXDeductionGuideName:
      KeyLen += sizeof(IdentifierID);
      break;
    case DeclarationName::ObjCZeroArgSelector:
    case DeclarationName::ObjCOneArgSelector:
    case DeclarationName::ObjCMultiArgSelector:
      KeyLen += 4;
      break;
    case DeclarationName::CXXOperatorName:
      KeyLen += 1;
      break;
    case DeclarationName::CXXConstructorName:
    case DeclarationName::CXXDestructorName:
    case DeclarationName::CXXConversionFunctionName:
    case DeclarationName::CXXUsingDirective:
      break;
    }

    // length of DeclIDs.
    unsigned DataLen = sizeof(DeclID) * (Lookup.second - Lookup.first);

    return emitULEBKeyDataLength(KeyLen, DataLen, Out);
  }

  void EmitKey(raw_ostream &Out, DeclarationNameKey Name, unsigned) {
    using namespace llvm::support;

    endian::Writer LE(Out, llvm::endianness::little);
    LE.write<uint8_t>(Name.getKind());
    switch (Name.getKind()) {
    case DeclarationName::Identifier:
    case DeclarationName::CXXLiteralOperatorName:
    case DeclarationName::CXXDeductionGuideName:
      LE.write<IdentifierID>(Writer.getIdentifierRef(Name.getIdentifier()));
      return;
    case DeclarationName::ObjCZeroArgSelector:
    case DeclarationName::ObjCOneArgSelector:
    case DeclarationName::ObjCMultiArgSelector:
      LE.write<uint32_t>(Writer.getSelectorRef(Name.getSelector()));
      return;
    case DeclarationName::CXXOperatorName:
      assert(Name.getOperatorKind() < NUM_OVERLOADED_OPERATORS &&
             "Invalid operator?");
      LE.write<uint8_t>(Name.getOperatorKind());
      return;
    case DeclarationName::CXXConstructorName:
    case DeclarationName::CXXDestructorName:
    case DeclarationName::CXXConversionFunctionName:
    case DeclarationName::CXXUsingDirective:
      return;
    }

    llvm_unreachable("Invalid name kind?");
  }

  void EmitData(raw_ostream &Out, key_type_ref, data_type Lookup,
                unsigned DataLen) {
    using namespace llvm::support;

    endian::Writer LE(Out, llvm::endianness::little);
    uint64_t Start = Out.tell(); (void)Start;
    for (unsigned I = Lookup.first, N = Lookup.second; I != N; ++I)
      LE.write<DeclID>((DeclID)DeclIDs[I]);
    assert(Out.tell() - Start == DataLen && "Data length is wrong");
  }
};

} // namespace

bool ASTWriter::isLookupResultExternal(StoredDeclsList &Result,
                                       DeclContext *DC) {
  return Result.hasExternalDecls() &&
         DC->hasNeedToReconcileExternalVisibleStorage();
}

/// Returns ture if all of the lookup result are either external, not emitted or
/// predefined. In such cases, the lookup result is not interesting and we don't
/// need to record the result in the current being written module. Return false
/// otherwise.
static bool isLookupResultNotInteresting(ASTWriter &Writer,
                                         StoredDeclsList &Result) {
  for (auto *D : Result.getLookupResult()) {
    auto *LocalD = getDeclForLocalLookup(Writer.getLangOpts(), D);
    if (LocalD->isFromASTFile())
      continue;

    // We can only be sure whether the local declaration is reachable
    // after we done writing the declarations and types.
    if (Writer.getDoneWritingDeclsAndTypes() && !Writer.wasDeclEmitted(LocalD))
      continue;

    // We don't need to emit the predefined decls.
    if (Writer.isDeclPredefined(LocalD))
      continue;

    return false;
  }

  return true;
}

void
ASTWriter::GenerateNameLookupTable(const DeclContext *ConstDC,
                                   llvm::SmallVectorImpl<char> &LookupTable) {
  assert(!ConstDC->hasLazyLocalLexicalLookups() &&
         !ConstDC->hasLazyExternalLexicalLookups() &&
         "must call buildLookups first");

  // FIXME: We need to build the lookups table, which is logically const.
  auto *DC = const_cast<DeclContext*>(ConstDC);
  assert(DC == DC->getPrimaryContext() && "only primary DC has lookup table");

  // Create the on-disk hash table representation.
  MultiOnDiskHashTableGenerator<reader::ASTDeclContextNameLookupTrait,
                                ASTDeclContextNameLookupTrait> Generator;
  ASTDeclContextNameLookupTrait Trait(*this);

  // The first step is to collect the declaration names which we need to
  // serialize into the name lookup table, and to collect them in a stable
  // order.
  SmallVector<DeclarationName, 16> Names;

  // We also build up small sets of the constructor and conversion function
  // names which are visible.
  llvm::SmallPtrSet<DeclarationName, 8> ConstructorNameSet, ConversionNameSet;

  for (auto &Lookup : *DC->buildLookup()) {
    auto &Name = Lookup.first;
    auto &Result = Lookup.second;

    // If there are no local declarations in our lookup result, we
    // don't need to write an entry for the name at all. If we can't
    // write out a lookup set without performing more deserialization,
    // just skip this entry.
    //
    // Also in reduced BMI, we'd like to avoid writing unreachable
    // declarations in GMF, so we need to avoid writing declarations
    // that entirely external or unreachable.
    //
    // FIMXE: It looks sufficient to test
    // isLookupResultNotInteresting here. But due to bug we have
    // to test isLookupResultExternal here. See
    // https://github.com/llvm/llvm-project/issues/61065 for details.
    if ((GeneratingReducedBMI || isLookupResultExternal(Result, DC)) &&
        isLookupResultNotInteresting(*this, Result))
      continue;

    // We also skip empty results. If any of the results could be external and
    // the currently available results are empty, then all of the results are
    // external and we skip it above. So the only way we get here with an empty
    // results is when no results could have been external *and* we have
    // external results.
    //
    // FIXME: While we might want to start emitting on-disk entries for negative
    // lookups into a decl context as an optimization, today we *have* to skip
    // them because there are names with empty lookup results in decl contexts
    // which we can't emit in any stable ordering: we lookup constructors and
    // conversion functions in the enclosing namespace scope creating empty
    // results for them. This in almost certainly a bug in Clang's name lookup,
    // but that is likely to be hard or impossible to fix and so we tolerate it
    // here by omitting lookups with empty results.
    if (Lookup.second.getLookupResult().empty())
      continue;

    switch (Lookup.first.getNameKind()) {
    default:
      Names.push_back(Lookup.first);
      break;

    case DeclarationName::CXXConstructorName:
      assert(isa<CXXRecordDecl>(DC) &&
             "Cannot have a constructor name outside of a class!");
      ConstructorNameSet.insert(Name);
      break;

    case DeclarationName::CXXConversionFunctionName:
      assert(isa<CXXRecordDecl>(DC) &&
             "Cannot have a conversion function name outside of a class!");
      ConversionNameSet.insert(Name);
      break;
    }
  }

  // Sort the names into a stable order.
  llvm::sort(Names);

  if (auto *D = dyn_cast<CXXRecordDecl>(DC)) {
    // We need to establish an ordering of constructor and conversion function
    // names, and they don't have an intrinsic ordering.

    // First we try the easy case by forming the current context's constructor
    // name and adding that name first. This is a very useful optimization to
    // avoid walking the lexical declarations in many cases, and it also
    // handles the only case where a constructor name can come from some other
    // lexical context -- when that name is an implicit constructor merged from
    // another declaration in the redecl chain. Any non-implicit constructor or
    // conversion function which doesn't occur in all the lexical contexts
    // would be an ODR violation.
    auto ImplicitCtorName = Context->DeclarationNames.getCXXConstructorName(
        Context->getCanonicalType(Context->getRecordType(D)));
    if (ConstructorNameSet.erase(ImplicitCtorName))
      Names.push_back(ImplicitCtorName);

    // If we still have constructors or conversion functions, we walk all the
    // names in the decl and add the constructors and conversion functions
    // which are visible in the order they lexically occur within the context.
    if (!ConstructorNameSet.empty() || !ConversionNameSet.empty())
      for (Decl *ChildD : cast<CXXRecordDecl>(DC)->decls())
        if (auto *ChildND = dyn_cast<NamedDecl>(ChildD)) {
          auto Name = ChildND->getDeclName();
          switch (Name.getNameKind()) {
          default:
            continue;

          case DeclarationName::CXXConstructorName:
            if (ConstructorNameSet.erase(Name))
              Names.push_back(Name);
            break;

          case DeclarationName::CXXConversionFunctionName:
            if (ConversionNameSet.erase(Name))
              Names.push_back(Name);
            break;
          }

          if (ConstructorNameSet.empty() && ConversionNameSet.empty())
            break;
        }

    assert(ConstructorNameSet.empty() && "Failed to find all of the visible "
                                         "constructors by walking all the "
                                         "lexical members of the context.");
    assert(ConversionNameSet.empty() && "Failed to find all of the visible "
                                        "conversion functions by walking all "
                                        "the lexical members of the context.");
  }

  // Next we need to do a lookup with each name into this decl context to fully
  // populate any results from external sources. We don't actually use the
  // results of these lookups because we only want to use the results after all
  // results have been loaded and the pointers into them will be stable.
  for (auto &Name : Names)
    DC->lookup(Name);

  // Now we need to insert the results for each name into the hash table. For
  // constructor names and conversion function names, we actually need to merge
  // all of the results for them into one list of results each and insert
  // those.
  SmallVector<NamedDecl *, 8> ConstructorDecls;
  SmallVector<NamedDecl *, 8> ConversionDecls;

  // Now loop over the names, either inserting them or appending for the two
  // special cases.
  for (auto &Name : Names) {
    DeclContext::lookup_result Result = DC->noload_lookup(Name);

    switch (Name.getNameKind()) {
    default:
      Generator.insert(Name, Trait.getData(Result), Trait);
      break;

    case DeclarationName::CXXConstructorName:
      ConstructorDecls.append(Result.begin(), Result.end());
      break;

    case DeclarationName::CXXConversionFunctionName:
      ConversionDecls.append(Result.begin(), Result.end());
      break;
    }
  }

  // Handle our two special cases if we ended up having any. We arbitrarily use
  // the first declaration's name here because the name itself isn't part of
  // the key, only the kind of name is used.
  if (!ConstructorDecls.empty())
    Generator.insert(ConstructorDecls.front()->getDeclName(),
                     Trait.getData(ConstructorDecls), Trait);
  if (!ConversionDecls.empty())
    Generator.insert(ConversionDecls.front()->getDeclName(),
                     Trait.getData(ConversionDecls), Trait);

  // Create the on-disk hash table. Also emit the existing imported and
  // merged table if there is one.
  auto *Lookups = Chain ? Chain->getLoadedLookupTables(DC) : nullptr;
  Generator.emit(LookupTable, Trait, Lookups ? &Lookups->Table : nullptr);
}

/// Write the block containing all of the declaration IDs
/// visible from the given DeclContext.
///
/// \returns the offset of the DECL_CONTEXT_VISIBLE block within the
/// bitstream, or 0 if no block was written.
uint64_t ASTWriter::WriteDeclContextVisibleBlock(ASTContext &Context,
                                                 DeclContext *DC) {
  // If we imported a key declaration of this namespace, write the visible
  // lookup results as an update record for it rather than including them
  // on this declaration. We will only look at key declarations on reload.
  if (isa<NamespaceDecl>(DC) && Chain &&
      Chain->getKeyDeclaration(cast<Decl>(DC))->isFromASTFile()) {
    // Only do this once, for the first local declaration of the namespace.
    for (auto *Prev = cast<NamespaceDecl>(DC)->getPreviousDecl(); Prev;
         Prev = Prev->getPreviousDecl())
      if (!Prev->isFromASTFile())
        return 0;

    // Note that we need to emit an update record for the primary context.
    UpdatedDeclContexts.insert(DC->getPrimaryContext());

    // Make sure all visible decls are written. They will be recorded later. We
    // do this using a side data structure so we can sort the names into
    // a deterministic order.
    StoredDeclsMap *Map = DC->getPrimaryContext()->buildLookup();
    SmallVector<std::pair<DeclarationName, DeclContext::lookup_result>, 16>
        LookupResults;
    if (Map) {
      LookupResults.reserve(Map->size());
      for (auto &Entry : *Map)
        LookupResults.push_back(
            std::make_pair(Entry.first, Entry.second.getLookupResult()));
    }

    llvm::sort(LookupResults, llvm::less_first());
    for (auto &NameAndResult : LookupResults) {
      DeclarationName Name = NameAndResult.first;
      DeclContext::lookup_result Result = NameAndResult.second;
      if (Name.getNameKind() == DeclarationName::CXXConstructorName ||
          Name.getNameKind() == DeclarationName::CXXConversionFunctionName) {
        // We have to work around a name lookup bug here where negative lookup
        // results for these names get cached in namespace lookup tables (these
        // names should never be looked up in a namespace).
        assert(Result.empty() && "Cannot have a constructor or conversion "
                                 "function name in a namespace!");
        continue;
      }

      for (NamedDecl *ND : Result) {
        if (ND->isFromASTFile())
          continue;

        if (DoneWritingDeclsAndTypes && !wasDeclEmitted(ND))
          continue;

        // We don't need to force emitting internal decls into reduced BMI.
        // See comments in ASTWriter::WriteDeclContextLexicalBlock for details.
        if (GeneratingReducedBMI && !ND->isFromExplicitGlobalModule() &&
            IsInternalDeclFromFileContext(ND))
          continue;

        GetDeclRef(ND);
      }
    }

    return 0;
  }

  if (DC->getPrimaryContext() != DC)
    return 0;

  // Skip contexts which don't support name lookup.
  if (!DC->isLookupContext())
    return 0;

  // If not in C++, we perform name lookup for the translation unit via the
  // IdentifierInfo chains, don't bother to build a visible-declarations table.
  if (DC->isTranslationUnit() && !Context.getLangOpts().CPlusPlus)
    return 0;

  // Serialize the contents of the mapping used for lookup. Note that,
  // although we have two very different code paths, the serialized
  // representation is the same for both cases: a declaration name,
  // followed by a size, followed by references to the visible
  // declarations that have that name.
  uint64_t Offset = Stream.GetCurrentBitNo();
  StoredDeclsMap *Map = DC->buildLookup();
  if (!Map || Map->empty())
    return 0;

  // Create the on-disk hash table in a buffer.
  SmallString<4096> LookupTable;
  GenerateNameLookupTable(DC, LookupTable);

  // Write the lookup table
  RecordData::value_type Record[] = {DECL_CONTEXT_VISIBLE};
  Stream.EmitRecordWithBlob(DeclContextVisibleLookupAbbrev, Record,
                            LookupTable);
  ++NumVisibleDeclContexts;
  return Offset;
}

/// Write an UPDATE_VISIBLE block for the given context.
///
/// UPDATE_VISIBLE blocks contain the declarations that are added to an existing
/// DeclContext in a dependent AST file. As such, they only exist for the TU
/// (in C++), for namespaces, and for classes with forward-declared unscoped
/// enumeration members (in C++11).
void ASTWriter::WriteDeclContextVisibleUpdate(const DeclContext *DC) {
  StoredDeclsMap *Map = DC->getLookupPtr();
  if (!Map || Map->empty())
    return;

  // Create the on-disk hash table in a buffer.
  SmallString<4096> LookupTable;
  GenerateNameLookupTable(DC, LookupTable);

  // If we're updating a namespace, select a key declaration as the key for the
  // update record; those are the only ones that will be checked on reload.
  if (isa<NamespaceDecl>(DC))
    DC = cast<DeclContext>(Chain->getKeyDeclaration(cast<Decl>(DC)));

  // Write the lookup table
  RecordData::value_type Record[] = {UPDATE_VISIBLE,
                                     getDeclID(cast<Decl>(DC)).getRawValue()};
  Stream.EmitRecordWithBlob(UpdateVisibleAbbrev, Record, LookupTable);
}

/// Write an FP_PRAGMA_OPTIONS block for the given FPOptions.
void ASTWriter::WriteFPPragmaOptions(const FPOptionsOverride &Opts) {
  RecordData::value_type Record[] = {Opts.getAsOpaqueInt()};
  Stream.EmitRecord(FP_PRAGMA_OPTIONS, Record);
}

/// Write an OPENCL_EXTENSIONS block for the given OpenCLOptions.
void ASTWriter::WriteOpenCLExtensions(Sema &SemaRef) {
  if (!SemaRef.Context.getLangOpts().OpenCL)
    return;

  const OpenCLOptions &Opts = SemaRef.getOpenCLOptions();
  RecordData Record;
  for (const auto &I:Opts.OptMap) {
    AddString(I.getKey(), Record);
    auto V = I.getValue();
    Record.push_back(V.Supported ? 1 : 0);
    Record.push_back(V.Enabled ? 1 : 0);
    Record.push_back(V.WithPragma ? 1 : 0);
    Record.push_back(V.Avail);
    Record.push_back(V.Core);
    Record.push_back(V.Opt);
  }
  Stream.EmitRecord(OPENCL_EXTENSIONS, Record);
}
void ASTWriter::WriteCUDAPragmas(Sema &SemaRef) {
  if (SemaRef.CUDA().ForceHostDeviceDepth > 0) {
    RecordData::value_type Record[] = {SemaRef.CUDA().ForceHostDeviceDepth};
    Stream.EmitRecord(CUDA_PRAGMA_FORCE_HOST_DEVICE_DEPTH, Record);
  }
}

void ASTWriter::WriteObjCCategories() {
  SmallVector<ObjCCategoriesInfo, 2> CategoriesMap;
  RecordData Categories;

  for (unsigned I = 0, N = ObjCClassesWithCategories.size(); I != N; ++I) {
    unsigned Size = 0;
    unsigned StartIndex = Categories.size();

    ObjCInterfaceDecl *Class = ObjCClassesWithCategories[I];

    // Allocate space for the size.
    Categories.push_back(0);

    // Add the categories.
    for (ObjCInterfaceDecl::known_categories_iterator
           Cat = Class->known_categories_begin(),
           CatEnd = Class->known_categories_end();
         Cat != CatEnd; ++Cat, ++Size) {
      assert(getDeclID(*Cat).isValid() && "Bogus category");
      AddDeclRef(*Cat, Categories);
    }

    // Update the size.
    Categories[StartIndex] = Size;

    // Record this interface -> category map.
    ObjCCategoriesInfo CatInfo = { getDeclID(Class), StartIndex };
    CategoriesMap.push_back(CatInfo);
  }

  // Sort the categories map by the definition ID, since the reader will be
  // performing binary searches on this information.
  llvm::array_pod_sort(CategoriesMap.begin(), CategoriesMap.end());

  // Emit the categories map.
  using namespace llvm;

  auto Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(OBJC_CATEGORIES_MAP));
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // # of entries
  Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));
  unsigned AbbrevID = Stream.EmitAbbrev(std::move(Abbrev));

  RecordData::value_type Record[] = {OBJC_CATEGORIES_MAP, CategoriesMap.size()};
  Stream.EmitRecordWithBlob(AbbrevID, Record,
                            reinterpret_cast<char *>(CategoriesMap.data()),
                            CategoriesMap.size() * sizeof(ObjCCategoriesInfo));

  // Emit the category lists.
  Stream.EmitRecord(OBJC_CATEGORIES, Categories);
}

void ASTWriter::WriteLateParsedTemplates(Sema &SemaRef) {
  Sema::LateParsedTemplateMapT &LPTMap = SemaRef.LateParsedTemplateMap;

  if (LPTMap.empty())
    return;

  RecordData Record;
  for (auto &LPTMapEntry : LPTMap) {
    const FunctionDecl *FD = LPTMapEntry.first;
    LateParsedTemplate &LPT = *LPTMapEntry.second;
    AddDeclRef(FD, Record);
    AddDeclRef(LPT.D, Record);
    Record.push_back(LPT.FPO.getAsOpaqueInt());
    Record.push_back(LPT.Toks.size());

    for (const auto &Tok : LPT.Toks) {
      AddToken(Tok, Record);
    }
  }
  Stream.EmitRecord(LATE_PARSED_TEMPLATE, Record);
}

/// Write the state of 'pragma clang optimize' at the end of the module.
void ASTWriter::WriteOptimizePragmaOptions(Sema &SemaRef) {
  RecordData Record;
  SourceLocation PragmaLoc = SemaRef.getOptimizeOffPragmaLocation();
  AddSourceLocation(PragmaLoc, Record);
  Stream.EmitRecord(OPTIMIZE_PRAGMA_OPTIONS, Record);
}

/// Write the state of 'pragma ms_struct' at the end of the module.
void ASTWriter::WriteMSStructPragmaOptions(Sema &SemaRef) {
  RecordData Record;
  Record.push_back(SemaRef.MSStructPragmaOn ? PMSST_ON : PMSST_OFF);
  Stream.EmitRecord(MSSTRUCT_PRAGMA_OPTIONS, Record);
}

/// Write the state of 'pragma pointers_to_members' at the end of the
//module.
void ASTWriter::WriteMSPointersToMembersPragmaOptions(Sema &SemaRef) {
  RecordData Record;
  Record.push_back(SemaRef.MSPointerToMemberRepresentationMethod);
  AddSourceLocation(SemaRef.ImplicitMSInheritanceAttrLoc, Record);
  Stream.EmitRecord(POINTERS_TO_MEMBERS_PRAGMA_OPTIONS, Record);
}

/// Write the state of 'pragma align/pack' at the end of the module.
void ASTWriter::WritePackPragmaOptions(Sema &SemaRef) {
  // Don't serialize pragma align/pack state for modules, since it should only
  // take effect on a per-submodule basis.
  if (WritingModule)
    return;

  RecordData Record;
  AddAlignPackInfo(SemaRef.AlignPackStack.CurrentValue, Record);
  AddSourceLocation(SemaRef.AlignPackStack.CurrentPragmaLocation, Record);
  Record.push_back(SemaRef.AlignPackStack.Stack.size());
  for (const auto &StackEntry : SemaRef.AlignPackStack.Stack) {
    AddAlignPackInfo(StackEntry.Value, Record);
    AddSourceLocation(StackEntry.PragmaLocation, Record);
    AddSourceLocation(StackEntry.PragmaPushLocation, Record);
    AddString(StackEntry.StackSlotLabel, Record);
  }
  Stream.EmitRecord(ALIGN_PACK_PRAGMA_OPTIONS, Record);
}

/// Write the state of 'pragma float_control' at the end of the module.
void ASTWriter::WriteFloatControlPragmaOptions(Sema &SemaRef) {
  // Don't serialize pragma float_control state for modules,
  // since it should only take effect on a per-submodule basis.
  if (WritingModule)
    return;

  RecordData Record;
  Record.push_back(SemaRef.FpPragmaStack.CurrentValue.getAsOpaqueInt());
  AddSourceLocation(SemaRef.FpPragmaStack.CurrentPragmaLocation, Record);
  Record.push_back(SemaRef.FpPragmaStack.Stack.size());
  for (const auto &StackEntry : SemaRef.FpPragmaStack.Stack) {
    Record.push_back(StackEntry.Value.getAsOpaqueInt());
    AddSourceLocation(StackEntry.PragmaLocation, Record);
    AddSourceLocation(StackEntry.PragmaPushLocation, Record);
    AddString(StackEntry.StackSlotLabel, Record);
  }
  Stream.EmitRecord(FLOAT_CONTROL_PRAGMA_OPTIONS, Record);
}

void ASTWriter::WriteModuleFileExtension(Sema &SemaRef,
                                         ModuleFileExtensionWriter &Writer) {
  // Enter the extension block.
  Stream.EnterSubblock(EXTENSION_BLOCK_ID, 4);

  // Emit the metadata record abbreviation.
  auto Abv = std::make_shared<llvm::BitCodeAbbrev>();
  Abv->Add(llvm::BitCodeAbbrevOp(EXTENSION_METADATA));
  Abv->Add(llvm::BitCodeAbbrevOp(llvm::BitCodeAbbrevOp::VBR, 6));
  Abv->Add(llvm::BitCodeAbbrevOp(llvm::BitCodeAbbrevOp::VBR, 6));
  Abv->Add(llvm::BitCodeAbbrevOp(llvm::BitCodeAbbrevOp::VBR, 6));
  Abv->Add(llvm::BitCodeAbbrevOp(llvm::BitCodeAbbrevOp::VBR, 6));
  Abv->Add(llvm::BitCodeAbbrevOp(llvm::BitCodeAbbrevOp::Blob));
  unsigned Abbrev = Stream.EmitAbbrev(std::move(Abv));

  // Emit the metadata record.
  RecordData Record;
  auto Metadata = Writer.getExtension()->getExtensionMetadata();
  Record.push_back(EXTENSION_METADATA);
  Record.push_back(Metadata.MajorVersion);
  Record.push_back(Metadata.MinorVersion);
  Record.push_back(Metadata.BlockName.size());
  Record.push_back(Metadata.UserInfo.size());
  SmallString<64> Buffer;
  Buffer += Metadata.BlockName;
  Buffer += Metadata.UserInfo;
  Stream.EmitRecordWithBlob(Abbrev, Record, Buffer);

  // Emit the contents of the extension block.
  Writer.writeExtensionContents(SemaRef, Stream);

  // Exit the extension block.
  Stream.ExitBlock();
}

//===----------------------------------------------------------------------===//
// General Serialization Routines
//===----------------------------------------------------------------------===//

void ASTRecordWriter::AddAttr(const Attr *A) {
  auto &Record = *this;
  // FIXME: Clang can't handle the serialization/deserialization of
  // preferred_name properly now. See
  // https://github.com/llvm/llvm-project/issues/56490 for example.
  if (!A || (isa<PreferredNameAttr>(A) &&
             Writer->isWritingStdCXXNamedModules()))
    return Record.push_back(0);

  Record.push_back(A->getKind() + 1); // FIXME: stable encoding, target attrs

  Record.AddIdentifierRef(A->getAttrName());
  Record.AddIdentifierRef(A->getScopeName());
  Record.AddSourceRange(A->getRange());
  Record.AddSourceLocation(A->getScopeLoc());
  Record.push_back(A->getParsedKind());
  Record.push_back(A->getSyntax());
  Record.push_back(A->getAttributeSpellingListIndexRaw());
  Record.push_back(A->isRegularKeywordAttribute());

#include "clang/Serialization/AttrPCHWrite.inc"
}

/// Emit the list of attributes to the specified record.
void ASTRecordWriter::AddAttributes(ArrayRef<const Attr *> Attrs) {
  push_back(Attrs.size());
  for (const auto *A : Attrs)
    AddAttr(A);
}

void ASTWriter::AddToken(const Token &Tok, RecordDataImpl &Record) {
  AddSourceLocation(Tok.getLocation(), Record);
  // FIXME: Should translate token kind to a stable encoding.
  Record.push_back(Tok.getKind());
  // FIXME: Should translate token flags to a stable encoding.
  Record.push_back(Tok.getFlags());

  if (Tok.isAnnotation()) {
    AddSourceLocation(Tok.getAnnotationEndLoc(), Record);
    switch (Tok.getKind()) {
    case tok::annot_pragma_loop_hint: {
      auto *Info = static_cast<PragmaLoopHintInfo *>(Tok.getAnnotationValue());
      AddToken(Info->PragmaName, Record);
      AddToken(Info->Option, Record);
      Record.push_back(Info->Toks.size());
      for (const auto &T : Info->Toks)
        AddToken(T, Record);
      break;
    }
    case tok::annot_pragma_pack: {
      auto *Info =
          static_cast<Sema::PragmaPackInfo *>(Tok.getAnnotationValue());
      Record.push_back(static_cast<unsigned>(Info->Action));
      AddString(Info->SlotLabel, Record);
      AddToken(Info->Alignment, Record);
      break;
    }
    // Some annotation tokens do not use the PtrData field.
    case tok::annot_pragma_openmp:
    case tok::annot_pragma_openmp_end:
    case tok::annot_pragma_unused:
    case tok::annot_pragma_openacc:
    case tok::annot_pragma_openacc_end:
      break;
    default:
      llvm_unreachable("missing serialization code for annotation token");
    }
  } else {
    Record.push_back(Tok.getLength());
    // FIXME: When reading literal tokens, reconstruct the literal pointer if it
    // is needed.
    AddIdentifierRef(Tok.getIdentifierInfo(), Record);
  }
}

void ASTWriter::AddString(StringRef Str, RecordDataImpl &Record) {
  Record.push_back(Str.size());
  Record.insert(Record.end(), Str.begin(), Str.end());
}

bool ASTWriter::PreparePathForOutput(SmallVectorImpl<char> &Path) {
  assert(Context && "should have context when outputting path");

  // Leave special file names as they are.
  StringRef PathStr(Path.data(), Path.size());
  if (PathStr == "<built-in>" || PathStr == "<command line>")
    return false;

  bool Changed =
      cleanPathForOutput(Context->getSourceManager().getFileManager(), Path);

  // Remove a prefix to make the path relative, if relevant.
  const char *PathBegin = Path.data();
  const char *PathPtr =
      adjustFilenameForRelocatableAST(PathBegin, BaseDirectory);
  if (PathPtr != PathBegin) {
    Path.erase(Path.begin(), Path.begin() + (PathPtr - PathBegin));
    Changed = true;
  }

  return Changed;
}

void ASTWriter::AddPath(StringRef Path, RecordDataImpl &Record) {
  SmallString<128> FilePath(Path);
  PreparePathForOutput(FilePath);
  AddString(FilePath, Record);
}

void ASTWriter::EmitRecordWithPath(unsigned Abbrev, RecordDataRef Record,
                                   StringRef Path) {
  SmallString<128> FilePath(Path);
  PreparePathForOutput(FilePath);
  Stream.EmitRecordWithBlob(Abbrev, Record, FilePath);
}

void ASTWriter::AddVersionTuple(const VersionTuple &Version,
                                RecordDataImpl &Record) {
  Record.push_back(Version.getMajor());
  if (std::optional<unsigned> Minor = Version.getMinor())
    Record.push_back(*Minor + 1);
  else
    Record.push_back(0);
  if (std::optional<unsigned> Subminor = Version.getSubminor())
    Record.push_back(*Subminor + 1);
  else
    Record.push_back(0);
}

/// Note that the identifier II occurs at the given offset
/// within the identifier table.
void ASTWriter::SetIdentifierOffset(const IdentifierInfo *II, uint32_t Offset) {
  IdentifierID ID = IdentifierIDs[II];
  // Only store offsets new to this AST file. Other identifier names are looked
  // up earlier in the chain and thus don't need an offset.
  if (!isLocalIdentifierID(ID))
    return;

  // For local identifiers, the module file index must be 0.

  assert(ID != 0);
  ID -= NUM_PREDEF_IDENT_IDS;
  assert(ID < IdentifierOffsets.size());
  IdentifierOffsets[ID] = Offset;
}

/// Note that the selector Sel occurs at the given offset
/// within the method pool/selector table.
void ASTWriter::SetSelectorOffset(Selector Sel, uint32_t Offset) {
  unsigned ID = SelectorIDs[Sel];
  assert(ID && "Unknown selector");
  // Don't record offsets for selectors that are also available in a different
  // file.
  if (ID < FirstSelectorID)
    return;
  SelectorOffsets[ID - FirstSelectorID] = Offset;
}

ASTWriter::ASTWriter(llvm::BitstreamWriter &Stream,
                     SmallVectorImpl<char> &Buffer,
                     InMemoryModuleCache &ModuleCache,
                     ArrayRef<std::shared_ptr<ModuleFileExtension>> Extensions,
                     bool IncludeTimestamps, bool BuildingImplicitModule,
                     bool GeneratingReducedBMI)
    : Stream(Stream), Buffer(Buffer), ModuleCache(ModuleCache),
      IncludeTimestamps(IncludeTimestamps),
      BuildingImplicitModule(BuildingImplicitModule),
      GeneratingReducedBMI(GeneratingReducedBMI) {
  for (const auto &Ext : Extensions) {
    if (auto Writer = Ext->createExtensionWriter(*this))
      ModuleFileExtensionWriters.push_back(std::move(Writer));
  }
}

ASTWriter::~ASTWriter() = default;

const LangOptions &ASTWriter::getLangOpts() const {
  assert(WritingAST && "can't determine lang opts when not writing AST");
  return Context->getLangOpts();
}

time_t ASTWriter::getTimestampForOutput(const FileEntry *E) const {
  return IncludeTimestamps ? E->getModificationTime() : 0;
}

ASTFileSignature ASTWriter::WriteAST(Sema &SemaRef, StringRef OutputFile,
                                     Module *WritingModule, StringRef isysroot,
                                     bool ShouldCacheASTInMemory) {
  llvm::TimeTraceScope scope("WriteAST", OutputFile);
  WritingAST = true;

  ASTHasCompilerErrors =
      SemaRef.PP.getDiagnostics().hasUncompilableErrorOccurred();

  // Emit the file header.
  Stream.Emit((unsigned)'C', 8);
  Stream.Emit((unsigned)'P', 8);
  Stream.Emit((unsigned)'C', 8);
  Stream.Emit((unsigned)'H', 8);

  WriteBlockInfoBlock();

  Context = &SemaRef.Context;
  PP = &SemaRef.PP;
  this->WritingModule = WritingModule;
  ASTFileSignature Signature = WriteASTCore(SemaRef, isysroot, WritingModule);
  Context = nullptr;
  PP = nullptr;
  this->WritingModule = nullptr;
  this->BaseDirectory.clear();

  WritingAST = false;
  if (ShouldCacheASTInMemory) {
    // Construct MemoryBuffer and update buffer manager.
    ModuleCache.addBuiltPCM(OutputFile,
                            llvm::MemoryBuffer::getMemBufferCopy(
                                StringRef(Buffer.begin(), Buffer.size())));
  }
  return Signature;
}

template<typename Vector>
static void AddLazyVectorDecls(ASTWriter &Writer, Vector &Vec) {
  for (typename Vector::iterator I = Vec.begin(nullptr, true), E = Vec.end();
       I != E; ++I) {
    Writer.GetDeclRef(*I);
  }
}

template <typename Vector>
static void AddLazyVectorEmiitedDecls(ASTWriter &Writer, Vector &Vec,
                                      ASTWriter::RecordData &Record) {
  for (typename Vector::iterator I = Vec.begin(nullptr, true), E = Vec.end();
       I != E; ++I) {
    Writer.AddEmittedDeclRef(*I, Record);
  }
}

void ASTWriter::computeNonAffectingInputFiles() {
  SourceManager &SrcMgr = PP->getSourceManager();
  unsigned N = SrcMgr.local_sloc_entry_size();

  IsSLocAffecting.resize(N, true);

  if (!WritingModule)
    return;

  auto AffectingModuleMaps = GetAffectingModuleMaps(*PP, WritingModule);

  unsigned FileIDAdjustment = 0;
  unsigned OffsetAdjustment = 0;

  NonAffectingFileIDAdjustments.reserve(N);
  NonAffectingOffsetAdjustments.reserve(N);

  NonAffectingFileIDAdjustments.push_back(FileIDAdjustment);
  NonAffectingOffsetAdjustments.push_back(OffsetAdjustment);

  for (unsigned I = 1; I != N; ++I) {
    const SrcMgr::SLocEntry *SLoc = &SrcMgr.getLocalSLocEntry(I);
    FileID FID = FileID::get(I);
    assert(&SrcMgr.getSLocEntry(FID) == SLoc);

    if (!SLoc->isFile())
      continue;
    const SrcMgr::FileInfo &File = SLoc->getFile();
    const SrcMgr::ContentCache *Cache = &File.getContentCache();
    if (!Cache->OrigEntry)
      continue;

    // Don't prune anything other than module maps.
    if (!isModuleMap(File.getFileCharacteristic()))
      continue;

    // Don't prune module maps if all are guaranteed to be affecting.
    if (!AffectingModuleMaps)
      continue;

    // Don't prune module maps that are affecting.
    if (llvm::is_contained(*AffectingModuleMaps, *Cache->OrigEntry))
      continue;

    IsSLocAffecting[I] = false;

    FileIDAdjustment += 1;
    // Even empty files take up one element in the offset table.
    OffsetAdjustment += SrcMgr.getFileIDSize(FID) + 1;

    // If the previous file was non-affecting as well, just extend its entry
    // with our information.
    if (!NonAffectingFileIDs.empty() &&
        NonAffectingFileIDs.back().ID == FID.ID - 1) {
      NonAffectingFileIDs.back() = FID;
      NonAffectingRanges.back().setEnd(SrcMgr.getLocForEndOfFile(FID));
      NonAffectingFileIDAdjustments.back() = FileIDAdjustment;
      NonAffectingOffsetAdjustments.back() = OffsetAdjustment;
      continue;
    }

    NonAffectingFileIDs.push_back(FID);
    NonAffectingRanges.emplace_back(SrcMgr.getLocForStartOfFile(FID),
                                    SrcMgr.getLocForEndOfFile(FID));
    NonAffectingFileIDAdjustments.push_back(FileIDAdjustment);
    NonAffectingOffsetAdjustments.push_back(OffsetAdjustment);
  }

  if (!PP->getHeaderSearchInfo().getHeaderSearchOpts().ModulesIncludeVFSUsage)
    return;

  FileManager &FileMgr = PP->getFileManager();
  FileMgr.trackVFSUsage(true);
  // Lookup the paths in the VFS to trigger `-ivfsoverlay` usage tracking.
  for (StringRef Path :
       PP->getHeaderSearchInfo().getHeaderSearchOpts().VFSOverlayFiles)
    FileMgr.getVirtualFileSystem().exists(Path);
  for (unsigned I = 1; I != N; ++I) {
    if (IsSLocAffecting[I]) {
      const SrcMgr::SLocEntry *SLoc = &SrcMgr.getLocalSLocEntry(I);
      if (!SLoc->isFile())
        continue;
      const SrcMgr::FileInfo &File = SLoc->getFile();
      const SrcMgr::ContentCache *Cache = &File.getContentCache();
      if (!Cache->OrigEntry)
        continue;
      FileMgr.getVirtualFileSystem().exists(
          Cache->OrigEntry->getNameAsRequested());
    }
  }
  FileMgr.trackVFSUsage(false);
}

void ASTWriter::PrepareWritingSpecialDecls(Sema &SemaRef) {
  ASTContext &Context = SemaRef.Context;

  bool isModule = WritingModule != nullptr;

  // Set up predefined declaration IDs.
  auto RegisterPredefDecl = [&] (Decl *D, PredefinedDeclIDs ID) {
    if (D) {
      assert(D->isCanonicalDecl() && "predefined decl is not canonical");
      DeclIDs[D] = ID;
      PredefinedDecls.insert(D);
    }
  };
  RegisterPredefDecl(Context.getTranslationUnitDecl(),
                     PREDEF_DECL_TRANSLATION_UNIT_ID);
  RegisterPredefDecl(Context.ObjCIdDecl, PREDEF_DECL_OBJC_ID_ID);
  RegisterPredefDecl(Context.ObjCSelDecl, PREDEF_DECL_OBJC_SEL_ID);
  RegisterPredefDecl(Context.ObjCClassDecl, PREDEF_DECL_OBJC_CLASS_ID);
  RegisterPredefDecl(Context.ObjCProtocolClassDecl,
                     PREDEF_DECL_OBJC_PROTOCOL_ID);
  RegisterPredefDecl(Context.Int128Decl, PREDEF_DECL_INT_128_ID);
  RegisterPredefDecl(Context.UInt128Decl, PREDEF_DECL_UNSIGNED_INT_128_ID);
  RegisterPredefDecl(Context.ObjCInstanceTypeDecl,
                     PREDEF_DECL_OBJC_INSTANCETYPE_ID);
  RegisterPredefDecl(Context.BuiltinVaListDecl, PREDEF_DECL_BUILTIN_VA_LIST_ID);
  RegisterPredefDecl(Context.VaListTagDecl, PREDEF_DECL_VA_LIST_TAG);
  RegisterPredefDecl(Context.BuiltinMSVaListDecl,
                     PREDEF_DECL_BUILTIN_MS_VA_LIST_ID);
  RegisterPredefDecl(Context.MSGuidTagDecl,
                     PREDEF_DECL_BUILTIN_MS_GUID_ID);
  RegisterPredefDecl(Context.ExternCContext, PREDEF_DECL_EXTERN_C_CONTEXT_ID);
  RegisterPredefDecl(Context.MakeIntegerSeqDecl,
                     PREDEF_DECL_MAKE_INTEGER_SEQ_ID);
  RegisterPredefDecl(Context.CFConstantStringTypeDecl,
                     PREDEF_DECL_CF_CONSTANT_STRING_ID);
  RegisterPredefDecl(Context.CFConstantStringTagDecl,
                     PREDEF_DECL_CF_CONSTANT_STRING_TAG_ID);
  RegisterPredefDecl(Context.TypePackElementDecl,
                     PREDEF_DECL_TYPE_PACK_ELEMENT_ID);

  const TranslationUnitDecl *TU = Context.getTranslationUnitDecl();

  // Force all top level declarations to be emitted.
  //
  // We start emitting top level declarations from the module purview to
  // implement the eliding unreachable declaration feature.
  for (const auto *D : TU->noload_decls()) {
    if (D->isFromASTFile())
      continue;

    if (GeneratingReducedBMI) {
      if (D->isFromExplicitGlobalModule())
        continue;

      // Don't force emitting static entities.
      //
      // Technically, all static entities shouldn't be in reduced BMI. The
      // language also specifies that the program exposes TU-local entities
      // is ill-formed. However, in practice, there are a lot of projects
      // uses `static inline` in the headers. So we can't get rid of all
      // static entities in reduced BMI now.
      if (IsInternalDeclFromFileContext(D))
        continue;
    }

    // If we're writing C++ named modules, don't emit declarations which are
    // not from modules by default. They may be built in declarations (be
    // handled above) or implcit declarations (see the implementation of
    // `Sema::Initialize()` for example).
    if (isWritingStdCXXNamedModules() && !D->getOwningModule() &&
        D->isImplicit())
      continue;

    GetDeclRef(D);
  }

  if (GeneratingReducedBMI)
    return;

  // Writing all of the tentative definitions in this file, in
  // TentativeDefinitions order.  Generally, this record will be empty for
  // headers.
  RecordData TentativeDefinitions;
  AddLazyVectorDecls(*this, SemaRef.TentativeDefinitions);

  // Writing all of the file scoped decls in this file.
  if (!isModule)
    AddLazyVectorDecls(*this, SemaRef.UnusedFileScopedDecls);

  // Writing all of the delegating constructors we still need
  // to resolve.
  if (!isModule)
    AddLazyVectorDecls(*this, SemaRef.DelegatingCtorDecls);

  // Writing all of the ext_vector declarations.
  AddLazyVectorDecls(*this, SemaRef.ExtVectorDecls);

  // Writing all of the VTable uses information.
  if (!SemaRef.VTableUses.empty())
    for (unsigned I = 0, N = SemaRef.VTableUses.size(); I != N; ++I)
      GetDeclRef(SemaRef.VTableUses[I].first);

  // Writing all of the UnusedLocalTypedefNameCandidates.
  for (const TypedefNameDecl *TD : SemaRef.UnusedLocalTypedefNameCandidates)
    GetDeclRef(TD);

  // Writing all of pending implicit instantiations.
  for (const auto &I : SemaRef.PendingInstantiations)
    GetDeclRef(I.first);
  assert(SemaRef.PendingLocalImplicitInstantiations.empty() &&
         "There are local ones at end of translation unit!");

  // Writing some declaration references.
  if (SemaRef.StdNamespace || SemaRef.StdBadAlloc || SemaRef.StdAlignValT) {
    GetDeclRef(SemaRef.getStdNamespace());
    GetDeclRef(SemaRef.getStdBadAlloc());
    GetDeclRef(SemaRef.getStdAlignValT());
  }

  if (Context.getcudaConfigureCallDecl())
    GetDeclRef(Context.getcudaConfigureCallDecl());

  // Writing all of the known namespaces.
  for (const auto &I : SemaRef.KnownNamespaces)
    if (!I.second)
      GetDeclRef(I.first);

  // Writing all used, undefined objects that require definitions.
  SmallVector<std::pair<NamedDecl *, SourceLocation>, 16> Undefined;
  SemaRef.getUndefinedButUsed(Undefined);
  for (const auto &I : Undefined)
    GetDeclRef(I.first);

  // Writing all delete-expressions that we would like to
  // analyze later in AST.
  if (!isModule)
    for (const auto &DeleteExprsInfo :
         SemaRef.getMismatchingDeleteExpressions())
      GetDeclRef(DeleteExprsInfo.first);

  // Make sure visible decls, added to DeclContexts previously loaded from
  // an AST file, are registered for serialization. Likewise for template
  // specializations added to imported templates.
  for (const auto *I : DeclsToEmitEvenIfUnreferenced)
    GetDeclRef(I);
  DeclsToEmitEvenIfUnreferenced.clear();

  // Make sure all decls associated with an identifier are registered for
  // serialization, if we're storing decls with identifiers.
  if (!WritingModule || !getLangOpts().CPlusPlus) {
    llvm::SmallVector<const IdentifierInfo*, 256> IIs;
    for (const auto &ID : SemaRef.PP.getIdentifierTable()) {
      const IdentifierInfo *II = ID.second;
      if (!Chain || !II->isFromAST() || II->hasChangedSinceDeserialization())
        IIs.push_back(II);
    }
    // Sort the identifiers to visit based on their name.
    llvm::sort(IIs, llvm::deref<std::less<>>());
    for (const IdentifierInfo *II : IIs)
      for (const Decl *D : SemaRef.IdResolver.decls(II))
        GetDeclRef(D);
  }

  // Write all of the DeclsToCheckForDeferredDiags.
  for (auto *D : SemaRef.DeclsToCheckForDeferredDiags)
    GetDeclRef(D);

  // Write all classes that need to emit the vtable definitions if required.
  if (isWritingStdCXXNamedModules())
    for (CXXRecordDecl *RD : PendingEmittingVTables)
      GetDeclRef(RD);
  else
    PendingEmittingVTables.clear();
}

void ASTWriter::WriteSpecialDeclRecords(Sema &SemaRef) {
  ASTContext &Context = SemaRef.Context;

  bool isModule = WritingModule != nullptr;

  // Write the record containing external, unnamed definitions.
  if (!EagerlyDeserializedDecls.empty())
    Stream.EmitRecord(EAGERLY_DESERIALIZED_DECLS, EagerlyDeserializedDecls);

  if (!ModularCodegenDecls.empty())
    Stream.EmitRecord(MODULAR_CODEGEN_DECLS, ModularCodegenDecls);

  // Write the record containing tentative definitions.
  RecordData TentativeDefinitions;
  AddLazyVectorEmiitedDecls(*this, SemaRef.TentativeDefinitions,
                            TentativeDefinitions);
  if (!TentativeDefinitions.empty())
    Stream.EmitRecord(TENTATIVE_DEFINITIONS, TentativeDefinitions);

  // Write the record containing unused file scoped decls.
  RecordData UnusedFileScopedDecls;
  if (!isModule)
    AddLazyVectorEmiitedDecls(*this, SemaRef.UnusedFileScopedDecls,
                              UnusedFileScopedDecls);
  if (!UnusedFileScopedDecls.empty())
    Stream.EmitRecord(UNUSED_FILESCOPED_DECLS, UnusedFileScopedDecls);

  // Write the record containing ext_vector type names.
  RecordData ExtVectorDecls;
  AddLazyVectorEmiitedDecls(*this, SemaRef.ExtVectorDecls, ExtVectorDecls);
  if (!ExtVectorDecls.empty())
    Stream.EmitRecord(EXT_VECTOR_DECLS, ExtVectorDecls);

  // Write the record containing VTable uses information.
  RecordData VTableUses;
  if (!SemaRef.VTableUses.empty()) {
    for (unsigned I = 0, N = SemaRef.VTableUses.size(); I != N; ++I) {
      CXXRecordDecl *D = SemaRef.VTableUses[I].first;
      if (!wasDeclEmitted(D))
        continue;

      AddDeclRef(D, VTableUses);
      AddSourceLocation(SemaRef.VTableUses[I].second, VTableUses);
      VTableUses.push_back(SemaRef.VTablesUsed[D]);
    }
    Stream.EmitRecord(VTABLE_USES, VTableUses);
  }

  // Write the record containing potentially unused local typedefs.
  RecordData UnusedLocalTypedefNameCandidates;
  for (const TypedefNameDecl *TD : SemaRef.UnusedLocalTypedefNameCandidates)
    AddEmittedDeclRef(TD, UnusedLocalTypedefNameCandidates);
  if (!UnusedLocalTypedefNameCandidates.empty())
    Stream.EmitRecord(UNUSED_LOCAL_TYPEDEF_NAME_CANDIDATES,
                      UnusedLocalTypedefNameCandidates);

  // Write the record containing pending implicit instantiations.
  RecordData PendingInstantiations;
  for (const auto &I : SemaRef.PendingInstantiations) {
    if (!wasDeclEmitted(I.first))
      continue;

    AddDeclRef(I.first, PendingInstantiations);
    AddSourceLocation(I.second, PendingInstantiations);
  }
  if (!PendingInstantiations.empty())
    Stream.EmitRecord(PENDING_IMPLICIT_INSTANTIATIONS, PendingInstantiations);

  // Write the record containing declaration references of Sema.
  RecordData SemaDeclRefs;
  if (SemaRef.StdNamespace || SemaRef.StdBadAlloc || SemaRef.StdAlignValT) {
    auto AddEmittedDeclRefOrZero = [this, &SemaDeclRefs](Decl *D) {
      if (!D || !wasDeclEmitted(D))
        SemaDeclRefs.push_back(0);
      else
        AddDeclRef(D, SemaDeclRefs);
    };

    AddEmittedDeclRefOrZero(SemaRef.getStdNamespace());
    AddEmittedDeclRefOrZero(SemaRef.getStdBadAlloc());
    AddEmittedDeclRefOrZero(SemaRef.getStdAlignValT());
  }
  if (!SemaDeclRefs.empty())
    Stream.EmitRecord(SEMA_DECL_REFS, SemaDeclRefs);

  // Write the record containing decls to be checked for deferred diags.
  RecordData DeclsToCheckForDeferredDiags;
  for (auto *D : SemaRef.DeclsToCheckForDeferredDiags)
    if (wasDeclEmitted(D))
      AddDeclRef(D, DeclsToCheckForDeferredDiags);
  if (!DeclsToCheckForDeferredDiags.empty())
    Stream.EmitRecord(DECLS_TO_CHECK_FOR_DEFERRED_DIAGS,
        DeclsToCheckForDeferredDiags);

  // Write the record containing CUDA-specific declaration references.
  RecordData CUDASpecialDeclRefs;
  if (auto *CudaCallDecl = Context.getcudaConfigureCallDecl();
      CudaCallDecl && wasDeclEmitted(CudaCallDecl)) {
    AddDeclRef(CudaCallDecl, CUDASpecialDeclRefs);
    Stream.EmitRecord(CUDA_SPECIAL_DECL_REFS, CUDASpecialDeclRefs);
  }

  // Write the delegating constructors.
  RecordData DelegatingCtorDecls;
  if (!isModule)
    AddLazyVectorEmiitedDecls(*this, SemaRef.DelegatingCtorDecls,
                              DelegatingCtorDecls);
  if (!DelegatingCtorDecls.empty())
    Stream.EmitRecord(DELEGATING_CTORS, DelegatingCtorDecls);

  // Write the known namespaces.
  RecordData KnownNamespaces;
  for (const auto &I : SemaRef.KnownNamespaces) {
    if (!I.second && wasDeclEmitted(I.first))
      AddDeclRef(I.first, KnownNamespaces);
  }
  if (!KnownNamespaces.empty())
    Stream.EmitRecord(KNOWN_NAMESPACES, KnownNamespaces);

  // Write the undefined internal functions and variables, and inline functions.
  RecordData UndefinedButUsed;
  SmallVector<std::pair<NamedDecl *, SourceLocation>, 16> Undefined;
  SemaRef.getUndefinedButUsed(Undefined);
  for (const auto &I : Undefined) {
    if (!wasDeclEmitted(I.first))
      continue;

    AddDeclRef(I.first, UndefinedButUsed);
    AddSourceLocation(I.second, UndefinedButUsed);
  }
  if (!UndefinedButUsed.empty())
    Stream.EmitRecord(UNDEFINED_BUT_USED, UndefinedButUsed);

  // Write all delete-expressions that we would like to
  // analyze later in AST.
  RecordData DeleteExprsToAnalyze;
  if (!isModule) {
    for (const auto &DeleteExprsInfo :
         SemaRef.getMismatchingDeleteExpressions()) {
      if (!wasDeclEmitted(DeleteExprsInfo.first))
        continue;

      AddDeclRef(DeleteExprsInfo.first, DeleteExprsToAnalyze);
      DeleteExprsToAnalyze.push_back(DeleteExprsInfo.second.size());
      for (const auto &DeleteLoc : DeleteExprsInfo.second) {
        AddSourceLocation(DeleteLoc.first, DeleteExprsToAnalyze);
        DeleteExprsToAnalyze.push_back(DeleteLoc.second);
      }
    }
  }
  if (!DeleteExprsToAnalyze.empty())
    Stream.EmitRecord(DELETE_EXPRS_TO_ANALYZE, DeleteExprsToAnalyze);

  RecordData VTablesToEmit;
  for (CXXRecordDecl *RD : PendingEmittingVTables) {
    if (!wasDeclEmitted(RD))
      continue;

    AddDeclRef(RD, VTablesToEmit);
  }

  if (!VTablesToEmit.empty())
    Stream.EmitRecord(VTABLES_TO_EMIT, VTablesToEmit);
}

ASTFileSignature ASTWriter::WriteASTCore(Sema &SemaRef, StringRef isysroot,
                                         Module *WritingModule) {
  using namespace llvm;

  bool isModule = WritingModule != nullptr;

  // Make sure that the AST reader knows to finalize itself.
  if (Chain)
    Chain->finalizeForWriting();

  ASTContext &Context = SemaRef.Context;
  Preprocessor &PP = SemaRef.PP;

  // This needs to be done very early, since everything that writes
  // SourceLocations or FileIDs depends on it.
  computeNonAffectingInputFiles();

  writeUnhashedControlBlock(PP, Context);

  // Don't reuse type ID and Identifier ID from readers for C++ standard named
  // modules since we want to support no-transitive-change model for named
  // modules. The theory for no-transitive-change model is,
  // for a user of a named module, the user can only access the indirectly
  // imported decls via the directly imported module. So that it is possible to
  // control what matters to the users when writing the module. It would be
  // problematic if the users can reuse the type IDs and identifier IDs from
  // indirectly imported modules arbitrarily. So we choose to clear these ID
  // here.
  if (isWritingStdCXXNamedModules()) {
    TypeIdxs.clear();
    IdentifierIDs.clear();
  }

  // Look for any identifiers that were named while processing the
  // headers, but are otherwise not needed. We add these to the hash
  // table to enable checking of the predefines buffer in the case
  // where the user adds new macro definitions when building the AST
  // file.
  //
  // We do this before emitting any Decl and Types to make sure the
  // Identifier ID is stable.
  SmallVector<const IdentifierInfo *, 128> IIs;
  for (const auto &ID : PP.getIdentifierTable())
    if (IsInterestingNonMacroIdentifier(ID.second, *this))
      IIs.push_back(ID.second);
  // Sort the identifiers lexicographically before getting the references so
  // that their order is stable.
  llvm::sort(IIs, llvm::deref<std::less<>>());
  for (const IdentifierInfo *II : IIs)
    getIdentifierRef(II);

  // Write the set of weak, undeclared identifiers. We always write the
  // entire table, since later PCH files in a PCH chain are only interested in
  // the results at the end of the chain.
  RecordData WeakUndeclaredIdentifiers;
  for (const auto &WeakUndeclaredIdentifierList :
       SemaRef.WeakUndeclaredIdentifiers) {
    const IdentifierInfo *const II = WeakUndeclaredIdentifierList.first;
    for (const auto &WI : WeakUndeclaredIdentifierList.second) {
      AddIdentifierRef(II, WeakUndeclaredIdentifiers);
      AddIdentifierRef(WI.getAlias(), WeakUndeclaredIdentifiers);
      AddSourceLocation(WI.getLocation(), WeakUndeclaredIdentifiers);
    }
  }

  // Form the record of special types.
  RecordData SpecialTypes;
  AddTypeRef(Context.getRawCFConstantStringType(), SpecialTypes);
  AddTypeRef(Context.getFILEType(), SpecialTypes);
  AddTypeRef(Context.getjmp_bufType(), SpecialTypes);
  AddTypeRef(Context.getsigjmp_bufType(), SpecialTypes);
  AddTypeRef(Context.ObjCIdRedefinitionType, SpecialTypes);
  AddTypeRef(Context.ObjCClassRedefinitionType, SpecialTypes);
  AddTypeRef(Context.ObjCSelRedefinitionType, SpecialTypes);
  AddTypeRef(Context.getucontext_tType(), SpecialTypes);

  PrepareWritingSpecialDecls(SemaRef);

  // Write the control block
  WriteControlBlock(PP, Context, isysroot);

  // Write the remaining AST contents.
  Stream.FlushToWord();
  ASTBlockRange.first = Stream.GetCurrentBitNo() >> 3;
  Stream.EnterSubblock(AST_BLOCK_ID, 5);
  ASTBlockStartOffset = Stream.GetCurrentBitNo();

  // This is so that older clang versions, before the introduction
  // of the control block, can read and reject the newer PCH format.
  {
    RecordData Record = {VERSION_MAJOR};
    Stream.EmitRecord(METADATA_OLD_FORMAT, Record);
  }

  // For method pool in the module, if it contains an entry for a selector,
  // the entry should be complete, containing everything introduced by that
  // module and all modules it imports. It's possible that the entry is out of
  // date, so we need to pull in the new content here.

  // It's possible that updateOutOfDateSelector can update SelectorIDs. To be
  // safe, we copy all selectors out.
  llvm::SmallVector<Selector, 256> AllSelectors;
  for (auto &SelectorAndID : SelectorIDs)
    AllSelectors.push_back(SelectorAndID.first);
  for (auto &Selector : AllSelectors)
    SemaRef.ObjC().updateOutOfDateSelector(Selector);

  if (Chain) {
    // Write the mapping information describing our module dependencies and how
    // each of those modules were mapped into our own offset/ID space, so that
    // the reader can build the appropriate mapping to its own offset/ID space.
    // The map consists solely of a blob with the following format:
    // *(module-kind:i8
    //   module-name-len:i16 module-name:len*i8
    //   source-location-offset:i32
    //   identifier-id:i32
    //   preprocessed-entity-id:i32
    //   macro-definition-id:i32
    //   submodule-id:i32
    //   selector-id:i32
    //   declaration-id:i32
    //   c++-base-specifiers-id:i32
    //   type-id:i32)
    //
    // module-kind is the ModuleKind enum value. If it is MK_PrebuiltModule,
    // MK_ExplicitModule or MK_ImplicitModule, then the module-name is the
    // module name. Otherwise, it is the module file name.
    auto Abbrev = std::make_shared<BitCodeAbbrev>();
    Abbrev->Add(BitCodeAbbrevOp(MODULE_OFFSET_MAP));
    Abbrev->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));
    unsigned ModuleOffsetMapAbbrev = Stream.EmitAbbrev(std::move(Abbrev));
    SmallString<2048> Buffer;
    {
      llvm::raw_svector_ostream Out(Buffer);
      for (ModuleFile &M : Chain->ModuleMgr) {
        using namespace llvm::support;

        endian::Writer LE(Out, llvm::endianness::little);
        LE.write<uint8_t>(static_cast<uint8_t>(M.Kind));
        StringRef Name = M.isModule() ? M.ModuleName : M.FileName;
        LE.write<uint16_t>(Name.size());
        Out.write(Name.data(), Name.size());

        // Note: if a base ID was uint max, it would not be possible to load
        // another module after it or have more than one entity inside it.
        uint32_t None = std::numeric_limits<uint32_t>::max();

        auto writeBaseIDOrNone = [&](auto BaseID, bool ShouldWrite) {
          assert(BaseID < std::numeric_limits<uint32_t>::max() && "base id too high");
          if (ShouldWrite)
            LE.write<uint32_t>(BaseID);
          else
            LE.write<uint32_t>(None);
        };

        // These values should be unique within a chain, since they will be read
        // as keys into ContinuousRangeMaps.
        writeBaseIDOrNone(M.BaseMacroID, M.LocalNumMacros);
        writeBaseIDOrNone(M.BasePreprocessedEntityID,
                          M.NumPreprocessedEntities);
        writeBaseIDOrNone(M.BaseSubmoduleID, M.LocalNumSubmodules);
        writeBaseIDOrNone(M.BaseSelectorID, M.LocalNumSelectors);
      }
    }
    RecordData::value_type Record[] = {MODULE_OFFSET_MAP};
    Stream.EmitRecordWithBlob(ModuleOffsetMapAbbrev, Record,
                              Buffer.data(), Buffer.size());
  }

  WriteDeclAndTypes(Context);

  WriteFileDeclIDsMap();
  WriteSourceManagerBlock(Context.getSourceManager(), PP);
  WriteComments();
  WritePreprocessor(PP, isModule);
  WriteHeaderSearch(PP.getHeaderSearchInfo());
  WriteSelectors(SemaRef);
  WriteReferencedSelectorsPool(SemaRef);
  WriteLateParsedTemplates(SemaRef);
  WriteIdentifierTable(PP, SemaRef.IdResolver, isModule);
  WriteFPPragmaOptions(SemaRef.CurFPFeatureOverrides());
  WriteOpenCLExtensions(SemaRef);
  WriteCUDAPragmas(SemaRef);

  // If we're emitting a module, write out the submodule information.
  if (WritingModule)
    WriteSubmodules(WritingModule);

  Stream.EmitRecord(SPECIAL_TYPES, SpecialTypes);

  WriteSpecialDeclRecords(SemaRef);

  // Write the record containing weak undeclared identifiers.
  if (!WeakUndeclaredIdentifiers.empty())
    Stream.EmitRecord(WEAK_UNDECLARED_IDENTIFIERS,
                      WeakUndeclaredIdentifiers);

  if (!WritingModule) {
    // Write the submodules that were imported, if any.
    struct ModuleInfo {
      uint64_t ID;
      Module *M;
      ModuleInfo(uint64_t ID, Module *M) : ID(ID), M(M) {}
    };
    llvm::SmallVector<ModuleInfo, 64> Imports;
    for (const auto *I : Context.local_imports()) {
      assert(SubmoduleIDs.contains(I->getImportedModule()));
      Imports.push_back(ModuleInfo(SubmoduleIDs[I->getImportedModule()],
                         I->getImportedModule()));
    }

    if (!Imports.empty()) {
      auto Cmp = [](const ModuleInfo &A, const ModuleInfo &B) {
        return A.ID < B.ID;
      };
      auto Eq = [](const ModuleInfo &A, const ModuleInfo &B) {
        return A.ID == B.ID;
      };

      // Sort and deduplicate module IDs.
      llvm::sort(Imports, Cmp);
      Imports.erase(std::unique(Imports.begin(), Imports.end(), Eq),
                    Imports.end());

      RecordData ImportedModules;
      for (const auto &Import : Imports) {
        ImportedModules.push_back(Import.ID);
        // FIXME: If the module has macros imported then later has declarations
        // imported, this location won't be the right one as a location for the
        // declaration imports.
        AddSourceLocation(PP.getModuleImportLoc(Import.M), ImportedModules);
      }

      Stream.EmitRecord(IMPORTED_MODULES, ImportedModules);
    }
  }

  WriteObjCCategories();
  if(!WritingModule) {
    WriteOptimizePragmaOptions(SemaRef);
    WriteMSStructPragmaOptions(SemaRef);
    WriteMSPointersToMembersPragmaOptions(SemaRef);
  }
  WritePackPragmaOptions(SemaRef);
  WriteFloatControlPragmaOptions(SemaRef);

  // Some simple statistics
  RecordData::value_type Record[] = {
      NumStatements, NumMacros, NumLexicalDeclContexts, NumVisibleDeclContexts};
  Stream.EmitRecord(STATISTICS, Record);
  Stream.ExitBlock();
  Stream.FlushToWord();
  ASTBlockRange.second = Stream.GetCurrentBitNo() >> 3;

  // Write the module file extension blocks.
  for (const auto &ExtWriter : ModuleFileExtensionWriters)
    WriteModuleFileExtension(SemaRef, *ExtWriter);

  return backpatchSignature();
}

void ASTWriter::EnteringModulePurview() {
  // In C++20 named modules, all entities before entering the module purview
  // lives in the GMF.
  if (GeneratingReducedBMI)
    DeclUpdatesFromGMF.swap(DeclUpdates);
}

// Add update records for all mangling numbers and static local numbers.
// These aren't really update records, but this is a convenient way of
// tagging this rare extra data onto the declarations.
void ASTWriter::AddedManglingNumber(const Decl *D, unsigned Number) {
  if (D->isFromASTFile())
    return;

  DeclUpdates[D].push_back(DeclUpdate(UPD_MANGLING_NUMBER, Number));
}
void ASTWriter::AddedStaticLocalNumbers(const Decl *D, unsigned Number) {
  if (D->isFromASTFile())
    return;

  DeclUpdates[D].push_back(DeclUpdate(UPD_STATIC_LOCAL_NUMBER, Number));
}

void ASTWriter::AddedAnonymousNamespace(const TranslationUnitDecl *TU,
                                        NamespaceDecl *AnonNamespace) {
  // If the translation unit has an anonymous namespace, and we don't already
  // have an update block for it, write it as an update block.
  // FIXME: Why do we not do this if there's already an update block?
  if (NamespaceDecl *NS = TU->getAnonymousNamespace()) {
    ASTWriter::UpdateRecord &Record = DeclUpdates[TU];
    if (Record.empty())
      Record.push_back(DeclUpdate(UPD_CXX_ADDED_ANONYMOUS_NAMESPACE, NS));
  }
}

void ASTWriter::WriteDeclAndTypes(ASTContext &Context) {
  // Keep writing types, declarations, and declaration update records
  // until we've emitted all of them.
  RecordData DeclUpdatesOffsetsRecord;
  Stream.EnterSubblock(DECLTYPES_BLOCK_ID, /*bits for abbreviations*/5);
  DeclTypesBlockStartOffset = Stream.GetCurrentBitNo();
  WriteTypeAbbrevs();
  WriteDeclAbbrevs();
  do {
    WriteDeclUpdatesBlocks(DeclUpdatesOffsetsRecord);
    while (!DeclTypesToEmit.empty()) {
      DeclOrType DOT = DeclTypesToEmit.front();
      DeclTypesToEmit.pop();
      if (DOT.isType())
        WriteType(DOT.getType());
      else
        WriteDecl(Context, DOT.getDecl());
    }
  } while (!DeclUpdates.empty());

  DoneWritingDeclsAndTypes = true;

  // DelayedNamespace is only meaningful in reduced BMI.
  // See the comments of DelayedNamespace for details.
  assert(DelayedNamespace.empty() || GeneratingReducedBMI);
  RecordData DelayedNamespaceRecord;
  for (NamespaceDecl *NS : DelayedNamespace) {
    uint64_t LexicalOffset = WriteDeclContextLexicalBlock(Context, NS);
    uint64_t VisibleOffset = WriteDeclContextVisibleBlock(Context, NS);

    // Write the offset relative to current block.
    if (LexicalOffset)
      LexicalOffset -= DeclTypesBlockStartOffset;

    if (VisibleOffset)
      VisibleOffset -= DeclTypesBlockStartOffset;

    AddDeclRef(NS, DelayedNamespaceRecord);
    DelayedNamespaceRecord.push_back(LexicalOffset);
    DelayedNamespaceRecord.push_back(VisibleOffset);
  }

  // The process of writing lexical and visible block for delayed namespace
  // shouldn't introduce any new decls, types or update to emit.
  assert(DeclTypesToEmit.empty());
  assert(DeclUpdates.empty());

  Stream.ExitBlock();

  // These things can only be done once we've written out decls and types.
  WriteTypeDeclOffsets();
  if (!DeclUpdatesOffsetsRecord.empty())
    Stream.EmitRecord(DECL_UPDATE_OFFSETS, DeclUpdatesOffsetsRecord);

  if (!DelayedNamespaceRecord.empty())
    Stream.EmitRecord(DELAYED_NAMESPACE_LEXICAL_VISIBLE_RECORD,
                      DelayedNamespaceRecord);

  const TranslationUnitDecl *TU = Context.getTranslationUnitDecl();
  // Create a lexical update block containing all of the declarations in the
  // translation unit that do not come from other AST files.
  SmallVector<DeclID, 128> NewGlobalKindDeclPairs;
  for (const auto *D : TU->noload_decls()) {
    if (D->isFromASTFile())
      continue;

    // In reduced BMI, skip unreached declarations.
    if (!wasDeclEmitted(D))
      continue;

    NewGlobalKindDeclPairs.push_back(D->getKind());
    NewGlobalKindDeclPairs.push_back(GetDeclRef(D).getRawValue());
  }

  auto Abv = std::make_shared<llvm::BitCodeAbbrev>();
  Abv->Add(llvm::BitCodeAbbrevOp(TU_UPDATE_LEXICAL));
  Abv->Add(llvm::BitCodeAbbrevOp(llvm::BitCodeAbbrevOp::Blob));
  unsigned TuUpdateLexicalAbbrev = Stream.EmitAbbrev(std::move(Abv));

  RecordData::value_type Record[] = {TU_UPDATE_LEXICAL};
  Stream.EmitRecordWithBlob(TuUpdateLexicalAbbrev, Record,
                            bytes(NewGlobalKindDeclPairs));

  Abv = std::make_shared<llvm::BitCodeAbbrev>();
  Abv->Add(llvm::BitCodeAbbrevOp(UPDATE_VISIBLE));
  Abv->Add(llvm::BitCodeAbbrevOp(llvm::BitCodeAbbrevOp::VBR, 6));
  Abv->Add(llvm::BitCodeAbbrevOp(llvm::BitCodeAbbrevOp::Blob));
  UpdateVisibleAbbrev = Stream.EmitAbbrev(std::move(Abv));

  // And a visible updates block for the translation unit.
  WriteDeclContextVisibleUpdate(TU);

  // If we have any extern "C" names, write out a visible update for them.
  if (Context.ExternCContext)
    WriteDeclContextVisibleUpdate(Context.ExternCContext);

  // Write the visible updates to DeclContexts.
  for (auto *DC : UpdatedDeclContexts)
    WriteDeclContextVisibleUpdate(DC);
}

void ASTWriter::WriteDeclUpdatesBlocks(RecordDataImpl &OffsetsRecord) {
  if (DeclUpdates.empty())
    return;

  DeclUpdateMap LocalUpdates;
  LocalUpdates.swap(DeclUpdates);

  for (auto &DeclUpdate : LocalUpdates) {
    const Decl *D = DeclUpdate.first;

    bool HasUpdatedBody = false;
    bool HasAddedVarDefinition = false;
    RecordData RecordData;
    ASTRecordWriter Record(*this, RecordData);
    for (auto &Update : DeclUpdate.second) {
      DeclUpdateKind Kind = (DeclUpdateKind)Update.getKind();

      // An updated body is emitted last, so that the reader doesn't need
      // to skip over the lazy body to reach statements for other records.
      if (Kind == UPD_CXX_ADDED_FUNCTION_DEFINITION)
        HasUpdatedBody = true;
      else if (Kind == UPD_CXX_ADDED_VAR_DEFINITION)
        HasAddedVarDefinition = true;
      else
        Record.push_back(Kind);

      switch (Kind) {
      case UPD_CXX_ADDED_IMPLICIT_MEMBER:
      case UPD_CXX_ADDED_TEMPLATE_SPECIALIZATION:
      case UPD_CXX_ADDED_ANONYMOUS_NAMESPACE:
        assert(Update.getDecl() && "no decl to add?");
        Record.AddDeclRef(Update.getDecl());
        break;

      case UPD_CXX_ADDED_FUNCTION_DEFINITION:
      case UPD_CXX_ADDED_VAR_DEFINITION:
        break;

      case UPD_CXX_POINT_OF_INSTANTIATION:
        // FIXME: Do we need to also save the template specialization kind here?
        Record.AddSourceLocation(Update.getLoc());
        break;

      case UPD_CXX_INSTANTIATED_DEFAULT_ARGUMENT:
        Record.writeStmtRef(
            cast<ParmVarDecl>(Update.getDecl())->getDefaultArg());
        break;

      case UPD_CXX_INSTANTIATED_DEFAULT_MEMBER_INITIALIZER:
        Record.AddStmt(
            cast<FieldDecl>(Update.getDecl())->getInClassInitializer());
        break;

      case UPD_CXX_INSTANTIATED_CLASS_DEFINITION: {
        auto *RD = cast<CXXRecordDecl>(D);
        UpdatedDeclContexts.insert(RD->getPrimaryContext());
        Record.push_back(RD->isParamDestroyedInCallee());
        Record.push_back(llvm::to_underlying(RD->getArgPassingRestrictions()));
        Record.AddCXXDefinitionData(RD);
        Record.AddOffset(WriteDeclContextLexicalBlock(*Context, RD));

        // This state is sometimes updated by template instantiation, when we
        // switch from the specialization referring to the template declaration
        // to it referring to the template definition.
        if (auto *MSInfo = RD->getMemberSpecializationInfo()) {
          Record.push_back(MSInfo->getTemplateSpecializationKind());
          Record.AddSourceLocation(MSInfo->getPointOfInstantiation());
        } else {
          auto *Spec = cast<ClassTemplateSpecializationDecl>(RD);
          Record.push_back(Spec->getTemplateSpecializationKind());
          Record.AddSourceLocation(Spec->getPointOfInstantiation());

          // The instantiation might have been resolved to a partial
          // specialization. If so, record which one.
          auto From = Spec->getInstantiatedFrom();
          if (auto PartialSpec =
                From.dyn_cast<ClassTemplatePartialSpecializationDecl*>()) {
            Record.push_back(true);
            Record.AddDeclRef(PartialSpec);
            Record.AddTemplateArgumentList(
                &Spec->getTemplateInstantiationArgs());
          } else {
            Record.push_back(false);
          }
        }
        Record.push_back(llvm::to_underlying(RD->getTagKind()));
        Record.AddSourceLocation(RD->getLocation());
        Record.AddSourceLocation(RD->getBeginLoc());
        Record.AddSourceRange(RD->getBraceRange());

        // Instantiation may change attributes; write them all out afresh.
        Record.push_back(D->hasAttrs());
        if (D->hasAttrs())
          Record.AddAttributes(D->getAttrs());

        // FIXME: Ensure we don't get here for explicit instantiations.
        break;
      }

      case UPD_CXX_RESOLVED_DTOR_DELETE:
        Record.AddDeclRef(Update.getDecl());
        Record.AddStmt(cast<CXXDestructorDecl>(D)->getOperatorDeleteThisArg());
        break;

      case UPD_CXX_RESOLVED_EXCEPTION_SPEC: {
        auto prototype =
          cast<FunctionDecl>(D)->getType()->castAs<FunctionProtoType>();
        Record.writeExceptionSpecInfo(prototype->getExceptionSpecInfo());
        break;
      }

      case UPD_CXX_DEDUCED_RETURN_TYPE:
        Record.push_back(GetOrCreateTypeID(Update.getType()));
        break;

      case UPD_DECL_MARKED_USED:
        break;

      case UPD_MANGLING_NUMBER:
      case UPD_STATIC_LOCAL_NUMBER:
        Record.push_back(Update.getNumber());
        break;

      case UPD_DECL_MARKED_OPENMP_THREADPRIVATE:
        Record.AddSourceRange(
            D->getAttr<OMPThreadPrivateDeclAttr>()->getRange());
        break;

      case UPD_DECL_MARKED_OPENMP_ALLOCATE: {
        auto *A = D->getAttr<OMPAllocateDeclAttr>();
        Record.push_back(A->getAllocatorType());
        Record.AddStmt(A->getAllocator());
        Record.AddStmt(A->getAlignment());
        Record.AddSourceRange(A->getRange());
        break;
      }

      case UPD_DECL_MARKED_OPENMP_DECLARETARGET:
        Record.push_back(D->getAttr<OMPDeclareTargetDeclAttr>()->getMapType());
        Record.AddSourceRange(
            D->getAttr<OMPDeclareTargetDeclAttr>()->getRange());
        break;

      case UPD_DECL_EXPORTED:
        Record.push_back(getSubmoduleID(Update.getModule()));
        break;

      case UPD_ADDED_ATTR_TO_RECORD:
        Record.AddAttributes(llvm::ArrayRef(Update.getAttr()));
        break;
      }
    }

    // Add a trailing update record, if any. These must go last because we
    // lazily load their attached statement.
    if (!GeneratingReducedBMI || !CanElideDeclDef(D)) {
      if (HasUpdatedBody) {
        const auto *Def = cast<FunctionDecl>(D);
        Record.push_back(UPD_CXX_ADDED_FUNCTION_DEFINITION);
        Record.push_back(Def->isInlined());
        Record.AddSourceLocation(Def->getInnerLocStart());
        Record.AddFunctionDefinition(Def);
      } else if (HasAddedVarDefinition) {
        const auto *VD = cast<VarDecl>(D);
        Record.push_back(UPD_CXX_ADDED_VAR_DEFINITION);
        Record.push_back(VD->isInline());
        Record.push_back(VD->isInlineSpecified());
        Record.AddVarDeclInit(VD);
      }
    }

    AddDeclRef(D, OffsetsRecord);
    OffsetsRecord.push_back(Record.Emit(DECL_UPDATES));
  }
}

void ASTWriter::AddAlignPackInfo(const Sema::AlignPackInfo &Info,
                                 RecordDataImpl &Record) {
  uint32_t Raw = Sema::AlignPackInfo::getRawEncoding(Info);
  Record.push_back(Raw);
}

FileID ASTWriter::getAdjustedFileID(FileID FID) const {
  if (FID.isInvalid() || PP->getSourceManager().isLoadedFileID(FID) ||
      NonAffectingFileIDs.empty())
    return FID;
  auto It = llvm::lower_bound(NonAffectingFileIDs, FID);
  unsigned Idx = std::distance(NonAffectingFileIDs.begin(), It);
  unsigned Offset = NonAffectingFileIDAdjustments[Idx];
  return FileID::get(FID.getOpaqueValue() - Offset);
}

unsigned ASTWriter::getAdjustedNumCreatedFIDs(FileID FID) const {
  unsigned NumCreatedFIDs = PP->getSourceManager()
                                .getLocalSLocEntry(FID.ID)
                                .getFile()
                                .NumCreatedFIDs;

  unsigned AdjustedNumCreatedFIDs = 0;
  for (unsigned I = FID.ID, N = I + NumCreatedFIDs; I != N; ++I)
    if (IsSLocAffecting[I])
      ++AdjustedNumCreatedFIDs;
  return AdjustedNumCreatedFIDs;
}

SourceLocation ASTWriter::getAdjustedLocation(SourceLocation Loc) const {
  if (Loc.isInvalid())
    return Loc;
  return Loc.getLocWithOffset(-getAdjustment(Loc.getOffset()));
}

SourceRange ASTWriter::getAdjustedRange(SourceRange Range) const {
  return SourceRange(getAdjustedLocation(Range.getBegin()),
                     getAdjustedLocation(Range.getEnd()));
}

SourceLocation::UIntTy
ASTWriter::getAdjustedOffset(SourceLocation::UIntTy Offset) const {
  return Offset - getAdjustment(Offset);
}

SourceLocation::UIntTy
ASTWriter::getAdjustment(SourceLocation::UIntTy Offset) const {
  if (NonAffectingRanges.empty())
    return 0;

  if (PP->getSourceManager().isLoadedOffset(Offset))
    return 0;

  if (Offset > NonAffectingRanges.back().getEnd().getOffset())
    return NonAffectingOffsetAdjustments.back();

  if (Offset < NonAffectingRanges.front().getBegin().getOffset())
    return 0;

  auto Contains = [](const SourceRange &Range, SourceLocation::UIntTy Offset) {
    return Range.getEnd().getOffset() < Offset;
  };

  auto It = llvm::lower_bound(NonAffectingRanges, Offset, Contains);
  unsigned Idx = std::distance(NonAffectingRanges.begin(), It);
  return NonAffectingOffsetAdjustments[Idx];
}

void ASTWriter::AddFileID(FileID FID, RecordDataImpl &Record) {
  Record.push_back(getAdjustedFileID(FID).getOpaqueValue());
}

SourceLocationEncoding::RawLocEncoding
ASTWriter::getRawSourceLocationEncoding(SourceLocation Loc, LocSeq *Seq) {
  unsigned BaseOffset = 0;
  unsigned ModuleFileIndex = 0;

  // See SourceLocationEncoding.h for the encoding details.
  if (Context->getSourceManager().isLoadedSourceLocation(Loc) &&
      Loc.isValid()) {
    assert(getChain());
    auto SLocMapI = getChain()->GlobalSLocOffsetMap.find(
        SourceManager::MaxLoadedOffset - Loc.getOffset() - 1);
    assert(SLocMapI != getChain()->GlobalSLocOffsetMap.end() &&
           "Corrupted global sloc offset map");
    ModuleFile *F = SLocMapI->second;
    BaseOffset = F->SLocEntryBaseOffset - 2;
    // 0 means the location is not loaded. So we need to add 1 to the index to
    // make it clear.
    ModuleFileIndex = F->Index + 1;
    assert(&getChain()->getModuleManager()[F->Index] == F);
  }

  return SourceLocationEncoding::encode(Loc, BaseOffset, ModuleFileIndex, Seq);
}

void ASTWriter::AddSourceLocation(SourceLocation Loc, RecordDataImpl &Record,
                                  SourceLocationSequence *Seq) {
  Loc = getAdjustedLocation(Loc);
  Record.push_back(getRawSourceLocationEncoding(Loc, Seq));
}

void ASTWriter::AddSourceRange(SourceRange Range, RecordDataImpl &Record,
                               SourceLocationSequence *Seq) {
  AddSourceLocation(Range.getBegin(), Record, Seq);
  AddSourceLocation(Range.getEnd(), Record, Seq);
}

void ASTRecordWriter::AddAPFloat(const llvm::APFloat &Value) {
  AddAPInt(Value.bitcastToAPInt());
}

void ASTWriter::AddIdentifierRef(const IdentifierInfo *II, RecordDataImpl &Record) {
  Record.push_back(getIdentifierRef(II));
}

IdentifierID ASTWriter::getIdentifierRef(const IdentifierInfo *II) {
  if (!II)
    return 0;

  IdentifierID &ID = IdentifierIDs[II];
  if (ID == 0)
    ID = NextIdentID++;
  return ID;
}

MacroID ASTWriter::getMacroRef(MacroInfo *MI, const IdentifierInfo *Name) {
  // Don't emit builtin macros like __LINE__ to the AST file unless they
  // have been redefined by the header (in which case they are not
  // isBuiltinMacro).
  if (!MI || MI->isBuiltinMacro())
    return 0;

  MacroID &ID = MacroIDs[MI];
  if (ID == 0) {
    ID = NextMacroID++;
    MacroInfoToEmitData Info = { Name, MI, ID };
    MacroInfosToEmit.push_back(Info);
  }
  return ID;
}

MacroID ASTWriter::getMacroID(MacroInfo *MI) {
  if (!MI || MI->isBuiltinMacro())
    return 0;

  assert(MacroIDs.contains(MI) && "Macro not emitted!");
  return MacroIDs[MI];
}

uint32_t ASTWriter::getMacroDirectivesOffset(const IdentifierInfo *Name) {
  return IdentMacroDirectivesOffsetMap.lookup(Name);
}

void ASTRecordWriter::AddSelectorRef(const Selector SelRef) {
  Record->push_back(Writer->getSelectorRef(SelRef));
}

SelectorID ASTWriter::getSelectorRef(Selector Sel) {
  if (Sel.getAsOpaquePtr() == nullptr) {
    return 0;
  }

  SelectorID SID = SelectorIDs[Sel];
  if (SID == 0 && Chain) {
    // This might trigger a ReadSelector callback, which will set the ID for
    // this selector.
    Chain->LoadSelector(Sel);
    SID = SelectorIDs[Sel];
  }
  if (SID == 0) {
    SID = NextSelectorID++;
    SelectorIDs[Sel] = SID;
  }
  return SID;
}

void ASTRecordWriter::AddCXXTemporary(const CXXTemporary *Temp) {
  AddDeclRef(Temp->getDestructor());
}

void ASTRecordWriter::AddTemplateArgumentLocInfo(
    TemplateArgument::ArgKind Kind, const TemplateArgumentLocInfo &Arg) {
  switch (Kind) {
  case TemplateArgument::Expression:
    AddStmt(Arg.getAsExpr());
    break;
  case TemplateArgument::Type:
    AddTypeSourceInfo(Arg.getAsTypeSourceInfo());
    break;
  case TemplateArgument::Template:
    AddNestedNameSpecifierLoc(Arg.getTemplateQualifierLoc());
    AddSourceLocation(Arg.getTemplateNameLoc());
    break;
  case TemplateArgument::TemplateExpansion:
    AddNestedNameSpecifierLoc(Arg.getTemplateQualifierLoc());
    AddSourceLocation(Arg.getTemplateNameLoc());
    AddSourceLocation(Arg.getTemplateEllipsisLoc());
    break;
  case TemplateArgument::Null:
  case TemplateArgument::Integral:
  case TemplateArgument::Declaration:
  case TemplateArgument::NullPtr:
  case TemplateArgument::StructuralValue:
  case TemplateArgument::Pack:
    // FIXME: Is this right?
    break;
  }
}

void ASTRecordWriter::AddTemplateArgumentLoc(const TemplateArgumentLoc &Arg) {
  AddTemplateArgument(Arg.getArgument());

  if (Arg.getArgument().getKind() == TemplateArgument::Expression) {
    bool InfoHasSameExpr
      = Arg.getArgument().getAsExpr() == Arg.getLocInfo().getAsExpr();
    Record->push_back(InfoHasSameExpr);
    if (InfoHasSameExpr)
      return; // Avoid storing the same expr twice.
  }
  AddTemplateArgumentLocInfo(Arg.getArgument().getKind(), Arg.getLocInfo());
}

void ASTRecordWriter::AddTypeSourceInfo(TypeSourceInfo *TInfo) {
  if (!TInfo) {
    AddTypeRef(QualType());
    return;
  }

  AddTypeRef(TInfo->getType());
  AddTypeLoc(TInfo->getTypeLoc());
}

void ASTRecordWriter::AddTypeLoc(TypeLoc TL, LocSeq *OuterSeq) {
  LocSeq::State Seq(OuterSeq);
  TypeLocWriter TLW(*this, Seq);
  for (; !TL.isNull(); TL = TL.getNextTypeLoc())
    TLW.Visit(TL);
}

void ASTWriter::AddTypeRef(QualType T, RecordDataImpl &Record) {
  Record.push_back(GetOrCreateTypeID(T));
}

template <typename IdxForTypeTy>
static TypeID MakeTypeID(ASTContext &Context, QualType T,
                         IdxForTypeTy IdxForType) {
  if (T.isNull())
    return PREDEF_TYPE_NULL_ID;

  unsigned FastQuals = T.getLocalFastQualifiers();
  T.removeLocalFastQualifiers();

  if (T.hasLocalNonFastQualifiers())
    return IdxForType(T).asTypeID(FastQuals);

  assert(!T.hasLocalQualifiers());

  if (const BuiltinType *BT = dyn_cast<BuiltinType>(T.getTypePtr()))
    return TypeIdxFromBuiltin(BT).asTypeID(FastQuals);

  if (T == Context.AutoDeductTy)
    return TypeIdx(0, PREDEF_TYPE_AUTO_DEDUCT).asTypeID(FastQuals);
  if (T == Context.AutoRRefDeductTy)
    return TypeIdx(0, PREDEF_TYPE_AUTO_RREF_DEDUCT).asTypeID(FastQuals);

  return IdxForType(T).asTypeID(FastQuals);
}

TypeID ASTWriter::GetOrCreateTypeID(QualType T) {
  assert(Context);
  return MakeTypeID(*Context, T, [&](QualType T) -> TypeIdx {
    if (T.isNull())
      return TypeIdx();
    assert(!T.getLocalFastQualifiers());

    TypeIdx &Idx = TypeIdxs[T];
    if (Idx.getValue() == 0) {
      if (DoneWritingDeclsAndTypes) {
        assert(0 && "New type seen after serializing all the types to emit!");
        return TypeIdx();
      }

      // We haven't seen this type before. Assign it a new ID and put it
      // into the queue of types to emit.
      Idx = TypeIdx(0, NextTypeID++);
      DeclTypesToEmit.push(T);
    }
    return Idx;
  });
}

void ASTWriter::AddEmittedDeclRef(const Decl *D, RecordDataImpl &Record) {
  if (!wasDeclEmitted(D))
    return;

  AddDeclRef(D, Record);
}

void ASTWriter::AddDeclRef(const Decl *D, RecordDataImpl &Record) {
  Record.push_back(GetDeclRef(D).getRawValue());
}

LocalDeclID ASTWriter::GetDeclRef(const Decl *D) {
  assert(WritingAST && "Cannot request a declaration ID before AST writing");

  if (!D) {
    return LocalDeclID();
  }

  // If the DeclUpdate from the GMF gets touched, emit it.
  if (auto *Iter = DeclUpdatesFromGMF.find(D);
      Iter != DeclUpdatesFromGMF.end()) {
    for (DeclUpdate &Update : Iter->second)
      DeclUpdates[D].push_back(Update);
    DeclUpdatesFromGMF.erase(Iter);
  }

  // If D comes from an AST file, its declaration ID is already known and
  // fixed.
  if (D->isFromASTFile()) {
    if (isWritingStdCXXNamedModules() && D->getOwningModule())
      TouchedTopLevelModules.insert(D->getOwningModule()->getTopLevelModule());

    return LocalDeclID(D->getGlobalID());
  }

  assert(!(reinterpret_cast<uintptr_t>(D) & 0x01) && "Invalid decl pointer");
  LocalDeclID &ID = DeclIDs[D];
  if (ID.isInvalid()) {
    if (DoneWritingDeclsAndTypes) {
      assert(0 && "New decl seen after serializing all the decls to emit!");
      return LocalDeclID();
    }

    // We haven't seen this declaration before. Give it a new ID and
    // enqueue it in the list of declarations to emit.
    ID = NextDeclID++;
    DeclTypesToEmit.push(const_cast<Decl *>(D));
  }

  return ID;
}

LocalDeclID ASTWriter::getDeclID(const Decl *D) {
  if (!D)
    return LocalDeclID();

  // If D comes from an AST file, its declaration ID is already known and
  // fixed.
  if (D->isFromASTFile())
    return LocalDeclID(D->getGlobalID());

  assert(DeclIDs.contains(D) && "Declaration not emitted!");
  return DeclIDs[D];
}

bool ASTWriter::wasDeclEmitted(const Decl *D) const {
  assert(D);

  assert(DoneWritingDeclsAndTypes &&
         "wasDeclEmitted should only be called after writing declarations");

  if (D->isFromASTFile())
    return true;

  bool Emitted = DeclIDs.contains(D);
  assert((Emitted || (!D->getOwningModule() && isWritingStdCXXNamedModules()) ||
          GeneratingReducedBMI) &&
         "The declaration within modules can only be omitted in reduced BMI.");
  return Emitted;
}

void ASTWriter::associateDeclWithFile(const Decl *D, LocalDeclID ID) {
  assert(ID.isValid());
  assert(D);

  SourceLocation Loc = D->getLocation();
  if (Loc.isInvalid())
    return;

  // We only keep track of the file-level declarations of each file.
  if (!D->getLexicalDeclContext()->isFileContext())
    return;
  // FIXME: ParmVarDecls that are part of a function type of a parameter of
  // a function/objc method, should not have TU as lexical context.
  // TemplateTemplateParmDecls that are part of an alias template, should not
  // have TU as lexical context.
  if (isa<ParmVarDecl, TemplateTemplateParmDecl>(D))
    return;

  SourceManager &SM = Context->getSourceManager();
  SourceLocation FileLoc = SM.getFileLoc(Loc);
  assert(SM.isLocalSourceLocation(FileLoc));
  FileID FID;
  unsigned Offset;
  std::tie(FID, Offset) = SM.getDecomposedLoc(FileLoc);
  if (FID.isInvalid())
    return;
  assert(SM.getSLocEntry(FID).isFile());
  assert(IsSLocAffecting[FID.ID]);

  std::unique_ptr<DeclIDInFileInfo> &Info = FileDeclIDs[FID];
  if (!Info)
    Info = std::make_unique<DeclIDInFileInfo>();

  std::pair<unsigned, LocalDeclID> LocDecl(Offset, ID);
  LocDeclIDsTy &Decls = Info->DeclIDs;
  Decls.push_back(LocDecl);
}

unsigned ASTWriter::getAnonymousDeclarationNumber(const NamedDecl *D) {
  assert(needsAnonymousDeclarationNumber(D) &&
         "expected an anonymous declaration");

  // Number the anonymous declarations within this context, if we've not
  // already done so.
  auto It = AnonymousDeclarationNumbers.find(D);
  if (It == AnonymousDeclarationNumbers.end()) {
    auto *DC = D->getLexicalDeclContext();
    numberAnonymousDeclsWithin(DC, [&](const NamedDecl *ND, unsigned Number) {
      AnonymousDeclarationNumbers[ND] = Number;
    });

    It = AnonymousDeclarationNumbers.find(D);
    assert(It != AnonymousDeclarationNumbers.end() &&
           "declaration not found within its lexical context");
  }

  return It->second;
}

void ASTRecordWriter::AddDeclarationNameLoc(const DeclarationNameLoc &DNLoc,
                                            DeclarationName Name) {
  switch (Name.getNameKind()) {
  case DeclarationName::CXXConstructorName:
  case DeclarationName::CXXDestructorName:
  case DeclarationName::CXXConversionFunctionName:
    AddTypeSourceInfo(DNLoc.getNamedTypeInfo());
    break;

  case DeclarationName::CXXOperatorName:
    AddSourceRange(DNLoc.getCXXOperatorNameRange());
    break;

  case DeclarationName::CXXLiteralOperatorName:
    AddSourceLocation(DNLoc.getCXXLiteralOperatorNameLoc());
    break;

  case DeclarationName::Identifier:
  case DeclarationName::ObjCZeroArgSelector:
  case DeclarationName::ObjCOneArgSelector:
  case DeclarationName::ObjCMultiArgSelector:
  case DeclarationName::CXXUsingDirective:
  case DeclarationName::CXXDeductionGuideName:
    break;
  }
}

void ASTRecordWriter::AddDeclarationNameInfo(
    const DeclarationNameInfo &NameInfo) {
  AddDeclarationName(NameInfo.getName());
  AddSourceLocation(NameInfo.getLoc());
  AddDeclarationNameLoc(NameInfo.getInfo(), NameInfo.getName());
}

void ASTRecordWriter::AddQualifierInfo(const QualifierInfo &Info) {
  AddNestedNameSpecifierLoc(Info.QualifierLoc);
  Record->push_back(Info.NumTemplParamLists);
  for (unsigned i = 0, e = Info.NumTemplParamLists; i != e; ++i)
    AddTemplateParameterList(Info.TemplParamLists[i]);
}

void ASTRecordWriter::AddNestedNameSpecifierLoc(NestedNameSpecifierLoc NNS) {
  // Nested name specifiers usually aren't too long. I think that 8 would
  // typically accommodate the vast majority.
  SmallVector<NestedNameSpecifierLoc , 8> NestedNames;

  // Push each of the nested-name-specifiers's onto a stack for
  // serialization in reverse order.
  while (NNS) {
    NestedNames.push_back(NNS);
    NNS = NNS.getPrefix();
  }

  Record->push_back(NestedNames.size());
  while(!NestedNames.empty()) {
    NNS = NestedNames.pop_back_val();
    NestedNameSpecifier::SpecifierKind Kind
      = NNS.getNestedNameSpecifier()->getKind();
    Record->push_back(Kind);
    switch (Kind) {
    case NestedNameSpecifier::Identifier:
      AddIdentifierRef(NNS.getNestedNameSpecifier()->getAsIdentifier());
      AddSourceRange(NNS.getLocalSourceRange());
      break;

    case NestedNameSpecifier::Namespace:
      AddDeclRef(NNS.getNestedNameSpecifier()->getAsNamespace());
      AddSourceRange(NNS.getLocalSourceRange());
      break;

    case NestedNameSpecifier::NamespaceAlias:
      AddDeclRef(NNS.getNestedNameSpecifier()->getAsNamespaceAlias());
      AddSourceRange(NNS.getLocalSourceRange());
      break;

    case NestedNameSpecifier::TypeSpec:
    case NestedNameSpecifier::TypeSpecWithTemplate:
      Record->push_back(Kind == NestedNameSpecifier::TypeSpecWithTemplate);
      AddTypeRef(NNS.getTypeLoc().getType());
      AddTypeLoc(NNS.getTypeLoc());
      AddSourceLocation(NNS.getLocalSourceRange().getEnd());
      break;

    case NestedNameSpecifier::Global:
      AddSourceLocation(NNS.getLocalSourceRange().getEnd());
      break;

    case NestedNameSpecifier::Super:
      AddDeclRef(NNS.getNestedNameSpecifier()->getAsRecordDecl());
      AddSourceRange(NNS.getLocalSourceRange());
      break;
    }
  }
}

void ASTRecordWriter::AddTemplateParameterList(
    const TemplateParameterList *TemplateParams) {
  assert(TemplateParams && "No TemplateParams!");
  AddSourceLocation(TemplateParams->getTemplateLoc());
  AddSourceLocation(TemplateParams->getLAngleLoc());
  AddSourceLocation(TemplateParams->getRAngleLoc());

  Record->push_back(TemplateParams->size());
  for (const auto &P : *TemplateParams)
    AddDeclRef(P);
  if (const Expr *RequiresClause = TemplateParams->getRequiresClause()) {
    Record->push_back(true);
    writeStmtRef(RequiresClause);
  } else {
    Record->push_back(false);
  }
}

/// Emit a template argument list.
void ASTRecordWriter::AddTemplateArgumentList(
    const TemplateArgumentList *TemplateArgs) {
  assert(TemplateArgs && "No TemplateArgs!");
  Record->push_back(TemplateArgs->size());
  for (int i = 0, e = TemplateArgs->size(); i != e; ++i)
    AddTemplateArgument(TemplateArgs->get(i));
}

void ASTRecordWriter::AddASTTemplateArgumentListInfo(
    const ASTTemplateArgumentListInfo *ASTTemplArgList) {
  assert(ASTTemplArgList && "No ASTTemplArgList!");
  AddSourceLocation(ASTTemplArgList->LAngleLoc);
  AddSourceLocation(ASTTemplArgList->RAngleLoc);
  Record->push_back(ASTTemplArgList->NumTemplateArgs);
  const TemplateArgumentLoc *TemplArgs = ASTTemplArgList->getTemplateArgs();
  for (int i = 0, e = ASTTemplArgList->NumTemplateArgs; i != e; ++i)
    AddTemplateArgumentLoc(TemplArgs[i]);
}

void ASTRecordWriter::AddUnresolvedSet(const ASTUnresolvedSet &Set) {
  Record->push_back(Set.size());
  for (ASTUnresolvedSet::const_iterator
         I = Set.begin(), E = Set.end(); I != E; ++I) {
    AddDeclRef(I.getDecl());
    Record->push_back(I.getAccess());
  }
}

// FIXME: Move this out of the main ASTRecordWriter interface.
void ASTRecordWriter::AddCXXBaseSpecifier(const CXXBaseSpecifier &Base) {
  Record->push_back(Base.isVirtual());
  Record->push_back(Base.isBaseOfClass());
  Record->push_back(Base.getAccessSpecifierAsWritten());
  Record->push_back(Base.getInheritConstructors());
  AddTypeSourceInfo(Base.getTypeSourceInfo());
  AddSourceRange(Base.getSourceRange());
  AddSourceLocation(Base.isPackExpansion()? Base.getEllipsisLoc()
                                          : SourceLocation());
}

static uint64_t EmitCXXBaseSpecifiers(ASTWriter &W,
                                      ArrayRef<CXXBaseSpecifier> Bases) {
  ASTWriter::RecordData Record;
  ASTRecordWriter Writer(W, Record);
  Writer.push_back(Bases.size());

  for (auto &Base : Bases)
    Writer.AddCXXBaseSpecifier(Base);

  return Writer.Emit(serialization::DECL_CXX_BASE_SPECIFIERS);
}

// FIXME: Move this out of the main ASTRecordWriter interface.
void ASTRecordWriter::AddCXXBaseSpecifiers(ArrayRef<CXXBaseSpecifier> Bases) {
  AddOffset(EmitCXXBaseSpecifiers(*Writer, Bases));
}

static uint64_t
EmitCXXCtorInitializers(ASTWriter &W,
                        ArrayRef<CXXCtorInitializer *> CtorInits) {
  ASTWriter::RecordData Record;
  ASTRecordWriter Writer(W, Record);
  Writer.push_back(CtorInits.size());

  for (auto *Init : CtorInits) {
    if (Init->isBaseInitializer()) {
      Writer.push_back(CTOR_INITIALIZER_BASE);
      Writer.AddTypeSourceInfo(Init->getTypeSourceInfo());
      Writer.push_back(Init->isBaseVirtual());
    } else if (Init->isDelegatingInitializer()) {
      Writer.push_back(CTOR_INITIALIZER_DELEGATING);
      Writer.AddTypeSourceInfo(Init->getTypeSourceInfo());
    } else if (Init->isMemberInitializer()){
      Writer.push_back(CTOR_INITIALIZER_MEMBER);
      Writer.AddDeclRef(Init->getMember());
    } else {
      Writer.push_back(CTOR_INITIALIZER_INDIRECT_MEMBER);
      Writer.AddDeclRef(Init->getIndirectMember());
    }

    Writer.AddSourceLocation(Init->getMemberLocation());
    Writer.AddStmt(Init->getInit());
    Writer.AddSourceLocation(Init->getLParenLoc());
    Writer.AddSourceLocation(Init->getRParenLoc());
    Writer.push_back(Init->isWritten());
    if (Init->isWritten())
      Writer.push_back(Init->getSourceOrder());
  }

  return Writer.Emit(serialization::DECL_CXX_CTOR_INITIALIZERS);
}

// FIXME: Move this out of the main ASTRecordWriter interface.
void ASTRecordWriter::AddCXXCtorInitializers(
    ArrayRef<CXXCtorInitializer *> CtorInits) {
  AddOffset(EmitCXXCtorInitializers(*Writer, CtorInits));
}

void ASTRecordWriter::AddCXXDefinitionData(const CXXRecordDecl *D) {
  auto &Data = D->data();

  Record->push_back(Data.IsLambda);

  BitsPacker DefinitionBits;

#define FIELD(Name, Width, Merge)                                              \
  if (!DefinitionBits.canWriteNextNBits(Width)) {                              \
    Record->push_back(DefinitionBits);                                         \
    DefinitionBits.reset(0);                                                   \
  }                                                                            \
  DefinitionBits.addBits(Data.Name, Width);

#include "clang/AST/CXXRecordDeclDefinitionBits.def"
#undef FIELD

  Record->push_back(DefinitionBits);

  // getODRHash will compute the ODRHash if it has not been previously
  // computed.
  Record->push_back(D->getODRHash());

  bool ModulesCodegen =
      !D->isDependentType() &&
     (Writer->Context->getLangOpts().ModulesDebugInfo ||
      D->isInNamedModule());
  Record->push_back(ModulesCodegen);
  if (ModulesCodegen)
    Writer->AddDeclRef(D, Writer->ModularCodegenDecls);

  // IsLambda bit is already saved.

  AddUnresolvedSet(Data.Conversions.get(*Writer->Context));
  Record->push_back(Data.ComputedVisibleConversions);
  if (Data.ComputedVisibleConversions)
    AddUnresolvedSet(Data.VisibleConversions.get(*Writer->Context));
  // Data.Definition is the owning decl, no need to write it.

  if (!Data.IsLambda) {
    Record->push_back(Data.NumBases);
    if (Data.NumBases > 0)
      AddCXXBaseSpecifiers(Data.bases());

    // FIXME: Make VBases lazily computed when needed to avoid storing them.
    Record->push_back(Data.NumVBases);
    if (Data.NumVBases > 0)
      AddCXXBaseSpecifiers(Data.vbases());

    AddDeclRef(D->getFirstFriend());
  } else {
    auto &Lambda = D->getLambdaData();

    BitsPacker LambdaBits;
    LambdaBits.addBits(Lambda.DependencyKind, /*Width=*/2);
    LambdaBits.addBit(Lambda.IsGenericLambda);
    LambdaBits.addBits(Lambda.CaptureDefault, /*Width=*/2);
    LambdaBits.addBits(Lambda.NumCaptures, /*Width=*/15);
    LambdaBits.addBit(Lambda.HasKnownInternalLinkage);
    Record->push_back(LambdaBits);

    Record->push_back(Lambda.NumExplicitCaptures);
    Record->push_back(Lambda.ManglingNumber);
    Record->push_back(D->getDeviceLambdaManglingNumber());
    // The lambda context declaration and index within the context are provided
    // separately, so that they can be used for merging.
    AddTypeSourceInfo(Lambda.MethodTyInfo);
    for (unsigned I = 0, N = Lambda.NumCaptures; I != N; ++I) {
      const LambdaCapture &Capture = Lambda.Captures.front()[I];
      AddSourceLocation(Capture.getLocation());

      BitsPacker CaptureBits;
      CaptureBits.addBit(Capture.isImplicit());
      CaptureBits.addBits(Capture.getCaptureKind(), /*Width=*/3);
      Record->push_back(CaptureBits);

      switch (Capture.getCaptureKind()) {
      case LCK_StarThis:
      case LCK_This:
      case LCK_VLAType:
        break;
      case LCK_ByCopy:
      case LCK_ByRef:
        ValueDecl *Var =
            Capture.capturesVariable() ? Capture.getCapturedVar() : nullptr;
        AddDeclRef(Var);
        AddSourceLocation(Capture.isPackExpansion() ? Capture.getEllipsisLoc()
                                                    : SourceLocation());
        break;
      }
    }
  }
}

void ASTRecordWriter::AddVarDeclInit(const VarDecl *VD) {
  const Expr *Init = VD->getInit();
  if (!Init) {
    push_back(0);
    return;
  }

  uint64_t Val = 1;
  if (EvaluatedStmt *ES = VD->getEvaluatedStmt()) {
    Val |= (ES->HasConstantInitialization ? 2 : 0);
    Val |= (ES->HasConstantDestruction ? 4 : 0);
    APValue *Evaluated = VD->getEvaluatedValue();
    // If the evaluated result is constant, emit it.
    if (Evaluated && (Evaluated->isInt() || Evaluated->isFloat()))
      Val |= 8;
  }
  push_back(Val);
  if (Val & 8) {
    AddAPValue(*VD->getEvaluatedValue());
  }

  writeStmtRef(Init);
}

void ASTWriter::ReaderInitialized(ASTReader *Reader) {
  assert(Reader && "Cannot remove chain");
  assert((!Chain || Chain == Reader) && "Cannot replace chain");
  assert(FirstDeclID == NextDeclID &&
         FirstTypeID == NextTypeID &&
         FirstIdentID == NextIdentID &&
         FirstMacroID == NextMacroID &&
         FirstSubmoduleID == NextSubmoduleID &&
         FirstSelectorID == NextSelectorID &&
         "Setting chain after writing has started.");

  Chain = Reader;

  // Note, this will get called multiple times, once one the reader starts up
  // and again each time it's done reading a PCH or module.
  FirstMacroID = NUM_PREDEF_MACRO_IDS + Chain->getTotalNumMacros();
  FirstSubmoduleID = NUM_PREDEF_SUBMODULE_IDS + Chain->getTotalNumSubmodules();
  FirstSelectorID = NUM_PREDEF_SELECTOR_IDS + Chain->getTotalNumSelectors();
  NextMacroID = FirstMacroID;
  NextSelectorID = FirstSelectorID;
  NextSubmoduleID = FirstSubmoduleID;
}

void ASTWriter::IdentifierRead(IdentifierID ID, IdentifierInfo *II) {
  // Don't reuse Type ID from external modules for named modules. See the
  // comments in WriteASTCore for details.
  if (isWritingStdCXXNamedModules())
    return;

  IdentifierID &StoredID = IdentifierIDs[II];
  unsigned OriginalModuleFileIndex = StoredID >> 32;

  // Always keep the local identifier ID. See \p TypeRead() for more
  // information.
  if (OriginalModuleFileIndex == 0 && StoredID)
    return;

  // Otherwise, keep the highest ID since the module file comes later has
  // higher module file indexes.
  if (ID > StoredID)
    StoredID = ID;
}

void ASTWriter::MacroRead(serialization::MacroID ID, MacroInfo *MI) {
  // Always keep the highest ID. See \p TypeRead() for more information.
  MacroID &StoredID = MacroIDs[MI];
  if (ID > StoredID)
    StoredID = ID;
}

void ASTWriter::TypeRead(TypeIdx Idx, QualType T) {
  // Don't reuse Type ID from external modules for named modules. See the
  // comments in WriteASTCore for details.
  if (isWritingStdCXXNamedModules())
    return;

  // Always take the type index that comes in later module files.
  // This copes with an interesting
  // case for chained AST writing where we schedule writing the type and then,
  // later, deserialize the type from another AST. In this case, we want to
  // keep the entry from a later module so that we can properly write it out to
  // the AST file.
  TypeIdx &StoredIdx = TypeIdxs[T];

  // Ignore it if the type comes from the current being written module file.
  // Since the current module file being written logically has the highest
  // index.
  unsigned ModuleFileIndex = StoredIdx.getModuleFileIndex();
  if (ModuleFileIndex == 0 && StoredIdx.getValue())
    return;

  // Otherwise, keep the highest ID since the module file comes later has
  // higher module file indexes.
  if (Idx.getModuleFileIndex() >= StoredIdx.getModuleFileIndex())
    StoredIdx = Idx;
}

void ASTWriter::SelectorRead(SelectorID ID, Selector S) {
  // Always keep the highest ID. See \p TypeRead() for more information.
  SelectorID &StoredID = SelectorIDs[S];
  if (ID > StoredID)
    StoredID = ID;
}

void ASTWriter::MacroDefinitionRead(serialization::PreprocessedEntityID ID,
                                    MacroDefinitionRecord *MD) {
  assert(!MacroDefinitions.contains(MD));
  MacroDefinitions[MD] = ID;
}

void ASTWriter::ModuleRead(serialization::SubmoduleID ID, Module *Mod) {
  assert(!SubmoduleIDs.contains(Mod));
  SubmoduleIDs[Mod] = ID;
}

void ASTWriter::CompletedTagDefinition(const TagDecl *D) {
  if (Chain && Chain->isProcessingUpdateRecords()) return;
  assert(D->isCompleteDefinition());
  assert(!WritingAST && "Already writing the AST!");
  if (auto *RD = dyn_cast<CXXRecordDecl>(D)) {
    // We are interested when a PCH decl is modified.
    if (RD->isFromASTFile()) {
      // A forward reference was mutated into a definition. Rewrite it.
      // FIXME: This happens during template instantiation, should we
      // have created a new definition decl instead ?
      assert(isTemplateInstantiation(RD->getTemplateSpecializationKind()) &&
             "completed a tag from another module but not by instantiation?");
      DeclUpdates[RD].push_back(
          DeclUpdate(UPD_CXX_INSTANTIATED_CLASS_DEFINITION));
    }
  }
}

static bool isImportedDeclContext(ASTReader *Chain, const Decl *D) {
  if (D->isFromASTFile())
    return true;

  // The predefined __va_list_tag struct is imported if we imported any decls.
  // FIXME: This is a gross hack.
  return D == D->getASTContext().getVaListTagDecl();
}

void ASTWriter::AddedVisibleDecl(const DeclContext *DC, const Decl *D) {
  if (Chain && Chain->isProcessingUpdateRecords()) return;
  assert(DC->isLookupContext() &&
          "Should not add lookup results to non-lookup contexts!");

  // TU is handled elsewhere.
  if (isa<TranslationUnitDecl>(DC))
    return;

  // Namespaces are handled elsewhere, except for template instantiations of
  // FunctionTemplateDecls in namespaces. We are interested in cases where the
  // local instantiations are added to an imported context. Only happens when
  // adding ADL lookup candidates, for example templated friends.
  if (isa<NamespaceDecl>(DC) && D->getFriendObjectKind() == Decl::FOK_None &&
      !isa<FunctionTemplateDecl>(D))
    return;

  // We're only interested in cases where a local declaration is added to an
  // imported context.
  if (D->isFromASTFile() || !isImportedDeclContext(Chain, cast<Decl>(DC)))
    return;

  assert(DC == DC->getPrimaryContext() && "added to non-primary context");
  assert(!getDefinitiveDeclContext(DC) && "DeclContext not definitive!");
  assert(!WritingAST && "Already writing the AST!");
  if (UpdatedDeclContexts.insert(DC) && !cast<Decl>(DC)->isFromASTFile()) {
    // We're adding a visible declaration to a predefined decl context. Ensure
    // that we write out all of its lookup results so we don't get a nasty
    // surprise when we try to emit its lookup table.
    llvm::append_range(DeclsToEmitEvenIfUnreferenced, DC->decls());
  }
  DeclsToEmitEvenIfUnreferenced.push_back(D);
}

void ASTWriter::AddedCXXImplicitMember(const CXXRecordDecl *RD, const Decl *D) {
  if (Chain && Chain->isProcessingUpdateRecords()) return;
  assert(D->isImplicit());

  // We're only interested in cases where a local declaration is added to an
  // imported context.
  if (D->isFromASTFile() || !isImportedDeclContext(Chain, RD))
    return;

  if (!isa<CXXMethodDecl>(D))
    return;

  // A decl coming from PCH was modified.
  assert(RD->isCompleteDefinition());
  assert(!WritingAST && "Already writing the AST!");
  DeclUpdates[RD].push_back(DeclUpdate(UPD_CXX_ADDED_IMPLICIT_MEMBER, D));
}

void ASTWriter::ResolvedExceptionSpec(const FunctionDecl *FD) {
  if (Chain && Chain->isProcessingUpdateRecords()) return;
  assert(!DoneWritingDeclsAndTypes && "Already done writing updates!");
  if (!Chain) return;
  Chain->forEachImportedKeyDecl(FD, [&](const Decl *D) {
    // If we don't already know the exception specification for this redecl
    // chain, add an update record for it.
    if (isUnresolvedExceptionSpec(cast<FunctionDecl>(D)
                                      ->getType()
                                      ->castAs<FunctionProtoType>()
                                      ->getExceptionSpecType()))
      DeclUpdates[D].push_back(UPD_CXX_RESOLVED_EXCEPTION_SPEC);
  });
}

void ASTWriter::DeducedReturnType(const FunctionDecl *FD, QualType ReturnType) {
  if (Chain && Chain->isProcessingUpdateRecords()) return;
  assert(!WritingAST && "Already writing the AST!");
  if (!Chain) return;
  Chain->forEachImportedKeyDecl(FD, [&](const Decl *D) {
    DeclUpdates[D].push_back(
        DeclUpdate(UPD_CXX_DEDUCED_RETURN_TYPE, ReturnType));
  });
}

void ASTWriter::ResolvedOperatorDelete(const CXXDestructorDecl *DD,
                                       const FunctionDecl *Delete,
                                       Expr *ThisArg) {
  if (Chain && Chain->isProcessingUpdateRecords()) return;
  assert(!WritingAST && "Already writing the AST!");
  assert(Delete && "Not given an operator delete");
  if (!Chain) return;
  Chain->forEachImportedKeyDecl(DD, [&](const Decl *D) {
    DeclUpdates[D].push_back(DeclUpdate(UPD_CXX_RESOLVED_DTOR_DELETE, Delete));
  });
}

void ASTWriter::CompletedImplicitDefinition(const FunctionDecl *D) {
  if (Chain && Chain->isProcessingUpdateRecords()) return;
  assert(!WritingAST && "Already writing the AST!");
  if (!D->isFromASTFile())
    return; // Declaration not imported from PCH.

  // Implicit function decl from a PCH was defined.
  DeclUpdates[D].push_back(DeclUpdate(UPD_CXX_ADDED_FUNCTION_DEFINITION));
}

void ASTWriter::VariableDefinitionInstantiated(const VarDecl *D) {
  if (Chain && Chain->isProcessingUpdateRecords()) return;
  assert(!WritingAST && "Already writing the AST!");
  if (!D->isFromASTFile())
    return;

  DeclUpdates[D].push_back(DeclUpdate(UPD_CXX_ADDED_VAR_DEFINITION));
}

void ASTWriter::FunctionDefinitionInstantiated(const FunctionDecl *D) {
  if (Chain && Chain->isProcessingUpdateRecords()) return;
  assert(!WritingAST && "Already writing the AST!");
  if (!D->isFromASTFile())
    return;

  DeclUpdates[D].push_back(DeclUpdate(UPD_CXX_ADDED_FUNCTION_DEFINITION));
}

void ASTWriter::InstantiationRequested(const ValueDecl *D) {
  if (Chain && Chain->isProcessingUpdateRecords()) return;
  assert(!WritingAST && "Already writing the AST!");
  if (!D->isFromASTFile())
    return;

  // Since the actual instantiation is delayed, this really means that we need
  // to update the instantiation location.
  SourceLocation POI;
  if (auto *VD = dyn_cast<VarDecl>(D))
    POI = VD->getPointOfInstantiation();
  else
    POI = cast<FunctionDecl>(D)->getPointOfInstantiation();
  DeclUpdates[D].push_back(DeclUpdate(UPD_CXX_POINT_OF_INSTANTIATION, POI));
}

void ASTWriter::DefaultArgumentInstantiated(const ParmVarDecl *D) {
  if (Chain && Chain->isProcessingUpdateRecords()) return;
  assert(!WritingAST && "Already writing the AST!");
  if (!D->isFromASTFile())
    return;

  DeclUpdates[D].push_back(
      DeclUpdate(UPD_CXX_INSTANTIATED_DEFAULT_ARGUMENT, D));
}

void ASTWriter::DefaultMemberInitializerInstantiated(const FieldDecl *D) {
  assert(!WritingAST && "Already writing the AST!");
  if (!D->isFromASTFile())
    return;

  DeclUpdates[D].push_back(
      DeclUpdate(UPD_CXX_INSTANTIATED_DEFAULT_MEMBER_INITIALIZER, D));
}

void ASTWriter::AddedObjCCategoryToInterface(const ObjCCategoryDecl *CatD,
                                             const ObjCInterfaceDecl *IFD) {
  if (Chain && Chain->isProcessingUpdateRecords()) return;
  assert(!WritingAST && "Already writing the AST!");
  if (!IFD->isFromASTFile())
    return; // Declaration not imported from PCH.

  assert(IFD->getDefinition() && "Category on a class without a definition?");
  ObjCClassesWithCategories.insert(
    const_cast<ObjCInterfaceDecl *>(IFD->getDefinition()));
}

void ASTWriter::DeclarationMarkedUsed(const Decl *D) {
  if (Chain && Chain->isProcessingUpdateRecords()) return;
  assert(!WritingAST && "Already writing the AST!");

  // If there is *any* declaration of the entity that's not from an AST file,
  // we can skip writing the update record. We make sure that isUsed() triggers
  // completion of the redeclaration chain of the entity.
  for (auto Prev = D->getMostRecentDecl(); Prev; Prev = Prev->getPreviousDecl())
    if (IsLocalDecl(Prev))
      return;

  DeclUpdates[D].push_back(DeclUpdate(UPD_DECL_MARKED_USED));
}

void ASTWriter::DeclarationMarkedOpenMPThreadPrivate(const Decl *D) {
  if (Chain && Chain->isProcessingUpdateRecords()) return;
  assert(!WritingAST && "Already writing the AST!");
  if (!D->isFromASTFile())
    return;

  DeclUpdates[D].push_back(DeclUpdate(UPD_DECL_MARKED_OPENMP_THREADPRIVATE));
}

void ASTWriter::DeclarationMarkedOpenMPAllocate(const Decl *D, const Attr *A) {
  if (Chain && Chain->isProcessingUpdateRecords()) return;
  assert(!WritingAST && "Already writing the AST!");
  if (!D->isFromASTFile())
    return;

  DeclUpdates[D].push_back(DeclUpdate(UPD_DECL_MARKED_OPENMP_ALLOCATE, A));
}

void ASTWriter::DeclarationMarkedOpenMPDeclareTarget(const Decl *D,
                                                     const Attr *Attr) {
  if (Chain && Chain->isProcessingUpdateRecords()) return;
  assert(!WritingAST && "Already writing the AST!");
  if (!D->isFromASTFile())
    return;

  DeclUpdates[D].push_back(
      DeclUpdate(UPD_DECL_MARKED_OPENMP_DECLARETARGET, Attr));
}

void ASTWriter::RedefinedHiddenDefinition(const NamedDecl *D, Module *M) {
  if (Chain && Chain->isProcessingUpdateRecords()) return;
  assert(!WritingAST && "Already writing the AST!");
  assert(!D->isUnconditionallyVisible() && "expected a hidden declaration");
  DeclUpdates[D].push_back(DeclUpdate(UPD_DECL_EXPORTED, M));
}

void ASTWriter::AddedAttributeToRecord(const Attr *Attr,
                                       const RecordDecl *Record) {
  if (Chain && Chain->isProcessingUpdateRecords()) return;
  assert(!WritingAST && "Already writing the AST!");
  if (!Record->isFromASTFile())
    return;
  DeclUpdates[Record].push_back(DeclUpdate(UPD_ADDED_ATTR_TO_RECORD, Attr));
}

void ASTWriter::AddedCXXTemplateSpecialization(
    const ClassTemplateDecl *TD, const ClassTemplateSpecializationDecl *D) {
  assert(!WritingAST && "Already writing the AST!");

  if (!TD->getFirstDecl()->isFromASTFile())
    return;
  if (Chain && Chain->isProcessingUpdateRecords())
    return;

  DeclsToEmitEvenIfUnreferenced.push_back(D);
}

void ASTWriter::AddedCXXTemplateSpecialization(
    const VarTemplateDecl *TD, const VarTemplateSpecializationDecl *D) {
  assert(!WritingAST && "Already writing the AST!");

  if (!TD->getFirstDecl()->isFromASTFile())
    return;
  if (Chain && Chain->isProcessingUpdateRecords())
    return;

  DeclsToEmitEvenIfUnreferenced.push_back(D);
}

void ASTWriter::AddedCXXTemplateSpecialization(const FunctionTemplateDecl *TD,
                                               const FunctionDecl *D) {
  assert(!WritingAST && "Already writing the AST!");

  if (!TD->getFirstDecl()->isFromASTFile())
    return;
  if (Chain && Chain->isProcessingUpdateRecords())
    return;

  DeclsToEmitEvenIfUnreferenced.push_back(D);
}

//===----------------------------------------------------------------------===//
//// OMPClause Serialization
////===----------------------------------------------------------------------===//

namespace {

class OMPClauseWriter : public OMPClauseVisitor<OMPClauseWriter> {
  ASTRecordWriter &Record;

public:
  OMPClauseWriter(ASTRecordWriter &Record) : Record(Record) {}
#define GEN_CLANG_CLAUSE_CLASS
#define CLAUSE_CLASS(Enum, Str, Class) void Visit##Class(Class *S);
#include "llvm/Frontend/OpenMP/OMP.inc"
  void writeClause(OMPClause *C);
  void VisitOMPClauseWithPreInit(OMPClauseWithPreInit *C);
  void VisitOMPClauseWithPostUpdate(OMPClauseWithPostUpdate *C);
};

}

void ASTRecordWriter::writeOMPClause(OMPClause *C) {
  OMPClauseWriter(*this).writeClause(C);
}

void OMPClauseWriter::writeClause(OMPClause *C) {
  Record.push_back(unsigned(C->getClauseKind()));
  Visit(C);
  Record.AddSourceLocation(C->getBeginLoc());
  Record.AddSourceLocation(C->getEndLoc());
}

void OMPClauseWriter::VisitOMPClauseWithPreInit(OMPClauseWithPreInit *C) {
  Record.push_back(uint64_t(C->getCaptureRegion()));
  Record.AddStmt(C->getPreInitStmt());
}

void OMPClauseWriter::VisitOMPClauseWithPostUpdate(OMPClauseWithPostUpdate *C) {
  VisitOMPClauseWithPreInit(C);
  Record.AddStmt(C->getPostUpdateExpr());
}

void OMPClauseWriter::VisitOMPIfClause(OMPIfClause *C) {
  VisitOMPClauseWithPreInit(C);
  Record.push_back(uint64_t(C->getNameModifier()));
  Record.AddSourceLocation(C->getNameModifierLoc());
  Record.AddSourceLocation(C->getColonLoc());
  Record.AddStmt(C->getCondition());
  Record.AddSourceLocation(C->getLParenLoc());
}

void OMPClauseWriter::VisitOMPFinalClause(OMPFinalClause *C) {
  VisitOMPClauseWithPreInit(C);
  Record.AddStmt(C->getCondition());
  Record.AddSourceLocation(C->getLParenLoc());
}

void OMPClauseWriter::VisitOMPNumThreadsClause(OMPNumThreadsClause *C) {
  VisitOMPClauseWithPreInit(C);
  Record.AddStmt(C->getNumThreads());
  Record.AddSourceLocation(C->getLParenLoc());
}

void OMPClauseWriter::VisitOMPSafelenClause(OMPSafelenClause *C) {
  Record.AddStmt(C->getSafelen());
  Record.AddSourceLocation(C->getLParenLoc());
}

void OMPClauseWriter::VisitOMPSimdlenClause(OMPSimdlenClause *C) {
  Record.AddStmt(C->getSimdlen());
  Record.AddSourceLocation(C->getLParenLoc());
}

void OMPClauseWriter::VisitOMPSizesClause(OMPSizesClause *C) {
  Record.push_back(C->getNumSizes());
  for (Expr *Size : C->getSizesRefs())
    Record.AddStmt(Size);
  Record.AddSourceLocation(C->getLParenLoc());
}

void OMPClauseWriter::VisitOMPFullClause(OMPFullClause *C) {}

void OMPClauseWriter::VisitOMPPartialClause(OMPPartialClause *C) {
  Record.AddStmt(C->getFactor());
  Record.AddSourceLocation(C->getLParenLoc());
}

void OMPClauseWriter::VisitOMPAllocatorClause(OMPAllocatorClause *C) {
  Record.AddStmt(C->getAllocator());
  Record.AddSourceLocation(C->getLParenLoc());
}

void OMPClauseWriter::VisitOMPCollapseClause(OMPCollapseClause *C) {
  Record.AddStmt(C->getNumForLoops());
  Record.AddSourceLocation(C->getLParenLoc());
}

void OMPClauseWriter::VisitOMPDetachClause(OMPDetachClause *C) {
  Record.AddStmt(C->getEventHandler());
  Record.AddSourceLocation(C->getLParenLoc());
}

void OMPClauseWriter::VisitOMPDefaultClause(OMPDefaultClause *C) {
  Record.push_back(unsigned(C->getDefaultKind()));
  Record.AddSourceLocation(C->getLParenLoc());
  Record.AddSourceLocation(C->getDefaultKindKwLoc());
}

void OMPClauseWriter::VisitOMPProcBindClause(OMPProcBindClause *C) {
  Record.push_back(unsigned(C->getProcBindKind()));
  Record.AddSourceLocation(C->getLParenLoc());
  Record.AddSourceLocation(C->getProcBindKindKwLoc());
}

void OMPClauseWriter::VisitOMPScheduleClause(OMPScheduleClause *C) {
  VisitOMPClauseWithPreInit(C);
  Record.push_back(C->getScheduleKind());
  Record.push_back(C->getFirstScheduleModifier());
  Record.push_back(C->getSecondScheduleModifier());
  Record.AddStmt(C->getChunkSize());
  Record.AddSourceLocation(C->getLParenLoc());
  Record.AddSourceLocation(C->getFirstScheduleModifierLoc());
  Record.AddSourceLocation(C->getSecondScheduleModifierLoc());
  Record.AddSourceLocation(C->getScheduleKindLoc());
  Record.AddSourceLocation(C->getCommaLoc());
}

void OMPClauseWriter::VisitOMPOrderedClause(OMPOrderedClause *C) {
  Record.push_back(C->getLoopNumIterations().size());
  Record.AddStmt(C->getNumForLoops());
  for (Expr *NumIter : C->getLoopNumIterations())
    Record.AddStmt(NumIter);
  for (unsigned I = 0, E = C->getLoopNumIterations().size(); I <E; ++I)
    Record.AddStmt(C->getLoopCounter(I));
  Record.AddSourceLocation(C->getLParenLoc());
}

void OMPClauseWriter::VisitOMPNowaitClause(OMPNowaitClause *) {}

void OMPClauseWriter::VisitOMPUntiedClause(OMPUntiedClause *) {}

void OMPClauseWriter::VisitOMPMergeableClause(OMPMergeableClause *) {}

void OMPClauseWriter::VisitOMPReadClause(OMPReadClause *) {}

void OMPClauseWriter::VisitOMPWriteClause(OMPWriteClause *) {}

void OMPClauseWriter::VisitOMPUpdateClause(OMPUpdateClause *C) {
  Record.push_back(C->isExtended() ? 1 : 0);
  if (C->isExtended()) {
    Record.AddSourceLocation(C->getLParenLoc());
    Record.AddSourceLocation(C->getArgumentLoc());
    Record.writeEnum(C->getDependencyKind());
  }
}

void OMPClauseWriter::VisitOMPCaptureClause(OMPCaptureClause *) {}

void OMPClauseWriter::VisitOMPCompareClause(OMPCompareClause *) {}

// Save the parameter of fail clause.
void OMPClauseWriter::VisitOMPFailClause(OMPFailClause *C) {
  Record.AddSourceLocation(C->getLParenLoc());
  Record.AddSourceLocation(C->getFailParameterLoc());
  Record.writeEnum(C->getFailParameter());
}

void OMPClauseWriter::VisitOMPSeqCstClause(OMPSeqCstClause *) {}

void OMPClauseWriter::VisitOMPAcqRelClause(OMPAcqRelClause *) {}

void OMPClauseWriter::VisitOMPAcquireClause(OMPAcquireClause *) {}

void OMPClauseWriter::VisitOMPReleaseClause(OMPReleaseClause *) {}

void OMPClauseWriter::VisitOMPRelaxedClause(OMPRelaxedClause *) {}

void OMPClauseWriter::VisitOMPWeakClause(OMPWeakClause *) {}

void OMPClauseWriter::VisitOMPThreadsClause(OMPThreadsClause *) {}

void OMPClauseWriter::VisitOMPSIMDClause(OMPSIMDClause *) {}

void OMPClauseWriter::VisitOMPNogroupClause(OMPNogroupClause *) {}

void OMPClauseWriter::VisitOMPInitClause(OMPInitClause *C) {
  Record.push_back(C->varlist_size());
  for (Expr *VE : C->varlists())
    Record.AddStmt(VE);
  Record.writeBool(C->getIsTarget());
  Record.writeBool(C->getIsTargetSync());
  Record.AddSourceLocation(C->getLParenLoc());
  Record.AddSourceLocation(C->getVarLoc());
}

void OMPClauseWriter::VisitOMPUseClause(OMPUseClause *C) {
  Record.AddStmt(C->getInteropVar());
  Record.AddSourceLocation(C->getLParenLoc());
  Record.AddSourceLocation(C->getVarLoc());
}

void OMPClauseWriter::VisitOMPDestroyClause(OMPDestroyClause *C) {
  Record.AddStmt(C->getInteropVar());
  Record.AddSourceLocation(C->getLParenLoc());
  Record.AddSourceLocation(C->getVarLoc());
}

void OMPClauseWriter::VisitOMPNovariantsClause(OMPNovariantsClause *C) {
  VisitOMPClauseWithPreInit(C);
  Record.AddStmt(C->getCondition());
  Record.AddSourceLocation(C->getLParenLoc());
}

void OMPClauseWriter::VisitOMPNocontextClause(OMPNocontextClause *C) {
  VisitOMPClauseWithPreInit(C);
  Record.AddStmt(C->getCondition());
  Record.AddSourceLocation(C->getLParenLoc());
}

void OMPClauseWriter::VisitOMPFilterClause(OMPFilterClause *C) {
  VisitOMPClauseWithPreInit(C);
  Record.AddStmt(C->getThreadID());
  Record.AddSourceLocation(C->getLParenLoc());
}

void OMPClauseWriter::VisitOMPAlignClause(OMPAlignClause *C) {
  Record.AddStmt(C->getAlignment());
  Record.AddSourceLocation(C->getLParenLoc());
}

void OMPClauseWriter::VisitOMPPrivateClause(OMPPrivateClause *C) {
  Record.push_back(C->varlist_size());
  Record.AddSourceLocation(C->getLParenLoc());
  for (auto *VE : C->varlists()) {
    Record.AddStmt(VE);
  }
  for (auto *VE : C->private_copies()) {
    Record.AddStmt(VE);
  }
}

void OMPClauseWriter::VisitOMPFirstprivateClause(OMPFirstprivateClause *C) {
  Record.push_back(C->varlist_size());
  VisitOMPClauseWithPreInit(C);
  Record.AddSourceLocation(C->getLParenLoc());
  for (auto *VE : C->varlists()) {
    Record.AddStmt(VE);
  }
  for (auto *VE : C->private_copies()) {
    Record.AddStmt(VE);
  }
  for (auto *VE : C->inits()) {
    Record.AddStmt(VE);
  }
}

void OMPClauseWriter::VisitOMPLastprivateClause(OMPLastprivateClause *C) {
  Record.push_back(C->varlist_size());
  VisitOMPClauseWithPostUpdate(C);
  Record.AddSourceLocation(C->getLParenLoc());
  Record.writeEnum(C->getKind());
  Record.AddSourceLocation(C->getKindLoc());
  Record.AddSourceLocation(C->getColonLoc());
  for (auto *VE : C->varlists())
    Record.AddStmt(VE);
  for (auto *E : C->private_copies())
    Record.AddStmt(E);
  for (auto *E : C->source_exprs())
    Record.AddStmt(E);
  for (auto *E : C->destination_exprs())
    Record.AddStmt(E);
  for (auto *E : C->assignment_ops())
    Record.AddStmt(E);
}

void OMPClauseWriter::VisitOMPSharedClause(OMPSharedClause *C) {
  Record.push_back(C->varlist_size());
  Record.AddSourceLocation(C->getLParenLoc());
  for (auto *VE : C->varlists())
    Record.AddStmt(VE);
}

void OMPClauseWriter::VisitOMPReductionClause(OMPReductionClause *C) {
  Record.push_back(C->varlist_size());
  Record.writeEnum(C->getModifier());
  VisitOMPClauseWithPostUpdate(C);
  Record.AddSourceLocation(C->getLParenLoc());
  Record.AddSourceLocation(C->getModifierLoc());
  Record.AddSourceLocation(C->getColonLoc());
  Record.AddNestedNameSpecifierLoc(C->getQualifierLoc());
  Record.AddDeclarationNameInfo(C->getNameInfo());
  for (auto *VE : C->varlists())
    Record.AddStmt(VE);
  for (auto *VE : C->privates())
    Record.AddStmt(VE);
  for (auto *E : C->lhs_exprs())
    Record.AddStmt(E);
  for (auto *E : C->rhs_exprs())
    Record.AddStmt(E);
  for (auto *E : C->reduction_ops())
    Record.AddStmt(E);
  if (C->getModifier() == clang::OMPC_REDUCTION_inscan) {
    for (auto *E : C->copy_ops())
      Record.AddStmt(E);
    for (auto *E : C->copy_array_temps())
      Record.AddStmt(E);
    for (auto *E : C->copy_array_elems())
      Record.AddStmt(E);
  }
}

void OMPClauseWriter::VisitOMPTaskReductionClause(OMPTaskReductionClause *C) {
  Record.push_back(C->varlist_size());
  VisitOMPClauseWithPostUpdate(C);
  Record.AddSourceLocation(C->getLParenLoc());
  Record.AddSourceLocation(C->getColonLoc());
  Record.AddNestedNameSpecifierLoc(C->getQualifierLoc());
  Record.AddDeclarationNameInfo(C->getNameInfo());
  for (auto *VE : C->varlists())
    Record.AddStmt(VE);
  for (auto *VE : C->privates())
    Record.AddStmt(VE);
  for (auto *E : C->lhs_exprs())
    Record.AddStmt(E);
  for (auto *E : C->rhs_exprs())
    Record.AddStmt(E);
  for (auto *E : C->reduction_ops())
    Record.AddStmt(E);
}

void OMPClauseWriter::VisitOMPInReductionClause(OMPInReductionClause *C) {
  Record.push_back(C->varlist_size());
  VisitOMPClauseWithPostUpdate(C);
  Record.AddSourceLocation(C->getLParenLoc());
  Record.AddSourceLocation(C->getColonLoc());
  Record.AddNestedNameSpecifierLoc(C->getQualifierLoc());
  Record.AddDeclarationNameInfo(C->getNameInfo());
  for (auto *VE : C->varlists())
    Record.AddStmt(VE);
  for (auto *VE : C->privates())
    Record.AddStmt(VE);
  for (auto *E : C->lhs_exprs())
    Record.AddStmt(E);
  for (auto *E : C->rhs_exprs())
    Record.AddStmt(E);
  for (auto *E : C->reduction_ops())
    Record.AddStmt(E);
  for (auto *E : C->taskgroup_descriptors())
    Record.AddStmt(E);
}

void OMPClauseWriter::VisitOMPLinearClause(OMPLinearClause *C) {
  Record.push_back(C->varlist_size());
  VisitOMPClauseWithPostUpdate(C);
  Record.AddSourceLocation(C->getLParenLoc());
  Record.AddSourceLocation(C->getColonLoc());
  Record.push_back(C->getModifier());
  Record.AddSourceLocation(C->getModifierLoc());
  for (auto *VE : C->varlists()) {
    Record.AddStmt(VE);
  }
  for (auto *VE : C->privates()) {
    Record.AddStmt(VE);
  }
  for (auto *VE : C->inits()) {
    Record.AddStmt(VE);
  }
  for (auto *VE : C->updates()) {
    Record.AddStmt(VE);
  }
  for (auto *VE : C->finals()) {
    Record.AddStmt(VE);
  }
  Record.AddStmt(C->getStep());
  Record.AddStmt(C->getCalcStep());
  for (auto *VE : C->used_expressions())
    Record.AddStmt(VE);
}

void OMPClauseWriter::VisitOMPAlignedClause(OMPAlignedClause *C) {
  Record.push_back(C->varlist_size());
  Record.AddSourceLocation(C->getLParenLoc());
  Record.AddSourceLocation(C->getColonLoc());
  for (auto *VE : C->varlists())
    Record.AddStmt(VE);
  Record.AddStmt(C->getAlignment());
}

void OMPClauseWriter::VisitOMPCopyinClause(OMPCopyinClause *C) {
  Record.push_back(C->varlist_size());
  Record.AddSourceLocation(C->getLParenLoc());
  for (auto *VE : C->varlists())
    Record.AddStmt(VE);
  for (auto *E : C->source_exprs())
    Record.AddStmt(E);
  for (auto *E : C->destination_exprs())
    Record.AddStmt(E);
  for (auto *E : C->assignment_ops())
    Record.AddStmt(E);
}

void OMPClauseWriter::VisitOMPCopyprivateClause(OMPCopyprivateClause *C) {
  Record.push_back(C->varlist_size());
  Record.AddSourceLocation(C->getLParenLoc());
  for (auto *VE : C->varlists())
    Record.AddStmt(VE);
  for (auto *E : C->source_exprs())
    Record.AddStmt(E);
  for (auto *E : C->destination_exprs())
    Record.AddStmt(E);
  for (auto *E : C->assignment_ops())
    Record.AddStmt(E);
}

void OMPClauseWriter::VisitOMPFlushClause(OMPFlushClause *C) {
  Record.push_back(C->varlist_size());
  Record.AddSourceLocation(C->getLParenLoc());
  for (auto *VE : C->varlists())
    Record.AddStmt(VE);
}

void OMPClauseWriter::VisitOMPDepobjClause(OMPDepobjClause *C) {
  Record.AddStmt(C->getDepobj());
  Record.AddSourceLocation(C->getLParenLoc());
}

void OMPClauseWriter::VisitOMPDependClause(OMPDependClause *C) {
  Record.push_back(C->varlist_size());
  Record.push_back(C->getNumLoops());
  Record.AddSourceLocation(C->getLParenLoc());
  Record.AddStmt(C->getModifier());
  Record.push_back(C->getDependencyKind());
  Record.AddSourceLocation(C->getDependencyLoc());
  Record.AddSourceLocation(C->getColonLoc());
  Record.AddSourceLocation(C->getOmpAllMemoryLoc());
  for (auto *VE : C->varlists())
    Record.AddStmt(VE);
  for (unsigned I = 0, E = C->getNumLoops(); I < E; ++I)
    Record.AddStmt(C->getLoopData(I));
}

void OMPClauseWriter::VisitOMPDeviceClause(OMPDeviceClause *C) {
  VisitOMPClauseWithPreInit(C);
  Record.writeEnum(C->getModifier());
  Record.AddStmt(C->getDevice());
  Record.AddSourceLocation(C->getModifierLoc());
  Record.AddSourceLocation(C->getLParenLoc());
}

void OMPClauseWriter::VisitOMPMapClause(OMPMapClause *C) {
  Record.push_back(C->varlist_size());
  Record.push_back(C->getUniqueDeclarationsNum());
  Record.push_back(C->getTotalComponentListNum());
  Record.push_back(C->getTotalComponentsNum());
  Record.AddSourceLocation(C->getLParenLoc());
  bool HasIteratorModifier = false;
  for (unsigned I = 0; I < NumberOfOMPMapClauseModifiers; ++I) {
    Record.push_back(C->getMapTypeModifier(I));
    Record.AddSourceLocation(C->getMapTypeModifierLoc(I));
    if (C->getMapTypeModifier(I) == OMPC_MAP_MODIFIER_iterator)
      HasIteratorModifier = true;
  }
  Record.AddNestedNameSpecifierLoc(C->getMapperQualifierLoc());
  Record.AddDeclarationNameInfo(C->getMapperIdInfo());
  Record.push_back(C->getMapType());
  Record.AddSourceLocation(C->getMapLoc());
  Record.AddSourceLocation(C->getColonLoc());
  for (auto *E : C->varlists())
    Record.AddStmt(E);
  for (auto *E : C->mapperlists())
    Record.AddStmt(E);
  if (HasIteratorModifier)
    Record.AddStmt(C->getIteratorModifier());
  for (auto *D : C->all_decls())
    Record.AddDeclRef(D);
  for (auto N : C->all_num_lists())
    Record.push_back(N);
  for (auto N : C->all_lists_sizes())
    Record.push_back(N);
  for (auto &M : C->all_components()) {
    Record.AddStmt(M.getAssociatedExpression());
    Record.AddDeclRef(M.getAssociatedDeclaration());
  }
}

void OMPClauseWriter::VisitOMPAllocateClause(OMPAllocateClause *C) {
  Record.push_back(C->varlist_size());
  Record.AddSourceLocation(C->getLParenLoc());
  Record.AddSourceLocation(C->getColonLoc());
  Record.AddStmt(C->getAllocator());
  for (auto *VE : C->varlists())
    Record.AddStmt(VE);
}

void OMPClauseWriter::VisitOMPNumTeamsClause(OMPNumTeamsClause *C) {
  VisitOMPClauseWithPreInit(C);
  Record.AddStmt(C->getNumTeams());
  Record.AddSourceLocation(C->getLParenLoc());
}

void OMPClauseWriter::VisitOMPThreadLimitClause(OMPThreadLimitClause *C) {
  VisitOMPClauseWithPreInit(C);
  Record.AddStmt(C->getThreadLimit());
  Record.AddSourceLocation(C->getLParenLoc());
}

void OMPClauseWriter::VisitOMPPriorityClause(OMPPriorityClause *C) {
  VisitOMPClauseWithPreInit(C);
  Record.AddStmt(C->getPriority());
  Record.AddSourceLocation(C->getLParenLoc());
}

void OMPClauseWriter::VisitOMPGrainsizeClause(OMPGrainsizeClause *C) {
  VisitOMPClauseWithPreInit(C);
  Record.writeEnum(C->getModifier());
  Record.AddStmt(C->getGrainsize());
  Record.AddSourceLocation(C->getModifierLoc());
  Record.AddSourceLocation(C->getLParenLoc());
}

void OMPClauseWriter::VisitOMPNumTasksClause(OMPNumTasksClause *C) {
  VisitOMPClauseWithPreInit(C);
  Record.writeEnum(C->getModifier());
  Record.AddStmt(C->getNumTasks());
  Record.AddSourceLocation(C->getModifierLoc());
  Record.AddSourceLocation(C->getLParenLoc());
}

void OMPClauseWriter::VisitOMPHintClause(OMPHintClause *C) {
  Record.AddStmt(C->getHint());
  Record.AddSourceLocation(C->getLParenLoc());
}

void OMPClauseWriter::VisitOMPDistScheduleClause(OMPDistScheduleClause *C) {
  VisitOMPClauseWithPreInit(C);
  Record.push_back(C->getDistScheduleKind());
  Record.AddStmt(C->getChunkSize());
  Record.AddSourceLocation(C->getLParenLoc());
  Record.AddSourceLocation(C->getDistScheduleKindLoc());
  Record.AddSourceLocation(C->getCommaLoc());
}

void OMPClauseWriter::VisitOMPDefaultmapClause(OMPDefaultmapClause *C) {
  Record.push_back(C->getDefaultmapKind());
  Record.push_back(C->getDefaultmapModifier());
  Record.AddSourceLocation(C->getLParenLoc());
  Record.AddSourceLocation(C->getDefaultmapModifierLoc());
  Record.AddSourceLocation(C->getDefaultmapKindLoc());
}

void OMPClauseWriter::VisitOMPToClause(OMPToClause *C) {
  Record.push_back(C->varlist_size());
  Record.push_back(C->getUniqueDeclarationsNum());
  Record.push_back(C->getTotalComponentListNum());
  Record.push_back(C->getTotalComponentsNum());
  Record.AddSourceLocation(C->getLParenLoc());
  for (unsigned I = 0; I < NumberOfOMPMotionModifiers; ++I) {
    Record.push_back(C->getMotionModifier(I));
    Record.AddSourceLocation(C->getMotionModifierLoc(I));
  }
  Record.AddNestedNameSpecifierLoc(C->getMapperQualifierLoc());
  Record.AddDeclarationNameInfo(C->getMapperIdInfo());
  Record.AddSourceLocation(C->getColonLoc());
  for (auto *E : C->varlists())
    Record.AddStmt(E);
  for (auto *E : C->mapperlists())
    Record.AddStmt(E);
  for (auto *D : C->all_decls())
    Record.AddDeclRef(D);
  for (auto N : C->all_num_lists())
    Record.push_back(N);
  for (auto N : C->all_lists_sizes())
    Record.push_back(N);
  for (auto &M : C->all_components()) {
    Record.AddStmt(M.getAssociatedExpression());
    Record.writeBool(M.isNonContiguous());
    Record.AddDeclRef(M.getAssociatedDeclaration());
  }
}

void OMPClauseWriter::VisitOMPFromClause(OMPFromClause *C) {
  Record.push_back(C->varlist_size());
  Record.push_back(C->getUniqueDeclarationsNum());
  Record.push_back(C->getTotalComponentListNum());
  Record.push_back(C->getTotalComponentsNum());
  Record.AddSourceLocation(C->getLParenLoc());
  for (unsigned I = 0; I < NumberOfOMPMotionModifiers; ++I) {
    Record.push_back(C->getMotionModifier(I));
    Record.AddSourceLocation(C->getMotionModifierLoc(I));
  }
  Record.AddNestedNameSpecifierLoc(C->getMapperQualifierLoc());
  Record.AddDeclarationNameInfo(C->getMapperIdInfo());
  Record.AddSourceLocation(C->getColonLoc());
  for (auto *E : C->varlists())
    Record.AddStmt(E);
  for (auto *E : C->mapperlists())
    Record.AddStmt(E);
  for (auto *D : C->all_decls())
    Record.AddDeclRef(D);
  for (auto N : C->all_num_lists())
    Record.push_back(N);
  for (auto N : C->all_lists_sizes())
    Record.push_back(N);
  for (auto &M : C->all_components()) {
    Record.AddStmt(M.getAssociatedExpression());
    Record.writeBool(M.isNonContiguous());
    Record.AddDeclRef(M.getAssociatedDeclaration());
  }
}

void OMPClauseWriter::VisitOMPUseDevicePtrClause(OMPUseDevicePtrClause *C) {
  Record.push_back(C->varlist_size());
  Record.push_back(C->getUniqueDeclarationsNum());
  Record.push_back(C->getTotalComponentListNum());
  Record.push_back(C->getTotalComponentsNum());
  Record.AddSourceLocation(C->getLParenLoc());
  for (auto *E : C->varlists())
    Record.AddStmt(E);
  for (auto *VE : C->private_copies())
    Record.AddStmt(VE);
  for (auto *VE : C->inits())
    Record.AddStmt(VE);
  for (auto *D : C->all_decls())
    Record.AddDeclRef(D);
  for (auto N : C->all_num_lists())
    Record.push_back(N);
  for (auto N : C->all_lists_sizes())
    Record.push_back(N);
  for (auto &M : C->all_components()) {
    Record.AddStmt(M.getAssociatedExpression());
    Record.AddDeclRef(M.getAssociatedDeclaration());
  }
}

void OMPClauseWriter::VisitOMPUseDeviceAddrClause(OMPUseDeviceAddrClause *C) {
  Record.push_back(C->varlist_size());
  Record.push_back(C->getUniqueDeclarationsNum());
  Record.push_back(C->getTotalComponentListNum());
  Record.push_back(C->getTotalComponentsNum());
  Record.AddSourceLocation(C->getLParenLoc());
  for (auto *E : C->varlists())
    Record.AddStmt(E);
  for (auto *D : C->all_decls())
    Record.AddDeclRef(D);
  for (auto N : C->all_num_lists())
    Record.push_back(N);
  for (auto N : C->all_lists_sizes())
    Record.push_back(N);
  for (auto &M : C->all_components()) {
    Record.AddStmt(M.getAssociatedExpression());
    Record.AddDeclRef(M.getAssociatedDeclaration());
  }
}

void OMPClauseWriter::VisitOMPIsDevicePtrClause(OMPIsDevicePtrClause *C) {
  Record.push_back(C->varlist_size());
  Record.push_back(C->getUniqueDeclarationsNum());
  Record.push_back(C->getTotalComponentListNum());
  Record.push_back(C->getTotalComponentsNum());
  Record.AddSourceLocation(C->getLParenLoc());
  for (auto *E : C->varlists())
    Record.AddStmt(E);
  for (auto *D : C->all_decls())
    Record.AddDeclRef(D);
  for (auto N : C->all_num_lists())
    Record.push_back(N);
  for (auto N : C->all_lists_sizes())
    Record.push_back(N);
  for (auto &M : C->all_components()) {
    Record.AddStmt(M.getAssociatedExpression());
    Record.AddDeclRef(M.getAssociatedDeclaration());
  }
}

void OMPClauseWriter::VisitOMPHasDeviceAddrClause(OMPHasDeviceAddrClause *C) {
  Record.push_back(C->varlist_size());
  Record.push_back(C->getUniqueDeclarationsNum());
  Record.push_back(C->getTotalComponentListNum());
  Record.push_back(C->getTotalComponentsNum());
  Record.AddSourceLocation(C->getLParenLoc());
  for (auto *E : C->varlists())
    Record.AddStmt(E);
  for (auto *D : C->all_decls())
    Record.AddDeclRef(D);
  for (auto N : C->all_num_lists())
    Record.push_back(N);
  for (auto N : C->all_lists_sizes())
    Record.push_back(N);
  for (auto &M : C->all_components()) {
    Record.AddStmt(M.getAssociatedExpression());
    Record.AddDeclRef(M.getAssociatedDeclaration());
  }
}

void OMPClauseWriter::VisitOMPUnifiedAddressClause(OMPUnifiedAddressClause *) {}

void OMPClauseWriter::VisitOMPUnifiedSharedMemoryClause(
    OMPUnifiedSharedMemoryClause *) {}

void OMPClauseWriter::VisitOMPReverseOffloadClause(OMPReverseOffloadClause *) {}

void
OMPClauseWriter::VisitOMPDynamicAllocatorsClause(OMPDynamicAllocatorsClause *) {
}

void OMPClauseWriter::VisitOMPAtomicDefaultMemOrderClause(
    OMPAtomicDefaultMemOrderClause *C) {
  Record.push_back(C->getAtomicDefaultMemOrderKind());
  Record.AddSourceLocation(C->getLParenLoc());
  Record.AddSourceLocation(C->getAtomicDefaultMemOrderKindKwLoc());
}

void OMPClauseWriter::VisitOMPAtClause(OMPAtClause *C) {
  Record.push_back(C->getAtKind());
  Record.AddSourceLocation(C->getLParenLoc());
  Record.AddSourceLocation(C->getAtKindKwLoc());
}

void OMPClauseWriter::VisitOMPSeverityClause(OMPSeverityClause *C) {
  Record.push_back(C->getSeverityKind());
  Record.AddSourceLocation(C->getLParenLoc());
  Record.AddSourceLocation(C->getSeverityKindKwLoc());
}

void OMPClauseWriter::VisitOMPMessageClause(OMPMessageClause *C) {
  Record.AddStmt(C->getMessageString());
  Record.AddSourceLocation(C->getLParenLoc());
}

void OMPClauseWriter::VisitOMPNontemporalClause(OMPNontemporalClause *C) {
  Record.push_back(C->varlist_size());
  Record.AddSourceLocation(C->getLParenLoc());
  for (auto *VE : C->varlists())
    Record.AddStmt(VE);
  for (auto *E : C->private_refs())
    Record.AddStmt(E);
}

void OMPClauseWriter::VisitOMPInclusiveClause(OMPInclusiveClause *C) {
  Record.push_back(C->varlist_size());
  Record.AddSourceLocation(C->getLParenLoc());
  for (auto *VE : C->varlists())
    Record.AddStmt(VE);
}

void OMPClauseWriter::VisitOMPExclusiveClause(OMPExclusiveClause *C) {
  Record.push_back(C->varlist_size());
  Record.AddSourceLocation(C->getLParenLoc());
  for (auto *VE : C->varlists())
    Record.AddStmt(VE);
}

void OMPClauseWriter::VisitOMPOrderClause(OMPOrderClause *C) {
  Record.writeEnum(C->getKind());
  Record.writeEnum(C->getModifier());
  Record.AddSourceLocation(C->getLParenLoc());
  Record.AddSourceLocation(C->getKindKwLoc());
  Record.AddSourceLocation(C->getModifierKwLoc());
}

void OMPClauseWriter::VisitOMPUsesAllocatorsClause(OMPUsesAllocatorsClause *C) {
  Record.push_back(C->getNumberOfAllocators());
  Record.AddSourceLocation(C->getLParenLoc());
  for (unsigned I = 0, E = C->getNumberOfAllocators(); I < E; ++I) {
    OMPUsesAllocatorsClause::Data Data = C->getAllocatorData(I);
    Record.AddStmt(Data.Allocator);
    Record.AddStmt(Data.AllocatorTraits);
    Record.AddSourceLocation(Data.LParenLoc);
    Record.AddSourceLocation(Data.RParenLoc);
  }
}

void OMPClauseWriter::VisitOMPAffinityClause(OMPAffinityClause *C) {
  Record.push_back(C->varlist_size());
  Record.AddSourceLocation(C->getLParenLoc());
  Record.AddStmt(C->getModifier());
  Record.AddSourceLocation(C->getColonLoc());
  for (Expr *E : C->varlists())
    Record.AddStmt(E);
}

void OMPClauseWriter::VisitOMPBindClause(OMPBindClause *C) {
  Record.writeEnum(C->getBindKind());
  Record.AddSourceLocation(C->getLParenLoc());
  Record.AddSourceLocation(C->getBindKindLoc());
}

void OMPClauseWriter::VisitOMPXDynCGroupMemClause(OMPXDynCGroupMemClause *C) {
  VisitOMPClauseWithPreInit(C);
  Record.AddStmt(C->getSize());
  Record.AddSourceLocation(C->getLParenLoc());
}

void OMPClauseWriter::VisitOMPDoacrossClause(OMPDoacrossClause *C) {
  Record.push_back(C->varlist_size());
  Record.push_back(C->getNumLoops());
  Record.AddSourceLocation(C->getLParenLoc());
  Record.push_back(C->getDependenceType());
  Record.AddSourceLocation(C->getDependenceLoc());
  Record.AddSourceLocation(C->getColonLoc());
  for (auto *VE : C->varlists())
    Record.AddStmt(VE);
  for (unsigned I = 0, E = C->getNumLoops(); I < E; ++I)
    Record.AddStmt(C->getLoopData(I));
}

void OMPClauseWriter::VisitOMPXAttributeClause(OMPXAttributeClause *C) {
  Record.AddAttributes(C->getAttrs());
  Record.AddSourceLocation(C->getBeginLoc());
  Record.AddSourceLocation(C->getLParenLoc());
  Record.AddSourceLocation(C->getEndLoc());
}

void OMPClauseWriter::VisitOMPXBareClause(OMPXBareClause *C) {}

void ASTRecordWriter::writeOMPTraitInfo(const OMPTraitInfo *TI) {
  writeUInt32(TI->Sets.size());
  for (const auto &Set : TI->Sets) {
    writeEnum(Set.Kind);
    writeUInt32(Set.Selectors.size());
    for (const auto &Selector : Set.Selectors) {
      writeEnum(Selector.Kind);
      writeBool(Selector.ScoreOrCondition);
      if (Selector.ScoreOrCondition)
        writeExprRef(Selector.ScoreOrCondition);
      writeUInt32(Selector.Properties.size());
      for (const auto &Property : Selector.Properties)
        writeEnum(Property.Kind);
    }
  }
}

void ASTRecordWriter::writeOMPChildren(OMPChildren *Data) {
  if (!Data)
    return;
  writeUInt32(Data->getNumClauses());
  writeUInt32(Data->getNumChildren());
  writeBool(Data->hasAssociatedStmt());
  for (unsigned I = 0, E = Data->getNumClauses(); I < E; ++I)
    writeOMPClause(Data->getClauses()[I]);
  if (Data->hasAssociatedStmt())
    AddStmt(Data->getAssociatedStmt());
  for (unsigned I = 0, E = Data->getNumChildren(); I < E; ++I)
    AddStmt(Data->getChildren()[I]);
}

void ASTRecordWriter::writeOpenACCVarList(const OpenACCClauseWithVarList *C) {
  writeUInt32(C->getVarList().size());
  for (Expr *E : C->getVarList())
    AddStmt(E);
}

void ASTRecordWriter::writeOpenACCIntExprList(ArrayRef<Expr *> Exprs) {
  writeUInt32(Exprs.size());
  for (Expr *E : Exprs)
    AddStmt(E);
}

void ASTRecordWriter::writeOpenACCClause(const OpenACCClause *C) {
  writeEnum(C->getClauseKind());
  writeSourceLocation(C->getBeginLoc());
  writeSourceLocation(C->getEndLoc());

  switch (C->getClauseKind()) {
  case OpenACCClauseKind::Default: {
    const auto *DC = cast<OpenACCDefaultClause>(C);
    writeSourceLocation(DC->getLParenLoc());
    writeEnum(DC->getDefaultClauseKind());
    return;
  }
  case OpenACCClauseKind::If: {
    const auto *IC = cast<OpenACCIfClause>(C);
    writeSourceLocation(IC->getLParenLoc());
    AddStmt(const_cast<Expr*>(IC->getConditionExpr()));
    return;
  }
  case OpenACCClauseKind::Self: {
    const auto *SC = cast<OpenACCSelfClause>(C);
    writeSourceLocation(SC->getLParenLoc());
    writeBool(SC->hasConditionExpr());
    if (SC->hasConditionExpr())
      AddStmt(const_cast<Expr*>(SC->getConditionExpr()));
    return;
  }
  case OpenACCClauseKind::NumGangs: {
    const auto *NGC = cast<OpenACCNumGangsClause>(C);
    writeSourceLocation(NGC->getLParenLoc());
    writeUInt32(NGC->getIntExprs().size());
    for (Expr *E : NGC->getIntExprs())
      AddStmt(E);
    return;
  }
  case OpenACCClauseKind::NumWorkers: {
    const auto *NWC = cast<OpenACCNumWorkersClause>(C);
    writeSourceLocation(NWC->getLParenLoc());
    AddStmt(const_cast<Expr*>(NWC->getIntExpr()));
    return;
  }
  case OpenACCClauseKind::VectorLength: {
    const auto *NWC = cast<OpenACCVectorLengthClause>(C);
    writeSourceLocation(NWC->getLParenLoc());
    AddStmt(const_cast<Expr*>(NWC->getIntExpr()));
    return;
  }
  case OpenACCClauseKind::Private: {
    const auto *PC = cast<OpenACCPrivateClause>(C);
    writeSourceLocation(PC->getLParenLoc());
    writeOpenACCVarList(PC);
    return;
  }
  case OpenACCClauseKind::FirstPrivate: {
    const auto *FPC = cast<OpenACCFirstPrivateClause>(C);
    writeSourceLocation(FPC->getLParenLoc());
    writeOpenACCVarList(FPC);
    return;
  }
  case OpenACCClauseKind::Attach: {
    const auto *AC = cast<OpenACCAttachClause>(C);
    writeSourceLocation(AC->getLParenLoc());
    writeOpenACCVarList(AC);
    return;
  }
  case OpenACCClauseKind::DevicePtr: {
    const auto *DPC = cast<OpenACCDevicePtrClause>(C);
    writeSourceLocation(DPC->getLParenLoc());
    writeOpenACCVarList(DPC);
    return;
  }
  case OpenACCClauseKind::NoCreate: {
    const auto *NCC = cast<OpenACCNoCreateClause>(C);
    writeSourceLocation(NCC->getLParenLoc());
    writeOpenACCVarList(NCC);
    return;
  }
  case OpenACCClauseKind::Present: {
    const auto *PC = cast<OpenACCPresentClause>(C);
    writeSourceLocation(PC->getLParenLoc());
    writeOpenACCVarList(PC);
    return;
  }
  case OpenACCClauseKind::Copy:
  case OpenACCClauseKind::PCopy:
  case OpenACCClauseKind::PresentOrCopy: {
    const auto *CC = cast<OpenACCCopyClause>(C);
    writeSourceLocation(CC->getLParenLoc());
    writeOpenACCVarList(CC);
    return;
  }
  case OpenACCClauseKind::CopyIn:
  case OpenACCClauseKind::PCopyIn:
  case OpenACCClauseKind::PresentOrCopyIn: {
    const auto *CIC = cast<OpenACCCopyInClause>(C);
    writeSourceLocation(CIC->getLParenLoc());
    writeBool(CIC->isReadOnly());
    writeOpenACCVarList(CIC);
    return;
  }
  case OpenACCClauseKind::CopyOut:
  case OpenACCClauseKind::PCopyOut:
  case OpenACCClauseKind::PresentOrCopyOut: {
    const auto *COC = cast<OpenACCCopyOutClause>(C);
    writeSourceLocation(COC->getLParenLoc());
    writeBool(COC->isZero());
    writeOpenACCVarList(COC);
    return;
  }
  case OpenACCClauseKind::Create:
  case OpenACCClauseKind::PCreate:
  case OpenACCClauseKind::PresentOrCreate: {
    const auto *CC = cast<OpenACCCreateClause>(C);
    writeSourceLocation(CC->getLParenLoc());
    writeBool(CC->isZero());
    writeOpenACCVarList(CC);
    return;
  }
  case OpenACCClauseKind::Async: {
    const auto *AC = cast<OpenACCAsyncClause>(C);
    writeSourceLocation(AC->getLParenLoc());
    writeBool(AC->hasIntExpr());
    if (AC->hasIntExpr())
      AddStmt(const_cast<Expr*>(AC->getIntExpr()));
    return;
  }
  case OpenACCClauseKind::Wait: {
    const auto *WC = cast<OpenACCWaitClause>(C);
    writeSourceLocation(WC->getLParenLoc());
    writeBool(WC->getDevNumExpr());
    if (Expr *DNE = WC->getDevNumExpr())
      AddStmt(DNE);
    writeSourceLocation(WC->getQueuesLoc());

    writeOpenACCIntExprList(WC->getQueueIdExprs());
    return;
  }
  case OpenACCClauseKind::DeviceType:
  case OpenACCClauseKind::DType: {
    const auto *DTC = cast<OpenACCDeviceTypeClause>(C);
    writeSourceLocation(DTC->getLParenLoc());
    writeUInt32(DTC->getArchitectures().size());
    for (const DeviceTypeArgument &Arg : DTC->getArchitectures()) {
      writeBool(Arg.first);
      if (Arg.first)
        AddIdentifierRef(Arg.first);
      writeSourceLocation(Arg.second);
    }
    return;
  }
  case OpenACCClauseKind::Reduction: {
    const auto *RC = cast<OpenACCReductionClause>(C);
    writeSourceLocation(RC->getLParenLoc());
    writeEnum(RC->getReductionOp());
    writeOpenACCVarList(RC);
    return;
  }
  case OpenACCClauseKind::Seq:
  case OpenACCClauseKind::Independent:
  case OpenACCClauseKind::Auto:
    // Nothing to do here, there is no additional information beyond the
    // begin/end loc and clause kind.
    return;

  case OpenACCClauseKind::Finalize:
  case OpenACCClauseKind::IfPresent:
  case OpenACCClauseKind::Worker:
  case OpenACCClauseKind::Vector:
  case OpenACCClauseKind::NoHost:
  case OpenACCClauseKind::UseDevice:
  case OpenACCClauseKind::Delete:
  case OpenACCClauseKind::Detach:
  case OpenACCClauseKind::Device:
  case OpenACCClauseKind::DeviceResident:
  case OpenACCClauseKind::Host:
  case OpenACCClauseKind::Link:
  case OpenACCClauseKind::Collapse:
  case OpenACCClauseKind::Bind:
  case OpenACCClauseKind::DeviceNum:
  case OpenACCClauseKind::DefaultAsync:
  case OpenACCClauseKind::Tile:
  case OpenACCClauseKind::Gang:
  case OpenACCClauseKind::Invalid:
    llvm_unreachable("Clause serialization not yet implemented");
  }
  llvm_unreachable("Invalid Clause Kind");
}

void ASTRecordWriter::writeOpenACCClauseList(
    ArrayRef<const OpenACCClause *> Clauses) {
  for (const OpenACCClause *Clause : Clauses)
    writeOpenACCClause(Clause);
}
