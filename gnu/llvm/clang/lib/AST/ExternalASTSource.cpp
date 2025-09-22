//===- ExternalASTSource.cpp - Abstract External AST Interface ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file provides the default implementation of the ExternalASTSource
//  interface, which enables construction of AST nodes from some external
//  source.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ExternalASTSource.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclarationName.h"
#include "clang/Basic/ASTSourceDescriptor.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/Support/ErrorHandling.h"
#include <cstdint>
#include <optional>

using namespace clang;

char ExternalASTSource::ID;

ExternalASTSource::~ExternalASTSource() = default;

std::optional<ASTSourceDescriptor>
ExternalASTSource::getSourceDescriptor(unsigned ID) {
  return std::nullopt;
}

ExternalASTSource::ExtKind
ExternalASTSource::hasExternalDefinitions(const Decl *D) {
  return EK_ReplyHazy;
}

void ExternalASTSource::FindFileRegionDecls(FileID File, unsigned Offset,
                                            unsigned Length,
                                            SmallVectorImpl<Decl *> &Decls) {}

void ExternalASTSource::CompleteRedeclChain(const Decl *D) {}

void ExternalASTSource::CompleteType(TagDecl *Tag) {}

void ExternalASTSource::CompleteType(ObjCInterfaceDecl *Class) {}

void ExternalASTSource::ReadComments() {}

void ExternalASTSource::StartedDeserializing() {}

void ExternalASTSource::FinishedDeserializing() {}

void ExternalASTSource::StartTranslationUnit(ASTConsumer *Consumer) {}

void ExternalASTSource::PrintStats() {}

bool ExternalASTSource::layoutRecordType(
    const RecordDecl *Record, uint64_t &Size, uint64_t &Alignment,
    llvm::DenseMap<const FieldDecl *, uint64_t> &FieldOffsets,
    llvm::DenseMap<const CXXRecordDecl *, CharUnits> &BaseOffsets,
    llvm::DenseMap<const CXXRecordDecl *, CharUnits> &VirtualBaseOffsets) {
  return false;
}

Decl *ExternalASTSource::GetExternalDecl(GlobalDeclID ID) { return nullptr; }

Selector ExternalASTSource::GetExternalSelector(uint32_t ID) {
  return Selector();
}

uint32_t ExternalASTSource::GetNumExternalSelectors() {
   return 0;
}

Stmt *ExternalASTSource::GetExternalDeclStmt(uint64_t Offset) {
  return nullptr;
}

CXXCtorInitializer **
ExternalASTSource::GetExternalCXXCtorInitializers(uint64_t Offset) {
  return nullptr;
}

CXXBaseSpecifier *
ExternalASTSource::GetExternalCXXBaseSpecifiers(uint64_t Offset) {
  return nullptr;
}

bool
ExternalASTSource::FindExternalVisibleDeclsByName(const DeclContext *DC,
                                                  DeclarationName Name) {
  return false;
}

void ExternalASTSource::completeVisibleDeclsMap(const DeclContext *DC) {}

void ExternalASTSource::FindExternalLexicalDecls(
    const DeclContext *DC, llvm::function_ref<bool(Decl::Kind)> IsKindWeWant,
    SmallVectorImpl<Decl *> &Result) {}

void ExternalASTSource::getMemoryBufferSizes(MemoryBufferSizes &sizes) const {}

uint32_t ExternalASTSource::incrementGeneration(ASTContext &C) {
  uint32_t OldGeneration = CurrentGeneration;

  // Make sure the generation of the topmost external source for the context is
  // incremented. That might not be us.
  auto *P = C.getExternalSource();
  if (P && P != this)
    CurrentGeneration = P->incrementGeneration(C);
  else {
    // FIXME: Only bump the generation counter if the current generation number
    // has been observed?
    if (!++CurrentGeneration)
      llvm::report_fatal_error("generation counter overflowed", false);
  }

  return OldGeneration;
}
