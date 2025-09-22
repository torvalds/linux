//===-- CommandObjectType.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CommandObjectType.h"

#include "lldb/Core/Debugger.h"
#include "lldb/Core/IOHandler.h"
#include "lldb/DataFormatters/DataVisualization.h"
#include "lldb/DataFormatters/FormatClasses.h"
#include "lldb/Host/Config.h"
#include "lldb/Host/OptionParser.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandObject.h"
#include "lldb/Interpreter/CommandOptionArgumentTable.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/Interpreter/OptionGroupFormat.h"
#include "lldb/Interpreter/OptionValueBoolean.h"
#include "lldb/Interpreter/OptionValueLanguage.h"
#include "lldb/Interpreter/OptionValueString.h"
#include "lldb/Interpreter/Options.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Target/Language.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/RegularExpression.h"
#include "lldb/Utility/StringList.h"

#include "llvm/ADT/STLExtras.h"

#include <algorithm>
#include <functional>
#include <memory>

using namespace lldb;
using namespace lldb_private;

class ScriptAddOptions {
public:
  TypeSummaryImpl::Flags m_flags;
  StringList m_target_types;
  FormatterMatchType m_match_type;
  ConstString m_name;
  std::string m_category;

  ScriptAddOptions(const TypeSummaryImpl::Flags &flags,
                   FormatterMatchType match_type, ConstString name,
                   std::string catg)
      : m_flags(flags), m_match_type(match_type), m_name(name),
        m_category(catg) {}

  typedef std::shared_ptr<ScriptAddOptions> SharedPointer;
};

class SynthAddOptions {
public:
  bool m_skip_pointers;
  bool m_skip_references;
  bool m_cascade;
  FormatterMatchType m_match_type;
  StringList m_target_types;
  std::string m_category;

  SynthAddOptions(bool sptr, bool sref, bool casc,
                  FormatterMatchType match_type, std::string catg)
      : m_skip_pointers(sptr), m_skip_references(sref), m_cascade(casc),
        m_match_type(match_type), m_category(catg) {}

  typedef std::shared_ptr<SynthAddOptions> SharedPointer;
};

static bool WarnOnPotentialUnquotedUnsignedType(Args &command,
                                                CommandReturnObject &result) {
  if (command.empty())
    return false;

  for (auto entry : llvm::enumerate(command.entries().drop_back())) {
    if (entry.value().ref() != "unsigned")
      continue;
    auto next = command.entries()[entry.index() + 1].ref();
    if (next == "int" || next == "short" || next == "char" || next == "long") {
      result.AppendWarningWithFormat(
          "unsigned %s being treated as two types. if you meant the combined "
          "type "
          "name use  quotes, as in \"unsigned %s\"\n",
          next.str().c_str(), next.str().c_str());
      return true;
    }
  }
  return false;
}

const char *FormatCategoryToString(FormatCategoryItem item, bool long_name) {
  switch (item) {
  case eFormatCategoryItemSummary:
    return "summary";
  case eFormatCategoryItemFilter:
    return "filter";
  case eFormatCategoryItemSynth:
    if (long_name)
      return "synthetic child provider";
    return "synthetic";
  case eFormatCategoryItemFormat:
    return "format";
  }
  llvm_unreachable("Fully covered switch above!");
}

#define LLDB_OPTIONS_type_summary_add
#include "CommandOptions.inc"

class CommandObjectTypeSummaryAdd : public CommandObjectParsed,
                                    public IOHandlerDelegateMultiline {
private:
  class CommandOptions : public Options {
  public:
    CommandOptions(CommandInterpreter &interpreter) {}

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override;

    void OptionParsingStarting(ExecutionContext *execution_context) override;

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_type_summary_add_options);
    }

    // Instance variables to hold the values for command options.

    TypeSummaryImpl::Flags m_flags;
    FormatterMatchType m_match_type = eFormatterMatchExact;
    std::string m_format_string;
    ConstString m_name;
    std::string m_python_script;
    std::string m_python_function;
    bool m_is_add_script = false;
    std::string m_category;
  };

  CommandOptions m_options;

  Options *GetOptions() override { return &m_options; }

  bool Execute_ScriptSummary(Args &command, CommandReturnObject &result);

  bool Execute_StringSummary(Args &command, CommandReturnObject &result);

public:
  CommandObjectTypeSummaryAdd(CommandInterpreter &interpreter);

  ~CommandObjectTypeSummaryAdd() override = default;

  void IOHandlerActivated(IOHandler &io_handler, bool interactive) override {
    static const char *g_summary_addreader_instructions =
        "Enter your Python command(s). Type 'DONE' to end.\n"
        "def function (valobj,internal_dict):\n"
        "     \"\"\"valobj: an SBValue which you want to provide a summary "
        "for\n"
        "        internal_dict: an LLDB support object not to be used\"\"\"\n";

    StreamFileSP output_sp(io_handler.GetOutputStreamFileSP());
    if (output_sp && interactive) {
      output_sp->PutCString(g_summary_addreader_instructions);
      output_sp->Flush();
    }
  }

  void IOHandlerInputComplete(IOHandler &io_handler,
                              std::string &data) override {
    StreamFileSP error_sp = io_handler.GetErrorStreamFileSP();

#if LLDB_ENABLE_PYTHON
    ScriptInterpreter *interpreter = GetDebugger().GetScriptInterpreter();
    if (interpreter) {
      StringList lines;
      lines.SplitIntoLines(data);
      if (lines.GetSize() > 0) {
        ScriptAddOptions *options_ptr =
            ((ScriptAddOptions *)io_handler.GetUserData());
        if (options_ptr) {
          ScriptAddOptions::SharedPointer options(
              options_ptr); // this will ensure that we get rid of the pointer
                            // when going out of scope

          ScriptInterpreter *interpreter = GetDebugger().GetScriptInterpreter();
          if (interpreter) {
            std::string funct_name_str;
            if (interpreter->GenerateTypeScriptFunction(lines,
                                                        funct_name_str)) {
              if (funct_name_str.empty()) {
                error_sp->Printf("unable to obtain a valid function name from "
                                 "the script interpreter.\n");
                error_sp->Flush();
              } else {
                // now I have a valid function name, let's add this as script
                // for every type in the list

                TypeSummaryImplSP script_format;
                script_format = std::make_shared<ScriptSummaryFormat>(
                    options->m_flags, funct_name_str.c_str(),
                    lines.CopyList("    ").c_str());

                Status error;

                for (const std::string &type_name : options->m_target_types) {
                  AddSummary(ConstString(type_name), script_format,
                             options->m_match_type, options->m_category,
                             &error);
                  if (error.Fail()) {
                    error_sp->Printf("error: %s", error.AsCString());
                    error_sp->Flush();
                  }
                }

                if (options->m_name) {
                  CommandObjectTypeSummaryAdd::AddNamedSummary(
                      options->m_name, script_format, &error);
                  if (error.Fail()) {
                    CommandObjectTypeSummaryAdd::AddNamedSummary(
                        options->m_name, script_format, &error);
                    if (error.Fail()) {
                      error_sp->Printf("error: %s", error.AsCString());
                      error_sp->Flush();
                    }
                  } else {
                    error_sp->Printf("error: %s", error.AsCString());
                    error_sp->Flush();
                  }
                } else {
                  if (error.AsCString()) {
                    error_sp->Printf("error: %s", error.AsCString());
                    error_sp->Flush();
                  }
                }
              }
            } else {
              error_sp->Printf("error: unable to generate a function.\n");
              error_sp->Flush();
            }
          } else {
            error_sp->Printf("error: no script interpreter.\n");
            error_sp->Flush();
          }
        } else {
          error_sp->Printf("error: internal synchronization information "
                           "missing or invalid.\n");
          error_sp->Flush();
        }
      } else {
        error_sp->Printf("error: empty function, didn't add python command.\n");
        error_sp->Flush();
      }
    } else {
      error_sp->Printf(
          "error: script interpreter missing, didn't add python command.\n");
      error_sp->Flush();
    }
#endif
    io_handler.SetIsDone(true);
  }

  bool AddSummary(ConstString type_name, lldb::TypeSummaryImplSP entry,
                  FormatterMatchType match_type, std::string category,
                  Status *error = nullptr);

  bool AddNamedSummary(ConstString summary_name, lldb::TypeSummaryImplSP entry,
                       Status *error = nullptr);

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override;
};

static const char *g_synth_addreader_instructions =
    "Enter your Python command(s). Type 'DONE' to end.\n"
    "You must define a Python class with these methods:\n"
    "    def __init__(self, valobj, internal_dict):\n"
    "    def num_children(self):\n"
    "    def get_child_at_index(self, index):\n"
    "    def get_child_index(self, name):\n"
    "    def update(self):\n"
    "        '''Optional'''\n"
    "class synthProvider:\n";

#define LLDB_OPTIONS_type_synth_add
#include "CommandOptions.inc"

