//===-- DNBError.cpp --------------------------------------------*- C++ -*-===//
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

#include "DNBError.h"
#include "CFString.h"
#include "DNBLog.h"
#include "PThreadMutex.h"

#ifdef WITH_SPRINGBOARD
#include <SpringBoardServices/SpringBoardServer.h>
#endif

const char *DNBError::AsString() const {
  if (Success())
    return NULL;

  if (m_str.empty()) {
    const char *s = NULL;
    switch (m_flavor) {
    case MachKernel:
      s = ::mach_error_string(m_err);
      break;

    case POSIX:
      s = ::strerror(m_err);
      break;

#ifdef WITH_SPRINGBOARD
    case SpringBoard: {
      CFStringRef statusStr = SBSApplicationLaunchingErrorString(m_err);
      if (CFString::UTF8(statusStr, m_str) == NULL)
        m_str.clear();
    } break;
#endif
#ifdef WITH_BKS
    case BackBoard: {
      // You have to call ObjC routines to get the error string from
      // BackBoardServices.
      // Not sure I want to make DNBError.cpp an .mm file.  For now just make
      // sure you
      // pre-populate the error string when you make the DNBError of type
      // BackBoard.
      m_str.assign(
          "Should have set BackBoard error when making the error string.");
    } break;
#endif
#ifdef WITH_FBS
    case FrontBoard: {
      // You have to call ObjC routines to get the error string from
      // FrontBoardServices.
      // Not sure I want to make DNBError.cpp an .mm file.  For now just make
      // sure you
      // pre-populate the error string when you make the DNBError of type
      // FrontBoard.
      m_str.assign(
          "Should have set FrontBoard error when making the error string.");
    } break;
#endif
    default:
      break;
    }
    if (s)
      m_str.assign(s);
  }
  if (m_str.empty())
    return NULL;
  return m_str.c_str();
}

void DNBError::LogThreadedIfError(const char *format, ...) const {
  if (Fail()) {
    char *arg_msg = NULL;
    va_list args;
    va_start(args, format);
    ::vasprintf(&arg_msg, format, args);
    va_end(args);

    if (arg_msg != NULL) {
      const char *err_str = AsString();
      if (err_str == NULL)
        err_str = "???";
      DNBLogThreaded("error: %s err = %s (0x%8.8x)", arg_msg, err_str, m_err);
      free(arg_msg);
    }
  }
}

void DNBError::LogThreaded(const char *format, ...) const {
  char *arg_msg = NULL;
  va_list args;
  va_start(args, format);
  ::vasprintf(&arg_msg, format, args);
  va_end(args);

  if (arg_msg != NULL) {
    if (Fail()) {
      const char *err_str = AsString();
      if (err_str == NULL)
        err_str = "???";
      DNBLogThreaded("error: %s err = %s (0x%8.8x)", arg_msg, err_str, m_err);
    } else {
      DNBLogThreaded("%s err = 0x%8.8x", arg_msg, m_err);
    }
    free(arg_msg);
  }
}
