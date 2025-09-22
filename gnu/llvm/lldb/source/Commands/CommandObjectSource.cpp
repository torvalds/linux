//===-- CommandObjectSource.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CommandObjectSource.h"

#include "lldb/Core/Debugger.h"
#include "lldb/Core/FileLineResolver.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/SourceManager.h"
#include "lldb/Host/OptionParser.h"
#include "lldb/Interpreter/CommandOptionArgumentTable.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/Interpreter/OptionValueFileColonLine.h"
#include "lldb/Interpreter/Options.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Utility/FileSpec.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;

#pragma mark CommandObjectSourceInfo
// CommandObjectSourceInfo - debug line entries dumping command
#define LLDB_OPTIONS_source_info
#include "CommandOptions.inc"

class CommandObjectSourceInfo : public CommandObjectParsed {
  class CommandOptions : public Options {
  public:
    CommandOptions() = default;

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = GetDefinitions()[option_idx].short_option;
      switch (short_option) {
      case 'l':
        if (option_arg.getAsInteger(0, start_line))
          error.SetErrorStringWithFormat("invalid line number: '%s'",
                                         option_arg.str().c_str());
        break;

      case 'e':
        if (option_arg.getAsInteger(0, end_line))
          error.SetErrorStringWithFormat("invalid line number: '%s'",
                                         option_arg.str().c_str());
        break;

      case 'c':
        if (option_arg.getAsInteger(0, num_lines))
          error.SetErrorStringWithFormat("invalid line count: '%s'",
                                         option_arg.str().c_str());
        break;

      case 'f':
        file_name = std::string(option_arg);
        break;

      case 'n':
        symbol_name = std::string(option_arg);
        break;

      case 'a': {
        address = OptionArgParser::ToAddress(execution_context, option_arg,
                                             LLDB_INVALID_ADDRESS, &error);
      } break;
      case 's':
        modules.push_back(std::string(option_arg));
        break;
      default:
        llvm_unreachable("Unimplemented option");
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      file_spec.Clear();
      file_name.clear();
      symbol_name.clear();
      address = LLDB_INVALID_ADDRESS;
      start_line = 0;
      end_line = 0;
      num_lines = 0;
      modules.clear();
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_source_info_options);
    }

    // Instance variables to hold the values for command options.
    FileSpec file_spec;
    std::string file_name;
    std::string symbol_name;
    lldb::addr_t address;
    uint32_t start_line;
    uint32_t end_line;
    uint32_t num_lines;
    std::vector<std::string> modules;
  };

public:
  CommandObjectSourceInfo(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "source info",
            "Display source line information for the current target "
            "process.  Defaults to instruction pointer in current stack "
            "frame.",
            nullptr, eCommandRequiresTarget) {}

  ~CommandObjectSourceInfo() override = default;

  Options *GetOptions() override { return &m_options; }

