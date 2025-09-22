//===-- TraceDumper.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/TraceDumper.h"
#include "lldb/Core/Module.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/SectionLoadList.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;
using namespace llvm;

/// \return
///   The given string or \b std::nullopt if it's empty.
static std::optional<const char *> ToOptionalString(const char *s) {
  if (!s)
    return std::nullopt;
  return s;
}

static const char *GetModuleName(const SymbolContext &sc) {
  if (!sc.module_sp)
    return nullptr;
  return sc.module_sp->GetFileSpec().GetFilename().AsCString();
}

/// \return
///   The module name (basename if the module is a file, or the actual name if
///   it's a virtual module), or \b nullptr if no name nor module was found.
static const char *GetModuleName(const TraceDumper::TraceItem &item) {
  if (!item.symbol_info)
    return nullptr;
  return GetModuleName(item.symbol_info->sc);
}

// This custom LineEntry validator is neded because some line_entries have
// 0 as line, which is meaningless. Notice that LineEntry::IsValid only
// checks that line is not LLDB_INVALID_LINE_NUMBER, i.e. UINT32_MAX.
static bool IsLineEntryValid(const LineEntry &line_entry) {
  return line_entry.IsValid() && line_entry.line > 0;
}

/// \return
///     \b true if the provided line entries match line, column and source file.
///     This function assumes that the line entries are valid.
static bool FileLineAndColumnMatches(const LineEntry &a, const LineEntry &b) {
  if (a.line != b.line)
    return false;
  if (a.column != b.column)
    return false;
  return a.GetFile() == b.GetFile();
}

/// Compare the symbol contexts of the provided \a SymbolInfo
/// objects.
///
/// \return
///     \a true if both instructions belong to the same scope level analized
///     in the following order:
///       - module
///       - symbol
///       - function
///       - inlined function
///       - source line info
static bool
IsSameInstructionSymbolContext(const TraceDumper::SymbolInfo &prev_insn,
                               const TraceDumper::SymbolInfo &insn,
                               bool check_source_line_info = true) {
  // module checks
  if (insn.sc.module_sp != prev_insn.sc.module_sp)
    return false;

  // symbol checks
  if (insn.sc.symbol != prev_insn.sc.symbol)
    return false;

  // function checks
  if (!insn.sc.function && !prev_insn.sc.function)
    return true; // This means two dangling instruction in the same module. We
                 // can assume they are part of the same unnamed symbol
  else if (insn.sc.function != prev_insn.sc.function)
    return false;

  Block *inline_block_a =
      insn.sc.block ? insn.sc.block->GetContainingInlinedBlock() : nullptr;
  Block *inline_block_b = prev_insn.sc.block
                              ? prev_insn.sc.block->GetContainingInlinedBlock()
                              : nullptr;
  if (inline_block_a != inline_block_b)
    return false;

  // line entry checks
  if (!check_source_line_info)
    return true;

  const bool curr_line_valid = IsLineEntryValid(insn.sc.line_entry);
  const bool prev_line_valid = IsLineEntryValid(prev_insn.sc.line_entry);
  if (curr_line_valid && prev_line_valid)
    return FileLineAndColumnMatches(insn.sc.line_entry,
                                    prev_insn.sc.line_entry);
  return curr_line_valid == prev_line_valid;
}

class OutputWriterCLI : public TraceDumper::OutputWriter {
public:
  OutputWriterCLI(Stream &s, const TraceDumperOptions &options, Thread &thread)
      : m_s(s), m_options(options) {
    m_s.Format("thread #{0}: tid = {1}\n", thread.GetIndexID(), thread.GetID());
  };

  void NoMoreData() override { m_s << "    no more data\n"; }

  void FunctionCallForest(
      const std::vector<TraceDumper::FunctionCallUP> &forest) override {
    for (size_t i = 0; i < forest.size(); i++) {
      m_s.Format("\n[call tree #{0}]\n", i);
      DumpFunctionCallTree(*forest[i]);
    }
  }

