//===-- TraceDumper.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Target/TraceCursor.h"
#include <optional>
#include <stack>

#ifndef LLDB_TARGET_TRACE_INSTRUCTION_DUMPER_H
#define LLDB_TARGET_TRACE_INSTRUCTION_DUMPER_H

namespace lldb_private {

/// Class that holds the configuration used by \a TraceDumper for
/// traversing and dumping instructions.
struct TraceDumperOptions {
  /// If \b true, the cursor will be iterated forwards starting from the
  /// oldest instruction. Otherwise, the iteration starts from the most
  /// recent instruction.
  bool forwards = false;
  /// Dump only instruction addresses without disassembly nor symbol
  /// information.
  bool raw = false;
  /// Dump in json format.
  bool json = false;
  /// When dumping in JSON format, pretty print the output.
  bool pretty_print_json = false;
  /// For each trace item, print the corresponding timestamp in nanoseconds if
  /// available.
  bool show_timestamps = false;
  /// Dump the events that happened between instructions.
  bool show_events = false;
  /// Dump events and none of the instructions.
  bool only_events = false;
  /// For each instruction, print the instruction kind.
  bool show_control_flow_kind = false;
  /// Optional custom id to start traversing from.
  std::optional<uint64_t> id;
  /// Optional number of instructions to skip from the starting position
  /// of the cursor.
  std::optional<size_t> skip;
};

/// Class used to dump the instructions of a \a TraceCursor using its current
/// state and granularity.
class TraceDumper {
public:
  /// Helper struct that holds symbol, disassembly and address information of an
  /// instruction.
  struct SymbolInfo {
    SymbolContext sc;
    Address address;
    lldb::DisassemblerSP disassembler;
    lldb::InstructionSP instruction;
    lldb_private::ExecutionContext exe_ctx;
  };

  /// Helper struct that holds all the information we know about a trace item
  struct TraceItem {
    lldb::user_id_t id;
    lldb::addr_t load_address;
    std::optional<double> timestamp;
    std::optional<uint64_t> hw_clock;
    std::optional<std::string> sync_point_metadata;
    std::optional<llvm::StringRef> error;
    std::optional<lldb::TraceEvent> event;
    std::optional<SymbolInfo> symbol_info;
    std::optional<SymbolInfo> prev_symbol_info;
    std::optional<lldb::cpu_id_t> cpu_id;
  };