class CommandObjectTypeSynthAdd : public CommandObjectParsed,
                                  public IOHandlerDelegateMultiline {
private:
  class CommandOptions : public Options {
  public:
    CommandOptions() = default;

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;
      bool success;

      switch (short_option) {
      case 'C':
        m_cascade = OptionArgParser::ToBoolean(option_arg, true, &success);
        if (!success)
          error.SetErrorStringWithFormat("invalid value for cascade: %s",
                                         option_arg.str().c_str());
        break;
      case 'P':
        handwrite_python = true;
        break;
      case 'l':
        m_class_name = std::string(option_arg);
        is_class_based = true;
        break;
      case 'p':
        m_skip_pointers = true;
        break;
      case 'r':
        m_skip_references = true;
        break;
      case 'w':
        m_category = std::string(option_arg);
        break;
      case 'x':
        if (m_match_type == eFormatterMatchCallback)
          error.SetErrorString(
              "can't use --regex and --recognizer-function at the same time");
        else
          m_match_type = eFormatterMatchRegex;
        break;
      case '\x01':
        if (m_match_type == eFormatterMatchRegex)
          error.SetErrorString(
              "can't use --regex and --recognizer-function at the same time");
        else
          m_match_type = eFormatterMatchCallback;
        break;
      default:
        llvm_unreachable("Unimplemented option");
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_cascade = true;
      m_class_name = "";
      m_skip_pointers = false;
      m_skip_references = false;
      m_category = "default";
      is_class_based = false;
      handwrite_python = false;
      m_match_type = eFormatterMatchExact;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_type_synth_add_options);
    }

    // Instance variables to hold the values for command options.

    bool m_cascade;
    bool m_skip_references;
    bool m_skip_pointers;
    std::string m_class_name;
    bool m_input_python;
    std::string m_category;
    bool is_class_based;
    bool handwrite_python;
    FormatterMatchType m_match_type;
  };

  CommandOptions m_options;

  Options *GetOptions() override { return &m_options; }

  bool Execute_HandwritePython(Args &command, CommandReturnObject &result);

  bool Execute_PythonClass(Args &command, CommandReturnObject &result);

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    WarnOnPotentialUnquotedUnsignedType(command, result);

    if (m_options.handwrite_python)
      Execute_HandwritePython(command, result);
    else if (m_options.is_class_based)
      Execute_PythonClass(command, result);
    else {
      result.AppendError("must either provide a children list, a Python class "
                         "name, or use -P and type a Python class "
                         "line-by-line");
    }
  }

  void IOHandlerActivated(IOHandler &io_handler, bool interactive) override {
    StreamFileSP output_sp(io_handler.GetOutputStreamFileSP());
    if (output_sp && interactive) {
      output_sp->PutCString(g_synth_addreader_instructions);
      output_sp->Flush();
    }
  }

  void IOHandlerInputComplete(IOHandler &io_handler,
                              std::string &data) override {
    StreamFileSP error_sp = io_handler.GetErrorStreamFileSP();

#if LLDB_ENABLE_PYTHON
    ScriptInterpreter *interpreter = GetDebugger().GetScriptInterpreter();
    if (interpreter) {
      StringList lines;
      lines.SplitIntoLines(data);
      if (lines.GetSize() > 0) {
        SynthAddOptions *options_ptr =
            ((SynthAddOptions *)io_handler.GetUserData());
        if (options_ptr) {
          SynthAddOptions::SharedPointer options(
              options_ptr); // this will ensure that we get rid of the pointer
                            // when going out of scope

          ScriptInterpreter *interpreter = GetDebugger().GetScriptInterpreter();
          if (interpreter) {
            std::string class_name_str;
            if (interpreter->GenerateTypeSynthClass(lines, class_name_str)) {
              if (class_name_str.empty()) {
                error_sp->Printf(
                    "error: unable to obtain a proper name for the class.\n");
                error_sp->Flush();
              } else {
                // everything should be fine now, let's add the synth provider
                // class

                SyntheticChildrenSP synth_provider;
                synth_provider = std::make_shared<ScriptedSyntheticChildren>(
                    SyntheticChildren::Flags()
                        .SetCascades(options->m_cascade)
                        .SetSkipPointers(options->m_skip_pointers)
                        .SetSkipReferences(options->m_skip_references),
                    class_name_str.c_str());

                lldb::TypeCategoryImplSP category;
                DataVisualization::Categories::GetCategory(
                    ConstString(options->m_category.c_str()), category);

                Status error;

                for (const std::string &type_name : options->m_target_types) {
                  if (!type_name.empty()) {
                    if (AddSynth(ConstString(type_name), synth_provider,
                                 options->m_match_type, options->m_category,
                                 &error)) {
                      error_sp->Printf("error: %s\n", error.AsCString());
                      error_sp->Flush();
                      break;
                    }
                  } else {
                    error_sp->Printf("error: invalid type name.\n");
                    error_sp->Flush();
                    break;
                  }
                }
              }
            } else {
              error_sp->Printf("error: unable to generate a class.\n");
              error_sp->Flush();
            }
          } else {
            error_sp->Printf("error: no script interpreter.\n");
            error_sp->Flush();
          }
        } else {
          error_sp->Printf("error: internal synchronization data missing.\n");
          error_sp->Flush();
        }
      } else {
        error_sp->Printf("error: empty function, didn't add python command.\n");
        error_sp->Flush();
      }
    } else {
      error_sp->Printf(
          "error: script interpreter missing, didn't add python command.\n");
      error_sp->Flush();
    }

#endif
    io_handler.SetIsDone(true);
  }

public:
  CommandObjectTypeSynthAdd(CommandInterpreter &interpreter);

  ~CommandObjectTypeSynthAdd() override = default;

  bool AddSynth(ConstString type_name, lldb::SyntheticChildrenSP entry,
                FormatterMatchType match_type, std::string category_name,
                Status *error);
};

// CommandObjectTypeFormatAdd

#define LLDB_OPTIONS_type_format_add
#include "CommandOptions.inc"

class CommandObjectTypeFormatAdd : public CommandObjectParsed {
private:
  class CommandOptions : public OptionGroup {
  public:
    CommandOptions() = default;

    ~CommandOptions() override = default;

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_type_format_add_options);
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_cascade = true;
      m_skip_pointers = false;
      m_skip_references = false;
      m_regex = false;
      m_category.assign("default");
      m_custom_type_name.clear();
    }

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_value,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option =
          g_type_format_add_options[option_idx].short_option;
      bool success;

      switch (short_option) {
      case 'C':
        m_cascade = OptionArgParser::ToBoolean(option_value, true, &success);
        if (!success)
          error.SetErrorStringWithFormat("invalid value for cascade: %s",
                                         option_value.str().c_str());
        break;
      case 'p':
        m_skip_pointers = true;
        break;
      case 'w':
        m_category.assign(std::string(option_value));
        break;
      case 'r':
        m_skip_references = true;
        break;
      case 'x':
        m_regex = true;
        break;
      case 't':
        m_custom_type_name.assign(std::string(option_value));
        break;
      default:
        llvm_unreachable("Unimplemented option");
      }

      return error;
    }

    // Instance variables to hold the values for command options.

    bool m_cascade;
    bool m_skip_references;
    bool m_skip_pointers;
    bool m_regex;
    std::string m_category;
    std::string m_custom_type_name;
  };

  OptionGroupOptions m_option_group;
  OptionGroupFormat m_format_options;
  CommandOptions m_command_options;

  Options *GetOptions() override { return &m_option_group; }

public:
  CommandObjectTypeFormatAdd(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "type format add",
                            "Add a new formatting style for a type.", nullptr),
        m_format_options(eFormatInvalid) {
    AddSimpleArgumentList(eArgTypeName, eArgRepeatPlus);

    SetHelpLong(
        R"(
The following examples of 'type format add' refer to this code snippet for context:

    typedef int Aint;
    typedef float Afloat;
    typedef Aint Bint;
    typedef Afloat Bfloat;

    Aint ix = 5;
    Bint iy = 5;

    Afloat fx = 3.14;
    BFloat fy = 3.14;

Adding default formatting:

(lldb) type format add -f hex AInt
(lldb) frame variable iy

)"
        "    Produces hexadecimal display of iy, because no formatter is available for Bint and \
the one for Aint is used instead."
        R"(

To prevent this use the cascade option '-C no' to prevent evaluation of typedef chains:


(lldb) type format add -f hex -C no AInt

Similar reasoning applies to this:

(lldb) type format add -f hex -C no float -p

)"
        "    All float values and float references are now formatted as hexadecimal, but not \
pointers to floats.  Nor will it change the default display for Afloat and Bfloat objects.");

    // Add the "--format" to all options groups
    m_option_group.Append(&m_format_options,
                          OptionGroupFormat::OPTION_GROUP_FORMAT,
                          LLDB_OPT_SET_1);
    m_option_group.Append(&m_command_options);
    m_option_group.Finalize();
  }

  ~CommandObjectTypeFormatAdd() override = default;

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    const size_t argc = command.GetArgumentCount();

    if (argc < 1) {
      result.AppendErrorWithFormat("%s takes one or more args.\n",
                                   m_cmd_name.c_str());
      return;
    }

    const Format format = m_format_options.GetFormat();
    if (format == eFormatInvalid &&
        m_command_options.m_custom_type_name.empty()) {
      result.AppendErrorWithFormat("%s needs a valid format.\n",
                                   m_cmd_name.c_str());
      return;
    }

    TypeFormatImplSP entry;

    if (m_command_options.m_custom_type_name.empty())
      entry = std::make_shared<TypeFormatImpl_Format>(
          format, TypeFormatImpl::Flags()
                      .SetCascades(m_command_options.m_cascade)
                      .SetSkipPointers(m_command_options.m_skip_pointers)
                      .SetSkipReferences(m_command_options.m_skip_references));
    else
      entry = std::make_shared<TypeFormatImpl_EnumType>(
          ConstString(m_command_options.m_custom_type_name.c_str()),
          TypeFormatImpl::Flags()
              .SetCascades(m_command_options.m_cascade)
              .SetSkipPointers(m_command_options.m_skip_pointers)
              .SetSkipReferences(m_command_options.m_skip_references));

    // now I have a valid format, let's add it to every type

    TypeCategoryImplSP category_sp;
    DataVisualization::Categories::GetCategory(
        ConstString(m_command_options.m_category), category_sp);
    if (!category_sp)
      return;

    WarnOnPotentialUnquotedUnsignedType(command, result);

    for (auto &arg_entry : command.entries()) {
      if (arg_entry.ref().empty()) {
        result.AppendError("empty typenames not allowed");
        return;
      }

      FormatterMatchType match_type = eFormatterMatchExact;
      if (m_command_options.m_regex) {
        match_type = eFormatterMatchRegex;
        RegularExpression typeRX(arg_entry.ref());
        if (!typeRX.IsValid()) {
          result.AppendError(
              "regex format error (maybe this is not really a regex?)");
          return;
        }
      }
      category_sp->AddTypeFormat(arg_entry.ref(), match_type, entry);
    }

    result.SetStatus(eReturnStatusSuccessFinishNoResult);
  }
};

#define LLDB_OPTIONS_type_formatter_delete
#include "CommandOptions.inc"

class CommandObjectTypeFormatterDelete : public CommandObjectParsed {
protected:
  class CommandOptions : public Options {
  public:
    CommandOptions() = default;

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'a':
        m_delete_all = true;
        break;
      case 'w':
        m_category = std::string(option_arg);
        break;
      case 'l':
        m_language = Language::GetLanguageTypeFromString(option_arg);
        break;
      default:
        llvm_unreachable("Unimplemented option");
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_delete_all = false;
      m_category = "default";
      m_language = lldb::eLanguageTypeUnknown;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_type_formatter_delete_options);
    }

    // Instance variables to hold the values for command options.

    bool m_delete_all;
    std::string m_category;
    lldb::LanguageType m_language;
  };

  CommandOptions m_options;
  FormatCategoryItem m_formatter_kind;

  Options *GetOptions() override { return &m_options; }

  static constexpr const char *g_short_help_template =
      "Delete an existing %s for a type.";

  static constexpr const char *g_long_help_template =
      "Delete an existing %s for a type.  Unless you specify a "
      "specific category or all categories, only the "
      "'default' category is searched.  The names must be exactly as "
      "shown in the 'type %s list' output";

