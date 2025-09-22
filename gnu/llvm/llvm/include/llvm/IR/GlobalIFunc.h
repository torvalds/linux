//===-------- llvm/GlobalIFunc.h - GlobalIFunc class ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declaration of the GlobalIFunc class, which
/// represents a single indirect function in the IR. Indirect function uses
/// ELF symbol type extension to mark that the address of a declaration should
/// be resolved at runtime by calling a resolver function.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_GLOBALIFUNC_H
#define LLVM_IR_GLOBALIFUNC_H

#include "llvm/ADT/ilist_node.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/GlobalObject.h"
#include "llvm/IR/OperandTraits.h"
#include "llvm/IR/Value.h"

namespace llvm {

class Twine;
class Module;

// Traits class for using GlobalIFunc in symbol table in Module.
template <typename ValueSubClass, typename... Args> class SymbolTableListTraits;

class GlobalIFunc final : public GlobalObject, public ilist_node<GlobalIFunc> {
  friend class SymbolTableListTraits<GlobalIFunc>;

  GlobalIFunc(Type *Ty, unsigned AddressSpace, LinkageTypes Linkage,
              const Twine &Name, Constant *Resolver, Module *Parent);

public:
  GlobalIFunc(const GlobalIFunc &) = delete;
  GlobalIFunc &operator=(const GlobalIFunc &) = delete;

  /// If a parent module is specified, the ifunc is automatically inserted into
  /// the end of the specified module's ifunc list.
  static GlobalIFunc *create(Type *Ty, unsigned AddressSpace,
                             LinkageTypes Linkage, const Twine &Name,
                             Constant *Resolver, Module *Parent);

  // allocate space for exactly one operand
  void *operator new(size_t S) { return User::operator new(S, 1); }
  void operator delete(void *Ptr) { User::operator delete(Ptr); }

  /// Provide fast operand accessors
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Constant);

  void copyAttributesFrom(const GlobalIFunc *Src) {
    GlobalObject::copyAttributesFrom(Src);
  }

  /// This method unlinks 'this' from the containing module, but does not
  /// delete it.
  void removeFromParent();

  /// This method unlinks 'this' from the containing module and deletes it.
  void eraseFromParent();

  /// These methods retrieve and set ifunc resolver function.
  void setResolver(Constant *Resolver) { Op<0>().set(Resolver); }
  const Constant *getResolver() const {
    return static_cast<Constant *>(Op<0>().get());
  }
  Constant *getResolver() { return static_cast<Constant *>(Op<0>().get()); }

  // Return the resolver function after peeling off potential ConstantExpr
  // indirection.
  const Function *getResolverFunction() const;
  Function *getResolverFunction() {
    return const_cast<Function *>(
        static_cast<const GlobalIFunc *>(this)->getResolverFunction());
  }

  static FunctionType *getResolverFunctionType(Type *IFuncValTy) {
    return FunctionType::get(IFuncValTy->getPointerTo(), false);
  }

  static bool isValidLinkage(LinkageTypes L) {
    return isExternalLinkage(L) || isLocalLinkage(L) || isWeakLinkage(L) ||
           isLinkOnceLinkage(L);
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const Value *V) {
    return V->getValueID() == Value::GlobalIFuncVal;
  }

  // Apply specific operation to all resolver-related values. If resolver target
  // is already a global object, then apply the operation to it directly. If
  // target is a GlobalExpr or a GlobalAlias, evaluate it to its base object and
  // apply the operation for the base object and all aliases along the path.
  void applyAlongResolverPath(function_ref<void(const GlobalValue &)> Op) const;
};

template <>
struct OperandTraits<GlobalIFunc>
    : public FixedNumOperandTraits<GlobalIFunc, 1> {};

DEFINE_TRANSPARENT_OPERAND_ACCESSORS(GlobalIFunc, Constant)

} // end namespace llvm

#endif // LLVM_IR_GLOBALIFUNC_H
