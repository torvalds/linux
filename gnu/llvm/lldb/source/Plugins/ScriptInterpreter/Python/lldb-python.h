//===-- lldb-python.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_PLUGINS_SCRIPTINTERPRETER_PYTHON_LLDB_PYTHON_H
#define LLDB_PLUGINS_SCRIPTINTERPRETER_PYTHON_LLDB_PYTHON_H

// BEGIN FIXME
// This declaration works around a clang module build failure.
// It should be deleted ASAP.
#include "llvm/Support/Error.h"
static llvm::Expected<bool> *g_fcxx_modules_workaround;
// END

#include "lldb/Host/Config.h"

// Python.h needs to be included before any system headers in order to avoid
// redefinition of macros

#if LLDB_ENABLE_PYTHON
#include "llvm/Support/Compiler.h"
#if defined(_WIN32)
// If anyone #includes Host/PosixApi.h later, it will try to typedef pid_t.  We
// need to ensure this doesn't happen.  At the same time, Python.h will also try
// to redefine a bunch of stuff that PosixApi.h defines.  So define it all now
// so that PosixApi.h doesn't redefine it.
#define NO_PID_T
#endif
#if defined(__linux__)
// features.h will define _POSIX_C_SOURCE if _GNU_SOURCE is defined.  This value
// may be different from the value that Python defines it to be which results
// in a warning.  Undefine _POSIX_C_SOURCE before including Python.h  The same
// holds for _XOPEN_SOURCE.
#undef _POSIX_C_SOURCE
#undef _XOPEN_SOURCE
#endif

// Include locale before Python so _PY_PORT_CTYPE_UTF8_ISSUE doesn't cause
// macro redefinitions.
#if defined(__APPLE__)
#include <locale>
#endif

// Include python for non windows machines
#include <Python.h>
#endif

#endif // LLDB_PLUGINS_SCRIPTINTERPRETER_PYTHON_LLDB_PYTHON_H
