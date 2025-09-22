//===-------- llvm/GlobalAlias.h - GlobalAlias class ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the GlobalAlias class, which
// represents a single function or variable alias in the IR.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_GLOBALALIAS_H
#define LLVM_IR_GLOBALALIAS_H

#include "llvm/ADT/ilist_node.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/OperandTraits.h"
#include "llvm/IR/Value.h"

namespace llvm {

class Twine;
class Module;
template <typename ValueSubClass, typename... Args> class SymbolTableListTraits;

class GlobalAlias : public GlobalValue, public ilist_node<GlobalAlias> {
  friend class SymbolTableListTraits<GlobalAlias>;

  GlobalAlias(Type *Ty, unsigned AddressSpace, LinkageTypes Linkage,
              const Twine &Name, Constant *Aliasee, Module *Parent);

public:
  GlobalAlias(const GlobalAlias &) = delete;
  GlobalAlias &operator=(const GlobalAlias &) = delete;

  /// If a parent module is specified, the alias is automatically inserted into
  /// the end of the specified module's alias list.
  static GlobalAlias *create(Type *Ty, unsigned AddressSpace,
                             LinkageTypes Linkage, const Twine &Name,
                             Constant *Aliasee, Module *Parent);

  // Without the Aliasee.
  static GlobalAlias *create(Type *Ty, unsigned AddressSpace,
                             LinkageTypes Linkage, const Twine &Name,
                             Module *Parent);

  // The module is taken from the Aliasee.
  static GlobalAlias *create(Type *Ty, unsigned AddressSpace,
                             LinkageTypes Linkage, const Twine &Name,
                             GlobalValue *Aliasee);

  // Type, Parent and AddressSpace taken from the Aliasee.
  static GlobalAlias *create(LinkageTypes Linkage, const Twine &Name,
                             GlobalValue *Aliasee);

  // Linkage, Type, Parent and AddressSpace taken from the Aliasee.
  static GlobalAlias *create(const Twine &Name, GlobalValue *Aliasee);

  // allocate space for exactly one operand
  void *operator new(size_t S) { return User::operator new(S, 1); }
  void operator delete(void *Ptr) { User::operator delete(Ptr); }

  /// Provide fast operand accessors
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Constant);

  void copyAttributesFrom(const GlobalAlias *Src) {
    GlobalValue::copyAttributesFrom(Src);
  }

  /// removeFromParent - This method unlinks 'this' from the containing module,
  /// but does not delete it.
  ///
  void removeFromParent();

  /// eraseFromParent - This method unlinks 'this' from the containing module
  /// and deletes it.
  ///
  void eraseFromParent();

  /// These methods retrieve and set alias target.
  void setAliasee(Constant *Aliasee);
  const Constant *getAliasee() const {
    return static_cast<Constant *>(Op<0>().get());
  }
  Constant *getAliasee() { return static_cast<Constant *>(Op<0>().get()); }

  const GlobalObject *getAliaseeObject() const;
  GlobalObject *getAliaseeObject() {
    return const_cast<GlobalObject *>(
        static_cast<const GlobalAlias *>(this)->getAliaseeObject());
  }

  static bool isValidLinkage(LinkageTypes L) {
    return isExternalLinkage(L) || isLocalLinkage(L) || isWeakLinkage(L) ||
           isLinkOnceLinkage(L) || isAvailableExternallyLinkage(L);
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Value *V) {
    return V->getValueID() == Value::GlobalAliasVal;
  }
};

template <>
struct OperandTraits<GlobalAlias>
    : public FixedNumOperandTraits<GlobalAlias, 1> {};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(GlobalAlias, Constant)

} // end namespace llvm

#endif // LLVM_IR_GLOBALALIAS_H
