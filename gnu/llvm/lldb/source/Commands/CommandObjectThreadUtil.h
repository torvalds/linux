//===-- CommandObjectThreadUtil.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_COMMANDS_COMMANDOBJECTTHREADUTIL_H
#define LLDB_SOURCE_COMMANDS_COMMANDOBJECTTHREADUTIL_H

#include "lldb/Interpreter/CommandObjectMultiword.h"
#include <stack>

namespace lldb_private {

class CommandObjectIterateOverThreads : public CommandObjectParsed {

  class UniqueStack {
  public:
    UniqueStack(std::stack<lldb::addr_t> stack_frames, uint32_t thread_index_id)
        : m_stack_frames(stack_frames) {
      m_thread_index_ids.push_back(thread_index_id);
    }

    void AddThread(uint32_t thread_index_id) const {
      m_thread_index_ids.push_back(thread_index_id);
    }

    const std::vector<uint32_t> &GetUniqueThreadIndexIDs() const {
      return m_thread_index_ids;
    }

    lldb::tid_t GetRepresentativeThread() const {
      return m_thread_index_ids.front();
    }

    friend bool inline operator<(const UniqueStack &lhs,
                                 const UniqueStack &rhs) {
      return lhs.m_stack_frames < rhs.m_stack_frames;
    }

  protected:
    // Mark the thread index as mutable, as we don't care about it from a const
    // perspective, we only care about m_stack_frames so we keep our std::set
    // sorted.
    mutable std::vector<uint32_t> m_thread_index_ids;
    std::stack<lldb::addr_t> m_stack_frames;
  };

public:
  CommandObjectIterateOverThreads(CommandInterpreter &interpreter,
                                  const char *name, const char *help,
                                  const char *syntax, uint32_t flags);

  ~CommandObjectIterateOverThreads() override = default;

  void DoExecute(Args &command, CommandReturnObject &result) override;

protected:
  // Override this to do whatever you need to do for one thread.
  //
  // If you return false, the iteration will stop, otherwise it will proceed.
  // The result is set to m_success_return (defaults to
  // eReturnStatusSuccessFinishResult) before the iteration, so you only need
  // to set the return status in HandleOneThread if you want to indicate an
  // error. If m_add_return is true, a blank line will be inserted between each
  // of the listings (except the last one.)

  virtual bool HandleOneThread(lldb::tid_t, CommandReturnObject &result) = 0;

  bool BucketThread(lldb::tid_t tid, std::set<UniqueStack> &unique_stacks,
                    CommandReturnObject &result);

  lldb::ReturnStatus m_success_return = lldb::eReturnStatusSuccessFinishResult;
  bool m_unique_stacks = false;
  bool m_add_return = true;
};

/// Class similar to \a CommandObjectIterateOverThreads, but which performs
/// an action on multiple threads at once instead of iterating over each thread.
class CommandObjectMultipleThreads : public CommandObjectParsed {
public:
  CommandObjectMultipleThreads(CommandInterpreter &interpreter,
                               const char *name, const char *help,
                               const char *syntax, uint32_t flags);

  void DoExecute(Args &command, CommandReturnObject &result) override;

protected:
  /// Method that handles the command after the main arguments have been parsed.
  ///
  /// \param[in] tids
  ///     The thread ids passed as arguments.
  ///
  /// \return
  ///     A boolean result similar to the one expected from \a DoExecute.
  virtual bool DoExecuteOnThreads(Args &command, CommandReturnObject &result,
                                  llvm::ArrayRef<lldb::tid_t> tids) = 0;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_COMMANDS_COMMANDOBJECTTHREADUTIL_H
