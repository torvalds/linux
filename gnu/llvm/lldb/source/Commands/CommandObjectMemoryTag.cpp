//===-- CommandObjectMemoryTag.cpp ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CommandObjectMemoryTag.h"
#include "lldb/Host/OptionParser.h"
#include "lldb/Interpreter/CommandOptionArgumentTable.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/Interpreter/OptionGroupFormat.h"
#include "lldb/Interpreter/OptionValueString.h"
#include "lldb/Target/ABI.h"
#include "lldb/Target/Process.h"

using namespace lldb;
using namespace lldb_private;

#define LLDB_OPTIONS_memory_tag_read
#include "CommandOptions.inc"

class CommandObjectMemoryTagRead : public CommandObjectParsed {
public:
  CommandObjectMemoryTagRead(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "tag",
                            "Read memory tags for the given range of memory."
                            " Mismatched tags will be marked.",
                            nullptr,
                            eCommandRequiresTarget | eCommandRequiresProcess |
                                eCommandProcessMustBePaused) {
    // Address
    m_arguments.push_back(
        CommandArgumentEntry{CommandArgumentData(eArgTypeAddressOrExpression)});
    // Optional end address
    m_arguments.push_back(CommandArgumentEntry{
        CommandArgumentData(eArgTypeAddressOrExpression, eArgRepeatOptional)});
  }

  ~CommandObjectMemoryTagRead() override = default;

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    if ((command.GetArgumentCount() < 1) || (command.GetArgumentCount() > 2)) {
      result.AppendError(
          "wrong number of arguments; expected at least <address-expression>, "
          "at most <address-expression> <end-address-expression>");
      return;
    }

    Status error;
    addr_t start_addr = OptionArgParser::ToRawAddress(
        &m_exe_ctx, command[0].ref(), LLDB_INVALID_ADDRESS, &error);
    if (start_addr == LLDB_INVALID_ADDRESS) {
      result.AppendErrorWithFormatv("Invalid address expression, {0}",
                                    error.AsCString());
      return;
    }

    // Default 1 byte beyond start, rounds up to at most 1 granule later
    addr_t end_addr = start_addr + 1;

    if (command.GetArgumentCount() > 1) {
      end_addr = OptionArgParser::ToRawAddress(&m_exe_ctx, command[1].ref(),
                                               LLDB_INVALID_ADDRESS, &error);
      if (end_addr == LLDB_INVALID_ADDRESS) {
        result.AppendErrorWithFormatv("Invalid end address expression, {0}",
                                      error.AsCString());
        return;
      }
    }

    Process *process = m_exe_ctx.GetProcessPtr();
    llvm::Expected<const MemoryTagManager *> tag_manager_or_err =
        process->GetMemoryTagManager();

    if (!tag_manager_or_err) {
      result.SetError(Status(tag_manager_or_err.takeError()));
      return;
    }

    const MemoryTagManager *tag_manager = *tag_manager_or_err;

    MemoryRegionInfos memory_regions;
    // If this fails the list of regions is cleared, so we don't need to read
    // the return status here.
    process->GetMemoryRegions(memory_regions);

    lldb::addr_t logical_tag = tag_manager->GetLogicalTag(start_addr);

    // The tag manager only removes tag bits. These addresses may include other
    // non-address bits that must also be ignored.
    ABISP abi = process->GetABI();
    if (abi) {
      start_addr = abi->FixDataAddress(start_addr);
      end_addr = abi->FixDataAddress(end_addr);
    }

    llvm::Expected<MemoryTagManager::TagRange> tagged_range =
        tag_manager->MakeTaggedRange(start_addr, end_addr, memory_regions);

    if (!tagged_range) {
      result.SetError(Status(tagged_range.takeError()));
      return;
    }

    llvm::Expected<std::vector<lldb::addr_t>> tags = process->ReadMemoryTags(
        tagged_range->GetRangeBase(), tagged_range->GetByteSize());

    if (!tags) {
      result.SetError(Status(tags.takeError()));
      return;
    }

    result.AppendMessageWithFormatv("Logical tag: {0:x}", logical_tag);
    result.AppendMessage("Allocation tags:");

    addr_t addr = tagged_range->GetRangeBase();
    for (auto tag : *tags) {
      addr_t next_addr = addr + tag_manager->GetGranuleSize();
      // Showing tagged adresses here until we have non address bit handling
      result.AppendMessageWithFormatv("[{0:x}, {1:x}): {2:x}{3}", addr,
                                      next_addr, tag,
                                      logical_tag == tag ? "" : " (mismatch)");
      addr = next_addr;
    }

    result.SetStatus(eReturnStatusSuccessFinishResult);
  }
};

#define LLDB_OPTIONS_memory_tag_write
#include "CommandOptions.inc"

class CommandObjectMemoryTagWrite : public CommandObjectParsed {
public:
  class OptionGroupTagWrite : public OptionGroup {
  public:
    OptionGroupTagWrite() = default;

