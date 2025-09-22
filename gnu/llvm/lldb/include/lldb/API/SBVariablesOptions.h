//===-- SBVariablesOptions.h ------------------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBVARIABLESOPTIONS_H
#define LLDB_API_SBVARIABLESOPTIONS_H

#include "lldb/API/SBDefines.h"

class VariablesOptionsImpl;

namespace lldb {

class LLDB_API SBVariablesOptions {
public:
  SBVariablesOptions();

  SBVariablesOptions(const SBVariablesOptions &options);

  SBVariablesOptions &operator=(const SBVariablesOptions &options);

  ~SBVariablesOptions();

  explicit operator bool() const;

  bool IsValid() const;

  bool GetIncludeArguments() const;

  void SetIncludeArguments(bool);

  bool GetIncludeRecognizedArguments(const lldb::SBTarget &) const;

  void SetIncludeRecognizedArguments(bool);

  bool GetIncludeLocals() const;

  void SetIncludeLocals(bool);

  bool GetIncludeStatics() const;

  void SetIncludeStatics(bool);

  bool GetInScopeOnly() const;

  void SetInScopeOnly(bool);

  bool GetIncludeRuntimeSupportValues() const;

  void SetIncludeRuntimeSupportValues(bool);

  lldb::DynamicValueType GetUseDynamic() const;

  void SetUseDynamic(lldb::DynamicValueType);

protected:
  VariablesOptionsImpl *operator->();

  const VariablesOptionsImpl *operator->() const;

  VariablesOptionsImpl *get();

  VariablesOptionsImpl &ref();

  const VariablesOptionsImpl &ref() const;

  SBVariablesOptions(VariablesOptionsImpl *lldb_object_ptr);

  void SetOptions(VariablesOptionsImpl *lldb_object_ptr);

private:
  std::unique_ptr<VariablesOptionsImpl> m_opaque_up;
};

} // namespace lldb

#endif // LLDB_API_SBVARIABLESOPTIONS_H
