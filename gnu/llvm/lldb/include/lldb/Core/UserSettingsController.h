//====-- UserSettingsController.h --------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_USERSETTINGSCONTROLLER_H
#define LLDB_CORE_USERSETTINGSCONTROLLER_H

#include "lldb/Interpreter/OptionValueProperties.h"
#include "lldb/Utility/Status.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-private-enumerations.h"

#include "llvm/ADT/StringRef.h"

#include <vector>

#include <cstddef>
#include <cstdint>

namespace lldb_private {
class CommandInterpreter;
class ExecutionContext;
class Property;
class Stream;
}

namespace lldb_private {

class Properties {
public:
  Properties();

  Properties(const lldb::OptionValuePropertiesSP &collection_sp);

  virtual ~Properties();

  virtual lldb::OptionValuePropertiesSP GetValueProperties() const {
    // This function is virtual in case subclasses want to lazily implement
    // creating the properties.
    return m_collection_sp;
  }

  virtual lldb::OptionValueSP GetPropertyValue(const ExecutionContext *exe_ctx,
                                               llvm::StringRef property_path,
                                               Status &error) const;

  virtual Status SetPropertyValue(const ExecutionContext *exe_ctx,
                                  VarSetOperationType op,
                                  llvm::StringRef property_path,
                                  llvm::StringRef value);

  virtual Status DumpPropertyValue(const ExecutionContext *exe_ctx,
                                   Stream &strm, llvm::StringRef property_path,
                                   uint32_t dump_mask, bool is_json = false);

  virtual void DumpAllPropertyValues(const ExecutionContext *exe_ctx,
                                     Stream &strm, uint32_t dump_mask,
                                     bool is_json = false);

  virtual void DumpAllDescriptions(CommandInterpreter &interpreter,
                                   Stream &strm) const;

  size_t Apropos(llvm::StringRef keyword,
                 std::vector<const Property *> &matching_properties) const;

  // We sometimes need to introduce a setting to enable experimental features,
  // but then we don't want the setting for these to cause errors when the
  // setting goes away.  Add a sub-topic of the settings using this
  // experimental name, and two things will happen.  One is that settings that
  // don't find the name will not be treated as errors.  Also, if you decide to
  // keep the settings just move them into the containing properties, and we
  // will auto-forward the experimental settings to the real one.
  static llvm::StringRef GetExperimentalSettingsName();

  static bool IsSettingExperimental(llvm::StringRef setting);

  template <typename T>
  T GetPropertyAtIndexAs(uint32_t idx, T default_value,
                         const ExecutionContext *exe_ctx = nullptr) const {
    return m_collection_sp->GetPropertyAtIndexAs<T>(idx, exe_ctx)
        .value_or(default_value);
  }

  template <typename T, typename U = typename std::remove_pointer<T>::type,
            std::enable_if_t<std::is_pointer_v<T>, bool> = true>
  const U *
  GetPropertyAtIndexAs(uint32_t idx,
                       const ExecutionContext *exe_ctx = nullptr) const {
    return m_collection_sp->GetPropertyAtIndexAs<T>(idx, exe_ctx);
  }

  template <typename T>
  bool SetPropertyAtIndex(uint32_t idx, T t,
                          const ExecutionContext *exe_ctx = nullptr) const {
    return m_collection_sp->SetPropertyAtIndex<T>(idx, t, exe_ctx);
  }

protected:
  lldb::OptionValuePropertiesSP m_collection_sp;
};

} // namespace lldb_private

#endif // LLDB_CORE_USERSETTINGSCONTROLLER_H
