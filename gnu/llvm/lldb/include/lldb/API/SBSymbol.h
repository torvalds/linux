//===-- SBSymbol.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBSYMBOL_H
#define LLDB_API_SBSYMBOL_H

#include "lldb/API/SBAddress.h"
#include "lldb/API/SBDefines.h"
#include "lldb/API/SBInstructionList.h"
#include "lldb/API/SBTarget.h"

namespace lldb {

class LLDB_API SBSymbol {
public:
  SBSymbol();

  ~SBSymbol();

  SBSymbol(const lldb::SBSymbol &rhs);

  const lldb::SBSymbol &operator=(const lldb::SBSymbol &rhs);

  explicit operator bool() const;

  bool IsValid() const;

  const char *GetName() const;

  const char *GetDisplayName() const;

  const char *GetMangledName() const;

  lldb::SBInstructionList GetInstructions(lldb::SBTarget target);

  lldb::SBInstructionList GetInstructions(lldb::SBTarget target,
                                          const char *flavor_string);

  /// Get the start address of this symbol
  ///
  /// \returns
  ///   If the symbol's value is not an address, an invalid SBAddress object
  ///   will be returned. If the symbol's value is an address, a valid SBAddress
  ///   object will be returned.
  SBAddress GetStartAddress();

  /// Get the end address of this symbol
  ///
  /// \returns
  ///   If the symbol's value is not an address, an invalid SBAddress object
  ///   will be returned. If the symbol's value is an address, a valid SBAddress
  ///   object will be returned.
  SBAddress GetEndAddress();

  /// Get the raw value of a symbol.
  ///
  /// This accessor allows direct access to the symbol's value from the symbol
  /// table regardless of what the value is. The value can be a file address or
  /// it can be an integer value that depends on what the symbol's type is. Some
  /// symbol values are not addresses, but absolute values or integer values
  /// that can be mean different things. The GetStartAddress() accessor will
  /// only return a valid SBAddress if the symbol's value is an address, so this
  /// accessor provides a way to access the symbol's value when the value is
  /// not an address.
  ///
  /// \returns
  ///   Returns the raw integer value of a symbol from the symbol table.
  uint64_t GetValue();

  /// Get the size of the symbol.
  ///
  /// This accessor allows direct access to the symbol's size from the symbol
  /// table regardless of what the value is (address or integer value).
  ///
  /// \returns
  ///   Returns the size of a symbol from the symbol table.
  uint64_t GetSize();

  uint32_t GetPrologueByteSize();

  SymbolType GetType();

  bool operator==(const lldb::SBSymbol &rhs) const;

  bool operator!=(const lldb::SBSymbol &rhs) const;

  bool GetDescription(lldb::SBStream &description);

  // Returns true if the symbol is externally visible in the module that it is
  // defined in
  bool IsExternal();

  // Returns true if the symbol was synthetically generated from something
  // other than the actual symbol table itself in the object file.
  bool IsSynthetic();

protected:
  lldb_private::Symbol *get();

  void reset(lldb_private::Symbol *);

private:
  friend class SBAddress;
  friend class SBFrame;
  friend class SBModule;
  friend class SBSymbolContext;

  SBSymbol(lldb_private::Symbol *lldb_object_ptr);

  void SetSymbol(lldb_private::Symbol *lldb_object_ptr);

  lldb_private::Symbol *m_opaque_ptr = nullptr;
};

} // namespace lldb

#endif // LLDB_API_SBSYMBOL_H
