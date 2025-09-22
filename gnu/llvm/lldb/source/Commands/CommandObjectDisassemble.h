//===-- CommandObjectDisassemble.h ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_COMMANDS_COMMANDOBJECTDISASSEMBLE_H
#define LLDB_SOURCE_COMMANDS_COMMANDOBJECTDISASSEMBLE_H

#include "lldb/Interpreter/CommandObject.h"
#include "lldb/Interpreter/Options.h"
#include "lldb/Utility/ArchSpec.h"

namespace lldb_private {

// CommandObjectDisassemble

class CommandObjectDisassemble : public CommandObjectParsed {
public:
  class CommandOptions : public Options {
  public:
    CommandOptions();

    ~CommandOptions() override;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override;

    void OptionParsingStarting(ExecutionContext *execution_context) override;

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override;

    const char *GetPluginName() {
      return (plugin_name.empty() ? nullptr : plugin_name.c_str());
    }

    const char *GetFlavorString() {
      if (flavor_string.empty() || flavor_string == "default")
        return nullptr;
      return flavor_string.c_str();
    }

    Status OptionParsingFinished(ExecutionContext *execution_context) override;

    bool show_mixed; // Show mixed source/assembly
    bool show_bytes;
    bool show_control_flow_kind;
    uint32_t num_lines_context = 0;
    uint32_t num_instructions = 0;
    bool raw;
    std::string func_name;
    bool current_function = false;
    lldb::addr_t start_addr = 0;
    lldb::addr_t end_addr = 0;
    bool at_pc = false;
    bool frame_line = false;
    std::string plugin_name;
    std::string flavor_string;
    ArchSpec arch;
    bool some_location_specified = false; // If no location was specified, we'll
                                          // select "at_pc".  This should be set
    // in SetOptionValue if anything the selects a location is set.
    lldb::addr_t symbol_containing_addr = 0;
    bool force = false;
  };

  CommandObjectDisassemble(CommandInterpreter &interpreter);

  ~CommandObjectDisassemble() override;

  Options *GetOptions() override { return &m_options; }

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override;

  llvm::Expected<std::vector<AddressRange>>
  GetRangesForSelectedMode(CommandReturnObject &result);

  llvm::Expected<std::vector<AddressRange>> GetContainingAddressRanges();
  llvm::Expected<std::vector<AddressRange>> GetCurrentFunctionRanges();
  llvm::Expected<std::vector<AddressRange>> GetCurrentLineRanges();
  llvm::Expected<std::vector<AddressRange>>
  GetNameRanges(CommandReturnObject &result);
  llvm::Expected<std::vector<AddressRange>> GetPCRanges();
  llvm::Expected<std::vector<AddressRange>> GetStartEndAddressRanges();

  llvm::Error CheckRangeSize(const AddressRange &range, llvm::StringRef what);

  CommandOptions m_options;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_COMMANDS_COMMANDOBJECTDISASSEMBLE_H
