//===-- CommandObjectMemory.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CommandObjectMemory.h"
#include "CommandObjectMemoryTag.h"
#include "lldb/Core/DumpDataExtractor.h"
#include "lldb/Core/Section.h"
#include "lldb/Core/ValueObjectMemory.h"
#include "lldb/Expression/ExpressionVariable.h"
#include "lldb/Host/OptionParser.h"
#include "lldb/Interpreter/CommandOptionArgumentTable.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/Interpreter/OptionGroupFormat.h"
#include "lldb/Interpreter/OptionGroupMemoryTag.h"
#include "lldb/Interpreter/OptionGroupOutputFile.h"
#include "lldb/Interpreter/OptionGroupValueObjectDisplay.h"
#include "lldb/Interpreter/OptionValueLanguage.h"
#include "lldb/Interpreter/OptionValueString.h"
#include "lldb/Interpreter/Options.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/TypeList.h"
#include "lldb/Target/ABI.h"
#include "lldb/Target/Language.h"
#include "lldb/Target/MemoryHistory.h"
#include "lldb/Target/MemoryRegionInfo.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/StreamString.h"
#include "llvm/Support/MathExtras.h"
#include <cinttypes>
#include <memory>
#include <optional>

using namespace lldb;
using namespace lldb_private;

#define LLDB_OPTIONS_memory_read
#include "CommandOptions.inc"

class OptionGroupReadMemory : public OptionGroup {
public:
  OptionGroupReadMemory()
      : m_num_per_line(1, 1), m_offset(0, 0),
        m_language_for_type(eLanguageTypeUnknown) {}

  ~OptionGroupReadMemory() override = default;

  llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
    return llvm::ArrayRef(g_memory_read_options);
  }

  Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_value,
                        ExecutionContext *execution_context) override {
    Status error;
    const int short_option = g_memory_read_options[option_idx].short_option;

    switch (short_option) {
    case 'l':
      error = m_num_per_line.SetValueFromString(option_value);
      if (m_num_per_line.GetCurrentValue() == 0)
        error.SetErrorStringWithFormat(
            "invalid value for --num-per-line option '%s'",
            option_value.str().c_str());
      break;

    case 'b':
      m_output_as_binary = true;
      break;

    case 't':
      error = m_view_as_type.SetValueFromString(option_value);
      break;

    case 'r':
      m_force = true;
      break;

    case 'x':
      error = m_language_for_type.SetValueFromString(option_value);
      break;

    case 'E':
      error = m_offset.SetValueFromString(option_value);
      break;

    default:
      llvm_unreachable("Unimplemented option");
    }
    return error;
  }

  void OptionParsingStarting(ExecutionContext *execution_context) override {
    m_num_per_line.Clear();
    m_output_as_binary = false;
    m_view_as_type.Clear();
    m_force = false;
    m_offset.Clear();
    m_language_for_type.Clear();
  }

  Status FinalizeSettings(Target *target, OptionGroupFormat &format_options) {
    Status error;
    OptionValueUInt64 &byte_size_value = format_options.GetByteSizeValue();
    OptionValueUInt64 &count_value = format_options.GetCountValue();
    const bool byte_size_option_set = byte_size_value.OptionWasSet();
    const bool num_per_line_option_set = m_num_per_line.OptionWasSet();
    const bool count_option_set = format_options.GetCountValue().OptionWasSet();

    switch (format_options.GetFormat()) {
    default:
      break;

    case eFormatBoolean:
      if (!byte_size_option_set)
        byte_size_value = 1;
      if (!num_per_line_option_set)
        m_num_per_line = 1;
      if (!count_option_set)
        format_options.GetCountValue() = 8;
      break;

    case eFormatCString:
      break;

    case eFormatInstruction:
      if (count_option_set)
        byte_size_value = target->GetArchitecture().GetMaximumOpcodeByteSize();
      m_num_per_line = 1;
      break;

    case eFormatAddressInfo:
      if (!byte_size_option_set)
        byte_size_value = target->GetArchitecture().GetAddressByteSize();
      m_num_per_line = 1;
      if (!count_option_set)
        format_options.GetCountValue() = 8;
      break;

    case eFormatPointer:
      byte_size_value = target->GetArchitecture().GetAddressByteSize();
      if (!num_per_line_option_set)
        m_num_per_line = 4;
      if (!count_option_set)
        format_options.GetCountValue() = 8;
      break;

    case eFormatBinary:
    case eFormatFloat:
    case eFormatOctal:
    case eFormatDecimal:
    case eFormatEnum:
    case eFormatUnicode8:
    case eFormatUnicode16:
    case eFormatUnicode32:
    case eFormatUnsigned:
    case eFormatHexFloat:
      if (!byte_size_option_set)
        byte_size_value = 4;
      if (!num_per_line_option_set)
        m_num_per_line = 1;
      if (!count_option_set)
        format_options.GetCountValue() = 8;
      break;

    case eFormatBytes:
    case eFormatBytesWithASCII:
      if (byte_size_option_set) {
        if (byte_size_value > 1)
          error.SetErrorStringWithFormat(
              "display format (bytes/bytes with ASCII) conflicts with the "
              "specified byte size %" PRIu64 "\n"
              "\tconsider using a different display format or don't specify "
              "the byte size.",
              byte_size_value.GetCurrentValue());
      } else
        byte_size_value = 1;
      if (!num_per_line_option_set)
        m_num_per_line = 16;
      if (!count_option_set)
        format_options.GetCountValue() = 32;
      break;

    case eFormatCharArray:
    case eFormatChar:
    case eFormatCharPrintable:
      if (!byte_size_option_set)
        byte_size_value = 1;
      if (!num_per_line_option_set)
        m_num_per_line = 32;
      if (!count_option_set)
        format_options.GetCountValue() = 64;
      break;

    case eFormatComplex:
      if (!byte_size_option_set)
        byte_size_value = 8;
      if (!num_per_line_option_set)
        m_num_per_line = 1;
      if (!count_option_set)
        format_options.GetCountValue() = 8;
      break;

    case eFormatComplexInteger:
      if (!byte_size_option_set)
        byte_size_value = 8;
      if (!num_per_line_option_set)
        m_num_per_line = 1;
      if (!count_option_set)
        format_options.GetCountValue() = 8;
      break;

    case eFormatHex:
      if (!byte_size_option_set)
        byte_size_value = 4;
      if (!num_per_line_option_set) {
        switch (byte_size_value) {
        case 1:
        case 2:
          m_num_per_line = 8;
          break;
        case 4:
          m_num_per_line = 4;
          break;
        case 8:
          m_num_per_line = 2;
          break;
        default:
          m_num_per_line = 1;
          break;
        }
      }
      if (!count_option_set)
        count_value = 8;
      break;

    case eFormatVectorOfChar:
    case eFormatVectorOfSInt8:
    case eFormatVectorOfUInt8:
    case eFormatVectorOfSInt16:
    case eFormatVectorOfUInt16:
    case eFormatVectorOfSInt32:
    case eFormatVectorOfUInt32:
    case eFormatVectorOfSInt64:
    case eFormatVectorOfUInt64:
    case eFormatVectorOfFloat16:
    case eFormatVectorOfFloat32:
    case eFormatVectorOfFloat64:
    case eFormatVectorOfUInt128:
      if (!byte_size_option_set)
        byte_size_value = 128;
      if (!num_per_line_option_set)
        m_num_per_line = 1;
      if (!count_option_set)
        count_value = 4;
      break;
    }
    return error;
  }

  bool AnyOptionWasSet() const {
    return m_num_per_line.OptionWasSet() || m_output_as_binary ||
           m_view_as_type.OptionWasSet() || m_offset.OptionWasSet() ||
           m_language_for_type.OptionWasSet();
  }

  OptionValueUInt64 m_num_per_line;
  bool m_output_as_binary = false;
  OptionValueString m_view_as_type;
  bool m_force = false;
  OptionValueUInt64 m_offset;
  OptionValueLanguage m_language_for_type;
};

