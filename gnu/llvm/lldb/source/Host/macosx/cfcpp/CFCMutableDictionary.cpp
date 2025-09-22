//===-- CFCMutableDictionary.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CFCMutableDictionary.h"
#include "CFCString.h"
// CFCString constructor
CFCMutableDictionary::CFCMutableDictionary(CFMutableDictionaryRef s)
    : CFCReleaser<CFMutableDictionaryRef>(s) {}

// CFCMutableDictionary copy constructor
CFCMutableDictionary::CFCMutableDictionary(const CFCMutableDictionary &rhs) =
    default;

// CFCMutableDictionary copy constructor
const CFCMutableDictionary &CFCMutableDictionary::
operator=(const CFCMutableDictionary &rhs) {
  if (this != &rhs)
    *this = rhs;
  return *this;
}

// Destructor
CFCMutableDictionary::~CFCMutableDictionary() = default;

CFIndex CFCMutableDictionary::GetCount() const {
  CFMutableDictionaryRef dict = get();
  if (dict)
    return ::CFDictionaryGetCount(dict);
  return 0;
}

CFIndex CFCMutableDictionary::GetCountOfKey(const void *key) const

{
  CFMutableDictionaryRef dict = get();
  if (dict)
    return ::CFDictionaryGetCountOfKey(dict, key);
  return 0;
}

CFIndex CFCMutableDictionary::GetCountOfValue(const void *value) const

{
  CFMutableDictionaryRef dict = get();
  if (dict)
    return ::CFDictionaryGetCountOfValue(dict, value);
  return 0;
}

void CFCMutableDictionary::GetKeysAndValues(const void **keys,
                                            const void **values) const {
  CFMutableDictionaryRef dict = get();
  if (dict)
    ::CFDictionaryGetKeysAndValues(dict, keys, values);
}

const void *CFCMutableDictionary::GetValue(const void *key) const

{
  CFMutableDictionaryRef dict = get();
  if (dict)
    return ::CFDictionaryGetValue(dict, key);
  return NULL;
}

Boolean
CFCMutableDictionary::GetValueIfPresent(const void *key,
                                        const void **value_handle) const {
  CFMutableDictionaryRef dict = get();
  if (dict)
    return ::CFDictionaryGetValueIfPresent(dict, key, value_handle);
  return false;
}

CFMutableDictionaryRef CFCMutableDictionary::Dictionary(bool can_create) {
  CFMutableDictionaryRef dict = get();
  if (can_create && dict == NULL) {
    dict = ::CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
                                       &kCFTypeDictionaryKeyCallBacks,
                                       &kCFTypeDictionaryValueCallBacks);
    reset(dict);
  }
  return dict;
}

bool CFCMutableDictionary::AddValue(CFStringRef key, const void *value,
                                    bool can_create) {
  CFMutableDictionaryRef dict = Dictionary(can_create);
  if (dict != NULL) {
    // Let the dictionary own the CFNumber
    ::CFDictionaryAddValue(dict, key, value);
    return true;
  }
  return false;
}

bool CFCMutableDictionary::SetValue(CFStringRef key, const void *value,
                                    bool can_create) {
  CFMutableDictionaryRef dict = Dictionary(can_create);
  if (dict != NULL) {
    // Let the dictionary own the CFNumber
    ::CFDictionarySetValue(dict, key, value);
    return true;
  }
  return false;
}

bool CFCMutableDictionary::AddValueSInt8(CFStringRef key, int8_t value,
                                         bool can_create) {
  CFMutableDictionaryRef dict = Dictionary(can_create);
  if (dict != NULL) {
    CFCReleaser<CFNumberRef> cf_number(
        ::CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt8Type, &value));
    if (cf_number.get()) {
      // Let the dictionary own the CFNumber
      ::CFDictionaryAddValue(dict, key, cf_number.get());
      return true;
    }
  }
  return false;
}

bool CFCMutableDictionary::SetValueSInt8(CFStringRef key, int8_t value,
                                         bool can_create) {
  CFMutableDictionaryRef dict = Dictionary(can_create);
  if (dict != NULL) {
    CFCReleaser<CFNumberRef> cf_number(
        ::CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt8Type, &value));
    if (cf_number.get()) {
      // Let the dictionary own the CFNumber
      ::CFDictionarySetValue(dict, key, cf_number.get());
      return true;
    }
  }
  return false;
}

bool CFCMutableDictionary::AddValueSInt16(CFStringRef key, int16_t value,
                                          bool can_create) {
  CFMutableDictionaryRef dict = Dictionary(can_create);
  if (dict != NULL) {
    CFCReleaser<CFNumberRef> cf_number(
        ::CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt16Type, &value));
    if (cf_number.get()) {
      // Let the dictionary own the CFNumber
      ::CFDictionaryAddValue(dict, key, cf_number.get());
      return true;
    }
  }
  return false;
}

