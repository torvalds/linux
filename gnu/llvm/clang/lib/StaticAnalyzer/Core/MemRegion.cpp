//===- MemRegion.cpp - Abstract memory regions for static analysis --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines MemRegion and its subclasses.  MemRegion defines a
//  partially-typed abstraction of memory useful for path-sensitive dataflow
//  analyses.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/PathSensitive/MemRegion.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/CharUnits.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Expr.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/Type.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Analysis/Support/BumpVector.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceManager.h"
#include "clang/StaticAnalyzer/Core/AnalyzerOptions.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/DynamicExtent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SValBuilder.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymbolManager.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CheckedArithmetic.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

using namespace clang;
using namespace ento;

#define DEBUG_TYPE "MemRegion"

//===----------------------------------------------------------------------===//
// MemRegion Construction.
//===----------------------------------------------------------------------===//

template <typename RegionTy, typename SuperTy, typename Arg1Ty>
RegionTy* MemRegionManager::getSubRegion(const Arg1Ty arg1,
                                         const SuperTy *superRegion) {
  llvm::FoldingSetNodeID ID;
  RegionTy::ProfileRegion(ID, arg1, superRegion);
  void *InsertPos;
  auto *R = cast_or_null<RegionTy>(Regions.FindNodeOrInsertPos(ID, InsertPos));

  if (!R) {
    R = new (A) RegionTy(arg1, superRegion);
    Regions.InsertNode(R, InsertPos);
  }

  return R;
}

template <typename RegionTy, typename SuperTy, typename Arg1Ty, typename Arg2Ty>
RegionTy* MemRegionManager::getSubRegion(const Arg1Ty arg1, const Arg2Ty arg2,
                                         const SuperTy *superRegion) {
  llvm::FoldingSetNodeID ID;
  RegionTy::ProfileRegion(ID, arg1, arg2, superRegion);
  void *InsertPos;
  auto *R = cast_or_null<RegionTy>(Regions.FindNodeOrInsertPos(ID, InsertPos));

  if (!R) {
    R = new (A) RegionTy(arg1, arg2, superRegion);
    Regions.InsertNode(R, InsertPos);
  }

  return R;
}

template <typename RegionTy, typename SuperTy,
          typename Arg1Ty, typename Arg2Ty, typename Arg3Ty>
RegionTy* MemRegionManager::getSubRegion(const Arg1Ty arg1, const Arg2Ty arg2,
                                         const Arg3Ty arg3,
                                         const SuperTy *superRegion) {
  llvm::FoldingSetNodeID ID;
  RegionTy::ProfileRegion(ID, arg1, arg2, arg3, superRegion);
  void *InsertPos;
  auto *R = cast_or_null<RegionTy>(Regions.FindNodeOrInsertPos(ID, InsertPos));

  if (!R) {
    R = new (A) RegionTy(arg1, arg2, arg3, superRegion);
    Regions.InsertNode(R, InsertPos);
  }

  return R;
}

//===----------------------------------------------------------------------===//
// Object destruction.
//===----------------------------------------------------------------------===//

MemRegion::~MemRegion() = default;

// All regions and their data are BumpPtrAllocated.  No need to call their
// destructors.
MemRegionManager::~MemRegionManager() = default;

//===----------------------------------------------------------------------===//
// Basic methods.
//===----------------------------------------------------------------------===//

bool SubRegion::isSubRegionOf(const MemRegion* R) const {
  const MemRegion* r = this;
  do {
    if (r == R)
      return true;
    if (const auto *sr = dyn_cast<SubRegion>(r))
      r = sr->getSuperRegion();
    else
      break;
  } while (r != nullptr);
  return false;
}

MemRegionManager &SubRegion::getMemRegionManager() const {
  const SubRegion* r = this;
  do {
    const MemRegion *superRegion = r->getSuperRegion();
    if (const auto *sr = dyn_cast<SubRegion>(superRegion)) {
      r = sr;
      continue;
    }
    return superRegion->getMemRegionManager();
  } while (true);
}

const StackFrameContext *VarRegion::getStackFrame() const {
  const auto *SSR = dyn_cast<StackSpaceRegion>(getMemorySpace());
  return SSR ? SSR->getStackFrame() : nullptr;
}

const StackFrameContext *
CXXLifetimeExtendedObjectRegion::getStackFrame() const {
  const auto *SSR = dyn_cast<StackSpaceRegion>(getMemorySpace());
  return SSR ? SSR->getStackFrame() : nullptr;
}

const StackFrameContext *CXXTempObjectRegion::getStackFrame() const {
  assert(isa<StackSpaceRegion>(getMemorySpace()) &&
         "A temporary object can only be allocated on the stack");
  return cast<StackSpaceRegion>(getMemorySpace())->getStackFrame();
}

ObjCIvarRegion::ObjCIvarRegion(const ObjCIvarDecl *ivd, const SubRegion *sReg)
    : DeclRegion(sReg, ObjCIvarRegionKind), IVD(ivd) {
  assert(IVD);
}

const ObjCIvarDecl *ObjCIvarRegion::getDecl() const { return IVD; }

QualType ObjCIvarRegion::getValueType() const {
  return getDecl()->getType();
}

QualType CXXBaseObjectRegion::getValueType() const {
  return QualType(getDecl()->getTypeForDecl(), 0);
}

QualType CXXDerivedObjectRegion::getValueType() const {
  return QualType(getDecl()->getTypeForDecl(), 0);
}

QualType ParamVarRegion::getValueType() const {
  assert(getDecl() &&
         "`ParamVarRegion` support functions without `Decl` not implemented"
         " yet.");
  return getDecl()->getType();
}

const ParmVarDecl *ParamVarRegion::getDecl() const {
  const Decl *D = getStackFrame()->getDecl();

  if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
    assert(Index < FD->param_size());
    return FD->parameters()[Index];
  } else if (const auto *BD = dyn_cast<BlockDecl>(D)) {
    assert(Index < BD->param_size());
    return BD->parameters()[Index];
  } else if (const auto *MD = dyn_cast<ObjCMethodDecl>(D)) {
    assert(Index < MD->param_size());
    return MD->parameters()[Index];
  } else if (const auto *CD = dyn_cast<CXXConstructorDecl>(D)) {
    assert(Index < CD->param_size());
    return CD->parameters()[Index];
  } else {
    llvm_unreachable("Unexpected Decl kind!");
  }
}

//===----------------------------------------------------------------------===//
// FoldingSet profiling.
//===----------------------------------------------------------------------===//

void MemSpaceRegion::Profile(llvm::FoldingSetNodeID &ID) const {
  ID.AddInteger(static_cast<unsigned>(getKind()));
}

void StackSpaceRegion::Profile(llvm::FoldingSetNodeID &ID) const {
  ID.AddInteger(static_cast<unsigned>(getKind()));
  ID.AddPointer(getStackFrame());
}

void StaticGlobalSpaceRegion::Profile(llvm::FoldingSetNodeID &ID) const {
  ID.AddInteger(static_cast<unsigned>(getKind()));
  ID.AddPointer(getCodeRegion());
}

void StringRegion::ProfileRegion(llvm::FoldingSetNodeID &ID,
                                 const StringLiteral *Str,
                                 const MemRegion *superRegion) {
  ID.AddInteger(static_cast<unsigned>(StringRegionKind));
  ID.AddPointer(Str);
  ID.AddPointer(superRegion);
}

void ObjCStringRegion::ProfileRegion(llvm::FoldingSetNodeID &ID,
                                     const ObjCStringLiteral *Str,
                                     const MemRegion *superRegion) {
  ID.AddInteger(static_cast<unsigned>(ObjCStringRegionKind));
  ID.AddPointer(Str);
  ID.AddPointer(superRegion);
}

void AllocaRegion::ProfileRegion(llvm::FoldingSetNodeID& ID,
                                 const Expr *Ex, unsigned cnt,
                                 const MemRegion *superRegion) {
  ID.AddInteger(static_cast<unsigned>(AllocaRegionKind));
  ID.AddPointer(Ex);
  ID.AddInteger(cnt);
  ID.AddPointer(superRegion);
}

void AllocaRegion::Profile(llvm::FoldingSetNodeID& ID) const {
  ProfileRegion(ID, Ex, Cnt, superRegion);
}

