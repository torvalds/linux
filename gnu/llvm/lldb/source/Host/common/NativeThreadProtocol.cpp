//===-- NativeThreadProtocol.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/common/NativeThreadProtocol.h"

#include "lldb/Host/common/NativeProcessProtocol.h"
#include "lldb/Host/common/NativeRegisterContext.h"

using namespace lldb;
using namespace lldb_private;

NativeThreadProtocol::NativeThreadProtocol(NativeProcessProtocol &process,
                                           lldb::tid_t tid)
    : m_process(process), m_tid(tid) {}
