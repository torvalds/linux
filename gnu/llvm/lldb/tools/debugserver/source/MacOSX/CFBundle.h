//===-- CFBundle.h ----------------------------------------------*- C++ -*-===//
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

#ifndef LLDB_TOOLS_DEBUGSERVER_SOURCE_MACOSX_CFBUNDLE_H
#define LLDB_TOOLS_DEBUGSERVER_SOURCE_MACOSX_CFBUNDLE_H

#include "CFUtils.h"

class CFBundle : public CFReleaser<CFBundleRef> {
public:
  // Constructors and Destructors
  CFBundle(const char *path = NULL);
  CFBundle(const CFBundle &rhs);
  CFBundle &operator=(const CFBundle &rhs);
  virtual ~CFBundle();
  bool SetPath(const char *path);

  CFStringRef GetIdentifier() const;

  CFURLRef CopyExecutableURL() const;

protected:
  CFReleaser<CFURLRef> m_bundle_url;
};

#endif // LLDB_TOOLS_DEBUGSERVER_SOURCE_MACOSX_CFBUNDLE_H