    ~OptionGroupTagWrite() override = default;

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_memory_tag_write_options);
    }

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_value,
                          ExecutionContext *execution_context) override {
      Status status;
      const int short_option =
          g_memory_tag_write_options[option_idx].short_option;

      switch (short_option) {
      case 'e':
        m_end_addr = OptionArgParser::ToRawAddress(
            execution_context, option_value, LLDB_INVALID_ADDRESS, &status);
        break;
      default:
        llvm_unreachable("Unimplemented option");
      }

      return status;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_end_addr = LLDB_INVALID_ADDRESS;
    }

    lldb::addr_t m_end_addr = LLDB_INVALID_ADDRESS;
  };

  CommandObjectMemoryTagWrite(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "tag",
                            "Write memory tags starting from the granule that "
                            "contains the given address.",
                            nullptr,
                            eCommandRequiresTarget | eCommandRequiresProcess |
                                eCommandProcessMustBePaused) {
    // Address
    m_arguments.push_back(
        CommandArgumentEntry{CommandArgumentData(eArgTypeAddressOrExpression)});
    // One or more tag values
    m_arguments.push_back(CommandArgumentEntry{
        CommandArgumentData(eArgTypeValue, eArgRepeatPlus)});

    m_option_group.Append(&m_tag_write_options);
    m_option_group.Finalize();
  }

  ~CommandObjectMemoryTagWrite() override = default;

  Options *GetOptions() override { return &m_option_group; }

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    if (command.GetArgumentCount() < 2) {
      result.AppendError("wrong number of arguments; expected "
                         "<address-expression> <tag> [<tag> [...]]");
      return;
    }

    Status error;
    addr_t start_addr = OptionArgParser::ToRawAddress(
        &m_exe_ctx, command[0].ref(), LLDB_INVALID_ADDRESS, &error);
    if (start_addr == LLDB_INVALID_ADDRESS) {
      result.AppendErrorWithFormatv("Invalid address expression, {0}",
                                    error.AsCString());
      return;
    }

    command.Shift(); // shift off start address

    std::vector<lldb::addr_t> tags;
    for (auto &entry : command) {
      lldb::addr_t tag_value;
      // getAsInteger returns true on failure
      if (entry.ref().getAsInteger(0, tag_value)) {
        result.AppendErrorWithFormat(
            "'%s' is not a valid unsigned decimal string value.\n",
            entry.c_str());
        return;
      }
      tags.push_back(tag_value);
    }

    Process *process = m_exe_ctx.GetProcessPtr();
    llvm::Expected<const MemoryTagManager *> tag_manager_or_err =
        process->GetMemoryTagManager();

    if (!tag_manager_or_err) {
      result.SetError(Status(tag_manager_or_err.takeError()));
      return;
    }

    const MemoryTagManager *tag_manager = *tag_manager_or_err;

    MemoryRegionInfos memory_regions;
    // If this fails the list of regions is cleared, so we don't need to read
    // the return status here.
    process->GetMemoryRegions(memory_regions);

    // The tag manager only removes tag bits. These addresses may include other
    // non-address bits that must also be ignored.
    ABISP abi = process->GetABI();
    if (abi)
      start_addr = abi->FixDataAddress(start_addr);

    // We have to assume start_addr is not granule aligned.
    // So if we simply made a range:
    // (start_addr, start_addr + (N * granule_size))
    // We would end up with a range that isn't N granules but N+1
    // granules. To avoid this we'll align the start first using the method that
    // doesn't check memory attributes. (if the final range is untagged we'll
    // handle that error later)
    lldb::addr_t aligned_start_addr =
        tag_manager->ExpandToGranule(MemoryTagManager::TagRange(start_addr, 1))
            .GetRangeBase();

    lldb::addr_t end_addr = 0;
    // When you have an end address you want to align the range like tag read
    // does. Meaning, align the start down (which we've done) and align the end
    // up.
    if (m_tag_write_options.m_end_addr != LLDB_INVALID_ADDRESS)
      end_addr = m_tag_write_options.m_end_addr;
    else
      // Without an end address assume number of tags matches number of granules
      // to write to
      end_addr =
          aligned_start_addr + (tags.size() * tag_manager->GetGranuleSize());

    // Remove non-address bits that aren't memory tags
    if (abi)
      end_addr = abi->FixDataAddress(end_addr);

    // Now we've aligned the start address so if we ask for another range
    // using the number of tags N, we'll get back a range that is also N
    // granules in size.
    llvm::Expected<MemoryTagManager::TagRange> tagged_range =
        tag_manager->MakeTaggedRange(aligned_start_addr, end_addr,
                                     memory_regions);

    if (!tagged_range) {
      result.SetError(Status(tagged_range.takeError()));
      return;
    }

    Status status = process->WriteMemoryTags(tagged_range->GetRangeBase(),
                                             tagged_range->GetByteSize(), tags);

    if (status.Fail()) {
      result.SetError(status);
      return;
    }

    result.SetStatus(eReturnStatusSuccessFinishResult);
  }

  OptionGroupOptions m_option_group;
  OptionGroupTagWrite m_tag_write_options;
};

CommandObjectMemoryTag::CommandObjectMemoryTag(CommandInterpreter &interpreter)
    : CommandObjectMultiword(
          interpreter, "tag", "Commands for manipulating memory tags",
          "memory tag <sub-command> [<sub-command-options>]") {
  CommandObjectSP read_command_object(
      new CommandObjectMemoryTagRead(interpreter));
  read_command_object->SetCommandName("memory tag read");
  LoadSubCommand("read", read_command_object);

  CommandObjectSP write_command_object(
      new CommandObjectMemoryTagWrite(interpreter));
  write_command_object->SetCommandName("memory tag write");
  LoadSubCommand("write", write_command_object);
}

CommandObjectMemoryTag::~CommandObjectMemoryTag() = default;
