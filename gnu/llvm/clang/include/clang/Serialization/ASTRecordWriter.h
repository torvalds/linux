//===- ASTRecordWriter.h - Helper classes for writing AST -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ASTRecordWriter class, a helper class useful
//  when serializing AST.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SERIALIZATION_ASTRECORDWRITER_H
#define LLVM_CLANG_SERIALIZATION_ASTRECORDWRITER_H

#include "clang/AST/AbstractBasicWriter.h"
#include "clang/AST/OpenACCClause.h"
#include "clang/AST/OpenMPClause.h"
#include "clang/Serialization/ASTWriter.h"
#include "clang/Serialization/SourceLocationEncoding.h"

namespace clang {

class OpenACCClause;
class TypeLoc;

/// An object for streaming information to a record.
class ASTRecordWriter
    : public serialization::DataStreamBasicWriter<ASTRecordWriter> {
  using LocSeq = SourceLocationSequence;

  ASTWriter *Writer;
  ASTWriter::RecordDataImpl *Record;

  /// Statements that we've encountered while serializing a
  /// declaration or type.
  SmallVector<Stmt *, 16> StmtsToEmit;

  /// Indices of record elements that describe offsets within the
  /// bitcode. These will be converted to offsets relative to the current
  /// record when emitted.
  SmallVector<unsigned, 8> OffsetIndices;

  /// Flush all of the statements and expressions that have
  /// been added to the queue via AddStmt().
  void FlushStmts();
  void FlushSubStmts();

  void PrepareToEmit(uint64_t MyOffset) {
    // Convert offsets into relative form.
    for (unsigned I : OffsetIndices) {
      auto &StoredOffset = (*Record)[I];
      assert(StoredOffset < MyOffset && "invalid offset");
      if (StoredOffset)
        StoredOffset = MyOffset - StoredOffset;
    }
    OffsetIndices.clear();
  }

public:
  /// Construct a ASTRecordWriter that uses the default encoding scheme.
  ASTRecordWriter(ASTWriter &W, ASTWriter::RecordDataImpl &Record)
      : DataStreamBasicWriter(W.getASTContext()), Writer(&W), Record(&Record) {}

  /// Construct a ASTRecordWriter that uses the same encoding scheme as another
  /// ASTRecordWriter.
  ASTRecordWriter(ASTRecordWriter &Parent, ASTWriter::RecordDataImpl &Record)
      : DataStreamBasicWriter(Parent.getASTContext()), Writer(Parent.Writer),
        Record(&Record) {}

  /// Copying an ASTRecordWriter is almost certainly a bug.
  ASTRecordWriter(const ASTRecordWriter &) = delete;
  ASTRecordWriter &operator=(const ASTRecordWriter &) = delete;

  /// Extract the underlying record storage.
  ASTWriter::RecordDataImpl &getRecordData() const { return *Record; }

  /// Minimal vector-like interface.
  /// @{
  void push_back(uint64_t N) { Record->push_back(N); }
  template<typename InputIterator>
  void append(InputIterator begin, InputIterator end) {
    Record->append(begin, end);
  }
  bool empty() const { return Record->empty(); }
  size_t size() const { return Record->size(); }
  uint64_t &operator[](size_t N) { return (*Record)[N]; }
  /// @}

  /// Emit the record to the stream, followed by its substatements, and
  /// return its offset.
  // FIXME: Allow record producers to suggest Abbrevs.
  uint64_t Emit(unsigned Code, unsigned Abbrev = 0) {
    uint64_t Offset = Writer->Stream.GetCurrentBitNo();
    PrepareToEmit(Offset);
    Writer->Stream.EmitRecord(Code, *Record, Abbrev);
    FlushStmts();
    return Offset;
  }

  /// Emit the record to the stream, preceded by its substatements.
  uint64_t EmitStmt(unsigned Code, unsigned Abbrev = 0) {
    FlushSubStmts();
    PrepareToEmit(Writer->Stream.GetCurrentBitNo());
    Writer->Stream.EmitRecord(Code, *Record, Abbrev);
    return Writer->Stream.GetCurrentBitNo();
  }

  /// Add a bit offset into the record. This will be converted into an
  /// offset relative to the current record when emitted.
  void AddOffset(uint64_t BitOffset) {
    OffsetIndices.push_back(Record->size());
    Record->push_back(BitOffset);
  }

  /// Add the given statement or expression to the queue of
  /// statements to emit.
  ///
  /// This routine should be used when emitting types and declarations
  /// that have expressions as part of their formulation. Once the
  /// type or declaration has been written, Emit() will write
  /// the corresponding statements just after the record.
  void AddStmt(Stmt *S) {
    StmtsToEmit.push_back(S);
  }
  void writeStmtRef(const Stmt *S) {
    AddStmt(const_cast<Stmt*>(S));
  }

  /// Write an BTFTypeTagAttr object.
  void writeBTFTypeTagAttr(const BTFTypeTagAttr *A) { AddAttr(A); }

  /// Add a definition for the given function to the queue of statements
  /// to emit.
  void AddFunctionDefinition(const FunctionDecl *FD);

  /// Emit a source location.
  void AddSourceLocation(SourceLocation Loc, LocSeq *Seq = nullptr) {
    return Writer->AddSourceLocation(Loc, *Record, Seq);
  }
  void writeSourceLocation(SourceLocation Loc) {
    AddSourceLocation(Loc);
  }

  void writeTypeCoupledDeclRefInfo(TypeCoupledDeclRefInfo Info) {
    writeDeclRef(Info.getDecl());
    writeBool(Info.isDeref());
  }

  /// Emit a source range.
  void AddSourceRange(SourceRange Range, LocSeq *Seq = nullptr) {
    return Writer->AddSourceRange(Range, *Record, Seq);
  }

  void writeBool(bool Value) {
    Record->push_back(Value);
  }

  void writeUInt32(uint32_t Value) {
    Record->push_back(Value);
  }

  void writeUInt64(uint64_t Value) {
    Record->push_back(Value);
  }

  /// Emit an integral value.
  void AddAPInt(const llvm::APInt &Value) {
    writeAPInt(Value);
  }

  /// Emit a signed integral value.
  void AddAPSInt(const llvm::APSInt &Value) {
    writeAPSInt(Value);
  }

  /// Emit a floating-point value.
  void AddAPFloat(const llvm::APFloat &Value);

  /// Emit an APvalue.
  void AddAPValue(const APValue &Value) { writeAPValue(Value); }

  /// Emit a reference to an identifier.
  void AddIdentifierRef(const IdentifierInfo *II) {
    return Writer->AddIdentifierRef(II, *Record);
  }
  void writeIdentifier(const IdentifierInfo *II) {
    AddIdentifierRef(II);
  }

  /// Emit a Selector (which is a smart pointer reference).
  void AddSelectorRef(Selector S);
  void writeSelector(Selector sel) {
    AddSelectorRef(sel);
  }

  /// Emit a CXXTemporary.
  void AddCXXTemporary(const CXXTemporary *Temp);

  /// Emit a C++ base specifier.
  void AddCXXBaseSpecifier(const CXXBaseSpecifier &Base);

  /// Emit a set of C++ base specifiers.
  void AddCXXBaseSpecifiers(ArrayRef<CXXBaseSpecifier> Bases);

  /// Emit a reference to a type.
  void AddTypeRef(QualType T) {
    return Writer->AddTypeRef(T, *Record);
  }
  void writeQualType(QualType T) {
    AddTypeRef(T);
  }

  /// Emits a reference to a declarator info.
  void AddTypeSourceInfo(TypeSourceInfo *TInfo);

  /// Emits source location information for a type. Does not emit the type.
  void AddTypeLoc(TypeLoc TL, LocSeq *Seq = nullptr);

  /// Emits a template argument location info.
  void AddTemplateArgumentLocInfo(TemplateArgument::ArgKind Kind,
                                  const TemplateArgumentLocInfo &Arg);

  /// Emits a template argument location.
  void AddTemplateArgumentLoc(const TemplateArgumentLoc &Arg);

  /// Emits an AST template argument list info.
  void AddASTTemplateArgumentListInfo(
      const ASTTemplateArgumentListInfo *ASTTemplArgList);

  // Emits a concept reference.
  void AddConceptReference(const ConceptReference *CR);

  /// Emit a reference to a declaration.
  void AddDeclRef(const Decl *D) {
    return Writer->AddDeclRef(D, *Record);
  }
  void writeDeclRef(const Decl *D) {
    AddDeclRef(D);
  }

  /// Emit a declaration name.
  void AddDeclarationName(DeclarationName Name) {
    writeDeclarationName(Name);
  }

  void AddDeclarationNameLoc(const DeclarationNameLoc &DNLoc,
                             DeclarationName Name);
  void AddDeclarationNameInfo(const DeclarationNameInfo &NameInfo);

  void AddQualifierInfo(const QualifierInfo &Info);

  /// Emit a nested name specifier.
  void AddNestedNameSpecifier(NestedNameSpecifier *NNS) {
    writeNestedNameSpecifier(NNS);
  }

  /// Emit a nested name specifier with source-location information.
  void AddNestedNameSpecifierLoc(NestedNameSpecifierLoc NNS);

  /// Emit a template name.
  void AddTemplateName(TemplateName Name) {
    writeTemplateName(Name);
  }

  /// Emit a template argument.
  void AddTemplateArgument(const TemplateArgument &Arg) {
    writeTemplateArgument(Arg);
  }

  /// Emit a template parameter list.
  void AddTemplateParameterList(const TemplateParameterList *TemplateParams);

  /// Emit a template argument list.
  void AddTemplateArgumentList(const TemplateArgumentList *TemplateArgs);

  /// Emit a UnresolvedSet structure.
  void AddUnresolvedSet(const ASTUnresolvedSet &Set);

  /// Emit a CXXCtorInitializer array.
  void AddCXXCtorInitializers(ArrayRef<CXXCtorInitializer *> CtorInits);

  void AddCXXDefinitionData(const CXXRecordDecl *D);

  /// Emit information about the initializer of a VarDecl.
  void AddVarDeclInit(const VarDecl *VD);

  /// Write an OMPTraitInfo object.
  void writeOMPTraitInfo(const OMPTraitInfo *TI);

  void writeOMPClause(OMPClause *C);

  /// Writes data related to the OpenMP directives.
  void writeOMPChildren(OMPChildren *Data);

  void writeOpenACCVarList(const OpenACCClauseWithVarList *C);

  void writeOpenACCIntExprList(ArrayRef<Expr *> Exprs);

  /// Writes out a single OpenACC Clause.
  void writeOpenACCClause(const OpenACCClause *C);

  /// Writes out a list of OpenACC clauses.
  void writeOpenACCClauseList(ArrayRef<const OpenACCClause *> Clauses);

  /// Emit a string.
  void AddString(StringRef Str) {
    return Writer->AddString(Str, *Record);
  }

  /// Emit a path.
  void AddPath(StringRef Path) {
    return Writer->AddPath(Path, *Record);
  }

  /// Emit a version tuple.
  void AddVersionTuple(const VersionTuple &Version) {
    return Writer->AddVersionTuple(Version, *Record);
  }

  // Emit an attribute.
  void AddAttr(const Attr *A);

  /// Emit a list of attributes.
  void AddAttributes(ArrayRef<const Attr*> Attrs);
};

} // end namespace clang

#endif
