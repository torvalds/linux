//===- AutoConvert.cpp - Auto conversion between ASCII/EBCDIC -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains functions used for auto conversion between
// ASCII/EBCDIC codepages specific to z/OS.
//
//===----------------------------------------------------------------------===//

#ifdef __MVS__

#include "llvm/Support/AutoConvert.h"
#include "llvm/Support/Error.h"
#include <cassert>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static int savedStdHandleAutoConversionMode[3] = {-1, -1, -1};

int disableAutoConversion(int FD) {
  static const struct f_cnvrt Convert = {
      SETCVTOFF, // cvtcmd
      0,         // pccsid
      0,         // fccsid
  };

  return fcntl(FD, F_CONTROL_CVT, &Convert);
}

int restoreStdHandleAutoConversion(int FD) {
  assert(FD == STDIN_FILENO || FD == STDOUT_FILENO || FD == STDERR_FILENO);
  if (savedStdHandleAutoConversionMode[FD] == -1)
    return 0;
  struct f_cnvrt Cvt = {
      savedStdHandleAutoConversionMode[FD], // cvtcmd
      0,                                    // pccsid
      0,                                    // fccsid
  };
  return (fcntl(FD, F_CONTROL_CVT, &Cvt));
}

int enableAutoConversion(int FD) {
  struct f_cnvrt Query = {
      QUERYCVT, // cvtcmd
      0,        // pccsid
      0,        // fccsid
  };

  if (fcntl(FD, F_CONTROL_CVT, &Query) == -1)
    return -1;

  // We don't need conversion for UTF-8 tagged files.
  // TODO: Remove the assumption of ISO8859-1 = UTF-8 here when we fully resolve
  // problems related to UTF-8 tagged source files.
  // When the pccsid is not ISO8859-1, autoconversion is still needed.
  if (Query.pccsid == CCSID_ISO8859_1 &&
      (Query.fccsid == CCSID_UTF_8 || Query.fccsid == CCSID_ISO8859_1))
    return 0;

  // Save the state of std handles before we make changes to it.
  if ((FD == STDIN_FILENO || FD == STDOUT_FILENO || FD == STDERR_FILENO) &&
      savedStdHandleAutoConversionMode[FD] == -1)
    savedStdHandleAutoConversionMode[FD] = Query.cvtcmd;

  if (FD == STDOUT_FILENO || FD == STDERR_FILENO)
    Query.cvtcmd = SETCVTON;
  else
    Query.cvtcmd = SETCVTALL;

  Query.pccsid =
      (FD == STDIN_FILENO || FD == STDOUT_FILENO || FD == STDERR_FILENO)
          ? 0
          : CCSID_UTF_8;
  // Assume untagged files to be IBM-1047 encoded.
  Query.fccsid = (Query.fccsid == FT_UNTAGGED) ? CCSID_IBM_1047 : Query.fccsid;
  return fcntl(FD, F_CONTROL_CVT, &Query);
}

std::error_code llvm::disableAutoConversion(int FD) {
  if (::disableAutoConversion(FD) == -1)
    return errnoAsErrorCode();

  return std::error_code();
}

std::error_code llvm::enableAutoConversion(int FD) {
  if (::enableAutoConversion(FD) == -1)
    return errnoAsErrorCode();

  return std::error_code();
}

std::error_code llvm::restoreStdHandleAutoConversion(int FD) {
  if (::restoreStdHandleAutoConversion(FD) == -1)
    return errnoAsErrorCode();

  return std::error_code();
}

std::error_code llvm::setFileTag(int FD, int CCSID, bool Text) {
  assert((!Text || (CCSID != FT_UNTAGGED && CCSID != FT_BINARY)) &&
         "FT_UNTAGGED and FT_BINARY are not allowed for text files");
  struct file_tag Tag;
  Tag.ft_ccsid = CCSID;
  Tag.ft_txtflag = Text;
  Tag.ft_deferred = 0;
  Tag.ft_rsvflags = 0;

  if (fcntl(FD, F_SETTAG, &Tag) == -1)
    return errnoAsErrorCode();
  return std::error_code();
}

#endif // __MVS__
