//===-- ThreadPlanTracer.h --------------------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_THREADPLANTRACER_H
#define LLDB_TARGET_THREADPLANTRACER_H

#include "lldb/Symbol/TaggedASTType.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

class ThreadPlanTracer {
  friend class ThreadPlan;

public:
  enum ThreadPlanTracerStyle {
    eLocation = 0,
    eStateChange,
    eCheckFrames,
    ePython
  };

  ThreadPlanTracer(Thread &thread, lldb::StreamSP &stream_sp);
  ThreadPlanTracer(Thread &thread);

  virtual ~ThreadPlanTracer() = default;

  virtual void TracingStarted() {}

  virtual void TracingEnded() {}

  bool EnableTracing(bool value) {
    bool old_value = m_enabled;
    m_enabled = value;
    if (old_value == false && value == true)
      TracingStarted();
    else if (old_value == true && value == false)
      TracingEnded();

    return old_value;
  }

  bool TracingEnabled() { return m_enabled; }

  Thread &GetThread();

protected:
  Process &m_process;
  lldb::tid_t m_tid;

  Stream *GetLogStream();

  virtual void Log();

private:
  bool TracerExplainsStop();

  bool m_enabled;
  lldb::StreamSP m_stream_sp;
  Thread *m_thread;
};

class ThreadPlanAssemblyTracer : public ThreadPlanTracer {
public:
  ThreadPlanAssemblyTracer(Thread &thread, lldb::StreamSP &stream_sp);
  ThreadPlanAssemblyTracer(Thread &thread);
  ~ThreadPlanAssemblyTracer() override;

  void TracingStarted() override;
  void TracingEnded() override;
  void Log() override;

private:
  Disassembler *GetDisassembler();

  TypeFromUser GetIntPointerType();

  lldb::DisassemblerSP m_disassembler_sp;
  TypeFromUser m_intptr_type;
  std::vector<RegisterValue> m_register_values;
  lldb::DataBufferSP m_buffer_sp;
};

} // namespace lldb_private

#endif // LLDB_TARGET_THREADPLANTRACER_H
