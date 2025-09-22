//===-- MachException.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Created by Greg Clayton on 6/18/07.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_DEBUGSERVER_SOURCE_MACOSX_MACHEXCEPTION_H
#define LLDB_TOOLS_DEBUGSERVER_SOURCE_MACOSX_MACHEXCEPTION_H

#include <mach/mach.h>
#include <vector>

class MachProcess;
class PThreadMutex;

typedef union MachMessageTag {
  mach_msg_header_t hdr;
  char data[1024];
} MachMessage;

class MachException {
public:
  struct PortInfo {
    exception_mask_t mask; // the exception mask for this device which may be a
                           // subset of EXC_MASK_ALL...
    exception_mask_t masks[EXC_TYPES_COUNT];
    mach_port_t ports[EXC_TYPES_COUNT];
    exception_behavior_t behaviors[EXC_TYPES_COUNT];
    thread_state_flavor_t flavors[EXC_TYPES_COUNT];
    mach_msg_type_number_t count;

    kern_return_t Save(task_t task);
    kern_return_t Restore(task_t task);
  };

  struct Data {
    task_t task_port;
    thread_t thread_port;
    exception_type_t exc_type;
    std::vector<mach_exception_data_type_t> exc_data;
    Data()
        : task_port(TASK_NULL), thread_port(THREAD_NULL), exc_type(0),
          exc_data() {}

    void Clear() {
      task_port = TASK_NULL;
      thread_port = THREAD_NULL;
      exc_type = 0;
      exc_data.clear();
    }
    bool IsValid() const {
      return task_port != TASK_NULL && thread_port != THREAD_NULL &&
             exc_type != 0;
    }
    // Return the SoftSignal for this MachException data, or zero if there is
    // none
    int SoftSignal() const {
      if (exc_type == EXC_SOFTWARE && exc_data.size() == 2 &&
          exc_data[0] == EXC_SOFT_SIGNAL)
        return static_cast<int>(exc_data[1]);
      return 0;
    }
    bool IsBreakpoint() const {
      return (exc_type == EXC_BREAKPOINT ||
              ((exc_type == EXC_SOFTWARE) && exc_data[0] == 1));
    }
    void AppendExceptionData(mach_exception_data_t Data,
                             mach_msg_type_number_t Count) {
      mach_exception_data_type_t Buf;
      for (mach_msg_type_number_t i = 0; i < Count; ++i) {
        // Perform an unaligned copy.
        memcpy(&Buf, Data + i, sizeof(mach_exception_data_type_t));
        exc_data.push_back(Buf);
      }
    }
    void Dump() const;
    void DumpStopReason() const;
    bool GetStopInfo(struct DNBThreadStopInfo *stop_info) const;
  };

  struct Message {
    MachMessage exc_msg;
    MachMessage reply_msg;
    Data state;

    Message() : state() {
      memset(&exc_msg, 0, sizeof(exc_msg));
      memset(&reply_msg, 0, sizeof(reply_msg));
    }
    bool CatchExceptionRaise(task_t task);
    void Dump() const;
    kern_return_t Reply(MachProcess *process, int signal);
    kern_return_t Receive(mach_port_t receive_port, mach_msg_option_t options,
                          mach_msg_timeout_t timeout,
                          mach_port_t notify_port = MACH_PORT_NULL);

    typedef std::vector<Message> collection;
    typedef collection::iterator iterator;
    typedef collection::const_iterator const_iterator;
  };

  enum {
    e_actionForward, // Forward signal to inferior process
    e_actionStop,    // Stop when this signal is received
  };
  struct Action {
    task_t task_port;          // Set to TASK_NULL for any TASK
    thread_t thread_port;      // Set to THREAD_NULL for any thread
    exception_type_t exc_mask; // Mach exception mask to watch for
    std::vector<mach_exception_data_type_t> exc_data_mask; // Mask to apply to
                                                           // exception data, or
                                                           // empty to ignore
                                                           // exc_data value for
                                                           // exception
    std::vector<mach_exception_data_type_t> exc_data_value; // Value to compare
                                                            // to exception data
                                                            // after masking, or
                                                            // empty to ignore
                                                            // exc_data value
                                                            // for exception
    uint8_t flags; // Action flags describing what to do with the exception
  };
  static const char *Name(exception_type_t exc_type);
  static exception_mask_t ExceptionMask(const char *name);
};

#endif
