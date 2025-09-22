//===-- ValueObjectUpdater.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/ValueObjectUpdater.h"

using namespace lldb_private;

ValueObjectUpdater::ValueObjectUpdater(lldb::ValueObjectSP in_valobj_sp) {
  if (!in_valobj_sp)
    return;
  // If the user passes in a value object that is dynamic or synthetic, then
  // water it down to the static type.
  m_root_valobj_sp = in_valobj_sp->GetQualifiedRepresentationIfAvailable(
      lldb::eNoDynamicValues, false);
}

lldb::ValueObjectSP ValueObjectUpdater::GetSP() {
  lldb::ProcessSP process_sp = GetProcessSP();
  if (!process_sp)
    return lldb::ValueObjectSP();

  const uint32_t current_stop_id = process_sp->GetLastNaturalStopID();
  if (current_stop_id == m_stop_id)
    return m_user_valobj_sp;

  m_stop_id = current_stop_id;

  if (!m_root_valobj_sp) {
    m_user_valobj_sp.reset();
    return m_root_valobj_sp;
  }

  m_user_valobj_sp = m_root_valobj_sp;

  lldb::ValueObjectSP dynamic_sp =
      m_user_valobj_sp->GetDynamicValue(lldb::eDynamicDontRunTarget);
  if (dynamic_sp)
    m_user_valobj_sp = dynamic_sp;

  lldb::ValueObjectSP synthetic_sp = m_user_valobj_sp->GetSyntheticValue();
  if (synthetic_sp)
    m_user_valobj_sp = synthetic_sp;

  return m_user_valobj_sp;
}

lldb::ProcessSP ValueObjectUpdater::GetProcessSP() const {
  if (m_root_valobj_sp)
    return m_root_valobj_sp->GetProcessSP();
  return lldb::ProcessSP();
}
