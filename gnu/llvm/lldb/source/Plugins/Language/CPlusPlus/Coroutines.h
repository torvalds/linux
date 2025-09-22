//===-- Coroutines.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_LANGUAGE_CPLUSPLUS_COROUTINES_H
#define LLDB_SOURCE_PLUGINS_LANGUAGE_CPLUSPLUS_COROUTINES_H

#include "lldb/Core/ValueObject.h"
#include "lldb/DataFormatters/TypeSummary.h"
#include "lldb/DataFormatters/TypeSynthetic.h"
#include "lldb/Utility/Stream.h"

namespace lldb_private {

namespace formatters {

/// Summary provider for `std::coroutine_handle<T>` from  libc++, libstdc++ and
/// MSVC STL.
bool StdlibCoroutineHandleSummaryProvider(ValueObject &valobj, Stream &stream,
                                          const TypeSummaryOptions &options);

/// Synthetic children frontend for `std::coroutine_handle<promise_type>` from
/// libc++, libstdc++ and MSVC STL. Shows the compiler-generated `resume` and
/// `destroy` function pointers as well as the `promise`, if the promise type
/// is `promise_type != void`.
class StdlibCoroutineHandleSyntheticFrontEnd
    : public SyntheticChildrenFrontEnd {
public:
  StdlibCoroutineHandleSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp);

  ~StdlibCoroutineHandleSyntheticFrontEnd() override;

  llvm::Expected<uint32_t> CalculateNumChildren() override;

  lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) override;

  lldb::ChildCacheState Update() override;

  bool MightHaveChildren() override;

  size_t GetIndexOfChildWithName(ConstString name) override;

private:
  lldb::ValueObjectSP m_resume_ptr_sp;
  lldb::ValueObjectSP m_destroy_ptr_sp;
  lldb::ValueObjectSP m_promise_ptr_sp;
};

SyntheticChildrenFrontEnd *
StdlibCoroutineHandleSyntheticFrontEndCreator(CXXSyntheticChildren *,
                                              lldb::ValueObjectSP);

} // namespace formatters
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_LANGUAGE_CPLUSPLUS_COROUTINES_H