  /// An object representing a traced function call.
  ///
  /// A function call is represented using segments and subcalls.
  ///
  /// TracedSegment:
  ///   A traced segment is a maximal list of consecutive traced instructions
  ///   that belong to the same function call. A traced segment will end in
  ///   three possible ways:
  ///     - With a call to a function deeper in the callstack. In this case,
  ///     most of the times this nested call will return
  ///       and resume with the next segment of this segment's owning function
  ///       call. More on this later.
  ///     - Abruptly due to end of trace. In this case, we weren't able to trace
  ///     the end of this function call.
  ///     - Simply a return higher in the callstack.
  ///
  ///   In terms of implementation details, as segment can be represented with
  ///   the beginning and ending instruction IDs from the instruction trace.
  ///
  ///  UntracedPrefixSegment:
  ///   It might happen that we didn't trace the beginning of a function and we
  ///   saw it for the first time as part of a return. As a way to signal these
  ///   cases, we have a placeholder UntracedPrefixSegment class that completes the
  ///   callgraph.
  ///
  ///  Example:
  ///   We might have this piece of execution:
  ///
  ///     main() [offset 0x00 to 0x20] [traced instruction ids 1 to 4]
  ///       foo()  [offset 0x00 to 0x80] [traced instruction ids 5 to 20] # main
  ///       invoked foo
  ///     main() [offset 0x24 to 0x40] [traced instruction ids 21 to 30]
  ///
  ///   In this case, our function main invokes foo. We have 3 segments: main
  ///   [offset 0x00 to 0x20], foo() [offset 0x00 to 0x80], and main() [offset
  ///   0x24 to 0x40]. We also have the instruction ids from the corresponding
  ///   linear instruction trace for each segment.
  ///
  ///   But what if we started tracing since the middle of foo? Then we'd have
  ///   an incomplete trace
  ///
  ///       foo() [offset 0x30 to 0x80] [traced instruction ids 1 to 10]
  ///     main() [offset 0x24 to 0x40] [traced instruction ids 11 to 20]
  ///
  ///   Notice that we changed the instruction ids because this is a new trace.
  ///   Here, in order to have a somewhat complete tree with good traversal
  ///   capabilities, we can create an UntracedPrefixSegment to signal the portion of
  ///   main() that we didn't trace. We don't know if this segment was in fact
  ///   multiple segments with many function calls. We'll never know. The
  ///   resulting tree looks like the following:
  ///
  ///     main() [untraced]
  ///       foo() [offset 0x30 to 0x80] [traced instruction ids 1 to 10]
  ///     main() [offset 0x24 to 0x40] [traced instruction ids 11 to 20]
  ///
  ///   And in pseudo-code:
  ///
  ///     FunctionCall [
  ///       UntracedPrefixSegment {
  ///         symbol: main()
  ///         nestedCall: FunctionCall [ # this untraced segment has a nested
  ///         call
  ///           TracedSegment {
  ///             symbol: foo()
  ///             fromInstructionId: 1
  ///             toInstructionId: 10
  ///             nestedCall: none # this doesn't have a nested call
  ///           }
  ///         }
  ///       ],
  ///       TracedSegment {
  ///         symbol: main()
  ///         fromInstructionId: 11
  ///         toInstructionId: 20
  ///         nestedCall: none # this also doesn't have a nested call
  ///       }
  ///   ]
  ///
  ///   We can see the nested structure and how instructions are represented as
  ///   segments.
  ///
  ///
  ///   Returns:
  ///     Code doesn't always behave intuitively. Some interesting functions
  ///     might modify the stack and thus change the behavior of common
  ///     instructions like CALL and RET. We try to identify these cases, and
  ///     the result is that the return edge from a segment might connect with a
  ///     function call very high the stack. For example, you might have
  ///
  ///     main()
  ///       foo()
  ///         bar()
  ///         # here bar modifies the stack and pops foo() from it. Then it
  ///         finished the a RET (return)
  ///     main() # we came back directly to main()
  ///
  ///     I have observed some trampolines doing this, as well as some std
  ///     functions (like ostream functions). So consumers should be aware of
  ///     this.
  ///
  ///     There are all sorts of "abnormal" behaviors you can see in code, and
  ///     whenever we fail at identifying what's going on, we prefer to create a
  ///     new tree.
  ///
  ///   Function call forest:
  ///     A single tree would suffice if a trace didn't contain errors nor
  ///     abnormal behaviors that made our algorithms fail. Sadly these
  ///     anomalies exist and we prefer not to use too many heuristics and
  ///     probably end up lying to the user. So we create a new tree from the
  ///     point we can't continue using the previous tree. This results in
  ///     having a forest instead of a single tree. This is probably the best we
  ///     can do if we consumers want to use this data to perform performance
  ///     analysis or reverse debugging.
  ///
  ///   Non-functions:
  ///     Not everything in a program is a function. There are blocks of
  ///     instructions that are simply labeled or even regions without symbol
  ///     information that we don't what they are. We treat all of them as
  ///     functions for simplicity.
  ///
  ///   Errors:
  ///     Whenever an error is found, a new tree with a single segment is
  ///     created. All consecutive errors after the original one are then
  ///     appended to this segment. As a note, something that GDB does is to use
  ///     some heuristics to merge trees that were interrupted by errors. We are
  ///     leaving that out of scope until a feature like that one is really
  ///     needed.

  /// Forward declaration
  class FunctionCall;
  using FunctionCallUP = std::unique_ptr<FunctionCall>;