  void TraceItem(const TraceDumper::TraceItem &item) override {
    if (item.symbol_info) {
      if (!item.prev_symbol_info ||
          !IsSameInstructionSymbolContext(*item.prev_symbol_info,
                                          *item.symbol_info)) {
        m_s << "  ";
        const char *module_name = GetModuleName(item);
        if (!module_name)
          m_s << "(none)";
        else if (!item.symbol_info->sc.function && !item.symbol_info->sc.symbol)
          m_s.Format("{0}`(none)", module_name);
        else
          item.symbol_info->sc.DumpStopContext(
              &m_s, item.symbol_info->exe_ctx.GetTargetPtr(),
              item.symbol_info->address,
              /*show_fullpaths=*/false,
              /*show_module=*/true, /*show_inlined_frames=*/false,
              /*show_function_arguments=*/true,
              /*show_function_name=*/true);
        m_s << "\n";
      }
    }

    if (item.error && !m_was_prev_instruction_an_error)
      m_s << "    ...missing instructions\n";

    m_s.Format("    {0}: ", item.id);

    if (m_options.show_timestamps) {
      m_s.Format("[{0}] ", item.timestamp
                               ? formatv("{0:3} ns", *item.timestamp).str()
                               : "unavailable");
    }

    if (item.event) {
      m_s << "(event) " << TraceCursor::EventKindToString(*item.event);
      switch (*item.event) {
      case eTraceEventCPUChanged:
        m_s.Format(" [new CPU={0}]",
                   item.cpu_id ? std::to_string(*item.cpu_id) : "unavailable");
        break;
      case eTraceEventHWClockTick:
        m_s.Format(" [{0}]", item.hw_clock ? std::to_string(*item.hw_clock)
                                           : "unavailable");
        break;
      case eTraceEventDisabledHW:
      case eTraceEventDisabledSW:
        break;
      case eTraceEventSyncPoint:
        m_s.Format(" [{0}]", item.sync_point_metadata);
        break;
      }
    } else if (item.error) {
      m_s << "(error) " << *item.error;
    } else {
      m_s.Format("{0:x+16}", item.load_address);
      if (item.symbol_info && item.symbol_info->instruction) {
        m_s << "    ";
        item.symbol_info->instruction->Dump(
            &m_s, /*max_opcode_byte_size=*/0,
            /*show_address=*/false,
            /*show_bytes=*/false, m_options.show_control_flow_kind,
            &item.symbol_info->exe_ctx, &item.symbol_info->sc,
            /*prev_sym_ctx=*/nullptr,
            /*disassembly_addr_format=*/nullptr,
            /*max_address_text_size=*/0);
      }
    }

    m_was_prev_instruction_an_error = (bool)item.error;
    m_s << "\n";
  }

private:
  void
  DumpSegmentContext(const TraceDumper::FunctionCall::TracedSegment &segment) {
    if (segment.GetOwningCall().IsError()) {
      m_s << "<tracing errors>";
      return;
    }

    const SymbolContext &first_sc = segment.GetFirstInstructionSymbolInfo().sc;
    first_sc.DumpStopContext(
        &m_s, segment.GetFirstInstructionSymbolInfo().exe_ctx.GetTargetPtr(),
        segment.GetFirstInstructionSymbolInfo().address,
        /*show_fullpaths=*/false,
        /*show_module=*/true, /*show_inlined_frames=*/false,
        /*show_function_arguments=*/true,
        /*show_function_name=*/true);
    m_s << " to ";
    const SymbolContext &last_sc = segment.GetLastInstructionSymbolInfo().sc;
    if (IsLineEntryValid(first_sc.line_entry) &&
        IsLineEntryValid(last_sc.line_entry)) {
      m_s.Format("{0}:{1}", last_sc.line_entry.line, last_sc.line_entry.column);
    } else {
      last_sc.DumpStopContext(
          &m_s, segment.GetFirstInstructionSymbolInfo().exe_ctx.GetTargetPtr(),
          segment.GetLastInstructionSymbolInfo().address,
          /*show_fullpaths=*/false,
          /*show_module=*/false, /*show_inlined_frames=*/false,
          /*show_function_arguments=*/false,
          /*show_function_name=*/false);
    }
  }

  void DumpUntracedContext(const TraceDumper::FunctionCall &function_call) {
    if (function_call.IsError()) {
      m_s << "tracing error";
    }
    const SymbolContext &sc = function_call.GetSymbolInfo().sc;

    const char *module_name = GetModuleName(sc);
    if (!module_name)
      m_s << "(none)";
    else if (!sc.function && !sc.symbol)
      m_s << module_name << "`(none)";
    else
      m_s << module_name << "`" << sc.GetFunctionName().AsCString();
  }

