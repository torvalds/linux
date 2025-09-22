//===- llvm/ValueSymbolTable.h - Implement a Value Symtab -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the name/Value symbol table for LLVM.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_VALUESYMBOLTABLE_H
#define LLVM_IR_VALUESYMBOLTABLE_H

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Value.h"
#include <cstdint>

namespace llvm {

class Argument;
class BasicBlock;
class Function;
class GlobalAlias;
class GlobalIFunc;
class GlobalVariable;
class Instruction;
template <bool ExtraIteratorBits> struct ilist_iterator_bits;
template <class ParentTy> struct ilist_parent;
template <unsigned InternalLen> class SmallString;
template <typename ValueSubClass, typename ... Args> class SymbolTableListTraits;

/// This class provides a symbol table of name/value pairs. It is essentially
/// a std::map<std::string,Value*> but has a controlled interface provided by
/// LLVM as well as ensuring uniqueness of names.
///
class ValueSymbolTable {
  friend class SymbolTableListTraits<Argument>;
  friend class SymbolTableListTraits<BasicBlock>;
  friend class SymbolTableListTraits<Function>;
  friend class SymbolTableListTraits<GlobalAlias>;
  friend class SymbolTableListTraits<GlobalIFunc>;
  friend class SymbolTableListTraits<GlobalVariable>;
  friend class SymbolTableListTraits<Instruction, ilist_iterator_bits<true>,
                                     ilist_parent<BasicBlock>>;
  friend class Value;

/// @name Types
/// @{
public:
  /// A mapping of names to values.
  using ValueMap = StringMap<Value*>;

  /// An iterator over a ValueMap.
  using iterator = ValueMap::iterator;

  /// A const_iterator over a ValueMap.
  using const_iterator = ValueMap::const_iterator;

/// @}
/// @name Constructors
/// @{

  ValueSymbolTable(int MaxNameSize = -1) : vmap(0), MaxNameSize(MaxNameSize) {}
  ~ValueSymbolTable();

  /// @}
  /// @name Accessors
  /// @{

  /// This method finds the value with the given \p Name in the
  /// the symbol table.
  /// @returns the value associated with the \p Name
  /// Lookup a named Value.
  Value *lookup(StringRef Name) const {
    if (MaxNameSize > -1 && Name.size() > (unsigned)MaxNameSize)
      Name = Name.substr(0, std::max(1u, (unsigned)MaxNameSize));

    return vmap.lookup(Name);
  }

  /// @returns true iff the symbol table is empty
  /// Determine if the symbol table is empty
  inline bool empty() const { return vmap.empty(); }

  /// The number of name/type pairs is returned.
  inline unsigned size() const { return unsigned(vmap.size()); }

  /// This function can be used from the debugger to display the
  /// content of the symbol table while debugging.
  /// Print out symbol table on stderr
  void dump() const;

/// @}
/// @name Iteration
/// @{

  /// Get an iterator that from the beginning of the symbol table.
  inline iterator begin() { return vmap.begin(); }

  /// Get a const_iterator that from the beginning of the symbol table.
  inline const_iterator begin() const { return vmap.begin(); }

  /// Get an iterator to the end of the symbol table.
  inline iterator end() { return vmap.end(); }

  /// Get a const_iterator to the end of the symbol table.
  inline const_iterator end() const { return vmap.end(); }

  /// @}
  /// @name Mutators
  /// @{
private:
  ValueName *makeUniqueName(Value *V, SmallString<256> &UniqueName);

  /// This method adds the provided value \p N to the symbol table.  The Value
  /// must have a name which is used to place the value in the symbol table.
  /// If the inserted name conflicts, this renames the value.
  /// Add a named value to the symbol table
  void reinsertValue(Value *V);

  /// createValueName - This method attempts to create a value name and insert
  /// it into the symbol table with the specified name.  If it conflicts, it
  /// auto-renames the name and returns that instead.
  ValueName *createValueName(StringRef Name, Value *V);

  /// This method removes a value from the symbol table.  It leaves the
  /// ValueName attached to the value, but it is no longer inserted in the
  /// symtab.
  void removeValueName(ValueName *V);

  /// @}
  /// @name Internal Data
  /// @{

  ValueMap vmap;                    ///< The map that holds the symbol table.
  int MaxNameSize; ///< The maximum size for each name. If the limit is
                   ///< exceeded, the name is capped.
  mutable uint32_t LastUnique = 0;  ///< Counter for tracking unique names

/// @}
};

} // end namespace llvm

#endif // LLVM_IR_VALUESYMBOLTABLE_H
