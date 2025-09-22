//===-- StackFrameList.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_STACKFRAMELIST_H
#define LLDB_TARGET_STACKFRAMELIST_H

#include <memory>
#include <mutex>
#include <vector>

#include "lldb/Target/StackFrame.h"

namespace lldb_private {

class ScriptedThread;

class StackFrameList {
public:
  // Constructors and Destructors
  StackFrameList(Thread &thread, const lldb::StackFrameListSP &prev_frames_sp,
                 bool show_inline_frames);

  ~StackFrameList();

  /// Get the number of visible frames. Frames may be created if \p can_create
  /// is true. Synthetic (inline) frames expanded from the concrete frame #0
  /// (aka invisible frames) are not included in this count.
  uint32_t GetNumFrames(bool can_create = true);

  /// Get the frame at index \p idx. Invisible frames cannot be indexed.
  lldb::StackFrameSP GetFrameAtIndex(uint32_t idx);

  /// Get the first concrete frame with index greater than or equal to \p idx.
  /// Unlike \ref GetFrameAtIndex, this cannot return a synthetic frame.
  lldb::StackFrameSP GetFrameWithConcreteFrameIndex(uint32_t unwind_idx);

  /// Retrieve the stack frame with the given ID \p stack_id.
  lldb::StackFrameSP GetFrameWithStackID(const StackID &stack_id);

  /// Mark a stack frame as the currently selected frame and return its index.
  uint32_t SetSelectedFrame(lldb_private::StackFrame *frame);

  /// Get the currently selected frame index.
  /// We should only call SelectMostRelevantFrame if (a) the user hasn't already
  /// selected a frame, and (b) if this really is a user facing
  /// "GetSelectedFrame".  SMRF runs the frame recognizers which can do
  /// arbitrary work that ends up being dangerous to do internally.  Also,
  /// for most internal uses we don't actually want the frame changed by the
  /// SMRF logic.  So unless this is in a command or SB API, you should
  /// pass false here.
  uint32_t
  GetSelectedFrameIndex(SelectMostRelevant select_most_relevant_frame);

  /// Mark a stack frame as the currently selected frame using the frame index
  /// \p idx. Like \ref GetFrameAtIndex, invisible frames cannot be selected.
  bool SetSelectedFrameByIndex(uint32_t idx);

  /// If the current inline depth (i.e the number of invisible frames) is valid,
  /// subtract it from \p idx. Otherwise simply return \p idx.
  uint32_t GetVisibleStackFrameIndex(uint32_t idx) {
    if (m_current_inlined_depth < UINT32_MAX)
      return idx - m_current_inlined_depth;
    else
      return idx;
  }

  /// Calculate and set the current inline depth. This may be used to update
  /// the StackFrameList's set of inline frames when execution stops, e.g when
  /// a breakpoint is hit.
  void CalculateCurrentInlinedDepth();

  /// If the currently selected frame comes from the currently selected thread,
  /// point the default file and line of the thread's target to the location
  /// specified by the frame.
  void SetDefaultFileAndLineToSelectedFrame();

  /// Clear the cache of frames.
  void Clear();

  void Dump(Stream *s);

  /// If \p stack_frame_ptr is contained in this StackFrameList, return its
  /// wrapping shared pointer.
  lldb::StackFrameSP
  GetStackFrameSPForStackFramePtr(StackFrame *stack_frame_ptr);

  size_t GetStatus(Stream &strm, uint32_t first_frame, uint32_t num_frames,
                   bool show_frame_info, uint32_t num_frames_with_source,
                   bool show_unique = false,
                   const char *frame_marker = nullptr);

protected:
  friend class Thread;
  friend class ScriptedThread;

  bool SetFrameAtIndex(uint32_t idx, lldb::StackFrameSP &frame_sp);

  /// Realizes frames up to (and including) end_idx (which can be greater than  
  /// the actual number of frames.)  
  /// Returns true if the function was interrupted, false otherwise.
  bool GetFramesUpTo(uint32_t end_idx, 
      InterruptionControl allow_interrupt = AllowInterruption);

  void GetOnlyConcreteFramesUpTo(uint32_t end_idx, Unwind &unwinder);

  void SynthesizeTailCallFrames(StackFrame &next_frame);

  bool GetAllFramesFetched() { return m_concrete_frames_fetched == UINT32_MAX; }

  void SetAllFramesFetched() { m_concrete_frames_fetched = UINT32_MAX; }

  bool DecrementCurrentInlinedDepth();

  void ResetCurrentInlinedDepth();

  uint32_t GetCurrentInlinedDepth();

  void SetCurrentInlinedDepth(uint32_t new_depth);

  void SelectMostRelevantFrame();

  typedef std::vector<lldb::StackFrameSP> collection;
  typedef collection::iterator iterator;
  typedef collection::const_iterator const_iterator;

  /// The thread this frame list describes.
  Thread &m_thread;

  /// The old stack frame list.
  // TODO: The old stack frame list is used to fill in missing frame info
  // heuristically when it's otherwise unavailable (say, because the unwinder
  // fails). We should have stronger checks to make sure that this is a valid
  // source of information.
  lldb::StackFrameListSP m_prev_frames_sp;

  /// A mutex for this frame list.
  // TODO: This mutex may not always be held when required. In particular, uses
  // of the StackFrameList APIs in lldb_private::Thread look suspect. Consider
  // passing around a lock_guard reference to enforce proper locking.
  mutable std::recursive_mutex m_mutex;

  /// A cache of frames. This may need to be updated when the program counter
  /// changes.
  collection m_frames;

  /// The currently selected frame. An optional is used to record whether anyone
  /// has set the selected frame on this stack yet. We only let recognizers
  /// change the frame if this is the first time GetSelectedFrame is called.
  std::optional<uint32_t> m_selected_frame_idx;

  /// The number of concrete frames fetched while filling the frame list. This
  /// is only used when synthetic frames are enabled.
  uint32_t m_concrete_frames_fetched;

  /// The number of synthetic function activations (invisible frames) expanded
  /// from the concrete frame #0 activation.
  // TODO: Use an optional instead of UINT32_MAX to denote invalid values.
  uint32_t m_current_inlined_depth;

  /// The program counter value at the currently selected synthetic activation.
  /// This is only valid if m_current_inlined_depth is valid.
  // TODO: Use an optional instead of UINT32_MAX to denote invalid values.
  lldb::addr_t m_current_inlined_pc;

  /// Whether or not to show synthetic (inline) frames. Immutable.
  const bool m_show_inlined_frames;

private:
  StackFrameList(const StackFrameList &) = delete;
  const StackFrameList &operator=(const StackFrameList &) = delete;
};

} // namespace lldb_private

#endif // LLDB_TARGET_STACKFRAMELIST_H
