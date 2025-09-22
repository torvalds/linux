//===-- CFString.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Created by Greg Clayton on 1/16/08.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_DEBUGSERVER_SOURCE_MACOSX_CFSTRING_H
#define LLDB_TOOLS_DEBUGSERVER_SOURCE_MACOSX_CFSTRING_H

#include "CFUtils.h"
#include <iosfwd>

class CFString : public CFReleaser<CFStringRef> {
public:
  // Constructors and Destructors
  CFString(CFStringRef cf_str = NULL);
  CFString(const char *s, CFStringEncoding encoding = kCFStringEncodingUTF8);
  CFString(const CFString &rhs);
  CFString &operator=(const CFString &rhs);
  virtual ~CFString();

  const char *GetFileSystemRepresentation(std::string &str);
  CFStringRef SetFileSystemRepresentation(const char *path);
  CFStringRef SetFileSystemRepresentationFromCFType(CFTypeRef cf_type);
  CFStringRef SetFileSystemRepresentationAndExpandTilde(const char *path);
  const char *UTF8(std::string &str);
  CFIndex GetLength() const;
  static const char *UTF8(CFStringRef cf_str, std::string &str);
  static const char *FileSystemRepresentation(CFStringRef cf_str,
                                              std::string &str);
  static const char *GlobPath(const char *path, std::string &expanded_path);
};

#endif // LLDB_TOOLS_DEBUGSERVER_SOURCE_MACOSX_CFSTRING_H
