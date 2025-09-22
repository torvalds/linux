//===-- CFCMutableDictionary.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_HOST_MACOSX_CFCPP_CFCMUTABLEDICTIONARY_H
#define LLDB_SOURCE_HOST_MACOSX_CFCPP_CFCMUTABLEDICTIONARY_H

#include "CFCReleaser.h"

class CFCMutableDictionary : public CFCReleaser<CFMutableDictionaryRef> {
public:
  // Constructors and Destructors
  CFCMutableDictionary(CFMutableDictionaryRef s = NULL);
  CFCMutableDictionary(const CFCMutableDictionary &rhs);
  ~CFCMutableDictionary() override;

  // Operators
  const CFCMutableDictionary &operator=(const CFCMutableDictionary &rhs);

  CFIndex GetCount() const;
  CFIndex GetCountOfKey(const void *value) const;
  CFIndex GetCountOfValue(const void *value) const;
  void GetKeysAndValues(const void **keys, const void **values) const;
  const void *GetValue(const void *key) const;
  Boolean GetValueIfPresent(const void *key, const void **value_handle) const;
  bool AddValue(CFStringRef key, const void *value, bool can_create = false);
  bool SetValue(CFStringRef key, const void *value, bool can_create = false);
  bool AddValueSInt8(CFStringRef key, int8_t value, bool can_create = false);
  bool SetValueSInt8(CFStringRef key, int8_t value, bool can_create = false);
  bool AddValueSInt16(CFStringRef key, int16_t value, bool can_create = false);
  bool SetValueSInt16(CFStringRef key, int16_t value, bool can_create = false);
  bool AddValueSInt32(CFStringRef key, int32_t value, bool can_create = false);
  bool SetValueSInt32(CFStringRef key, int32_t value, bool can_create = false);
  bool AddValueSInt64(CFStringRef key, int64_t value, bool can_create = false);
  bool SetValueSInt64(CFStringRef key, int64_t value, bool can_create = false);
  bool AddValueUInt8(CFStringRef key, uint8_t value, bool can_create = false);
  bool SetValueUInt8(CFStringRef key, uint8_t value, bool can_create = false);
  bool AddValueUInt16(CFStringRef key, uint16_t value, bool can_create = false);
  bool SetValueUInt16(CFStringRef key, uint16_t value, bool can_create = false);
  bool AddValueUInt32(CFStringRef key, uint32_t value, bool can_create = false);
  bool SetValueUInt32(CFStringRef key, uint32_t value, bool can_create = false);
  bool AddValueUInt64(CFStringRef key, uint64_t value, bool can_create = false);
  bool SetValueUInt64(CFStringRef key, uint64_t value, bool can_create = false);
  bool AddValueDouble(CFStringRef key, double value, bool can_create = false);
  bool SetValueDouble(CFStringRef key, double value, bool can_create = false);
  bool AddValueCString(CFStringRef key, const char *cstr,
                       bool can_create = false);
  bool SetValueCString(CFStringRef key, const char *cstr,
                       bool can_create = false);
  void RemoveValue(const void *value);
  void ReplaceValue(const void *key, const void *value);
  void RemoveAllValues();
  CFMutableDictionaryRef Dictionary(bool can_create);

protected:
  // Classes that inherit from CFCMutableDictionary can see and modify these

private:
  // For CFCMutableDictionary only
};

#endif // LLDB_SOURCE_HOST_MACOSX_CFCPP_CFCMUTABLEDICTIONARY_H