void CompoundLiteralRegion::Profile(llvm::FoldingSetNodeID& ID) const {
  CompoundLiteralRegion::ProfileRegion(ID, CL, superRegion);
}

void CompoundLiteralRegion::ProfileRegion(llvm::FoldingSetNodeID& ID,
                                          const CompoundLiteralExpr *CL,
                                          const MemRegion* superRegion) {
  ID.AddInteger(static_cast<unsigned>(CompoundLiteralRegionKind));
  ID.AddPointer(CL);
  ID.AddPointer(superRegion);
}

void CXXThisRegion::ProfileRegion(llvm::FoldingSetNodeID &ID,
                                  const PointerType *PT,
                                  const MemRegion *sRegion) {
  ID.AddInteger(static_cast<unsigned>(CXXThisRegionKind));
  ID.AddPointer(PT);
  ID.AddPointer(sRegion);
}

void CXXThisRegion::Profile(llvm::FoldingSetNodeID &ID) const {
  CXXThisRegion::ProfileRegion(ID, ThisPointerTy, superRegion);
}

void FieldRegion::Profile(llvm::FoldingSetNodeID &ID) const {
  ProfileRegion(ID, getDecl(), superRegion);
}

void ObjCIvarRegion::ProfileRegion(llvm::FoldingSetNodeID& ID,
                                   const ObjCIvarDecl *ivd,
                                   const MemRegion* superRegion) {
  ID.AddInteger(static_cast<unsigned>(ObjCIvarRegionKind));
  ID.AddPointer(ivd);
  ID.AddPointer(superRegion);
}

void ObjCIvarRegion::Profile(llvm::FoldingSetNodeID &ID) const {
  ProfileRegion(ID, getDecl(), superRegion);
}

void NonParamVarRegion::ProfileRegion(llvm::FoldingSetNodeID &ID,
                                      const VarDecl *VD,
                                      const MemRegion *superRegion) {
  ID.AddInteger(static_cast<unsigned>(NonParamVarRegionKind));
  ID.AddPointer(VD);
  ID.AddPointer(superRegion);
}

void NonParamVarRegion::Profile(llvm::FoldingSetNodeID &ID) const {
  ProfileRegion(ID, getDecl(), superRegion);
}

void ParamVarRegion::ProfileRegion(llvm::FoldingSetNodeID &ID, const Expr *OE,
                                   unsigned Idx, const MemRegion *SReg) {
  ID.AddInteger(static_cast<unsigned>(ParamVarRegionKind));
  ID.AddPointer(OE);
  ID.AddInteger(Idx);
  ID.AddPointer(SReg);
}

void ParamVarRegion::Profile(llvm::FoldingSetNodeID &ID) const {
  ProfileRegion(ID, getOriginExpr(), getIndex(), superRegion);
}

void SymbolicRegion::ProfileRegion(llvm::FoldingSetNodeID& ID, SymbolRef sym,
                                   const MemRegion *sreg) {
  ID.AddInteger(static_cast<unsigned>(MemRegion::SymbolicRegionKind));
  ID.Add(sym);
  ID.AddPointer(sreg);
}

void SymbolicRegion::Profile(llvm::FoldingSetNodeID& ID) const {
  SymbolicRegion::ProfileRegion(ID, sym, getSuperRegion());
}

void ElementRegion::ProfileRegion(llvm::FoldingSetNodeID& ID,
                                  QualType ElementType, SVal Idx,
                                  const MemRegion* superRegion) {
  ID.AddInteger(MemRegion::ElementRegionKind);
  ID.Add(ElementType);
  ID.AddPointer(superRegion);
  Idx.Profile(ID);
}

void ElementRegion::Profile(llvm::FoldingSetNodeID& ID) const {
  ElementRegion::ProfileRegion(ID, ElementType, Index, superRegion);
}

void FunctionCodeRegion::ProfileRegion(llvm::FoldingSetNodeID& ID,
                                       const NamedDecl *FD,
                                       const MemRegion*) {
  ID.AddInteger(MemRegion::FunctionCodeRegionKind);
  ID.AddPointer(FD);
}

void FunctionCodeRegion::Profile(llvm::FoldingSetNodeID& ID) const {
  FunctionCodeRegion::ProfileRegion(ID, FD, superRegion);
}

void BlockCodeRegion::ProfileRegion(llvm::FoldingSetNodeID& ID,
                                    const BlockDecl *BD, CanQualType,
                                    const AnalysisDeclContext *AC,
                                    const MemRegion*) {
  ID.AddInteger(MemRegion::BlockCodeRegionKind);
  ID.AddPointer(BD);
}

void BlockCodeRegion::Profile(llvm::FoldingSetNodeID& ID) const {
  BlockCodeRegion::ProfileRegion(ID, BD, locTy, AC, superRegion);
}

void BlockDataRegion::ProfileRegion(llvm::FoldingSetNodeID& ID,
                                    const BlockCodeRegion *BC,
                                    const LocationContext *LC,
                                    unsigned BlkCount,
                                    const MemRegion *sReg) {
  ID.AddInteger(MemRegion::BlockDataRegionKind);
  ID.AddPointer(BC);
  ID.AddPointer(LC);
  ID.AddInteger(BlkCount);
  ID.AddPointer(sReg);
}

void BlockDataRegion::Profile(llvm::FoldingSetNodeID& ID) const {
  BlockDataRegion::ProfileRegion(ID, BC, LC, BlockCount, getSuperRegion());
}

void CXXTempObjectRegion::ProfileRegion(llvm::FoldingSetNodeID &ID,
                                        Expr const *Ex,
                                        const MemRegion *sReg) {
  ID.AddPointer(Ex);
  ID.AddPointer(sReg);
}

void CXXTempObjectRegion::Profile(llvm::FoldingSetNodeID &ID) const {
  ProfileRegion(ID, Ex, getSuperRegion());
}

void CXXLifetimeExtendedObjectRegion::ProfileRegion(llvm::FoldingSetNodeID &ID,
                                                    const Expr *E,
                                                    const ValueDecl *D,
                                                    const MemRegion *sReg) {
  ID.AddPointer(E);
  ID.AddPointer(D);
  ID.AddPointer(sReg);
}

void CXXLifetimeExtendedObjectRegion::Profile(
    llvm::FoldingSetNodeID &ID) const {
  ProfileRegion(ID, Ex, ExD, getSuperRegion());
}

void CXXBaseObjectRegion::ProfileRegion(llvm::FoldingSetNodeID &ID,
                                        const CXXRecordDecl *RD,
                                        bool IsVirtual,
                                        const MemRegion *SReg) {
  ID.AddPointer(RD);
  ID.AddBoolean(IsVirtual);
  ID.AddPointer(SReg);
}

void CXXBaseObjectRegion::Profile(llvm::FoldingSetNodeID &ID) const {
  ProfileRegion(ID, getDecl(), isVirtual(), superRegion);
}

void CXXDerivedObjectRegion::ProfileRegion(llvm::FoldingSetNodeID &ID,
                                           const CXXRecordDecl *RD,
                                           const MemRegion *SReg) {
  ID.AddPointer(RD);
  ID.AddPointer(SReg);
}

void CXXDerivedObjectRegion::Profile(llvm::FoldingSetNodeID &ID) const {
  ProfileRegion(ID, getDecl(), superRegion);
}

//===----------------------------------------------------------------------===//
// Region anchors.
//===----------------------------------------------------------------------===//

void GlobalsSpaceRegion::anchor() {}

void NonStaticGlobalSpaceRegion::anchor() {}

void StackSpaceRegion::anchor() {}

void TypedRegion::anchor() {}

void TypedValueRegion::anchor() {}

void CodeTextRegion::anchor() {}

void SubRegion::anchor() {}

//===----------------------------------------------------------------------===//
// Region pretty-printing.
//===----------------------------------------------------------------------===//

LLVM_DUMP_METHOD void MemRegion::dump() const {
  dumpToStream(llvm::errs());
}

std::string MemRegion::getString() const {
  std::string s;
  llvm::raw_string_ostream os(s);
  dumpToStream(os);
  return s;
}

void MemRegion::dumpToStream(raw_ostream &os) const {
  os << "<Unknown Region>";
}