bool CFCMutableDictionary::SetValueSInt16(CFStringRef key, int16_t value,
                                          bool can_create) {
  CFMutableDictionaryRef dict = Dictionary(can_create);
  if (dict != NULL) {
    CFCReleaser<CFNumberRef> cf_number(
        ::CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt16Type, &value));
    if (cf_number.get()) {
      // Let the dictionary own the CFNumber
      ::CFDictionarySetValue(dict, key, cf_number.get());
      return true;
    }
  }
  return false;
}

bool CFCMutableDictionary::AddValueSInt32(CFStringRef key, int32_t value,
                                          bool can_create) {
  CFMutableDictionaryRef dict = Dictionary(can_create);
  if (dict != NULL) {
    CFCReleaser<CFNumberRef> cf_number(
        ::CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &value));
    if (cf_number.get()) {
      // Let the dictionary own the CFNumber
      ::CFDictionaryAddValue(dict, key, cf_number.get());
      return true;
    }
  }
  return false;
}

bool CFCMutableDictionary::SetValueSInt32(CFStringRef key, int32_t value,
                                          bool can_create) {
  CFMutableDictionaryRef dict = Dictionary(can_create);
  if (dict != NULL) {
    CFCReleaser<CFNumberRef> cf_number(
        ::CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &value));
    if (cf_number.get()) {
      // Let the dictionary own the CFNumber
      ::CFDictionarySetValue(dict, key, cf_number.get());
      return true;
    }
  }
  return false;
}

bool CFCMutableDictionary::AddValueSInt64(CFStringRef key, int64_t value,
                                          bool can_create) {
  CFMutableDictionaryRef dict = Dictionary(can_create);
  if (dict != NULL) {
    CFCReleaser<CFNumberRef> cf_number(
        ::CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt64Type, &value));
    if (cf_number.get()) {
      // Let the dictionary own the CFNumber
      ::CFDictionaryAddValue(dict, key, cf_number.get());
      return true;
    }
  }
  return false;
}

bool CFCMutableDictionary::SetValueSInt64(CFStringRef key, int64_t value,
                                          bool can_create) {
  CFMutableDictionaryRef dict = Dictionary(can_create);
  if (dict != NULL) {
    CFCReleaser<CFNumberRef> cf_number(
        ::CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt64Type, &value));
    if (cf_number.get()) {
      // Let the dictionary own the CFNumber
      ::CFDictionarySetValue(dict, key, cf_number.get());
      return true;
    }
  }
  return false;
}

bool CFCMutableDictionary::AddValueUInt8(CFStringRef key, uint8_t value,
                                         bool can_create) {
  CFMutableDictionaryRef dict = Dictionary(can_create);
  if (dict != NULL) {
    // Have to promote to the next size type so things don't appear negative of
    // the MSBit is set...
    int16_t sval = value;
    CFCReleaser<CFNumberRef> cf_number(
        ::CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt16Type, &sval));
    if (cf_number.get()) {
      // Let the dictionary own the CFNumber
      ::CFDictionaryAddValue(dict, key, cf_number.get());
      return true;
    }
  }
  return false;
}

bool CFCMutableDictionary::SetValueUInt8(CFStringRef key, uint8_t value,
                                         bool can_create) {
  CFMutableDictionaryRef dict = Dictionary(can_create);
  if (dict != NULL) {
    // Have to promote to the next size type so things don't appear negative of
    // the MSBit is set...
    int16_t sval = value;
    CFCReleaser<CFNumberRef> cf_number(
        ::CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt16Type, &sval));
    if (cf_number.get()) {
      // Let the dictionary own the CFNumber
      ::CFDictionarySetValue(dict, key, cf_number.get());
      return true;
    }
  }
  return false;
}

bool CFCMutableDictionary::AddValueUInt16(CFStringRef key, uint16_t value,
                                          bool can_create) {
  CFMutableDictionaryRef dict = Dictionary(can_create);
  if (dict != NULL) {
    // Have to promote to the next size type so things don't appear negative of
    // the MSBit is set...
    int32_t sval = value;
    CFCReleaser<CFNumberRef> cf_number(
        ::CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &sval));
    if (cf_number.get()) {
      // Let the dictionary own the CFNumber
      ::CFDictionaryAddValue(dict, key, cf_number.get());
      return true;
    }
  }
  return false;
}

bool CFCMutableDictionary::SetValueUInt16(CFStringRef key, uint16_t value,
                                          bool can_create) {
  CFMutableDictionaryRef dict = Dictionary(can_create);
  if (dict != NULL) {
    // Have to promote to the next size type so things don't appear negative of
    // the MSBit is set...
    int32_t sval = value;
    CFCReleaser<CFNumberRef> cf_number(
        ::CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &sval));
    if (cf_number.get()) {
      // Let the dictionary own the CFNumber
      ::CFDictionarySetValue(dict, key, cf_number.get());
      return true;
    }
  }
  return false;
}

bool CFCMutableDictionary::AddValueUInt32(CFStringRef key, uint32_t value,
                                          bool can_create) {
  CFMutableDictionaryRef dict = Dictionary(can_create);
  if (dict != NULL) {
    // Have to promote to the next size type so things don't appear negative of
    // the MSBit is set...
    int64_t sval = value;
    CFCReleaser<CFNumberRef> cf_number(
        ::CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt64Type, &sval));
    if (cf_number.get()) {
      // Let the dictionary own the CFNumber
      ::CFDictionaryAddValue(dict, key, cf_number.get());
      return true;
    }
  }
  return false;
}

