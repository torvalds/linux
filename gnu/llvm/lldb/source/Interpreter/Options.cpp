//===-- Options.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/Options.h"

#include <algorithm>
#include <bitset>
#include <map>
#include <set>

#include "lldb/Host/OptionParser.h"
#include "lldb/Interpreter/CommandCompletions.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandObject.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/StreamString.h"
#include "llvm/ADT/STLExtras.h"

using namespace lldb;
using namespace lldb_private;

// Options
Options::Options() { BuildValidOptionSets(); }

Options::~Options() = default;

void Options::NotifyOptionParsingStarting(ExecutionContext *execution_context) {
  m_seen_options.clear();
  // Let the subclass reset its option values
  OptionParsingStarting(execution_context);
}

Status
Options::NotifyOptionParsingFinished(ExecutionContext *execution_context) {
  return OptionParsingFinished(execution_context);
}

void Options::OptionSeen(int option_idx) { m_seen_options.insert(option_idx); }

// Returns true is set_a is a subset of set_b;  Otherwise returns false.

bool Options::IsASubset(const OptionSet &set_a, const OptionSet &set_b) {
  bool is_a_subset = true;
  OptionSet::const_iterator pos_a;
  OptionSet::const_iterator pos_b;

  // set_a is a subset of set_b if every member of set_a is also a member of
  // set_b

  for (pos_a = set_a.begin(); pos_a != set_a.end() && is_a_subset; ++pos_a) {
    pos_b = set_b.find(*pos_a);
    if (pos_b == set_b.end())
      is_a_subset = false;
  }

  return is_a_subset;
}

// Returns the set difference set_a - set_b, i.e. { x | ElementOf (x, set_a) &&
// !ElementOf (x, set_b) }

size_t Options::OptionsSetDiff(const OptionSet &set_a, const OptionSet &set_b,
                               OptionSet &diffs) {
  size_t num_diffs = 0;
  OptionSet::const_iterator pos_a;
  OptionSet::const_iterator pos_b;

  for (pos_a = set_a.begin(); pos_a != set_a.end(); ++pos_a) {
    pos_b = set_b.find(*pos_a);
    if (pos_b == set_b.end()) {
      ++num_diffs;
      diffs.insert(*pos_a);
    }
  }

  return num_diffs;
}

// Returns the union of set_a and set_b.  Does not put duplicate members into
// the union.

void Options::OptionsSetUnion(const OptionSet &set_a, const OptionSet &set_b,
                              OptionSet &union_set) {
  OptionSet::const_iterator pos;
  OptionSet::iterator pos_union;

  // Put all the elements of set_a into the union.

  for (pos = set_a.begin(); pos != set_a.end(); ++pos)
    union_set.insert(*pos);

  // Put all the elements of set_b that are not already there into the union.
  for (pos = set_b.begin(); pos != set_b.end(); ++pos) {
    pos_union = union_set.find(*pos);
    if (pos_union == union_set.end())
      union_set.insert(*pos);
  }
}

bool Options::VerifyOptions(CommandReturnObject &result) {
  bool options_are_valid = false;

  int num_levels = GetRequiredOptions().size();
  if (num_levels) {
    for (int i = 0; i < num_levels && !options_are_valid; ++i) {
      // This is the correct set of options if:  1). m_seen_options contains
      // all of m_required_options[i] (i.e. all the required options at this
      // level are a subset of m_seen_options); AND 2). { m_seen_options -
      // m_required_options[i] is a subset of m_options_options[i] (i.e. all
      // the rest of m_seen_options are in the set of optional options at this
      // level.

      // Check to see if all of m_required_options[i] are a subset of
      // m_seen_options
      if (IsASubset(GetRequiredOptions()[i], m_seen_options)) {
        // Construct the set difference: remaining_options = {m_seen_options} -
        // {m_required_options[i]}
        OptionSet remaining_options;
        OptionsSetDiff(m_seen_options, GetRequiredOptions()[i],
                       remaining_options);
        // Check to see if remaining_options is a subset of
        // m_optional_options[i]
        if (IsASubset(remaining_options, GetOptionalOptions()[i]))
          options_are_valid = true;
      }
    }
  } else {
    options_are_valid = true;
  }

  if (options_are_valid) {
    result.SetStatus(eReturnStatusSuccessFinishNoResult);
  } else {
    result.AppendError("invalid combination of options for the given command");
  }

  return options_are_valid;
}

// This is called in the Options constructor, though we could call it lazily if
// that ends up being a performance problem.

void Options::BuildValidOptionSets() {
  // Check to see if we already did this.
  if (m_required_options.size() != 0)
    return;

  // Check to see if there are any options.
  int num_options = NumCommandOptions();
  if (num_options == 0)
    return;

  auto opt_defs = GetDefinitions();
  m_required_options.resize(1);
  m_optional_options.resize(1);

  // First count the number of option sets we've got.  Ignore
  // LLDB_ALL_OPTION_SETS...

  uint32_t num_option_sets = 0;

  for (const auto &def : opt_defs) {
    uint32_t this_usage_mask = def.usage_mask;
    if (this_usage_mask == LLDB_OPT_SET_ALL) {
      if (num_option_sets == 0)
        num_option_sets = 1;
    } else {
      for (uint32_t j = 0; j < LLDB_MAX_NUM_OPTION_SETS; j++) {
        if (this_usage_mask & (1 << j)) {
          if (num_option_sets <= j)
            num_option_sets = j + 1;
        }
      }
    }
  }

  if (num_option_sets > 0) {
    m_required_options.resize(num_option_sets);
    m_optional_options.resize(num_option_sets);

    for (const auto &def : opt_defs) {
      for (uint32_t j = 0; j < num_option_sets; j++) {
        if (def.usage_mask & 1 << j) {
          if (def.required)
            m_required_options[j].insert(def.short_option);
          else
            m_optional_options[j].insert(def.short_option);
        }
      }
    }
  }
}

