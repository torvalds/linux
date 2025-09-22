//===-- CommandObjectDisassemble.cpp --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CommandObjectDisassemble.h"
#include "lldb/Core/AddressRange.h"
#include "lldb/Core/Disassembler.h"
#include "lldb/Core/Module.h"
#include "lldb/Host/OptionParser.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandOptionArgumentTable.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/Interpreter/Options.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"

static constexpr unsigned default_disasm_byte_size = 32;
static constexpr unsigned default_disasm_num_ins = 4;

using namespace lldb;
using namespace lldb_private;

#define LLDB_OPTIONS_disassemble
#include "CommandOptions.inc"

CommandObjectDisassemble::CommandOptions::CommandOptions() {
  OptionParsingStarting(nullptr);
}

CommandObjectDisassemble::CommandOptions::~CommandOptions() = default;

Status CommandObjectDisassemble::CommandOptions::SetOptionValue(
    uint32_t option_idx, llvm::StringRef option_arg,
    ExecutionContext *execution_context) {
  Status error;

  const int short_option = m_getopt_table[option_idx].val;

  switch (short_option) {
  case 'm':
    show_mixed = true;
    break;

  case 'C':
    if (option_arg.getAsInteger(0, num_lines_context))
      error.SetErrorStringWithFormat("invalid num context lines string: \"%s\"",
                                     option_arg.str().c_str());
    break;

  case 'c':
    if (option_arg.getAsInteger(0, num_instructions))
      error.SetErrorStringWithFormat(
          "invalid num of instructions string: \"%s\"",
          option_arg.str().c_str());
    break;

  case 'b':
    show_bytes = true;
    break;

  case 'k':
    show_control_flow_kind = true;
    break;

  case 's': {
    start_addr = OptionArgParser::ToAddress(execution_context, option_arg,
                                            LLDB_INVALID_ADDRESS, &error);
    if (start_addr != LLDB_INVALID_ADDRESS)
      some_location_specified = true;
  } break;
  case 'e': {
    end_addr = OptionArgParser::ToAddress(execution_context, option_arg,
                                          LLDB_INVALID_ADDRESS, &error);
    if (end_addr != LLDB_INVALID_ADDRESS)
      some_location_specified = true;
  } break;

  case 'n':
    func_name.assign(std::string(option_arg));
    some_location_specified = true;
    break;

  case 'p':
    at_pc = true;
    some_location_specified = true;
    break;

  case 'l':
    frame_line = true;
    // Disassemble the current source line kind of implies showing mixed source
    // code context.
    show_mixed = true;
    some_location_specified = true;
    break;

  case 'P':
    plugin_name.assign(std::string(option_arg));
    break;

  case 'F': {
    TargetSP target_sp =
        execution_context ? execution_context->GetTargetSP() : TargetSP();
    if (target_sp && (target_sp->GetArchitecture().GetTriple().getArch() ==
                          llvm::Triple::x86 ||
                      target_sp->GetArchitecture().GetTriple().getArch() ==
                          llvm::Triple::x86_64)) {
      flavor_string.assign(std::string(option_arg));
    } else
      error.SetErrorStringWithFormat("Disassembler flavors are currently only "
                                     "supported for x86 and x86_64 targets.");
    break;
  }

  case 'r':
    raw = true;
    break;

  case 'f':
    current_function = true;
    some_location_specified = true;
    break;

  case 'A':
    if (execution_context) {
      const auto &target_sp = execution_context->GetTargetSP();
      auto platform_ptr = target_sp ? target_sp->GetPlatform().get() : nullptr;
      arch = Platform::GetAugmentedArchSpec(platform_ptr, option_arg);
    }
    break;

  case 'a': {
    symbol_containing_addr = OptionArgParser::ToAddress(
        execution_context, option_arg, LLDB_INVALID_ADDRESS, &error);
    if (symbol_containing_addr != LLDB_INVALID_ADDRESS) {
      some_location_specified = true;
    }
  } break;

  case '\x01':
    force = true;
    break;

  default:
    llvm_unreachable("Unimplemented option");
  }

  return error;
}

