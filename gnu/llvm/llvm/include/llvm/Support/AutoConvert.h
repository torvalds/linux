//===- AutoConvert.h - Auto conversion between ASCII/EBCDIC -----*- C++ -*-===//
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

#ifndef LLVM_SUPPORT_AUTOCONVERT_H
#define LLVM_SUPPORT_AUTOCONVERT_H

#ifdef __MVS__
#include <_Ccsid.h>
#ifdef __cplusplus
#include <system_error>
#endif // __cplusplus

#define CCSID_IBM_1047 1047
#define CCSID_UTF_8 1208
#define CCSID_ISO8859_1 819

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus
int enableAutoConversion(int FD);
int disableAutoConversion(int FD);
int restoreStdHandleAutoConversion(int FD);
#ifdef __cplusplus
}
#endif // __cplusplus

#ifdef __cplusplus
namespace llvm {

/// \brief Disable the z/OS enhanced ASCII auto-conversion for the file
/// descriptor.
std::error_code disableAutoConversion(int FD);

/// \brief Query the z/OS enhanced ASCII auto-conversion status of a file
/// descriptor and force the conversion if the file is not tagged with a
/// codepage.
std::error_code enableAutoConversion(int FD);

/// Restore the z/OS enhanced ASCII auto-conversion for the std handle.
std::error_code restoreStdHandleAutoConversion(int FD);

/// \brief Set the tag information for a file descriptor.
std::error_code setFileTag(int FD, int CCSID, bool Text);

} // namespace llvm
#endif // __cplusplus

#endif // __MVS__

#endif // LLVM_SUPPORT_AUTOCONVERT_H
