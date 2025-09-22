//===-- UnwindLLDB.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_UNWINDLLDB_H
#define LLDB_TARGET_UNWINDLLDB_H

#include <vector>

#include "lldb/Symbol/FuncUnwinders.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/Unwind.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/lldb-public.h"

namespace lldb_private {

class RegisterContextUnwind;

class UnwindLLDB : public lldb_private::Unwind {
public:
  UnwindLLDB(lldb_private::Thread &thread);

  ~UnwindLLDB() override = default;

  enum RegisterSearchResult {
    eRegisterFound = 0,
    eRegisterNotFound,
    eRegisterIsVolatile
  };

protected:
  friend class lldb_private::RegisterContextUnwind;

  struct RegisterLocation {
    enum RegisterLocationTypes {
      eRegisterNotSaved = 0, // register was not preserved by callee.  If
                             // volatile reg, is unavailable
      eRegisterSavedAtMemoryLocation, // register is saved at a specific word of
                                      // target mem (target_memory_location)
      eRegisterInRegister, // register is available in a (possible other)
                           // register (register_number)
      eRegisterSavedAtHostMemoryLocation, // register is saved at a word in
                                          // lldb's address space
      eRegisterValueInferred,        // register val was computed (and is in
                                     // inferred_value)
      eRegisterInLiveRegisterContext // register value is in a live (stack frame
                                     // #0) register
    };
    int type;
    union {
      lldb::addr_t target_memory_location;
      uint32_t
          register_number; // in eRegisterKindLLDB register numbering system
      void *host_memory_location;
      uint64_t inferred_value; // eRegisterValueInferred - e.g. stack pointer ==
                               // cfa + offset
    } location;
  };

  void DoClear() override {
    m_frames.clear();
    m_candidate_frame.reset();
    m_unwind_complete = false;
  }

  uint32_t DoGetFrameCount() override;

  bool DoGetFrameInfoAtIndex(uint32_t frame_idx, lldb::addr_t &cfa,
                             lldb::addr_t &start_pc,
                             bool &behaves_like_zeroth_frame) override;

  lldb::RegisterContextSP
  DoCreateRegisterContextForFrame(lldb_private::StackFrame *frame) override;

  typedef std::shared_ptr<RegisterContextUnwind> RegisterContextLLDBSP;

  // Needed to retrieve the "next" frame (e.g. frame 2 needs to retrieve frame
  // 1's RegisterContextUnwind)
  // The RegisterContext for frame_num must already exist or this returns an
  // empty shared pointer.
  RegisterContextLLDBSP GetRegisterContextForFrameNum(uint32_t frame_num);

  // Iterate over the RegisterContextUnwind's in our m_frames vector, look for
  // the first one that has a saved location for this reg.
  bool SearchForSavedLocationForRegister(
      uint32_t lldb_regnum, lldb_private::UnwindLLDB::RegisterLocation &regloc,
      uint32_t starting_frame_num, bool pc_register);

  /// Provide the list of user-specified trap handler functions
  ///
  /// The Platform is one source of trap handler function names; that
  /// may be augmented via a setting.  The setting needs to be converted
  /// into an array of ConstStrings before it can be used - we only want
  /// to do that once per thread so it's here in the UnwindLLDB object.
  ///
  /// \return
  ///     Vector of ConstStrings of trap handler function names.  May be
  ///     empty.
  const std::vector<ConstString> &GetUserSpecifiedTrapHandlerFunctionNames() {
    return m_user_supplied_trap_handler_functions;
  }

private:
  struct Cursor {
    lldb::addr_t start_pc =
        LLDB_INVALID_ADDRESS; // The start address of the function/symbol for
                              // this frame - current pc if unknown
    lldb::addr_t cfa = LLDB_INVALID_ADDRESS; // The canonical frame address for
                                             // this stack frame
    lldb_private::SymbolContext sctx; // A symbol context we'll contribute to &
                                      // provide to the StackFrame creation
    RegisterContextLLDBSP
        reg_ctx_lldb_sp; // These are all RegisterContextUnwind's

    Cursor() = default;

  private:
    Cursor(const Cursor &) = delete;
    const Cursor &operator=(const Cursor &) = delete;
  };

  typedef std::shared_ptr<Cursor> CursorSP;
  std::vector<CursorSP> m_frames;
  CursorSP m_candidate_frame;
  bool m_unwind_complete; // If this is true, we've enumerated all the frames in
                          // the stack, and m_frames.size() is the
  // number of frames, etc.  Otherwise we've only gone as far as directly asked,
  // and m_frames.size()
  // is how far we've currently gone.

  std::vector<ConstString> m_user_supplied_trap_handler_functions;

  // Check if Full UnwindPlan of First frame is valid or not.
  // If not then try Fallback UnwindPlan of the frame. If Fallback
  // UnwindPlan succeeds then update the Full UnwindPlan with the
  // Fallback UnwindPlan.
  void UpdateUnwindPlanForFirstFrameIfInvalid(ABI *abi);

  CursorSP GetOneMoreFrame(ABI *abi);

  bool AddOneMoreFrame(ABI *abi);

  bool AddFirstFrame();

  // For UnwindLLDB only
  UnwindLLDB(const UnwindLLDB &) = delete;
  const UnwindLLDB &operator=(const UnwindLLDB &) = delete;
};

} // namespace lldb_private

#endif // LLDB_TARGET_UNWINDLLDB_H