// Read memory from the inferior process
class CommandObjectMemoryRead : public CommandObjectParsed {
public:
  CommandObjectMemoryRead(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "memory read",
            "Read from the memory of the current target process.", nullptr,
            eCommandRequiresTarget | eCommandProcessMustBePaused),
        m_format_options(eFormatBytesWithASCII, 1, 8),
        m_memory_tag_options(/*note_binary=*/true),
        m_prev_format_options(eFormatBytesWithASCII, 1, 8) {
    CommandArgumentEntry arg1;
    CommandArgumentEntry arg2;
    CommandArgumentData start_addr_arg;
    CommandArgumentData end_addr_arg;

    // Define the first (and only) variant of this arg.
    start_addr_arg.arg_type = eArgTypeAddressOrExpression;
    start_addr_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg1.push_back(start_addr_arg);

    // Define the first (and only) variant of this arg.
    end_addr_arg.arg_type = eArgTypeAddressOrExpression;
    end_addr_arg.arg_repetition = eArgRepeatOptional;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg2.push_back(end_addr_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg1);
    m_arguments.push_back(arg2);

    // Add the "--format" and "--count" options to group 1 and 3
    m_option_group.Append(&m_format_options,
                          OptionGroupFormat::OPTION_GROUP_FORMAT |
                              OptionGroupFormat::OPTION_GROUP_COUNT,
                          LLDB_OPT_SET_1 | LLDB_OPT_SET_2 | LLDB_OPT_SET_3);
    m_option_group.Append(&m_format_options,
                          OptionGroupFormat::OPTION_GROUP_GDB_FMT,
                          LLDB_OPT_SET_1 | LLDB_OPT_SET_3);
    // Add the "--size" option to group 1 and 2
    m_option_group.Append(&m_format_options,
                          OptionGroupFormat::OPTION_GROUP_SIZE,
                          LLDB_OPT_SET_1 | LLDB_OPT_SET_2);
    m_option_group.Append(&m_memory_options);
    m_option_group.Append(&m_outfile_options, LLDB_OPT_SET_ALL,
                          LLDB_OPT_SET_1 | LLDB_OPT_SET_2 | LLDB_OPT_SET_3);
    m_option_group.Append(&m_varobj_options, LLDB_OPT_SET_ALL, LLDB_OPT_SET_3);
    m_option_group.Append(&m_memory_tag_options, LLDB_OPT_SET_ALL,
                          LLDB_OPT_SET_ALL);
    m_option_group.Finalize();
  }

  ~CommandObjectMemoryRead() override = default;

  Options *GetOptions() override { return &m_option_group; }

  std::optional<std::string> GetRepeatCommand(Args &current_command_args,
                                              uint32_t index) override {
    return m_cmd_name;
  }

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    // No need to check "target" for validity as eCommandRequiresTarget ensures
    // it is valid
    Target *target = m_exe_ctx.GetTargetPtr();

    const size_t argc = command.GetArgumentCount();

    if ((argc == 0 && m_next_addr == LLDB_INVALID_ADDRESS) || argc > 2) {
      result.AppendErrorWithFormat("%s takes a start address expression with "
                                   "an optional end address expression.\n",
                                   m_cmd_name.c_str());
      result.AppendWarning("Expressions should be quoted if they contain "
                           "spaces or other special characters.");
      return;
    }

    CompilerType compiler_type;
    Status error;

    const char *view_as_type_cstr =
        m_memory_options.m_view_as_type.GetCurrentValue();
    if (view_as_type_cstr && view_as_type_cstr[0]) {
      // We are viewing memory as a type

      uint32_t reference_count = 0;
      uint32_t pointer_count = 0;
      size_t idx;

#define ALL_KEYWORDS                                                           \
  KEYWORD("const")                                                             \
  KEYWORD("volatile")                                                          \
  KEYWORD("restrict")                                                          \
  KEYWORD("struct")                                                            \
  KEYWORD("class")                                                             \
  KEYWORD("union")

#define KEYWORD(s) s,
      static const char *g_keywords[] = {ALL_KEYWORDS};
#undef KEYWORD

#define KEYWORD(s) (sizeof(s) - 1),
      static const int g_keyword_lengths[] = {ALL_KEYWORDS};
#undef KEYWORD

#undef ALL_KEYWORDS

      static size_t g_num_keywords = sizeof(g_keywords) / sizeof(const char *);
      std::string type_str(view_as_type_cstr);

      // Remove all instances of g_keywords that are followed by spaces
      for (size_t i = 0; i < g_num_keywords; ++i) {
        const char *keyword = g_keywords[i];
        int keyword_len = g_keyword_lengths[i];

        idx = 0;
        while ((idx = type_str.find(keyword, idx)) != std::string::npos) {
          if (type_str[idx + keyword_len] == ' ' ||
              type_str[idx + keyword_len] == '\t') {
            type_str.erase(idx, keyword_len + 1);
            idx = 0;
          } else {
            idx += keyword_len;
          }
        }
      }
      bool done = type_str.empty();
      //
      idx = type_str.find_first_not_of(" \t");
      if (idx > 0 && idx != std::string::npos)
        type_str.erase(0, idx);
      while (!done) {
        // Strip trailing spaces
        if (type_str.empty())
          done = true;
        else {
          switch (type_str[type_str.size() - 1]) {
          case '*':
            ++pointer_count;
            [[fallthrough]];
          case ' ':
          case '\t':
            type_str.erase(type_str.size() - 1);
            break;

          case '&':
            if (reference_count == 0) {
              reference_count = 1;
              type_str.erase(type_str.size() - 1);
            } else {
              result.AppendErrorWithFormat("invalid type string: '%s'\n",
                                           view_as_type_cstr);
              return;
            }
            break;

          default:
            done = true;
            break;
          }
        }
      }

      ConstString lookup_type_name(type_str.c_str());
      StackFrame *frame = m_exe_ctx.GetFramePtr();
      ModuleSP search_first;
      if (frame)
        search_first = frame->GetSymbolContext(eSymbolContextModule).module_sp;
      TypeQuery query(lookup_type_name.GetStringRef(),
                      TypeQueryOptions::e_find_one);
      TypeResults results;
      target->GetImages().FindTypes(search_first.get(), query, results);
      TypeSP type_sp = results.GetFirstType();

      if (!type_sp && lookup_type_name.GetCString()) {
        LanguageType language_for_type =
            m_memory_options.m_language_for_type.GetCurrentValue();
        std::set<LanguageType> languages_to_check;
        if (language_for_type != eLanguageTypeUnknown) {
          languages_to_check.insert(language_for_type);
        } else {
          languages_to_check = Language::GetSupportedLanguages();
        }

        std::set<CompilerType> user_defined_types;
        for (auto lang : languages_to_check) {
          if (auto *persistent_vars =
                  target->GetPersistentExpressionStateForLanguage(lang)) {
            if (std::optional<CompilerType> type =
                    persistent_vars->GetCompilerTypeFromPersistentDecl(
                        lookup_type_name)) {
              user_defined_types.emplace(*type);
            }
          }
        }

        if (user_defined_types.size() > 1) {
          result.AppendErrorWithFormat(
              "Mutiple types found matching raw type '%s', please disambiguate "
              "by specifying the language with -x",
              lookup_type_name.GetCString());
          return;
        }

        if (user_defined_types.size() == 1) {
          compiler_type = *user_defined_types.begin();
        }
      }

      if (!compiler_type.IsValid()) {
        if (type_sp) {
          compiler_type = type_sp->GetFullCompilerType();
        } else {
          result.AppendErrorWithFormat("unable to find any types that match "
                                       "the raw type '%s' for full type '%s'\n",
                                       lookup_type_name.GetCString(),
                                       view_as_type_cstr);
          return;
        }
      }

      while (pointer_count > 0) {
        CompilerType pointer_type = compiler_type.GetPointerType();
        if (pointer_type.IsValid())
          compiler_type = pointer_type;
        else {
          result.AppendError("unable make a pointer type\n");
          return;
        }
        --pointer_count;
      }

      std::optional<uint64_t> size = compiler_type.GetByteSize(nullptr);
      if (!size) {
        result.AppendErrorWithFormat(
            "unable to get the byte size of the type '%s'\n",
            view_as_type_cstr);
        return;
      }
      m_format_options.GetByteSizeValue() = *size;

      if (!m_format_options.GetCountValue().OptionWasSet())
        m_format_options.GetCountValue() = 1;
    } else {
      error = m_memory_options.FinalizeSettings(target, m_format_options);
    }