uint32_t Options::NumCommandOptions() { return GetDefinitions().size(); }

Option *Options::GetLongOptions() {
  // Check to see if this has already been done.
  if (m_getopt_table.empty()) {
    auto defs = GetDefinitions();
    if (defs.empty())
      return nullptr;

    std::map<int, uint32_t> option_seen;

    m_getopt_table.resize(defs.size() + 1);
    for (size_t i = 0; i < defs.size(); ++i) {
      const int short_opt = defs[i].short_option;

      m_getopt_table[i].definition = &defs[i];
      m_getopt_table[i].flag = nullptr;
      m_getopt_table[i].val = short_opt;

      if (option_seen.find(short_opt) == option_seen.end()) {
        option_seen[short_opt] = i;
      } else if (short_opt) {
        m_getopt_table[i].val = 0;
        std::map<int, uint32_t>::const_iterator pos =
            option_seen.find(short_opt);
        StreamString strm;
        if (defs[i].HasShortOption())
          Debugger::ReportError(
              llvm::formatv(
                  "option[{0}] --{1} has a short option -{2} that "
                  "conflicts with option[{3}] --{4}, short option won't "
                  "be used for --{5}",
                  i, defs[i].long_option, short_opt, pos->second,
                  m_getopt_table[pos->second].definition->long_option,
                  defs[i].long_option)
                  .str());
        else
          Debugger::ReportError(
              llvm::formatv(
                  "option[{0}] --{1} has a short option {2:x} that "
                  "conflicts with option[{3}] --{4}, short option won't "
                  "be used for --{5}",
                  (int)i, defs[i].long_option, short_opt, pos->second,
                  m_getopt_table[pos->second].definition->long_option,
                  defs[i].long_option)
                  .str());
      }
    }

    // getopt_long_only requires a NULL final entry in the table:

    m_getopt_table.back().definition = nullptr;
    m_getopt_table.back().flag = nullptr;
    m_getopt_table.back().val = 0;
  }

  if (m_getopt_table.empty())
    return nullptr;

  return &m_getopt_table.front();
}

// This function takes INDENT, which tells how many spaces to output at the
// front of each line; SPACES, which is a string containing 80 spaces; and
// TEXT, which is the text that is to be output.   It outputs the text, on
// multiple lines if necessary, to RESULT, with INDENT spaces at the front of
// each line.  It breaks lines on spaces, tabs or newlines, shortening the line
// if necessary to not break in the middle of a word.  It assumes that each
// output line should contain a maximum of OUTPUT_MAX_COLUMNS characters.

void Options::OutputFormattedUsageText(Stream &strm,
                                       const OptionDefinition &option_def,
                                       uint32_t output_max_columns) {
  std::string actual_text;
  if (option_def.validator) {
    const char *condition = option_def.validator->ShortConditionString();
    if (condition) {
      actual_text = "[";
      actual_text.append(condition);
      actual_text.append("] ");
    }
  }
  actual_text.append(option_def.usage_text);

  // Will it all fit on one line?

  if (static_cast<uint32_t>(actual_text.length() + strm.GetIndentLevel()) <
      output_max_columns) {
    // Output it as a single line.
    strm.Indent(actual_text);
    strm.EOL();
  } else {
    // We need to break it up into multiple lines.

    int text_width = output_max_columns - strm.GetIndentLevel() - 1;
    int start = 0;
    int end = start;
    int final_end = actual_text.length();
    int sub_len;

    while (end < final_end) {
      // Don't start the 'text' on a space, since we're already outputting the
      // indentation.
      while ((start < final_end) && (actual_text[start] == ' '))
        start++;

      end = start + text_width;
      if (end > final_end)
        end = final_end;
      else {
        // If we're not at the end of the text, make sure we break the line on
        // white space.
        while (end > start && actual_text[end] != ' ' &&
               actual_text[end] != '\t' && actual_text[end] != '\n')
          end--;
      }

      sub_len = end - start;
      if (start != 0)
        strm.EOL();
      strm.Indent();
      assert(start < final_end);
      assert(start + sub_len <= final_end);
      strm.Write(actual_text.c_str() + start, sub_len);
      start = end + 1;
    }
    strm.EOL();
  }
}

bool Options::SupportsLongOption(const char *long_option) {
  if (!long_option || !long_option[0])
    return false;

  auto opt_defs = GetDefinitions();
  if (opt_defs.empty())
    return false;

  const char *long_option_name = long_option;
  if (long_option[0] == '-' && long_option[1] == '-')
    long_option_name += 2;

  for (auto &def : opt_defs) {
    if (!def.long_option)
      continue;

    if (strcmp(def.long_option, long_option_name) == 0)
      return true;
  }

  return false;
}

enum OptionDisplayType {
  eDisplayBestOption,
  eDisplayShortOption,
  eDisplayLongOption
};

