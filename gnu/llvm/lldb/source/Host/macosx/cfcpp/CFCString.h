//===-- CFCString.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_HOST_MACOSX_CFCPP_CFCSTRING_H
#define LLDB_SOURCE_HOST_MACOSX_CFCPP_CFCSTRING_H

#include <iosfwd>

#include "CFCReleaser.h"

class CFCString : public CFCReleaser<CFStringRef> {
public:
  // Constructors and Destructors
  CFCString(CFStringRef cf_str = NULL);
  CFCString(const char *s, CFStringEncoding encoding = kCFStringEncodingUTF8);
  CFCString(const CFCString &rhs);
  CFCString &operator=(const CFCString &rhs);
  ~CFCString() override;

  const char *GetFileSystemRepresentation(std::string &str);
  CFStringRef SetFileSystemRepresentation(const char *path);
  CFStringRef SetFileSystemRepresentationFromCFType(CFTypeRef cf_type);
  CFStringRef SetFileSystemRepresentationAndExpandTilde(const char *path);
  const char *UTF8(std::string &str);
  CFIndex GetLength() const;
  static const char *UTF8(CFStringRef cf_str, std::string &str);
  static const char *FileSystemRepresentation(CFStringRef cf_str,
                                              std::string &str);
  static const char *ExpandTildeInPath(const char *path,
                                       std::string &expanded_path);
};

#endif // LLDB_SOURCE_HOST_MACOSX_CFCPP_CFCSTRING_H
