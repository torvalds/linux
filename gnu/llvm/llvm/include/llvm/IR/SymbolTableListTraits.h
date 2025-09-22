//===- llvm/SymbolTableListTraits.h - Traits for iplist ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a generic class that is used to implement the automatic
// symbol table manipulation that occurs when you put (for example) a named
// instruction into a basic block.
//
// The way that this is implemented is by using a special traits class with the
// intrusive list that makes up the list of instructions in a basic block.  When
// a new element is added to the list of instructions, the traits class is
// notified, allowing the symbol table to be updated.
//
// This generic class implements the traits class.  It must be generic so that
// it can work for all uses it, which include lists of instructions, basic
// blocks, arguments, functions, global variables, etc...
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_SYMBOLTABLELISTTRAITS_H
#define LLVM_IR_SYMBOLTABLELISTTRAITS_H

#include "llvm/ADT/ilist.h"
#include "llvm/ADT/simple_ilist.h"
#include <cstddef>

namespace llvm {

class Argument;
class BasicBlock;
class Function;
class GlobalAlias;
class GlobalIFunc;
class GlobalVariable;
class Instruction;
class Module;
class ValueSymbolTable;

/// Template metafunction to get the parent type for a symbol table list.
///
/// Implementations create a typedef called \c type so that we only need a
/// single template parameter for the list and traits.
template <typename NodeTy> struct SymbolTableListParentType {};

#define DEFINE_SYMBOL_TABLE_PARENT_TYPE(NODE, PARENT)                          \
  template <> struct SymbolTableListParentType<NODE> { using type = PARENT; };
DEFINE_SYMBOL_TABLE_PARENT_TYPE(Instruction, BasicBlock)
DEFINE_SYMBOL_TABLE_PARENT_TYPE(BasicBlock, Function)
DEFINE_SYMBOL_TABLE_PARENT_TYPE(Argument, Function)
DEFINE_SYMBOL_TABLE_PARENT_TYPE(Function, Module)
DEFINE_SYMBOL_TABLE_PARENT_TYPE(GlobalVariable, Module)
DEFINE_SYMBOL_TABLE_PARENT_TYPE(GlobalAlias, Module)
DEFINE_SYMBOL_TABLE_PARENT_TYPE(GlobalIFunc, Module)
#undef DEFINE_SYMBOL_TABLE_PARENT_TYPE

template <typename NodeTy, typename... Args> class SymbolTableList;

// ValueSubClass   - The type of objects that I hold, e.g. Instruction.
// ItemParentClass - The type of object that owns the list, e.g. BasicBlock.
// OptionsT        - Extra options to ilist nodes.
//
template <typename ValueSubClass, typename... Args>
class SymbolTableListTraits : public ilist_alloc_traits<ValueSubClass> {
  using ListTy = SymbolTableList<ValueSubClass, Args...>;
  using iterator = typename simple_ilist<ValueSubClass, Args...>::iterator;
  using ItemParentClass =
      typename SymbolTableListParentType<ValueSubClass>::type;

public:
  SymbolTableListTraits() = default;

private:
  /// getListOwner - Return the object that owns this list.  If this is a list
  /// of instructions, it returns the BasicBlock that owns them.
  ItemParentClass *getListOwner() {
    size_t Offset = reinterpret_cast<size_t>(
        &((ItemParentClass *)nullptr->*ItemParentClass::getSublistAccess(
                                           static_cast<ValueSubClass *>(
                                               nullptr))));
    ListTy *Anchor = static_cast<ListTy *>(this);
    return reinterpret_cast<ItemParentClass*>(reinterpret_cast<char*>(Anchor)-
                                              Offset);
  }

  static ListTy &getList(ItemParentClass *Par) {
    return Par->*(Par->getSublistAccess((ValueSubClass*)nullptr));
  }

  static ValueSymbolTable *getSymTab(ItemParentClass *Par) {
    return Par ? toPtr(Par->getValueSymbolTable()) : nullptr;
  }

public:
  void addNodeToList(ValueSubClass *V);
  void removeNodeFromList(ValueSubClass *V);
  void transferNodesFromList(SymbolTableListTraits &L2, iterator first,
                             iterator last);
  // private:
  template<typename TPtr>
  void setSymTabObject(TPtr *, TPtr);
  static ValueSymbolTable *toPtr(ValueSymbolTable *P) { return P; }
  static ValueSymbolTable *toPtr(ValueSymbolTable &R) { return &R; }
};

/// List that automatically updates parent links and symbol tables.
///
/// When nodes are inserted into and removed from this list, the associated
/// symbol table will be automatically updated.  Similarly, parent links get
/// updated automatically.
template <class T, typename... Args>
class SymbolTableList : public iplist_impl<simple_ilist<T, Args...>,
                                           SymbolTableListTraits<T, Args...>> {
};

} // end namespace llvm

#endif // LLVM_IR_SYMBOLTABLELISTTRAITS_H
