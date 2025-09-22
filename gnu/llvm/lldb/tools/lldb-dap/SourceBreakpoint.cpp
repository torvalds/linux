//===-- SourceBreakpoint.cpp ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SourceBreakpoint.h"
#include "DAP.h"

namespace lldb_dap {

SourceBreakpoint::SourceBreakpoint(const llvm::json::Object &obj)
    : Breakpoint(obj), logMessage(std::string(GetString(obj, "logMessage"))),
      line(GetUnsigned(obj, "line", 0)), column(GetUnsigned(obj, "column", 0)) {
}

void SourceBreakpoint::SetBreakpoint(const llvm::StringRef source_path) {
  lldb::SBFileSpecList module_list;
  bp = g_dap.target.BreakpointCreateByLocation(source_path.str().c_str(), line,
                                               column, 0, module_list);
  if (!logMessage.empty())
    SetLogMessage();
  Breakpoint::SetBreakpoint();
}

void SourceBreakpoint::UpdateBreakpoint(const SourceBreakpoint &request_bp) {
  if (logMessage != request_bp.logMessage) {
    logMessage = request_bp.logMessage;
    SetLogMessage();
  }
  BreakpointBase::UpdateBreakpoint(request_bp);
}

lldb::SBError SourceBreakpoint::AppendLogMessagePart(llvm::StringRef part,
                                                     bool is_expr) {
  if (is_expr) {
    logMessageParts.emplace_back(part, is_expr);
  } else {
    std::string formatted;
    lldb::SBError error = FormatLogText(part, formatted);
    if (error.Fail())
      return error;
    logMessageParts.emplace_back(formatted, is_expr);
  }
  return lldb::SBError();
}

// TODO: consolidate this code with the implementation in
// FormatEntity::ParseInternal().
lldb::SBError SourceBreakpoint::FormatLogText(llvm::StringRef text,
                                              std::string &formatted) {
  lldb::SBError error;
  while (!text.empty()) {
    size_t backslash_pos = text.find_first_of('\\');
    if (backslash_pos == std::string::npos) {
      formatted += text.str();
      return error;
    }

    formatted += text.substr(0, backslash_pos).str();
    // Skip the characters before and including '\'.
    text = text.drop_front(backslash_pos + 1);

    if (text.empty()) {
      error.SetErrorString(
          "'\\' character was not followed by another character");
      return error;
    }

    const char desens_char = text[0];
    text = text.drop_front(); // Skip the desensitized char character
    switch (desens_char) {
    case 'a':
      formatted.push_back('\a');
      break;
    case 'b':
      formatted.push_back('\b');
      break;
    case 'f':
      formatted.push_back('\f');
      break;
    case 'n':
      formatted.push_back('\n');
      break;
    case 'r':
      formatted.push_back('\r');
      break;
    case 't':
      formatted.push_back('\t');
      break;
    case 'v':
      formatted.push_back('\v');
      break;
    case '\'':
      formatted.push_back('\'');
      break;
    case '\\':
      formatted.push_back('\\');
      break;
    case '0':
      // 1 to 3 octal chars
      {
        if (text.empty()) {
          error.SetErrorString("missing octal number following '\\0'");
          return error;
        }

        // Make a string that can hold onto the initial zero char, up to 3
        // octal digits, and a terminating NULL.
        char oct_str[5] = {0, 0, 0, 0, 0};

        size_t i;
        for (i = 0;
             i < text.size() && i < 4 && (text[i] >= '0' && text[i] <= '7');
             ++i) {
          oct_str[i] = text[i];
        }

        text = text.drop_front(i);
        unsigned long octal_value = ::strtoul(oct_str, nullptr, 8);
        if (octal_value <= UINT8_MAX) {
          formatted.push_back((char)octal_value);
        } else {
          error.SetErrorString("octal number is larger than a single byte");
          return error;
        }
      }
      break;

    case 'x': {
      if (text.empty()) {
        error.SetErrorString("missing hex number following '\\x'");
        return error;
      }
      // hex number in the text
      if (isxdigit(text[0])) {
        // Make a string that can hold onto two hex chars plus a
        // NULL terminator
        char hex_str[3] = {0, 0, 0};
        hex_str[0] = text[0];

        text = text.drop_front();

        if (!text.empty() && isxdigit(text[0])) {
          hex_str[1] = text[0];
          text = text.drop_front();
        }

        unsigned long hex_value = strtoul(hex_str, nullptr, 16);
        if (hex_value <= UINT8_MAX) {
          formatted.push_back((char)hex_value);
        } else {
          error.SetErrorString("hex number is larger than a single byte");
          return error;
        }
      } else {
        formatted.push_back(desens_char);
      }
      break;
    }

    default:
      // Just desensitize any other character by just printing what came
      // after the '\'
      formatted.push_back(desens_char);
      break;
    }
  }
  return error;
}

// logMessage will be divided into array of LogMessagePart as two kinds:
// 1. raw print text message, and
// 2. interpolated expression for evaluation which is inside matching curly
//    braces.
//
// The function tries to parse logMessage into a list of LogMessageParts
// for easy later access in BreakpointHitCallback.
void SourceBreakpoint::SetLogMessage() {
  logMessageParts.clear();

  // Contains unmatched open curly braces indices.
  std::vector<int> unmatched_curly_braces;

  // Contains all matched curly braces in logMessage.
  // Loop invariant: matched_curly_braces_ranges are sorted by start index in
  // ascending order without any overlap between them.
  std::vector<std::pair<int, int>> matched_curly_braces_ranges;

  lldb::SBError error;
  // Part1 - parse matched_curly_braces_ranges.
  // locating all curly braced expression ranges in logMessage.
  // The algorithm takes care of nested and imbalanced curly braces.
  for (size_t i = 0; i < logMessage.size(); ++i) {
    if (logMessage[i] == '{') {
      unmatched_curly_braces.push_back(i);
    } else if (logMessage[i] == '}') {
      if (unmatched_curly_braces.empty())
        // Nothing to match.
        continue;

      int last_unmatched_index = unmatched_curly_braces.back();
      unmatched_curly_braces.pop_back();

      // Erase any matched ranges included in the new match.
      while (!matched_curly_braces_ranges.empty()) {
        assert(matched_curly_braces_ranges.back().first !=
                   last_unmatched_index &&
               "How can a curley brace be matched twice?");
        if (matched_curly_braces_ranges.back().first < last_unmatched_index)
          break;

        // This is a nested range let's earse it.
        assert((size_t)matched_curly_braces_ranges.back().second < i);
        matched_curly_braces_ranges.pop_back();
      }

      // Assert invariant.
      assert(matched_curly_braces_ranges.empty() ||
             matched_curly_braces_ranges.back().first < last_unmatched_index);
      matched_curly_braces_ranges.emplace_back(last_unmatched_index, i);
    }
  }

  // Part2 - parse raw text and expresions parts.
  // All expression ranges have been parsed in matched_curly_braces_ranges.
  // The code below uses matched_curly_braces_ranges to divide logMessage
  // into raw text parts and expression parts.
  int last_raw_text_start = 0;
  for (const std::pair<int, int> &curly_braces_range :
       matched_curly_braces_ranges) {
    // Raw text before open curly brace.
    assert(curly_braces_range.first >= last_raw_text_start);
    size_t raw_text_len = curly_braces_range.first - last_raw_text_start;
    if (raw_text_len > 0) {
      error = AppendLogMessagePart(
          llvm::StringRef(logMessage.c_str() + last_raw_text_start,
                          raw_text_len),
          /*is_expr=*/false);
      if (error.Fail()) {
        NotifyLogMessageError(error.GetCString());
        return;
      }
    }

    // Expression between curly braces.
    assert(curly_braces_range.second > curly_braces_range.first);
    size_t expr_len = curly_braces_range.second - curly_braces_range.first - 1;
    error = AppendLogMessagePart(
        llvm::StringRef(logMessage.c_str() + curly_braces_range.first + 1,
                        expr_len),
        /*is_expr=*/true);
    if (error.Fail()) {
      NotifyLogMessageError(error.GetCString());
      return;
    }

    last_raw_text_start = curly_braces_range.second + 1;
  }
  // Trailing raw text after close curly brace.
  assert(last_raw_text_start >= 0);
  if (logMessage.size() > (size_t)last_raw_text_start) {
    error = AppendLogMessagePart(
        llvm::StringRef(logMessage.c_str() + last_raw_text_start,
                        logMessage.size() - last_raw_text_start),
        /*is_expr=*/false);
    if (error.Fail()) {
      NotifyLogMessageError(error.GetCString());
      return;
    }
  }

  bp.SetCallback(BreakpointHitCallback, this);
}

void SourceBreakpoint::NotifyLogMessageError(llvm::StringRef error) {
  std::string message = "Log message has error: ";
  message += error;
  g_dap.SendOutput(OutputType::Console, message);
}

/*static*/
bool SourceBreakpoint::BreakpointHitCallback(
    void *baton, lldb::SBProcess &process, lldb::SBThread &thread,
    lldb::SBBreakpointLocation &location) {
  if (!baton)
    return true;

  SourceBreakpoint *bp = (SourceBreakpoint *)baton;
  lldb::SBFrame frame = thread.GetSelectedFrame();

  std::string output;
  for (const SourceBreakpoint::LogMessagePart &messagePart :
       bp->logMessageParts) {
    if (messagePart.is_expr) {
      // Try local frame variables first before fall back to expression
      // evaluation
      const std::string &expr_str = messagePart.text;
      const char *expr = expr_str.c_str();
      lldb::SBValue value =
          frame.GetValueForVariablePath(expr, lldb::eDynamicDontRunTarget);
      if (value.GetError().Fail())
        value = frame.EvaluateExpression(expr);
      output += VariableDescription(value).display_value;
    } else {
      output += messagePart.text;
    }
  }
  if (!output.empty() && output.back() != '\n')
    output.push_back('\n'); // Ensure log message has line break.
  g_dap.SendOutput(OutputType::Console, output.c_str());

  // Do not stop.
  return false;
}

} // namespace lldb_dap