    // Look for invalid combinations of settings
    if (error.Fail()) {
      result.AppendError(error.AsCString());
      return;
    }

    lldb::addr_t addr;
    size_t total_byte_size = 0;
    if (argc == 0) {
      // Use the last address and byte size and all options as they were if no
      // options have been set
      addr = m_next_addr;
      total_byte_size = m_prev_byte_size;
      compiler_type = m_prev_compiler_type;
      if (!m_format_options.AnyOptionWasSet() &&
          !m_memory_options.AnyOptionWasSet() &&
          !m_outfile_options.AnyOptionWasSet() &&
          !m_varobj_options.AnyOptionWasSet() &&
          !m_memory_tag_options.AnyOptionWasSet()) {
        m_format_options = m_prev_format_options;
        m_memory_options = m_prev_memory_options;
        m_outfile_options = m_prev_outfile_options;
        m_varobj_options = m_prev_varobj_options;
        m_memory_tag_options = m_prev_memory_tag_options;
      }
    }

    size_t item_count = m_format_options.GetCountValue().GetCurrentValue();

    // TODO For non-8-bit byte addressable architectures this needs to be
    // revisited to fully support all lldb's range of formatting options.
    // Furthermore code memory reads (for those architectures) will not be
    // correctly formatted even w/o formatting options.
    size_t item_byte_size =
        target->GetArchitecture().GetDataByteSize() > 1
            ? target->GetArchitecture().GetDataByteSize()
            : m_format_options.GetByteSizeValue().GetCurrentValue();

    const size_t num_per_line =
        m_memory_options.m_num_per_line.GetCurrentValue();

    if (total_byte_size == 0) {
      total_byte_size = item_count * item_byte_size;
      if (total_byte_size == 0)
        total_byte_size = 32;
    }

    if (argc > 0)
      addr = OptionArgParser::ToAddress(&m_exe_ctx, command[0].ref(),
                                        LLDB_INVALID_ADDRESS, &error);

    if (addr == LLDB_INVALID_ADDRESS) {
      result.AppendError("invalid start address expression.");
      result.AppendError(error.AsCString());
      return;
    }

    if (argc == 2) {
      lldb::addr_t end_addr = OptionArgParser::ToAddress(
          &m_exe_ctx, command[1].ref(), LLDB_INVALID_ADDRESS, nullptr);

      if (end_addr == LLDB_INVALID_ADDRESS) {
        result.AppendError("invalid end address expression.");
        result.AppendError(error.AsCString());
        return;
      } else if (end_addr <= addr) {
        result.AppendErrorWithFormat(
            "end address (0x%" PRIx64
            ") must be greater than the start address (0x%" PRIx64 ").\n",
            end_addr, addr);
        return;
      } else if (m_format_options.GetCountValue().OptionWasSet()) {
        result.AppendErrorWithFormat(
            "specify either the end address (0x%" PRIx64
            ") or the count (--count %" PRIu64 "), not both.\n",
            end_addr, (uint64_t)item_count);
        return;
      }

      total_byte_size = end_addr - addr;
      item_count = total_byte_size / item_byte_size;
    }

    uint32_t max_unforced_size = target->GetMaximumMemReadSize();

    if (total_byte_size > max_unforced_size && !m_memory_options.m_force) {
      result.AppendErrorWithFormat(
          "Normally, \'memory read\' will not read over %" PRIu32
          " bytes of data.\n",
          max_unforced_size);
      result.AppendErrorWithFormat(
          "Please use --force to override this restriction just once.\n");
      result.AppendErrorWithFormat("or set target.max-memory-read-size if you "
                                   "will often need a larger limit.\n");
      return;
    }

    WritableDataBufferSP data_sp;
    size_t bytes_read = 0;
    if (compiler_type.GetOpaqueQualType()) {
      // Make sure we don't display our type as ASCII bytes like the default
      // memory read
      if (!m_format_options.GetFormatValue().OptionWasSet())
        m_format_options.GetFormatValue().SetCurrentValue(eFormatDefault);

      std::optional<uint64_t> size = compiler_type.GetByteSize(nullptr);
      if (!size) {
        result.AppendError("can't get size of type");
        return;
      }
      bytes_read = *size * m_format_options.GetCountValue().GetCurrentValue();

      if (argc > 0)
        addr = addr + (*size * m_memory_options.m_offset.GetCurrentValue());
    } else if (m_format_options.GetFormatValue().GetCurrentValue() !=
               eFormatCString) {
      data_sp = std::make_shared<DataBufferHeap>(total_byte_size, '\0');
      if (data_sp->GetBytes() == nullptr) {
        result.AppendErrorWithFormat(
            "can't allocate 0x%" PRIx32
            " bytes for the memory read buffer, specify a smaller size to read",
            (uint32_t)total_byte_size);
        return;
      }

      Address address(addr, nullptr);
      bytes_read = target->ReadMemory(address, data_sp->GetBytes(),
                                      data_sp->GetByteSize(), error, true);
      if (bytes_read == 0) {
        const char *error_cstr = error.AsCString();
        if (error_cstr && error_cstr[0]) {
          result.AppendError(error_cstr);
        } else {
          result.AppendErrorWithFormat(
              "failed to read memory from 0x%" PRIx64 ".\n", addr);
        }
        return;
      }

      if (bytes_read < total_byte_size)
        result.AppendWarningWithFormat(
            "Not all bytes (%" PRIu64 "/%" PRIu64
            ") were able to be read from 0x%" PRIx64 ".\n",
            (uint64_t)bytes_read, (uint64_t)total_byte_size, addr);
    } else {
      // we treat c-strings as a special case because they do not have a fixed
      // size
      if (m_format_options.GetByteSizeValue().OptionWasSet() &&
          !m_format_options.HasGDBFormat())
        item_byte_size = m_format_options.GetByteSizeValue().GetCurrentValue();
      else
        item_byte_size = target->GetMaximumSizeOfStringSummary();
      if (!m_format_options.GetCountValue().OptionWasSet())
        item_count = 1;
      data_sp = std::make_shared<DataBufferHeap>(
          (item_byte_size + 1) * item_count,
          '\0'); // account for NULLs as necessary
      if (data_sp->GetBytes() == nullptr) {
        result.AppendErrorWithFormat(
            "can't allocate 0x%" PRIx64
            " bytes for the memory read buffer, specify a smaller size to read",
            (uint64_t)((item_byte_size + 1) * item_count));
        return;
      }
      uint8_t *data_ptr = data_sp->GetBytes();
      auto data_addr = addr;
      auto count = item_count;
      item_count = 0;
      bool break_on_no_NULL = false;
      while (item_count < count) {
        std::string buffer;
        buffer.resize(item_byte_size + 1, 0);
        Status error;
        size_t read = target->ReadCStringFromMemory(data_addr, &buffer[0],
                                                    item_byte_size + 1, error);
        if (error.Fail()) {
          result.AppendErrorWithFormat(
              "failed to read memory from 0x%" PRIx64 ".\n", addr);
          return;
        }

        if (item_byte_size == read) {
          result.AppendWarningWithFormat(
              "unable to find a NULL terminated string at 0x%" PRIx64
              ". Consider increasing the maximum read length.\n",
              data_addr);
          --read;
          break_on_no_NULL = true;
        } else
          ++read; // account for final NULL byte

        memcpy(data_ptr, &buffer[0], read);
        data_ptr += read;
        data_addr += read;
        bytes_read += read;
        item_count++; // if we break early we know we only read item_count
                      // strings

        if (break_on_no_NULL)
          break;
      }
      data_sp =
          std::make_shared<DataBufferHeap>(data_sp->GetBytes(), bytes_read + 1);
    }