public:
  CommandObjectTypeFormatterDelete(CommandInterpreter &interpreter,
                                   FormatCategoryItem formatter_kind)
      : CommandObjectParsed(interpreter,
                            FormatCategoryToString(formatter_kind, false)),
        m_formatter_kind(formatter_kind) {
    AddSimpleArgumentList(eArgTypeName);

    const char *kind = FormatCategoryToString(formatter_kind, true);
    const char *short_kind = FormatCategoryToString(formatter_kind, false);

    StreamString s;
    s.Printf(g_short_help_template, kind);
    SetHelp(s.GetData());
    s.Clear();
    s.Printf(g_long_help_template, kind, short_kind);
    SetHelpLong(s.GetData());
    s.Clear();
    s.Printf("type %s delete", short_kind);
    SetCommandName(s.GetData());
  }

  ~CommandObjectTypeFormatterDelete() override = default;

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    if (request.GetCursorIndex())
      return;

    DataVisualization::Categories::ForEach(
        [this, &request](const lldb::TypeCategoryImplSP &category_sp) {
          category_sp->AutoComplete(request, m_formatter_kind);
          return true;
        });
  }

protected:
  virtual bool FormatterSpecificDeletion(ConstString typeCS) { return false; }

  void DoExecute(Args &command, CommandReturnObject &result) override {
    const size_t argc = command.GetArgumentCount();

    if (argc != 1) {
      result.AppendErrorWithFormat("%s takes 1 arg.\n", m_cmd_name.c_str());
      return;
    }

    const char *typeA = command.GetArgumentAtIndex(0);
    ConstString typeCS(typeA);

    if (!typeCS) {
      result.AppendError("empty typenames not allowed");
      return;
    }

    if (m_options.m_delete_all) {
      DataVisualization::Categories::ForEach(
          [this, typeCS](const lldb::TypeCategoryImplSP &category_sp) -> bool {
            category_sp->Delete(typeCS, m_formatter_kind);
            return true;
          });
      result.SetStatus(eReturnStatusSuccessFinishNoResult);
      return;
    }

    bool delete_category = false;
    bool extra_deletion = false;

    if (m_options.m_language != lldb::eLanguageTypeUnknown) {
      lldb::TypeCategoryImplSP category;
      DataVisualization::Categories::GetCategory(m_options.m_language,
                                                 category);
      if (category)
        delete_category = category->Delete(typeCS, m_formatter_kind);
      extra_deletion = FormatterSpecificDeletion(typeCS);
    } else {
      lldb::TypeCategoryImplSP category;
      DataVisualization::Categories::GetCategory(
          ConstString(m_options.m_category.c_str()), category);
      if (category)
        delete_category = category->Delete(typeCS, m_formatter_kind);
      extra_deletion = FormatterSpecificDeletion(typeCS);
    }

    if (delete_category || extra_deletion) {
      result.SetStatus(eReturnStatusSuccessFinishNoResult);
    } else {
      result.AppendErrorWithFormat("no custom formatter for %s.\n", typeA);
    }
  }
};

#define LLDB_OPTIONS_type_formatter_clear
#include "CommandOptions.inc"

class CommandObjectTypeFormatterClear : public CommandObjectParsed {
private:
  class CommandOptions : public Options {
  public:
    CommandOptions() = default;

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'a':
        m_delete_all = true;
        break;
      default:
        llvm_unreachable("Unimplemented option");
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_delete_all = false;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_type_formatter_clear_options);
    }

    // Instance variables to hold the values for command options.
    bool m_delete_all;
  };

  CommandOptions m_options;
  FormatCategoryItem m_formatter_kind;

  Options *GetOptions() override { return &m_options; }

public:
  CommandObjectTypeFormatterClear(CommandInterpreter &interpreter,
                                  FormatCategoryItem formatter_kind,
                                  const char *name, const char *help)
      : CommandObjectParsed(interpreter, name, help, nullptr),
        m_formatter_kind(formatter_kind) {
    AddSimpleArgumentList(eArgTypeName, eArgRepeatOptional);
  }

  ~CommandObjectTypeFormatterClear() override = default;

protected:
  virtual void FormatterSpecificDeletion() {}

  void DoExecute(Args &command, CommandReturnObject &result) override {
    if (m_options.m_delete_all) {
      DataVisualization::Categories::ForEach(
          [this](const TypeCategoryImplSP &category_sp) -> bool {
            category_sp->Clear(m_formatter_kind);
            return true;
          });
    } else {
      lldb::TypeCategoryImplSP category;
      if (command.GetArgumentCount() > 0) {
        const char *cat_name = command.GetArgumentAtIndex(0);
        ConstString cat_nameCS(cat_name);
        DataVisualization::Categories::GetCategory(cat_nameCS, category);
      } else {
        DataVisualization::Categories::GetCategory(ConstString(nullptr),
                                                   category);
      }
      category->Clear(m_formatter_kind);
    }

    FormatterSpecificDeletion();

    result.SetStatus(eReturnStatusSuccessFinishResult);
  }
};

// CommandObjectTypeFormatDelete

class CommandObjectTypeFormatDelete : public CommandObjectTypeFormatterDelete {
public:
  CommandObjectTypeFormatDelete(CommandInterpreter &interpreter)
      : CommandObjectTypeFormatterDelete(
            interpreter, eFormatCategoryItemFormat) {}

  ~CommandObjectTypeFormatDelete() override = default;
};

// CommandObjectTypeFormatClear

class CommandObjectTypeFormatClear : public CommandObjectTypeFormatterClear {
public:
  CommandObjectTypeFormatClear(CommandInterpreter &interpreter)
      : CommandObjectTypeFormatterClear(interpreter, eFormatCategoryItemFormat,
                                        "type format clear",
                                        "Delete all existing format styles.") {}
};

#define LLDB_OPTIONS_type_formatter_list
#include "CommandOptions.inc"

template <typename FormatterType>
class CommandObjectTypeFormatterList : public CommandObjectParsed {
  typedef typename FormatterType::SharedPointer FormatterSharedPointer;

  class CommandOptions : public Options {
  public:
    CommandOptions()
        : Options(), m_category_regex("", ""),
          m_category_language(lldb::eLanguageTypeUnknown,
                              lldb::eLanguageTypeUnknown) {}

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;
      switch (short_option) {
      case 'w':
        m_category_regex.SetCurrentValue(option_arg);
        m_category_regex.SetOptionWasSet();
        break;
      case 'l':
        error = m_category_language.SetValueFromString(option_arg);
        if (error.Success())
          m_category_language.SetOptionWasSet();
        break;
      default:
        llvm_unreachable("Unimplemented option");
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_category_regex.Clear();
      m_category_language.Clear();
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_type_formatter_list_options);
    }

    // Instance variables to hold the values for command options.

    OptionValueString m_category_regex;
    OptionValueLanguage m_category_language;
  };

  CommandOptions m_options;

  Options *GetOptions() override { return &m_options; }

public:
  CommandObjectTypeFormatterList(CommandInterpreter &interpreter,
                                 const char *name, const char *help)
      : CommandObjectParsed(interpreter, name, help, nullptr), m_options() {
    AddSimpleArgumentList(eArgTypeName, eArgRepeatOptional);
  }

  ~CommandObjectTypeFormatterList() override = default;

protected:
  virtual bool FormatterSpecificList(CommandReturnObject &result) {
    return false;
  }

  static bool ShouldListItem(llvm::StringRef s, RegularExpression *regex) {
    // If we have a regex, it can match two kinds of results:
    //   - An item created with that same regex string (exact string match), so
    //     the user can list it using the same string it used at creation time.
    //   - Items that match the regex.
    // No regex means list everything.
    return regex == nullptr || s == regex->GetText() || regex->Execute(s);
  }

  void DoExecute(Args &command, CommandReturnObject &result) override {
    const size_t argc = command.GetArgumentCount();

    std::unique_ptr<RegularExpression> category_regex;
    std::unique_ptr<RegularExpression> formatter_regex;

    if (m_options.m_category_regex.OptionWasSet()) {
      category_regex = std::make_unique<RegularExpression>(
          m_options.m_category_regex.GetCurrentValueAsRef());
      if (!category_regex->IsValid()) {
        result.AppendErrorWithFormat(
            "syntax error in category regular expression '%s'",
            m_options.m_category_regex.GetCurrentValueAsRef().str().c_str());
        return;
      }
    }

    if (argc == 1) {
      const char *arg = command.GetArgumentAtIndex(0);
      formatter_regex = std::make_unique<RegularExpression>(arg);
      if (!formatter_regex->IsValid()) {
        result.AppendErrorWithFormat("syntax error in regular expression '%s'",
                                     arg);
        return;
      }
    }

    bool any_printed = false;

    auto category_closure =
        [&result, &formatter_regex,
         &any_printed](const lldb::TypeCategoryImplSP &category) -> void {
      result.GetOutputStream().Printf(
          "-----------------------\nCategory: %s%s\n-----------------------\n",
          category->GetName(), category->IsEnabled() ? "" : " (disabled)");

      TypeCategoryImpl::ForEachCallback<FormatterType> print_formatter =
          [&result, &formatter_regex,
           &any_printed](const TypeMatcher &type_matcher,
                         const FormatterSharedPointer &format_sp) -> bool {
        if (ShouldListItem(type_matcher.GetMatchString().GetStringRef(),
                           formatter_regex.get())) {
          any_printed = true;
          result.GetOutputStream().Printf(
              "%s: %s\n", type_matcher.GetMatchString().GetCString(),
              format_sp->GetDescription().c_str());
        }
        return true;
      };
      category->ForEach(print_formatter);
    };

    if (m_options.m_category_language.OptionWasSet()) {
      lldb::TypeCategoryImplSP category_sp;
      DataVisualization::Categories::GetCategory(
          m_options.m_category_language.GetCurrentValue(), category_sp);
      if (category_sp)
        category_closure(category_sp);
    } else {
      DataVisualization::Categories::ForEach(
          [&category_regex, &category_closure](
              const lldb::TypeCategoryImplSP &category) -> bool {
            if (ShouldListItem(category->GetName(), category_regex.get())) {
              category_closure(category);
            }
            return true;
          });

      any_printed = FormatterSpecificList(result) | any_printed;
    }

    if (any_printed)
      result.SetStatus(eReturnStatusSuccessFinishResult);
    else {
      result.GetOutputStream().PutCString("no matching results found.\n");
      result.SetStatus(eReturnStatusSuccessFinishNoResult);
    }
  }
};

// CommandObjectTypeFormatList

class CommandObjectTypeFormatList
    : public CommandObjectTypeFormatterList<TypeFormatImpl> {
public:
  CommandObjectTypeFormatList(CommandInterpreter &interpreter)
      : CommandObjectTypeFormatterList(interpreter, "type format list",
                                       "Show a list of current formats.") {}
};