void AllocaRegion::dumpToStream(raw_ostream &os) const {
  os << "alloca{S" << Ex->getID(getContext()) << ',' << Cnt << '}';
}

void FunctionCodeRegion::dumpToStream(raw_ostream &os) const {
  os << "code{" << getDecl()->getDeclName().getAsString() << '}';
}

void BlockCodeRegion::dumpToStream(raw_ostream &os) const {
  os << "block_code{" << static_cast<const void *>(this) << '}';
}

void BlockDataRegion::dumpToStream(raw_ostream &os) const {
  os << "block_data{" << BC;
  os << "; ";
  for (auto Var : referenced_vars())
    os << "(" << Var.getCapturedRegion() << "<-" << Var.getOriginalRegion()
       << ") ";
  os << '}';
}

void CompoundLiteralRegion::dumpToStream(raw_ostream &os) const {
  // FIXME: More elaborate pretty-printing.
  os << "{ S" << CL->getID(getContext()) <<  " }";
}

void CXXTempObjectRegion::dumpToStream(raw_ostream &os) const {
  os << "temp_object{" << getValueType() << ", "
     << "S" << Ex->getID(getContext()) << '}';
}

void CXXLifetimeExtendedObjectRegion::dumpToStream(raw_ostream &os) const {
  os << "lifetime_extended_object{" << getValueType() << ", ";
  if (const IdentifierInfo *ID = ExD->getIdentifier())
    os << ID->getName();
  else
    os << "D" << ExD->getID();
  os << ", "
     << "S" << Ex->getID(getContext()) << '}';
}

void CXXBaseObjectRegion::dumpToStream(raw_ostream &os) const {
  os << "Base{" << superRegion << ',' << getDecl()->getName() << '}';
}

void CXXDerivedObjectRegion::dumpToStream(raw_ostream &os) const {
  os << "Derived{" << superRegion << ',' << getDecl()->getName() << '}';
}

void CXXThisRegion::dumpToStream(raw_ostream &os) const {
  os << "this";
}

void ElementRegion::dumpToStream(raw_ostream &os) const {
  os << "Element{" << superRegion << ',' << Index << ',' << getElementType()
     << '}';
}

void FieldRegion::dumpToStream(raw_ostream &os) const {
  os << superRegion << "." << *getDecl();
}

void ObjCIvarRegion::dumpToStream(raw_ostream &os) const {
  os << "Ivar{" << superRegion << ',' << *getDecl() << '}';
}

void StringRegion::dumpToStream(raw_ostream &os) const {
  assert(Str != nullptr && "Expecting non-null StringLiteral");
  Str->printPretty(os, nullptr, PrintingPolicy(getContext().getLangOpts()));
}

void ObjCStringRegion::dumpToStream(raw_ostream &os) const {
  assert(Str != nullptr && "Expecting non-null ObjCStringLiteral");
  Str->printPretty(os, nullptr, PrintingPolicy(getContext().getLangOpts()));
}

void SymbolicRegion::dumpToStream(raw_ostream &os) const {
  if (isa<HeapSpaceRegion>(getSuperRegion()))
    os << "Heap";
  os << "SymRegion{" << sym << '}';
}

void NonParamVarRegion::dumpToStream(raw_ostream &os) const {
  if (const IdentifierInfo *ID = VD->getIdentifier())
    os << ID->getName();
  else
    os << "NonParamVarRegion{D" << VD->getID() << '}';
}

LLVM_DUMP_METHOD void RegionRawOffset::dump() const {
  dumpToStream(llvm::errs());
}

void RegionRawOffset::dumpToStream(raw_ostream &os) const {
  os << "raw_offset{" << getRegion() << ',' << getOffset().getQuantity() << '}';
}

void CodeSpaceRegion::dumpToStream(raw_ostream &os) const {
  os << "CodeSpaceRegion";
}

void StaticGlobalSpaceRegion::dumpToStream(raw_ostream &os) const {
  os << "StaticGlobalsMemSpace{" << CR << '}';
}

void GlobalInternalSpaceRegion::dumpToStream(raw_ostream &os) const {
  os << "GlobalInternalSpaceRegion";
}

void GlobalSystemSpaceRegion::dumpToStream(raw_ostream &os) const {
  os << "GlobalSystemSpaceRegion";
}

void GlobalImmutableSpaceRegion::dumpToStream(raw_ostream &os) const {
  os << "GlobalImmutableSpaceRegion";
}

void HeapSpaceRegion::dumpToStream(raw_ostream &os) const {
  os << "HeapSpaceRegion";
}

void UnknownSpaceRegion::dumpToStream(raw_ostream &os) const {
  os << "UnknownSpaceRegion";
}

void StackArgumentsSpaceRegion::dumpToStream(raw_ostream &os) const {
  os << "StackArgumentsSpaceRegion";
}

void StackLocalsSpaceRegion::dumpToStream(raw_ostream &os) const {
  os << "StackLocalsSpaceRegion";
}

void ParamVarRegion::dumpToStream(raw_ostream &os) const {
  const ParmVarDecl *PVD = getDecl();
  assert(PVD &&
         "`ParamVarRegion` support functions without `Decl` not implemented"
         " yet.");
  if (const IdentifierInfo *ID = PVD->getIdentifier()) {
    os << ID->getName();
  } else {
    os << "ParamVarRegion{P" << PVD->getID() << '}';
  }
}

bool MemRegion::canPrintPretty() const {
  return canPrintPrettyAsExpr();
}

bool MemRegion::canPrintPrettyAsExpr() const {
  return false;
}

StringRef MemRegion::getKindStr() const {
  switch (getKind()) {
#define REGION(Id, Parent)                                                     \
  case Id##Kind:                                                               \
    return #Id;
#include "clang/StaticAnalyzer/Core/PathSensitive/Regions.def"
#undef REGION
  }
  llvm_unreachable("Unkown kind!");
}

void MemRegion::printPretty(raw_ostream &os) const {
  assert(canPrintPretty() && "This region cannot be printed pretty.");
  os << "'";
  printPrettyAsExpr(os);
  os << "'";
}

void MemRegion::printPrettyAsExpr(raw_ostream &) const {
  llvm_unreachable("This region cannot be printed pretty.");
}

bool NonParamVarRegion::canPrintPrettyAsExpr() const { return true; }

void NonParamVarRegion::printPrettyAsExpr(raw_ostream &os) const {
  os << getDecl()->getName();
}

bool ParamVarRegion::canPrintPrettyAsExpr() const { return true; }

void ParamVarRegion::printPrettyAsExpr(raw_ostream &os) const {
  assert(getDecl() &&
         "`ParamVarRegion` support functions without `Decl` not implemented"
         " yet.");
  os << getDecl()->getName();
}

bool ObjCIvarRegion::canPrintPrettyAsExpr() const {
  return true;
}

void ObjCIvarRegion::printPrettyAsExpr(raw_ostream &os) const {
  os << getDecl()->getName();
}

bool FieldRegion::canPrintPretty() const {
  return true;
}

bool FieldRegion::canPrintPrettyAsExpr() const {
  return superRegion->canPrintPrettyAsExpr();
}

void FieldRegion::printPrettyAsExpr(raw_ostream &os) const {
  assert(canPrintPrettyAsExpr());
  superRegion->printPrettyAsExpr(os);
  os << "." << getDecl()->getName();
}

void FieldRegion::printPretty(raw_ostream &os) const {
  if (canPrintPrettyAsExpr()) {
    os << "\'";
    printPrettyAsExpr(os);
    os << "'";
  } else {
    os << "field " << "\'" << getDecl()->getName() << "'";
  }
}

bool CXXBaseObjectRegion::canPrintPrettyAsExpr() const {
  return superRegion->canPrintPrettyAsExpr();
}

void CXXBaseObjectRegion::printPrettyAsExpr(raw_ostream &os) const {
  superRegion->printPrettyAsExpr(os);
}

bool CXXDerivedObjectRegion::canPrintPrettyAsExpr() const {
  return superRegion->canPrintPrettyAsExpr();
}

void CXXDerivedObjectRegion::printPrettyAsExpr(raw_ostream &os) const {
  superRegion->printPrettyAsExpr(os);
}