    m_next_addr = addr + bytes_read;
    m_prev_byte_size = bytes_read;
    m_prev_format_options = m_format_options;
    m_prev_memory_options = m_memory_options;
    m_prev_outfile_options = m_outfile_options;
    m_prev_varobj_options = m_varobj_options;
    m_prev_memory_tag_options = m_memory_tag_options;
    m_prev_compiler_type = compiler_type;

    std::unique_ptr<Stream> output_stream_storage;
    Stream *output_stream_p = nullptr;
    const FileSpec &outfile_spec =
        m_outfile_options.GetFile().GetCurrentValue();

    std::string path = outfile_spec.GetPath();
    if (outfile_spec) {

      File::OpenOptions open_options =
          File::eOpenOptionWriteOnly | File::eOpenOptionCanCreate;
      const bool append = m_outfile_options.GetAppend().GetCurrentValue();
      open_options |=
          append ? File::eOpenOptionAppend : File::eOpenOptionTruncate;

      auto outfile = FileSystem::Instance().Open(outfile_spec, open_options);

      if (outfile) {
        auto outfile_stream_up =
            std::make_unique<StreamFile>(std::move(outfile.get()));
        if (m_memory_options.m_output_as_binary) {
          const size_t bytes_written =
              outfile_stream_up->Write(data_sp->GetBytes(), bytes_read);
          if (bytes_written > 0) {
            result.GetOutputStream().Printf(
                "%zi bytes %s to '%s'\n", bytes_written,
                append ? "appended" : "written", path.c_str());
            return;
          } else {
            result.AppendErrorWithFormat("Failed to write %" PRIu64
                                         " bytes to '%s'.\n",
                                         (uint64_t)bytes_read, path.c_str());
            return;
          }
        } else {
          // We are going to write ASCII to the file just point the
          // output_stream to our outfile_stream...
          output_stream_storage = std::move(outfile_stream_up);
          output_stream_p = output_stream_storage.get();
        }
      } else {
        result.AppendErrorWithFormat("Failed to open file '%s' for %s:\n",
                                     path.c_str(), append ? "append" : "write");

        result.AppendError(llvm::toString(outfile.takeError()));
        return;
      }
    } else {
      output_stream_p = &result.GetOutputStream();
    }

    ExecutionContextScope *exe_scope = m_exe_ctx.GetBestExecutionContextScope();
    if (compiler_type.GetOpaqueQualType()) {
      for (uint32_t i = 0; i < item_count; ++i) {
        addr_t item_addr = addr + (i * item_byte_size);
        Address address(item_addr);
        StreamString name_strm;
        name_strm.Printf("0x%" PRIx64, item_addr);
        ValueObjectSP valobj_sp(ValueObjectMemory::Create(
            exe_scope, name_strm.GetString(), address, compiler_type));
        if (valobj_sp) {
          Format format = m_format_options.GetFormat();
          if (format != eFormatDefault)
            valobj_sp->SetFormat(format);

          DumpValueObjectOptions options(m_varobj_options.GetAsDumpOptions(
              eLanguageRuntimeDescriptionDisplayVerbosityFull, format));

          if (llvm::Error error = valobj_sp->Dump(*output_stream_p, options)) {
            result.AppendError(toString(std::move(error)));
            return;
          }
        } else {
          result.AppendErrorWithFormat(
              "failed to create a value object for: (%s) %s\n",
              view_as_type_cstr, name_strm.GetData());
          return;
        }
      }
      return;
    }

    result.SetStatus(eReturnStatusSuccessFinishResult);
    DataExtractor data(data_sp, target->GetArchitecture().GetByteOrder(),
                       target->GetArchitecture().GetAddressByteSize(),
                       target->GetArchitecture().GetDataByteSize());

    Format format = m_format_options.GetFormat();
    if (((format == eFormatChar) || (format == eFormatCharPrintable)) &&
        (item_byte_size != 1)) {
      // if a count was not passed, or it is 1
      if (!m_format_options.GetCountValue().OptionWasSet() || item_count == 1) {
        // this turns requests such as
        // memory read -fc -s10 -c1 *charPtrPtr
        // which make no sense (what is a char of size 10?) into a request for
        // fetching 10 chars of size 1 from the same memory location
        format = eFormatCharArray;
        item_count = item_byte_size;
        item_byte_size = 1;
      } else {
        // here we passed a count, and it was not 1 so we have a byte_size and
        // a count we could well multiply those, but instead let's just fail
        result.AppendErrorWithFormat(
            "reading memory as characters of size %" PRIu64 " is not supported",
            (uint64_t)item_byte_size);
        return;
      }
    }

    assert(output_stream_p);
    size_t bytes_dumped = DumpDataExtractor(
        data, output_stream_p, 0, format, item_byte_size, item_count,
        num_per_line / target->GetArchitecture().GetDataByteSize(), addr, 0, 0,
        exe_scope, m_memory_tag_options.GetShowTags().GetCurrentValue());
    m_next_addr = addr + bytes_dumped;
    output_stream_p->EOL();
  }

  OptionGroupOptions m_option_group;
  OptionGroupFormat m_format_options;
  OptionGroupReadMemory m_memory_options;
  OptionGroupOutputFile m_outfile_options;
  OptionGroupValueObjectDisplay m_varobj_options;
  OptionGroupMemoryTag m_memory_tag_options;
  lldb::addr_t m_next_addr = LLDB_INVALID_ADDRESS;
  lldb::addr_t m_prev_byte_size = 0;
  OptionGroupFormat m_prev_format_options;
  OptionGroupReadMemory m_prev_memory_options;
  OptionGroupOutputFile m_prev_outfile_options;
  OptionGroupValueObjectDisplay m_prev_varobj_options;
  OptionGroupMemoryTag m_prev_memory_tag_options;
  CompilerType m_prev_compiler_type;
};

#define LLDB_OPTIONS_memory_find
#include "CommandOptions.inc"

