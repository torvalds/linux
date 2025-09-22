//===-- RNBDefs.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Created by Greg Clayton on 12/14/07.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_DEBUGSERVER_SOURCE_RNBDEFS_H
#define LLDB_TOOLS_DEBUGSERVER_SOURCE_RNBDEFS_H

#include "DNBDefs.h"
#include <memory>

#define CONCAT2(a, b) a##b
#define CONCAT(a, b) CONCAT2(a, b)
#define STRINGIZE2(x) #x
#define STRINGIZE(x) STRINGIZE2(x)

#if !defined(DEBUGSERVER_PROGRAM_SYMBOL)
#define DEBUGSERVER_PROGRAM_SYMBOL debugserver
#endif

#if !defined(DEBUGSERVER_PROGRAM_NAME)
#define DEBUGSERVER_PROGRAM_NAME STRINGIZE(DEBUGSERVER_PROGRAM_SYMBOL)
#endif

#ifndef DEBUGSERVER_VERSION_NUM
extern "C" const unsigned char CONCAT(DEBUGSERVER_PROGRAM_SYMBOL,
                                      VersionString)[];
#define DEBUGSERVER_VERSION_NUM                                                \
  CONCAT(DEBUGSERVER_PROGRAM_SYMBOL, VersionNumber)
#endif

#ifndef DEBUGSERVER_VERSION_STR
extern "C" const double CONCAT(DEBUGSERVER_PROGRAM_SYMBOL, VersionNumber);
#define DEBUGSERVER_VERSION_STR                                                \
  CONCAT(DEBUGSERVER_PROGRAM_SYMBOL, VersionString)
#endif

#if defined(__i386__)

#define RNB_ARCH "i386"

#elif defined(__x86_64__)

#define RNB_ARCH "x86_64"

#elif defined(__arm64__) || defined(__aarch64__)

#define RNB_ARCH "arm64"

#elif defined(__arm__)

#define RNB_ARCH "armv7"

#else

#error undefined architecture

#endif

class RNBRemote;
typedef std::shared_ptr<RNBRemote> RNBRemoteSP;

enum rnb_err_t { rnb_success = 0, rnb_err = 1, rnb_not_connected = 2 };

// Log bits
// reserve low bits for DNB
#define LOG_RNB_MINIMAL                                                        \
  ((LOG_LO_USER) << 0) // Minimal logging    (min verbosity)
#define LOG_RNB_MEDIUM                                                         \
  ((LOG_LO_USER) << 1)                    // Medium logging     (med verbosity)
#define LOG_RNB_MAX ((LOG_LO_USER) << 2)  // Max logging        (max verbosity)
#define LOG_RNB_COMM ((LOG_LO_USER) << 3) // Log communications (RNBSocket)
#define LOG_RNB_REMOTE ((LOG_LO_USER) << 4) // Log remote         (RNBRemote)
#define LOG_RNB_EVENTS                                                         \
  ((LOG_LO_USER) << 5)                    // Log events         (PThreadEvents)
#define LOG_RNB_PROC ((LOG_LO_USER) << 6) // Log process state  (Process thread)
#define LOG_RNB_PACKETS ((LOG_LO_USER) << 7) // Log gdb remote packets
#define LOG_RNB_ALL (~((LOG_LO_USER)-1))
#define LOG_RNB_DEFAULT (LOG_RNB_ALL)

extern RNBRemoteSP g_remoteSP;

#endif // LLDB_TOOLS_DEBUGSERVER_SOURCE_RNBDEFS_H