Status CommandObjectTypeSummaryAdd::CommandOptions::SetOptionValue(
    uint32_t option_idx, llvm::StringRef option_arg,
    ExecutionContext *execution_context) {
  Status error;
  const int short_option = m_getopt_table[option_idx].val;
  bool success;

  switch (short_option) {
  case 'C':
    m_flags.SetCascades(OptionArgParser::ToBoolean(option_arg, true, &success));
    if (!success)
      error.SetErrorStringWithFormat("invalid value for cascade: %s",
                                     option_arg.str().c_str());
    break;
  case 'e':
    m_flags.SetDontShowChildren(false);
    break;
  case 'h':
    m_flags.SetHideEmptyAggregates(true);
    break;
  case 'v':
    m_flags.SetDontShowValue(true);
    break;
  case 'c':
    m_flags.SetShowMembersOneLiner(true);
    break;
  case 's':
    m_format_string = std::string(option_arg);
    break;
  case 'p':
    m_flags.SetSkipPointers(true);
    break;
  case 'r':
    m_flags.SetSkipReferences(true);
    break;
  case 'x':
    if (m_match_type == eFormatterMatchCallback)
      error.SetErrorString(
          "can't use --regex and --recognizer-function at the same time");
    else
      m_match_type = eFormatterMatchRegex;
    break;
  case '\x01':
    if (m_match_type == eFormatterMatchRegex)
      error.SetErrorString(
          "can't use --regex and --recognizer-function at the same time");
    else
      m_match_type = eFormatterMatchCallback;
    break;
  case 'n':
    m_name.SetString(option_arg);
    break;
  case 'o':
    m_python_script = std::string(option_arg);
    m_is_add_script = true;
    break;
  case 'F':
    m_python_function = std::string(option_arg);
    m_is_add_script = true;
    break;
  case 'P':
    m_is_add_script = true;
    break;
  case 'w':
    m_category = std::string(option_arg);
    break;
  case 'O':
    m_flags.SetHideItemNames(true);
    break;
  default:
    llvm_unreachable("Unimplemented option");
  }

  return error;
}

void CommandObjectTypeSummaryAdd::CommandOptions::OptionParsingStarting(
    ExecutionContext *execution_context) {
  m_flags.Clear().SetCascades().SetDontShowChildren().SetDontShowValue(false);
  m_flags.SetShowMembersOneLiner(false)
      .SetSkipPointers(false)
      .SetSkipReferences(false)
      .SetHideItemNames(false);

  m_match_type = eFormatterMatchExact;
  m_name.Clear();
  m_python_script = "";
  m_python_function = "";
  m_format_string = "";
  m_is_add_script = false;
  m_category = "default";
}

#if LLDB_ENABLE_PYTHON

bool CommandObjectTypeSummaryAdd::Execute_ScriptSummary(
    Args &command, CommandReturnObject &result) {
  const size_t argc = command.GetArgumentCount();

  if (argc < 1 && !m_options.m_name) {
    result.AppendErrorWithFormat("%s takes one or more args.\n",
                                 m_cmd_name.c_str());
    return false;
  }

  TypeSummaryImplSP script_format;

  if (!m_options.m_python_function
           .empty()) // we have a Python function ready to use
  {
    const char *funct_name = m_options.m_python_function.c_str();
    if (!funct_name || !funct_name[0]) {
      result.AppendError("function name empty.\n");
      return false;
    }

    std::string code =
        ("    " + m_options.m_python_function + "(valobj,internal_dict)");

    script_format = std::make_shared<ScriptSummaryFormat>(
        m_options.m_flags, funct_name, code.c_str());

    ScriptInterpreter *interpreter = GetDebugger().GetScriptInterpreter();

    if (interpreter && !interpreter->CheckObjectExists(funct_name))
      result.AppendWarningWithFormat(
          "The provided function \"%s\" does not exist - "
          "please define it before attempting to use this summary.\n",
          funct_name);
  } else if (!m_options.m_python_script
                  .empty()) // we have a quick 1-line script, just use it
  {
    ScriptInterpreter *interpreter = GetDebugger().GetScriptInterpreter();
    if (!interpreter) {
      result.AppendError("script interpreter missing - unable to generate "
                         "function wrapper.\n");
      return false;
    }
    StringList funct_sl;
    funct_sl << m_options.m_python_script.c_str();
    std::string funct_name_str;
    if (!interpreter->GenerateTypeScriptFunction(funct_sl, funct_name_str)) {
      result.AppendError("unable to generate function wrapper.\n");
      return false;
    }
    if (funct_name_str.empty()) {
      result.AppendError(
          "script interpreter failed to generate a valid function name.\n");
      return false;
    }

    std::string code = "    " + m_options.m_python_script;

    script_format = std::make_shared<ScriptSummaryFormat>(
        m_options.m_flags, funct_name_str.c_str(), code.c_str());
  } else {
    // Use an IOHandler to grab Python code from the user
    auto options = std::make_unique<ScriptAddOptions>(
        m_options.m_flags, m_options.m_match_type, m_options.m_name,
        m_options.m_category);

    for (auto &entry : command.entries()) {
      if (entry.ref().empty()) {
        result.AppendError("empty typenames not allowed");
        return false;
      }

      options->m_target_types << std::string(entry.ref());
    }

    m_interpreter.GetPythonCommandsFromIOHandler(
        "    ",             // Prompt
        *this,              // IOHandlerDelegate
        options.release()); // Baton for the "io_handler" that will be passed
                            // back into our IOHandlerDelegate functions
    result.SetStatus(eReturnStatusSuccessFinishNoResult);

    return result.Succeeded();
  }

  // if I am here, script_format must point to something good, so I can add
  // that as a script summary to all interested parties

  Status error;

  for (auto &entry : command.entries()) {
    AddSummary(ConstString(entry.ref()), script_format, m_options.m_match_type,
               m_options.m_category, &error);
    if (error.Fail()) {
      result.AppendError(error.AsCString());
      return false;
    }
  }

  if (m_options.m_name) {
    AddNamedSummary(m_options.m_name, script_format, &error);
    if (error.Fail()) {
      result.AppendError(error.AsCString());
      result.AppendError("added to types, but not given a name");
      return false;
    }
  }

  return result.Succeeded();
}

#endif

bool CommandObjectTypeSummaryAdd::Execute_StringSummary(
    Args &command, CommandReturnObject &result) {
  const size_t argc = command.GetArgumentCount();

  if (argc < 1 && !m_options.m_name) {
    result.AppendErrorWithFormat("%s takes one or more args.\n",
                                 m_cmd_name.c_str());
    return false;
  }

  if (!m_options.m_flags.GetShowMembersOneLiner() &&
      m_options.m_format_string.empty()) {
    result.AppendError("empty summary strings not allowed");
    return false;
  }

  const char *format_cstr = (m_options.m_flags.GetShowMembersOneLiner()
                                 ? ""
                                 : m_options.m_format_string.c_str());

  // ${var%S} is an endless recursion, prevent it
  if (strcmp(format_cstr, "${var%S}") == 0) {
    result.AppendError("recursive summary not allowed");
    return false;
  }

  std::unique_ptr<StringSummaryFormat> string_format(
      new StringSummaryFormat(m_options.m_flags, format_cstr));
  if (!string_format) {
    result.AppendError("summary creation failed");
    return false;
  }
  if (string_format->m_error.Fail()) {
    result.AppendErrorWithFormat("syntax error: %s",
                                 string_format->m_error.AsCString("<unknown>"));
    return false;
  }
  lldb::TypeSummaryImplSP entry(string_format.release());

  // now I have a valid format, let's add it to every type
  Status error;
  for (auto &arg_entry : command.entries()) {
    if (arg_entry.ref().empty()) {
      result.AppendError("empty typenames not allowed");
      return false;
    }
    ConstString typeCS(arg_entry.ref());

    AddSummary(typeCS, entry, m_options.m_match_type, m_options.m_category,
               &error);

    if (error.Fail()) {
      result.AppendError(error.AsCString());
      return false;
    }
  }

  if (m_options.m_name) {
    AddNamedSummary(m_options.m_name, entry, &error);
    if (error.Fail()) {
      result.AppendError(error.AsCString());
      result.AppendError("added to types, but not given a name");
      return false;
    }
  }

  result.SetStatus(eReturnStatusSuccessFinishNoResult);
  return result.Succeeded();
}

CommandObjectTypeSummaryAdd::CommandObjectTypeSummaryAdd(
    CommandInterpreter &interpreter)
    : CommandObjectParsed(interpreter, "type summary add",
                          "Add a new summary style for a type.", nullptr),
      IOHandlerDelegateMultiline("DONE"), m_options(interpreter) {
  AddSimpleArgumentList(eArgTypeName, eArgRepeatPlus);

  SetHelpLong(
      R"(
The following examples of 'type summary add' refer to this code snippet for context:

    struct JustADemo
    {
        int* ptr;
        float value;
        JustADemo(int p = 1, float v = 0.1) : ptr(new int(p)), value(v) {}
    };
    JustADemo demo_instance(42, 3.14);

    typedef JustADemo NewDemo;
    NewDemo new_demo_instance(42, 3.14);

(lldb) type summary add --summary-string "the answer is ${*var.ptr}" JustADemo

    Subsequently displaying demo_instance with 'frame variable' or 'expression' will display "the answer is 42"

(lldb) type summary add --summary-string "the answer is ${*var.ptr}, and the question is ${var.value}" JustADemo

    Subsequently displaying demo_instance with 'frame variable' or 'expression' will display "the answer is 42 and the question is 3.14"

)"
      "Alternatively, you could define formatting for all pointers to integers and \
rely on that when formatting JustADemo to obtain the same result:"
      R"(

(lldb) type summary add --summary-string "${var%V} -> ${*var}" "int *"
(lldb) type summary add --summary-string "the answer is ${var.ptr}, and the question is ${var.value}" JustADemo

)"
      "Type summaries are automatically applied to derived typedefs, so the examples \
above apply to both JustADemo and NewDemo.  The cascade option can be used to \
suppress this behavior:"
      R"(

(lldb) type summary add --summary-string "${var.ptr}, ${var.value},{${var.byte}}" JustADemo -C no

    The summary will now be used for values of JustADemo but not NewDemo.

)"
      "By default summaries are shown for pointers and references to values of the \
specified type.  To suppress formatting for pointers use the -p option, or apply \
the corresponding -r option to suppress formatting for references:"
      R"(

(lldb) type summary add -p -r --summary-string "${var.ptr}, ${var.value},{${var.byte}}" JustADemo

)"
      "One-line summaries including all fields in a type can be inferred without supplying an \
explicit summary string by passing the -c option:"
      R"(

(lldb) type summary add -c JustADemo
(lldb) frame variable demo_instance
(ptr=<address>, value=3.14)

)"
      "Type summaries normally suppress the nested display of individual fields.  To \
supply a summary to supplement the default structure add the -e option:"
      R"(

(lldb) type summary add -e --summary-string "*ptr = ${*var.ptr}" JustADemo

)"
      "Now when displaying JustADemo values the int* is displayed, followed by the \
standard LLDB sequence of children, one per line:"
      R"(

*ptr = 42 {
  ptr = <address>
  value = 3.14
}

)"
      "You can also add summaries written in Python.  These scripts use lldb public API to \
gather information from your variables and produce a meaningful summary.  To start a \
multi-line script use the -P option.  The function declaration will be displayed along with \
a comment describing the two arguments.  End your script with the  word 'DONE' on a line by \
itself:"
      R"(

