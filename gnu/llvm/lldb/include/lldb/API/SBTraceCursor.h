//===-- SBTraceCursor.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBTRACECURSOR_H
#define LLDB_API_SBTRACECURSOR_H

#include "lldb/API/SBDefines.h"
#include "lldb/API/SBError.h"
#include "lldb/API/SBExecutionContext.h"

namespace lldb {

class LLDB_API SBTraceCursor {
public:
  /// Default constructor for an invalid \a SBTraceCursor object.
  SBTraceCursor();

  /// Set the direction to use in the \a SBTraceCursor::Next() method.
  ///
  /// \param[in] forwards
  ///     If \b true, then the traversal will be forwards, otherwise backwards.
  void SetForwards(bool forwards);

  /// Check if the direction to use in the \a SBTraceCursor::Next() method is
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
  ///     changed with \a SBTraceCursor::SetForwards().
  void Next();

  /// \return
  ///     \b true if the cursor is pointing to a valid item. \b false if the
  ///     cursor has reached the end of the trace.
  bool HasValue() const;

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
  ///   SBTraceCursor::Next() method and only use \a SBTraceCursor::GoToId(id)
  ///   sparingly.

  /// Make the cursor point to the item whose identifier is \p id.
  ///
  /// \return
  ///     \b true if the given identifier exists and the cursor effectively
  ///     moved to it. Otherwise, \b false is returned and the cursor now points
  ///     to an invalid item, i.e. calling \a HasValue() will return \b false.
  bool GoToId(lldb::user_id_t id);

  /// \return
  ///     \b true if and only if there's an instruction item with the given \p
  ///     id.
  bool HasId(lldb::user_id_t id) const;

  /// \return
  ///     A unique identifier for the instruction or error this cursor is
  ///     pointing to.
  lldb::user_id_t GetId() const;
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
  bool Seek(int64_t offset, lldb::TraceCursorSeekType origin);

  /// Trace item information (instructions, errors and events)
  /// \{

  /// \return
  ///     The kind of item the cursor is pointing at.
  lldb::TraceItemKind GetItemKind() const;

  /// \return
  ///     Whether the cursor points to an error or not.
  bool IsError() const;

  /// \return
  ///     The error message the cursor is pointing at.
  const char *GetError() const;

  /// \return
  ///     Whether the cursor points to an event or not.
  bool IsEvent() const;

  /// \return
  ///     The specific kind of event the cursor is pointing at.
  lldb::TraceEvent GetEventType() const;

  /// \return
  ///     A human-readable description of the event this cursor is pointing at.
  const char *GetEventTypeAsString() const;

  /// \return
  ///     Whether the cursor points to an instruction.
  bool IsInstruction() const;

  /// \return
  ///     The load address of the instruction the cursor is pointing at.
  lldb::addr_t GetLoadAddress() const;

  /// \return
  ///    The requested CPU id, or LLDB_INVALID_CPU_ID if this information is
  ///    not available for the current item.
  lldb::cpu_id_t GetCPU() const;

  bool IsValid() const;

  explicit operator bool() const;

protected:
  friend class SBTrace;

  /// Create a cursor that initially points to the end of the trace, i.e. the
  /// most recent item.
  SBTraceCursor(lldb::TraceCursorSP trace_cursor_sp);

  lldb::TraceCursorSP m_opaque_sp;
};
} // namespace lldb

#endif // LLDB_API_SBTRACECURSOR_H
