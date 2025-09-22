//===-- CommandObjectRegister.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CommandObjectRegister.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/DumpRegisterInfo.h"
#include "lldb/Core/DumpRegisterValue.h"
#include "lldb/Host/OptionParser.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandOptionArgumentTable.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/OptionGroupFormat.h"
#include "lldb/Interpreter/OptionValueArray.h"
#include "lldb/Interpreter/OptionValueBoolean.h"
#include "lldb/Interpreter/OptionValueUInt64.h"
#include "lldb/Interpreter/Options.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/RegisterValue.h"
#include "llvm/Support/Errno.h"

using namespace lldb;
using namespace lldb_private;

// "register read"
#define LLDB_OPTIONS_register_read
#include "CommandOptions.inc"

class CommandObjectRegisterRead : public CommandObjectParsed {
public:
  CommandObjectRegisterRead(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "register read",
            "Dump the contents of one or more register values from the current "
            "frame.  If no register is specified, dumps them all.",
            nullptr,
            eCommandRequiresFrame | eCommandRequiresRegContext |
                eCommandProcessMustBeLaunched | eCommandProcessMustBePaused),
        m_format_options(eFormatDefault, UINT64_MAX, UINT64_MAX,
                         {{CommandArgumentType::eArgTypeFormat,
                           "Specify a format to be used for display. If this "
                           "is set, register fields will not be displayed."}}) {
    AddSimpleArgumentList(eArgTypeRegisterName, eArgRepeatStar);

    // Add the "--format"
    m_option_group.Append(&m_format_options,
                          OptionGroupFormat::OPTION_GROUP_FORMAT |
                              OptionGroupFormat::OPTION_GROUP_GDB_FMT,
                          LLDB_OPT_SET_ALL);
    m_option_group.Append(&m_command_options);
    m_option_group.Finalize();
  }

