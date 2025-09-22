//===-- LibCxxQueue.cpp ---------------------------------------------------===//
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

class QueueFrontEnd : public SyntheticChildrenFrontEnd {
public:
  QueueFrontEnd(ValueObject &valobj) : SyntheticChildrenFrontEnd(valobj) {
    Update();
  }

  size_t GetIndexOfChildWithName(ConstString name) override {
    return m_container_sp ? m_container_sp->GetIndexOfChildWithName(name)
                          : UINT32_MAX;
  }

  bool MightHaveChildren() override { return true; }
  lldb::ChildCacheState Update() override;

  llvm::Expected<uint32_t> CalculateNumChildren() override {
    return m_container_sp ? m_container_sp->GetNumChildren() : 0;
  }

  ValueObjectSP GetChildAtIndex(uint32_t idx) override {
    return m_container_sp ? m_container_sp->GetChildAtIndex(idx)
                          : nullptr;
  }

private:
  // The lifetime of a ValueObject and all its derivative ValueObjects
  // (children, clones, etc.) is managed by a ClusterManager. These
  // objects are only destroyed when every shared pointer to any of them
  // is destroyed, so we must not store a shared pointer to any ValueObject
  // derived from our backend ValueObject (since we're in the same cluster).
  ValueObject* m_container_sp = nullptr;
};
} // namespace

lldb::ChildCacheState QueueFrontEnd::Update() {
  m_container_sp = nullptr;
  ValueObjectSP c_sp = m_backend.GetChildMemberWithName("c");
  if (!c_sp)
    return lldb::ChildCacheState::eRefetch;
  m_container_sp = c_sp->GetSyntheticValue().get();
  return lldb::ChildCacheState::eRefetch;
}

SyntheticChildrenFrontEnd *
formatters::LibcxxQueueFrontEndCreator(CXXSyntheticChildren *,
                                       lldb::ValueObjectSP valobj_sp) {
  if (valobj_sp)
    return new QueueFrontEnd(*valobj_sp);
  return nullptr;
}