protected:
  // Dump the line entries in each symbol context. Return the number of entries
  // found. If module_list is set, only dump lines contained in one of the
  // modules. If file_spec is set, only dump lines in the file. If the
  // start_line option was specified, don't print lines less than start_line.
  // If the end_line option was specified, don't print lines greater than
  // end_line. If the num_lines option was specified, dont print more than
  // num_lines entries.
  uint32_t DumpLinesInSymbolContexts(Stream &strm,
                                     const SymbolContextList &sc_list,
                                     const ModuleList &module_list,
                                     const FileSpec &file_spec) {
    uint32_t start_line = m_options.start_line;
    uint32_t end_line = m_options.end_line;
    uint32_t num_lines = m_options.num_lines;
    Target *target = m_exe_ctx.GetTargetPtr();

    uint32_t num_matches = 0;
    // Dump all the line entries for the file in the list.
    ConstString last_module_file_name;
    for (const SymbolContext &sc : sc_list) {
      if (sc.comp_unit) {
        Module *module = sc.module_sp.get();
        CompileUnit *cu = sc.comp_unit;
        const LineEntry &line_entry = sc.line_entry;
        assert(module && cu);

        // Are we looking for specific modules, files or lines?
        if (module_list.GetSize() &&
            module_list.GetIndexForModule(module) == LLDB_INVALID_INDEX32)
          continue;
        if (!FileSpec::Match(file_spec, line_entry.GetFile()))
          continue;
        if (start_line > 0 && line_entry.line < start_line)
          continue;
        if (end_line > 0 && line_entry.line > end_line)
          continue;
        if (num_lines > 0 && num_matches > num_lines)
          continue;

        // Print a new header if the module changed.
        ConstString module_file_name = module->GetFileSpec().GetFilename();
        assert(module_file_name);
        if (module_file_name != last_module_file_name) {
          if (num_matches > 0)
            strm << "\n\n";
          strm << "Lines found in module `" << module_file_name << "\n";
        }
        // Dump the line entry.
        line_entry.GetDescription(&strm, lldb::eDescriptionLevelBrief, cu,
                                  target, /*show_address_only=*/false);
        strm << "\n";
        last_module_file_name = module_file_name;
        num_matches++;
      }
    }
    return num_matches;
  }

  // Dump the requested line entries for the file in the compilation unit.
  // Return the number of entries found. If module_list is set, only dump lines
  // contained in one of the modules. If the start_line option was specified,
  // don't print lines less than start_line. If the end_line option was
  // specified, don't print lines greater than end_line. If the num_lines
  // option was specified, dont print more than num_lines entries.
  uint32_t DumpFileLinesInCompUnit(Stream &strm, Module *module,
                                   CompileUnit *cu, const FileSpec &file_spec) {
    uint32_t start_line = m_options.start_line;
    uint32_t end_line = m_options.end_line;
    uint32_t num_lines = m_options.num_lines;
    Target *target = m_exe_ctx.GetTargetPtr();

    uint32_t num_matches = 0;
    assert(module);
    if (cu) {
      assert(file_spec.GetFilename().AsCString());
      bool has_path = (file_spec.GetDirectory().AsCString() != nullptr);
      const SupportFileList &cu_file_list = cu->GetSupportFiles();
      size_t file_idx = cu_file_list.FindFileIndex(0, file_spec, has_path);
      if (file_idx != UINT32_MAX) {
        // Update the file to how it appears in the CU.
        const FileSpec &cu_file_spec =
            cu_file_list.GetFileSpecAtIndex(file_idx);

        // Dump all matching lines at or above start_line for the file in the
        // CU.
        ConstString file_spec_name = file_spec.GetFilename();
        ConstString module_file_name = module->GetFileSpec().GetFilename();
        bool cu_header_printed = false;
        uint32_t line = start_line;
        while (true) {
          LineEntry line_entry;

          // Find the lowest index of a line entry with a line equal to or
          // higher than 'line'.
          uint32_t start_idx = 0;
          start_idx = cu->FindLineEntry(start_idx, line, &cu_file_spec,
                                        /*exact=*/false, &line_entry);
          if (start_idx == UINT32_MAX)
            // No more line entries for our file in this CU.
            break;

          if (end_line > 0 && line_entry.line > end_line)
            break;

          // Loop through to find any other entries for this line, dumping
          // each.
          line = line_entry.line;
          do {
            num_matches++;
            if (num_lines > 0 && num_matches > num_lines)
              break;
            assert(cu_file_spec == line_entry.GetFile());
            if (!cu_header_printed) {
              if (num_matches > 0)
                strm << "\n\n";
              strm << "Lines found for file " << file_spec_name
                   << " in compilation unit "
                   << cu->GetPrimaryFile().GetFilename() << " in `"
                   << module_file_name << "\n";
              cu_header_printed = true;
            }
            line_entry.GetDescription(&strm, lldb::eDescriptionLevelBrief, cu,
                                      target, /*show_address_only=*/false);
            strm << "\n";

            // Anymore after this one?
            start_idx++;
            start_idx = cu->FindLineEntry(start_idx, line, &cu_file_spec,
                                          /*exact=*/true, &line_entry);
          } while (start_idx != UINT32_MAX);

          // Try the next higher line, starting over at start_idx 0.
          line++;
        }
      }
    }
    return num_matches;
  }

  // Dump the requested line entries for the file in the module. Return the
  // number of entries found. If module_list is set, only dump lines contained
  // in one of the modules. If the start_line option was specified, don't print
  // lines less than start_line. If the end_line option was specified, don't
  // print lines greater than end_line. If the num_lines option was specified,
  // dont print more than num_lines entries.
  uint32_t DumpFileLinesInModule(Stream &strm, Module *module,
                                 const FileSpec &file_spec) {
    uint32_t num_matches = 0;
    if (module) {
      // Look through all the compilation units (CUs) in this module for ones
      // that contain lines of code from this source file.
      for (size_t i = 0; i < module->GetNumCompileUnits(); i++) {
        // Look for a matching source file in this CU.
        CompUnitSP cu_sp(module->GetCompileUnitAtIndex(i));
        if (cu_sp) {
          num_matches +=
              DumpFileLinesInCompUnit(strm, module, cu_sp.get(), file_spec);
        }
      }
    }
    return num_matches;
  }

  // Given an address and a list of modules, append the symbol contexts of all
  // line entries containing the address found in the modules and return the
  // count of matches.  If none is found, return an error in 'error_strm'.
  size_t GetSymbolContextsForAddress(const ModuleList &module_list,
                                     lldb::addr_t addr,
                                     SymbolContextList &sc_list,
                                     StreamString &error_strm) {
    Address so_addr;
    size_t num_matches = 0;
    assert(module_list.GetSize() > 0);
    Target *target = m_exe_ctx.GetTargetPtr();
    if (target->GetSectionLoadList().IsEmpty()) {
      // The target isn't loaded yet, we need to lookup the file address in all
      // modules.  Note: the module list option does not apply to addresses.
      const size_t num_modules = module_list.GetSize();
      for (size_t i = 0; i < num_modules; ++i) {
        ModuleSP module_sp(module_list.GetModuleAtIndex(i));
        if (!module_sp)
          continue;
        if (module_sp->ResolveFileAddress(addr, so_addr)) {
          SymbolContext sc;
          sc.Clear(true);
          if (module_sp->ResolveSymbolContextForAddress(
                  so_addr, eSymbolContextEverything, sc) &
              eSymbolContextLineEntry) {
            sc_list.AppendIfUnique(sc, /*merge_symbol_into_function=*/false);
            ++num_matches;
          }
        }
      }
      if (num_matches == 0)
        error_strm.Printf("Source information for file address 0x%" PRIx64
                          " not found in any modules.\n",
                          addr);
    } else {
      // The target has some things loaded, resolve this address to a compile
      // unit + file + line and display
      if (target->GetSectionLoadList().ResolveLoadAddress(addr, so_addr)) {
        ModuleSP module_sp(so_addr.GetModule());
        // Check to make sure this module is in our list.
        if (module_sp && module_list.GetIndexForModule(module_sp.get()) !=
                             LLDB_INVALID_INDEX32) {
          SymbolContext sc;
          sc.Clear(true);
          if (module_sp->ResolveSymbolContextForAddress(
                  so_addr, eSymbolContextEverything, sc) &
              eSymbolContextLineEntry) {
            sc_list.AppendIfUnique(sc, /*merge_symbol_into_function=*/false);
            ++num_matches;
          } else {
            StreamString addr_strm;
            so_addr.Dump(&addr_strm, nullptr,
                         Address::DumpStyleModuleWithFileAddress);
            error_strm.Printf(
                "Address 0x%" PRIx64 " resolves to %s, but there is"
                " no source information available for this address.\n",
                addr, addr_strm.GetData());
          }
        } else {
          StreamString addr_strm;
          so_addr.Dump(&addr_strm, nullptr,
                       Address::DumpStyleModuleWithFileAddress);
          error_strm.Printf("Address 0x%" PRIx64
                            " resolves to %s, but it cannot"
                            " be found in any modules.\n",
                            addr, addr_strm.GetData());
        }
      } else
        error_strm.Printf("Unable to resolve address 0x%" PRIx64 ".\n", addr);
    }
    return num_matches;
  }

  // Dump the line entries found in functions matching the name specified in
  // the option.
  bool DumpLinesInFunctions(CommandReturnObject &result) {
    SymbolContextList sc_list_funcs;
    ConstString name(m_options.symbol_name.c_str());
    SymbolContextList sc_list_lines;
    Target *target = m_exe_ctx.GetTargetPtr();
    uint32_t addr_byte_size = target->GetArchitecture().GetAddressByteSize();

    ModuleFunctionSearchOptions function_options;
    function_options.include_symbols = false;
    function_options.include_inlines = true;

    // Note: module_list can't be const& because FindFunctionSymbols isn't
    // const.
    ModuleList module_list =
        (m_module_list.GetSize() > 0) ? m_module_list : target->GetImages();
    module_list.FindFunctions(name, eFunctionNameTypeAuto, function_options,
                              sc_list_funcs);
    size_t num_matches = sc_list_funcs.GetSize();

    if (!num_matches) {
      // If we didn't find any functions with that name, try searching for
      // symbols that line up exactly with function addresses.
      SymbolContextList sc_list_symbols;
      module_list.FindFunctionSymbols(name, eFunctionNameTypeAuto,
                                      sc_list_symbols);
      for (const SymbolContext &sc : sc_list_symbols) {
        if (sc.symbol && sc.symbol->ValueIsAddress()) {
          const Address &base_address = sc.symbol->GetAddressRef();
          Function *function = base_address.CalculateSymbolContextFunction();
          if (function) {
            sc_list_funcs.Append(SymbolContext(function));
            num_matches++;
          }
        }
      }
    }
    if (num_matches == 0) {
      result.AppendErrorWithFormat("Could not find function named \'%s\'.\n",
                                   m_options.symbol_name.c_str());
      return false;
    }
    for (const SymbolContext &sc : sc_list_funcs) {
      bool context_found_for_symbol = false;
      // Loop through all the ranges in the function.
      AddressRange range;
      for (uint32_t r = 0;
           sc.GetAddressRange(eSymbolContextEverything, r,
                              /*use_inline_block_range=*/true, range);
           ++r) {
        // Append the symbol contexts for each address in the range to
        // sc_list_lines.
        const Address &base_address = range.GetBaseAddress();
        const addr_t size = range.GetByteSize();
        lldb::addr_t start_addr = base_address.GetLoadAddress(target);
        if (start_addr == LLDB_INVALID_ADDRESS)
          start_addr = base_address.GetFileAddress();
        lldb::addr_t end_addr = start_addr + size;
        for (lldb::addr_t addr = start_addr; addr < end_addr;
             addr += addr_byte_size) {
          StreamString error_strm;
          if (!GetSymbolContextsForAddress(module_list, addr, sc_list_lines,
                                           error_strm))
            result.AppendWarningWithFormat("in symbol '%s': %s",
                                           sc.GetFunctionName().AsCString(),
                                           error_strm.GetData());
          else
            context_found_for_symbol = true;
        }
      }
      if (!context_found_for_symbol)
        result.AppendWarningWithFormat("Unable to find line information"
                                       " for matching symbol '%s'.\n",
                                       sc.GetFunctionName().AsCString());
    }
    if (sc_list_lines.GetSize() == 0) {
      result.AppendErrorWithFormat("No line information could be found"
                                   " for any symbols matching '%s'.\n",
                                   name.AsCString());
      return false;
    }
    FileSpec file_spec;
    if (!DumpLinesInSymbolContexts(result.GetOutputStream(), sc_list_lines,
                                   module_list, file_spec)) {
      result.AppendErrorWithFormat(
          "Unable to dump line information for symbol '%s'.\n",
          name.AsCString());
      return false;
    }
    return true;
  }

  // Dump the line entries found for the address specified in the option.
  bool DumpLinesForAddress(CommandReturnObject &result) {
    Target *target = m_exe_ctx.GetTargetPtr();
    SymbolContextList sc_list;

    StreamString error_strm;
    if (!GetSymbolContextsForAddress(target->GetImages(), m_options.address,
                                     sc_list, error_strm)) {
      result.AppendErrorWithFormat("%s.\n", error_strm.GetData());
      return false;
    }
    ModuleList module_list;
    FileSpec file_spec;
    if (!DumpLinesInSymbolContexts(result.GetOutputStream(), sc_list,
                                   module_list, file_spec)) {
      result.AppendErrorWithFormat("No modules contain load address 0x%" PRIx64
                                   ".\n",
                                   m_options.address);
      return false;
    }
    return true;
  }

  // Dump the line entries found in the file specified in the option.
  bool DumpLinesForFile(CommandReturnObject &result) {
    FileSpec file_spec(m_options.file_name);
    const char *filename = m_options.file_name.c_str();
    Target *target = m_exe_ctx.GetTargetPtr();
    const ModuleList &module_list =
        (m_module_list.GetSize() > 0) ? m_module_list : target->GetImages();

    bool displayed_something = false;
    const size_t num_modules = module_list.GetSize();
    for (uint32_t i = 0; i < num_modules; ++i) {
      // Dump lines for this module.
      Module *module = module_list.GetModulePointerAtIndex(i);
      assert(module);
      if (DumpFileLinesInModule(result.GetOutputStream(), module, file_spec))
        displayed_something = true;
    }
    if (!displayed_something) {
      result.AppendErrorWithFormat("No source filenames matched '%s'.\n",
                                   filename);
      return false;
    }
    return true;
  }

  // Dump the line entries for the current frame.
  bool DumpLinesForFrame(CommandReturnObject &result) {
    StackFrame *cur_frame = m_exe_ctx.GetFramePtr();
    if (cur_frame == nullptr) {
      result.AppendError(
          "No selected frame to use to find the default source.");
      return false;
    } else if (!cur_frame->HasDebugInformation()) {
      result.AppendError("No debug info for the selected frame.");
      return false;
    } else {
      const SymbolContext &sc =
          cur_frame->GetSymbolContext(eSymbolContextLineEntry);
      SymbolContextList sc_list;
      sc_list.Append(sc);
      ModuleList module_list;
      FileSpec file_spec;
      if (!DumpLinesInSymbolContexts(result.GetOutputStream(), sc_list,
                                     module_list, file_spec)) {
        result.AppendError(
            "No source line info available for the selected frame.");
        return false;
      }
    }
    return true;
  }

  void DoExecute(Args &command, CommandReturnObject &result) override {
    Target *target = m_exe_ctx.GetTargetPtr();
    if (target == nullptr) {
      target = GetDebugger().GetSelectedTarget().get();
      if (target == nullptr) {
        result.AppendError("invalid target, create a debug target using the "
                           "'target create' command.");
        return;
      }
    }

    uint32_t addr_byte_size = target->GetArchitecture().GetAddressByteSize();
    result.GetOutputStream().SetAddressByteSize(addr_byte_size);
    result.GetErrorStream().SetAddressByteSize(addr_byte_size);

    // Collect the list of modules to search.
    m_module_list.Clear();
    if (!m_options.modules.empty()) {
      for (size_t i = 0, e = m_options.modules.size(); i < e; ++i) {
        FileSpec module_file_spec(m_options.modules[i]);
        if (module_file_spec) {
          ModuleSpec module_spec(module_file_spec);
          target->GetImages().FindModules(module_spec, m_module_list);
          if (m_module_list.IsEmpty())
            result.AppendWarningWithFormat("No module found for '%s'.\n",
                                           m_options.modules[i].c_str());
        }
      }
      if (!m_module_list.GetSize()) {
        result.AppendError("No modules match the input.");
        return;
      }
    } else if (target->GetImages().GetSize() == 0) {
      result.AppendError("The target has no associated executable images.");
      return;
    }

    // Check the arguments to see what lines we should dump.
    if (!m_options.symbol_name.empty()) {
      // Print lines for symbol.
      if (DumpLinesInFunctions(result))
        result.SetStatus(eReturnStatusSuccessFinishResult);
      else
        result.SetStatus(eReturnStatusFailed);
    } else if (m_options.address != LLDB_INVALID_ADDRESS) {
      // Print lines for an address.
      if (DumpLinesForAddress(result))
        result.SetStatus(eReturnStatusSuccessFinishResult);
      else
        result.SetStatus(eReturnStatusFailed);
    } else if (!m_options.file_name.empty()) {
      // Dump lines for a file.
      if (DumpLinesForFile(result))
        result.SetStatus(eReturnStatusSuccessFinishResult);
      else
        result.SetStatus(eReturnStatusFailed);
    } else {
      // Dump the line for the current frame.
      if (DumpLinesForFrame(result))
        result.SetStatus(eReturnStatusSuccessFinishResult);
      else
        result.SetStatus(eReturnStatusFailed);
    }
  }

  CommandOptions m_options;
  ModuleList m_module_list;
};