  ~CommandObjectRegisterRead() override = default;

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    if (!m_exe_ctx.HasProcessScope())
      return;
    CommandObject::HandleArgumentCompletion(request, opt_element_vector);
  }

  Options *GetOptions() override { return &m_option_group; }

  bool DumpRegister(const ExecutionContext &exe_ctx, Stream &strm,
                    RegisterContext &reg_ctx, const RegisterInfo &reg_info,
                    bool print_flags) {
    RegisterValue reg_value;
    if (!reg_ctx.ReadRegister(&reg_info, reg_value))
      return false;

    strm.Indent();

    bool prefix_with_altname = (bool)m_command_options.alternate_name;
    bool prefix_with_name = !prefix_with_altname;
    DumpRegisterValue(reg_value, strm, reg_info, prefix_with_name,
                      prefix_with_altname, m_format_options.GetFormat(), 8,
                      exe_ctx.GetBestExecutionContextScope(), print_flags,
                      exe_ctx.GetTargetSP());
    if ((reg_info.encoding == eEncodingUint) ||
        (reg_info.encoding == eEncodingSint)) {
      Process *process = exe_ctx.GetProcessPtr();
      if (process && reg_info.byte_size == process->GetAddressByteSize()) {
        addr_t reg_addr = reg_value.GetAsUInt64(LLDB_INVALID_ADDRESS);
        if (reg_addr != LLDB_INVALID_ADDRESS) {
          Address so_reg_addr;
          if (exe_ctx.GetTargetRef().GetSectionLoadList().ResolveLoadAddress(
                  reg_addr, so_reg_addr)) {
            strm.PutCString("  ");
            so_reg_addr.Dump(&strm, exe_ctx.GetBestExecutionContextScope(),
                             Address::DumpStyleResolvedDescription);
          }
        }
      }
    }
    strm.EOL();
    return true;
  }

  bool DumpRegisterSet(const ExecutionContext &exe_ctx, Stream &strm,
                       RegisterContext *reg_ctx, size_t set_idx,
                       bool primitive_only = false) {
    uint32_t unavailable_count = 0;
    uint32_t available_count = 0;

    if (!reg_ctx)
      return false; // thread has no registers (i.e. core files are corrupt,
                    // incomplete crash logs...)

    const RegisterSet *const reg_set = reg_ctx->GetRegisterSet(set_idx);
    if (reg_set) {
      strm.Printf("%s:\n", (reg_set->name ? reg_set->name : "unknown"));
      strm.IndentMore();
      const size_t num_registers = reg_set->num_registers;
      for (size_t reg_idx = 0; reg_idx < num_registers; ++reg_idx) {
        const uint32_t reg = reg_set->registers[reg_idx];
        const RegisterInfo *reg_info = reg_ctx->GetRegisterInfoAtIndex(reg);
        // Skip the dumping of derived register if primitive_only is true.
        if (primitive_only && reg_info && reg_info->value_regs)
          continue;

        if (reg_info && DumpRegister(exe_ctx, strm, *reg_ctx, *reg_info,
                                     /*print_flags=*/false))
          ++available_count;
        else
          ++unavailable_count;
      }
      strm.IndentLess();
      if (unavailable_count) {
        strm.Indent();
        strm.Printf("%u registers were unavailable.\n", unavailable_count);
      }
      strm.EOL();
    }
    return available_count > 0;
  }

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    Stream &strm = result.GetOutputStream();
    RegisterContext *reg_ctx = m_exe_ctx.GetRegisterContext();

    if (command.GetArgumentCount() == 0) {
      size_t set_idx;

      size_t num_register_sets = 1;
      const size_t set_array_size = m_command_options.set_indexes.GetSize();
      if (set_array_size > 0) {
        for (size_t i = 0; i < set_array_size; ++i) {
          set_idx =
              m_command_options.set_indexes[i]->GetValueAs<uint64_t>().value_or(
                  UINT32_MAX);
          if (set_idx < reg_ctx->GetRegisterSetCount()) {
            if (!DumpRegisterSet(m_exe_ctx, strm, reg_ctx, set_idx)) {
              if (errno)
                result.AppendErrorWithFormatv("register read failed: {0}\n",
                                              llvm::sys::StrError());
              else
                result.AppendError("unknown error while reading registers.\n");
              break;
            }
          } else {
            result.AppendErrorWithFormat(
                "invalid register set index: %" PRIu64 "\n", (uint64_t)set_idx);
            break;
          }
        }
      } else {
        if (m_command_options.dump_all_sets)
          num_register_sets = reg_ctx->GetRegisterSetCount();

        for (set_idx = 0; set_idx < num_register_sets; ++set_idx) {
          // When dump_all_sets option is set, dump primitive as well as
          // derived registers.
          DumpRegisterSet(m_exe_ctx, strm, reg_ctx, set_idx,
                          !m_command_options.dump_all_sets.GetCurrentValue());
        }
      }
    } else {
      if (m_command_options.dump_all_sets) {
        result.AppendError("the --all option can't be used when registers "
                           "names are supplied as arguments\n");
      } else if (m_command_options.set_indexes.GetSize() > 0) {
        result.AppendError("the --set <set> option can't be used when "
                           "registers names are supplied as arguments\n");
      } else {
        for (auto &entry : command) {
          // in most LLDB commands we accept $rbx as the name for register RBX
          // - and here we would reject it and non-existant. we should be more
          // consistent towards the user and allow them to say reg read $rbx -
          // internally, however, we should be strict and not allow ourselves
          // to call our registers $rbx in our own API
          auto arg_str = entry.ref();
          arg_str.consume_front("$");

          if (const RegisterInfo *reg_info =
                  reg_ctx->GetRegisterInfoByName(arg_str)) {
            // If they have asked for a specific format don't obscure that by
            // printing flags afterwards.
            bool print_flags =
                !m_format_options.GetFormatValue().OptionWasSet();
            if (!DumpRegister(m_exe_ctx, strm, *reg_ctx, *reg_info,
                              print_flags))
              strm.Printf("%-12s = error: unavailable\n", reg_info->name);
          } else {
            result.AppendErrorWithFormat("Invalid register name '%s'.\n",
                                         arg_str.str().c_str());
          }
        }
      }
    }
  }

  class CommandOptions : public OptionGroup {
  public:
    CommandOptions()
        : set_indexes(OptionValue::ConvertTypeToMask(OptionValue::eTypeUInt64)),
          dump_all_sets(false, false), // Initial and default values are false
          alternate_name(false, false) {}

    ~CommandOptions() override = default;

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_register_read_options);
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      set_indexes.Clear();
      dump_all_sets.Clear();
      alternate_name.Clear();
    }

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_value,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = GetDefinitions()[option_idx].short_option;
      switch (short_option) {
      case 's': {
        OptionValueSP value_sp(OptionValueUInt64::Create(option_value, error));
        if (value_sp)
          set_indexes.AppendValue(value_sp);
      } break;

      case 'a':
        // When we don't use OptionValue::SetValueFromCString(const char *) to
        // set an option value, it won't be marked as being set in the options
        // so we make a call to let users know the value was set via option
        dump_all_sets.SetCurrentValue(true);
        dump_all_sets.SetOptionWasSet();
        break;

      case 'A':
        // When we don't use OptionValue::SetValueFromCString(const char *) to
        // set an option value, it won't be marked as being set in the options
        // so we make a call to let users know the value was set via option
        alternate_name.SetCurrentValue(true);
        dump_all_sets.SetOptionWasSet();
        break;

      default:
        llvm_unreachable("Unimplemented option");
      }
      return error;
    }

    // Instance variables to hold the values for command options.
    OptionValueArray set_indexes;
    OptionValueBoolean dump_all_sets;
    OptionValueBoolean alternate_name;
  };

  OptionGroupOptions m_option_group;
  OptionGroupFormat m_format_options;
  CommandOptions m_command_options;
};