  class FunctionCall {
  public:
    class TracedSegment {
    public:
      /// \param[in] cursor_sp
      ///   A cursor pointing to the beginning of the segment.
      ///
      /// \param[in] symbol_info
      ///   The symbol information of the first instruction of the segment.
      ///
      /// \param[in] call
      ///   The FunctionCall object that owns this segment.
      TracedSegment(const lldb::TraceCursorSP &cursor_sp,
                    const SymbolInfo &symbol_info, FunctionCall &owning_call)
          : m_first_insn_id(cursor_sp->GetId()),
            m_last_insn_id(cursor_sp->GetId()),
            m_first_symbol_info(symbol_info), m_last_symbol_info(symbol_info),
            m_owning_call(owning_call) {}

      /// \return
      ///   The chronologically first instruction ID in this segment.
      lldb::user_id_t GetFirstInstructionID() const;
      /// \return
      ///   The chronologically last instruction ID in this segment.
      lldb::user_id_t GetLastInstructionID() const;

      /// \return
      ///   The symbol information of the chronologically first instruction ID
      ///   in this segment.
      const SymbolInfo &GetFirstInstructionSymbolInfo() const;

      /// \return
      ///   The symbol information of the chronologically last instruction ID in
      ///   this segment.
      const SymbolInfo &GetLastInstructionSymbolInfo() const;

      /// \return
      ///   Get the call that owns this segment.
      const FunctionCall &GetOwningCall() const;

      /// Append a new instruction to this segment.
      ///
      /// \param[in] cursor_sp
      ///   A cursor pointing to the new instruction.
      ///
      /// \param[in] symbol_info
      ///   The symbol information of the new instruction.
      void AppendInsn(const lldb::TraceCursorSP &cursor_sp,
                      const SymbolInfo &symbol_info);

      /// Create a nested call at the end of this segment.
      ///
      /// \param[in] cursor_sp
      ///   A cursor pointing to the first instruction of the nested call.
      ///
      /// \param[in] symbol_info
      ///   The symbol information of the first instruction of the nested call.
      FunctionCall &CreateNestedCall(const lldb::TraceCursorSP &cursor_sp,
                                     const SymbolInfo &symbol_info);

      /// Executed the given callback if there's a nested call at the end of
      /// this segment.
      void IfNestedCall(std::function<void(const FunctionCall &function_call)>
                            callback) const;

    private:
      TracedSegment(const TracedSegment &) = delete;
      TracedSegment &operator=(TracedSegment const &);

      /// Delimiting instruction IDs taken chronologically.
      /// \{
      lldb::user_id_t m_first_insn_id;
      lldb::user_id_t m_last_insn_id;
      /// \}
      /// An optional nested call starting at the end of this segment.
      FunctionCallUP m_nested_call;
      /// The symbol information of the delimiting instructions
      /// \{
      SymbolInfo m_first_symbol_info;
      SymbolInfo m_last_symbol_info;
      /// \}
      FunctionCall &m_owning_call;
    };

    class UntracedPrefixSegment {
    public:
      /// Note: Untraced segments can only exist if have also seen a traced
      /// segment of the same function call. Thus, we can use those traced
      /// segments if we want symbol information and such.

      UntracedPrefixSegment(FunctionCallUP &&nested_call)
          : m_nested_call(std::move(nested_call)) {}

      const FunctionCall &GetNestedCall() const;

    private:
      UntracedPrefixSegment(const UntracedPrefixSegment &) = delete;
      UntracedPrefixSegment &operator=(UntracedPrefixSegment const &);
      FunctionCallUP m_nested_call;
    };

    /// Create a new function call given an instruction. This will also create a
    /// segment for that instruction.
    ///
    /// \param[in] cursor_sp
    ///   A cursor pointing to the first instruction of that function call.
    ///
    /// \param[in] symbol_info
    ///   The symbol information of that first instruction.
    FunctionCall(const lldb::TraceCursorSP &cursor_sp,
                 const SymbolInfo &symbol_info);