#pragma mark CommandObjectSourceList
// CommandObjectSourceList
#define LLDB_OPTIONS_source_list
#include "CommandOptions.inc"

class CommandObjectSourceList : public CommandObjectParsed {
  class CommandOptions : public Options {
  public:
    CommandOptions() = default;

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = GetDefinitions()[option_idx].short_option;
      switch (short_option) {
      case 'l':
        if (option_arg.getAsInteger(0, start_line))
          error.SetErrorStringWithFormat("invalid line number: '%s'",
                                         option_arg.str().c_str());
        break;

      case 'c':
        if (option_arg.getAsInteger(0, num_lines))
          error.SetErrorStringWithFormat("invalid line count: '%s'",
                                         option_arg.str().c_str());
        break;

      case 'f':
        file_name = std::string(option_arg);
        break;

      case 'n':
        symbol_name = std::string(option_arg);
        break;

      case 'a': {
        address = OptionArgParser::ToAddress(execution_context, option_arg,
                                             LLDB_INVALID_ADDRESS, &error);
      } break;
      case 's':
        modules.push_back(std::string(option_arg));
        break;

      case 'b':
        show_bp_locs = true;
        break;
      case 'r':
        reverse = true;
        break;
      case 'y':
      {
        OptionValueFileColonLine value;
        Status fcl_err = value.SetValueFromString(option_arg);
        if (!fcl_err.Success()) {
          error.SetErrorStringWithFormat(
              "Invalid value for file:line specifier: %s",
              fcl_err.AsCString());
        } else {
          file_name = value.GetFileSpec().GetPath();
          start_line = value.GetLineNumber();
          // I don't see anything useful to do with a column number, but I don't
          // want to complain since someone may well have cut and pasted a
          // listing from somewhere that included a column.
        }
      } break;
      default:
        llvm_unreachable("Unimplemented option");
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      file_spec.Clear();
      file_name.clear();
      symbol_name.clear();
      address = LLDB_INVALID_ADDRESS;
      start_line = 0;
      num_lines = 0;
      show_bp_locs = false;
      reverse = false;
      modules.clear();
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_source_list_options);
    }