std::string MemRegion::getDescriptiveName(bool UseQuotes) const {
  std::string VariableName;
  std::string ArrayIndices;
  const MemRegion *R = this;
  SmallString<50> buf;
  llvm::raw_svector_ostream os(buf);

  // Obtain array indices to add them to the variable name.
  const ElementRegion *ER = nullptr;
  while ((ER = R->getAs<ElementRegion>())) {
    // Index is a ConcreteInt.
    if (auto CI = ER->getIndex().getAs<nonloc::ConcreteInt>()) {
      llvm::SmallString<2> Idx;
      CI->getValue().toString(Idx);
      ArrayIndices = (llvm::Twine("[") + Idx.str() + "]" + ArrayIndices).str();
    }
    // Index is symbolic, but may have a descriptive name.
    else {
      auto SI = ER->getIndex().getAs<nonloc::SymbolVal>();
      if (!SI)
        return "";

      const MemRegion *OR = SI->getAsSymbol()->getOriginRegion();
      if (!OR)
        return "";

      std::string Idx = OR->getDescriptiveName(false);
      if (Idx.empty())
        return "";

      ArrayIndices = (llvm::Twine("[") + Idx + "]" + ArrayIndices).str();
    }
    R = ER->getSuperRegion();
  }

  // Get variable name.
  if (R && R->canPrintPrettyAsExpr()) {
    R->printPrettyAsExpr(os);
    if (UseQuotes)
      return (llvm::Twine("'") + os.str() + ArrayIndices + "'").str();
    else
      return (llvm::Twine(os.str()) + ArrayIndices).str();
  }

  return VariableName;
}

SourceRange MemRegion::sourceRange() const {
  // Check for more specific regions first.
  if (auto *FR = dyn_cast<FieldRegion>(this)) {
    return FR->getDecl()->getSourceRange();
  }

  if (auto *VR = dyn_cast<VarRegion>(this->getBaseRegion())) {
    return VR->getDecl()->getSourceRange();
  }

  // Return invalid source range (can be checked by client).
  return {};
}

//===----------------------------------------------------------------------===//
// MemRegionManager methods.
//===----------------------------------------------------------------------===//

DefinedOrUnknownSVal MemRegionManager::getStaticSize(const MemRegion *MR,
                                                     SValBuilder &SVB) const {
  const auto *SR = cast<SubRegion>(MR);
  SymbolManager &SymMgr = SVB.getSymbolManager();

  switch (SR->getKind()) {
  case MemRegion::AllocaRegionKind:
  case MemRegion::SymbolicRegionKind:
    return nonloc::SymbolVal(SymMgr.getExtentSymbol(SR));
  case MemRegion::StringRegionKind:
    return SVB.makeIntVal(
        cast<StringRegion>(SR)->getStringLiteral()->getByteLength() + 1,
        SVB.getArrayIndexType());
  case MemRegion::CompoundLiteralRegionKind:
  case MemRegion::CXXBaseObjectRegionKind:
  case MemRegion::CXXDerivedObjectRegionKind:
  case MemRegion::CXXTempObjectRegionKind:
  case MemRegion::CXXLifetimeExtendedObjectRegionKind:
  case MemRegion::CXXThisRegionKind:
  case MemRegion::ObjCIvarRegionKind:
  case MemRegion::NonParamVarRegionKind:
  case MemRegion::ParamVarRegionKind:
  case MemRegion::ElementRegionKind:
  case MemRegion::ObjCStringRegionKind: {
    QualType Ty = cast<TypedValueRegion>(SR)->getDesugaredValueType(Ctx);
    if (isa<VariableArrayType>(Ty))
      return nonloc::SymbolVal(SymMgr.getExtentSymbol(SR));

    if (Ty->isIncompleteType())
      return UnknownVal();

    return getElementExtent(Ty, SVB);
  }
  case MemRegion::FieldRegionKind: {
    // Force callers to deal with bitfields explicitly.
    if (cast<FieldRegion>(SR)->getDecl()->isBitField())
      return UnknownVal();

    QualType Ty = cast<TypedValueRegion>(SR)->getDesugaredValueType(Ctx);
    const DefinedOrUnknownSVal Size = getElementExtent(Ty, SVB);

    // We currently don't model flexible array members (FAMs), which are:
    //  - int array[]; of IncompleteArrayType
    //  - int array[0]; of ConstantArrayType with size 0
    //  - int array[1]; of ConstantArrayType with size 1
    // https://gcc.gnu.org/onlinedocs/gcc/Zero-Length.html
    const auto isFlexibleArrayMemberCandidate =
        [this](const ArrayType *AT) -> bool {
      if (!AT)
        return false;

      auto IsIncompleteArray = [](const ArrayType *AT) {
        return isa<IncompleteArrayType>(AT);
      };
      auto IsArrayOfZero = [](const ArrayType *AT) {
        const auto *CAT = dyn_cast<ConstantArrayType>(AT);
        return CAT && CAT->isZeroSize();
      };
      auto IsArrayOfOne = [](const ArrayType *AT) {
        const auto *CAT = dyn_cast<ConstantArrayType>(AT);
        return CAT && CAT->getSize() == 1;
      };

      using FAMKind = LangOptions::StrictFlexArraysLevelKind;
      const FAMKind StrictFlexArraysLevel =
          Ctx.getLangOpts().getStrictFlexArraysLevel();

      // "Default": Any trailing array member is a FAM.
      // Since we cannot tell at this point if this array is a trailing member
      // or not, let's just do the same as for "OneZeroOrIncomplete".
      if (StrictFlexArraysLevel == FAMKind::Default)
        return IsArrayOfOne(AT) || IsArrayOfZero(AT) || IsIncompleteArray(AT);

      if (StrictFlexArraysLevel == FAMKind::OneZeroOrIncomplete)
        return IsArrayOfOne(AT) || IsArrayOfZero(AT) || IsIncompleteArray(AT);

      if (StrictFlexArraysLevel == FAMKind::ZeroOrIncomplete)
        return IsArrayOfZero(AT) || IsIncompleteArray(AT);

      assert(StrictFlexArraysLevel == FAMKind::IncompleteOnly);
      return IsIncompleteArray(AT);
    };

    if (isFlexibleArrayMemberCandidate(Ctx.getAsArrayType(Ty)))
      return UnknownVal();

    return Size;
  }
    // FIXME: The following are being used in 'SimpleSValBuilder' and in
    // 'ArrayBoundChecker::checkLocation' because there is no symbol to
    // represent the regions more appropriately.
  case MemRegion::BlockDataRegionKind:
  case MemRegion::BlockCodeRegionKind:
  case MemRegion::FunctionCodeRegionKind:
    return nonloc::SymbolVal(SymMgr.getExtentSymbol(SR));
  default:
    llvm_unreachable("Unhandled region");
  }
}

template <typename REG>
const REG *MemRegionManager::LazyAllocate(REG*& region) {
  if (!region) {
    region = new (A) REG(*this);
  }

  return region;
}

template <typename REG, typename ARG>
const REG *MemRegionManager::LazyAllocate(REG*& region, ARG a) {
  if (!region) {
    region = new (A) REG(this, a);
  }

  return region;
}

const StackLocalsSpaceRegion*
MemRegionManager::getStackLocalsRegion(const StackFrameContext *STC) {
  assert(STC);
  StackLocalsSpaceRegion *&R = StackLocalsSpaceRegions[STC];

  if (R)
    return R;

  R = new (A) StackLocalsSpaceRegion(*this, STC);
  return R;
}

const StackArgumentsSpaceRegion *
MemRegionManager::getStackArgumentsRegion(const StackFrameContext *STC) {
  assert(STC);
  StackArgumentsSpaceRegion *&R = StackArgumentsSpaceRegions[STC];

  if (R)
    return R;

  R = new (A) StackArgumentsSpaceRegion(*this, STC);
  return R;
}

