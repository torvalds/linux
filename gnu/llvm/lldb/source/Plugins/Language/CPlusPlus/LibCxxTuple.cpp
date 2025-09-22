//===-- LibCxxTuple.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LibCxx.h"
#include "lldb/DataFormatters/FormattersHelpers.h"

using namespace lldb;
using namespace lldb_private;

namespace {

class TupleFrontEnd: public SyntheticChildrenFrontEnd {
public:
  TupleFrontEnd(ValueObject &valobj) : SyntheticChildrenFrontEnd(valobj) {
    Update();
  }

  size_t GetIndexOfChildWithName(ConstString name) override {
    return formatters::ExtractIndexFromString(name.GetCString());
  }

  bool MightHaveChildren() override { return true; }
  lldb::ChildCacheState Update() override;
  llvm::Expected<uint32_t> CalculateNumChildren() override {
    return m_elements.size();
  }
  ValueObjectSP GetChildAtIndex(uint32_t idx) override;

private:
  // The lifetime of a ValueObject and all its derivative ValueObjects
  // (children, clones, etc.) is managed by a ClusterManager. These
  // objects are only destroyed when every shared pointer to any of them
  // is destroyed, so we must not store a shared pointer to any ValueObject
  // derived from our backend ValueObject (since we're in the same cluster).
  std::vector<ValueObject*> m_elements;
  ValueObject* m_base = nullptr;
};
}

lldb::ChildCacheState TupleFrontEnd::Update() {
  m_elements.clear();
  m_base = nullptr;

  ValueObjectSP base_sp;
  base_sp = m_backend.GetChildMemberWithName("__base_");
  if (!base_sp) {
    // Pre r304382 name of the base element.
    base_sp = m_backend.GetChildMemberWithName("base_");
  }
  if (!base_sp)
    return lldb::ChildCacheState::eRefetch;
  m_base = base_sp.get();
  m_elements.assign(base_sp->GetCompilerType().GetNumDirectBaseClasses(),
                    nullptr);
  return lldb::ChildCacheState::eRefetch;
}

ValueObjectSP TupleFrontEnd::GetChildAtIndex(uint32_t idx) {
  if (idx >= m_elements.size())
    return ValueObjectSP();
  if (!m_base)
    return ValueObjectSP();
  if (m_elements[idx])
    return m_elements[idx]->GetSP();

  CompilerType holder_type =
      m_base->GetCompilerType().GetDirectBaseClassAtIndex(idx, nullptr);
  if (!holder_type)
    return ValueObjectSP();
  ValueObjectSP holder_sp = m_base->GetChildAtIndex(idx);
  if (!holder_sp)
    return ValueObjectSP();

  ValueObjectSP elem_sp = holder_sp->GetChildAtIndex(0);
  if (elem_sp)
    m_elements[idx] =
        elem_sp->Clone(ConstString(llvm::formatv("[{0}]", idx).str())).get();

  if (m_elements[idx])
    return m_elements[idx]->GetSP();
  return ValueObjectSP();
}

SyntheticChildrenFrontEnd *
formatters::LibcxxTupleFrontEndCreator(CXXSyntheticChildren *,
                                       lldb::ValueObjectSP valobj_sp) {
  if (valobj_sp)
    return new TupleFrontEnd(*valobj_sp);
  return nullptr;
}