  void DumpFunctionCallTree(const TraceDumper::FunctionCall &function_call) {
    if (function_call.GetUntracedPrefixSegment()) {
      m_s.Indent();
      DumpUntracedContext(function_call);
      m_s << "\n";

      m_s.IndentMore();
      DumpFunctionCallTree(function_call.GetUntracedPrefixSegment()->GetNestedCall());
      m_s.IndentLess();
    }

    for (const TraceDumper::FunctionCall::TracedSegment &segment :
         function_call.GetTracedSegments()) {
      m_s.Indent();
      DumpSegmentContext(segment);
      m_s.Format("  [{0}, {1}]\n", segment.GetFirstInstructionID(),
                 segment.GetLastInstructionID());

      segment.IfNestedCall([&](const TraceDumper::FunctionCall &nested_call) {
        m_s.IndentMore();
        DumpFunctionCallTree(nested_call);
        m_s.IndentLess();
      });
    }
  }

  Stream &m_s;
  TraceDumperOptions m_options;
  bool m_was_prev_instruction_an_error = false;
};

class OutputWriterJSON : public TraceDumper::OutputWriter {
  /* schema:
    error_message: string
    | {
      "event": string,
      "id": decimal,
      "tsc"?: string decimal,
      "cpuId"? decimal,
    } | {
      "error": string,
      "id": decimal,
      "tsc"?: string decimal,
    | {
      "loadAddress": string decimal,
      "id": decimal,
      "hwClock"?: string decimal,
      "syncPointMetadata"?: string,
      "timestamp_ns"?: string decimal,
      "module"?: string,
      "symbol"?: string,
      "line"?: decimal,
      "column"?: decimal,
      "source"?: string,
      "mnemonic"?: string,
      "controlFlowKind"?: string,
    }
  */
public:
  OutputWriterJSON(Stream &s, const TraceDumperOptions &options)
      : m_s(s), m_options(options),
        m_j(m_s.AsRawOstream(),
            /*IndentSize=*/options.pretty_print_json ? 2 : 0) {
    m_j.arrayBegin();
  };

  ~OutputWriterJSON() { m_j.arrayEnd(); }

  void FunctionCallForest(
      const std::vector<TraceDumper::FunctionCallUP> &forest) override {
    for (size_t i = 0; i < forest.size(); i++) {
      m_j.object([&] { DumpFunctionCallTree(*forest[i]); });
    }
  }

  void DumpFunctionCallTree(const TraceDumper::FunctionCall &function_call) {
    if (function_call.GetUntracedPrefixSegment()) {
      m_j.attributeObject("untracedPrefixSegment", [&] {
        m_j.attributeObject("nestedCall", [&] {
          DumpFunctionCallTree(
              function_call.GetUntracedPrefixSegment()->GetNestedCall());
        });
      });
    }

    if (!function_call.GetTracedSegments().empty()) {
      m_j.attributeArray("tracedSegments", [&] {
        for (const TraceDumper::FunctionCall::TracedSegment &segment :
             function_call.GetTracedSegments()) {
          m_j.object([&] {
            m_j.attribute("firstInstructionId",
                          std::to_string(segment.GetFirstInstructionID()));
            m_j.attribute("lastInstructionId",
                          std::to_string(segment.GetLastInstructionID()));
            segment.IfNestedCall(
                [&](const TraceDumper::FunctionCall &nested_call) {
                  m_j.attributeObject(
                      "nestedCall", [&] { DumpFunctionCallTree(nested_call); });
                });
          });
        }
      });
    }
  }

  void DumpEvent(const TraceDumper::TraceItem &item) {
    m_j.attribute("event", TraceCursor::EventKindToString(*item.event));
    switch (*item.event) {
    case eTraceEventCPUChanged:
      m_j.attribute("cpuId", item.cpu_id);
      break;
    case eTraceEventHWClockTick:
      m_j.attribute("hwClock", item.hw_clock);
      break;
    case eTraceEventDisabledHW:
    case eTraceEventDisabledSW:
      break;
    case eTraceEventSyncPoint:
      m_j.attribute("syncPointMetadata", item.sync_point_metadata);
      break;
    }
  }