(lldb) type summary add JustADemo -P
def function (valobj,internal_dict):
"""valobj: an SBValue which you want to provide a summary for
internal_dict: an LLDB support object not to be used"""
    value = valobj.GetChildMemberWithName('value');
    return 'My value is ' + value.GetValue();
    DONE

Alternatively, the -o option can be used when providing a simple one-line Python script:

(lldb) type summary add JustADemo -o "value = valobj.GetChildMemberWithName('value'); return 'My value is ' + value.GetValue();")");
}

void CommandObjectTypeSummaryAdd::DoExecute(Args &command,
                                            CommandReturnObject &result) {
  WarnOnPotentialUnquotedUnsignedType(command, result);

  if (m_options.m_is_add_script) {
#if LLDB_ENABLE_PYTHON
    Execute_ScriptSummary(command, result);
#else
    result.AppendError("python is disabled");
#endif
    return;
  }

  Execute_StringSummary(command, result);
}

static bool FixArrayTypeNameWithRegex(ConstString &type_name) {
  llvm::StringRef type_name_ref(type_name.GetStringRef());

  if (type_name_ref.ends_with("[]")) {
    std::string type_name_str(type_name.GetCString());
    type_name_str.resize(type_name_str.length() - 2);
    if (type_name_str.back() != ' ')
      type_name_str.append(" ?\\[[0-9]+\\]");
    else
      type_name_str.append("\\[[0-9]+\\]");
    type_name.SetCString(type_name_str.c_str());
    return true;
  }
  return false;
}

bool CommandObjectTypeSummaryAdd::AddNamedSummary(ConstString summary_name,
                                                  TypeSummaryImplSP entry,
                                                  Status *error) {
  // system named summaries do not exist (yet?)
  DataVisualization::NamedSummaryFormats::Add(summary_name, entry);
  return true;
}

bool CommandObjectTypeSummaryAdd::AddSummary(ConstString type_name,
                                             TypeSummaryImplSP entry,
                                             FormatterMatchType match_type,
                                             std::string category_name,
                                             Status *error) {
  lldb::TypeCategoryImplSP category;
  DataVisualization::Categories::GetCategory(ConstString(category_name.c_str()),
                                             category);

  if (match_type == eFormatterMatchExact) {
    if (FixArrayTypeNameWithRegex(type_name))
      match_type = eFormatterMatchRegex;
  }

  if (match_type == eFormatterMatchRegex) {
    match_type = eFormatterMatchRegex;
    RegularExpression typeRX(type_name.GetStringRef());
    if (!typeRX.IsValid()) {
      if (error)
        error->SetErrorString(
            "regex format error (maybe this is not really a regex?)");
      return false;
    }
  }

  if (match_type == eFormatterMatchCallback) {
    const char *function_name = type_name.AsCString();
    ScriptInterpreter *interpreter = GetDebugger().GetScriptInterpreter();
    if (interpreter && !interpreter->CheckObjectExists(function_name)) {
      error->SetErrorStringWithFormat(
          "The provided recognizer function \"%s\" does not exist - "
          "please define it before attempting to use this summary.\n",
          function_name);
      return false;
    }
  }
  category->AddTypeSummary(type_name.GetStringRef(), match_type, entry);
  return true;
}

// CommandObjectTypeSummaryDelete

class CommandObjectTypeSummaryDelete : public CommandObjectTypeFormatterDelete {
public:
  CommandObjectTypeSummaryDelete(CommandInterpreter &interpreter)
      : CommandObjectTypeFormatterDelete(
            interpreter, eFormatCategoryItemSummary) {}

  ~CommandObjectTypeSummaryDelete() override = default;

protected:
  bool FormatterSpecificDeletion(ConstString typeCS) override {
    if (m_options.m_language != lldb::eLanguageTypeUnknown)
      return false;
    return DataVisualization::NamedSummaryFormats::Delete(typeCS);
  }
};

class CommandObjectTypeSummaryClear : public CommandObjectTypeFormatterClear {
public:
  CommandObjectTypeSummaryClear(CommandInterpreter &interpreter)
      : CommandObjectTypeFormatterClear(interpreter, eFormatCategoryItemSummary,
                                        "type summary clear",
                                        "Delete all existing summaries.") {}

protected:
  void FormatterSpecificDeletion() override {
    DataVisualization::NamedSummaryFormats::Clear();
  }
};

// CommandObjectTypeSummaryList

class CommandObjectTypeSummaryList
    : public CommandObjectTypeFormatterList<TypeSummaryImpl> {
public:
  CommandObjectTypeSummaryList(CommandInterpreter &interpreter)
      : CommandObjectTypeFormatterList(interpreter, "type summary list",
                                       "Show a list of current summaries.") {}

protected:
  bool FormatterSpecificList(CommandReturnObject &result) override {
    if (DataVisualization::NamedSummaryFormats::GetCount() > 0) {
      result.GetOutputStream().Printf("Named summaries:\n");
      DataVisualization::NamedSummaryFormats::ForEach(
          [&result](const TypeMatcher &type_matcher,
                    const TypeSummaryImplSP &summary_sp) -> bool {
            result.GetOutputStream().Printf(
                "%s: %s\n", type_matcher.GetMatchString().GetCString(),
                summary_sp->GetDescription().c_str());
            return true;
          });
      return true;
    }
    return false;
  }
};

// CommandObjectTypeCategoryDefine
#define LLDB_OPTIONS_type_category_define
#include "CommandOptions.inc"

class CommandObjectTypeCategoryDefine : public CommandObjectParsed {
  class CommandOptions : public Options {
  public:
    CommandOptions()
        : m_define_enabled(false, false),
          m_cate_language(eLanguageTypeUnknown, eLanguageTypeUnknown) {}

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'e':
        m_define_enabled.SetValueFromString(llvm::StringRef("true"));
        break;
      case 'l':
        error = m_cate_language.SetValueFromString(option_arg);
        break;
      default:
        llvm_unreachable("Unimplemented option");
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_define_enabled.Clear();
      m_cate_language.Clear();
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_type_category_define_options);
    }

    // Instance variables to hold the values for command options.

    OptionValueBoolean m_define_enabled;
    OptionValueLanguage m_cate_language;
  };

  CommandOptions m_options;

  Options *GetOptions() override { return &m_options; }

public:
  CommandObjectTypeCategoryDefine(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "type category define",
                            "Define a new category as a source of formatters.",
                            nullptr) {
    AddSimpleArgumentList(eArgTypeName, eArgRepeatPlus);
  }

  ~CommandObjectTypeCategoryDefine() override = default;

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    const size_t argc = command.GetArgumentCount();

    if (argc < 1) {
      result.AppendErrorWithFormat("%s takes 1 or more args.\n",
                                   m_cmd_name.c_str());
      return;
    }

    for (auto &entry : command.entries()) {
      TypeCategoryImplSP category_sp;
      if (DataVisualization::Categories::GetCategory(ConstString(entry.ref()),
                                                     category_sp) &&
          category_sp) {
        category_sp->AddLanguage(m_options.m_cate_language.GetCurrentValue());
        if (m_options.m_define_enabled.GetCurrentValue())
          DataVisualization::Categories::Enable(category_sp,
                                                TypeCategoryMap::Default);
      }
    }

    result.SetStatus(eReturnStatusSuccessFinishResult);
  }
};

// CommandObjectTypeCategoryEnable
#define LLDB_OPTIONS_type_category_enable
#include "CommandOptions.inc"

class CommandObjectTypeCategoryEnable : public CommandObjectParsed {
  class CommandOptions : public Options {
  public:
    CommandOptions() = default;

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'l':
        if (!option_arg.empty()) {
          m_language = Language::GetLanguageTypeFromString(option_arg);
          if (m_language == lldb::eLanguageTypeUnknown)
            error.SetErrorStringWithFormat("unrecognized language '%s'",
                                           option_arg.str().c_str());
        }
        break;
      default:
        llvm_unreachable("Unimplemented option");
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_language = lldb::eLanguageTypeUnknown;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_type_category_enable_options);
    }

    // Instance variables to hold the values for command options.

    lldb::LanguageType m_language;
  };

  CommandOptions m_options;

  Options *GetOptions() override { return &m_options; }

public:
  CommandObjectTypeCategoryEnable(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "type category enable",
                            "Enable a category as a source of formatters.",
                            nullptr) {
    AddSimpleArgumentList(eArgTypeName, eArgRepeatPlus);
  }

  ~CommandObjectTypeCategoryEnable() override = default;

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    const size_t argc = command.GetArgumentCount();

    if (argc < 1 && m_options.m_language == lldb::eLanguageTypeUnknown) {
      result.AppendErrorWithFormat("%s takes arguments and/or a language",
                                   m_cmd_name.c_str());
      return;
    }

    if (argc == 1 && strcmp(command.GetArgumentAtIndex(0), "*") == 0) {
      DataVisualization::Categories::EnableStar();
    } else if (argc > 0) {
      for (int i = argc - 1; i >= 0; i--) {
        const char *typeA = command.GetArgumentAtIndex(i);
        ConstString typeCS(typeA);

        if (!typeCS) {
          result.AppendError("empty category name not allowed");
          return;
        }
        DataVisualization::Categories::Enable(typeCS);
        lldb::TypeCategoryImplSP cate;
        if (DataVisualization::Categories::GetCategory(typeCS, cate) && cate) {
          if (cate->GetCount() == 0) {
            result.AppendWarning("empty category enabled (typo?)");
          }
        }
      }
    }

    if (m_options.m_language != lldb::eLanguageTypeUnknown)
      DataVisualization::Categories::Enable(m_options.m_language);

    result.SetStatus(eReturnStatusSuccessFinishResult);
  }
};

// CommandObjectTypeCategoryDelete

class CommandObjectTypeCategoryDelete : public CommandObjectParsed {
public:
  CommandObjectTypeCategoryDelete(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "type category delete",
                            "Delete a category and all associated formatters.",
                            nullptr) {
    AddSimpleArgumentList(eArgTypeName, eArgRepeatPlus);
  }

  ~CommandObjectTypeCategoryDelete() override = default;

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    const size_t argc = command.GetArgumentCount();

    if (argc < 1) {
      result.AppendErrorWithFormat("%s takes 1 or more arg.\n",
                                   m_cmd_name.c_str());
      return;
    }

    bool success = true;

    // the order is not relevant here
    for (int i = argc - 1; i >= 0; i--) {
      const char *typeA = command.GetArgumentAtIndex(i);
      ConstString typeCS(typeA);

      if (!typeCS) {
        result.AppendError("empty category name not allowed");
        return;
      }
      if (!DataVisualization::Categories::Delete(typeCS))
        success = false; // keep deleting even if we hit an error
    }
    if (success) {
      result.SetStatus(eReturnStatusSuccessFinishResult);
    } else {
      result.AppendError("cannot delete one or more categories\n");
    }
  }
};

