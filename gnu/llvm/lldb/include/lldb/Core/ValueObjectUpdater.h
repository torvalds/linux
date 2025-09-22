//===-- ValueObjectUpdater.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_VALUEOBJECTUPDATER_H
#define LLDB_CORE_VALUEOBJECTUPDATER_H

#include "lldb/Core/ValueObject.h"

namespace lldb_private {

/// A value object class that is seeded with the static variable value
/// and it vends the user facing value object. If the type is dynamic it can
/// vend the dynamic type. If this user type also has a synthetic type
/// associated with it, it will vend the synthetic type. The class watches the
/// process' stop ID and will update the user type when needed.
class ValueObjectUpdater {
  /// The root value object is the static typed variable object.
  lldb::ValueObjectSP m_root_valobj_sp;
  /// The user value object is the value object the user wants to see.
  lldb::ValueObjectSP m_user_valobj_sp;
  /// The stop ID that m_user_valobj_sp is valid for.
  uint32_t m_stop_id = UINT32_MAX;

public:
  ValueObjectUpdater(lldb::ValueObjectSP in_valobj_sp);

  /// Gets the correct value object from the root object for a given process
  /// stop ID. If dynamic values are enabled, or if synthetic children are
  /// enabled, the value object that the user wants to see might change while
  /// debugging.
  lldb::ValueObjectSP GetSP();

  lldb::ProcessSP GetProcessSP() const;
};

} // namespace lldb_private

#endif // LLDB_CORE_VALUEOBJECTUPDATER_H