// Find the specified data in memory
class CommandObjectMemoryFind : public CommandObjectParsed {
public:
  class OptionGroupFindMemory : public OptionGroup {
  public:
    OptionGroupFindMemory() : m_count(1), m_offset(0) {}

    ~OptionGroupFindMemory() override = default;

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_memory_find_options);
    }

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_value,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = g_memory_find_options[option_idx].short_option;

      switch (short_option) {
      case 'e':
        m_expr.SetValueFromString(option_value);
        break;

      case 's':
        m_string.SetValueFromString(option_value);
        break;

      case 'c':
        if (m_count.SetValueFromString(option_value).Fail())
          error.SetErrorString("unrecognized value for count");
        break;

      case 'o':
        if (m_offset.SetValueFromString(option_value).Fail())
          error.SetErrorString("unrecognized value for dump-offset");
        break;

      default:
        llvm_unreachable("Unimplemented option");
      }
      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_expr.Clear();
      m_string.Clear();
      m_count.Clear();
    }

    OptionValueString m_expr;
    OptionValueString m_string;
    OptionValueUInt64 m_count;
    OptionValueUInt64 m_offset;
  };

  CommandObjectMemoryFind(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "memory find",
            "Find a value in the memory of the current target process.",
            nullptr, eCommandRequiresProcess | eCommandProcessMustBeLaunched) {
    CommandArgumentEntry arg1;
    CommandArgumentEntry arg2;
    CommandArgumentData addr_arg;
    CommandArgumentData value_arg;

    // Define the first (and only) variant of this arg.
    addr_arg.arg_type = eArgTypeAddressOrExpression;
    addr_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg1.push_back(addr_arg);

    // Define the first (and only) variant of this arg.
    value_arg.arg_type = eArgTypeAddressOrExpression;
    value_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg2.push_back(value_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg1);
    m_arguments.push_back(arg2);

    m_option_group.Append(&m_memory_options);
    m_option_group.Append(&m_memory_tag_options, LLDB_OPT_SET_ALL,
                          LLDB_OPT_SET_ALL);
    m_option_group.Finalize();
  }

  ~CommandObjectMemoryFind() override = default;

  Options *GetOptions() override { return &m_option_group; }

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    // No need to check "process" for validity as eCommandRequiresProcess
    // ensures it is valid
    Process *process = m_exe_ctx.GetProcessPtr();

    const size_t argc = command.GetArgumentCount();

    if (argc != 2) {
      result.AppendError("two addresses needed for memory find");
      return;
    }

    Status error;
    lldb::addr_t low_addr = OptionArgParser::ToAddress(
        &m_exe_ctx, command[0].ref(), LLDB_INVALID_ADDRESS, &error);
    if (low_addr == LLDB_INVALID_ADDRESS || error.Fail()) {
      result.AppendError("invalid low address");
      return;
    }
    lldb::addr_t high_addr = OptionArgParser::ToAddress(
        &m_exe_ctx, command[1].ref(), LLDB_INVALID_ADDRESS, &error);
    if (high_addr == LLDB_INVALID_ADDRESS || error.Fail()) {
      result.AppendError("invalid high address");
      return;
    }

    if (high_addr <= low_addr) {
      result.AppendError(
          "starting address must be smaller than ending address");
      return;
    }

    lldb::addr_t found_location = LLDB_INVALID_ADDRESS;

    DataBufferHeap buffer;

    if (m_memory_options.m_string.OptionWasSet()) {
      llvm::StringRef str =
          m_memory_options.m_string.GetValueAs<llvm::StringRef>().value_or("");
      if (str.empty()) {
        result.AppendError("search string must have non-zero length.");
        return;
      }
      buffer.CopyData(str);
    } else if (m_memory_options.m_expr.OptionWasSet()) {
      StackFrame *frame = m_exe_ctx.GetFramePtr();
      ValueObjectSP result_sp;
      if ((eExpressionCompleted ==
           process->GetTarget().EvaluateExpression(
               m_memory_options.m_expr.GetValueAs<llvm::StringRef>().value_or(
                   ""),
               frame, result_sp)) &&
          result_sp) {
        uint64_t value = result_sp->GetValueAsUnsigned(0);
        std::optional<uint64_t> size =
            result_sp->GetCompilerType().GetByteSize(nullptr);
        if (!size)
          return;
        switch (*size) {
        case 1: {
          uint8_t byte = (uint8_t)value;
          buffer.CopyData(&byte, 1);
        } break;
        case 2: {
          uint16_t word = (uint16_t)value;
          buffer.CopyData(&word, 2);
        } break;
        case 4: {
          uint32_t lword = (uint32_t)value;
          buffer.CopyData(&lword, 4);
        } break;
        case 8: {
          buffer.CopyData(&value, 8);
        } break;
        case 3:
        case 5:
        case 6:
        case 7:
          result.AppendError("unknown type. pass a string instead");
          return;
        default:
          result.AppendError(
              "result size larger than 8 bytes. pass a string instead");
          return;
        }
      } else {
        result.AppendError(
            "expression evaluation failed. pass a string instead");
        return;
      }
    } else {
      result.AppendError(
          "please pass either a block of text, or an expression to evaluate.");
      return;
    }

    size_t count = m_memory_options.m_count.GetCurrentValue();
    found_location = low_addr;
    bool ever_found = false;
    while (count) {
      found_location = process->FindInMemory(
          found_location, high_addr, buffer.GetBytes(), buffer.GetByteSize());
      if (found_location == LLDB_INVALID_ADDRESS) {
        if (!ever_found) {
          result.AppendMessage("data not found within the range.\n");
          result.SetStatus(lldb::eReturnStatusSuccessFinishNoResult);
        } else
          result.AppendMessage("no more matches within the range.\n");
        break;
      }
      result.AppendMessageWithFormat("data found at location: 0x%" PRIx64 "\n",
                                     found_location);

      DataBufferHeap dumpbuffer(32, 0);
      process->ReadMemory(
          found_location + m_memory_options.m_offset.GetCurrentValue(),
          dumpbuffer.GetBytes(), dumpbuffer.GetByteSize(), error);
      if (!error.Fail()) {
        DataExtractor data(dumpbuffer.GetBytes(), dumpbuffer.GetByteSize(),
                           process->GetByteOrder(),
                           process->GetAddressByteSize());
        DumpDataExtractor(
            data, &result.GetOutputStream(), 0, lldb::eFormatBytesWithASCII, 1,
            dumpbuffer.GetByteSize(), 16,
            found_location + m_memory_options.m_offset.GetCurrentValue(), 0, 0,
            m_exe_ctx.GetBestExecutionContextScope(),
            m_memory_tag_options.GetShowTags().GetCurrentValue());
        result.GetOutputStream().EOL();
      }

      --count;
      found_location++;
      ever_found = true;
    }

    result.SetStatus(lldb::eReturnStatusSuccessFinishResult);
  }

  OptionGroupOptions m_option_group;
  OptionGroupFindMemory m_memory_options;
  OptionGroupMemoryTag m_memory_tag_options;
};

#define LLDB_OPTIONS_memory_write
#include "CommandOptions.inc"

// Write memory to the inferior process
class CommandObjectMemoryWrite : public CommandObjectParsed {
public:
  class OptionGroupWriteMemory : public OptionGroup {
  public:
    OptionGroupWriteMemory() = default;