// CommandObjectTypeCategoryDisable
#define LLDB_OPTIONS_type_category_disable
#include "CommandOptions.inc"

class CommandObjectTypeCategoryDisable : public CommandObjectParsed {
  class CommandOptions : public Options {
  public:
    CommandOptions() = default;

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'l':
        if (!option_arg.empty()) {
          m_language = Language::GetLanguageTypeFromString(option_arg);
          if (m_language == lldb::eLanguageTypeUnknown)
            error.SetErrorStringWithFormat("unrecognized language '%s'",
                                           option_arg.str().c_str());
        }
        break;
      default:
        llvm_unreachable("Unimplemented option");
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_language = lldb::eLanguageTypeUnknown;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_type_category_disable_options);
    }

    // Instance variables to hold the values for command options.

    lldb::LanguageType m_language;
  };

  CommandOptions m_options;

  Options *GetOptions() override { return &m_options; }

public:
  CommandObjectTypeCategoryDisable(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "type category disable",
                            "Disable a category as a source of formatters.",
                            nullptr) {
    AddSimpleArgumentList(eArgTypeName, eArgRepeatPlus);
  }

  ~CommandObjectTypeCategoryDisable() override = default;

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    const size_t argc = command.GetArgumentCount();

    if (argc < 1 && m_options.m_language == lldb::eLanguageTypeUnknown) {
      result.AppendErrorWithFormat("%s takes arguments and/or a language",
                                   m_cmd_name.c_str());
      return;
    }

    if (argc == 1 && strcmp(command.GetArgumentAtIndex(0), "*") == 0) {
      DataVisualization::Categories::DisableStar();
    } else if (argc > 0) {
      // the order is not relevant here
      for (int i = argc - 1; i >= 0; i--) {
        const char *typeA = command.GetArgumentAtIndex(i);
        ConstString typeCS(typeA);

        if (!typeCS) {
          result.AppendError("empty category name not allowed");
          return;
        }
        DataVisualization::Categories::Disable(typeCS);
      }
    }

    if (m_options.m_language != lldb::eLanguageTypeUnknown)
      DataVisualization::Categories::Disable(m_options.m_language);

    result.SetStatus(eReturnStatusSuccessFinishResult);
  }
};

// CommandObjectTypeCategoryList

class CommandObjectTypeCategoryList : public CommandObjectParsed {
public:
  CommandObjectTypeCategoryList(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "type category list",
                            "Provide a list of all existing categories.",
                            nullptr) {
    AddSimpleArgumentList(eArgTypeName, eArgRepeatOptional);
  }

  ~CommandObjectTypeCategoryList() override = default;

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    if (request.GetCursorIndex())
      return;
    lldb_private::CommandCompletions::InvokeCommonCompletionCallbacks(
        GetCommandInterpreter(), lldb::eTypeCategoryNameCompletion, request,
        nullptr);
  }

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    const size_t argc = command.GetArgumentCount();

    std::unique_ptr<RegularExpression> regex;

    if (argc == 1) {
      const char *arg = command.GetArgumentAtIndex(0);
      regex = std::make_unique<RegularExpression>(arg);
      if (!regex->IsValid()) {
        result.AppendErrorWithFormat(
            "syntax error in category regular expression '%s'", arg);
        return;
      }
    } else if (argc != 0) {
      result.AppendErrorWithFormat("%s takes 0 or one arg.\n",
                                   m_cmd_name.c_str());
      return;
    }

    DataVisualization::Categories::ForEach(
        [&regex, &result](const lldb::TypeCategoryImplSP &category_sp) -> bool {
          if (regex) {
            bool escape = true;
            if (regex->GetText() == category_sp->GetName()) {
              escape = false;
            } else if (regex->Execute(category_sp->GetName())) {
              escape = false;
            }

            if (escape)
              return true;
          }

          result.GetOutputStream().Printf(
              "Category: %s\n", category_sp->GetDescription().c_str());

          return true;
        });

    result.SetStatus(eReturnStatusSuccessFinishResult);
  }
};

// CommandObjectTypeFilterList

class CommandObjectTypeFilterList
    : public CommandObjectTypeFormatterList<TypeFilterImpl> {
public:
  CommandObjectTypeFilterList(CommandInterpreter &interpreter)
      : CommandObjectTypeFormatterList(interpreter, "type filter list",
                                       "Show a list of current filters.") {}
};

// CommandObjectTypeSynthList

class CommandObjectTypeSynthList
    : public CommandObjectTypeFormatterList<SyntheticChildren> {
public:
  CommandObjectTypeSynthList(CommandInterpreter &interpreter)
      : CommandObjectTypeFormatterList(
            interpreter, "type synthetic list",
            "Show a list of current synthetic providers.") {}
};

// CommandObjectTypeFilterDelete

class CommandObjectTypeFilterDelete : public CommandObjectTypeFormatterDelete {
public:
  CommandObjectTypeFilterDelete(CommandInterpreter &interpreter)
      : CommandObjectTypeFormatterDelete(
            interpreter, eFormatCategoryItemFilter) {}

  ~CommandObjectTypeFilterDelete() override = default;
};

// CommandObjectTypeSynthDelete

class CommandObjectTypeSynthDelete : public CommandObjectTypeFormatterDelete {
public:
  CommandObjectTypeSynthDelete(CommandInterpreter &interpreter)
      : CommandObjectTypeFormatterDelete(
            interpreter, eFormatCategoryItemSynth) {}

  ~CommandObjectTypeSynthDelete() override = default;
};


// CommandObjectTypeFilterClear

class CommandObjectTypeFilterClear : public CommandObjectTypeFormatterClear {
public:
  CommandObjectTypeFilterClear(CommandInterpreter &interpreter)
      : CommandObjectTypeFormatterClear(interpreter, eFormatCategoryItemFilter,
                                        "type filter clear",
                                        "Delete all existing filter.") {}
};

// CommandObjectTypeSynthClear

class CommandObjectTypeSynthClear : public CommandObjectTypeFormatterClear {
public:
  CommandObjectTypeSynthClear(CommandInterpreter &interpreter)
      : CommandObjectTypeFormatterClear(
            interpreter, eFormatCategoryItemSynth, "type synthetic clear",
            "Delete all existing synthetic providers.") {}
};

bool CommandObjectTypeSynthAdd::Execute_HandwritePython(
    Args &command, CommandReturnObject &result) {
  auto options = std::make_unique<SynthAddOptions>(
      m_options.m_skip_pointers, m_options.m_skip_references,
      m_options.m_cascade, m_options.m_match_type, m_options.m_category);

  for (auto &entry : command.entries()) {
    if (entry.ref().empty()) {
      result.AppendError("empty typenames not allowed");
      return false;
    }

    options->m_target_types << std::string(entry.ref());
  }

  m_interpreter.GetPythonCommandsFromIOHandler(
      "    ",             // Prompt
      *this,              // IOHandlerDelegate
      options.release()); // Baton for the "io_handler" that will be passed back
                          // into our IOHandlerDelegate functions
  result.SetStatus(eReturnStatusSuccessFinishNoResult);
  return result.Succeeded();
}

bool CommandObjectTypeSynthAdd::Execute_PythonClass(
    Args &command, CommandReturnObject &result) {
  const size_t argc = command.GetArgumentCount();

  if (argc < 1) {
    result.AppendErrorWithFormat("%s takes one or more args.\n",
                                 m_cmd_name.c_str());
    return false;
  }

  if (m_options.m_class_name.empty() && !m_options.m_input_python) {
    result.AppendErrorWithFormat("%s needs either a Python class name or -P to "
                                 "directly input Python code.\n",
                                 m_cmd_name.c_str());
    return false;
  }

  SyntheticChildrenSP entry;

  ScriptedSyntheticChildren *impl = new ScriptedSyntheticChildren(
      SyntheticChildren::Flags()
          .SetCascades(m_options.m_cascade)
          .SetSkipPointers(m_options.m_skip_pointers)
          .SetSkipReferences(m_options.m_skip_references),
      m_options.m_class_name.c_str());

  entry.reset(impl);

  ScriptInterpreter *interpreter = GetDebugger().GetScriptInterpreter();

  if (interpreter &&
      !interpreter->CheckObjectExists(impl->GetPythonClassName()))
    result.AppendWarning("The provided class does not exist - please define it "
                         "before attempting to use this synthetic provider");

  // now I have a valid provider, let's add it to every type

  lldb::TypeCategoryImplSP category;
  DataVisualization::Categories::GetCategory(
      ConstString(m_options.m_category.c_str()), category);

  Status error;

  for (auto &arg_entry : command.entries()) {
    if (arg_entry.ref().empty()) {
      result.AppendError("empty typenames not allowed");
      return false;
    }

    ConstString typeCS(arg_entry.ref());
    if (!AddSynth(typeCS, entry, m_options.m_match_type, m_options.m_category,
                  &error)) {
      result.AppendError(error.AsCString());
      return false;
    }
  }

  result.SetStatus(eReturnStatusSuccessFinishNoResult);
  return result.Succeeded();
}

CommandObjectTypeSynthAdd::CommandObjectTypeSynthAdd(
    CommandInterpreter &interpreter)
    : CommandObjectParsed(interpreter, "type synthetic add",
                          "Add a new synthetic provider for a type.", nullptr),
      IOHandlerDelegateMultiline("DONE"), m_options() {
  AddSimpleArgumentList(eArgTypeName, eArgRepeatPlus);
}

bool CommandObjectTypeSynthAdd::AddSynth(ConstString type_name,
                                         SyntheticChildrenSP entry,
                                         FormatterMatchType match_type,
                                         std::string category_name,
                                         Status *error) {
  lldb::TypeCategoryImplSP category;
  DataVisualization::Categories::GetCategory(ConstString(category_name.c_str()),
                                             category);

  if (match_type == eFormatterMatchExact) {
    if (FixArrayTypeNameWithRegex(type_name))
      match_type = eFormatterMatchRegex;
  }

  // Only check for conflicting filters in the same category if `type_name` is
  // an actual type name. Matching a regex string against registered regexes
  // doesn't work.
  if (match_type == eFormatterMatchExact) {
    // It's not generally possible to get a type object here. For example, this
    // command can be run before loading any binaries. Do just a best-effort
    // name-based lookup here to try to prevent conflicts.
    FormattersMatchCandidate candidate_type(type_name, nullptr, TypeImpl(),
                                            FormattersMatchCandidate::Flags());
    if (category->AnyMatches(candidate_type, eFormatCategoryItemFilter,
                             false)) {
      if (error)
        error->SetErrorStringWithFormat("cannot add synthetic for type %s when "
                                        "filter is defined in same category!",
                                        type_name.AsCString());
      return false;
    }
  }

  if (match_type == eFormatterMatchRegex) {
    RegularExpression typeRX(type_name.GetStringRef());
    if (!typeRX.IsValid()) {
      if (error)
        error->SetErrorString(
            "regex format error (maybe this is not really a regex?)");
      return false;
    }
  }

  if (match_type == eFormatterMatchCallback) {
    const char *function_name = type_name.AsCString();
    ScriptInterpreter *interpreter = GetDebugger().GetScriptInterpreter();
    if (interpreter && !interpreter->CheckObjectExists(function_name)) {
      error->SetErrorStringWithFormat(
          "The provided recognizer function \"%s\" does not exist - "
          "please define it before attempting to use this summary.\n",
          function_name);
      return false;
    }
  }

  category->AddTypeSynthetic(type_name.GetStringRef(), match_type, entry);
  return true;
}