static bool PrintOption(const OptionDefinition &opt_def,
                        OptionDisplayType display_type, const char *header,
                        const char *footer, bool show_optional, Stream &strm) {
  if (display_type == eDisplayShortOption && !opt_def.HasShortOption())
    return false;

  if (header && header[0])
    strm.PutCString(header);

  if (show_optional && !opt_def.required)
    strm.PutChar('[');
  const bool show_short_option =
      opt_def.HasShortOption() && display_type != eDisplayLongOption;
  if (show_short_option)
    strm.Printf("-%c", opt_def.short_option);
  else
    strm.Printf("--%s", opt_def.long_option);
  switch (opt_def.option_has_arg) {
  case OptionParser::eNoArgument:
    break;
  case OptionParser::eRequiredArgument:
    strm.Printf(" <%s>", CommandObject::GetArgumentName(opt_def.argument_type));
    break;

  case OptionParser::eOptionalArgument:
    strm.Printf("%s[<%s>]", show_short_option ? "" : "=",
                CommandObject::GetArgumentName(opt_def.argument_type));
    break;
  }
  if (show_optional && !opt_def.required)
    strm.PutChar(']');
  if (footer && footer[0])
    strm.PutCString(footer);
  return true;
}

void Options::GenerateOptionUsage(Stream &strm, CommandObject &cmd,
                                  uint32_t screen_width) {
  auto opt_defs = GetDefinitions();
  const uint32_t save_indent_level = strm.GetIndentLevel();
  llvm::StringRef name = cmd.GetCommandName();
  StreamString arguments_str;
  cmd.GetFormattedCommandArguments(arguments_str);

  const uint32_t num_options = NumCommandOptions();
  if (num_options == 0)
    return;

  const bool only_print_args = cmd.IsDashDashCommand();
  if (!only_print_args)
    strm.PutCString("\nCommand Options Usage:\n");

  strm.IndentMore(2);

  // First, show each usage level set of options, e.g. <cmd> [options-for-
  // level-0]
  //                                                   <cmd>
  //                                                   [options-for-level-1]
  //                                                   etc.

  if (!only_print_args) {
    uint32_t num_option_sets = GetRequiredOptions().size();
    for (uint32_t opt_set = 0; opt_set < num_option_sets; ++opt_set) {
      if (opt_set > 0)
        strm.Printf("\n");
      strm.Indent(name);

      // Different option sets may require different args.
      StreamString args_str;
      uint32_t opt_set_mask = 1 << opt_set;
      cmd.GetFormattedCommandArguments(args_str, opt_set_mask);

      // First go through and print all options that take no arguments as a
      // single string. If a command has "-a" "-b" and "-c", this will show up
      // as [-abc]

      // We use a set here so that they will be sorted.
      std::set<int> required_options;
      std::set<int> optional_options;

      for (auto &def : opt_defs) {
        if (def.usage_mask & opt_set_mask && def.HasShortOption() &&
            def.option_has_arg == OptionParser::eNoArgument) {
          if (def.required) {
            required_options.insert(def.short_option);
          } else {
            optional_options.insert(def.short_option);
          }
        }
      }

      if (!required_options.empty()) {
        strm.PutCString(" -");
        for (int short_option : required_options)
          strm.PutChar(short_option);
      }

      if (!optional_options.empty()) {
        strm.PutCString(" [-");
        for (int short_option : optional_options)
          strm.PutChar(short_option);
        strm.PutChar(']');
      }

      // First go through and print the required options (list them up front).
      for (auto &def : opt_defs) {
        if (def.usage_mask & opt_set_mask && def.HasShortOption() &&
            def.required && def.option_has_arg != OptionParser::eNoArgument)
          PrintOption(def, eDisplayBestOption, " ", nullptr, true, strm);
      }

      // Now go through again, and this time only print the optional options.
      for (auto &def : opt_defs) {
        if (def.usage_mask & opt_set_mask && !def.required &&
            def.option_has_arg != OptionParser::eNoArgument)
          PrintOption(def, eDisplayBestOption, " ", nullptr, true, strm);
      }

      if (args_str.GetSize() > 0) {
        if (cmd.WantsRawCommandString())
          strm.Printf(" --");
        strm << " " << args_str.GetString();
      }
    }
  }

  if ((only_print_args || cmd.WantsRawCommandString()) &&
      arguments_str.GetSize() > 0) {
    if (!only_print_args)
      strm.PutChar('\n');
    strm.Indent(name);
    strm << " " << arguments_str.GetString();
  }

  if (!only_print_args) {
    strm.Printf("\n\n");

    // Now print out all the detailed information about the various options:
    // long form, short form and help text:
    //   -short <argument> ( --long_name <argument> )
    //   help text

    strm.IndentMore(5);

    // Put the command options in a sorted container, so we can output
    // them alphabetically by short_option.
    std::multimap<int, uint32_t> options_ordered;
    for (auto def : llvm::enumerate(opt_defs))
      options_ordered.insert(
          std::make_pair(def.value().short_option, def.index()));

    // Go through each option, find the table entry and write out the detailed
    // help information for that option.

    bool first_option_printed = false;

    for (auto pos : options_ordered) {
      // Put a newline separation between arguments
      if (first_option_printed)
        strm.EOL();
      else
        first_option_printed = true;

      OptionDefinition opt_def = opt_defs[pos.second];

      strm.Indent();
      if (opt_def.short_option && opt_def.HasShortOption()) {
        PrintOption(opt_def, eDisplayShortOption, nullptr, nullptr, false,
                    strm);
        PrintOption(opt_def, eDisplayLongOption, " ( ", " )", false, strm);
      } else {
        // Short option is not printable, just print long option
        PrintOption(opt_def, eDisplayLongOption, nullptr, nullptr, false, strm);
      }
      strm.EOL();

      strm.IndentMore(5);

      if (opt_def.usage_text)
        OutputFormattedUsageText(strm, opt_def, screen_width);
      if (!opt_def.enum_values.empty()) {
        strm.Indent();
        strm.Printf("Values: ");
        bool is_first = true;
        for (const auto &enum_value : opt_def.enum_values) {
          if (is_first) {
            strm.Printf("%s", enum_value.string_value);
            is_first = false;
          }
          else
            strm.Printf(" | %s", enum_value.string_value);
        }
        strm.EOL();
      }
      strm.IndentLess(5);
    }
  }

  // Restore the indent level
  strm.SetIndentLevel(save_indent_level);
}