  void DumpInstruction(const TraceDumper::TraceItem &item) {
    m_j.attribute("loadAddress", formatv("{0:x}", item.load_address));
    if (item.symbol_info) {
      m_j.attribute("module", ToOptionalString(GetModuleName(item)));
      m_j.attribute(
          "symbol",
          ToOptionalString(item.symbol_info->sc.GetFunctionName().AsCString()));

      if (lldb::InstructionSP instruction = item.symbol_info->instruction) {
        ExecutionContext exe_ctx = item.symbol_info->exe_ctx;
        m_j.attribute("mnemonic",
                      ToOptionalString(instruction->GetMnemonic(&exe_ctx)));
        if (m_options.show_control_flow_kind) {
          lldb::InstructionControlFlowKind instruction_control_flow_kind =
              instruction->GetControlFlowKind(&exe_ctx);
          m_j.attribute("controlFlowKind",
                        ToOptionalString(
                            Instruction::GetNameForInstructionControlFlowKind(
                                instruction_control_flow_kind)));
        }
      }

      if (IsLineEntryValid(item.symbol_info->sc.line_entry)) {
        m_j.attribute(
            "source",
            ToOptionalString(
                item.symbol_info->sc.line_entry.GetFile().GetPath().c_str()));
        m_j.attribute("line", item.symbol_info->sc.line_entry.line);
        m_j.attribute("column", item.symbol_info->sc.line_entry.column);
      }
    }
  }

  void TraceItem(const TraceDumper::TraceItem &item) override {
    m_j.object([&] {
      m_j.attribute("id", item.id);
      if (m_options.show_timestamps)
        m_j.attribute("timestamp_ns", item.timestamp
                                          ? std::optional<std::string>(
                                                std::to_string(*item.timestamp))
                                          : std::nullopt);

      if (item.event) {
        DumpEvent(item);
      } else if (item.error) {
        m_j.attribute("error", *item.error);
      } else {
        DumpInstruction(item);
      }
    });
  }

private:
  Stream &m_s;
  TraceDumperOptions m_options;
  json::OStream m_j;
};

static std::unique_ptr<TraceDumper::OutputWriter>
CreateWriter(Stream &s, const TraceDumperOptions &options, Thread &thread) {
  if (options.json)
    return std::unique_ptr<TraceDumper::OutputWriter>(
        new OutputWriterJSON(s, options));
  else
    return std::unique_ptr<TraceDumper::OutputWriter>(
        new OutputWriterCLI(s, options, thread));
}

TraceDumper::TraceDumper(lldb::TraceCursorSP cursor_sp, Stream &s,
                         const TraceDumperOptions &options)
    : m_cursor_sp(std::move(cursor_sp)), m_options(options),
      m_writer_up(CreateWriter(
          s, m_options, *m_cursor_sp->GetExecutionContextRef().GetThreadSP())) {

  if (m_options.id)
    m_cursor_sp->GoToId(*m_options.id);
  else if (m_options.forwards)
    m_cursor_sp->Seek(0, lldb::eTraceCursorSeekTypeBeginning);
  else
    m_cursor_sp->Seek(0, lldb::eTraceCursorSeekTypeEnd);

  m_cursor_sp->SetForwards(m_options.forwards);
  if (m_options.skip) {
    m_cursor_sp->Seek((m_options.forwards ? 1 : -1) * *m_options.skip,
                      lldb::eTraceCursorSeekTypeCurrent);
  }
}

TraceDumper::TraceItem TraceDumper::CreatRawTraceItem() {
  TraceItem item = {};
  item.id = m_cursor_sp->GetId();

  if (m_options.show_timestamps)
    item.timestamp = m_cursor_sp->GetWallClockTime();
  return item;
}

/// Find the symbol context for the given address reusing the previous
/// instruction's symbol context when possible.
static SymbolContext
CalculateSymbolContext(const Address &address,
                       const SymbolContext &prev_symbol_context) {
  lldb_private::AddressRange range;
  if (prev_symbol_context.GetAddressRange(eSymbolContextEverything, 0,
                                          /*inline_block_range*/ true, range) &&
      range.Contains(address))
    return prev_symbol_context;

  SymbolContext sc;
  address.CalculateSymbolContext(&sc, eSymbolContextEverything);
  return sc;
}

