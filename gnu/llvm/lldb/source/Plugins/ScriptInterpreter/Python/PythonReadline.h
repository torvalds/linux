//===-- PythonReadline.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_PLUGINS_SCRIPTINTERPRETER_PYTHON_PYTHONREADLINE_H
#define LLDB_PLUGINS_SCRIPTINTERPRETER_PYTHON_PYTHONREADLINE_H

#include "lldb/Host/Config.h"

#if LLDB_ENABLE_LIBEDIT && defined(__linux__)
// NOTE: Since Python may define some pre-processor definitions which affect the
// standard headers on some systems, you must include Python.h before any
// standard headers are included.
#include "Python.h"

// no need to hack into Python's readline module if libedit isn't used.
//
#define LLDB_USE_LIBEDIT_READLINE_COMPAT_MODULE 1

PyMODINIT_FUNC initlldb_readline(void);

#endif

#endif // LLDB_PLUGINS_SCRIPTINTERPRETER_PYTHON_PYTHONREADLINE_H