// This function is called when we have been given a potentially incomplete set
// of options, such as when an alias has been defined (more options might be
// added at at the time the alias is invoked).  We need to verify that the
// options in the set m_seen_options are all part of a set that may be used
// together, but m_seen_options may be missing some of the "required" options.

bool Options::VerifyPartialOptions(CommandReturnObject &result) {
  bool options_are_valid = false;

  int num_levels = GetRequiredOptions().size();
  if (num_levels) {
    for (int i = 0; i < num_levels && !options_are_valid; ++i) {
      // In this case we are treating all options as optional rather than
      // required. Therefore a set of options is correct if m_seen_options is a
      // subset of the union of m_required_options and m_optional_options.
      OptionSet union_set;
      OptionsSetUnion(GetRequiredOptions()[i], GetOptionalOptions()[i],
                      union_set);
      if (IsASubset(m_seen_options, union_set))
        options_are_valid = true;
    }
  }

  return options_are_valid;
}

bool Options::HandleOptionCompletion(CompletionRequest &request,
                                     OptionElementVector &opt_element_vector,
                                     CommandInterpreter &interpreter) {
  // For now we just scan the completions to see if the cursor position is in
  // an option or its argument.  Otherwise we'll call HandleArgumentCompletion.
  // In the future we can use completion to validate options as well if we
  // want.

  auto opt_defs = GetDefinitions();

  llvm::StringRef cur_opt_str = request.GetCursorArgumentPrefix();

  for (size_t i = 0; i < opt_element_vector.size(); i++) {
    size_t opt_pos = static_cast<size_t>(opt_element_vector[i].opt_pos);
    size_t opt_arg_pos = static_cast<size_t>(opt_element_vector[i].opt_arg_pos);
    int opt_defs_index = opt_element_vector[i].opt_defs_index;
    if (opt_pos == request.GetCursorIndex()) {
      // We're completing the option itself.

      if (opt_defs_index == OptionArgElement::eBareDash) {
        // We're completing a bare dash.  That means all options are open.
        // FIXME: We should scan the other options provided and only complete
        // options
        // within the option group they belong to.
        std::string opt_str = "-a";

        for (auto &def : opt_defs) {
          if (!def.short_option)
            continue;
          opt_str[1] = def.short_option;
          request.AddCompletion(opt_str, def.usage_text);
        }

        return true;
      } else if (opt_defs_index == OptionArgElement::eBareDoubleDash) {
        std::string full_name("--");
        for (auto &def : opt_defs) {
          if (!def.short_option)
            continue;

          full_name.erase(full_name.begin() + 2, full_name.end());
          full_name.append(def.long_option);
          request.AddCompletion(full_name, def.usage_text);
        }
        return true;
      } else if (opt_defs_index != OptionArgElement::eUnrecognizedArg) {
        // We recognized it, if it an incomplete long option, complete it
        // anyway (getopt_long_only is happy with shortest unique string, but
        // it's still a nice thing to do.)  Otherwise return The string so the
        // upper level code will know this is a full match and add the " ".
        const OptionDefinition &opt = opt_defs[opt_defs_index];
        llvm::StringRef long_option = opt.long_option;
        if (cur_opt_str.starts_with("--") && cur_opt_str != long_option) {
          request.AddCompletion("--" + long_option.str(), opt.usage_text);
          return true;
        } else
          request.AddCompletion(request.GetCursorArgumentPrefix());
        return true;
      } else {
        // FIXME - not handling wrong options yet:
        // Check to see if they are writing a long option & complete it.
        // I think we will only get in here if the long option table has two
        // elements
        // that are not unique up to this point.  getopt_long_only does
        // shortest unique match for long options already.
        if (cur_opt_str.consume_front("--")) {
          for (auto &def : opt_defs) {
            llvm::StringRef long_option(def.long_option);
            if (long_option.starts_with(cur_opt_str))
              request.AddCompletion("--" + long_option.str(), def.usage_text);
          }
        }
        return true;
      }

    } else if (opt_arg_pos == request.GetCursorIndex()) {
      // Okay the cursor is on the completion of an argument. See if it has a
      // completion, otherwise return no matches.
      if (opt_defs_index != -1) {
        HandleOptionArgumentCompletion(request, opt_element_vector, i,
                                       interpreter);
        return true;
      } else {
        // No completion callback means no completions...
        return true;
      }

    } else {
      // Not the last element, keep going.
      continue;
    }
  }
  return false;
}