    ~OptionGroupWriteMemory() override = default;

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_memory_write_options);
    }

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_value,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = g_memory_write_options[option_idx].short_option;

      switch (short_option) {
      case 'i':
        m_infile.SetFile(option_value, FileSpec::Style::native);
        FileSystem::Instance().Resolve(m_infile);
        if (!FileSystem::Instance().Exists(m_infile)) {
          m_infile.Clear();
          error.SetErrorStringWithFormat("input file does not exist: '%s'",
                                         option_value.str().c_str());
        }
        break;

      case 'o': {
        if (option_value.getAsInteger(0, m_infile_offset)) {
          m_infile_offset = 0;
          error.SetErrorStringWithFormat("invalid offset string '%s'",
                                         option_value.str().c_str());
        }
      } break;

      default:
        llvm_unreachable("Unimplemented option");
      }
      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_infile.Clear();
      m_infile_offset = 0;
    }

    FileSpec m_infile;
    off_t m_infile_offset;
  };

  CommandObjectMemoryWrite(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "memory write",
            "Write to the memory of the current target process.", nullptr,
            eCommandRequiresProcess | eCommandProcessMustBeLaunched),
        m_format_options(
            eFormatBytes, 1, UINT64_MAX,
            {std::make_tuple(
                 eArgTypeFormat,
                 "The format to use for each of the value to be written."),
             std::make_tuple(eArgTypeByteSize,
                             "The size in bytes to write from input file or "
                             "each value.")}) {
    CommandArgumentEntry arg1;
    CommandArgumentEntry arg2;
    CommandArgumentData addr_arg;
    CommandArgumentData value_arg;

    // Define the first (and only) variant of this arg.
    addr_arg.arg_type = eArgTypeAddress;
    addr_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg1.push_back(addr_arg);

    // Define the first (and only) variant of this arg.
    value_arg.arg_type = eArgTypeValue;
    value_arg.arg_repetition = eArgRepeatPlus;
    value_arg.arg_opt_set_association = LLDB_OPT_SET_1;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg2.push_back(value_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg1);
    m_arguments.push_back(arg2);

    m_option_group.Append(&m_format_options,
                          OptionGroupFormat::OPTION_GROUP_FORMAT,
                          LLDB_OPT_SET_1);
    m_option_group.Append(&m_format_options,
                          OptionGroupFormat::OPTION_GROUP_SIZE,
                          LLDB_OPT_SET_1 | LLDB_OPT_SET_2);
    m_option_group.Append(&m_memory_options, LLDB_OPT_SET_ALL, LLDB_OPT_SET_2);
    m_option_group.Finalize();
  }

  ~CommandObjectMemoryWrite() override = default;

  Options *GetOptions() override { return &m_option_group; }

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    // No need to check "process" for validity as eCommandRequiresProcess
    // ensures it is valid
    Process *process = m_exe_ctx.GetProcessPtr();

    const size_t argc = command.GetArgumentCount();

    if (m_memory_options.m_infile) {
      if (argc < 1) {
        result.AppendErrorWithFormat(
            "%s takes a destination address when writing file contents.\n",
            m_cmd_name.c_str());
        return;
      }
      if (argc > 1) {
        result.AppendErrorWithFormat(
            "%s takes only a destination address when writing file contents.\n",
            m_cmd_name.c_str());
        return;
      }
    } else if (argc < 2) {
      result.AppendErrorWithFormat(
          "%s takes a destination address and at least one value.\n",
          m_cmd_name.c_str());
      return;
    }

    StreamString buffer(
        Stream::eBinary,
        process->GetTarget().GetArchitecture().GetAddressByteSize(),
        process->GetTarget().GetArchitecture().GetByteOrder());

    OptionValueUInt64 &byte_size_value = m_format_options.GetByteSizeValue();
    size_t item_byte_size = byte_size_value.GetCurrentValue();

    Status error;
    lldb::addr_t addr = OptionArgParser::ToAddress(
        &m_exe_ctx, command[0].ref(), LLDB_INVALID_ADDRESS, &error);

    if (addr == LLDB_INVALID_ADDRESS) {
      result.AppendError("invalid address expression\n");
      result.AppendError(error.AsCString());
      return;
    }

    if (m_memory_options.m_infile) {
      size_t length = SIZE_MAX;
      if (item_byte_size > 1)
        length = item_byte_size;
      auto data_sp = FileSystem::Instance().CreateDataBuffer(
          m_memory_options.m_infile.GetPath(), length,
          m_memory_options.m_infile_offset);
      if (data_sp) {
        length = data_sp->GetByteSize();
        if (length > 0) {
          Status error;
          size_t bytes_written =
              process->WriteMemory(addr, data_sp->GetBytes(), length, error);

          if (bytes_written == length) {
            // All bytes written
            result.GetOutputStream().Printf(
                "%" PRIu64 " bytes were written to 0x%" PRIx64 "\n",
                (uint64_t)bytes_written, addr);
            result.SetStatus(eReturnStatusSuccessFinishResult);
          } else if (bytes_written > 0) {
            // Some byte written
            result.GetOutputStream().Printf(
                "%" PRIu64 " bytes of %" PRIu64
                " requested were written to 0x%" PRIx64 "\n",
                (uint64_t)bytes_written, (uint64_t)length, addr);
            result.SetStatus(eReturnStatusSuccessFinishResult);
          } else {
            result.AppendErrorWithFormat("Memory write to 0x%" PRIx64
                                         " failed: %s.\n",
                                         addr, error.AsCString());
          }
        }
      } else {
        result.AppendErrorWithFormat("Unable to read contents of file.\n");
      }
      return;
    } else if (item_byte_size == 0) {
      if (m_format_options.GetFormat() == eFormatPointer)
        item_byte_size = buffer.GetAddressByteSize();
      else
        item_byte_size = 1;
    }

    command.Shift(); // shift off the address argument
    uint64_t uval64;
    int64_t sval64;
    bool success = false;
    for (auto &entry : command) {
      switch (m_format_options.GetFormat()) {
      case kNumFormats:
      case eFormatFloat: // TODO: add support for floats soon
      case eFormatCharPrintable:
      case eFormatBytesWithASCII:
      case eFormatComplex:
      case eFormatEnum:
      case eFormatUnicode8:
      case eFormatUnicode16:
      case eFormatUnicode32:
      case eFormatVectorOfChar:
      case eFormatVectorOfSInt8:
      case eFormatVectorOfUInt8:
      case eFormatVectorOfSInt16:
      case eFormatVectorOfUInt16:
      case eFormatVectorOfSInt32:
      case eFormatVectorOfUInt32:
      case eFormatVectorOfSInt64:
      case eFormatVectorOfUInt64:
      case eFormatVectorOfFloat16:
      case eFormatVectorOfFloat32:
      case eFormatVectorOfFloat64:
      case eFormatVectorOfUInt128:
      case eFormatOSType:
      case eFormatComplexInteger:
      case eFormatAddressInfo:
      case eFormatHexFloat:
      case eFormatInstruction:
      case eFormatVoid:
        result.AppendError("unsupported format for writing memory");
        return;

      case eFormatDefault:
      case eFormatBytes:
      case eFormatHex:
      case eFormatHexUppercase:
      case eFormatPointer: {
        // Decode hex bytes
        // Be careful, getAsInteger with a radix of 16 rejects "0xab" so we
        // have to special case that:
        bool success = false;
        if (entry.ref().starts_with("0x"))
          success = !entry.ref().getAsInteger(0, uval64);
        if (!success)
          success = !entry.ref().getAsInteger(16, uval64);
        if (!success) {
          result.AppendErrorWithFormat(
              "'%s' is not a valid hex string value.\n", entry.c_str());
          return;
        } else if (!llvm::isUIntN(item_byte_size * 8, uval64)) {
          result.AppendErrorWithFormat("Value 0x%" PRIx64
                                       " is too large to fit in a %" PRIu64
                                       " byte unsigned integer value.\n",
                                       uval64, (uint64_t)item_byte_size);
          return;
        }
        buffer.PutMaxHex64(uval64, item_byte_size);
        break;
      }
      case eFormatBoolean:
        uval64 = OptionArgParser::ToBoolean(entry.ref(), false, &success);
        if (!success) {
          result.AppendErrorWithFormat(
              "'%s' is not a valid boolean string value.\n", entry.c_str());
          return;
        }
        buffer.PutMaxHex64(uval64, item_byte_size);
        break;

      case eFormatBinary:
        if (entry.ref().getAsInteger(2, uval64)) {
          result.AppendErrorWithFormat(
              "'%s' is not a valid binary string value.\n", entry.c_str());
          return;
        } else if (!llvm::isUIntN(item_byte_size * 8, uval64)) {
          result.AppendErrorWithFormat("Value 0x%" PRIx64
                                       " is too large to fit in a %" PRIu64
                                       " byte unsigned integer value.\n",
                                       uval64, (uint64_t)item_byte_size);
          return;
        }
        buffer.PutMaxHex64(uval64, item_byte_size);
        break;

      case eFormatCharArray:
      case eFormatChar:
      case eFormatCString: {
        if (entry.ref().empty())
          break;

        size_t len = entry.ref().size();
        // Include the NULL for C strings...
        if (m_format_options.GetFormat() == eFormatCString)
          ++len;
        Status error;
        if (process->WriteMemory(addr, entry.c_str(), len, error) == len) {
          addr += len;
        } else {
          result.AppendErrorWithFormat("Memory write to 0x%" PRIx64
                                       " failed: %s.\n",
                                       addr, error.AsCString());
          return;
        }
        break;
      }
      case eFormatDecimal:
        if (entry.ref().getAsInteger(0, sval64)) {
          result.AppendErrorWithFormat(
              "'%s' is not a valid signed decimal value.\n", entry.c_str());
          return;
        } else if (!llvm::isIntN(item_byte_size * 8, sval64)) {
          result.AppendErrorWithFormat(
              "Value %" PRIi64 " is too large or small to fit in a %" PRIu64
              " byte signed integer value.\n",
              sval64, (uint64_t)item_byte_size);
          return;
        }
        buffer.PutMaxHex64(sval64, item_byte_size);
        break;

      case eFormatUnsigned:

        if (entry.ref().getAsInteger(0, uval64)) {
          result.AppendErrorWithFormat(
              "'%s' is not a valid unsigned decimal string value.\n",
              entry.c_str());
          return;
        } else if (!llvm::isUIntN(item_byte_size * 8, uval64)) {
          result.AppendErrorWithFormat("Value %" PRIu64
                                       " is too large to fit in a %" PRIu64
                                       " byte unsigned integer value.\n",
                                       uval64, (uint64_t)item_byte_size);
          return;
        }
        buffer.PutMaxHex64(uval64, item_byte_size);
        break;

      case eFormatOctal:
        if (entry.ref().getAsInteger(8, uval64)) {
          result.AppendErrorWithFormat(
              "'%s' is not a valid octal string value.\n", entry.c_str());
          return;
        } else if (!llvm::isUIntN(item_byte_size * 8, uval64)) {
          result.AppendErrorWithFormat("Value %" PRIo64
                                       " is too large to fit in a %" PRIu64
                                       " byte unsigned integer value.\n",
                                       uval64, (uint64_t)item_byte_size);
          return;
        }
        buffer.PutMaxHex64(uval64, item_byte_size);
        break;
      }
    }

    if (!buffer.GetString().empty()) {
      Status error;
      const char *buffer_data = buffer.GetString().data();
      const size_t buffer_size = buffer.GetString().size();
      const size_t write_size =
          process->WriteMemory(addr, buffer_data, buffer_size, error);

      if (write_size != buffer_size) {
        result.AppendErrorWithFormat("Memory write to 0x%" PRIx64
                                     " failed: %s.\n",
                                     addr, error.AsCString());
        return;
      }
    }
  }

  OptionGroupOptions m_option_group;
  OptionGroupFormat m_format_options;
  OptionGroupWriteMemory m_memory_options;
};

