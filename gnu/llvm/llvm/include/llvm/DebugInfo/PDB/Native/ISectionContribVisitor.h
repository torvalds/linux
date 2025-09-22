//===- ISectionContribVisitor.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_ISECTIONCONTRIBVISITOR_H
#define LLVM_DEBUGINFO_PDB_NATIVE_ISECTIONCONTRIBVISITOR_H

namespace llvm {
namespace pdb {

struct SectionContrib;
struct SectionContrib2;

class ISectionContribVisitor {
public:
  virtual ~ISectionContribVisitor() = default;

  virtual void visit(const SectionContrib &C) = 0;
  virtual void visit(const SectionContrib2 &C) = 0;
};

} // end namespace pdb
} // end namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_ISECTIONCONTRIBVISITOR_H