/// Find the disassembler for the given address reusing the previous
/// instruction's disassembler when possible.
static std::tuple<DisassemblerSP, InstructionSP>
CalculateDisass(const TraceDumper::SymbolInfo &symbol_info,
                const TraceDumper::SymbolInfo &prev_symbol_info,
                const ExecutionContext &exe_ctx) {
  if (prev_symbol_info.disassembler) {
    if (InstructionSP instruction =
            prev_symbol_info.disassembler->GetInstructionList()
                .GetInstructionAtAddress(symbol_info.address))
      return std::make_tuple(prev_symbol_info.disassembler, instruction);
  }

  if (symbol_info.sc.function) {
    if (DisassemblerSP disassembler =
            symbol_info.sc.function->GetInstructions(exe_ctx, nullptr)) {
      if (InstructionSP instruction =
              disassembler->GetInstructionList().GetInstructionAtAddress(
                  symbol_info.address))
        return std::make_tuple(disassembler, instruction);
    }
  }
  // We fallback to a single instruction disassembler
  Target &target = exe_ctx.GetTargetRef();
  const ArchSpec arch = target.GetArchitecture();
  lldb_private::AddressRange range(symbol_info.address,
                                   arch.GetMaximumOpcodeByteSize());
  DisassemblerSP disassembler =
      Disassembler::DisassembleRange(arch, /*plugin_name*/ nullptr,
                                     /*flavor*/ nullptr, target, range);
  return std::make_tuple(
      disassembler,
      disassembler ? disassembler->GetInstructionList().GetInstructionAtAddress(
                         symbol_info.address)
                   : InstructionSP());
}

static TraceDumper::SymbolInfo
CalculateSymbolInfo(const ExecutionContext &exe_ctx, lldb::addr_t load_address,
                    const TraceDumper::SymbolInfo &prev_symbol_info) {
  TraceDumper::SymbolInfo symbol_info;
  symbol_info.exe_ctx = exe_ctx;
  symbol_info.address.SetLoadAddress(load_address, exe_ctx.GetTargetPtr());
  symbol_info.sc =
      CalculateSymbolContext(symbol_info.address, prev_symbol_info.sc);
  std::tie(symbol_info.disassembler, symbol_info.instruction) =
      CalculateDisass(symbol_info, prev_symbol_info, exe_ctx);
  return symbol_info;
}

std::optional<lldb::user_id_t> TraceDumper::DumpInstructions(size_t count) {
  ThreadSP thread_sp = m_cursor_sp->GetExecutionContextRef().GetThreadSP();

  SymbolInfo prev_symbol_info;
  std::optional<lldb::user_id_t> last_id;

  ExecutionContext exe_ctx;
  thread_sp->GetProcess()->GetTarget().CalculateExecutionContext(exe_ctx);

  for (size_t insn_seen = 0; insn_seen < count && m_cursor_sp->HasValue();
       m_cursor_sp->Next()) {

    last_id = m_cursor_sp->GetId();
    TraceItem item = CreatRawTraceItem();

    if (m_cursor_sp->IsEvent() && m_options.show_events) {
      item.event = m_cursor_sp->GetEventType();
      switch (*item.event) {
      case eTraceEventCPUChanged:
        item.cpu_id = m_cursor_sp->GetCPU();
        break;
      case eTraceEventHWClockTick:
        item.hw_clock = m_cursor_sp->GetHWClock();
        break;
      case eTraceEventDisabledHW:
      case eTraceEventDisabledSW:
        break;
      case eTraceEventSyncPoint:
        item.sync_point_metadata = m_cursor_sp->GetSyncPointMetadata();
        break;
      }
      m_writer_up->TraceItem(item);
    } else if (m_cursor_sp->IsError()) {
      item.error = m_cursor_sp->GetError();
      m_writer_up->TraceItem(item);
    } else if (m_cursor_sp->IsInstruction() && !m_options.only_events) {
      insn_seen++;
      item.load_address = m_cursor_sp->GetLoadAddress();

      if (!m_options.raw) {
        SymbolInfo symbol_info =
            CalculateSymbolInfo(exe_ctx, item.load_address, prev_symbol_info);
        item.prev_symbol_info = prev_symbol_info;
        item.symbol_info = symbol_info;
        prev_symbol_info = symbol_info;
      }
      m_writer_up->TraceItem(item);
    }
  }
  if (!m_cursor_sp->HasValue())
    m_writer_up->NoMoreData();
  return last_id;
}