bool CFCMutableDictionary::SetValueUInt32(CFStringRef key, uint32_t value,
                                          bool can_create) {
  CFMutableDictionaryRef dict = Dictionary(can_create);
  if (dict != NULL) {
    // Have to promote to the next size type so things don't appear negative of
    // the MSBit is set...
    int64_t sval = value;
    CFCReleaser<CFNumberRef> cf_number(
        ::CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt64Type, &sval));
    if (cf_number.get()) {
      // Let the dictionary own the CFNumber
      ::CFDictionarySetValue(dict, key, cf_number.get());
      return true;
    }
  }
  return false;
}

bool CFCMutableDictionary::AddValueUInt64(CFStringRef key, uint64_t value,
                                          bool can_create) {
  CFMutableDictionaryRef dict = Dictionary(can_create);
  if (dict != NULL) {
    // The number may appear negative if the MSBit is set in "value". Due to a
    // limitation of CFNumber, there isn't a way to have it show up otherwise
    // as of this writing.
    CFCReleaser<CFNumberRef> cf_number(
        ::CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt64Type, &value));
    if (cf_number.get()) {
      // Let the dictionary own the CFNumber
      ::CFDictionaryAddValue(dict, key, cf_number.get());
      return true;
    }
  }
  return false;
}

bool CFCMutableDictionary::SetValueUInt64(CFStringRef key, uint64_t value,
                                          bool can_create) {
  CFMutableDictionaryRef dict = Dictionary(can_create);
  if (dict != NULL) {
    // The number may appear negative if the MSBit is set in "value". Due to a
    // limitation of CFNumber, there isn't a way to have it show up otherwise
    // as of this writing.
    CFCReleaser<CFNumberRef> cf_number(
        ::CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt64Type, &value));
    if (cf_number.get()) {
      // Let the dictionary own the CFNumber
      ::CFDictionarySetValue(dict, key, cf_number.get());
      return true;
    }
  }
  return false;
}

bool CFCMutableDictionary::AddValueDouble(CFStringRef key, double value,
                                          bool can_create) {
  CFMutableDictionaryRef dict = Dictionary(can_create);
  if (dict != NULL) {
    // The number may appear negative if the MSBit is set in "value". Due to a
    // limitation of CFNumber, there isn't a way to have it show up otherwise
    // as of this writing.
    CFCReleaser<CFNumberRef> cf_number(
        ::CFNumberCreate(kCFAllocatorDefault, kCFNumberDoubleType, &value));
    if (cf_number.get()) {
      // Let the dictionary own the CFNumber
      ::CFDictionaryAddValue(dict, key, cf_number.get());
      return true;
    }
  }
  return false;
}

bool CFCMutableDictionary::SetValueDouble(CFStringRef key, double value,
                                          bool can_create) {
  CFMutableDictionaryRef dict = Dictionary(can_create);
  if (dict != NULL) {
    // The number may appear negative if the MSBit is set in "value". Due to a
    // limitation of CFNumber, there isn't a way to have it show up otherwise
    // as of this writing.
    CFCReleaser<CFNumberRef> cf_number(
        ::CFNumberCreate(kCFAllocatorDefault, kCFNumberDoubleType, &value));
    if (cf_number.get()) {
      // Let the dictionary own the CFNumber
      ::CFDictionarySetValue(dict, key, cf_number.get());
      return true;
    }
  }
  return false;
}

bool CFCMutableDictionary::AddValueCString(CFStringRef key, const char *cstr,
                                           bool can_create) {
  CFMutableDictionaryRef dict = Dictionary(can_create);
  if (dict != NULL) {
    CFCString cf_str(cstr, kCFStringEncodingUTF8);
    if (cf_str.get()) {
      // Let the dictionary own the CFNumber
      ::CFDictionaryAddValue(dict, key, cf_str.get());
      return true;
    }
  }
  return false;
}

bool CFCMutableDictionary::SetValueCString(CFStringRef key, const char *cstr,
                                           bool can_create) {
  CFMutableDictionaryRef dict = Dictionary(can_create);
  if (dict != NULL) {
    CFCString cf_str(cstr, kCFStringEncodingUTF8);
    if (cf_str.get()) {
      // Let the dictionary own the CFNumber
      ::CFDictionarySetValue(dict, key, cf_str.get());
      return true;
    }
  }
  return false;
}

void CFCMutableDictionary::RemoveAllValues() {
  CFMutableDictionaryRef dict = get();
  if (dict)
    ::CFDictionaryRemoveAllValues(dict);
}

void CFCMutableDictionary::RemoveValue(const void *value) {
  CFMutableDictionaryRef dict = get();
  if (dict)
    ::CFDictionaryRemoveValue(dict, value);
}
void CFCMutableDictionary::ReplaceValue(const void *key, const void *value) {
  CFMutableDictionaryRef dict = get();
  if (dict)
    ::CFDictionaryReplaceValue(dict, key, value);
}