    // Instance variables to hold the values for command options.
    FileSpec file_spec;
    std::string file_name;
    std::string symbol_name;
    lldb::addr_t address;
    uint32_t start_line;
    uint32_t num_lines;
    std::vector<std::string> modules;
    bool show_bp_locs;
    bool reverse;
  };

public:
  CommandObjectSourceList(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "source list",
                            "Display source code for the current target "
                            "process as specified by options.",
                            nullptr, eCommandRequiresTarget) {}

  ~CommandObjectSourceList() override = default;

  Options *GetOptions() override { return &m_options; }

  std::optional<std::string> GetRepeatCommand(Args &current_command_args,
                                              uint32_t index) override {
    // This is kind of gross, but the command hasn't been parsed yet so we
    // can't look at the option values for this invocation...  I have to scan
    // the arguments directly.
    auto iter =
        llvm::find_if(current_command_args, [](const Args::ArgEntry &e) {
          return e.ref() == "-r" || e.ref() == "--reverse";
        });
    if (iter == current_command_args.end())
      return m_cmd_name;

    if (m_reverse_name.empty()) {
      m_reverse_name = m_cmd_name;
      m_reverse_name.append(" -r");
    }
    return m_reverse_name;
  }

protected:
  struct SourceInfo {
    ConstString function;
    LineEntry line_entry;

    SourceInfo(ConstString name, const LineEntry &line_entry)
        : function(name), line_entry(line_entry) {}

    SourceInfo() = default;

    bool IsValid() const { return (bool)function && line_entry.IsValid(); }

    bool operator==(const SourceInfo &rhs) const {
      return function == rhs.function &&
             line_entry.original_file_sp->Equal(
                 *rhs.line_entry.original_file_sp,
                 SupportFile::eEqualFileSpecAndChecksumIfSet) &&
             line_entry.line == rhs.line_entry.line;
    }

    bool operator!=(const SourceInfo &rhs) const {
      return function != rhs.function ||
             !line_entry.original_file_sp->Equal(
                 *rhs.line_entry.original_file_sp,
                 SupportFile::eEqualFileSpecAndChecksumIfSet) ||
             line_entry.line != rhs.line_entry.line;
    }

    bool operator<(const SourceInfo &rhs) const {
      if (function.GetCString() < rhs.function.GetCString())
        return true;
      if (line_entry.GetFile().GetDirectory().GetCString() <
          rhs.line_entry.GetFile().GetDirectory().GetCString())
        return true;
      if (line_entry.GetFile().GetFilename().GetCString() <
          rhs.line_entry.GetFile().GetFilename().GetCString())
        return true;
      if (line_entry.line < rhs.line_entry.line)
        return true;
      return false;
    }
  };

  size_t DisplayFunctionSource(const SymbolContext &sc, SourceInfo &source_info,
                               CommandReturnObject &result) {
    if (!source_info.IsValid()) {
      source_info.function = sc.GetFunctionName();
      source_info.line_entry = sc.GetFunctionStartLineEntry();
    }

    if (sc.function) {
      Target *target = m_exe_ctx.GetTargetPtr();

      FileSpec start_file;
      uint32_t start_line;
      uint32_t end_line;
      FileSpec end_file;

      if (sc.block == nullptr) {
        // Not an inlined function
        sc.function->GetStartLineSourceInfo(start_file, start_line);
        if (start_line == 0) {
          result.AppendErrorWithFormat("Could not find line information for "
                                       "start of function: \"%s\".\n",
                                       source_info.function.GetCString());
          return 0;
        }
        sc.function->GetEndLineSourceInfo(end_file, end_line);
      } else {
        // We have an inlined function
        start_file = source_info.line_entry.GetFile();
        start_line = source_info.line_entry.line;
        end_line = start_line + m_options.num_lines;
      }

      // This is a little hacky, but the first line table entry for a function
      // points to the "{" that starts the function block.  It would be nice to
      // actually get the function declaration in there too.  So back up a bit,
      // but not further than what you're going to display.
      uint32_t extra_lines;
      if (m_options.num_lines >= 10)
        extra_lines = 5;
      else
        extra_lines = m_options.num_lines / 2;
      uint32_t line_no;
      if (start_line <= extra_lines)
        line_no = 1;
      else
        line_no = start_line - extra_lines;

      // For fun, if the function is shorter than the number of lines we're
      // supposed to display, only display the function...
      if (end_line != 0) {
        if (m_options.num_lines > end_line - line_no)
          m_options.num_lines = end_line - line_no + extra_lines;
      }

      m_breakpoint_locations.Clear();

      if (m_options.show_bp_locs) {
        const bool show_inlines = true;
        m_breakpoint_locations.Reset(start_file, 0, show_inlines);
        SearchFilterForUnconstrainedSearches target_search_filter(
            m_exe_ctx.GetTargetSP());
        target_search_filter.Search(m_breakpoint_locations);
      }

      result.AppendMessageWithFormat("File: %s\n",
                                     start_file.GetPath().c_str());
      // We don't care about the column here.
      const uint32_t column = 0;
      return target->GetSourceManager().DisplaySourceLinesWithLineNumbers(
          start_file, line_no, column, 0, m_options.num_lines, "",
          &result.GetOutputStream(), GetBreakpointLocations());
    } else {
      result.AppendErrorWithFormat(
          "Could not find function info for: \"%s\".\n",
          m_options.symbol_name.c_str());
    }
    return 0;
  }

  // From Jim: The FindMatchingFunctions / FindMatchingFunctionSymbols
  // functions "take a possibly empty vector of strings which are names of
  // modules, and run the two search functions on the subset of the full module
  // list that matches the strings in the input vector". If we wanted to put
  // these somewhere, there should probably be a module-filter-list that can be
  // passed to the various ModuleList::Find* calls, which would either be a
  // vector of string names or a ModuleSpecList.
  void FindMatchingFunctions(Target *target, ConstString name,
                             SymbolContextList &sc_list) {
    // Displaying the source for a symbol:
    if (m_options.num_lines == 0)
      m_options.num_lines = 10;

    ModuleFunctionSearchOptions function_options;
    function_options.include_symbols = true;
    function_options.include_inlines = false;

    const size_t num_modules = m_options.modules.size();
    if (num_modules > 0) {
      ModuleList matching_modules;
      for (size_t i = 0; i < num_modules; ++i) {
        FileSpec module_file_spec(m_options.modules[i]);
        if (module_file_spec) {
          ModuleSpec module_spec(module_file_spec);
          matching_modules.Clear();
          target->GetImages().FindModules(module_spec, matching_modules);

          matching_modules.FindFunctions(name, eFunctionNameTypeAuto,
                                         function_options, sc_list);
        }
      }
    } else {
      target->GetImages().FindFunctions(name, eFunctionNameTypeAuto,
                                        function_options, sc_list);
    }
  }

  void FindMatchingFunctionSymbols(Target *target, ConstString name,
                                   SymbolContextList &sc_list) {
    const size_t num_modules = m_options.modules.size();
    if (num_modules > 0) {
      ModuleList matching_modules;
      for (size_t i = 0; i < num_modules; ++i) {
        FileSpec module_file_spec(m_options.modules[i]);
        if (module_file_spec) {
          ModuleSpec module_spec(module_file_spec);
          matching_modules.Clear();
          target->GetImages().FindModules(module_spec, matching_modules);
          matching_modules.FindFunctionSymbols(name, eFunctionNameTypeAuto,
                                               sc_list);
        }
      }
    } else {
      target->GetImages().FindFunctionSymbols(name, eFunctionNameTypeAuto,
                                              sc_list);
    }
  }

  void DoExecute(Args &command, CommandReturnObject &result) override {
    Target *target = m_exe_ctx.GetTargetPtr();

    if (!m_options.symbol_name.empty()) {
      SymbolContextList sc_list;
      ConstString name(m_options.symbol_name.c_str());

      // Displaying the source for a symbol. Search for function named name.
      FindMatchingFunctions(target, name, sc_list);
      if (sc_list.GetSize() == 0) {
        // If we didn't find any functions with that name, try searching for
        // symbols that line up exactly with function addresses.
        SymbolContextList sc_list_symbols;
        FindMatchingFunctionSymbols(target, name, sc_list_symbols);
        for (const SymbolContext &sc : sc_list_symbols) {
          if (sc.symbol && sc.symbol->ValueIsAddress()) {
            const Address &base_address = sc.symbol->GetAddressRef();
            Function *function = base_address.CalculateSymbolContextFunction();
            if (function) {
              sc_list.Append(SymbolContext(function));
              break;
            }
          }
        }
      }

      if (sc_list.GetSize() == 0) {
        result.AppendErrorWithFormat("Could not find function named: \"%s\".\n",
                                     m_options.symbol_name.c_str());
        return;
      }

      std::set<SourceInfo> source_match_set;
      bool displayed_something = false;
      for (const SymbolContext &sc : sc_list) {
        SourceInfo source_info(sc.GetFunctionName(),
                               sc.GetFunctionStartLineEntry());
        if (source_info.IsValid() &&
            source_match_set.find(source_info) == source_match_set.end()) {
          source_match_set.insert(source_info);
          if (DisplayFunctionSource(sc, source_info, result))
            displayed_something = true;
        }
      }
      if (displayed_something)
        result.SetStatus(eReturnStatusSuccessFinishResult);
      else
        result.SetStatus(eReturnStatusFailed);
      return;
    } else if (m_options.address != LLDB_INVALID_ADDRESS) {
      Address so_addr;
      StreamString error_strm;
      SymbolContextList sc_list;

      if (target->GetSectionLoadList().IsEmpty()) {
        // The target isn't loaded yet, we need to lookup the file address in
        // all modules
        const ModuleList &module_list = target->GetImages();
        const size_t num_modules = module_list.GetSize();
        for (size_t i = 0; i < num_modules; ++i) {
          ModuleSP module_sp(module_list.GetModuleAtIndex(i));
          if (module_sp &&
              module_sp->ResolveFileAddress(m_options.address, so_addr)) {
            SymbolContext sc;
            sc.Clear(true);
            if (module_sp->ResolveSymbolContextForAddress(
                    so_addr, eSymbolContextEverything, sc) &
                eSymbolContextLineEntry)
              sc_list.Append(sc);
          }
        }

        if (sc_list.GetSize() == 0) {
          result.AppendErrorWithFormat(
              "no modules have source information for file address 0x%" PRIx64
              ".\n",
              m_options.address);
          return;
        }
      } else {
        // The target has some things loaded, resolve this address to a compile
        // unit + file + line and display
        if (target->GetSectionLoadList().ResolveLoadAddress(m_options.address,
                                                            so_addr)) {
          ModuleSP module_sp(so_addr.GetModule());
          if (module_sp) {
            SymbolContext sc;
            sc.Clear(true);
            if (module_sp->ResolveSymbolContextForAddress(
                    so_addr, eSymbolContextEverything, sc) &
                eSymbolContextLineEntry) {
              sc_list.Append(sc);
            } else {
              so_addr.Dump(&error_strm, nullptr,
                           Address::DumpStyleModuleWithFileAddress);
              result.AppendErrorWithFormat("address resolves to %s, but there "
                                           "is no line table information "
                                           "available for this address.\n",
                                           error_strm.GetData());
              return;
            }
          }
        }

        if (sc_list.GetSize() == 0) {
          result.AppendErrorWithFormat(
              "no modules contain load address 0x%" PRIx64 ".\n",
              m_options.address);
          return;
        }
      }
      for (const SymbolContext &sc : sc_list) {
        if (sc.comp_unit) {
          if (m_options.show_bp_locs) {
            m_breakpoint_locations.Clear();
            const bool show_inlines = true;
            m_breakpoint_locations.Reset(sc.comp_unit->GetPrimaryFile(), 0,
                                         show_inlines);
            SearchFilterForUnconstrainedSearches target_search_filter(
                target->shared_from_this());
            target_search_filter.Search(m_breakpoint_locations);
          }

          bool show_fullpaths = true;
          bool show_module = true;
          bool show_inlined_frames = true;
          const bool show_function_arguments = true;
          const bool show_function_name = true;
          sc.DumpStopContext(&result.GetOutputStream(),
                             m_exe_ctx.GetBestExecutionContextScope(),
                             sc.line_entry.range.GetBaseAddress(),
                             show_fullpaths, show_module, show_inlined_frames,
                             show_function_arguments, show_function_name);
          result.GetOutputStream().EOL();

          if (m_options.num_lines == 0)
            m_options.num_lines = 10;

          size_t lines_to_back_up =
              m_options.num_lines >= 10 ? 5 : m_options.num_lines / 2;

          const uint32_t column =
              (GetDebugger().GetStopShowColumn() != eStopShowColumnNone)
                  ? sc.line_entry.column
                  : 0;
          target->GetSourceManager().DisplaySourceLinesWithLineNumbers(
              sc.comp_unit->GetPrimaryFile(), sc.line_entry.line, column,
              lines_to_back_up, m_options.num_lines - lines_to_back_up, "->",
              &result.GetOutputStream(), GetBreakpointLocations());
          result.SetStatus(eReturnStatusSuccessFinishResult);
        }
      }
    } else if (m_options.file_name.empty()) {
      // Last valid source manager context, or the current frame if no valid
      // last context in source manager. One little trick here, if you type the
      // exact same list command twice in a row, it is more likely because you
      // typed it once, then typed it again
      if (m_options.start_line == 0) {
        if (target->GetSourceManager().DisplayMoreWithLineNumbers(
                &result.GetOutputStream(), m_options.num_lines,
                m_options.reverse, GetBreakpointLocations())) {
          result.SetStatus(eReturnStatusSuccessFinishResult);
        }
      } else {
        if (m_options.num_lines == 0)
          m_options.num_lines = 10;

        if (m_options.show_bp_locs) {
          SourceManager::FileSP last_file_sp(
              target->GetSourceManager().GetLastFile());
          if (last_file_sp) {
            const bool show_inlines = true;
            m_breakpoint_locations.Reset(last_file_sp->GetFileSpec(), 0,
                                         show_inlines);
            SearchFilterForUnconstrainedSearches target_search_filter(
                target->shared_from_this());
            target_search_filter.Search(m_breakpoint_locations);
          }
        } else
          m_breakpoint_locations.Clear();

        const uint32_t column = 0;
        if (target->GetSourceManager()
                .DisplaySourceLinesWithLineNumbersUsingLastFile(
                    m_options.start_line, // Line to display
                    m_options.num_lines,  // Lines after line to
                    UINT32_MAX,           // Don't mark "line"
                    column,
                    "", // Don't mark "line"
                    &result.GetOutputStream(), GetBreakpointLocations())) {
          result.SetStatus(eReturnStatusSuccessFinishResult);
        }
      }
    } else {
      const char *filename = m_options.file_name.c_str();

      bool check_inlines = false;
      SymbolContextList sc_list;
      size_t num_matches = 0;

      if (!m_options.modules.empty()) {
        ModuleList matching_modules;
        for (size_t i = 0, e = m_options.modules.size(); i < e; ++i) {
          FileSpec module_file_spec(m_options.modules[i]);
          if (module_file_spec) {
            ModuleSpec module_spec(module_file_spec);
            matching_modules.Clear();
            target->GetImages().FindModules(module_spec, matching_modules);
            num_matches += matching_modules.ResolveSymbolContextForFilePath(
                filename, 0, check_inlines,
                SymbolContextItem(eSymbolContextModule |
                                  eSymbolContextCompUnit),
                sc_list);
          }
        }
      } else {
        num_matches = target->GetImages().ResolveSymbolContextForFilePath(
            filename, 0, check_inlines,
            eSymbolContextModule | eSymbolContextCompUnit, sc_list);
      }

      if (num_matches == 0) {
        result.AppendErrorWithFormat("Could not find source file \"%s\".\n",
                                     m_options.file_name.c_str());
        return;
      }

      if (num_matches > 1) {
        bool got_multiple = false;
        CompileUnit *test_cu = nullptr;

        for (const SymbolContext &sc : sc_list) {
          if (sc.comp_unit) {
            if (test_cu) {
              if (test_cu != sc.comp_unit)
                got_multiple = true;
              break;
            } else
              test_cu = sc.comp_unit;
          }
        }
        if (got_multiple) {
          result.AppendErrorWithFormat(
              "Multiple source files found matching: \"%s.\"\n",
              m_options.file_name.c_str());
          return;
        }
      }

      SymbolContext sc;
      if (sc_list.GetContextAtIndex(0, sc)) {
        if (sc.comp_unit) {
          if (m_options.show_bp_locs) {
            const bool show_inlines = true;
            m_breakpoint_locations.Reset(sc.comp_unit->GetPrimaryFile(), 0,
                                         show_inlines);
            SearchFilterForUnconstrainedSearches target_search_filter(
                target->shared_from_this());
            target_search_filter.Search(m_breakpoint_locations);
          } else
            m_breakpoint_locations.Clear();

          if (m_options.num_lines == 0)
            m_options.num_lines = 10;
          const uint32_t column = 0;
          target->GetSourceManager().DisplaySourceLinesWithLineNumbers(
              sc.comp_unit->GetPrimaryFile(), m_options.start_line, column, 0,
              m_options.num_lines, "", &result.GetOutputStream(),
              GetBreakpointLocations());

          result.SetStatus(eReturnStatusSuccessFinishResult);
        } else {
          result.AppendErrorWithFormat("No comp unit found for: \"%s.\"\n",
                                       m_options.file_name.c_str());
        }
      }
    }
  }

  const SymbolContextList *GetBreakpointLocations() {
    if (m_breakpoint_locations.GetFileLineMatches().GetSize() > 0)
      return &m_breakpoint_locations.GetFileLineMatches();
    return nullptr;
  }

  CommandOptions m_options;
  FileLineResolver m_breakpoint_locations;
  std::string m_reverse_name;
};

