//===-- SBExecutionContext.h -----------------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBEXECUTIONCONTEXT_H
#define LLDB_API_SBEXECUTIONCONTEXT_H

#include "lldb/API/SBDefines.h"

#include <cstdio>
#include <vector>

namespace lldb_private {
namespace python {
class SWIGBridge;
}
} // namespace lldb_private

namespace lldb {

class LLDB_API SBExecutionContext {
  friend class SBCommandInterpreter;

public:
  SBExecutionContext();

  SBExecutionContext(const lldb::SBExecutionContext &rhs);

  SBExecutionContext(const lldb::SBTarget &target);

  SBExecutionContext(const lldb::SBProcess &process);

  SBExecutionContext(lldb::SBThread thread); // can't be a const& because
                                             // SBThread::get() isn't itself a
                                             // const function

  SBExecutionContext(const lldb::SBFrame &frame);

  ~SBExecutionContext();

  const SBExecutionContext &operator=(const lldb::SBExecutionContext &rhs);

  SBTarget GetTarget() const;

  SBProcess GetProcess() const;

  SBThread GetThread() const;

  SBFrame GetFrame() const;

protected:
  friend class lldb_private::python::SWIGBridge;

  lldb_private::ExecutionContextRef *get() const;

  SBExecutionContext(lldb::ExecutionContextRefSP exe_ctx_ref_sp);

private:
  mutable lldb::ExecutionContextRefSP m_exe_ctx_sp;
};

} // namespace lldb

#endif // LLDB_API_SBEXECUTIONCONTEXT_H