    /// Append a new traced segment to this function call.
    ///
    /// \param[in] cursor_sp
    ///   A cursor pointing to the first instruction of the new segment.
    ///
    /// \param[in] symbol_info
    ///   The symbol information of that first instruction.
    void AppendSegment(const lldb::TraceCursorSP &cursor_sp,
                       const SymbolInfo &symbol_info);

    /// \return
    ///   The symbol info of some traced instruction of this call.
    const SymbolInfo &GetSymbolInfo() const;

    /// \return
    ///   \b true if and only if the instructions in this function call are
    ///   trace errors, in which case this function call is a fake one.
    bool IsError() const;

    /// \return
    ///   The list of traced segments of this call.
    const std::deque<TracedSegment> &GetTracedSegments() const;

    /// \return
    ///   A non-const reference to the most-recent traced segment.
    TracedSegment &GetLastTracedSegment();

    /// Create an untraced segment for this call that jumps to the provided
    /// nested call.
    void SetUntracedPrefixSegment(FunctionCallUP &&nested_call);

    /// \return
    ///   A optional to the untraced prefix segment of this call.
    const std::optional<UntracedPrefixSegment> &
    GetUntracedPrefixSegment() const;

    /// \return
    ///   A pointer to the parent call. It may be \b nullptr.
    FunctionCall *GetParentCall() const;

    void SetParentCall(FunctionCall &parent_call);

  private:
    /// An optional untraced segment that precedes all the traced segments.
    std::optional<UntracedPrefixSegment> m_untraced_prefix_segment;
    /// The traced segments in order. We used a deque to prevent moving these
    /// objects when appending to the list, which would happen with vector.
    std::deque<TracedSegment> m_traced_segments;
    /// The parent call, which might be null. Useful for reconstructing
    /// callstacks.
    FunctionCall *m_parent_call = nullptr;
    /// Whether this call represents a list of consecutive errors.
    bool m_is_error;
  };

  /// Interface used to abstract away the format in which the instruction
  /// information will be dumped.
  class OutputWriter {
  public:
    virtual ~OutputWriter() = default;

    /// Notify this writer that the cursor ran out of data.
    virtual void NoMoreData() {}

    /// Dump a trace item (instruction, error or event).
    virtual void TraceItem(const TraceItem &item) = 0;

    /// Dump a function call forest.
    virtual void
    FunctionCallForest(const std::vector<FunctionCallUP> &forest) = 0;
  };

  /// Create a instruction dumper for the cursor.
  ///
  /// \param[in] cursor
  ///     The cursor whose instructions will be dumped.
  ///
  /// \param[in] s
  ///     The stream where to dump the instructions to.
  ///
  /// \param[in] options
  ///     Additional options for configuring the dumping.
  TraceDumper(lldb::TraceCursorSP cursor_sp, Stream &s,
              const TraceDumperOptions &options);

  /// Dump \a count instructions of the thread trace starting at the current
  /// cursor position.
  ///
  /// This effectively moves the cursor to the next unvisited position, so that
  /// a subsequent call to this method continues where it left off.
  ///
  /// \param[in] count
  ///     The number of instructions to print.
  ///
  /// \return
  ///     The instruction id of the last traversed instruction, or \b
  ///     std::nullopt if no instructions were visited.
  std::optional<lldb::user_id_t> DumpInstructions(size_t count);

  /// Dump all function calls forwards chronologically and hierarchically
  void DumpFunctionCalls();

private:
  /// Create a trace item for the current position without symbol information.
  TraceItem CreatRawTraceItem();

  lldb::TraceCursorSP m_cursor_sp;
  TraceDumperOptions m_options;
  std::unique_ptr<OutputWriter> m_writer_up;
};

} // namespace lldb_private

#endif // LLDB_TARGET_TRACE_INSTRUCTION_DUMPER_H
