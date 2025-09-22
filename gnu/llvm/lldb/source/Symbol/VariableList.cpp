//===-- VariableList.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/VariableList.h"

#include "lldb/Symbol/Block.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Utility/RegularExpression.h"

using namespace lldb;
using namespace lldb_private;

// VariableList constructor
VariableList::VariableList() : m_variables() {}

// Destructor
VariableList::~VariableList() = default;

void VariableList::AddVariable(const VariableSP &var_sp) {
  m_variables.push_back(var_sp);
}

bool VariableList::AddVariableIfUnique(const lldb::VariableSP &var_sp) {
  if (FindVariableIndex(var_sp) == UINT32_MAX) {
    m_variables.push_back(var_sp);
    return true;
  }
  return false;
}

void VariableList::AddVariables(VariableList *variable_list) {
  if (variable_list) {
    std::copy(variable_list->m_variables.begin(), // source begin
              variable_list->m_variables.end(),   // source end
              back_inserter(m_variables));        // destination
  }
}

void VariableList::Clear() { m_variables.clear(); }

VariableSP VariableList::GetVariableAtIndex(size_t idx) const {
  VariableSP var_sp;
  if (idx < m_variables.size())
    var_sp = m_variables[idx];
  return var_sp;
}

VariableSP VariableList::RemoveVariableAtIndex(size_t idx) {
  VariableSP var_sp;
  if (idx < m_variables.size()) {
    var_sp = m_variables[idx];
    m_variables.erase(m_variables.begin() + idx);
  }
  return var_sp;
}

uint32_t VariableList::FindVariableIndex(const VariableSP &var_sp) {
  iterator pos, end = m_variables.end();
  for (pos = m_variables.begin(); pos != end; ++pos) {
    if (pos->get() == var_sp.get())
      return std::distance(m_variables.begin(), pos);
  }
  return UINT32_MAX;
}

VariableSP VariableList::FindVariable(ConstString name,
                                      bool include_static_members) {
  VariableSP var_sp;
  iterator pos, end = m_variables.end();
  for (pos = m_variables.begin(); pos != end; ++pos) {
    if ((*pos)->NameMatches(name)) {
      if (include_static_members || !(*pos)->IsStaticMember()) {
        var_sp = (*pos);
        break;
      }
    }
  }
  return var_sp;
}

VariableSP VariableList::FindVariable(ConstString name,
                                      lldb::ValueType value_type,
                                      bool include_static_members) {
  VariableSP var_sp;
  iterator pos, end = m_variables.end();
  for (pos = m_variables.begin(); pos != end; ++pos) {
    if ((*pos)->NameMatches(name) && (*pos)->GetScope() == value_type) {
      if (include_static_members || !(*pos)->IsStaticMember()) {
        var_sp = (*pos);
        break;
      }
    }
  }
  return var_sp;
}

size_t VariableList::AppendVariablesIfUnique(VariableList &var_list) {
  const size_t initial_size = var_list.GetSize();
  iterator pos, end = m_variables.end();
  for (pos = m_variables.begin(); pos != end; ++pos)
    var_list.AddVariableIfUnique(*pos);
  return var_list.GetSize() - initial_size;
}

size_t VariableList::AppendVariablesIfUnique(const RegularExpression &regex,
                                             VariableList &var_list,
                                             size_t &total_matches) {
  const size_t initial_size = var_list.GetSize();
  iterator pos, end = m_variables.end();
  for (pos = m_variables.begin(); pos != end; ++pos) {
    if ((*pos)->NameMatches(regex)) {
      // Note the total matches found
      total_matches++;
      // Only add this variable if it isn't already in the "var_list"
      var_list.AddVariableIfUnique(*pos);
    }
  }
  // Return the number of new unique variables added to "var_list"
  return var_list.GetSize() - initial_size;
}

size_t VariableList::AppendVariablesWithScope(lldb::ValueType type,
                                              VariableList &var_list,
                                              bool if_unique) {
  const size_t initial_size = var_list.GetSize();
  iterator pos, end = m_variables.end();
  for (pos = m_variables.begin(); pos != end; ++pos) {
    if ((*pos)->GetScope() == type) {
      if (if_unique)
        var_list.AddVariableIfUnique(*pos);
      else
        var_list.AddVariable(*pos);
    }
  }
  // Return the number of new unique variables added to "var_list"
  return var_list.GetSize() - initial_size;
}

uint32_t VariableList::FindIndexForVariable(Variable *variable) {
  VariableSP var_sp;
  iterator pos;
  const iterator begin = m_variables.begin();
  const iterator end = m_variables.end();
  for (pos = m_variables.begin(); pos != end; ++pos) {
    if ((*pos).get() == variable)
      return std::distance(begin, pos);
  }
  return UINT32_MAX;
}

size_t VariableList::MemorySize() const {
  size_t mem_size = sizeof(VariableList);
  const_iterator pos, end = m_variables.end();
  for (pos = m_variables.begin(); pos != end; ++pos)
    mem_size += (*pos)->MemorySize();
  return mem_size;
}

size_t VariableList::GetSize() const { return m_variables.size(); }

void VariableList::Dump(Stream *s, bool show_context) const {
  //  s.Printf("%.*p: ", (int)sizeof(void*) * 2, this);
  //  s.Indent();
  //  s << "VariableList\n";

  const_iterator pos, end = m_variables.end();
  for (pos = m_variables.begin(); pos != end; ++pos) {
    (*pos)->Dump(s, show_context);
  }
}