const GlobalsSpaceRegion
*MemRegionManager::getGlobalsRegion(MemRegion::Kind K,
                                    const CodeTextRegion *CR) {
  if (!CR) {
    if (K == MemRegion::GlobalSystemSpaceRegionKind)
      return LazyAllocate(SystemGlobals);
    if (K == MemRegion::GlobalImmutableSpaceRegionKind)
      return LazyAllocate(ImmutableGlobals);
    assert(K == MemRegion::GlobalInternalSpaceRegionKind);
    return LazyAllocate(InternalGlobals);
  }

  assert(K == MemRegion::StaticGlobalSpaceRegionKind);
  StaticGlobalSpaceRegion *&R = StaticsGlobalSpaceRegions[CR];
  if (R)
    return R;

  R = new (A) StaticGlobalSpaceRegion(*this, CR);
  return R;
}

const HeapSpaceRegion *MemRegionManager::getHeapRegion() {
  return LazyAllocate(heap);
}

const UnknownSpaceRegion *MemRegionManager::getUnknownRegion() {
  return LazyAllocate(unknown);
}

const CodeSpaceRegion *MemRegionManager::getCodeRegion() {
  return LazyAllocate(code);
}

//===----------------------------------------------------------------------===//
// Constructing regions.
//===----------------------------------------------------------------------===//

const StringRegion *MemRegionManager::getStringRegion(const StringLiteral *Str){
  return getSubRegion<StringRegion>(
      Str, cast<GlobalInternalSpaceRegion>(getGlobalsRegion()));
}

const ObjCStringRegion *
MemRegionManager::getObjCStringRegion(const ObjCStringLiteral *Str){
  return getSubRegion<ObjCStringRegion>(
      Str, cast<GlobalInternalSpaceRegion>(getGlobalsRegion()));
}

/// Look through a chain of LocationContexts to either find the
/// StackFrameContext that matches a DeclContext, or find a VarRegion
/// for a variable captured by a block.
static llvm::PointerUnion<const StackFrameContext *, const VarRegion *>
getStackOrCaptureRegionForDeclContext(const LocationContext *LC,
                                      const DeclContext *DC,
                                      const VarDecl *VD) {
  while (LC) {
    if (const auto *SFC = dyn_cast<StackFrameContext>(LC)) {
      if (cast<DeclContext>(SFC->getDecl()) == DC)
        return SFC;
    }
    if (const auto *BC = dyn_cast<BlockInvocationContext>(LC)) {
      const auto *BR = static_cast<const BlockDataRegion *>(BC->getData());
      // FIXME: This can be made more efficient.
      for (auto Var : BR->referenced_vars()) {
        const TypedValueRegion *OrigR = Var.getOriginalRegion();
        if (const auto *VR = dyn_cast<VarRegion>(OrigR)) {
          if (VR->getDecl() == VD)
            return cast<VarRegion>(Var.getCapturedRegion());
        }
      }
    }

    LC = LC->getParent();
  }
  return (const StackFrameContext *)nullptr;
}

const VarRegion *MemRegionManager::getVarRegion(const VarDecl *D,
                                                const LocationContext *LC) {
  const auto *PVD = dyn_cast<ParmVarDecl>(D);
  if (PVD) {
    unsigned Index = PVD->getFunctionScopeIndex();
    const StackFrameContext *SFC = LC->getStackFrame();
    const Stmt *CallSite = SFC->getCallSite();
    if (CallSite) {
      const Decl *D = SFC->getDecl();
      if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
        if (Index < FD->param_size() && FD->parameters()[Index] == PVD)
          return getSubRegion<ParamVarRegion>(cast<Expr>(CallSite), Index,
                                              getStackArgumentsRegion(SFC));
      } else if (const auto *BD = dyn_cast<BlockDecl>(D)) {
        if (Index < BD->param_size() && BD->parameters()[Index] == PVD)
          return getSubRegion<ParamVarRegion>(cast<Expr>(CallSite), Index,
                                              getStackArgumentsRegion(SFC));
      } else {
        return getSubRegion<ParamVarRegion>(cast<Expr>(CallSite), Index,
                                            getStackArgumentsRegion(SFC));
      }
    }
  }

  D = D->getCanonicalDecl();
  const MemRegion *sReg = nullptr;

  if (D->hasGlobalStorage() && !D->isStaticLocal()) {
    QualType Ty = D->getType();
    assert(!Ty.isNull());
    if (Ty.isConstQualified()) {
      sReg = getGlobalsRegion(MemRegion::GlobalImmutableSpaceRegionKind);
    } else if (Ctx.getSourceManager().isInSystemHeader(D->getLocation())) {
      sReg = getGlobalsRegion(MemRegion::GlobalSystemSpaceRegionKind);
    } else {
      sReg = getGlobalsRegion(MemRegion::GlobalInternalSpaceRegionKind);
    }

  // Finally handle static locals.
  } else {
    // FIXME: Once we implement scope handling, we will need to properly lookup
    // 'D' to the proper LocationContext.
    const DeclContext *DC = D->getDeclContext();
    llvm::PointerUnion<const StackFrameContext *, const VarRegion *> V =
      getStackOrCaptureRegionForDeclContext(LC, DC, D);

    if (V.is<const VarRegion*>())
      return V.get<const VarRegion*>();

    const auto *STC = V.get<const StackFrameContext *>();

    if (!STC) {
      // FIXME: Assign a more sensible memory space to static locals
      // we see from within blocks that we analyze as top-level declarations.
      sReg = getUnknownRegion();
    } else {
      if (D->hasLocalStorage()) {
        sReg =
            isa<ParmVarDecl, ImplicitParamDecl>(D)
                ? static_cast<const MemRegion *>(getStackArgumentsRegion(STC))
                : static_cast<const MemRegion *>(getStackLocalsRegion(STC));
      }
      else {
        assert(D->isStaticLocal());
        const Decl *STCD = STC->getDecl();
        if (isa<FunctionDecl, ObjCMethodDecl>(STCD))
          sReg = getGlobalsRegion(MemRegion::StaticGlobalSpaceRegionKind,
                                  getFunctionCodeRegion(cast<NamedDecl>(STCD)));
        else if (const auto *BD = dyn_cast<BlockDecl>(STCD)) {
          // FIXME: The fallback type here is totally bogus -- though it should
          // never be queried, it will prevent uniquing with the real
          // BlockCodeRegion. Ideally we'd fix the AST so that we always had a
          // signature.
          QualType T;
          if (const TypeSourceInfo *TSI = BD->getSignatureAsWritten())
            T = TSI->getType();
          if (T.isNull())
            T = getContext().VoidTy;
          if (!T->getAs<FunctionType>()) {
            FunctionProtoType::ExtProtoInfo Ext;
            T = getContext().getFunctionType(T, std::nullopt, Ext);
          }
          T = getContext().getBlockPointerType(T);

          const BlockCodeRegion *BTR =
            getBlockCodeRegion(BD, Ctx.getCanonicalType(T),
                               STC->getAnalysisDeclContext());
          sReg = getGlobalsRegion(MemRegion::StaticGlobalSpaceRegionKind,
                                  BTR);
        }
        else {
          sReg = getGlobalsRegion();
        }
      }
    }
  }

  return getNonParamVarRegion(D, sReg);
}

const NonParamVarRegion *
MemRegionManager::getNonParamVarRegion(const VarDecl *D,
                                       const MemRegion *superR) {
  // Prefer the definition over the canonical decl as the canonical form.
  D = D->getCanonicalDecl();
  if (const VarDecl *Def = D->getDefinition())
    D = Def;
  return getSubRegion<NonParamVarRegion>(D, superR);
}

const ParamVarRegion *
MemRegionManager::getParamVarRegion(const Expr *OriginExpr, unsigned Index,
                                    const LocationContext *LC) {
  const StackFrameContext *SFC = LC->getStackFrame();
  assert(SFC);
  return getSubRegion<ParamVarRegion>(OriginExpr, Index,
                                      getStackArgumentsRegion(SFC));
}

const BlockDataRegion *
MemRegionManager::getBlockDataRegion(const BlockCodeRegion *BC,
                                     const LocationContext *LC,
                                     unsigned blockCount) {
  const MemSpaceRegion *sReg = nullptr;
  const BlockDecl *BD = BC->getDecl();
  if (!BD->hasCaptures()) {
    // This handles 'static' blocks.
    sReg = getGlobalsRegion(MemRegion::GlobalImmutableSpaceRegionKind);
  }
  else {
    bool IsArcManagedBlock = Ctx.getLangOpts().ObjCAutoRefCount;

    // ARC managed blocks can be initialized on stack or directly in heap
    // depending on the implementations.  So we initialize them with
    // UnknownRegion.
    if (!IsArcManagedBlock && LC) {
      // FIXME: Once we implement scope handling, we want the parent region
      // to be the scope.
      const StackFrameContext *STC = LC->getStackFrame();
      assert(STC);
      sReg = getStackLocalsRegion(STC);
    } else {
      // We allow 'LC' to be NULL for cases where want BlockDataRegions
      // without context-sensitivity.
      sReg = getUnknownRegion();
    }
  }

  return getSubRegion<BlockDataRegion>(BC, LC, blockCount, sReg);
}