void CommandObjectDisassemble::CommandOptions::OptionParsingStarting(
    ExecutionContext *execution_context) {
  show_mixed = false;
  show_bytes = false;
  show_control_flow_kind = false;
  num_lines_context = 0;
  num_instructions = 0;
  func_name.clear();
  current_function = false;
  at_pc = false;
  frame_line = false;
  start_addr = LLDB_INVALID_ADDRESS;
  end_addr = LLDB_INVALID_ADDRESS;
  symbol_containing_addr = LLDB_INVALID_ADDRESS;
  raw = false;
  plugin_name.clear();

  Target *target =
      execution_context ? execution_context->GetTargetPtr() : nullptr;

  // This is a hack till we get the ability to specify features based on
  // architecture.  For now GetDisassemblyFlavor is really only valid for x86
  // (and for the llvm assembler plugin, but I'm papering over that since that
  // is the only disassembler plugin we have...
  if (target) {
    if (target->GetArchitecture().GetTriple().getArch() == llvm::Triple::x86 ||
        target->GetArchitecture().GetTriple().getArch() ==
            llvm::Triple::x86_64) {
      flavor_string.assign(target->GetDisassemblyFlavor());
    } else
      flavor_string.assign("default");

  } else
    flavor_string.assign("default");

  arch.Clear();
  some_location_specified = false;
  force = false;
}

Status CommandObjectDisassemble::CommandOptions::OptionParsingFinished(
    ExecutionContext *execution_context) {
  if (!some_location_specified)
    current_function = true;
  return Status();
}

llvm::ArrayRef<OptionDefinition>
CommandObjectDisassemble::CommandOptions::GetDefinitions() {
  return llvm::ArrayRef(g_disassemble_options);
}

// CommandObjectDisassemble

CommandObjectDisassemble::CommandObjectDisassemble(
    CommandInterpreter &interpreter)
    : CommandObjectParsed(
          interpreter, "disassemble",
          "Disassemble specified instructions in the current target.  "
          "Defaults to the current function for the current thread and "
          "stack frame.",
          "disassemble [<cmd-options>]", eCommandRequiresTarget) {}

CommandObjectDisassemble::~CommandObjectDisassemble() = default;

llvm::Error CommandObjectDisassemble::CheckRangeSize(const AddressRange &range,
                                                     llvm::StringRef what) {
  if (m_options.num_instructions > 0 || m_options.force ||
      range.GetByteSize() < GetDebugger().GetStopDisassemblyMaxSize())
    return llvm::Error::success();
  StreamString msg;
  msg << "Not disassembling " << what << " because it is very large ";
  range.Dump(&msg, &GetSelectedTarget(), Address::DumpStyleLoadAddress,
             Address::DumpStyleFileAddress);
  msg << ". To disassemble specify an instruction count limit, start/stop "
         "addresses or use the --force option.";
  return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                 msg.GetString());
}

llvm::Expected<std::vector<AddressRange>>
CommandObjectDisassemble::GetContainingAddressRanges() {
  std::vector<AddressRange> ranges;
  const auto &get_range = [&](Address addr) {
    ModuleSP module_sp(addr.GetModule());
    SymbolContext sc;
    bool resolve_tail_call_address = true;
    addr.GetModule()->ResolveSymbolContextForAddress(
        addr, eSymbolContextEverything, sc, resolve_tail_call_address);
    if (sc.function || sc.symbol) {
      AddressRange range;
      sc.GetAddressRange(eSymbolContextFunction | eSymbolContextSymbol, 0,
                         false, range);
      ranges.push_back(range);
    }
  };

  Target &target = GetSelectedTarget();
  if (!target.GetSectionLoadList().IsEmpty()) {
    Address symbol_containing_address;
    if (target.GetSectionLoadList().ResolveLoadAddress(
            m_options.symbol_containing_addr, symbol_containing_address)) {
      get_range(symbol_containing_address);
    }
  } else {
    for (lldb::ModuleSP module_sp : target.GetImages().Modules()) {
      Address file_address;
      if (module_sp->ResolveFileAddress(m_options.symbol_containing_addr,
                                        file_address)) {
        get_range(file_address);
      }
    }
  }

  if (ranges.empty()) {
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "Could not find function bounds for address 0x%" PRIx64,
        m_options.symbol_containing_addr);
  }

  if (llvm::Error err = CheckRangeSize(ranges[0], "the function"))
    return std::move(err);
  return ranges;
}

llvm::Expected<std::vector<AddressRange>>
CommandObjectDisassemble::GetCurrentFunctionRanges() {
  Process *process = m_exe_ctx.GetProcessPtr();
  StackFrame *frame = m_exe_ctx.GetFramePtr();
  if (!frame) {
    if (process) {
      return llvm::createStringError(
          llvm::inconvertibleErrorCode(),
          "Cannot disassemble around the current "
          "function without the process being stopped.\n");
    } else {
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "Cannot disassemble around the current "
                                     "function without a selected frame: "
                                     "no currently running process.\n");
    }
  }
  SymbolContext sc(
      frame->GetSymbolContext(eSymbolContextFunction | eSymbolContextSymbol));
  AddressRange range;
  if (sc.function)
    range = sc.function->GetAddressRange();
  else if (sc.symbol && sc.symbol->ValueIsAddress()) {
    range = {sc.symbol->GetAddress(), sc.symbol->GetByteSize()};
  } else
    range = {frame->GetFrameCodeAddress(), default_disasm_byte_size};

  if (llvm::Error err = CheckRangeSize(range, "the current function"))
    return std::move(err);
  return std::vector<AddressRange>{range};
}