void Options::HandleOptionArgumentCompletion(
    CompletionRequest &request, OptionElementVector &opt_element_vector,
    int opt_element_index, CommandInterpreter &interpreter) {
  auto opt_defs = GetDefinitions();
  std::unique_ptr<SearchFilter> filter_up;

  int opt_defs_index = opt_element_vector[opt_element_index].opt_defs_index;

  // See if this is an enumeration type option, and if so complete it here:

  const auto &enum_values = opt_defs[opt_defs_index].enum_values;
  if (!enum_values.empty())
    for (const auto &enum_value : enum_values)
      request.TryCompleteCurrentArg(enum_value.string_value);

  // If this is a source file or symbol type completion, and  there is a -shlib
  // option somewhere in the supplied arguments, then make a search filter for
  // that shared library.
  // FIXME: Do we want to also have an "OptionType" so we don't have to match
  // string names?

  uint32_t completion_mask = opt_defs[opt_defs_index].completion_type;

  if (completion_mask == 0) {
    lldb::CommandArgumentType option_arg_type =
        opt_defs[opt_defs_index].argument_type;
    if (option_arg_type != eArgTypeNone) {
      const CommandObject::ArgumentTableEntry *arg_entry =
          CommandObject::FindArgumentDataByType(
              opt_defs[opt_defs_index].argument_type);
      if (arg_entry)
        completion_mask = arg_entry->completion_type;
    }
  }

  if (completion_mask & lldb::eSourceFileCompletion ||
      completion_mask & lldb::eSymbolCompletion) {
    for (size_t i = 0; i < opt_element_vector.size(); i++) {
      int cur_defs_index = opt_element_vector[i].opt_defs_index;

      // trying to use <0 indices will definitely cause problems
      if (cur_defs_index == OptionArgElement::eUnrecognizedArg ||
          cur_defs_index == OptionArgElement::eBareDash ||
          cur_defs_index == OptionArgElement::eBareDoubleDash)
        continue;

      int cur_arg_pos = opt_element_vector[i].opt_arg_pos;
      const char *cur_opt_name = opt_defs[cur_defs_index].long_option;

      // If this is the "shlib" option and there was an argument provided,
      // restrict it to that shared library.
      if (cur_opt_name && strcmp(cur_opt_name, "shlib") == 0 &&
          cur_arg_pos != -1) {
        const char *module_name =
            request.GetParsedLine().GetArgumentAtIndex(cur_arg_pos);
        if (module_name) {
          FileSpec module_spec(module_name);
          lldb::TargetSP target_sp =
              interpreter.GetDebugger().GetSelectedTarget();
          // Search filters require a target...
          if (target_sp)
            filter_up =
                std::make_unique<SearchFilterByModule>(target_sp, module_spec);
        }
        break;
      }
    }
  }

  lldb_private::CommandCompletions::InvokeCommonCompletionCallbacks(
      interpreter, completion_mask, request, filter_up.get());
}

void OptionGroupOptions::Append(OptionGroup *group) {
  auto group_option_defs = group->GetDefinitions();
  for (uint32_t i = 0; i < group_option_defs.size(); ++i) {
    m_option_infos.push_back(OptionInfo(group, i));
    m_option_defs.push_back(group_option_defs[i]);
  }
}

const OptionGroup *OptionGroupOptions::GetGroupWithOption(char short_opt) {
  for (uint32_t i = 0; i < m_option_defs.size(); i++) {
    OptionDefinition opt_def = m_option_defs[i];
    if (opt_def.short_option == short_opt)
      return m_option_infos[i].option_group;
  }
  return nullptr;
}

void OptionGroupOptions::Append(OptionGroup *group, uint32_t src_mask,
                                uint32_t dst_mask) {
  auto group_option_defs = group->GetDefinitions();
  for (uint32_t i = 0; i < group_option_defs.size(); ++i) {
    if (group_option_defs[i].usage_mask & src_mask) {
      m_option_infos.push_back(OptionInfo(group, i));
      m_option_defs.push_back(group_option_defs[i]);
      m_option_defs.back().usage_mask = dst_mask;
    }
  }
}

void OptionGroupOptions::Append(
    OptionGroup *group, llvm::ArrayRef<llvm::StringRef> exclude_long_options) {
  auto group_option_defs = group->GetDefinitions();
  for (uint32_t i = 0; i < group_option_defs.size(); ++i) {
    const auto &definition = group_option_defs[i];
    if (llvm::is_contained(exclude_long_options, definition.long_option))
      continue;

    m_option_infos.push_back(OptionInfo(group, i));
    m_option_defs.push_back(definition);
  }
}

void OptionGroupOptions::Finalize() {
  m_did_finalize = true;
}

Status OptionGroupOptions::SetOptionValue(uint32_t option_idx,
                                          llvm::StringRef option_value,
                                          ExecutionContext *execution_context) {
  // After calling OptionGroupOptions::Append(...), you must finalize the
  // groups by calling OptionGroupOptions::Finlize()
  assert(m_did_finalize);
  Status error;
  if (option_idx < m_option_infos.size()) {
    error = m_option_infos[option_idx].option_group->SetOptionValue(
        m_option_infos[option_idx].option_index, option_value,
        execution_context);

  } else {
    error.SetErrorString("invalid option index"); // Shouldn't happen...
  }
  return error;
}

void OptionGroupOptions::OptionParsingStarting(
    ExecutionContext *execution_context) {
  std::set<OptionGroup *> group_set;
  OptionInfos::iterator pos, end = m_option_infos.end();
  for (pos = m_option_infos.begin(); pos != end; ++pos) {
    OptionGroup *group = pos->option_group;
    if (group_set.find(group) == group_set.end()) {
      group->OptionParsingStarting(execution_context);
      group_set.insert(group);
    }
  }
}
Status
OptionGroupOptions::OptionParsingFinished(ExecutionContext *execution_context) {
  std::set<OptionGroup *> group_set;
  Status error;
  OptionInfos::iterator pos, end = m_option_infos.end();
  for (pos = m_option_infos.begin(); pos != end; ++pos) {
    OptionGroup *group = pos->option_group;
    if (group_set.find(group) == group_set.end()) {
      error = group->OptionParsingFinished(execution_context);
      group_set.insert(group);
      if (error.Fail())
        return error;
    }
  }
  return error;
}