class CommandObjectSourceCacheDump : public CommandObjectParsed {
public:
  CommandObjectSourceCacheDump(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "source cache dump",
                            "Dump the state of the source code cache. Intended "
                            "to be used for debugging LLDB itself.",
                            nullptr) {}

  ~CommandObjectSourceCacheDump() override = default;

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    // Dump the debugger source cache.
    result.GetOutputStream() << "Debugger Source File Cache\n";
    SourceManager::SourceFileCache &cache = GetDebugger().GetSourceFileCache();
    cache.Dump(result.GetOutputStream());

    // Dump the process source cache.
    if (ProcessSP process_sp = m_exe_ctx.GetProcessSP()) {
      result.GetOutputStream() << "\nProcess Source File Cache\n";
      SourceManager::SourceFileCache &cache = process_sp->GetSourceFileCache();
      cache.Dump(result.GetOutputStream());
    }

    result.SetStatus(eReturnStatusSuccessFinishResult);
  }
};

class CommandObjectSourceCacheClear : public CommandObjectParsed {
public:
  CommandObjectSourceCacheClear(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "source cache clear",
                            "Clear the source code cache.\n", nullptr) {}

  ~CommandObjectSourceCacheClear() override = default;

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    // Clear the debugger cache.
    SourceManager::SourceFileCache &cache = GetDebugger().GetSourceFileCache();
    cache.Clear();

