//===-- TraceCursor.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_TRACE_CURSOR_H
#define LLDB_TARGET_TRACE_CURSOR_H

#include "lldb/lldb-private.h"

#include "lldb/Target/ExecutionContext.h"
#include <optional>

namespace lldb_private {

/// Class used for iterating over the instructions of a thread's trace, among
/// other kinds of information.
///
/// This class attempts to be a generic interface for accessing the instructions
/// of the trace so that each Trace plug-in can reconstruct, represent and store
/// the instruction data in an flexible way that is efficient for the given
/// technology.
///
/// Live processes:
///   In the case of a live process trace, an instance of a \a TraceCursor
///   should point to the trace at the moment it was collected. If the process
///   is later resumed and new trace data is collected, then it's up to each
///   trace plug-in to decide whether to leave the old cursor unaffected or not.
///
/// Cursor items:
///   A \a TraceCursor can point at one of the following items:
///
///   Errors:
///     As there could be errors when reconstructing the instructions of a
///     trace, these errors are represented as failed instructions, and the
///     cursor can point at them.
///
///   Events:
///     The cursor can also point at events in the trace, which aren't errors
///     nor instructions. An example of an event could be a context switch in
///     between two instructions.
///
///   Instruction:
///     An actual instruction with a memory address.
///
/// Defaults:
///   By default, the cursor points at the most recent item in the trace and is
///   set up to iterate backwards. See the \a TraceCursor::Next() method for
///   more documentation.
///
/// Sample usage:
///
///  TraceCursorSP cursor = trace.GetTrace(thread);
///
///  for (; cursor->HasValue(); cursor->Next()) {
///     TraceItemKind kind = cursor->GetItemKind();
///     switch (cursor->GetItemKind()):
///       case eTraceItemKindError:
///         cout << "error found: " << cursor->GetError() << endl;
///         break;
///       case eTraceItemKindEvent:
///         cout << "event found: " << cursor->GetEventTypeAsString() << endl;
///         break;
///       case eTraceItemKindInstruction:
///         std::cout << "instructions found at " << cursor->GetLoadAddress() <<
///         std::endl; break;
///     }
///  }
///
///  As the trace might be empty or the cursor might have reached the end of the
///  trace, you should always invoke \a HasValue() to make sure you don't access
///  invalid memory.
///
/// Random accesses:
///
///   The Trace Cursor offer random acesses in the trace via two APIs:
///
///     TraceCursor::Seek():
///       Unlike the \a TraceCursor::Next() API, which moves instruction by
///       instruction, the \a TraceCursor::Seek() method can be used to
///       reposition the cursor to an offset of the end, beginning, or current
///       position of the trace.
///
///     TraceCursor::GetId() / TraceCursor::SetId(id):
///       Each item (error or instruction) in the trace has a numeric identifier
///       which is defined by the trace plug-in. It's possible to access the id
///       of the current item using GetId(), and to reposition the cursor to a
///       given id using SetId(id).
///
///   You can read more in the documentation of these methods.
class TraceCursor {
public:
  /// Create a cursor that initially points to the end of the trace, i.e. the
  /// most recent item.
  TraceCursor(lldb::ThreadSP thread_sp);

  virtual ~TraceCursor() = default;

  /// Set the direction to use in the \a TraceCursor::Next() method.
  ///
  /// \param[in] forwards
  ///     If \b true, then the traversal will be forwards, otherwise backwards.
  void SetForwards(bool forwards);

  /// Check if the direction to use in the \a TraceCursor::Next() method is
  /// forwards.
  ///
  /// \return
  ///     \b true if the current direction is forwards, \b false if backwards.
  bool IsForwards() const;

  /// Move the cursor to the next item (instruction or error).
  ///
  /// Direction:
  ///     The traversal is done following the current direction of the trace. If
  ///     it is forwards, the instructions are visited forwards
  ///     chronologically. Otherwise, the traversal is done in
  ///     the opposite direction. By default, a cursor moves backwards unless
  ///     changed with \a TraceCursor::SetForwards().
  virtual void Next() = 0;

  /// \return
  ///     \b true if the cursor is pointing to a valid item. \b false if the
  ///     cursor has reached the end of the trace.
  virtual bool HasValue() const = 0;

  /// Instruction identifiers:
  ///
  /// When building complex higher level tools, fast random accesses in the
  /// trace might be needed, for which each instruction requires a unique
  /// identifier within its thread trace. For example, a tool might want to
  /// repeatedly inspect random consecutive portions of a trace. This means that
  /// it will need to first move quickly to the beginning of each section and
  /// then start its iteration. Given that the number of instructions can be in
  /// the order of hundreds of millions, fast random access is necessary.
  ///
  /// An example of such a tool could be an inspector of the call graph of a
  /// trace, where each call is represented with its start and end instructions.
  /// Inspecting all the instructions of a call requires moving to its first
  /// instruction and then iterating until the last instruction, which following
  /// the pattern explained above.
  ///
  /// Instead of using 0-based indices as identifiers, each Trace plug-in can
  /// decide the nature of these identifiers and thus no assumptions can be made
  /// regarding their ordering and sequentiality. The reason is that an
  /// instruction might be encoded by the plug-in in a way that hides its actual
  /// 0-based index in the trace, but it's still possible to efficiently find
  /// it.
  ///
  /// Requirements:
  /// - For a given thread, no two instructions have the same id.
  /// - In terms of efficiency, moving the cursor to a given id should be as
  ///   fast as possible, but not necessarily O(1). That's why the recommended
  ///   way to traverse sequential instructions is to use the \a
  ///   TraceCursor::Next() method and only use \a TraceCursor::GoToId(id)
  ///   sparingly.

