//===- ASTRecordReader.h - Helper classes for reading AST -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines classes that are useful in the implementation of
//  the ASTReader.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SERIALIZATION_ASTRECORDREADER_H
#define LLVM_CLANG_SERIALIZATION_ASTRECORDREADER_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/AbstractBasicReader.h"
#include "clang/Lex/Token.h"
#include "clang/Serialization/ASTReader.h"
#include "clang/Serialization/SourceLocationEncoding.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/APSInt.h"

namespace clang {
class OpenACCClause;
class OMPTraitInfo;
class OMPChildren;

/// An object for streaming information from a record.
class ASTRecordReader
    : public serialization::DataStreamBasicReader<ASTRecordReader> {
  using ModuleFile = serialization::ModuleFile;
  using LocSeq = SourceLocationSequence;

  ASTReader *Reader;
  ModuleFile *F;
  unsigned Idx = 0;
  ASTReader::RecordData Record;

  using RecordData = ASTReader::RecordData;
  using RecordDataImpl = ASTReader::RecordDataImpl;

public:
  /// Construct an ASTRecordReader that uses the default encoding scheme.
  ASTRecordReader(ASTReader &Reader, ModuleFile &F)
    : DataStreamBasicReader(Reader.getContext()), Reader(&Reader), F(&F) {}

  /// Reads a record with id AbbrevID from Cursor, resetting the
  /// internal state.
  Expected<unsigned> readRecord(llvm::BitstreamCursor &Cursor,
                                unsigned AbbrevID);

  /// Is this a module file for a module (rather than a PCH or similar).
  bool isModule() const { return F->isModule(); }

  /// Retrieve the AST context that this AST reader supplements.
  ASTContext &getContext() { return Reader->getContext(); }

  /// The current position in this record.
  unsigned getIdx() const { return Idx; }

  /// The length of this record.
  size_t size() const { return Record.size(); }

  /// An arbitrary index in this record.
  const uint64_t &operator[](size_t N) { return Record[N]; }

  /// Returns the last value in this record.
  uint64_t back() { return Record.back(); }

  /// Returns the current value in this record, and advances to the
  /// next value.
  uint64_t readInt() { return Record[Idx++]; }

  ArrayRef<uint64_t> readIntArray(unsigned Len) {
    auto Array = llvm::ArrayRef(Record).slice(Idx, Len);
    Idx += Len;
    return Array;
  }

  /// Returns the current value in this record, without advancing.
  uint64_t peekInt() { return Record[Idx]; }

  /// Skips the specified number of values.
  void skipInts(unsigned N) { Idx += N; }

  /// Retrieve the global submodule ID its local ID number.
  serialization::SubmoduleID
  getGlobalSubmoduleID(unsigned LocalID) {
    return Reader->getGlobalSubmoduleID(*F, LocalID);
  }

  /// Retrieve the submodule that corresponds to a global submodule ID.
  Module *getSubmodule(serialization::SubmoduleID GlobalID) {
    return Reader->getSubmodule(GlobalID);
  }

  /// Read the record that describes the lexical contents of a DC.
  bool readLexicalDeclContextStorage(uint64_t Offset, DeclContext *DC) {
    return Reader->ReadLexicalDeclContextStorage(*F, F->DeclsCursor, Offset,
                                                 DC);
  }

  ExplicitSpecifier readExplicitSpec() {
    uint64_t Kind = readInt();
    bool HasExpr = Kind & 0x1;
    Kind = Kind >> 1;
    return ExplicitSpecifier(HasExpr ? readExpr() : nullptr,
                             static_cast<ExplicitSpecKind>(Kind));
  }

  /// Read information about an exception specification (inherited).
  //FunctionProtoType::ExceptionSpecInfo
  //readExceptionSpecInfo(SmallVectorImpl<QualType> &ExceptionStorage);

  /// Get the global offset corresponding to a local offset.
  uint64_t getGlobalBitOffset(uint64_t LocalOffset) {
    return Reader->getGlobalBitOffset(*F, LocalOffset);
  }

  /// Reads a statement.
  Stmt *readStmt() { return Reader->ReadStmt(*F); }
  Stmt *readStmtRef() { return readStmt(); /* FIXME: readSubStmt? */ }

  /// Reads an expression.
  Expr *readExpr() { return Reader->ReadExpr(*F); }

  /// Reads a sub-statement operand during statement reading.
  Stmt *readSubStmt() { return Reader->ReadSubStmt(); }

  /// Reads a sub-expression operand during statement reading.
  Expr *readSubExpr() { return Reader->ReadSubExpr(); }

  /// Reads a declaration with the given local ID in the given module.
  ///
  /// \returns The requested declaration, casted to the given return type.
  template <typename T> T *GetLocalDeclAs(LocalDeclID LocalID) {
    return cast_or_null<T>(Reader->GetLocalDecl(*F, LocalID));
  }

  /// Reads a TemplateArgumentLocInfo appropriate for the
  /// given TemplateArgument kind, advancing Idx.
  TemplateArgumentLocInfo
  readTemplateArgumentLocInfo(TemplateArgument::ArgKind Kind);

  /// Reads a TemplateArgumentLoc, advancing Idx.
  TemplateArgumentLoc readTemplateArgumentLoc();

  void readTemplateArgumentListInfo(TemplateArgumentListInfo &Result);

  const ASTTemplateArgumentListInfo*
  readASTTemplateArgumentListInfo();

  // Reads a concept reference from the given record.
  ConceptReference *readConceptReference();

  /// Reads a declarator info from the given record, advancing Idx.
  TypeSourceInfo *readTypeSourceInfo();

  /// Reads the location information for a type.
  void readTypeLoc(TypeLoc TL, LocSeq *Seq = nullptr);

  /// Map a local type ID within a given AST file to a global type ID.
  serialization::TypeID getGlobalTypeID(serialization::TypeID LocalID) const {
    return Reader->getGlobalTypeID(*F, LocalID);
  }

  Qualifiers readQualifiers() {
    return Qualifiers::fromOpaqueValue(readInt());
  }

  /// Read a type from the current position in the record.
  QualType readType() {
    return Reader->readType(*F, Record, Idx);
  }
  QualType readQualType() {
    return readType();
  }

  /// Reads a declaration ID from the given position in this record.
  ///
  /// \returns The declaration ID read from the record, adjusted to a global ID.
  GlobalDeclID readDeclID() { return Reader->ReadDeclID(*F, Record, Idx); }

  /// Reads a declaration from the given position in a record in the
  /// given module, advancing Idx.
  Decl *readDecl() {
    return Reader->ReadDecl(*F, Record, Idx);
  }
  Decl *readDeclRef() {
    return readDecl();
  }

  /// Reads a declaration from the given position in the record,
  /// advancing Idx.
  ///
  /// \returns The declaration read from this location, casted to the given
  /// result type.
  template<typename T>
  T *readDeclAs() {
    return Reader->ReadDeclAs<T>(*F, Record, Idx);
  }

  IdentifierInfo *readIdentifier() {
    return Reader->readIdentifier(*F, Record, Idx);
  }

  /// Read a selector from the Record, advancing Idx.
  Selector readSelector() {
    return Reader->ReadSelector(*F, Record, Idx);
  }

  TypeCoupledDeclRefInfo readTypeCoupledDeclRefInfo();

  /// Read a declaration name, advancing Idx.
  // DeclarationName readDeclarationName(); (inherited)
  DeclarationNameLoc readDeclarationNameLoc(DeclarationName Name);
  DeclarationNameInfo readDeclarationNameInfo();

  void readQualifierInfo(QualifierInfo &Info);

  /// Return a nested name specifier, advancing Idx.
  // NestedNameSpecifier *readNestedNameSpecifier(); (inherited)

  NestedNameSpecifierLoc readNestedNameSpecifierLoc();

  /// Read a template name, advancing Idx.
  // TemplateName readTemplateName(); (inherited)

  /// Read a template argument, advancing Idx. (inherited)
  // TemplateArgument readTemplateArgument();
  using DataStreamBasicReader::readTemplateArgument;
  TemplateArgument readTemplateArgument(bool Canonicalize) {
    TemplateArgument Arg = readTemplateArgument();
    if (Canonicalize) {
      Arg = getContext().getCanonicalTemplateArgument(Arg);
    }
    return Arg;
  }

  /// Read a template parameter list, advancing Idx.
  TemplateParameterList *readTemplateParameterList();

  /// Read a template argument array, advancing Idx.
  void readTemplateArgumentList(SmallVectorImpl<TemplateArgument> &TemplArgs,
                                bool Canonicalize = false);

  /// Read a UnresolvedSet structure, advancing Idx.
  void readUnresolvedSet(LazyASTUnresolvedSet &Set);

  /// Read a C++ base specifier, advancing Idx.
  CXXBaseSpecifier readCXXBaseSpecifier();

  /// Read a CXXCtorInitializer array, advancing Idx.
  CXXCtorInitializer **readCXXCtorInitializers();

  CXXTemporary *readCXXTemporary() {
    return Reader->ReadCXXTemporary(*F, Record, Idx);
  }

  /// Read an OMPTraitInfo object, advancing Idx.
  OMPTraitInfo *readOMPTraitInfo();

  /// Read an OpenMP clause, advancing Idx.
  OMPClause *readOMPClause();

  /// Read an OpenMP children, advancing Idx.
  void readOMPChildren(OMPChildren *Data);

  /// Read a list of Exprs used for a var-list.
  llvm::SmallVector<Expr *> readOpenACCVarList();

  /// Read a list of Exprs used for a int-expr-list.
  llvm::SmallVector<Expr *> readOpenACCIntExprList();

  /// Read an OpenACC clause, advancing Idx.
  OpenACCClause *readOpenACCClause();

  /// Read a list of OpenACC clauses into the passed SmallVector.
  void readOpenACCClauseList(MutableArrayRef<const OpenACCClause *> Clauses);

  /// Read a source location, advancing Idx.
  SourceLocation readSourceLocation(LocSeq *Seq = nullptr) {
    return Reader->ReadSourceLocation(*F, Record, Idx, Seq);
  }

  /// Read a source range, advancing Idx.
  SourceRange readSourceRange(LocSeq *Seq = nullptr) {
    return Reader->ReadSourceRange(*F, Record, Idx, Seq);
  }

  /// Read an arbitrary constant value, advancing Idx.
  // APValue readAPValue(); (inherited)

  /// Read an integral value, advancing Idx.
  // llvm::APInt readAPInt(); (inherited)

  /// Read a signed integral value, advancing Idx.
  // llvm::APSInt readAPSInt(); (inherited)

  /// Read a floating-point value, advancing Idx.
  llvm::APFloat readAPFloat(const llvm::fltSemantics &Sem);

  /// Read a boolean value, advancing Idx.
  bool readBool() { return readInt() != 0; }

  /// Read a 32-bit unsigned value; required to satisfy BasicReader.
  uint32_t readUInt32() {
    return uint32_t(readInt());
  }

  /// Read a 64-bit unsigned value; required to satisfy BasicReader.
  uint64_t readUInt64() {
    return readInt();
  }

  /// Read a string, advancing Idx.
  std::string readString() {
    return Reader->ReadString(Record, Idx);
  }

  /// Read a path, advancing Idx.
  std::string readPath() {
    return Reader->ReadPath(*F, Record, Idx);
  }

  /// Read a version tuple, advancing Idx.
  VersionTuple readVersionTuple() {
    return ASTReader::ReadVersionTuple(Record, Idx);
  }

  /// Reads one attribute from the current stream position, advancing Idx.
  Attr *readAttr();

  /// Reads attributes from the current stream position, advancing Idx.
  void readAttributes(AttrVec &Attrs);

  /// Read an BTFTypeTagAttr object.
  BTFTypeTagAttr *readBTFTypeTagAttr() {
    return cast<BTFTypeTagAttr>(readAttr());
  }

  /// Reads a token out of a record, advancing Idx.
  Token readToken() {
    return Reader->ReadToken(*F, Record, Idx);
  }

  void recordSwitchCaseID(SwitchCase *SC, unsigned ID) {
    Reader->RecordSwitchCaseID(SC, ID);
  }

  /// Retrieve the switch-case statement with the given ID.
  SwitchCase *getSwitchCaseWithID(unsigned ID) {
    return Reader->getSwitchCaseWithID(ID);
  }
};

/// Helper class that saves the current stream position and
/// then restores it when destroyed.
struct SavedStreamPosition {
  explicit SavedStreamPosition(llvm::BitstreamCursor &Cursor)
      : Cursor(Cursor), Offset(Cursor.GetCurrentBitNo()) {}

  ~SavedStreamPosition() {
    if (llvm::Error Err = Cursor.JumpToBit(Offset))
      llvm::report_fatal_error(
          llvm::Twine("Cursor should always be able to go back, failed: ") +
          toString(std::move(Err)));
  }

private:
  llvm::BitstreamCursor &Cursor;
  uint64_t Offset;
};

} // namespace clang

#endif