#define LLDB_OPTIONS_type_filter_add
#include "CommandOptions.inc"

class CommandObjectTypeFilterAdd : public CommandObjectParsed {
private:
  class CommandOptions : public Options {
    typedef std::vector<std::string> option_vector;

  public:
    CommandOptions() = default;

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;
      bool success;

      switch (short_option) {
      case 'C':
        m_cascade = OptionArgParser::ToBoolean(option_arg, true, &success);
        if (!success)
          error.SetErrorStringWithFormat("invalid value for cascade: %s",
                                         option_arg.str().c_str());
        break;
      case 'c':
        m_expr_paths.push_back(std::string(option_arg));
        has_child_list = true;
        break;
      case 'p':
        m_skip_pointers = true;
        break;
      case 'r':
        m_skip_references = true;
        break;
      case 'w':
        m_category = std::string(option_arg);
        break;
      case 'x':
        m_regex = true;
        break;
      default:
        llvm_unreachable("Unimplemented option");
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_cascade = true;
      m_skip_pointers = false;
      m_skip_references = false;
      m_category = "default";
      m_expr_paths.clear();
      has_child_list = false;
      m_regex = false;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_type_filter_add_options);
    }

    // Instance variables to hold the values for command options.

    bool m_cascade;
    bool m_skip_references;
    bool m_skip_pointers;
    bool m_input_python;
    option_vector m_expr_paths;
    std::string m_category;
    bool has_child_list;
    bool m_regex;

    typedef option_vector::iterator ExpressionPathsIterator;
  };

  CommandOptions m_options;

  Options *GetOptions() override { return &m_options; }

  enum FilterFormatType { eRegularFilter, eRegexFilter };

  bool AddFilter(ConstString type_name, TypeFilterImplSP entry,
                 FilterFormatType type, std::string category_name,
                 Status *error) {
    lldb::TypeCategoryImplSP category;
    DataVisualization::Categories::GetCategory(
        ConstString(category_name.c_str()), category);

    if (type == eRegularFilter) {
      if (FixArrayTypeNameWithRegex(type_name))
        type = eRegexFilter;
    }

    // Only check for conflicting synthetic child providers in the same category
    // if `type_name` is an actual type name. Matching a regex string against
    // registered regexes doesn't work.
    if (type == eRegularFilter) {
      // It's not generally possible to get a type object here. For example,
      // this command can be run before loading any binaries. Do just a
      // best-effort name-based lookup here to try to prevent conflicts.
      FormattersMatchCandidate candidate_type(
          type_name, nullptr, TypeImpl(), FormattersMatchCandidate::Flags());
      lldb::SyntheticChildrenSP entry;
      if (category->AnyMatches(candidate_type, eFormatCategoryItemSynth,
                               false)) {
        if (error)
          error->SetErrorStringWithFormat("cannot add filter for type %s when "
                                          "synthetic is defined in same "
                                          "category!",
                                          type_name.AsCString());
        return false;
      }
    }

    FormatterMatchType match_type = eFormatterMatchExact;
    if (type == eRegexFilter) {
      match_type = eFormatterMatchRegex;
      RegularExpression typeRX(type_name.GetStringRef());
      if (!typeRX.IsValid()) {
        if (error)
          error->SetErrorString(
              "regex format error (maybe this is not really a regex?)");
        return false;
      }
    }
    category->AddTypeFilter(type_name.GetStringRef(), match_type, entry);
    return true;
  }

public:
  CommandObjectTypeFilterAdd(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "type filter add",
                            "Add a new filter for a type.", nullptr) {
    AddSimpleArgumentList(eArgTypeName, eArgRepeatPlus);

    SetHelpLong(
        R"(
The following examples of 'type filter add' refer to this code snippet for context:

    class Foo {
        int a;
        int b;
        int c;
        int d;
        int e;
        int f;
        int g;
        int h;
        int i;
    }
    Foo my_foo;

Adding a simple filter:

(lldb) type filter add --child a --child g Foo
(lldb) frame variable my_foo

)"
        "Produces output where only a and g are displayed.  Other children of my_foo \
(b, c, d, e, f, h and i) are available by asking for them explicitly:"
        R"(

(lldb) frame variable my_foo.b my_foo.c my_foo.i

)"
        "The formatting option --raw on frame variable bypasses the filter, showing \
all children of my_foo as if no filter was defined:"
        R"(

(lldb) frame variable my_foo --raw)");
  }

  ~CommandObjectTypeFilterAdd() override = default;

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    const size_t argc = command.GetArgumentCount();

    if (argc < 1) {
      result.AppendErrorWithFormat("%s takes one or more args.\n",
                                   m_cmd_name.c_str());
      return;
    }

    if (m_options.m_expr_paths.empty()) {
      result.AppendErrorWithFormat("%s needs one or more children.\n",
                                   m_cmd_name.c_str());
      return;
    }

    TypeFilterImplSP entry(new TypeFilterImpl(
        SyntheticChildren::Flags()
            .SetCascades(m_options.m_cascade)
            .SetSkipPointers(m_options.m_skip_pointers)
            .SetSkipReferences(m_options.m_skip_references)));

    // go through the expression paths
    CommandOptions::ExpressionPathsIterator begin,
        end = m_options.m_expr_paths.end();

    for (begin = m_options.m_expr_paths.begin(); begin != end; begin++)
      entry->AddExpressionPath(*begin);

    // now I have a valid provider, let's add it to every type

    lldb::TypeCategoryImplSP category;
    DataVisualization::Categories::GetCategory(
        ConstString(m_options.m_category.c_str()), category);

    Status error;

    WarnOnPotentialUnquotedUnsignedType(command, result);

    for (auto &arg_entry : command.entries()) {
      if (arg_entry.ref().empty()) {
        result.AppendError("empty typenames not allowed");
        return;
      }

      ConstString typeCS(arg_entry.ref());
      if (!AddFilter(typeCS, entry,
                     m_options.m_regex ? eRegexFilter : eRegularFilter,
                     m_options.m_category, &error)) {
        result.AppendError(error.AsCString());
        return;
      }
    }

    result.SetStatus(eReturnStatusSuccessFinishNoResult);
  }
};

// "type lookup"
#define LLDB_OPTIONS_type_lookup
#include "CommandOptions.inc"

class CommandObjectTypeLookup : public CommandObjectRaw {
protected:
  // this function is allowed to do a more aggressive job at guessing languages
  // than the expression parser is comfortable with - so leave the original
  // call alone and add one that is specific to type lookup
  lldb::LanguageType GuessLanguage(StackFrame *frame) {
    lldb::LanguageType lang_type = lldb::eLanguageTypeUnknown;

    if (!frame)
      return lang_type;

    lang_type = frame->GuessLanguage().AsLanguageType();
    if (lang_type != lldb::eLanguageTypeUnknown)
      return lang_type;

    Symbol *s = frame->GetSymbolContext(eSymbolContextSymbol).symbol;
    if (s)
      lang_type = s->GetMangled().GuessLanguage();

    return lang_type;
  }

  class CommandOptions : public OptionGroup {
  public:
    CommandOptions() = default;

    ~CommandOptions() override = default;

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_type_lookup_options);
    }

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_value,
                          ExecutionContext *execution_context) override {
      Status error;

      const int short_option = g_type_lookup_options[option_idx].short_option;

      switch (short_option) {
      case 'h':
        m_show_help = true;
        break;

      case 'l':
        m_language = Language::GetLanguageTypeFromString(option_value);
        break;

      default:
        llvm_unreachable("Unimplemented option");
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_show_help = false;
      m_language = eLanguageTypeUnknown;
    }

    // Options table: Required for subclasses of Options.

    bool m_show_help = false;
    lldb::LanguageType m_language = eLanguageTypeUnknown;
  };

  OptionGroupOptions m_option_group;
  CommandOptions m_command_options;

public:
  CommandObjectTypeLookup(CommandInterpreter &interpreter)
      : CommandObjectRaw(interpreter, "type lookup",
                         "Lookup types and declarations in the current target, "
                         "following language-specific naming conventions.",
                         "type lookup <type-specifier>",
                         eCommandRequiresTarget) {
    m_option_group.Append(&m_command_options);
    m_option_group.Finalize();
  }

  ~CommandObjectTypeLookup() override = default;

  Options *GetOptions() override { return &m_option_group; }

  llvm::StringRef GetHelpLong() override {
    if (!m_cmd_help_long.empty())
      return m_cmd_help_long;

    StreamString stream;
    Language::ForEach([&](Language *lang) {
      if (const char *help = lang->GetLanguageSpecificTypeLookupHelp())
        stream.Printf("%s\n", help);
      return true;
    });

    m_cmd_help_long = std::string(stream.GetString());
    return m_cmd_help_long;
  }

  void DoExecute(llvm::StringRef raw_command_line,
                 CommandReturnObject &result) override {
    if (raw_command_line.empty()) {
      result.AppendError(
          "type lookup cannot be invoked without a type name as argument");
      return;
    }

    auto exe_ctx = GetCommandInterpreter().GetExecutionContext();
    m_option_group.NotifyOptionParsingStarting(&exe_ctx);

    OptionsWithRaw args(raw_command_line);
    const char *name_of_type = args.GetRawPart().c_str();

    if (args.HasArgs())
      if (!ParseOptionsAndNotify(args.GetArgs(), result, m_option_group,
                                 exe_ctx))
        return;

    ExecutionContextScope *best_scope = exe_ctx.GetBestExecutionContextScope();

    bool any_found = false;

    std::vector<Language *> languages;

    bool is_global_search = false;
    LanguageType guessed_language = lldb::eLanguageTypeUnknown;

    if ((is_global_search =
             (m_command_options.m_language == eLanguageTypeUnknown))) {
      Language::ForEach([&](Language *lang) {
        languages.push_back(lang);
        return true;
      });
    } else {
      languages.push_back(Language::FindPlugin(m_command_options.m_language));
    }

    // This is not the most efficient way to do this, but we support very few
    // languages so the cost of the sort is going to be dwarfed by the actual
    // lookup anyway
    if (StackFrame *frame = m_exe_ctx.GetFramePtr()) {
      guessed_language = GuessLanguage(frame);
      if (guessed_language != eLanguageTypeUnknown) {
        llvm::sort(
            languages.begin(), languages.end(),
            [guessed_language](Language *lang1, Language *lang2) -> bool {
              if (!lang1 || !lang2)
                return false;
              LanguageType lt1 = lang1->GetLanguageType();
              LanguageType lt2 = lang2->GetLanguageType();
              if (lt1 == guessed_language)
                return true; // make the selected frame's language come first
              if (lt2 == guessed_language)
                return false; // make the selected frame's language come first
              return (lt1 < lt2); // normal comparison otherwise
            });
      }
    }

    bool is_first_language = true;

    for (Language *language : languages) {
      if (!language)
        continue;

      if (auto scavenger = language->GetTypeScavenger()) {
        Language::TypeScavenger::ResultSet search_results;
        if (scavenger->Find(best_scope, name_of_type, search_results) > 0) {
          for (const auto &search_result : search_results) {
            if (search_result && search_result->IsValid()) {
              any_found = true;
              search_result->DumpToStream(result.GetOutputStream(),
                                          this->m_command_options.m_show_help);
            }
          }
        }
      }
      // this is "type lookup SomeName" and we did find a match, so get out
      if (any_found && is_global_search)
        break;
      else if (is_first_language && is_global_search &&
               guessed_language != lldb::eLanguageTypeUnknown) {
        is_first_language = false;
        result.GetOutputStream().Printf(
            "no type was found in the current language %s matching '%s'; "
            "performing a global search across all languages\n",
            Language::GetNameForLanguageType(guessed_language), name_of_type);
      }
    }

    if (!any_found)
      result.AppendMessageWithFormat("no type was found matching '%s'\n",
                                     name_of_type);

    result.SetStatus(any_found ? lldb::eReturnStatusSuccessFinishResult
                               : lldb::eReturnStatusSuccessFinishNoResult);
  }
};

