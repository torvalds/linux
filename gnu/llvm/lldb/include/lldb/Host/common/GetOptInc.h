//===-- GetOptInc.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_COMMON_GETOPTINC_H
#define LLDB_HOST_COMMON_GETOPTINC_H

#include "lldb/lldb-defines.h"

#if defined(_MSC_VER)
#define REPLACE_GETOPT
#define REPLACE_GETOPT_LONG
#endif
#if defined(_MSC_VER) || defined(__NetBSD__)
#define REPLACE_GETOPT_LONG_ONLY
#endif

#if defined(REPLACE_GETOPT)
// from getopt.h
#define no_argument 0
#define required_argument 1
#define optional_argument 2

// option structure
struct option {
  const char *name;
  // has_arg can't be an enum because some compilers complain about type
  // mismatches in all the code that assumes it is an int.
  int has_arg;
  int *flag;
  int val;
};

int getopt(int argc, char *const argv[], const char *optstring);

// from getopt.h
extern char *optarg;
extern int optind;
extern int opterr;
extern int optopt;

// defined in unistd.h
extern int optreset;
#else
#include <getopt.h>
#include <unistd.h>
#endif

#if defined(REPLACE_GETOPT_LONG)
int getopt_long(int argc, char *const *argv, const char *optstring,
                const struct option *longopts, int *longindex);
#endif

#if defined(REPLACE_GETOPT_LONG_ONLY)
int getopt_long_only(int argc, char *const *argv, const char *optstring,
                     const struct option *longopts, int *longindex);
#endif

#endif // LLDB_HOST_COMMON_GETOPTINC_H