const CompoundLiteralRegion*
MemRegionManager::getCompoundLiteralRegion(const CompoundLiteralExpr *CL,
                                           const LocationContext *LC) {
  const MemSpaceRegion *sReg = nullptr;

  if (CL->isFileScope())
    sReg = getGlobalsRegion();
  else {
    const StackFrameContext *STC = LC->getStackFrame();
    assert(STC);
    sReg = getStackLocalsRegion(STC);
  }

  return getSubRegion<CompoundLiteralRegion>(CL, sReg);
}

const ElementRegion *
MemRegionManager::getElementRegion(QualType elementType, NonLoc Idx,
                                   const SubRegion *superRegion,
                                   const ASTContext &Ctx) {
  QualType T = Ctx.getCanonicalType(elementType).getUnqualifiedType();

  llvm::FoldingSetNodeID ID;
  ElementRegion::ProfileRegion(ID, T, Idx, superRegion);

  void *InsertPos;
  MemRegion* data = Regions.FindNodeOrInsertPos(ID, InsertPos);
  auto *R = cast_or_null<ElementRegion>(data);

  if (!R) {
    R = new (A) ElementRegion(T, Idx, superRegion);
    Regions.InsertNode(R, InsertPos);
  }

  return R;
}

const FunctionCodeRegion *
MemRegionManager::getFunctionCodeRegion(const NamedDecl *FD) {
  // To think: should we canonicalize the declaration here?
  return getSubRegion<FunctionCodeRegion>(FD, getCodeRegion());
}

const BlockCodeRegion *
MemRegionManager::getBlockCodeRegion(const BlockDecl *BD, CanQualType locTy,
                                     AnalysisDeclContext *AC) {
  return getSubRegion<BlockCodeRegion>(BD, locTy, AC, getCodeRegion());
}

const SymbolicRegion *
MemRegionManager::getSymbolicRegion(SymbolRef sym,
                                    const MemSpaceRegion *MemSpace) {
  if (MemSpace == nullptr)
    MemSpace = getUnknownRegion();
  return getSubRegion<SymbolicRegion>(sym, MemSpace);
}

const SymbolicRegion *MemRegionManager::getSymbolicHeapRegion(SymbolRef Sym) {
  return getSubRegion<SymbolicRegion>(Sym, getHeapRegion());
}

const FieldRegion*
MemRegionManager::getFieldRegion(const FieldDecl *d,
                                 const SubRegion* superRegion){
  return getSubRegion<FieldRegion>(d, superRegion);
}

const ObjCIvarRegion*
MemRegionManager::getObjCIvarRegion(const ObjCIvarDecl *d,
                                    const SubRegion* superRegion) {
  return getSubRegion<ObjCIvarRegion>(d, superRegion);
}

const CXXTempObjectRegion*
MemRegionManager::getCXXTempObjectRegion(Expr const *E,
                                         LocationContext const *LC) {
  const StackFrameContext *SFC = LC->getStackFrame();
  assert(SFC);
  return getSubRegion<CXXTempObjectRegion>(E, getStackLocalsRegion(SFC));
}

const CXXLifetimeExtendedObjectRegion *
MemRegionManager::getCXXLifetimeExtendedObjectRegion(
    const Expr *Ex, const ValueDecl *VD, const LocationContext *LC) {
  const StackFrameContext *SFC = LC->getStackFrame();
  assert(SFC);
  return getSubRegion<CXXLifetimeExtendedObjectRegion>(
      Ex, VD, getStackLocalsRegion(SFC));
}

const CXXLifetimeExtendedObjectRegion *
MemRegionManager::getCXXStaticLifetimeExtendedObjectRegion(
    const Expr *Ex, const ValueDecl *VD) {
  return getSubRegion<CXXLifetimeExtendedObjectRegion>(
      Ex, VD,
      getGlobalsRegion(MemRegion::GlobalInternalSpaceRegionKind, nullptr));
}

/// Checks whether \p BaseClass is a valid virtual or direct non-virtual base
/// class of the type of \p Super.
static bool isValidBaseClass(const CXXRecordDecl *BaseClass,
                             const TypedValueRegion *Super,
                             bool IsVirtual) {
  BaseClass = BaseClass->getCanonicalDecl();

  const CXXRecordDecl *Class = Super->getValueType()->getAsCXXRecordDecl();
  if (!Class)
    return true;

  if (IsVirtual)
    return Class->isVirtuallyDerivedFrom(BaseClass);

  for (const auto &I : Class->bases()) {
    if (I.getType()->getAsCXXRecordDecl()->getCanonicalDecl() == BaseClass)
      return true;
  }

  return false;
}

const CXXBaseObjectRegion *
MemRegionManager::getCXXBaseObjectRegion(const CXXRecordDecl *RD,
                                         const SubRegion *Super,
                                         bool IsVirtual) {
  if (isa<TypedValueRegion>(Super)) {
    assert(isValidBaseClass(RD, cast<TypedValueRegion>(Super), IsVirtual));
    (void)&isValidBaseClass;

    if (IsVirtual) {
      // Virtual base regions should not be layered, since the layout rules
      // are different.
      while (const auto *Base = dyn_cast<CXXBaseObjectRegion>(Super))
        Super = cast<SubRegion>(Base->getSuperRegion());
      assert(Super && !isa<MemSpaceRegion>(Super));
    }
  }

  return getSubRegion<CXXBaseObjectRegion>(RD, IsVirtual, Super);
}

const CXXDerivedObjectRegion *
MemRegionManager::getCXXDerivedObjectRegion(const CXXRecordDecl *RD,
                                            const SubRegion *Super) {
  return getSubRegion<CXXDerivedObjectRegion>(RD, Super);
}

const CXXThisRegion*
MemRegionManager::getCXXThisRegion(QualType thisPointerTy,
                                   const LocationContext *LC) {
  const auto *PT = thisPointerTy->getAs<PointerType>();
  assert(PT);
  // Inside the body of the operator() of a lambda a this expr might refer to an
  // object in one of the parent location contexts.
  const auto *D = dyn_cast<CXXMethodDecl>(LC->getDecl());
  // FIXME: when operator() of lambda is analyzed as a top level function and
  // 'this' refers to a this to the enclosing scope, there is no right region to
  // return.
  while (!LC->inTopFrame() && (!D || D->isStatic() ||
                               PT != D->getThisType()->getAs<PointerType>())) {
    LC = LC->getParent();
    D = dyn_cast<CXXMethodDecl>(LC->getDecl());
  }
  const StackFrameContext *STC = LC->getStackFrame();
  assert(STC);
  return getSubRegion<CXXThisRegion>(PT, getStackArgumentsRegion(STC));
}

const AllocaRegion*
MemRegionManager::getAllocaRegion(const Expr *E, unsigned cnt,
                                  const LocationContext *LC) {
  const StackFrameContext *STC = LC->getStackFrame();
  assert(STC);
  return getSubRegion<AllocaRegion>(E, cnt, getStackLocalsRegion(STC));
}

const MemSpaceRegion *MemRegion::getMemorySpace() const {
  const MemRegion *R = this;
  const auto *SR = dyn_cast<SubRegion>(this);

  while (SR) {
    R = SR->getSuperRegion();
    SR = dyn_cast<SubRegion>(R);
  }

  return cast<MemSpaceRegion>(R);
}

bool MemRegion::hasStackStorage() const {
  return isa<StackSpaceRegion>(getMemorySpace());
}

bool MemRegion::hasStackNonParametersStorage() const {
  return isa<StackLocalsSpaceRegion>(getMemorySpace());
}

bool MemRegion::hasStackParametersStorage() const {
  return isa<StackArgumentsSpaceRegion>(getMemorySpace());
}

