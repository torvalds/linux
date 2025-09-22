//===-- sanitizer_flag_parser.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer/AddressSanitizer runtime.
//
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_FLAG_REGISTRY_H
#define SANITIZER_FLAG_REGISTRY_H

#include "sanitizer_common.h"
#include "sanitizer_internal_defs.h"
#include "sanitizer_libc.h"

namespace __sanitizer {

class FlagHandlerBase {
 public:
  virtual bool Parse(const char *value) { return false; }
  // Write the C string representation of the current value (truncated to fit)
  // into the buffer of size `size`. Returns false if truncation occurred and
  // returns true otherwise.
  virtual bool Format(char *buffer, uptr size) {
    if (size > 0)
      buffer[0] = '\0';
    return false;
  }

 protected:
  ~FlagHandlerBase() {}

  inline bool FormatString(char *buffer, uptr size, const char *str_to_use) {
    uptr num_symbols_should_write =
        internal_snprintf(buffer, size, "%s", str_to_use);
    return num_symbols_should_write < size;
  }
};

template <typename T>
class FlagHandler final : public FlagHandlerBase {
  T *t_;

 public:
  explicit FlagHandler(T *t) : t_(t) {}
  bool Parse(const char *value) final;
  bool Format(char *buffer, uptr size) final;
};

inline bool ParseBool(const char *value, bool *b) {
  if (internal_strcmp(value, "0") == 0 ||
      internal_strcmp(value, "no") == 0 ||
      internal_strcmp(value, "false") == 0) {
    *b = false;
    return true;
  }
  if (internal_strcmp(value, "1") == 0 ||
      internal_strcmp(value, "yes") == 0 ||
      internal_strcmp(value, "true") == 0) {
    *b = true;
    return true;
  }
  return false;
}

template <>
inline bool FlagHandler<bool>::Parse(const char *value) {
  if (ParseBool(value, t_)) return true;
  Printf("ERROR: Invalid value for bool option: '%s'\n", value);
  return false;
}

template <>
inline bool FlagHandler<bool>::Format(char *buffer, uptr size) {
  return FormatString(buffer, size, *t_ ? "true" : "false");
}

template <>
inline bool FlagHandler<HandleSignalMode>::Parse(const char *value) {
  bool b;
  if (ParseBool(value, &b)) {
    *t_ = b ? kHandleSignalYes : kHandleSignalNo;
    return true;
  }
  if (internal_strcmp(value, "2") == 0 ||
      internal_strcmp(value, "exclusive") == 0) {
    *t_ = kHandleSignalExclusive;
    return true;
  }
  Printf("ERROR: Invalid value for signal handler option: '%s'\n", value);
  return false;
}

template <>
inline bool FlagHandler<HandleSignalMode>::Format(char *buffer, uptr size) {
  uptr num_symbols_should_write = internal_snprintf(buffer, size, "%d", *t_);
  return num_symbols_should_write < size;
}

template <>
inline bool FlagHandler<const char *>::Parse(const char *value) {
  *t_ = value;
  return true;
}

template <>
inline bool FlagHandler<const char *>::Format(char *buffer, uptr size) {
  return FormatString(buffer, size, *t_);
}

template <>
inline bool FlagHandler<int>::Parse(const char *value) {
  const char *value_end;
  *t_ = internal_simple_strtoll(value, &value_end, 10);
  bool ok = *value_end == 0;
  if (!ok) Printf("ERROR: Invalid value for int option: '%s'\n", value);
  return ok;
}

template <>
inline bool FlagHandler<int>::Format(char *buffer, uptr size) {
  uptr num_symbols_should_write = internal_snprintf(buffer, size, "%d", *t_);
  return num_symbols_should_write < size;
}

template <>
inline bool FlagHandler<uptr>::Parse(const char *value) {
  const char *value_end;
  *t_ = internal_simple_strtoll(value, &value_end, 10);
  bool ok = *value_end == 0;
  if (!ok) Printf("ERROR: Invalid value for uptr option: '%s'\n", value);
  return ok;
}

template <>
inline bool FlagHandler<uptr>::Format(char *buffer, uptr size) {
  uptr num_symbols_should_write = internal_snprintf(buffer, size, "0x%zx", *t_);
  return num_symbols_should_write < size;
}

template <>
inline bool FlagHandler<s64>::Parse(const char *value) {
  const char *value_end;
  *t_ = internal_simple_strtoll(value, &value_end, 10);
  bool ok = *value_end == 0;
  if (!ok) Printf("ERROR: Invalid value for s64 option: '%s'\n", value);
  return ok;
}

template <>
inline bool FlagHandler<s64>::Format(char *buffer, uptr size) {
  uptr num_symbols_should_write = internal_snprintf(buffer, size, "%lld", *t_);
  return num_symbols_should_write < size;
}

class FlagParser {
  static const int kMaxFlags = 200;
  struct Flag {
    const char *name;
    const char *desc;
    FlagHandlerBase *handler;
  } *flags_;
  int n_flags_;

  const char *buf_;
  uptr pos_;

 public:
  FlagParser();
  void RegisterHandler(const char *name, FlagHandlerBase *handler,
                       const char *desc);
  void ParseString(const char *s, const char *env_name = 0);
  void ParseStringFromEnv(const char *env_name);
  bool ParseFile(const char *path, bool ignore_missing);
  void PrintFlagDescriptions();

 private:
  void fatal_error(const char *err);
  bool is_space(char c);
  void skip_whitespace();
  void parse_flags(const char *env_option_name);
  void parse_flag(const char *env_option_name);
  bool run_handler(const char *name, const char *value);
  char *ll_strndup(const char *s, uptr n);
};

template <typename T>
static void RegisterFlag(FlagParser *parser, const char *name, const char *desc,
                         T *var) {
  FlagHandler<T> *fh = new (GetGlobalLowLevelAllocator()) FlagHandler<T>(var);
  parser->RegisterHandler(name, fh, desc);
}

void ReportUnrecognizedFlags();

}  // namespace __sanitizer

#endif  // SANITIZER_FLAG_REGISTRY_H
