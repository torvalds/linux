//===-- DNBError.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Created by Greg Clayton on 6/26/07.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_DEBUGSERVER_SOURCE_DNBERROR_H
#define LLDB_TOOLS_DEBUGSERVER_SOURCE_DNBERROR_H

#include <cerrno>
#include <cstdio>
#include <mach/mach.h>
#include <string>

class DNBError {
public:
  typedef uint32_t ValueType;
  enum FlavorType {
    Generic = 0,
    MachKernel = 1,
    POSIX = 2
#ifdef WITH_SPRINGBOARD
    ,
    SpringBoard = 3
#endif
#ifdef WITH_BKS
    ,
    BackBoard = 4
#endif
#ifdef WITH_FBS
    ,
    FrontBoard = 5
#endif
  };

  explicit DNBError(ValueType err = 0, FlavorType flavor = Generic)
      : m_err(err), m_flavor(flavor) {}

  const char *AsString() const;
  void Clear() {
    m_err = 0;
    m_flavor = Generic;
    m_str.clear();
  }
  ValueType Status() const { return m_err; }
  FlavorType Flavor() const { return m_flavor; }

  ValueType operator=(kern_return_t err) {
    m_err = err;
    m_flavor = MachKernel;
    m_str.clear();
    return m_err;
  }

  void SetError(kern_return_t err) {
    m_err = err;
    m_flavor = MachKernel;
    m_str.clear();
  }

  void SetErrorToErrno() {
    m_err = errno;
    m_flavor = POSIX;
    m_str.clear();
  }

  void SetError(ValueType err, FlavorType flavor) {
    m_err = err;
    m_flavor = flavor;
    m_str.clear();
  }

  // Generic errors can set their own string values
  void SetErrorString(const char *err_str) {
    if (err_str && err_str[0])
      m_str = err_str;
    else
      m_str.clear();
  }
  bool Success() const { return m_err == 0; }
  bool Fail() const { return m_err != 0; }
  void LogThreadedIfError(const char *format, ...) const;
  void LogThreaded(const char *format, ...) const;

protected:
  ValueType m_err;
  FlavorType m_flavor;
  mutable std::string m_str;
};

#endif // LLDB_TOOLS_DEBUGSERVER_SOURCE_DNBERROR_H