// "register write"
class CommandObjectRegisterWrite : public CommandObjectParsed {
public:
  CommandObjectRegisterWrite(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "register write",
                            "Modify a single register value.", nullptr,
                            eCommandRequiresFrame | eCommandRequiresRegContext |
                                eCommandProcessMustBeLaunched |
                                eCommandProcessMustBePaused) {
    CommandArgumentEntry arg1;
    CommandArgumentEntry arg2;
    CommandArgumentData register_arg;
    CommandArgumentData value_arg;

    // Define the first (and only) variant of this arg.
    register_arg.arg_type = eArgTypeRegisterName;
    register_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg1.push_back(register_arg);

    // Define the first (and only) variant of this arg.
    value_arg.arg_type = eArgTypeValue;
    value_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg2.push_back(value_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg1);
    m_arguments.push_back(arg2);
  }

  ~CommandObjectRegisterWrite() override = default;

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    if (!m_exe_ctx.HasProcessScope() || request.GetCursorIndex() != 0)
      return;

    lldb_private::CommandCompletions::InvokeCommonCompletionCallbacks(
        GetCommandInterpreter(), lldb::eRegisterCompletion, request, nullptr);
  }

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    DataExtractor reg_data;
    RegisterContext *reg_ctx = m_exe_ctx.GetRegisterContext();

    if (command.GetArgumentCount() != 2) {
      result.AppendError(
          "register write takes exactly 2 arguments: <reg-name> <value>");
    } else {
      auto reg_name = command[0].ref();
      auto value_str = command[1].ref();

      // in most LLDB commands we accept $rbx as the name for register RBX -
      // and here we would reject it and non-existant. we should be more
      // consistent towards the user and allow them to say reg write $rbx -
      // internally, however, we should be strict and not allow ourselves to
      // call our registers $rbx in our own API
      reg_name.consume_front("$");

      const RegisterInfo *reg_info = reg_ctx->GetRegisterInfoByName(reg_name);

      if (reg_info) {
        RegisterValue reg_value;

        Status error(reg_value.SetValueFromString(reg_info, value_str));
        if (error.Success()) {
          if (reg_ctx->WriteRegister(reg_info, reg_value)) {
            // Toss all frames and anything else in the thread after a register
            // has been written.
            m_exe_ctx.GetThreadRef().Flush();
            result.SetStatus(eReturnStatusSuccessFinishNoResult);
            return;
          }
        }
        if (error.AsCString()) {
          result.AppendErrorWithFormat(
              "Failed to write register '%s' with value '%s': %s\n",
              reg_name.str().c_str(), value_str.str().c_str(),
              error.AsCString());
        } else {
          result.AppendErrorWithFormat(
              "Failed to write register '%s' with value '%s'",
              reg_name.str().c_str(), value_str.str().c_str());
        }
      } else {
        result.AppendErrorWithFormat("Register not found for '%s'.\n",
                                     reg_name.str().c_str());
      }
    }
  }
};

