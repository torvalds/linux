//===-- DiffEngine.h - File comparator --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header defines the interface to the llvm-tapi difference engine,
// which structurally compares two tbd files.
//
//===----------------------------------------------------------------------===/
#ifndef LLVM_TOOLS_LLVM_TAPI_DIFF_DIFFENGINE_H
#define LLVM_TOOLS_LLVM_TAPI_DIFF_DIFFENGINE_H

#include "llvm/Object/TapiUniversal.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TextAPI/Symbol.h"
#include "llvm/TextAPI/Target.h"

namespace llvm {

/// InterfaceInputOrder determines from which file the diff attribute belongs
/// to.
enum InterfaceInputOrder { lhs, rhs };

/// DiffAttrKind is the enum that holds the concrete bases for RTTI.
enum DiffAttrKind {
  AD_Diff_Scalar_PackedVersion,
  AD_Diff_Scalar_Unsigned,
  AD_Diff_Scalar_Bool,
  AD_Diff_Scalar_Str,
  AD_Str_Vec,
  AD_Sym_Vec,
  AD_Inline_Doc,
};

/// AttributeDiff is the abstract class for RTTI.
class AttributeDiff {
public:
  AttributeDiff(DiffAttrKind Kind) : Kind(Kind){};
  virtual ~AttributeDiff(){};
  DiffAttrKind getKind() const { return Kind; }

private:
  DiffAttrKind Kind;
};

/// DiffOutput is the representation of a diff for a single attribute.
struct DiffOutput {
  /// The name of the attribute.
  std::string Name;
  /// The kind for RTTI
  DiffAttrKind Kind;
  /// Different values for the attribute
  /// from each file where a diff is present.
  std::vector<std::unique_ptr<AttributeDiff>> Values;
  DiffOutput(std::string Name) : Name(Name){};
};

/// DiffScalarVal is a template class for the different types of scalar values.
template <class T, DiffAttrKind U> class DiffScalarVal : public AttributeDiff {
public:
  DiffScalarVal(InterfaceInputOrder Order, T Val)
      : AttributeDiff(U), Order(Order), Val(Val){};

  static bool classof(const AttributeDiff *A) { return A->getKind() == U; }

  void print(raw_ostream &, std::string);

  T getVal() const { return Val; }
  InterfaceInputOrder getOrder() const { return Order; }

private:
  /// The order is the file from which the diff is found.
  InterfaceInputOrder Order;
  T Val;
};

/// SymScalar is the diff symbol and the order.
class SymScalar {
public:
  SymScalar(InterfaceInputOrder Order, const MachO::Symbol *Sym)
      : Order(Order), Val(Sym){};

  std::string getFlagString(const MachO::Symbol *Sym);

  void print(raw_ostream &OS, std::string Indent, MachO::Target Targ);

  const MachO::Symbol *getVal() const { return Val; }
  InterfaceInputOrder getOrder() const { return Order; }

private:
  /// The order is the file from which the diff is found.
  InterfaceInputOrder Order;
  const MachO::Symbol *Val;
  StringLiteral getSymbolNamePrefix(MachO::EncodeKind Kind);
};

class DiffStrVec : public AttributeDiff {
public:
  MachO::Target Targ;
  /// Values is a vector of StringRef values associated with the target.
  std::vector<DiffScalarVal<StringRef, AD_Diff_Scalar_Str>> TargValues;
  DiffStrVec(MachO::Target Targ) : AttributeDiff(AD_Str_Vec), Targ(Targ){};

  static bool classof(const AttributeDiff *A) {
    return A->getKind() == AD_Str_Vec;
  }
};

class DiffSymVec : public AttributeDiff {
public:
  MachO::Target Targ;
  /// Values is a vector of symbol values associated with the target.
  std::vector<SymScalar> TargValues;
  DiffSymVec(MachO::Target Targ) : AttributeDiff(AD_Sym_Vec), Targ(Targ){};

  static bool classof(const AttributeDiff *A) {
    return A->getKind() == AD_Sym_Vec;
  }
};

/// InlineDoc represents an inlined framework/library in a TBD File.
class InlineDoc : public AttributeDiff {
public:
  /// Install name of the framework/library.
  std::string InstallName;
  /// Differences found from each file.
  std::vector<DiffOutput> DocValues;
  InlineDoc(StringRef InstName, std::vector<DiffOutput> Diff)
      : AttributeDiff(AD_Inline_Doc), InstallName(InstName),
        DocValues(std::move(Diff)){};

  static bool classof(const AttributeDiff *A) {
    return A->getKind() == AD_Inline_Doc;
  }
};

/// DiffEngine contains the methods to compare the input files and print the
/// output of the differences found in the files.
class DiffEngine {
public:
  DiffEngine(MachO::InterfaceFile *InputFileNameLHS,
             MachO::InterfaceFile *InputFileNameRHS)
      : FileLHS(InputFileNameLHS), FileRHS(InputFileNameRHS){};
  bool compareFiles(raw_ostream &);

private:
  MachO::InterfaceFile *FileLHS;
  MachO::InterfaceFile *FileRHS;

  /// Function that prints the differences found in the files.
  void printDifferences(raw_ostream &, const std::vector<DiffOutput> &, int);
  /// Function that does the comparison of the TBD files and returns the
  /// differences.
  std::vector<DiffOutput> findDifferences(const MachO::InterfaceFile *,
                                          const MachO::InterfaceFile *);
};

} // namespace llvm

#endif