    // Clear the process cache.
    if (ProcessSP process_sp = m_exe_ctx.GetProcessSP())
      process_sp->GetSourceFileCache().Clear();

    result.SetStatus(eReturnStatusSuccessFinishNoResult);
  }
};

class CommandObjectSourceCache : public CommandObjectMultiword {
public:
  CommandObjectSourceCache(CommandInterpreter &interpreter)
      : CommandObjectMultiword(interpreter, "source cache",
                               "Commands for managing the source code cache.",
                               "source cache <sub-command>") {
    LoadSubCommand(
        "dump", CommandObjectSP(new CommandObjectSourceCacheDump(interpreter)));
    LoadSubCommand("clear", CommandObjectSP(new CommandObjectSourceCacheClear(
                                interpreter)));
  }

  ~CommandObjectSourceCache() override = default;

private:
  CommandObjectSourceCache(const CommandObjectSourceCache &) = delete;
  const CommandObjectSourceCache &
  operator=(const CommandObjectSourceCache &) = delete;
};

#pragma mark CommandObjectMultiwordSource
// CommandObjectMultiwordSource

CommandObjectMultiwordSource::CommandObjectMultiwordSource(
    CommandInterpreter &interpreter)
    : CommandObjectMultiword(interpreter, "source",
                             "Commands for examining "
                             "source code described by "
                             "debug information for the "
                             "current target process.",
                             "source <subcommand> [<subcommand-options>]") {
  LoadSubCommand("info",
                 CommandObjectSP(new CommandObjectSourceInfo(interpreter)));
  LoadSubCommand("list",
                 CommandObjectSP(new CommandObjectSourceList(interpreter)));
  LoadSubCommand("cache",
                 CommandObjectSP(new CommandObjectSourceCache(interpreter)));
}

CommandObjectMultiwordSource::~CommandObjectMultiwordSource() = default;