// Strips away all elements and fields.
// Returns the base region of them.
const MemRegion *MemRegion::getBaseRegion() const {
  const MemRegion *R = this;
  while (true) {
    switch (R->getKind()) {
      case MemRegion::ElementRegionKind:
      case MemRegion::FieldRegionKind:
      case MemRegion::ObjCIvarRegionKind:
      case MemRegion::CXXBaseObjectRegionKind:
      case MemRegion::CXXDerivedObjectRegionKind:
        R = cast<SubRegion>(R)->getSuperRegion();
        continue;
      default:
        break;
    }
    break;
  }
  return R;
}

// Returns the region of the root class of a C++ class hierarchy.
const MemRegion *MemRegion::getMostDerivedObjectRegion() const {
  const MemRegion *R = this;
  while (const auto *BR = dyn_cast<CXXBaseObjectRegion>(R))
    R = BR->getSuperRegion();
  return R;
}

bool MemRegion::isSubRegionOf(const MemRegion *) const {
  return false;
}

//===----------------------------------------------------------------------===//
// View handling.
//===----------------------------------------------------------------------===//

const MemRegion *MemRegion::StripCasts(bool StripBaseAndDerivedCasts) const {
  const MemRegion *R = this;
  while (true) {
    switch (R->getKind()) {
    case ElementRegionKind: {
      const auto *ER = cast<ElementRegion>(R);
      if (!ER->getIndex().isZeroConstant())
        return R;
      R = ER->getSuperRegion();
      break;
    }
    case CXXBaseObjectRegionKind:
    case CXXDerivedObjectRegionKind:
      if (!StripBaseAndDerivedCasts)
        return R;
      R = cast<TypedValueRegion>(R)->getSuperRegion();
      break;
    default:
      return R;
    }
  }
}

const SymbolicRegion *MemRegion::getSymbolicBase() const {
  const auto *SubR = dyn_cast<SubRegion>(this);

  while (SubR) {
    if (const auto *SymR = dyn_cast<SymbolicRegion>(SubR))
      return SymR;
    SubR = dyn_cast<SubRegion>(SubR->getSuperRegion());
  }
  return nullptr;
}

RegionRawOffset ElementRegion::getAsArrayOffset() const {
  int64_t offset = 0;
  const ElementRegion *ER = this;
  const MemRegion *superR = nullptr;
  ASTContext &C = getContext();

  // FIXME: Handle multi-dimensional arrays.

  while (ER) {
    superR = ER->getSuperRegion();

    // FIXME: generalize to symbolic offsets.
    SVal index = ER->getIndex();
    if (auto CI = index.getAs<nonloc::ConcreteInt>()) {
      // Update the offset.
      int64_t i = CI->getValue().getSExtValue();

      if (i != 0) {
        QualType elemType = ER->getElementType();

        // If we are pointing to an incomplete type, go no further.
        if (elemType->isIncompleteType()) {
          superR = ER;
          break;
        }

        int64_t size = C.getTypeSizeInChars(elemType).getQuantity();
        if (auto NewOffset = llvm::checkedMulAdd(i, size, offset)) {
          offset = *NewOffset;
        } else {
          LLVM_DEBUG(llvm::dbgs() << "MemRegion::getAsArrayOffset: "
                                  << "offset overflowing, returning unknown\n");

          return nullptr;
        }
      }

      // Go to the next ElementRegion (if any).
      ER = dyn_cast<ElementRegion>(superR);
      continue;
    }

    return nullptr;
  }

  assert(superR && "super region cannot be NULL");
  return RegionRawOffset(superR, CharUnits::fromQuantity(offset));
}

/// Returns true if \p Base is an immediate base class of \p Child
static bool isImmediateBase(const CXXRecordDecl *Child,
                            const CXXRecordDecl *Base) {
  assert(Child && "Child must not be null");
  // Note that we do NOT canonicalize the base class here, because
  // ASTRecordLayout doesn't either. If that leads us down the wrong path,
  // so be it; at least we won't crash.
  for (const auto &I : Child->bases()) {
    if (I.getType()->getAsCXXRecordDecl() == Base)
      return true;
  }

  return false;
}

static RegionOffset calculateOffset(const MemRegion *R) {
  const MemRegion *SymbolicOffsetBase = nullptr;
  int64_t Offset = 0;

  while (true) {
    switch (R->getKind()) {
    case MemRegion::CodeSpaceRegionKind:
    case MemRegion::StackLocalsSpaceRegionKind:
    case MemRegion::StackArgumentsSpaceRegionKind:
    case MemRegion::HeapSpaceRegionKind:
    case MemRegion::UnknownSpaceRegionKind:
    case MemRegion::StaticGlobalSpaceRegionKind:
    case MemRegion::GlobalInternalSpaceRegionKind:
    case MemRegion::GlobalSystemSpaceRegionKind:
    case MemRegion::GlobalImmutableSpaceRegionKind:
      // Stores can bind directly to a region space to set a default value.
      assert(Offset == 0 && !SymbolicOffsetBase);
      goto Finish;

    case MemRegion::FunctionCodeRegionKind:
    case MemRegion::BlockCodeRegionKind:
    case MemRegion::BlockDataRegionKind:
      // These will never have bindings, but may end up having values requested
      // if the user does some strange casting.
      if (Offset != 0)
        SymbolicOffsetBase = R;
      goto Finish;

    case MemRegion::SymbolicRegionKind:
    case MemRegion::AllocaRegionKind:
    case MemRegion::CompoundLiteralRegionKind:
    case MemRegion::CXXThisRegionKind:
    case MemRegion::StringRegionKind:
    case MemRegion::ObjCStringRegionKind:
    case MemRegion::NonParamVarRegionKind:
    case MemRegion::ParamVarRegionKind:
    case MemRegion::CXXTempObjectRegionKind:
    case MemRegion::CXXLifetimeExtendedObjectRegionKind:
      // Usual base regions.
      goto Finish;

    case MemRegion::ObjCIvarRegionKind:
      // This is a little strange, but it's a compromise between
      // ObjCIvarRegions having unknown compile-time offsets (when using the
      // non-fragile runtime) and yet still being distinct, non-overlapping
      // regions. Thus we treat them as "like" base regions for the purposes
      // of computing offsets.
      goto Finish;

    case MemRegion::CXXBaseObjectRegionKind: {
      const auto *BOR = cast<CXXBaseObjectRegion>(R);
      R = BOR->getSuperRegion();

      QualType Ty;
      bool RootIsSymbolic = false;
      if (const auto *TVR = dyn_cast<TypedValueRegion>(R)) {
        Ty = TVR->getDesugaredValueType(R->getContext());
      } else if (const auto *SR = dyn_cast<SymbolicRegion>(R)) {
        // If our base region is symbolic, we don't know what type it really is.
        // Pretend the type of the symbol is the true dynamic type.
        // (This will at least be self-consistent for the life of the symbol.)
        Ty = SR->getPointeeStaticType();
        RootIsSymbolic = true;
      }

      const CXXRecordDecl *Child = Ty->getAsCXXRecordDecl();
      if (!Child) {
        // We cannot compute the offset of the base class.
        SymbolicOffsetBase = R;
      } else {
        if (RootIsSymbolic) {
          // Base layers on symbolic regions may not be type-correct.
          // Double-check the inheritance here, and revert to a symbolic offset
          // if it's invalid (e.g. due to a reinterpret_cast).
          if (BOR->isVirtual()) {
            if (!Child->isVirtuallyDerivedFrom(BOR->getDecl()))
              SymbolicOffsetBase = R;
          } else {
            if (!isImmediateBase(Child, BOR->getDecl()))
              SymbolicOffsetBase = R;
          }
        }
      }

      // Don't bother calculating precise offsets if we already have a
      // symbolic offset somewhere in the chain.
      if (SymbolicOffsetBase)
        continue;

      CharUnits BaseOffset;
      const ASTRecordLayout &Layout = R->getContext().getASTRecordLayout(Child);
      if (BOR->isVirtual())
        BaseOffset = Layout.getVBaseClassOffset(BOR->getDecl());
      else
        BaseOffset = Layout.getBaseClassOffset(BOR->getDecl());

      // The base offset is in chars, not in bits.
      Offset += BaseOffset.getQuantity() * R->getContext().getCharWidth();
      break;
    }

    case MemRegion::CXXDerivedObjectRegionKind: {
      // TODO: Store the base type in the CXXDerivedObjectRegion and use it.
      goto Finish;
    }

    case MemRegion::ElementRegionKind: {
      const auto *ER = cast<ElementRegion>(R);
      R = ER->getSuperRegion();

      QualType EleTy = ER->getValueType();
      if (EleTy->isIncompleteType()) {
        // We cannot compute the offset of the base class.
        SymbolicOffsetBase = R;
        continue;
      }

      SVal Index = ER->getIndex();
      if (std::optional<nonloc::ConcreteInt> CI =
              Index.getAs<nonloc::ConcreteInt>()) {
        // Don't bother calculating precise offsets if we already have a
        // symbolic offset somewhere in the chain.
        if (SymbolicOffsetBase)
          continue;

        int64_t i = CI->getValue().getSExtValue();
        // This type size is in bits.
        Offset += i * R->getContext().getTypeSize(EleTy);
      } else {
        // We cannot compute offset for non-concrete index.
        SymbolicOffsetBase = R;
      }
      break;
    }
    case MemRegion::FieldRegionKind: {
      const auto *FR = cast<FieldRegion>(R);
      R = FR->getSuperRegion();
      assert(R);

      const RecordDecl *RD = FR->getDecl()->getParent();
      if (RD->isUnion() || !RD->isCompleteDefinition()) {
        // We cannot compute offset for incomplete type.
        // For unions, we could treat everything as offset 0, but we'd rather
        // treat each field as a symbolic offset so they aren't stored on top
        // of each other, since we depend on things in typed regions actually
        // matching their types.
        SymbolicOffsetBase = R;
      }

      // Don't bother calculating precise offsets if we already have a
      // symbolic offset somewhere in the chain.
      if (SymbolicOffsetBase)
        continue;

      // Get the field number.
      unsigned idx = 0;
      for (RecordDecl::field_iterator FI = RD->field_begin(),
             FE = RD->field_end(); FI != FE; ++FI, ++idx) {
        if (FR->getDecl() == *FI)
          break;
      }
      const ASTRecordLayout &Layout = R->getContext().getASTRecordLayout(RD);
      // This is offset in bits.
      Offset += Layout.getFieldOffset(idx);
      break;
    }
    }
  }

 Finish:
  if (SymbolicOffsetBase)
    return RegionOffset(SymbolicOffsetBase, RegionOffset::Symbolic);
  return RegionOffset(R, Offset);
}