// Get malloc/free history of a memory address.
class CommandObjectMemoryHistory : public CommandObjectParsed {
public:
  CommandObjectMemoryHistory(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "memory history",
                            "Print recorded stack traces for "
                            "allocation/deallocation events "
                            "associated with an address.",
                            nullptr,
                            eCommandRequiresTarget | eCommandRequiresProcess |
                                eCommandProcessMustBePaused |
                                eCommandProcessMustBeLaunched) {
    CommandArgumentEntry arg1;
    CommandArgumentData addr_arg;

    // Define the first (and only) variant of this arg.
    addr_arg.arg_type = eArgTypeAddress;
    addr_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg1.push_back(addr_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg1);
  }

  ~CommandObjectMemoryHistory() override = default;

  std::optional<std::string> GetRepeatCommand(Args &current_command_args,
                                              uint32_t index) override {
    return m_cmd_name;
  }

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    const size_t argc = command.GetArgumentCount();

    if (argc == 0 || argc > 1) {
      result.AppendErrorWithFormat("%s takes an address expression",
                                   m_cmd_name.c_str());
      return;
    }

    Status error;
    lldb::addr_t addr = OptionArgParser::ToAddress(
        &m_exe_ctx, command[0].ref(), LLDB_INVALID_ADDRESS, &error);

    if (addr == LLDB_INVALID_ADDRESS) {
      result.AppendError("invalid address expression");
      result.AppendError(error.AsCString());
      return;
    }

    Stream *output_stream = &result.GetOutputStream();

    const ProcessSP &process_sp = m_exe_ctx.GetProcessSP();
    const MemoryHistorySP &memory_history =
        MemoryHistory::FindPlugin(process_sp);

    if (!memory_history) {
      result.AppendError("no available memory history provider");
      return;
    }

    HistoryThreads thread_list = memory_history->GetHistoryThreads(addr);

    const bool stop_format = false;
    for (auto thread : thread_list) {
      thread->GetStatus(*output_stream, 0, UINT32_MAX, 0, stop_format);
    }

    result.SetStatus(eReturnStatusSuccessFinishResult);
  }
};

// CommandObjectMemoryRegion
#pragma mark CommandObjectMemoryRegion

#define LLDB_OPTIONS_memory_region
#include "CommandOptions.inc"

class CommandObjectMemoryRegion : public CommandObjectParsed {
public:
  class OptionGroupMemoryRegion : public OptionGroup {
  public:
    OptionGroupMemoryRegion() : m_all(false, false) {}

    ~OptionGroupMemoryRegion() override = default;

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_memory_region_options);
    }

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_value,
                          ExecutionContext *execution_context) override {
      Status status;
      const int short_option = g_memory_region_options[option_idx].short_option;

      switch (short_option) {
      case 'a':
        m_all.SetCurrentValue(true);
        m_all.SetOptionWasSet();
        break;
      default:
        llvm_unreachable("Unimplemented option");
      }

      return status;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_all.Clear();
    }

    OptionValueBoolean m_all;
  };

  CommandObjectMemoryRegion(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "memory region",
                            "Get information on the memory region containing "
                            "an address in the current target process.",
                            "memory region <address-expression> (or --all)",
                            eCommandRequiresProcess | eCommandTryTargetAPILock |
                                eCommandProcessMustBeLaunched) {
    // Address in option set 1.
    m_arguments.push_back(CommandArgumentEntry{CommandArgumentData(
        eArgTypeAddressOrExpression, eArgRepeatPlain, LLDB_OPT_SET_1)});
    // "--all" will go in option set 2.
    m_option_group.Append(&m_memory_region_options);
    m_option_group.Finalize();
  }

  ~CommandObjectMemoryRegion() override = default;

  Options *GetOptions() override { return &m_option_group; }

