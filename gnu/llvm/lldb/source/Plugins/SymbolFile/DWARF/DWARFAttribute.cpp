//===-- DWARFAttribute.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DWARFAttribute.h"
#include "DWARFUnit.h"
#include "DWARFDebugInfo.h"

using namespace lldb_private::dwarf;
using namespace lldb_private::plugin::dwarf;

DWARFAttributes::DWARFAttributes() : m_infos() {}

DWARFAttributes::~DWARFAttributes() = default;

uint32_t DWARFAttributes::FindAttributeIndex(dw_attr_t attr) const {
  collection::const_iterator end = m_infos.end();
  collection::const_iterator beg = m_infos.begin();
  collection::const_iterator pos;
  for (pos = beg; pos != end; ++pos) {
    if (pos->attr.get_attr() == attr)
      return std::distance(beg, pos);
  }
  return UINT32_MAX;
}

void DWARFAttributes::Append(const DWARFFormValue &form_value,
                             dw_offset_t attr_die_offset, dw_attr_t attr) {
  AttributeValue attr_value = {const_cast<DWARFUnit *>(form_value.GetUnit()),
                               attr_die_offset,
                               {attr, form_value.Form(), form_value.Value()}};
  m_infos.push_back(attr_value);
}

bool DWARFAttributes::ExtractFormValueAtIndex(
    uint32_t i, DWARFFormValue &form_value) const {
  const DWARFUnit *cu = CompileUnitAtIndex(i);
  form_value.SetUnit(cu);
  form_value.SetForm(FormAtIndex(i));
  if (form_value.Form() == DW_FORM_implicit_const) {
    form_value.SetValue(ValueAtIndex(i));
    return true;
  }
  lldb::offset_t offset = DIEOffsetAtIndex(i);
  return form_value.ExtractValue(cu->GetData(), &offset);
}

DWARFDIE
DWARFAttributes::FormValueAsReference(dw_attr_t attr) const {
  const uint32_t attr_idx = FindAttributeIndex(attr);
  if (attr_idx != UINT32_MAX)
    return FormValueAsReferenceAtIndex(attr_idx);
  return {};
}

DWARFDIE
DWARFAttributes::FormValueAsReferenceAtIndex(uint32_t i) const {
  DWARFFormValue form_value;
  if (ExtractFormValueAtIndex(i, form_value))
    return form_value.Reference();
  return {};
}