template <typename FormatterType>
class CommandObjectFormatterInfo : public CommandObjectRaw {
public:
  typedef std::function<typename FormatterType::SharedPointer(ValueObject &)>
      DiscoveryFunction;
  CommandObjectFormatterInfo(CommandInterpreter &interpreter,
                             const char *formatter_name,
                             DiscoveryFunction discovery_func)
      : CommandObjectRaw(interpreter, "", "", "", eCommandRequiresFrame),
        m_formatter_name(formatter_name ? formatter_name : ""),
        m_discovery_function(discovery_func) {
    StreamString name;
    name.Printf("type %s info", formatter_name);
    SetCommandName(name.GetString());
    StreamString help;
    help.Printf("This command evaluates the provided expression and shows "
                "which %s is applied to the resulting value (if any).",
                formatter_name);
    SetHelp(help.GetString());
    StreamString syntax;
    syntax.Printf("type %s info <expr>", formatter_name);
    SetSyntax(syntax.GetString());
  }

  ~CommandObjectFormatterInfo() override = default;

protected:
  void DoExecute(llvm::StringRef command,
                 CommandReturnObject &result) override {
    TargetSP target_sp = GetDebugger().GetSelectedTarget();
    Thread *thread = GetDefaultThread();
    if (!thread) {
      result.AppendError("no default thread");
      return;
    }

    StackFrameSP frame_sp =
        thread->GetSelectedFrame(DoNoSelectMostRelevantFrame);
    ValueObjectSP result_valobj_sp;
    EvaluateExpressionOptions options;
    lldb::ExpressionResults expr_result = target_sp->EvaluateExpression(
        command, frame_sp.get(), result_valobj_sp, options);
    if (expr_result == eExpressionCompleted && result_valobj_sp) {
      result_valobj_sp =
          result_valobj_sp->GetQualifiedRepresentationIfAvailable(
              target_sp->GetPreferDynamicValue(),
              target_sp->GetEnableSyntheticValue());
      typename FormatterType::SharedPointer formatter_sp =
          m_discovery_function(*result_valobj_sp);
      if (formatter_sp) {
        std::string description(formatter_sp->GetDescription());
        result.GetOutputStream()
            << m_formatter_name << " applied to ("
            << result_valobj_sp->GetDisplayTypeName().AsCString("<unknown>")
            << ") " << command << " is: " << description << "\n";
        result.SetStatus(lldb::eReturnStatusSuccessFinishResult);
      } else {
        result.GetOutputStream()
            << "no " << m_formatter_name << " applies to ("
            << result_valobj_sp->GetDisplayTypeName().AsCString("<unknown>")
            << ") " << command << "\n";
        result.SetStatus(lldb::eReturnStatusSuccessFinishNoResult);
      }
    } else {
      result.AppendError("failed to evaluate expression");
    }
  }

private:
  std::string m_formatter_name;
  DiscoveryFunction m_discovery_function;
};

class CommandObjectTypeFormat : public CommandObjectMultiword {
public:
  CommandObjectTypeFormat(CommandInterpreter &interpreter)
      : CommandObjectMultiword(
            interpreter, "type format",
            "Commands for customizing value display formats.",
            "type format [<sub-command-options>] ") {
    LoadSubCommand(
        "add", CommandObjectSP(new CommandObjectTypeFormatAdd(interpreter)));
    LoadSubCommand("clear", CommandObjectSP(
                                new CommandObjectTypeFormatClear(interpreter)));
    LoadSubCommand("delete", CommandObjectSP(new CommandObjectTypeFormatDelete(
                                 interpreter)));
    LoadSubCommand(
        "list", CommandObjectSP(new CommandObjectTypeFormatList(interpreter)));
    LoadSubCommand(
        "info", CommandObjectSP(new CommandObjectFormatterInfo<TypeFormatImpl>(
                    interpreter, "format",
                    [](ValueObject &valobj) -> TypeFormatImpl::SharedPointer {
                      return valobj.GetValueFormat();
                    })));
  }

  ~CommandObjectTypeFormat() override = default;
};

class CommandObjectTypeSynth : public CommandObjectMultiword {
public:
  CommandObjectTypeSynth(CommandInterpreter &interpreter)
      : CommandObjectMultiword(
            interpreter, "type synthetic",
            "Commands for operating on synthetic type representations.",
            "type synthetic [<sub-command-options>] ") {
    LoadSubCommand("add",
                   CommandObjectSP(new CommandObjectTypeSynthAdd(interpreter)));
    LoadSubCommand(
        "clear", CommandObjectSP(new CommandObjectTypeSynthClear(interpreter)));
    LoadSubCommand("delete", CommandObjectSP(new CommandObjectTypeSynthDelete(
                                 interpreter)));
    LoadSubCommand(
        "list", CommandObjectSP(new CommandObjectTypeSynthList(interpreter)));
    LoadSubCommand(
        "info",
        CommandObjectSP(new CommandObjectFormatterInfo<SyntheticChildren>(
            interpreter, "synthetic",
            [](ValueObject &valobj) -> SyntheticChildren::SharedPointer {
              return valobj.GetSyntheticChildren();
            })));
  }

  ~CommandObjectTypeSynth() override = default;
};

class CommandObjectTypeFilter : public CommandObjectMultiword {
public:
  CommandObjectTypeFilter(CommandInterpreter &interpreter)
      : CommandObjectMultiword(interpreter, "type filter",
                               "Commands for operating on type filters.",
                               "type filter [<sub-command-options>] ") {
    LoadSubCommand(
        "add", CommandObjectSP(new CommandObjectTypeFilterAdd(interpreter)));
    LoadSubCommand("clear", CommandObjectSP(
                                new CommandObjectTypeFilterClear(interpreter)));
    LoadSubCommand("delete", CommandObjectSP(new CommandObjectTypeFilterDelete(
                                 interpreter)));
    LoadSubCommand(
        "list", CommandObjectSP(new CommandObjectTypeFilterList(interpreter)));
  }

  ~CommandObjectTypeFilter() override = default;
};

class CommandObjectTypeCategory : public CommandObjectMultiword {
public:
  CommandObjectTypeCategory(CommandInterpreter &interpreter)
      : CommandObjectMultiword(interpreter, "type category",
                               "Commands for operating on type categories.",
                               "type category [<sub-command-options>] ") {
    LoadSubCommand(
        "define",
        CommandObjectSP(new CommandObjectTypeCategoryDefine(interpreter)));
    LoadSubCommand(
        "enable",
        CommandObjectSP(new CommandObjectTypeCategoryEnable(interpreter)));
    LoadSubCommand(
        "disable",
        CommandObjectSP(new CommandObjectTypeCategoryDisable(interpreter)));
    LoadSubCommand(
        "delete",
        CommandObjectSP(new CommandObjectTypeCategoryDelete(interpreter)));
    LoadSubCommand("list", CommandObjectSP(
                               new CommandObjectTypeCategoryList(interpreter)));
  }

  ~CommandObjectTypeCategory() override = default;
};

class CommandObjectTypeSummary : public CommandObjectMultiword {
public:
  CommandObjectTypeSummary(CommandInterpreter &interpreter)
      : CommandObjectMultiword(
            interpreter, "type summary",
            "Commands for editing variable summary display options.",
            "type summary [<sub-command-options>] ") {
    LoadSubCommand(
        "add", CommandObjectSP(new CommandObjectTypeSummaryAdd(interpreter)));
    LoadSubCommand("clear", CommandObjectSP(new CommandObjectTypeSummaryClear(
                                interpreter)));
    LoadSubCommand("delete", CommandObjectSP(new CommandObjectTypeSummaryDelete(
                                 interpreter)));
    LoadSubCommand(
        "list", CommandObjectSP(new CommandObjectTypeSummaryList(interpreter)));
    LoadSubCommand(
        "info", CommandObjectSP(new CommandObjectFormatterInfo<TypeSummaryImpl>(
                    interpreter, "summary",
                    [](ValueObject &valobj) -> TypeSummaryImpl::SharedPointer {
                      return valobj.GetSummaryFormat();
                    })));
  }

  ~CommandObjectTypeSummary() override = default;
};

// CommandObjectType

CommandObjectType::CommandObjectType(CommandInterpreter &interpreter)
    : CommandObjectMultiword(interpreter, "type",
                             "Commands for operating on the type system.",
                             "type [<sub-command-options>]") {
  LoadSubCommand("category",
                 CommandObjectSP(new CommandObjectTypeCategory(interpreter)));
  LoadSubCommand("filter",
                 CommandObjectSP(new CommandObjectTypeFilter(interpreter)));
  LoadSubCommand("format",
                 CommandObjectSP(new CommandObjectTypeFormat(interpreter)));
  LoadSubCommand("summary",
                 CommandObjectSP(new CommandObjectTypeSummary(interpreter)));
  LoadSubCommand("synthetic",
                 CommandObjectSP(new CommandObjectTypeSynth(interpreter)));
  LoadSubCommand("lookup",
                 CommandObjectSP(new CommandObjectTypeLookup(interpreter)));
}

CommandObjectType::~CommandObjectType() = default;