llvm::Expected<std::vector<AddressRange>>
CommandObjectDisassemble::GetCurrentLineRanges() {
  Process *process = m_exe_ctx.GetProcessPtr();
  StackFrame *frame = m_exe_ctx.GetFramePtr();
  if (!frame) {
    if (process) {
      return llvm::createStringError(
          llvm::inconvertibleErrorCode(),
          "Cannot disassemble around the current "
          "function without the process being stopped.\n");
    } else {
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "Cannot disassemble around the current "
                                     "line without a selected frame: "
                                     "no currently running process.\n");
    }
  }

  LineEntry pc_line_entry(
      frame->GetSymbolContext(eSymbolContextLineEntry).line_entry);
  if (pc_line_entry.IsValid())
    return std::vector<AddressRange>{pc_line_entry.range};

  // No line entry, so just disassemble around the current pc
  m_options.show_mixed = false;
  return GetPCRanges();
}

llvm::Expected<std::vector<AddressRange>>
CommandObjectDisassemble::GetNameRanges(CommandReturnObject &result) {
  ConstString name(m_options.func_name.c_str());

  ModuleFunctionSearchOptions function_options;
  function_options.include_symbols = true;
  function_options.include_inlines = true;

  // Find functions matching the given name.
  SymbolContextList sc_list;
  GetSelectedTarget().GetImages().FindFunctions(name, eFunctionNameTypeAuto,
                                                function_options, sc_list);

  std::vector<AddressRange> ranges;
  llvm::Error range_errs = llvm::Error::success();
  AddressRange range;
  const uint32_t scope =
      eSymbolContextBlock | eSymbolContextFunction | eSymbolContextSymbol;
  const bool use_inline_block_range = true;
  for (SymbolContext sc : sc_list.SymbolContexts()) {
    for (uint32_t range_idx = 0;
         sc.GetAddressRange(scope, range_idx, use_inline_block_range, range);
         ++range_idx) {
      if (llvm::Error err = CheckRangeSize(range, "a range"))
        range_errs = joinErrors(std::move(range_errs), std::move(err));
      else
        ranges.push_back(range);
    }
  }
  if (ranges.empty()) {
    if (range_errs)
      return std::move(range_errs);
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "Unable to find symbol with name '%s'.\n",
                                   name.GetCString());
  }
  if (range_errs)
    result.AppendWarning(toString(std::move(range_errs)));
  return ranges;
}

llvm::Expected<std::vector<AddressRange>>
CommandObjectDisassemble::GetPCRanges() {
  Process *process = m_exe_ctx.GetProcessPtr();
  StackFrame *frame = m_exe_ctx.GetFramePtr();
  if (!frame) {
    if (process) {
      return llvm::createStringError(
          llvm::inconvertibleErrorCode(),
          "Cannot disassemble around the current "
          "function without the process being stopped.\n");
    } else {
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "Cannot disassemble around the current "
                                     "PC without a selected frame: "
                                     "no currently running process.\n");
    }
  }

  if (m_options.num_instructions == 0) {
    // Disassembling at the PC always disassembles some number of
    // instructions (not the whole function).
    m_options.num_instructions = default_disasm_num_ins;
  }
  return std::vector<AddressRange>{{frame->GetFrameCodeAddress(), 0}};
}

llvm::Expected<std::vector<AddressRange>>
CommandObjectDisassemble::GetStartEndAddressRanges() {
  addr_t size = 0;
  if (m_options.end_addr != LLDB_INVALID_ADDRESS) {
    if (m_options.end_addr <= m_options.start_addr) {
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "End address before start address.");
    }
    size = m_options.end_addr - m_options.start_addr;
  }
  return std::vector<AddressRange>{{Address(m_options.start_addr), size}};
}

