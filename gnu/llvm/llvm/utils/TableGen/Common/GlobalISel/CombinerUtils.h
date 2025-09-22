//===- CombinerUtils.h ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file Utility functions used by both Combiner backends.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_UTILS_TABLEGEN_COMBINERUTILS_H
#define LLVM_UTILS_TABLEGEN_COMBINERUTILS_H

#include "llvm/ADT/StringRef.h"
#include "llvm/TableGen/Record.h"

namespace llvm {

/// A convenience function to check that an Init refers to a specific def. This
/// is primarily useful for testing for defs and similar in DagInit's since
/// DagInit's support any type inside them.
inline bool isSpecificDef(const Init &N, StringRef Def) {
  if (const DefInit *OpI = dyn_cast<DefInit>(&N))
    if (OpI->getDef()->getName() == Def)
      return true;
  return false;
}

/// A convenience function to check that an Init refers to a def that is a
/// subclass of the given class and coerce it to a def if it is. This is
/// primarily useful for testing for subclasses of GIDefKind and similar in
/// DagInit's since DagInit's support any type inside them.
inline Record *getDefOfSubClass(const Init &N, StringRef Cls) {
  if (const DefInit *OpI = dyn_cast<DefInit>(&N))
    if (OpI->getDef()->isSubClassOf(Cls))
      return OpI->getDef();
  return nullptr;
}

/// A convenience function to check that an Init refers to a dag whose operator
/// is a specific def and coerce it to a dag if it is. This is primarily useful
/// for testing for subclasses of GIDefKind and similar in DagInit's since
/// DagInit's support any type inside them.
inline const DagInit *getDagWithSpecificOperator(const Init &N,
                                                 StringRef Name) {
  if (const DagInit *I = dyn_cast<DagInit>(&N))
    if (I->getNumArgs() > 0)
      if (const DefInit *OpI = dyn_cast<DefInit>(I->getOperator()))
        if (OpI->getDef()->getName() == Name)
          return I;
  return nullptr;
}

/// A convenience function to check that an Init refers to a dag whose operator
/// is a def that is a subclass of the given class and coerce it to a dag if it
/// is. This is primarily useful for testing for subclasses of GIDefKind and
/// similar in DagInit's since DagInit's support any type inside them.
inline const DagInit *getDagWithOperatorOfSubClass(const Init &N,
                                                   StringRef Cls) {
  if (const DagInit *I = dyn_cast<DagInit>(&N))
    if (const DefInit *OpI = dyn_cast<DefInit>(I->getOperator()))
      if (OpI->getDef()->isSubClassOf(Cls))
        return I;
  return nullptr;
}

/// Copies a StringRef into a static pool to preserve it.
StringRef insertStrRef(StringRef S);

} // namespace llvm

#endif
