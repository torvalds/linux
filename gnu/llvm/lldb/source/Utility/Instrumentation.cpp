//===-- Instrumentation.cpp -----------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/Instrumentation.h"
#include "lldb/Utility/LLDBLog.h"
#include "llvm/Support/Signposts.h"

#include <cstdio>
#include <cstdlib>
#include <limits>
#include <thread>

using namespace lldb_private;
using namespace lldb_private::instrumentation;

// Whether we're currently across the API boundary.
static thread_local bool g_global_boundary = false;

// Instrument SB API calls with singposts when supported.
static llvm::ManagedStatic<llvm::SignpostEmitter> g_api_signposts;

Instrumenter::Instrumenter(llvm::StringRef pretty_func,
                           std::string &&pretty_args)
    : m_pretty_func(pretty_func) {
  if (!g_global_boundary) {
    g_global_boundary = true;
    m_local_boundary = true;
    g_api_signposts->startInterval(this, m_pretty_func);
  }
  LLDB_LOG(GetLog(LLDBLog::API), "[{0}] {1} ({2})",
           m_local_boundary ? "external" : "internal", m_pretty_func,
           pretty_args);
}

Instrumenter::~Instrumenter() {
  if (m_local_boundary) {
    g_global_boundary = false;
    g_api_signposts->endInterval(this, m_pretty_func);
  }
}