void TraceDumper::FunctionCall::TracedSegment::AppendInsn(
    const TraceCursorSP &cursor_sp,
    const TraceDumper::SymbolInfo &symbol_info) {
  m_last_insn_id = cursor_sp->GetId();
  m_last_symbol_info = symbol_info;
}

lldb::user_id_t
TraceDumper::FunctionCall::TracedSegment::GetFirstInstructionID() const {
  return m_first_insn_id;
}

lldb::user_id_t
TraceDumper::FunctionCall::TracedSegment::GetLastInstructionID() const {
  return m_last_insn_id;
}

void TraceDumper::FunctionCall::TracedSegment::IfNestedCall(
    std::function<void(const FunctionCall &function_call)> callback) const {
  if (m_nested_call)
    callback(*m_nested_call);
}

const TraceDumper::FunctionCall &
TraceDumper::FunctionCall::TracedSegment::GetOwningCall() const {
  return m_owning_call;
}

TraceDumper::FunctionCall &
TraceDumper::FunctionCall::TracedSegment::CreateNestedCall(
    const TraceCursorSP &cursor_sp,
    const TraceDumper::SymbolInfo &symbol_info) {
  m_nested_call = std::make_unique<FunctionCall>(cursor_sp, symbol_info);
  m_nested_call->SetParentCall(m_owning_call);
  return *m_nested_call;
}

const TraceDumper::SymbolInfo &
TraceDumper::FunctionCall::TracedSegment::GetFirstInstructionSymbolInfo()
    const {
  return m_first_symbol_info;
}

const TraceDumper::SymbolInfo &
TraceDumper::FunctionCall::TracedSegment::GetLastInstructionSymbolInfo() const {
  return m_last_symbol_info;
}

const TraceDumper::FunctionCall &
TraceDumper::FunctionCall::UntracedPrefixSegment::GetNestedCall() const {
  return *m_nested_call;
}

TraceDumper::FunctionCall::FunctionCall(
    const TraceCursorSP &cursor_sp,
    const TraceDumper::SymbolInfo &symbol_info) {
  m_is_error = cursor_sp->IsError();
  AppendSegment(cursor_sp, symbol_info);
}

void TraceDumper::FunctionCall::AppendSegment(
    const TraceCursorSP &cursor_sp,
    const TraceDumper::SymbolInfo &symbol_info) {
  m_traced_segments.emplace_back(cursor_sp, symbol_info, *this);
}

const TraceDumper::SymbolInfo &
TraceDumper::FunctionCall::GetSymbolInfo() const {
  return m_traced_segments.back().GetLastInstructionSymbolInfo();
}

bool TraceDumper::FunctionCall::IsError() const { return m_is_error; }

const std::deque<TraceDumper::FunctionCall::TracedSegment> &
TraceDumper::FunctionCall::GetTracedSegments() const {
  return m_traced_segments;
}

TraceDumper::FunctionCall::TracedSegment &
TraceDumper::FunctionCall::GetLastTracedSegment() {
  return m_traced_segments.back();
}

const std::optional<TraceDumper::FunctionCall::UntracedPrefixSegment> &
TraceDumper::FunctionCall::GetUntracedPrefixSegment() const {
  return m_untraced_prefix_segment;
}

void TraceDumper::FunctionCall::SetUntracedPrefixSegment(
    TraceDumper::FunctionCallUP &&nested_call) {
  m_untraced_prefix_segment.emplace(std::move(nested_call));
}

TraceDumper::FunctionCall *TraceDumper::FunctionCall::GetParentCall() const {
  return m_parent_call;
}

void TraceDumper::FunctionCall::SetParentCall(
    TraceDumper::FunctionCall &parent_call) {
  m_parent_call = &parent_call;
}

