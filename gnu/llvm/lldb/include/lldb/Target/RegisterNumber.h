//===-- RegisterNumber.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_REGISTERNUMBER_H
#define LLDB_TARGET_REGISTERNUMBER_H

#include "lldb/lldb-private.h"
#include <map>

/// A class to represent register numbers, and able to convert between
/// different register numbering schemes that may be used in a single
/// debug session.

class RegisterNumber {
public:
  RegisterNumber(lldb_private::Thread &thread, lldb::RegisterKind kind,
                 uint32_t num);

  // This constructor plus the init() method below allow for the placeholder
  // creation of an invalid object initially, possibly to be filled in.  It
  // would be more consistent to have three Set* methods to set the three data
  // that the object needs.
  RegisterNumber();

  void init(lldb_private::Thread &thread, lldb::RegisterKind kind,
            uint32_t num);

  const RegisterNumber &operator=(const RegisterNumber &rhs);

  bool operator==(RegisterNumber &rhs);

  bool operator!=(RegisterNumber &rhs);

  bool IsValid() const;

  uint32_t GetAsKind(lldb::RegisterKind kind);

  uint32_t GetRegisterNumber() const;

  lldb::RegisterKind GetRegisterKind() const;

  const char *GetName();

private:
  typedef std::map<lldb::RegisterKind, uint32_t> Collection;

  lldb::RegisterContextSP m_reg_ctx_sp;
  uint32_t m_regnum = LLDB_INVALID_REGNUM;
  lldb::RegisterKind m_kind = lldb::kNumRegisterKinds;
  Collection m_kind_regnum_map;
  const char *m_name = nullptr;
};

#endif // LLDB_TARGET_REGISTERNUMBER_H