// "register info"
class CommandObjectRegisterInfo : public CommandObjectParsed {
public:
  CommandObjectRegisterInfo(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "register info",
                            "View information about a register.", nullptr,
                            eCommandRequiresFrame | eCommandRequiresRegContext |
                                eCommandProcessMustBeLaunched |
                                eCommandProcessMustBePaused) {
    SetHelpLong(R"(
Name             The name lldb uses for the register, optionally with an alias.
Size             The size of the register in bytes and again in bits.
Invalidates (*)  The registers that would be changed if you wrote this
                 register. For example, writing to a narrower alias of a wider
                 register would change the value of the wider register.
Read from   (*)  The registers that the value of this register is constructed
                 from. For example, a narrower alias of a wider register will be
                 read from the wider register.
In sets     (*)  The register sets that contain this register. For example the
                 PC will be in the "General Purpose Register" set.
Fields      (*)  A table of the names and bit positions of the values contained
                 in this register.

Fields marked with (*) may not always be present. Some information may be
different for the same register when connected to different debug servers.)");

    AddSimpleArgumentList(eArgTypeRegisterName);
  }

  ~CommandObjectRegisterInfo() override = default;

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    if (!m_exe_ctx.HasProcessScope() || request.GetCursorIndex() != 0)
      return;
    CommandObject::HandleArgumentCompletion(request, opt_element_vector);
  }

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    if (command.GetArgumentCount() != 1) {
      result.AppendError("register info takes exactly 1 argument: <reg-name>");
      return;
    }

    llvm::StringRef reg_name = command[0].ref();
    RegisterContext *reg_ctx = m_exe_ctx.GetRegisterContext();
    const RegisterInfo *reg_info = reg_ctx->GetRegisterInfoByName(reg_name);
    if (reg_info) {
      DumpRegisterInfo(
          result.GetOutputStream(), *reg_ctx, *reg_info,
          GetCommandInterpreter().GetDebugger().GetTerminalWidth());
      result.SetStatus(eReturnStatusSuccessFinishResult);
    } else
      result.AppendErrorWithFormat("No register found with name '%s'.\n",
                                   reg_name.str().c_str());
  }
};

// CommandObjectRegister constructor
CommandObjectRegister::CommandObjectRegister(CommandInterpreter &interpreter)
    : CommandObjectMultiword(interpreter, "register",
                             "Commands to access registers for the current "
                             "thread and stack frame.",
                             "register [read|write|info] ...") {
  LoadSubCommand("read",
                 CommandObjectSP(new CommandObjectRegisterRead(interpreter)));
  LoadSubCommand("write",
                 CommandObjectSP(new CommandObjectRegisterWrite(interpreter)));
  LoadSubCommand("info",
                 CommandObjectSP(new CommandObjectRegisterInfo(interpreter)));
}

CommandObjectRegister::~CommandObjectRegister() = default;
