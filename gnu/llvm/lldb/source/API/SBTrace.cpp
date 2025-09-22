//===-- SBTrace.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/Process.h"
#include "lldb/Utility/Instrumentation.h"

#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBStructuredData.h"
#include "lldb/API/SBThread.h"
#include "lldb/API/SBTrace.h"

#include "lldb/Core/StructuredDataImpl.h"

#include <memory>

using namespace lldb;
using namespace lldb_private;
using namespace llvm;

SBTrace::SBTrace() { LLDB_INSTRUMENT_VA(this); }

SBTrace::SBTrace(const lldb::TraceSP &trace_sp) : m_opaque_sp(trace_sp) {
  LLDB_INSTRUMENT_VA(this, trace_sp);
}

SBTrace SBTrace::LoadTraceFromFile(SBError &error, SBDebugger &debugger,
                                   const SBFileSpec &trace_description_file) {
  LLDB_INSTRUMENT_VA(error, debugger, trace_description_file);

  Expected<lldb::TraceSP> trace_or_err = Trace::LoadPostMortemTraceFromFile(
      debugger.ref(), trace_description_file.ref());

  if (!trace_or_err) {
    error.SetErrorString(toString(trace_or_err.takeError()).c_str());
    return SBTrace();
  }

  return SBTrace(trace_or_err.get());
}

SBTraceCursor SBTrace::CreateNewCursor(SBError &error, SBThread &thread) {
  LLDB_INSTRUMENT_VA(this, error, thread);

  if (!m_opaque_sp) {
    error.SetErrorString("error: invalid trace");
    return SBTraceCursor();
  }
  if (!thread.get()) {
    error.SetErrorString("error: invalid thread");
    return SBTraceCursor();
  }

  if (llvm::Expected<lldb::TraceCursorSP> trace_cursor_sp =
          m_opaque_sp->CreateNewCursor(*thread.get())) {
    return SBTraceCursor(std::move(*trace_cursor_sp));
  } else {
    error.SetErrorString(llvm::toString(trace_cursor_sp.takeError()).c_str());
    return SBTraceCursor();
  }
}

SBFileSpec SBTrace::SaveToDisk(SBError &error, const SBFileSpec &bundle_dir,
                               bool compact) {
  LLDB_INSTRUMENT_VA(this, error, bundle_dir, compact);

  error.Clear();
  SBFileSpec file_spec;

  if (!m_opaque_sp)
    error.SetErrorString("error: invalid trace");
  else if (Expected<FileSpec> desc_file =
               m_opaque_sp->SaveToDisk(bundle_dir.ref(), compact))
    file_spec.SetFileSpec(*desc_file);
  else
    error.SetErrorString(llvm::toString(desc_file.takeError()).c_str());

  return file_spec;
}

const char *SBTrace::GetStartConfigurationHelp() {
  LLDB_INSTRUMENT_VA(this);
  if (!m_opaque_sp)
    return nullptr;

  return ConstString(m_opaque_sp->GetStartConfigurationHelp()).GetCString();
}

SBError SBTrace::Start(const SBStructuredData &configuration) {
  LLDB_INSTRUMENT_VA(this, configuration);
  SBError error;
  if (!m_opaque_sp)
    error.SetErrorString("error: invalid trace");
  else if (llvm::Error err =
               m_opaque_sp->Start(configuration.m_impl_up->GetObjectSP()))
    error.SetErrorString(llvm::toString(std::move(err)).c_str());
  return error;
}

SBError SBTrace::Start(const SBThread &thread,
                       const SBStructuredData &configuration) {
  LLDB_INSTRUMENT_VA(this, thread, configuration);

  SBError error;
  if (!m_opaque_sp)
    error.SetErrorString("error: invalid trace");
  else {
    if (llvm::Error err =
            m_opaque_sp->Start(std::vector<lldb::tid_t>{thread.GetThreadID()},
                               configuration.m_impl_up->GetObjectSP()))
      error.SetErrorString(llvm::toString(std::move(err)).c_str());
  }

  return error;
}

SBError SBTrace::Stop() {
  LLDB_INSTRUMENT_VA(this);
  SBError error;
  if (!m_opaque_sp)
    error.SetErrorString("error: invalid trace");
  else if (llvm::Error err = m_opaque_sp->Stop())
    error.SetErrorString(llvm::toString(std::move(err)).c_str());
  return error;
}

SBError SBTrace::Stop(const SBThread &thread) {
  LLDB_INSTRUMENT_VA(this, thread);
  SBError error;
  if (!m_opaque_sp)
    error.SetErrorString("error: invalid trace");
  else if (llvm::Error err = m_opaque_sp->Stop({thread.GetThreadID()}))
    error.SetErrorString(llvm::toString(std::move(err)).c_str());
  return error;
}

bool SBTrace::IsValid() {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}

SBTrace::operator bool() const {
  LLDB_INSTRUMENT_VA(this);
  return (bool)m_opaque_sp;
}