// OptionParser permutes the arguments while processing them, so we create a
// temporary array holding to avoid modification of the input arguments. The
// options themselves are never modified, but the API expects a char * anyway,
// hence the const_cast.
static std::vector<char *> GetArgvForParsing(const Args &args) {
  std::vector<char *> result;
  // OptionParser always skips the first argument as it is based on getopt().
  result.push_back(const_cast<char *>("<FAKE-ARG0>"));
  for (const Args::ArgEntry &entry : args)
    result.push_back(const_cast<char *>(entry.c_str()));
  result.push_back(nullptr);
  return result;
}

// Given a permuted argument, find it's position in the original Args vector.
static Args::const_iterator FindOriginalIter(const char *arg,
                                             const Args &original) {
  return llvm::find_if(
      original, [arg](const Args::ArgEntry &D) { return D.c_str() == arg; });
}

// Given a permuted argument, find it's index in the original Args vector.
static size_t FindOriginalIndex(const char *arg, const Args &original) {
  return std::distance(original.begin(), FindOriginalIter(arg, original));
}

// Construct a new Args object, consisting of the entries from the original
// arguments, but in the permuted order.
static Args ReconstituteArgsAfterParsing(llvm::ArrayRef<char *> parsed,
                                         const Args &original) {
  Args result;
  for (const char *arg : parsed) {
    auto pos = FindOriginalIter(arg, original);
    assert(pos != original.end());
    result.AppendArgument(pos->ref(), pos->GetQuoteChar());
  }
  return result;
}

static size_t FindArgumentIndexForOption(const Args &args,
                                         const Option &long_option) {
  std::string short_opt = llvm::formatv("-{0}", char(long_option.val)).str();
  std::string long_opt =
      std::string(llvm::formatv("--{0}", long_option.definition->long_option));
  for (const auto &entry : llvm::enumerate(args)) {
    if (entry.value().ref().starts_with(short_opt) ||
        entry.value().ref().starts_with(long_opt))
      return entry.index();
  }

  return size_t(-1);
}

static std::string BuildShortOptions(const Option *long_options) {
  std::string storage;
  llvm::raw_string_ostream sstr(storage);

  // Leading : tells getopt to return a : for a missing option argument AND to
  // suppress error messages.
  sstr << ":";

  for (size_t i = 0; long_options[i].definition != nullptr; ++i) {
    if (long_options[i].flag == nullptr) {
      sstr << (char)long_options[i].val;
      switch (long_options[i].definition->option_has_arg) {
      default:
      case OptionParser::eNoArgument:
        break;
      case OptionParser::eRequiredArgument:
        sstr << ":";
        break;
      case OptionParser::eOptionalArgument:
        sstr << "::";
        break;
      }
    }
  }
  return std::move(sstr.str());
}

llvm::Expected<Args> Options::ParseAlias(const Args &args,
                                         OptionArgVector *option_arg_vector,
                                         std::string &input_line) {
  Option *long_options = GetLongOptions();

  if (long_options == nullptr) {
    return llvm::createStringError("Invalid long options");
  }

  std::string short_options = BuildShortOptions(long_options);

  Args args_copy = args;
  std::vector<char *> argv = GetArgvForParsing(args);

  std::unique_lock<std::mutex> lock;
  OptionParser::Prepare(lock);
  int val;
  while (true) {
    int long_options_index = -1;
    val = OptionParser::Parse(argv, short_options, long_options,
                              &long_options_index);

    if (val == ':') {
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "last option requires an argument");
    }

    if (val == -1)
      break;

    if (val == '?') {
      return llvm::createStringError("Unknown or ambiguous option");
    }

    if (val == 0)
      continue;

    OptionSeen(val);

    // Look up the long option index
    if (long_options_index == -1) {
      for (int j = 0; long_options[j].definition || long_options[j].flag ||
                      long_options[j].val;
           ++j) {
        if (long_options[j].val == val) {
          long_options_index = j;
          break;
        }
      }
    }

    // See if the option takes an argument, and see if one was supplied.
    if (long_options_index == -1) {
      return llvm::createStringError(
          llvm::formatv("Invalid option with value '{0}'.", char(val)).str());
    }

    StreamString option_str;
    option_str.Printf("-%c", val);
    const OptionDefinition *def = long_options[long_options_index].definition;
    int has_arg =
        (def == nullptr) ? OptionParser::eNoArgument : def->option_has_arg;

    const char *option_arg = nullptr;
    switch (has_arg) {
    case OptionParser::eRequiredArgument:
      if (OptionParser::GetOptionArgument() == nullptr) {
        return llvm::createStringError(
            llvm::formatv("Option '{0}' is missing argument specifier.",
                          option_str.GetString())
                .str());
      }
      [[fallthrough]];
    case OptionParser::eOptionalArgument:
      option_arg = OptionParser::GetOptionArgument();
      [[fallthrough]];
    case OptionParser::eNoArgument:
      break;
    default:
      return llvm::createStringError(
          llvm::formatv("error with options table; invalid value in has_arg "
                        "field for option '{0}'.",
                        char(val))
              .str());
    }
    // Find option in the argument list; also see if it was supposed to take an
    // argument and if one was supplied.  Remove option (and argument, if
    // given) from the argument list.  Also remove them from the
    // raw_input_string, if one was passed in.
    // Note: We also need to preserve any option argument values that were
    // surrounded by backticks, as we lose track of them in the
    // option_args_vector.
    size_t idx =
        FindArgumentIndexForOption(args_copy, long_options[long_options_index]);
    std::string option_to_insert;
    if (option_arg) {
      if (idx != size_t(-1) && has_arg) {
        bool arg_has_backtick = args_copy[idx + 1].GetQuoteChar() == '`';
        if (arg_has_backtick)
          option_to_insert = "`";
        option_to_insert += option_arg;
        if (arg_has_backtick)
          option_to_insert += "`";
      } else
        option_to_insert = option_arg;
    } else
      option_to_insert = CommandInterpreter::g_no_argument;

    option_arg_vector->emplace_back(std::string(option_str.GetString()),
                                    has_arg, option_to_insert);

    if (idx == size_t(-1))
      continue;

    if (!input_line.empty()) {
      llvm::StringRef tmp_arg = args_copy[idx].ref();
      size_t pos = input_line.find(std::string(tmp_arg));
      if (pos != std::string::npos)
        input_line.erase(pos, tmp_arg.size());
    }
    args_copy.DeleteArgumentAtIndex(idx);
    if ((option_to_insert != CommandInterpreter::g_no_argument) &&
        (OptionParser::GetOptionArgument() != nullptr) &&
        (idx < args_copy.GetArgumentCount()) &&
        (args_copy[idx].ref() == OptionParser::GetOptionArgument())) {
      if (input_line.size() > 0) {
        size_t pos = input_line.find(option_to_insert);
        if (pos != std::string::npos)
          input_line.erase(pos, option_to_insert.size());
      }
      args_copy.DeleteArgumentAtIndex(idx);
    }
  }

  return std::move(args_copy);
}