/// Given an instruction that happens after a return, find the ancestor function
/// call that owns it. If this ancestor doesn't exist, create a new ancestor and
/// make it the root of the tree.
///
/// \param[in] last_function_call
///   The function call that performs the return.
///
/// \param[in] symbol_info
///   The symbol information of the instruction after the return.
///
/// \param[in] cursor_sp
///   The cursor pointing to the instruction after the return.
///
/// \param[in,out] roots
///   The object owning the roots. It might be modified if a new root needs to
///   be created.
///
/// \return
///   A reference to the function call that owns the new instruction
static TraceDumper::FunctionCall &AppendReturnedInstructionToFunctionCallForest(
    TraceDumper::FunctionCall &last_function_call,
    const TraceDumper::SymbolInfo &symbol_info, const TraceCursorSP &cursor_sp,
    std::vector<TraceDumper::FunctionCallUP> &roots) {

  // We omit the current node because we can't return to itself.
  TraceDumper::FunctionCall *ancestor = last_function_call.GetParentCall();

  for (; ancestor; ancestor = ancestor->GetParentCall()) {
    // This loop traverses the tree until it finds a call that we can return to.
    if (IsSameInstructionSymbolContext(ancestor->GetSymbolInfo(), symbol_info,
                                       /*check_source_line_info=*/false)) {
      // We returned to this symbol, so we are assuming we are returning there
      // Note: If this is not robust enough, we should actually check if we
      // returning to the instruction that follows the last instruction from
      // that call, as that's the behavior of CALL instructions.
      ancestor->AppendSegment(cursor_sp, symbol_info);
      return *ancestor;
    }
  }

  // We didn't find the call we were looking for, so we now create a synthetic
  // one that will contain the new instruction in its first traced segment.
  TraceDumper::FunctionCallUP new_root =
      std::make_unique<TraceDumper::FunctionCall>(cursor_sp, symbol_info);
  // This new root will own the previous root through an untraced prefix segment.
  new_root->SetUntracedPrefixSegment(std::move(roots.back()));
  roots.pop_back();
  // We update the roots container to point to the new root
  roots.emplace_back(std::move(new_root));
  return *roots.back();
}

/// Append an instruction to a function call forest. The new instruction might
/// be appended to the current segment, to a new nest call, or return to an
/// ancestor call.
///
/// \param[in] exe_ctx
///   The exeuction context of the traced thread.
///
/// \param[in] last_function_call
///   The chronologically most recent function call before the new instruction.
///
/// \param[in] prev_symbol_info
///   The symbol information of the previous instruction in the trace.
///
/// \param[in] symbol_info
///   The symbol information of the new instruction.
///
/// \param[in] cursor_sp
///   The cursor pointing to the new instruction.
///
/// \param[in,out] roots
///   The object owning the roots. It might be modified if a new root needs to
///   be created.
///
/// \return
///   A reference to the function call that owns the new instruction.
static TraceDumper::FunctionCall &AppendInstructionToFunctionCallForest(
    const ExecutionContext &exe_ctx,
    TraceDumper::FunctionCall *last_function_call,
    const TraceDumper::SymbolInfo &prev_symbol_info,
    const TraceDumper::SymbolInfo &symbol_info, const TraceCursorSP &cursor_sp,
    std::vector<TraceDumper::FunctionCallUP> &roots) {
  if (!last_function_call || last_function_call->IsError()) {
    // We create a brand new root
    roots.emplace_back(
        std::make_unique<TraceDumper::FunctionCall>(cursor_sp, symbol_info));
    return *roots.back();
  }

  lldb_private::AddressRange range;
  if (symbol_info.sc.GetAddressRange(
          eSymbolContextBlock | eSymbolContextFunction | eSymbolContextSymbol,
          0, /*inline_block_range*/ true, range)) {
    if (range.GetBaseAddress() == symbol_info.address) {
      // Our instruction is the first instruction of a function. This has
      // to be a call. This should also identify if a trampoline or the linker
      // is making a call using a non-CALL instruction.
      return last_function_call->GetLastTracedSegment().CreateNestedCall(
          cursor_sp, symbol_info);
    }
  }
  if (IsSameInstructionSymbolContext(prev_symbol_info, symbol_info,
                                     /*check_source_line_info=*/false)) {
    // We are still in the same function. This can't be a call because otherwise
    // we would be in the first instruction of the symbol.
    last_function_call->GetLastTracedSegment().AppendInsn(cursor_sp,
                                                          symbol_info);
    return *last_function_call;
  }
  // Now we are in a different symbol. Let's see if this is a return or a
  // call
  const InstructionSP &insn = last_function_call->GetLastTracedSegment()
                                  .GetLastInstructionSymbolInfo()
                                  .instruction;
  InstructionControlFlowKind insn_kind =
      insn ? insn->GetControlFlowKind(&exe_ctx)
           : eInstructionControlFlowKindOther;

  switch (insn_kind) {
  case lldb::eInstructionControlFlowKindCall:
  case lldb::eInstructionControlFlowKindFarCall: {
    // This is a regular call
    return last_function_call->GetLastTracedSegment().CreateNestedCall(
        cursor_sp, symbol_info);
  }
  case lldb::eInstructionControlFlowKindFarReturn:
  case lldb::eInstructionControlFlowKindReturn: {
    // We should have caught most trampolines and linker functions earlier, so
    // let's assume this is a regular return.
    return AppendReturnedInstructionToFunctionCallForest(
        *last_function_call, symbol_info, cursor_sp, roots);
  }
  default:
    // we changed symbols not using a call or return and we are not in the
    // beginning of a symbol, so this should be something very artificial
    // or maybe a jump to some label in the middle of it section.

    // We first check if it's a return from an inline method
    if (prev_symbol_info.sc.block &&
        prev_symbol_info.sc.block->GetContainingInlinedBlock()) {
      return AppendReturnedInstructionToFunctionCallForest(
          *last_function_call, symbol_info, cursor_sp, roots);
    }
    // Now We assume it's a call. We should revisit this in the future.
    // Ideally we should be able to decide whether to create a new tree,
    // or go deeper or higher in the stack.
    return last_function_call->GetLastTracedSegment().CreateNestedCall(
        cursor_sp, symbol_info);
  }
}

