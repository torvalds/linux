//===-- HostGetOpt.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_HOSTGETOPT_H
#define LLDB_HOST_HOSTGETOPT_H

#if !defined(_MSC_VER) && !defined(__NetBSD__)

#include <getopt.h>
#include <unistd.h>

#else

#include <lldb/Host/common/GetOptInc.h>

#endif

#endif // LLDB_HOST_HOSTGETOPT_H