OptionElementVector Options::ParseForCompletion(const Args &args,
                                                uint32_t cursor_index) {
  OptionElementVector option_element_vector;
  Option *long_options = GetLongOptions();
  option_element_vector.clear();

  if (long_options == nullptr)
    return option_element_vector;

  std::string short_options = BuildShortOptions(long_options);

  std::unique_lock<std::mutex> lock;
  OptionParser::Prepare(lock);
  OptionParser::EnableError(false);

  int val;
  auto opt_defs = GetDefinitions();

  std::vector<char *> dummy_vec = GetArgvForParsing(args);

  bool failed_once = false;
  uint32_t dash_dash_pos = -1;

  while (true) {
    bool missing_argument = false;
    int long_options_index = -1;

    val = OptionParser::Parse(dummy_vec, short_options, long_options,
                              &long_options_index);

    if (val == -1) {
      // When we're completing a "--" which is the last option on line,
      if (failed_once)
        break;

      failed_once = true;

      // If this is a bare  "--" we mark it as such so we can complete it
      // successfully later.  Handling the "--" is a little tricky, since that
      // may mean end of options or arguments, or the user might want to
      // complete options by long name.  I make this work by checking whether
      // the cursor is in the "--" argument, and if so I assume we're
      // completing the long option, otherwise I let it pass to
      // OptionParser::Parse which will terminate the option parsing.  Note, in
      // either case we continue parsing the line so we can figure out what
      // other options were passed.  This will be useful when we come to
      // restricting completions based on what other options we've seen on the
      // line.

      if (static_cast<size_t>(OptionParser::GetOptionIndex()) <
              dummy_vec.size() &&
          (strcmp(dummy_vec[OptionParser::GetOptionIndex() - 1], "--") == 0)) {
        dash_dash_pos = FindOriginalIndex(
            dummy_vec[OptionParser::GetOptionIndex() - 1], args);
        if (dash_dash_pos == cursor_index) {
          option_element_vector.push_back(
              OptionArgElement(OptionArgElement::eBareDoubleDash, dash_dash_pos,
                               OptionArgElement::eBareDoubleDash));
          continue;
        } else
          break;
      } else
        break;
    } else if (val == '?') {
      option_element_vector.push_back(OptionArgElement(
          OptionArgElement::eUnrecognizedArg,
          FindOriginalIndex(dummy_vec[OptionParser::GetOptionIndex() - 1],
                            args),
          OptionArgElement::eUnrecognizedArg));
      continue;
    } else if (val == 0) {
      continue;
    } else if (val == ':') {
      // This is a missing argument.
      val = OptionParser::GetOptionErrorCause();
      missing_argument = true;
    }

    OptionSeen(val);

    // Look up the long option index
    if (long_options_index == -1) {
      for (int j = 0; long_options[j].definition || long_options[j].flag ||
                      long_options[j].val;
           ++j) {
        if (long_options[j].val == val) {
          long_options_index = j;
          break;
        }
      }
    }

    // See if the option takes an argument, and see if one was supplied.
    if (long_options_index >= 0) {
      int opt_defs_index = -1;
      for (size_t i = 0; i < opt_defs.size(); i++) {
        if (opt_defs[i].short_option != val)
          continue;
        opt_defs_index = i;
        break;
      }

      const OptionDefinition *def = long_options[long_options_index].definition;
      int has_arg =
          (def == nullptr) ? OptionParser::eNoArgument : def->option_has_arg;
      switch (has_arg) {
      case OptionParser::eNoArgument:
        option_element_vector.push_back(OptionArgElement(
            opt_defs_index,
            FindOriginalIndex(dummy_vec[OptionParser::GetOptionIndex() - 1],
                              args),
            0));
        break;
      case OptionParser::eRequiredArgument:
        if (OptionParser::GetOptionArgument() != nullptr) {
          int arg_index;
          if (missing_argument)
            arg_index = -1;
          else
            arg_index = OptionParser::GetOptionIndex() - 2;

          option_element_vector.push_back(OptionArgElement(
              opt_defs_index,
              FindOriginalIndex(dummy_vec[OptionParser::GetOptionIndex() - 2],
                                args),
              arg_index));
        } else {
          option_element_vector.push_back(OptionArgElement(
              opt_defs_index,
              FindOriginalIndex(dummy_vec[OptionParser::GetOptionIndex() - 1],
                                args),
              -1));
        }
        break;
      case OptionParser::eOptionalArgument:
        option_element_vector.push_back(OptionArgElement(
            opt_defs_index,
            FindOriginalIndex(dummy_vec[OptionParser::GetOptionIndex() - 2],
                              args),
            FindOriginalIndex(dummy_vec[OptionParser::GetOptionIndex() - 1],
                              args)));
        break;
      default:
        // The options table is messed up.  Here we'll just continue
        option_element_vector.push_back(OptionArgElement(
            OptionArgElement::eUnrecognizedArg,
            FindOriginalIndex(dummy_vec[OptionParser::GetOptionIndex() - 1],
                              args),
            OptionArgElement::eUnrecognizedArg));
        break;
      }
    } else {
      option_element_vector.push_back(OptionArgElement(
          OptionArgElement::eUnrecognizedArg,
          FindOriginalIndex(dummy_vec[OptionParser::GetOptionIndex() - 1],
                            args),
          OptionArgElement::eUnrecognizedArg));
    }
  }

  // Finally we have to handle the case where the cursor index points at a
  // single "-".  We want to mark that in the option_element_vector, but only
  // if it is not after the "--".  But it turns out that OptionParser::Parse
  // just ignores an isolated "-".  So we have to look it up by hand here.  We
  // only care if it is AT the cursor position. Note, a single quoted dash is
  // not the same as a single dash...

  const Args::ArgEntry &cursor = args[cursor_index];
  if ((static_cast<int32_t>(dash_dash_pos) == -1 ||
       cursor_index < dash_dash_pos) &&
      !cursor.IsQuoted() && cursor.ref() == "-") {
    option_element_vector.push_back(
        OptionArgElement(OptionArgElement::eBareDash, cursor_index,
                         OptionArgElement::eBareDash));
  }
  return option_element_vector;
}