protected:
  void DumpRegion(CommandReturnObject &result, Target &target,
                  const MemoryRegionInfo &range_info, lldb::addr_t load_addr) {
    lldb_private::Address addr;
    ConstString section_name;
    if (target.ResolveLoadAddress(load_addr, addr)) {
      SectionSP section_sp(addr.GetSection());
      if (section_sp) {
        // Got the top most section, not the deepest section
        while (section_sp->GetParent())
          section_sp = section_sp->GetParent();
        section_name = section_sp->GetName();
      }
    }

    ConstString name = range_info.GetName();
    result.AppendMessageWithFormatv(
        "[{0:x16}-{1:x16}) {2:r}{3:w}{4:x}{5}{6}{7}{8}",
        range_info.GetRange().GetRangeBase(),
        range_info.GetRange().GetRangeEnd(), range_info.GetReadable(),
        range_info.GetWritable(), range_info.GetExecutable(), name ? " " : "",
        name, section_name ? " " : "", section_name);
    MemoryRegionInfo::OptionalBool memory_tagged = range_info.GetMemoryTagged();
    if (memory_tagged == MemoryRegionInfo::OptionalBool::eYes)
      result.AppendMessage("memory tagging: enabled");

    const std::optional<std::vector<addr_t>> &dirty_page_list =
        range_info.GetDirtyPageList();
    if (dirty_page_list) {
      const size_t page_count = dirty_page_list->size();
      result.AppendMessageWithFormat(
          "Modified memory (dirty) page list provided, %zu entries.\n",
          page_count);
      if (page_count > 0) {
        bool print_comma = false;
        result.AppendMessageWithFormat("Dirty pages: ");
        for (size_t i = 0; i < page_count; i++) {
          if (print_comma)
            result.AppendMessageWithFormat(", ");
          else
            print_comma = true;
          result.AppendMessageWithFormat("0x%" PRIx64, (*dirty_page_list)[i]);
        }
        result.AppendMessageWithFormat(".\n");
      }
    }
  }

  void DoExecute(Args &command, CommandReturnObject &result) override {
    ProcessSP process_sp = m_exe_ctx.GetProcessSP();
    if (!process_sp) {
      m_prev_end_addr = LLDB_INVALID_ADDRESS;
      result.AppendError("invalid process");
      return;
    }

    Status error;
    lldb::addr_t load_addr = m_prev_end_addr;
    m_prev_end_addr = LLDB_INVALID_ADDRESS;

    const size_t argc = command.GetArgumentCount();
    const lldb::ABISP &abi = process_sp->GetABI();

    if (argc == 1) {
      if (m_memory_region_options.m_all) {
        result.AppendError(
            "The \"--all\" option cannot be used when an address "
            "argument is given");
        return;
      }

      auto load_addr_str = command[0].ref();
      load_addr = OptionArgParser::ToAddress(&m_exe_ctx, load_addr_str,
                                             LLDB_INVALID_ADDRESS, &error);
      if (error.Fail() || load_addr == LLDB_INVALID_ADDRESS) {
        result.AppendErrorWithFormat("invalid address argument \"%s\": %s\n",
                                     command[0].c_str(), error.AsCString());
        return;
      }
    } else if (argc > 1 ||
               // When we're repeating the command, the previous end address is
               // used for load_addr. If that was 0xF...F then we must have
               // reached the end of memory.
               (argc == 0 && !m_memory_region_options.m_all &&
                load_addr == LLDB_INVALID_ADDRESS) ||
               // If the target has non-address bits (tags, limited virtual
               // address size, etc.), the end of mappable memory will be lower
               // than that. So if we find any non-address bit set, we must be
               // at the end of the mappable range.
               (abi && (abi->FixAnyAddress(load_addr) != load_addr))) {
      result.AppendErrorWithFormat(
          "'%s' takes one argument or \"--all\" option:\nUsage: %s\n",
          m_cmd_name.c_str(), m_cmd_syntax.c_str());
      return;
    }

    // It is important that we track the address used to request the region as
    // this will give the correct section name in the case that regions overlap.
    // On Windows we get mutliple regions that start at the same place but are
    // different sizes and refer to different sections.
    std::vector<std::pair<lldb_private::MemoryRegionInfo, lldb::addr_t>>
        region_list;
    if (m_memory_region_options.m_all) {
      // We don't use GetMemoryRegions here because it doesn't include unmapped
      // areas like repeating the command would. So instead, emulate doing that.
      lldb::addr_t addr = 0;
      while (error.Success() && addr != LLDB_INVALID_ADDRESS &&
             // When there are non-address bits the last range will not extend
             // to LLDB_INVALID_ADDRESS but to the max virtual address.
             // This prevents us looping forever if that is the case.
             (!abi || (abi->FixAnyAddress(addr) == addr))) {
        lldb_private::MemoryRegionInfo region_info;
        error = process_sp->GetMemoryRegionInfo(addr, region_info);

        if (error.Success()) {
          region_list.push_back({region_info, addr});
          addr = region_info.GetRange().GetRangeEnd();
        }
      }
    } else {
      lldb_private::MemoryRegionInfo region_info;
      error = process_sp->GetMemoryRegionInfo(load_addr, region_info);
      if (error.Success())
        region_list.push_back({region_info, load_addr});
    }

    if (error.Success()) {
      for (std::pair<MemoryRegionInfo, addr_t> &range : region_list) {
        DumpRegion(result, process_sp->GetTarget(), range.first, range.second);
        m_prev_end_addr = range.first.GetRange().GetRangeEnd();
      }

      result.SetStatus(eReturnStatusSuccessFinishResult);
      return;
    }

    result.AppendErrorWithFormat("%s\n", error.AsCString());
  }

  std::optional<std::string> GetRepeatCommand(Args &current_command_args,
                                              uint32_t index) override {
    // If we repeat this command, repeat it without any arguments so we can
    // show the next memory range
    return m_cmd_name;
  }

  lldb::addr_t m_prev_end_addr = LLDB_INVALID_ADDRESS;

  OptionGroupOptions m_option_group;
  OptionGroupMemoryRegion m_memory_region_options;
};

// CommandObjectMemory

CommandObjectMemory::CommandObjectMemory(CommandInterpreter &interpreter)
    : CommandObjectMultiword(
          interpreter, "memory",
          "Commands for operating on memory in the current target process.",
          "memory <subcommand> [<subcommand-options>]") {
  LoadSubCommand("find",
                 CommandObjectSP(new CommandObjectMemoryFind(interpreter)));
  LoadSubCommand("read",
                 CommandObjectSP(new CommandObjectMemoryRead(interpreter)));
  LoadSubCommand("write",
                 CommandObjectSP(new CommandObjectMemoryWrite(interpreter)));
  LoadSubCommand("history",
                 CommandObjectSP(new CommandObjectMemoryHistory(interpreter)));
  LoadSubCommand("region",
                 CommandObjectSP(new CommandObjectMemoryRegion(interpreter)));
  LoadSubCommand("tag",
                 CommandObjectSP(new CommandObjectMemoryTag(interpreter)));
}

CommandObjectMemory::~CommandObjectMemory() = default;