  /// Make the cursor point to the item whose identifier is \p id.
  ///
  /// \return
  ///     \b true if the given identifier exists and the cursor effectively
  ///     moved to it. Otherwise, \b false is returned and the cursor now points
  ///     to an invalid item, i.e. calling \a HasValue() will return \b false.
  virtual bool GoToId(lldb::user_id_t id) = 0;

  /// \return
  ///     \b true if and only if there's an instruction item with the given \p
  ///     id.
  virtual bool HasId(lldb::user_id_t id) const = 0;

  /// \return
  ///     A unique identifier for the instruction or error this cursor is
  ///     pointing to.
  virtual lldb::user_id_t GetId() const = 0;
  /// \}

  /// Make the cursor point to an item in the trace based on an origin point and
  /// an offset.
  ///
  /// The resulting position of the trace is
  ///     origin + offset
  ///
  /// If this resulting position would be out of bounds, the trace then points
  /// to an invalid item, i.e. calling \a HasValue() returns \b false.
  ///
  /// \param[in] offset
  ///     How many items to move forwards (if positive) or backwards (if
  ///     negative) from the given origin point. For example, if origin is \b
  ///     End, then a negative offset would move backward in the trace, but a
  ///     positive offset would move past the trace to an invalid item.
  ///
  /// \param[in] origin
  ///     The reference point to use when moving the cursor.
  ///
  /// \return
  ///     \b true if and only if the cursor ends up pointing to a valid item.
  virtual bool Seek(int64_t offset, lldb::TraceCursorSeekType origin) = 0;

  /// \return
  ///   The \a ExecutionContextRef of the backing thread from the creation time
  ///   of this cursor.
  ExecutionContextRef &GetExecutionContextRef();

  /// Trace item information (instructions, errors and events)
  /// \{

  /// \return
  ///     The kind of item the cursor is pointing at.
  virtual lldb::TraceItemKind GetItemKind() const = 0;

  /// \return
  ///     Whether the cursor points to an error or not.
  bool IsError() const;

  /// \return
  ///     The error message the cursor is pointing at.
  virtual llvm::StringRef GetError() const = 0;

  /// \return
  ///     Whether the cursor points to an event or not.
  bool IsEvent() const;

  /// \return
  ///     The specific kind of event the cursor is pointing at.
  virtual lldb::TraceEvent GetEventType() const = 0;

  /// \return
  ///     A human-readable description of the event this cursor is pointing at.
  const char *GetEventTypeAsString() const;

  /// \return
  ///     A human-readable description of the given event.
  static const char *EventKindToString(lldb::TraceEvent event_kind);

  /// \return
  ///     Whether the cursor points to an instruction.
  bool IsInstruction() const;

  /// \return
  ///     The load address of the instruction the cursor is pointing at.
  virtual lldb::addr_t GetLoadAddress() const = 0;

  /// Get the CPU associated with the current trace item.
  ///
  /// This call might not be O(1), so it's suggested to invoke this method
  /// whenever an eTraceEventCPUChanged event is fired.
  ///
  /// \return
  ///    The requested CPU id, or LLDB_INVALID_CPU_ID if this information is
  ///    not available for the current item.
  virtual lldb::cpu_id_t GetCPU() const = 0;

  /// Get the last hardware clock value that was emitted before the current
  /// trace item.
  ///
  /// This call might not be O(1), so it's suggested to invoke this method
  /// whenever an eTraceEventHWClockTick event is fired.
  ///
  /// \return
  ///     The requested HW clock value, or \a std::nullopt if this information
  ///     is not available for the current item.
  virtual std::optional<uint64_t> GetHWClock() const = 0;

  /// Get the approximate wall clock time in nanoseconds at which the current
  /// trace item was executed. Each trace plug-in has a different definition for
  /// what time 0 means.
  ///
  /// \return
  ///     The approximate wall clock time for the trace item, or \a std::nullopt
  ///     if not available.
  virtual std::optional<double> GetWallClockTime() const = 0;

  /// Get some metadata associated with a synchronization point event. As
  /// different trace technologies might have different values for this,
  /// we return a string for flexibility.
  ///
  /// \return
  ///     A string representing some metadata associated with a
  ///     \a eTraceEventSyncPoint event. \b std::nullopt if no metadata is
  ///     available.
  virtual std::optional<std::string> GetSyncPointMetadata() const = 0;
  /// \}

protected:
  ExecutionContextRef m_exe_ctx_ref;
  bool m_forwards = false;
};
} // namespace lldb_private

#endif // LLDB_TARGET_TRACE_CURSOR_H