llvm::Expected<Args> Options::Parse(const Args &args,
                                    ExecutionContext *execution_context,
                                    lldb::PlatformSP platform_sp,
                                    bool require_validation) {
  Status error;
  Option *long_options = GetLongOptions();
  if (long_options == nullptr) {
    return llvm::createStringError("Invalid long options.");
  }

  std::string short_options = BuildShortOptions(long_options);
  std::vector<char *> argv = GetArgvForParsing(args);
  std::unique_lock<std::mutex> lock;
  OptionParser::Prepare(lock);
  int val;
  while (true) {
    int long_options_index = -1;
    val = OptionParser::Parse(argv, short_options, long_options,
                              &long_options_index);

    if (val == ':') {
      error.SetErrorString("last option requires an argument");
      break;
    }

    if (val == -1)
      break;

    // Did we get an error?
    if (val == '?') {
      error.SetErrorString("unknown or ambiguous option");
      break;
    }
    // The option auto-set itself
    if (val == 0)
      continue;

    OptionSeen(val);

    // Lookup the long option index
    if (long_options_index == -1) {
      for (int i = 0; long_options[i].definition || long_options[i].flag ||
                      long_options[i].val;
           ++i) {
        if (long_options[i].val == val) {
          long_options_index = i;
          break;
        }
      }
    }
    // Call the callback with the option
    if (long_options_index >= 0 &&
        long_options[long_options_index].definition) {
      const OptionDefinition *def = long_options[long_options_index].definition;

      if (!platform_sp) {
        // User did not pass in an explicit platform.  Try to grab from the
        // execution context.
        TargetSP target_sp =
            execution_context ? execution_context->GetTargetSP() : TargetSP();
        platform_sp = target_sp ? target_sp->GetPlatform() : PlatformSP();
      }
      OptionValidator *validator = def->validator;

      if (!platform_sp && require_validation) {
        // Caller requires validation but we cannot validate as we don't have
        // the mandatory platform against which to validate.
        return llvm::createStringError(
            "cannot validate options: no platform available");
      }

      bool validation_failed = false;
      if (platform_sp) {
        // Ensure we have an execution context, empty or not.
        ExecutionContext dummy_context;
        ExecutionContext *exe_ctx_p =
            execution_context ? execution_context : &dummy_context;
        if (validator && !validator->IsValid(*platform_sp, *exe_ctx_p)) {
          validation_failed = true;
          error.SetErrorStringWithFormat("Option \"%s\" invalid.  %s",
                                         def->long_option,
                                         def->validator->LongConditionString());
        }
      }

      // As long as validation didn't fail, we set the option value.
      if (!validation_failed)
        error =
            SetOptionValue(long_options_index,
                           (def->option_has_arg == OptionParser::eNoArgument)
                               ? nullptr
                               : OptionParser::GetOptionArgument(),
                           execution_context);
      // If the Option setting returned an error, we should stop parsing
      // and return the error.
      if (error.Fail())
        break;
    } else {
      error.SetErrorStringWithFormat("invalid option with value '%i'", val);
    }
  }

  if (error.Fail())
    return error.ToError();

  argv.pop_back();
  argv.erase(argv.begin(), argv.begin() + OptionParser::GetOptionIndex());
  return ReconstituteArgsAfterParsing(argv, args);
}

llvm::Error lldb_private::CreateOptionParsingError(
    llvm::StringRef option_arg, const char short_option,
    llvm::StringRef long_option, llvm::StringRef additional_context) {
  std::string buffer;
  llvm::raw_string_ostream stream(buffer);
  stream << "Invalid value ('" << option_arg << "') for -" << short_option;
  if (!long_option.empty())
    stream << " (" << long_option << ")";
  if (!additional_context.empty())
    stream << ": " << additional_context;
  return llvm::createStringError(llvm::inconvertibleErrorCode(), buffer);
}
