//===-- VectorIterator.h ----------------------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_DATAFORMATTERS_VECTORITERATOR_H
#define LLDB_DATAFORMATTERS_VECTORITERATOR_H

#include "lldb/lldb-forward.h"

#include "lldb/DataFormatters/TypeSynthetic.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Utility/ConstString.h"
#include "llvm/ADT/SmallVector.h"

namespace lldb_private {
namespace formatters {
class VectorIteratorSyntheticFrontEnd : public SyntheticChildrenFrontEnd {
public:
  VectorIteratorSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp,
                                  llvm::ArrayRef<ConstString> item_names);

  llvm::Expected<uint32_t> CalculateNumChildren() override;

  lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) override;

  lldb::ChildCacheState Update() override;

  bool MightHaveChildren() override;

  size_t GetIndexOfChildWithName(ConstString name) override;

private:
  ExecutionContextRef m_exe_ctx_ref;
  llvm::SmallVector<ConstString, 2> m_item_names;
  lldb::ValueObjectSP m_item_sp;
};

} // namespace formatters
} // namespace lldb_private

#endif // LLDB_DATAFORMATTERS_VECTORITERATOR_H