/// Append an error to a function call forest. The new error might be appended
/// to the current segment if it contains errors or will create a new root.
///
/// \param[in] last_function_call
///   The chronologically most recent function call before the new error.
///
/// \param[in] cursor_sp
///   The cursor pointing to the new error.
///
/// \param[in,out] roots
///   The object owning the roots. It might be modified if a new root needs to
///   be created.
///
/// \return
///   A reference to the function call that owns the new error.
TraceDumper::FunctionCall &AppendErrorToFunctionCallForest(
    TraceDumper::FunctionCall *last_function_call, TraceCursorSP &cursor_sp,
    std::vector<TraceDumper::FunctionCallUP> &roots) {
  if (last_function_call && last_function_call->IsError()) {
    last_function_call->GetLastTracedSegment().AppendInsn(
        cursor_sp, TraceDumper::SymbolInfo{});
    return *last_function_call;
  } else {
    roots.emplace_back(std::make_unique<TraceDumper::FunctionCall>(
        cursor_sp, TraceDumper::SymbolInfo{}));
    return *roots.back();
  }
}

static std::vector<TraceDumper::FunctionCallUP>
CreateFunctionCallForest(TraceCursorSP &cursor_sp,
                         const ExecutionContext &exe_ctx) {

  std::vector<TraceDumper::FunctionCallUP> roots;
  TraceDumper::SymbolInfo prev_symbol_info;

  TraceDumper::FunctionCall *last_function_call = nullptr;

  for (; cursor_sp->HasValue(); cursor_sp->Next()) {
    if (cursor_sp->IsError()) {
      last_function_call = &AppendErrorToFunctionCallForest(last_function_call,
                                                            cursor_sp, roots);
      prev_symbol_info = {};
    } else if (cursor_sp->IsInstruction()) {
      TraceDumper::SymbolInfo symbol_info = CalculateSymbolInfo(
          exe_ctx, cursor_sp->GetLoadAddress(), prev_symbol_info);

      last_function_call = &AppendInstructionToFunctionCallForest(
          exe_ctx, last_function_call, prev_symbol_info, symbol_info, cursor_sp,
          roots);
      prev_symbol_info = symbol_info;
    } else if (cursor_sp->GetEventType() == eTraceEventCPUChanged) {
      // TODO: In case of a CPU change, we create a new root because we haven't
      // investigated yet if a call tree can safely continue or if interrupts
      // could have polluted the original call tree.
      last_function_call = nullptr;
      prev_symbol_info = {};
    }
  }

  return roots;
}

void TraceDumper::DumpFunctionCalls() {
  ThreadSP thread_sp = m_cursor_sp->GetExecutionContextRef().GetThreadSP();
  ExecutionContext exe_ctx;
  thread_sp->GetProcess()->GetTarget().CalculateExecutionContext(exe_ctx);

  m_writer_up->FunctionCallForest(
      CreateFunctionCallForest(m_cursor_sp, exe_ctx));
}