llvm::Expected<std::vector<AddressRange>>
CommandObjectDisassemble::GetRangesForSelectedMode(
    CommandReturnObject &result) {
  if (m_options.symbol_containing_addr != LLDB_INVALID_ADDRESS)
    return CommandObjectDisassemble::GetContainingAddressRanges();
  if (m_options.current_function)
    return CommandObjectDisassemble::GetCurrentFunctionRanges();
  if (m_options.frame_line)
    return CommandObjectDisassemble::GetCurrentLineRanges();
  if (!m_options.func_name.empty())
    return CommandObjectDisassemble::GetNameRanges(result);
  if (m_options.start_addr != LLDB_INVALID_ADDRESS)
    return CommandObjectDisassemble::GetStartEndAddressRanges();
  return CommandObjectDisassemble::GetPCRanges();
}

void CommandObjectDisassemble::DoExecute(Args &command,
                                         CommandReturnObject &result) {
  Target *target = &GetSelectedTarget();

  if (!m_options.arch.IsValid())
    m_options.arch = target->GetArchitecture();

  if (!m_options.arch.IsValid()) {
    result.AppendError(
        "use the --arch option or set the target architecture to disassemble");
    return;
  }

  const char *plugin_name = m_options.GetPluginName();
  const char *flavor_string = m_options.GetFlavorString();

  DisassemblerSP disassembler =
      Disassembler::FindPlugin(m_options.arch, flavor_string, plugin_name);

  if (!disassembler) {
    if (plugin_name) {
      result.AppendErrorWithFormat(
          "Unable to find Disassembler plug-in named '%s' that supports the "
          "'%s' architecture.\n",
          plugin_name, m_options.arch.GetArchitectureName());
    } else
      result.AppendErrorWithFormat(
          "Unable to find Disassembler plug-in for the '%s' architecture.\n",
          m_options.arch.GetArchitectureName());
    return;
  } else if (flavor_string != nullptr && !disassembler->FlavorValidForArchSpec(
                                             m_options.arch, flavor_string))
    result.AppendWarningWithFormat(
        "invalid disassembler flavor \"%s\", using default.\n", flavor_string);

  result.SetStatus(eReturnStatusSuccessFinishResult);

  if (!command.empty()) {
    result.AppendErrorWithFormat(
        "\"disassemble\" arguments are specified as options.\n");
    const int terminal_width =
        GetCommandInterpreter().GetDebugger().GetTerminalWidth();
    GetOptions()->GenerateOptionUsage(result.GetErrorStream(), *this,
                                      terminal_width);
    return;
  }

  if (m_options.show_mixed && m_options.num_lines_context == 0)
    m_options.num_lines_context = 2;

  // Always show the PC in the disassembly
  uint32_t options = Disassembler::eOptionMarkPCAddress;

  // Mark the source line for the current PC only if we are doing mixed source
  // and assembly
  if (m_options.show_mixed)
    options |= Disassembler::eOptionMarkPCSourceLine;

  if (m_options.show_bytes)
    options |= Disassembler::eOptionShowBytes;

  if (m_options.show_control_flow_kind)
    options |= Disassembler::eOptionShowControlFlowKind;

  if (m_options.raw)
    options |= Disassembler::eOptionRawOuput;

  llvm::Expected<std::vector<AddressRange>> ranges =
      GetRangesForSelectedMode(result);
  if (!ranges) {
    result.AppendError(toString(ranges.takeError()));
    return;
  }

  bool print_sc_header = ranges->size() > 1;
  for (AddressRange cur_range : *ranges) {
    Disassembler::Limit limit;
    if (m_options.num_instructions == 0) {
      limit = {Disassembler::Limit::Bytes, cur_range.GetByteSize()};
      if (limit.value == 0)
        limit.value = default_disasm_byte_size;
    } else {
      limit = {Disassembler::Limit::Instructions, m_options.num_instructions};
    }
    if (Disassembler::Disassemble(
            GetDebugger(), m_options.arch, plugin_name, flavor_string,
            m_exe_ctx, cur_range.GetBaseAddress(), limit, m_options.show_mixed,
            m_options.show_mixed ? m_options.num_lines_context : 0, options,
            result.GetOutputStream())) {
      result.SetStatus(eReturnStatusSuccessFinishResult);
    } else {
      if (m_options.symbol_containing_addr != LLDB_INVALID_ADDRESS) {
        result.AppendErrorWithFormat(
            "Failed to disassemble memory in function at 0x%8.8" PRIx64 ".\n",
            m_options.symbol_containing_addr);
      } else {
        result.AppendErrorWithFormat(
            "Failed to disassemble memory at 0x%8.8" PRIx64 ".\n",
            cur_range.GetBaseAddress().GetLoadAddress(target));
      }
    }
    if (print_sc_header)
      result.GetOutputStream() << "\n";
  }
}
