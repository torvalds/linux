//===-- DWARFAttribute.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFATTRIBUTE_H
#define LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFATTRIBUTE_H

#include "DWARFDefines.h"
#include "DWARFFormValue.h"
#include "llvm/ADT/SmallVector.h"
#include <vector>

namespace lldb_private::plugin {
namespace dwarf {
class DWARFUnit;

class DWARFAttribute {
public:
  DWARFAttribute(dw_attr_t attr, dw_form_t form,
                 DWARFFormValue::ValueType value)
      : m_attr(attr), m_form(form), m_value(value) {}

  dw_attr_t get_attr() const { return m_attr; }
  dw_form_t get_form() const { return m_form; }
  DWARFFormValue::ValueType get_value() const { return m_value; }
  void get(dw_attr_t &attr, dw_form_t &form,
           DWARFFormValue::ValueType &val) const {
    attr = m_attr;
    form = m_form;
    val = m_value;
  }

protected:
  dw_attr_t m_attr;
  dw_form_t m_form;
  DWARFFormValue::ValueType m_value;
};

class DWARFAttributes {
public:
  DWARFAttributes();
  ~DWARFAttributes();

  void Append(const DWARFFormValue &form_value, dw_offset_t attr_die_offset,
              dw_attr_t attr);
  DWARFUnit *CompileUnitAtIndex(uint32_t i) const { return m_infos[i].cu; }
  dw_offset_t DIEOffsetAtIndex(uint32_t i) const {
    return m_infos[i].die_offset;
  }
  dw_attr_t AttributeAtIndex(uint32_t i) const {
    return m_infos[i].attr.get_attr();
  }
  dw_form_t FormAtIndex(uint32_t i) const { return m_infos[i].attr.get_form(); }
  DWARFFormValue::ValueType ValueAtIndex(uint32_t i) const {
    return m_infos[i].attr.get_value();
  }
  bool ExtractFormValueAtIndex(uint32_t i, DWARFFormValue &form_value) const;
  DWARFDIE FormValueAsReferenceAtIndex(uint32_t i) const;
  DWARFDIE FormValueAsReference(dw_attr_t attr) const;
  uint32_t FindAttributeIndex(dw_attr_t attr) const;
  void Clear() { m_infos.clear(); }
  size_t Size() const { return m_infos.size(); }

protected:
  struct AttributeValue {
    DWARFUnit *cu; // Keep the compile unit with each attribute in
                   // case we have DW_FORM_ref_addr values
    dw_offset_t die_offset;
    DWARFAttribute attr;
  };
  typedef llvm::SmallVector<AttributeValue, 8> collection;
  collection m_infos;
};
} // namespace dwarf
} // namespace lldb_private::plugin

#endif // LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFATTRIBUTE_H