RegionOffset MemRegion::getAsOffset() const {
  if (!cachedOffset)
    cachedOffset = calculateOffset(this);
  return *cachedOffset;
}

//===----------------------------------------------------------------------===//
// BlockDataRegion
//===----------------------------------------------------------------------===//

std::pair<const VarRegion *, const VarRegion *>
BlockDataRegion::getCaptureRegions(const VarDecl *VD) {
  MemRegionManager &MemMgr = getMemRegionManager();
  const VarRegion *VR = nullptr;
  const VarRegion *OriginalVR = nullptr;

  if (!VD->hasAttr<BlocksAttr>() && VD->hasLocalStorage()) {
    VR = MemMgr.getNonParamVarRegion(VD, this);
    OriginalVR = MemMgr.getVarRegion(VD, LC);
  }
  else {
    if (LC) {
      VR = MemMgr.getVarRegion(VD, LC);
      OriginalVR = VR;
    }
    else {
      VR = MemMgr.getNonParamVarRegion(VD, MemMgr.getUnknownRegion());
      OriginalVR = MemMgr.getVarRegion(VD, LC);
    }
  }
  return std::make_pair(VR, OriginalVR);
}

void BlockDataRegion::LazyInitializeReferencedVars() {
  if (ReferencedVars)
    return;

  AnalysisDeclContext *AC = getCodeRegion()->getAnalysisDeclContext();
  const auto &ReferencedBlockVars = AC->getReferencedBlockVars(BC->getDecl());
  auto NumBlockVars =
      std::distance(ReferencedBlockVars.begin(), ReferencedBlockVars.end());

  if (NumBlockVars == 0) {
    ReferencedVars = (void*) 0x1;
    return;
  }

  MemRegionManager &MemMgr = getMemRegionManager();
  llvm::BumpPtrAllocator &A = MemMgr.getAllocator();
  BumpVectorContext BC(A);

  using VarVec = BumpVector<const MemRegion *>;

  auto *BV = new (A) VarVec(BC, NumBlockVars);
  auto *BVOriginal = new (A) VarVec(BC, NumBlockVars);

  for (const auto *VD : ReferencedBlockVars) {
    const VarRegion *VR = nullptr;
    const VarRegion *OriginalVR = nullptr;
    std::tie(VR, OriginalVR) = getCaptureRegions(VD);
    assert(VR);
    assert(OriginalVR);
    BV->push_back(VR, BC);
    BVOriginal->push_back(OriginalVR, BC);
  }

  ReferencedVars = BV;
  OriginalVars = BVOriginal;
}

BlockDataRegion::referenced_vars_iterator
BlockDataRegion::referenced_vars_begin() const {
  const_cast<BlockDataRegion*>(this)->LazyInitializeReferencedVars();

  auto *Vec = static_cast<BumpVector<const MemRegion *> *>(ReferencedVars);

  if (Vec == (void*) 0x1)
    return BlockDataRegion::referenced_vars_iterator(nullptr, nullptr);

  auto *VecOriginal =
      static_cast<BumpVector<const MemRegion *> *>(OriginalVars);

  return BlockDataRegion::referenced_vars_iterator(Vec->begin(),
                                                   VecOriginal->begin());
}

BlockDataRegion::referenced_vars_iterator
BlockDataRegion::referenced_vars_end() const {
  const_cast<BlockDataRegion*>(this)->LazyInitializeReferencedVars();

  auto *Vec = static_cast<BumpVector<const MemRegion *> *>(ReferencedVars);

  if (Vec == (void*) 0x1)
    return BlockDataRegion::referenced_vars_iterator(nullptr, nullptr);

  auto *VecOriginal =
      static_cast<BumpVector<const MemRegion *> *>(OriginalVars);

  return BlockDataRegion::referenced_vars_iterator(Vec->end(),
                                                   VecOriginal->end());
}

llvm::iterator_range<BlockDataRegion::referenced_vars_iterator>
BlockDataRegion::referenced_vars() const {
  return llvm::make_range(referenced_vars_begin(), referenced_vars_end());
}

const VarRegion *BlockDataRegion::getOriginalRegion(const VarRegion *R) const {
  for (const auto &I : referenced_vars()) {
    if (I.getCapturedRegion() == R)
      return I.getOriginalRegion();
  }
  return nullptr;
}

//===----------------------------------------------------------------------===//
// RegionAndSymbolInvalidationTraits
//===----------------------------------------------------------------------===//

void RegionAndSymbolInvalidationTraits::setTrait(SymbolRef Sym,
                                                 InvalidationKinds IK) {
  SymTraitsMap[Sym] |= IK;
}

void RegionAndSymbolInvalidationTraits::setTrait(const MemRegion *MR,
                                                 InvalidationKinds IK) {
  assert(MR);
  if (const auto *SR = dyn_cast<SymbolicRegion>(MR))
    setTrait(SR->getSymbol(), IK);
  else
    MRTraitsMap[MR] |= IK;
}

bool RegionAndSymbolInvalidationTraits::hasTrait(SymbolRef Sym,
                                                 InvalidationKinds IK) const {
  const_symbol_iterator I = SymTraitsMap.find(Sym);
  if (I != SymTraitsMap.end())
    return I->second & IK;

  return false;
}

bool RegionAndSymbolInvalidationTraits::hasTrait(const MemRegion *MR,
                                                 InvalidationKinds IK) const {
  if (!MR)
    return false;

  if (const auto *SR = dyn_cast<SymbolicRegion>(MR))
    return hasTrait(SR->getSymbol(), IK);

  const_region_iterator I = MRTraitsMap.find(MR);
  if (I != MRTraitsMap.end())
    return I->second & IK;

  return false;
}
