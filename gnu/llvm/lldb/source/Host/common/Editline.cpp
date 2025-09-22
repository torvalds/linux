//===-- Editline.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <climits>
#include <iomanip>
#include <optional>

#include "lldb/Host/Editline.h"

#include "lldb/Host/ConnectionFileDescriptor.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/Host.h"
#include "lldb/Utility/CompletionRequest.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/SelectHelper.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/StringList.h"
#include "lldb/Utility/Timeout.h"

#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Locale.h"
#include "llvm/Support/Threading.h"

using namespace lldb_private;
using namespace lldb_private::line_editor;

// Editline uses careful cursor management to achieve the illusion of editing a
// multi-line block of text with a single line editor.  Preserving this
// illusion requires fairly careful management of cursor state.  Read and
// understand the relationship between DisplayInput(), MoveCursor(),
// SetCurrentLine(), and SaveEditedLine() before making changes.

/// https://www.ecma-international.org/publications/files/ECMA-ST/Ecma-048.pdf
#define ESCAPE "\x1b"
#define ANSI_CLEAR_BELOW ESCAPE "[J"
#define ANSI_CLEAR_RIGHT ESCAPE "[K"
#define ANSI_SET_COLUMN_N ESCAPE "[%dG"
#define ANSI_UP_N_ROWS ESCAPE "[%dA"
#define ANSI_DOWN_N_ROWS ESCAPE "[%dB"

#if LLDB_EDITLINE_USE_WCHAR

#define EditLineConstString(str) L##str
#define EditLineStringFormatSpec "%ls"

#else

#define EditLineConstString(str) str
#define EditLineStringFormatSpec "%s"

// use #defines so wide version functions and structs will resolve to old
// versions for case of libedit not built with wide char support
#define history_w history
#define history_winit history_init
#define history_wend history_end
#define HistoryW History
#define HistEventW HistEvent
#define LineInfoW LineInfo

#define el_wgets el_gets
#define el_wgetc el_getc
#define el_wpush el_push
#define el_wparse el_parse
#define el_wset el_set
#define el_wget el_get
#define el_wline el_line
#define el_winsertstr el_insertstr
#define el_wdeletestr el_deletestr

#endif // #if LLDB_EDITLINE_USE_WCHAR

bool IsOnlySpaces(const EditLineStringType &content) {
  for (wchar_t ch : content) {
    if (ch != EditLineCharType(' '))
      return false;
  }
  return true;
}

static size_t ColumnWidth(llvm::StringRef str) {
  return llvm::sys::locale::columnWidth(str);
}

static int GetOperation(HistoryOperation op) {
  // The naming used by editline for the history operations is counter
  // intuitive to how it's used in LLDB's editline implementation.
  //
  //  - The H_LAST returns the oldest entry in the history.
  //
  //  - The H_PREV operation returns the previous element in the history, which
  //    is newer than the current one.
  //
  //  - The H_CURR returns the current entry in the history.
  //
  //  - The H_NEXT operation returns the next element in the history, which is
  //    older than the current one.
  //
  //  - The H_FIRST returns the most recent entry in the history.
  //
  // The naming of the enum entries match the semantic meaning.
  switch(op) {
    case HistoryOperation::Oldest:
      return H_LAST;
    case HistoryOperation::Older:
      return H_NEXT;
    case HistoryOperation::Current:
      return H_CURR;
    case HistoryOperation::Newer:
      return H_PREV;
    case HistoryOperation::Newest:
      return H_FIRST;
  }
  llvm_unreachable("Fully covered switch!");
}


EditLineStringType CombineLines(const std::vector<EditLineStringType> &lines) {
  EditLineStringStreamType combined_stream;
  for (EditLineStringType line : lines) {
    combined_stream << line.c_str() << "\n";
  }
  return combined_stream.str();
}

std::vector<EditLineStringType> SplitLines(const EditLineStringType &input) {
  std::vector<EditLineStringType> result;
  size_t start = 0;
  while (start < input.length()) {
    size_t end = input.find('\n', start);
    if (end == std::string::npos) {
      result.push_back(input.substr(start));
      break;
    }
    result.push_back(input.substr(start, end - start));
    start = end + 1;
  }
  // Treat an empty history session as a single command of zero-length instead
  // of returning an empty vector.
  if (result.empty()) {
    result.emplace_back();
  }
  return result;
}

EditLineStringType FixIndentation(const EditLineStringType &line,
                                  int indent_correction) {
  if (indent_correction == 0)
    return line;
  if (indent_correction < 0)
    return line.substr(-indent_correction);
  return EditLineStringType(indent_correction, EditLineCharType(' ')) + line;
}

int GetIndentation(const EditLineStringType &line) {
  int space_count = 0;
  for (EditLineCharType ch : line) {
    if (ch != EditLineCharType(' '))
      break;
    ++space_count;
  }
  return space_count;
}

bool IsInputPending(FILE *file) {
  // FIXME: This will be broken on Windows if we ever re-enable Editline.  You
  // can't use select
  // on something that isn't a socket.  This will have to be re-written to not
  // use a FILE*, but instead use some kind of yet-to-be-created abstraction
  // that select-like functionality on non-socket objects.
  const int fd = fileno(file);
  SelectHelper select_helper;
  select_helper.SetTimeout(std::chrono::microseconds(0));
  select_helper.FDSetRead(fd);
  return select_helper.Select().Success();
}

namespace lldb_private {
namespace line_editor {
typedef std::weak_ptr<EditlineHistory> EditlineHistoryWP;

// EditlineHistory objects are sometimes shared between multiple Editline
// instances with the same program name.

class EditlineHistory {
private:
  // Use static GetHistory() function to get a EditlineHistorySP to one of
  // these objects
  EditlineHistory(const std::string &prefix, uint32_t size, bool unique_entries)
      : m_prefix(prefix) {
    m_history = history_winit();
    history_w(m_history, &m_event, H_SETSIZE, size);
    if (unique_entries)
      history_w(m_history, &m_event, H_SETUNIQUE, 1);
  }

  const char *GetHistoryFilePath() {
    // Compute the history path lazily.
    if (m_path.empty() && m_history && !m_prefix.empty()) {
      llvm::SmallString<128> lldb_history_file;
      FileSystem::Instance().GetHomeDirectory(lldb_history_file);
      llvm::sys::path::append(lldb_history_file, ".lldb");

      // LLDB stores its history in ~/.lldb/. If for some reason this directory
      // isn't writable or cannot be created, history won't be available.
      if (!llvm::sys::fs::create_directory(lldb_history_file)) {
#if LLDB_EDITLINE_USE_WCHAR
        std::string filename = m_prefix + "-widehistory";
#else
        std::string filename = m_prefix + "-history";
#endif
        llvm::sys::path::append(lldb_history_file, filename);
        m_path = std::string(lldb_history_file.str());
      }
    }

    if (m_path.empty())
      return nullptr;

    return m_path.c_str();
  }

public:
  ~EditlineHistory() {
    Save();

    if (m_history) {
      history_wend(m_history);
      m_history = nullptr;
    }
  }

  static EditlineHistorySP GetHistory(const std::string &prefix) {
    typedef std::map<std::string, EditlineHistoryWP> WeakHistoryMap;
    static std::recursive_mutex g_mutex;
    static WeakHistoryMap g_weak_map;
    std::lock_guard<std::recursive_mutex> guard(g_mutex);
    WeakHistoryMap::const_iterator pos = g_weak_map.find(prefix);
    EditlineHistorySP history_sp;
    if (pos != g_weak_map.end()) {
      history_sp = pos->second.lock();
      if (history_sp)
        return history_sp;
      g_weak_map.erase(pos);
    }
    history_sp.reset(new EditlineHistory(prefix, 800, true));
    g_weak_map[prefix] = history_sp;
    return history_sp;
  }

  bool IsValid() const { return m_history != nullptr; }

  HistoryW *GetHistoryPtr() { return m_history; }

  void Enter(const EditLineCharType *line_cstr) {
    if (m_history)
      history_w(m_history, &m_event, H_ENTER, line_cstr);
  }

  bool Load() {
    if (m_history) {
      const char *path = GetHistoryFilePath();
      if (path) {
        history_w(m_history, &m_event, H_LOAD, path);
        return true;
      }
    }
    return false;
  }

  bool Save() {
    if (m_history) {
      const char *path = GetHistoryFilePath();
      if (path) {
        history_w(m_history, &m_event, H_SAVE, path);
        return true;
      }
    }
    return false;
  }

protected:
  /// The history object.
  HistoryW *m_history = nullptr;
  /// The history event needed to contain all history events.
  HistEventW m_event;
  /// The prefix name (usually the editline program name) to use when
  /// loading/saving history.
  std::string m_prefix;
  /// Path to the history file.
  std::string m_path;
};
}
}

// Editline private methods

void Editline::SetBaseLineNumber(int line_number) {
  m_base_line_number = line_number;
  m_line_number_digits =
      std::max<int>(3, std::to_string(line_number).length() + 1);
}

std::string Editline::PromptForIndex(int line_index) {
  bool use_line_numbers = m_multiline_enabled && m_base_line_number > 0;
  std::string prompt = m_set_prompt;
  if (use_line_numbers && prompt.length() == 0)
    prompt = ": ";
  std::string continuation_prompt = prompt;
  if (m_set_continuation_prompt.length() > 0) {
    continuation_prompt = m_set_continuation_prompt;
    // Ensure that both prompts are the same length through space padding
    const size_t prompt_width = ColumnWidth(prompt);
    const size_t cont_prompt_width = ColumnWidth(continuation_prompt);
    const size_t padded_prompt_width =
        std::max(prompt_width, cont_prompt_width);
    if (prompt_width < padded_prompt_width)
      prompt += std::string(padded_prompt_width - prompt_width, ' ');
    else if (cont_prompt_width < padded_prompt_width)
      continuation_prompt +=
          std::string(padded_prompt_width - cont_prompt_width, ' ');
  }

  if (use_line_numbers) {
    StreamString prompt_stream;
    prompt_stream.Printf(
        "%*d%s", m_line_number_digits, m_base_line_number + line_index,
        (line_index == 0) ? prompt.c_str() : continuation_prompt.c_str());
    return std::string(std::move(prompt_stream.GetString()));
  }
  return (line_index == 0) ? prompt : continuation_prompt;
}

void Editline::SetCurrentLine(int line_index) {
  m_current_line_index = line_index;
  m_current_prompt = PromptForIndex(line_index);
}

size_t Editline::GetPromptWidth() { return ColumnWidth(PromptForIndex(0)); }

bool Editline::IsEmacs() {
  const char *editor;
  el_get(m_editline, EL_EDITOR, &editor);
  return editor[0] == 'e';
}

bool Editline::IsOnlySpaces() {
  const LineInfoW *info = el_wline(m_editline);
  for (const EditLineCharType *character = info->buffer;
       character < info->lastchar; character++) {
    if (*character != ' ')
      return false;
  }
  return true;
}

int Editline::GetLineIndexForLocation(CursorLocation location, int cursor_row) {
  int line = 0;
  if (location == CursorLocation::EditingPrompt ||
      location == CursorLocation::BlockEnd ||
      location == CursorLocation::EditingCursor) {
    for (unsigned index = 0; index < m_current_line_index; index++) {
      line += CountRowsForLine(m_input_lines[index]);
    }
    if (location == CursorLocation::EditingCursor) {
      line += cursor_row;
    } else if (location == CursorLocation::BlockEnd) {
      for (unsigned index = m_current_line_index; index < m_input_lines.size();
           index++) {
        line += CountRowsForLine(m_input_lines[index]);
      }
      --line;
    }
  }
  return line;
}

void Editline::MoveCursor(CursorLocation from, CursorLocation to) {
  const LineInfoW *info = el_wline(m_editline);
  int editline_cursor_position =
      (int)((info->cursor - info->buffer) + GetPromptWidth());
  int editline_cursor_row = editline_cursor_position / m_terminal_width;

  // Determine relative starting and ending lines
  int fromLine = GetLineIndexForLocation(from, editline_cursor_row);
  int toLine = GetLineIndexForLocation(to, editline_cursor_row);
  if (toLine != fromLine) {
    fprintf(m_output_file,
            (toLine > fromLine) ? ANSI_DOWN_N_ROWS : ANSI_UP_N_ROWS,
            std::abs(toLine - fromLine));
  }

  // Determine target column
  int toColumn = 1;
  if (to == CursorLocation::EditingCursor) {
    toColumn =
        editline_cursor_position - (editline_cursor_row * m_terminal_width) + 1;
  } else if (to == CursorLocation::BlockEnd && !m_input_lines.empty()) {
    toColumn =
        ((m_input_lines[m_input_lines.size() - 1].length() + GetPromptWidth()) %
         80) +
        1;
  }
  fprintf(m_output_file, ANSI_SET_COLUMN_N, toColumn);
}

void Editline::DisplayInput(int firstIndex) {
  fprintf(m_output_file, ANSI_SET_COLUMN_N ANSI_CLEAR_BELOW, 1);
  int line_count = (int)m_input_lines.size();
  for (int index = firstIndex; index < line_count; index++) {
    fprintf(m_output_file,
            "%s"
            "%s"
            "%s" EditLineStringFormatSpec " ",
            m_prompt_ansi_prefix.c_str(), PromptForIndex(index).c_str(),
            m_prompt_ansi_suffix.c_str(), m_input_lines[index].c_str());
    if (index < line_count - 1)
      fprintf(m_output_file, "\n");
  }
}

int Editline::CountRowsForLine(const EditLineStringType &content) {
  std::string prompt =
      PromptForIndex(0); // Prompt width is constant during an edit session
  int line_length = (int)(content.length() + ColumnWidth(prompt));
  return (line_length / m_terminal_width) + 1;
}

void Editline::SaveEditedLine() {
  const LineInfoW *info = el_wline(m_editline);
  m_input_lines[m_current_line_index] =
      EditLineStringType(info->buffer, info->lastchar - info->buffer);
}

StringList Editline::GetInputAsStringList(int line_count) {
  StringList lines;
  for (EditLineStringType line : m_input_lines) {
    if (line_count == 0)
      break;
#if LLDB_EDITLINE_USE_WCHAR
    lines.AppendString(m_utf8conv.to_bytes(line));
#else
    lines.AppendString(line);
#endif
    --line_count;
  }
  return lines;
}

unsigned char Editline::RecallHistory(HistoryOperation op) {
  assert(op == HistoryOperation::Older || op == HistoryOperation::Newer);
  if (!m_history_sp || !m_history_sp->IsValid())
    return CC_ERROR;

  HistoryW *pHistory = m_history_sp->GetHistoryPtr();
  HistEventW history_event;
  std::vector<EditLineStringType> new_input_lines;

  // Treat moving from the "live" entry differently
  if (!m_in_history) {
    switch (op) {
    case HistoryOperation::Newer:
      return CC_ERROR; // Can't go newer than the "live" entry
    case HistoryOperation::Older: {
      if (history_w(pHistory, &history_event,
                    GetOperation(HistoryOperation::Newest)) == -1)
        return CC_ERROR;
      // Save any edits to the "live" entry in case we return by moving forward
      // in history (it would be more bash-like to save over any current entry,
      // but libedit doesn't offer the ability to add entries anywhere except
      // the end.)
      SaveEditedLine();
      m_live_history_lines = m_input_lines;
      m_in_history = true;
    } break;
    default:
      llvm_unreachable("unsupported history direction");
    }
  } else {
    if (history_w(pHistory, &history_event, GetOperation(op)) == -1) {
      switch (op) {
      case HistoryOperation::Older:
        // Can't move earlier than the earliest entry.
        return CC_ERROR;
      case HistoryOperation::Newer:
        // Moving to newer-than-the-newest entry yields the "live" entry.
        new_input_lines = m_live_history_lines;
        m_in_history = false;
        break;
      default:
        llvm_unreachable("unsupported history direction");
      }
    }
  }

  // If we're pulling the lines from history, split them apart
  if (m_in_history)
    new_input_lines = SplitLines(history_event.str);

  // Erase the current edit session and replace it with a new one
  MoveCursor(CursorLocation::EditingCursor, CursorLocation::BlockStart);
  m_input_lines = new_input_lines;
  DisplayInput();

  // Prepare to edit the last line when moving to previous entry, or the first
  // line when moving to next entry
  switch (op) {
  case HistoryOperation::Older:
    m_current_line_index = (int)m_input_lines.size() - 1;
    break;
  case HistoryOperation::Newer:
    m_current_line_index = 0;
    break;
  default:
    llvm_unreachable("unsupported history direction");
  }
  SetCurrentLine(m_current_line_index);
  MoveCursor(CursorLocation::BlockEnd, CursorLocation::EditingPrompt);
  return CC_NEWLINE;
}

int Editline::GetCharacter(EditLineGetCharType *c) {
  const LineInfoW *info = el_wline(m_editline);

  // Paint a ANSI formatted version of the desired prompt over the version
  // libedit draws. (will only be requested if colors are supported)
  if (m_needs_prompt_repaint) {
    MoveCursor(CursorLocation::EditingCursor, CursorLocation::EditingPrompt);
    fprintf(m_output_file,
            "%s"
            "%s"
            "%s",
            m_prompt_ansi_prefix.c_str(), Prompt(),
            m_prompt_ansi_suffix.c_str());
    MoveCursor(CursorLocation::EditingPrompt, CursorLocation::EditingCursor);
    m_needs_prompt_repaint = false;
  }

  if (m_multiline_enabled) {
    // Detect when the number of rows used for this input line changes due to
    // an edit
    int lineLength = (int)((info->lastchar - info->buffer) + GetPromptWidth());
    int new_line_rows = (lineLength / m_terminal_width) + 1;
    if (m_current_line_rows != -1 && new_line_rows != m_current_line_rows) {
      // Respond by repainting the current state from this line on
      MoveCursor(CursorLocation::EditingCursor, CursorLocation::EditingPrompt);
      SaveEditedLine();
      DisplayInput(m_current_line_index);
      MoveCursor(CursorLocation::BlockEnd, CursorLocation::EditingCursor);
    }
    m_current_line_rows = new_line_rows;
  }

  // Read an actual character
  while (true) {
    lldb::ConnectionStatus status = lldb::eConnectionStatusSuccess;
    char ch = 0;

    if (m_terminal_size_has_changed)
      ApplyTerminalSizeChange();

    // This mutex is locked by our caller (GetLine). Unlock it while we read a
    // character (blocking operation), so we do not hold the mutex
    // indefinitely. This gives a chance for someone to interrupt us. After
    // Read returns, immediately lock the mutex again and check if we were
    // interrupted.
    m_output_mutex.unlock();
    int read_count =
        m_input_connection.Read(&ch, 1, std::nullopt, status, nullptr);
    m_output_mutex.lock();
    if (m_editor_status == EditorStatus::Interrupted) {
      while (read_count > 0 && status == lldb::eConnectionStatusSuccess)
        read_count =
            m_input_connection.Read(&ch, 1, std::nullopt, status, nullptr);
      lldbassert(status == lldb::eConnectionStatusInterrupted);
      return 0;
    }

    if (read_count) {
      if (CompleteCharacter(ch, *c))
        return 1;
    } else {
      switch (status) {
      case lldb::eConnectionStatusSuccess: // Success
        break;

      case lldb::eConnectionStatusInterrupted:
        llvm_unreachable("Interrupts should have been handled above.");

      case lldb::eConnectionStatusError:        // Check GetError() for details
      case lldb::eConnectionStatusTimedOut:     // Request timed out
      case lldb::eConnectionStatusEndOfFile:    // End-of-file encountered
      case lldb::eConnectionStatusNoConnection: // No connection
      case lldb::eConnectionStatusLostConnection: // Lost connection while
                                                  // connected to a valid
                                                  // connection
        m_editor_status = EditorStatus::EndOfInput;
        return 0;
      }
    }
  }
}

const char *Editline::Prompt() {
  if (!m_prompt_ansi_prefix.empty() || !m_prompt_ansi_suffix.empty())
    m_needs_prompt_repaint = true;
  return m_current_prompt.c_str();
}

unsigned char Editline::BreakLineCommand(int ch) {
  // Preserve any content beyond the cursor, truncate and save the current line
  const LineInfoW *info = el_wline(m_editline);
  auto current_line =
      EditLineStringType(info->buffer, info->cursor - info->buffer);
  auto new_line_fragment =
      EditLineStringType(info->cursor, info->lastchar - info->cursor);
  m_input_lines[m_current_line_index] = current_line;

  // Ignore whitespace-only extra fragments when breaking a line
  if (::IsOnlySpaces(new_line_fragment))
    new_line_fragment = EditLineConstString("");

  // Establish the new cursor position at the start of a line when inserting a
  // line break
  m_revert_cursor_index = 0;

  // Don't perform automatic formatting when pasting
  if (!IsInputPending(m_input_file)) {
    // Apply smart indentation
    if (m_fix_indentation_callback) {
      StringList lines = GetInputAsStringList(m_current_line_index + 1);
#if LLDB_EDITLINE_USE_WCHAR
      lines.AppendString(m_utf8conv.to_bytes(new_line_fragment));
#else
      lines.AppendString(new_line_fragment);
#endif

      int indent_correction = m_fix_indentation_callback(this, lines, 0);
      new_line_fragment = FixIndentation(new_line_fragment, indent_correction);
      m_revert_cursor_index = GetIndentation(new_line_fragment);
    }
  }

  // Insert the new line and repaint everything from the split line on down
  m_input_lines.insert(m_input_lines.begin() + m_current_line_index + 1,
                       new_line_fragment);
  MoveCursor(CursorLocation::EditingCursor, CursorLocation::EditingPrompt);
  DisplayInput(m_current_line_index);

  // Reposition the cursor to the right line and prepare to edit the new line
  SetCurrentLine(m_current_line_index + 1);
  MoveCursor(CursorLocation::BlockEnd, CursorLocation::EditingPrompt);
  return CC_NEWLINE;
}

unsigned char Editline::EndOrAddLineCommand(int ch) {
  // Don't perform end of input detection when pasting, always treat this as a
  // line break
  if (IsInputPending(m_input_file)) {
    return BreakLineCommand(ch);
  }

  // Save any edits to this line
  SaveEditedLine();

  // If this is the end of the last line, consider whether to add a line
  // instead
  const LineInfoW *info = el_wline(m_editline);
  if (m_current_line_index == m_input_lines.size() - 1 &&
      info->cursor == info->lastchar) {
    if (m_is_input_complete_callback) {
      auto lines = GetInputAsStringList();
      if (!m_is_input_complete_callback(this, lines)) {
        return BreakLineCommand(ch);
      }

      // The completion test is allowed to change the input lines when complete
      m_input_lines.clear();
      for (unsigned index = 0; index < lines.GetSize(); index++) {
#if LLDB_EDITLINE_USE_WCHAR
        m_input_lines.insert(m_input_lines.end(),
                             m_utf8conv.from_bytes(lines[index]));
#else
        m_input_lines.insert(m_input_lines.end(), lines[index]);
#endif
      }
    }
  }
  MoveCursor(CursorLocation::EditingCursor, CursorLocation::BlockEnd);
  fprintf(m_output_file, "\n");
  m_editor_status = EditorStatus::Complete;
  return CC_NEWLINE;
}

unsigned char Editline::DeleteNextCharCommand(int ch) {
  LineInfoW *info = const_cast<LineInfoW *>(el_wline(m_editline));

  // Just delete the next character normally if possible
  if (info->cursor < info->lastchar) {
    info->cursor++;
    el_deletestr(m_editline, 1);
    return CC_REFRESH;
  }

  // Fail when at the end of the last line, except when ^D is pressed on the
  // line is empty, in which case it is treated as EOF
  if (m_current_line_index == m_input_lines.size() - 1) {
    if (ch == 4 && info->buffer == info->lastchar) {
      fprintf(m_output_file, "^D\n");
      m_editor_status = EditorStatus::EndOfInput;
      return CC_EOF;
    }
    return CC_ERROR;
  }

  // Prepare to combine this line with the one below
  MoveCursor(CursorLocation::EditingCursor, CursorLocation::EditingPrompt);

  // Insert the next line of text at the cursor and restore the cursor position
  const EditLineCharType *cursor = info->cursor;
  el_winsertstr(m_editline, m_input_lines[m_current_line_index + 1].c_str());
  info->cursor = cursor;
  SaveEditedLine();

  // Delete the extra line
  m_input_lines.erase(m_input_lines.begin() + m_current_line_index + 1);

  // Clear and repaint from this line on down
  DisplayInput(m_current_line_index);
  MoveCursor(CursorLocation::BlockEnd, CursorLocation::EditingCursor);
  return CC_REFRESH;
}

unsigned char Editline::DeletePreviousCharCommand(int ch) {
  LineInfoW *info = const_cast<LineInfoW *>(el_wline(m_editline));

  // Just delete the previous character normally when not at the start of a
  // line
  if (info->cursor > info->buffer) {
    el_deletestr(m_editline, 1);
    return CC_REFRESH;
  }

  // No prior line and no prior character?  Let the user know
  if (m_current_line_index == 0)
    return CC_ERROR;

  // No prior character, but prior line?  Combine with the line above
  SaveEditedLine();
  SetCurrentLine(m_current_line_index - 1);
  auto priorLine = m_input_lines[m_current_line_index];
  m_input_lines.erase(m_input_lines.begin() + m_current_line_index);
  m_input_lines[m_current_line_index] =
      priorLine + m_input_lines[m_current_line_index];

  // Repaint from the new line down
  fprintf(m_output_file, ANSI_UP_N_ROWS ANSI_SET_COLUMN_N,
          CountRowsForLine(priorLine), 1);
  DisplayInput(m_current_line_index);

  // Put the cursor back where libedit expects it to be before returning to
  // editing by telling libedit about the newly inserted text
  MoveCursor(CursorLocation::BlockEnd, CursorLocation::EditingPrompt);
  el_winsertstr(m_editline, priorLine.c_str());
  return CC_REDISPLAY;
}

unsigned char Editline::PreviousLineCommand(int ch) {
  SaveEditedLine();

  if (m_current_line_index == 0) {
    return RecallHistory(HistoryOperation::Older);
  }

  // Start from a known location
  MoveCursor(CursorLocation::EditingCursor, CursorLocation::EditingPrompt);

  // Treat moving up from a blank last line as a deletion of that line
  if (m_current_line_index == m_input_lines.size() - 1 && IsOnlySpaces()) {
    m_input_lines.erase(m_input_lines.begin() + m_current_line_index);
    fprintf(m_output_file, ANSI_CLEAR_BELOW);
  }

  SetCurrentLine(m_current_line_index - 1);
  fprintf(m_output_file, ANSI_UP_N_ROWS ANSI_SET_COLUMN_N,
          CountRowsForLine(m_input_lines[m_current_line_index]), 1);
  return CC_NEWLINE;
}

unsigned char Editline::NextLineCommand(int ch) {
  SaveEditedLine();

  // Handle attempts to move down from the last line
  if (m_current_line_index == m_input_lines.size() - 1) {
    // Don't add an extra line if the existing last line is blank, move through
    // history instead
    if (IsOnlySpaces()) {
      return RecallHistory(HistoryOperation::Newer);
    }

    // Determine indentation for the new line
    int indentation = 0;
    if (m_fix_indentation_callback) {
      StringList lines = GetInputAsStringList();
      lines.AppendString("");
      indentation = m_fix_indentation_callback(this, lines, 0);
    }
    m_input_lines.insert(
        m_input_lines.end(),
        EditLineStringType(indentation, EditLineCharType(' ')));
  }

  // Move down past the current line using newlines to force scrolling if
  // needed
  SetCurrentLine(m_current_line_index + 1);
  const LineInfoW *info = el_wline(m_editline);
  int cursor_position = (int)((info->cursor - info->buffer) + GetPromptWidth());
  int cursor_row = cursor_position / m_terminal_width;
  for (int line_count = 0; line_count < m_current_line_rows - cursor_row;
       line_count++) {
    fprintf(m_output_file, "\n");
  }
  return CC_NEWLINE;
}

unsigned char Editline::PreviousHistoryCommand(int ch) {
  SaveEditedLine();

  return RecallHistory(HistoryOperation::Older);
}

unsigned char Editline::NextHistoryCommand(int ch) {
  SaveEditedLine();

  return RecallHistory(HistoryOperation::Newer);
}

unsigned char Editline::FixIndentationCommand(int ch) {
  if (!m_fix_indentation_callback)
    return CC_NORM;

  // Insert the character typed before proceeding
  EditLineCharType inserted[] = {(EditLineCharType)ch, 0};
  el_winsertstr(m_editline, inserted);
  LineInfoW *info = const_cast<LineInfoW *>(el_wline(m_editline));
  int cursor_position = info->cursor - info->buffer;

  // Save the edits and determine the correct indentation level
  SaveEditedLine();
  StringList lines = GetInputAsStringList(m_current_line_index + 1);
  int indent_correction =
      m_fix_indentation_callback(this, lines, cursor_position);

  // If it is already correct no special work is needed
  if (indent_correction == 0)
    return CC_REFRESH;

  // Change the indentation level of the line
  std::string currentLine = lines.GetStringAtIndex(m_current_line_index);
  if (indent_correction > 0) {
    currentLine = currentLine.insert(0, indent_correction, ' ');
  } else {
    currentLine = currentLine.erase(0, -indent_correction);
  }
#if LLDB_EDITLINE_USE_WCHAR
  m_input_lines[m_current_line_index] = m_utf8conv.from_bytes(currentLine);
#else
  m_input_lines[m_current_line_index] = currentLine;
#endif

  // Update the display to reflect the change
  MoveCursor(CursorLocation::EditingCursor, CursorLocation::EditingPrompt);
  DisplayInput(m_current_line_index);

  // Reposition the cursor back on the original line and prepare to restart
  // editing with a new cursor position
  SetCurrentLine(m_current_line_index);
  MoveCursor(CursorLocation::BlockEnd, CursorLocation::EditingPrompt);
  m_revert_cursor_index = cursor_position + indent_correction;
  return CC_NEWLINE;
}

unsigned char Editline::RevertLineCommand(int ch) {
  el_winsertstr(m_editline, m_input_lines[m_current_line_index].c_str());
  if (m_revert_cursor_index >= 0) {
    LineInfoW *info = const_cast<LineInfoW *>(el_wline(m_editline));
    info->cursor = info->buffer + m_revert_cursor_index;
    if (info->cursor > info->lastchar) {
      info->cursor = info->lastchar;
    }
    m_revert_cursor_index = -1;
  }
  return CC_REFRESH;
}

unsigned char Editline::BufferStartCommand(int ch) {
  SaveEditedLine();
  MoveCursor(CursorLocation::EditingCursor, CursorLocation::BlockStart);
  SetCurrentLine(0);
  m_revert_cursor_index = 0;
  return CC_NEWLINE;
}

unsigned char Editline::BufferEndCommand(int ch) {
  SaveEditedLine();
  MoveCursor(CursorLocation::EditingCursor, CursorLocation::BlockEnd);
  SetCurrentLine((int)m_input_lines.size() - 1);
  MoveCursor(CursorLocation::BlockEnd, CursorLocation::EditingPrompt);
  return CC_NEWLINE;
}

/// Prints completions and their descriptions to the given file. Only the
/// completions in the interval [start, end) are printed.
static void
PrintCompletion(FILE *output_file,
                llvm::ArrayRef<CompletionResult::Completion> results,
                size_t max_len) {
  for (const CompletionResult::Completion &c : results) {
    fprintf(output_file, "\t%-*s", (int)max_len, c.GetCompletion().c_str());
    if (!c.GetDescription().empty())
      fprintf(output_file, " -- %s", c.GetDescription().c_str());
    fprintf(output_file, "\n");
  }
}

void Editline::DisplayCompletions(
    Editline &editline, llvm::ArrayRef<CompletionResult::Completion> results) {
  assert(!results.empty());

  fprintf(editline.m_output_file,
          "\n" ANSI_CLEAR_BELOW "Available completions:\n");
  const size_t page_size = 40;
  bool all = false;

  auto longest =
      std::max_element(results.begin(), results.end(), [](auto &c1, auto &c2) {
        return c1.GetCompletion().size() < c2.GetCompletion().size();
      });

  const size_t max_len = longest->GetCompletion().size();

  if (results.size() < page_size) {
    PrintCompletion(editline.m_output_file, results, max_len);
    return;
  }

  size_t cur_pos = 0;
  while (cur_pos < results.size()) {
    size_t remaining = results.size() - cur_pos;
    size_t next_size = all ? remaining : std::min(page_size, remaining);

    PrintCompletion(editline.m_output_file, results.slice(cur_pos, next_size),
                    max_len);

    cur_pos += next_size;

    if (cur_pos >= results.size())
      break;

    fprintf(editline.m_output_file, "More (Y/n/a): ");
    // The type for the output and the type for the parameter are different,
    // to allow interoperability with older versions of libedit. The container
    // for the reply must be as wide as what our implementation is using,
    // but libedit may use a narrower type depending on the build
    // configuration.
    EditLineGetCharType reply = L'n';
    int got_char = el_wgetc(editline.m_editline,
                            reinterpret_cast<EditLineCharType *>(&reply));
    // Check for a ^C or other interruption.
    if (editline.m_editor_status == EditorStatus::Interrupted) {
      editline.m_editor_status = EditorStatus::Editing;
      fprintf(editline.m_output_file, "^C\n");
      break;
    }

    fprintf(editline.m_output_file, "\n");
    if (got_char == -1 || reply == 'n')
      break;
    if (reply == 'a')
      all = true;
  }
}

unsigned char Editline::TabCommand(int ch) {
  if (!m_completion_callback)
    return CC_ERROR;

  const LineInfo *line_info = el_line(m_editline);

  llvm::StringRef line(line_info->buffer,
                       line_info->lastchar - line_info->buffer);
  unsigned cursor_index = line_info->cursor - line_info->buffer;
  CompletionResult result;
  CompletionRequest request(line, cursor_index, result);

  m_completion_callback(request);

  llvm::ArrayRef<CompletionResult::Completion> results = result.GetResults();

  StringList completions;
  result.GetMatches(completions);

  if (results.size() == 0)
    return CC_ERROR;

  if (results.size() == 1) {
    CompletionResult::Completion completion = results.front();
    switch (completion.GetMode()) {
    case CompletionMode::Normal: {
      std::string to_add = completion.GetCompletion();
      // Terminate the current argument with a quote if it started with a quote.
      Args &parsedLine = request.GetParsedLine();
      if (!parsedLine.empty() && request.GetCursorIndex() < parsedLine.size() &&
          request.GetParsedArg().IsQuoted()) {
        to_add.push_back(request.GetParsedArg().GetQuoteChar());
      }
      to_add.push_back(' ');
      el_deletestr(m_editline, request.GetCursorArgumentPrefix().size());
      el_insertstr(m_editline, to_add.c_str());
      // Clear all the autosuggestion parts if the only single space can be completed.
      if (to_add == " ")
        return CC_REDISPLAY;
      return CC_REFRESH;
    }
    case CompletionMode::Partial: {
      std::string to_add = completion.GetCompletion();
      to_add = to_add.substr(request.GetCursorArgumentPrefix().size());
      el_insertstr(m_editline, to_add.c_str());
      break;
    }
    case CompletionMode::RewriteLine: {
      el_deletestr(m_editline, line_info->cursor - line_info->buffer);
      el_insertstr(m_editline, completion.GetCompletion().c_str());
      break;
    }
    }
    return CC_REDISPLAY;
  }

  // If we get a longer match display that first.
  std::string longest_prefix = completions.LongestCommonPrefix();
  if (!longest_prefix.empty())
    longest_prefix =
        longest_prefix.substr(request.GetCursorArgumentPrefix().size());
  if (!longest_prefix.empty()) {
    el_insertstr(m_editline, longest_prefix.c_str());
    return CC_REDISPLAY;
  }

  DisplayCompletions(*this, results);

  DisplayInput();
  MoveCursor(CursorLocation::BlockEnd, CursorLocation::EditingCursor);
  return CC_REDISPLAY;
}

unsigned char Editline::ApplyAutosuggestCommand(int ch) {
  if (!m_suggestion_callback) {
    return CC_REDISPLAY;
  }

  const LineInfo *line_info = el_line(m_editline);
  llvm::StringRef line(line_info->buffer,
                       line_info->lastchar - line_info->buffer);

  if (std::optional<std::string> to_add = m_suggestion_callback(line))
    el_insertstr(m_editline, to_add->c_str());

  return CC_REDISPLAY;
}

unsigned char Editline::TypedCharacter(int ch) {
  std::string typed = std::string(1, ch);
  el_insertstr(m_editline, typed.c_str());

  if (!m_suggestion_callback) {
    return CC_REDISPLAY;
  }

  const LineInfo *line_info = el_line(m_editline);
  llvm::StringRef line(line_info->buffer,
                       line_info->lastchar - line_info->buffer);

  if (std::optional<std::string> to_add = m_suggestion_callback(line)) {
    std::string to_add_color =
        m_suggestion_ansi_prefix + to_add.value() + m_suggestion_ansi_suffix;
    fputs(typed.c_str(), m_output_file);
    fputs(to_add_color.c_str(), m_output_file);
    size_t new_autosuggestion_size = line.size() + to_add->length();
    // Print spaces to hide any remains of a previous longer autosuggestion.
    if (new_autosuggestion_size < m_previous_autosuggestion_size) {
      size_t spaces_to_print =
          m_previous_autosuggestion_size - new_autosuggestion_size;
      std::string spaces = std::string(spaces_to_print, ' ');
      fputs(spaces.c_str(), m_output_file);
    }
    m_previous_autosuggestion_size = new_autosuggestion_size;

    int editline_cursor_position =
        (int)((line_info->cursor - line_info->buffer) + GetPromptWidth());
    int editline_cursor_row = editline_cursor_position / m_terminal_width;
    int toColumn =
        editline_cursor_position - (editline_cursor_row * m_terminal_width);
    fprintf(m_output_file, ANSI_SET_COLUMN_N, toColumn);
    return CC_REFRESH;
  }

  return CC_REDISPLAY;
}

void Editline::AddFunctionToEditLine(const EditLineCharType *command,
                                     const EditLineCharType *helptext,
                                     EditlineCommandCallbackType callbackFn) {
  el_wset(m_editline, EL_ADDFN, command, helptext, callbackFn);
}

void Editline::SetEditLinePromptCallback(
    EditlinePromptCallbackType callbackFn) {
  el_set(m_editline, EL_PROMPT, callbackFn);
}

void Editline::SetGetCharacterFunction(EditlineGetCharCallbackType callbackFn) {
  el_wset(m_editline, EL_GETCFN, callbackFn);
}

void Editline::ConfigureEditor(bool multiline) {
  if (m_editline && m_multiline_enabled == multiline)
    return;
  m_multiline_enabled = multiline;

  if (m_editline) {
    // Disable edit mode to stop the terminal from flushing all input during
    // the call to el_end() since we expect to have multiple editline instances
    // in this program.
    el_set(m_editline, EL_EDITMODE, 0);
    el_end(m_editline);
  }

  m_editline =
      el_init(m_editor_name.c_str(), m_input_file, m_output_file, m_error_file);
  ApplyTerminalSizeChange();

  if (m_history_sp && m_history_sp->IsValid()) {
    if (!m_history_sp->Load()) {
        fputs("Could not load history file\n.", m_output_file);
    }
    el_wset(m_editline, EL_HIST, history, m_history_sp->GetHistoryPtr());
  }
  el_set(m_editline, EL_CLIENTDATA, this);
  el_set(m_editline, EL_SIGNAL, 0);
  el_set(m_editline, EL_EDITOR, "emacs");

  SetGetCharacterFunction([](EditLine *editline, EditLineGetCharType *c) {
    return Editline::InstanceFor(editline)->GetCharacter(c);
  });

  SetEditLinePromptCallback([](EditLine *editline) {
    return Editline::InstanceFor(editline)->Prompt();
  });

  // Commands used for multiline support, registered whether or not they're
  // used
  AddFunctionToEditLine(
      EditLineConstString("lldb-break-line"),
      EditLineConstString("Insert a line break"),
      [](EditLine *editline, int ch) {
        return Editline::InstanceFor(editline)->BreakLineCommand(ch);
      });

  AddFunctionToEditLine(
      EditLineConstString("lldb-end-or-add-line"),
      EditLineConstString("End editing or continue when incomplete"),
      [](EditLine *editline, int ch) {
        return Editline::InstanceFor(editline)->EndOrAddLineCommand(ch);
      });
  AddFunctionToEditLine(
      EditLineConstString("lldb-delete-next-char"),
      EditLineConstString("Delete next character"),
      [](EditLine *editline, int ch) {
        return Editline::InstanceFor(editline)->DeleteNextCharCommand(ch);
      });
  AddFunctionToEditLine(
      EditLineConstString("lldb-delete-previous-char"),
      EditLineConstString("Delete previous character"),
      [](EditLine *editline, int ch) {
        return Editline::InstanceFor(editline)->DeletePreviousCharCommand(ch);
      });
  AddFunctionToEditLine(
      EditLineConstString("lldb-previous-line"),
      EditLineConstString("Move to previous line"),
      [](EditLine *editline, int ch) {
        return Editline::InstanceFor(editline)->PreviousLineCommand(ch);
      });
  AddFunctionToEditLine(
      EditLineConstString("lldb-next-line"),
      EditLineConstString("Move to next line"), [](EditLine *editline, int ch) {
        return Editline::InstanceFor(editline)->NextLineCommand(ch);
      });
  AddFunctionToEditLine(
      EditLineConstString("lldb-previous-history"),
      EditLineConstString("Move to previous history"),
      [](EditLine *editline, int ch) {
        return Editline::InstanceFor(editline)->PreviousHistoryCommand(ch);
      });
  AddFunctionToEditLine(
      EditLineConstString("lldb-next-history"),
      EditLineConstString("Move to next history"),
      [](EditLine *editline, int ch) {
        return Editline::InstanceFor(editline)->NextHistoryCommand(ch);
      });
  AddFunctionToEditLine(
      EditLineConstString("lldb-buffer-start"),
      EditLineConstString("Move to start of buffer"),
      [](EditLine *editline, int ch) {
        return Editline::InstanceFor(editline)->BufferStartCommand(ch);
      });
  AddFunctionToEditLine(
      EditLineConstString("lldb-buffer-end"),
      EditLineConstString("Move to end of buffer"),
      [](EditLine *editline, int ch) {
        return Editline::InstanceFor(editline)->BufferEndCommand(ch);
      });
  AddFunctionToEditLine(
      EditLineConstString("lldb-fix-indentation"),
      EditLineConstString("Fix line indentation"),
      [](EditLine *editline, int ch) {
        return Editline::InstanceFor(editline)->FixIndentationCommand(ch);
      });

  // Register the complete callback under two names for compatibility with
  // older clients using custom .editrc files (largely because libedit has a
  // bad bug where if you have a bind command that tries to bind to a function
  // name that doesn't exist, it can corrupt the heap and crash your process
  // later.)
  EditlineCommandCallbackType complete_callback = [](EditLine *editline,
                                                     int ch) {
    return Editline::InstanceFor(editline)->TabCommand(ch);
  };
  AddFunctionToEditLine(EditLineConstString("lldb-complete"),
                        EditLineConstString("Invoke completion"),
                        complete_callback);
  AddFunctionToEditLine(EditLineConstString("lldb_complete"),
                        EditLineConstString("Invoke completion"),
                        complete_callback);

  // General bindings we don't mind being overridden
  if (!multiline) {
    el_set(m_editline, EL_BIND, "^r", "em-inc-search-prev",
           NULL); // Cycle through backwards search, entering string

    if (m_suggestion_callback) {
      AddFunctionToEditLine(
          EditLineConstString("lldb-apply-complete"),
          EditLineConstString("Adopt autocompletion"),
          [](EditLine *editline, int ch) {
            return Editline::InstanceFor(editline)->ApplyAutosuggestCommand(ch);
          });

      el_set(m_editline, EL_BIND, "^f", "lldb-apply-complete",
             NULL); // Apply a part that is suggested automatically

      AddFunctionToEditLine(
          EditLineConstString("lldb-typed-character"),
          EditLineConstString("Typed character"),
          [](EditLine *editline, int ch) {
            return Editline::InstanceFor(editline)->TypedCharacter(ch);
          });

      char bind_key[2] = {0, 0};
      llvm::StringRef ascii_chars =
          "abcdefghijklmnopqrstuvwxzyABCDEFGHIJKLMNOPQRSTUVWXZY1234567890!\"#$%"
          "&'()*+,./:;<=>?@[]_`{|}~ ";
      for (char c : ascii_chars) {
        bind_key[0] = c;
        el_set(m_editline, EL_BIND, bind_key, "lldb-typed-character", NULL);
      }
      el_set(m_editline, EL_BIND, "\\-", "lldb-typed-character", NULL);
      el_set(m_editline, EL_BIND, "\\^", "lldb-typed-character", NULL);
      el_set(m_editline, EL_BIND, "\\\\", "lldb-typed-character", NULL);
    }
  }

  el_set(m_editline, EL_BIND, "^w", "ed-delete-prev-word",
         NULL); // Delete previous word, behave like bash in emacs mode
  el_set(m_editline, EL_BIND, "\t", "lldb-complete",
         NULL); // Bind TAB to auto complete

  // Allow ctrl-left-arrow and ctrl-right-arrow for navigation, behave like
  // bash in emacs mode.
  el_set(m_editline, EL_BIND, ESCAPE "[1;5C", "em-next-word", NULL);
  el_set(m_editline, EL_BIND, ESCAPE "[1;5D", "ed-prev-word", NULL);
  el_set(m_editline, EL_BIND, ESCAPE "[5C", "em-next-word", NULL);
  el_set(m_editline, EL_BIND, ESCAPE "[5D", "ed-prev-word", NULL);
  el_set(m_editline, EL_BIND, ESCAPE ESCAPE "[C", "em-next-word", NULL);
  el_set(m_editline, EL_BIND, ESCAPE ESCAPE "[D", "ed-prev-word", NULL);

  // Allow user-specific customization prior to registering bindings we
  // absolutely require
  el_source(m_editline, nullptr);

  // Register an internal binding that external developers shouldn't use
  AddFunctionToEditLine(
      EditLineConstString("lldb-revert-line"),
      EditLineConstString("Revert line to saved state"),
      [](EditLine *editline, int ch) {
        return Editline::InstanceFor(editline)->RevertLineCommand(ch);
      });

  // Register keys that perform auto-indent correction
  if (m_fix_indentation_callback && m_fix_indentation_callback_chars) {
    char bind_key[2] = {0, 0};
    const char *indent_chars = m_fix_indentation_callback_chars;
    while (*indent_chars) {
      bind_key[0] = *indent_chars;
      el_set(m_editline, EL_BIND, bind_key, "lldb-fix-indentation", NULL);
      ++indent_chars;
    }
  }

  // Multi-line editor bindings
  if (multiline) {
    el_set(m_editline, EL_BIND, "\n", "lldb-end-or-add-line", NULL);
    el_set(m_editline, EL_BIND, "\r", "lldb-end-or-add-line", NULL);
    el_set(m_editline, EL_BIND, ESCAPE "\n", "lldb-break-line", NULL);
    el_set(m_editline, EL_BIND, ESCAPE "\r", "lldb-break-line", NULL);
    el_set(m_editline, EL_BIND, "^p", "lldb-previous-line", NULL);
    el_set(m_editline, EL_BIND, "^n", "lldb-next-line", NULL);
    el_set(m_editline, EL_BIND, "^?", "lldb-delete-previous-char", NULL);
    el_set(m_editline, EL_BIND, "^d", "lldb-delete-next-char", NULL);
    el_set(m_editline, EL_BIND, ESCAPE "[3~", "lldb-delete-next-char", NULL);
    el_set(m_editline, EL_BIND, ESCAPE "[\\^", "lldb-revert-line", NULL);

    // Editor-specific bindings
    if (IsEmacs()) {
      el_set(m_editline, EL_BIND, ESCAPE "<", "lldb-buffer-start", NULL);
      el_set(m_editline, EL_BIND, ESCAPE ">", "lldb-buffer-end", NULL);
      el_set(m_editline, EL_BIND, ESCAPE "[A", "lldb-previous-line", NULL);
      el_set(m_editline, EL_BIND, ESCAPE "[B", "lldb-next-line", NULL);
      el_set(m_editline, EL_BIND, ESCAPE ESCAPE "[A", "lldb-previous-history",
             NULL);
      el_set(m_editline, EL_BIND, ESCAPE ESCAPE "[B", "lldb-next-history",
             NULL);
      el_set(m_editline, EL_BIND, ESCAPE "[1;3A", "lldb-previous-history",
             NULL);
      el_set(m_editline, EL_BIND, ESCAPE "[1;3B", "lldb-next-history", NULL);
    } else {
      el_set(m_editline, EL_BIND, "^H", "lldb-delete-previous-char", NULL);

      el_set(m_editline, EL_BIND, "-a", ESCAPE "[A", "lldb-previous-line",
             NULL);
      el_set(m_editline, EL_BIND, "-a", ESCAPE "[B", "lldb-next-line", NULL);
      el_set(m_editline, EL_BIND, "-a", "x", "lldb-delete-next-char", NULL);
      el_set(m_editline, EL_BIND, "-a", "^H", "lldb-delete-previous-char",
             NULL);
      el_set(m_editline, EL_BIND, "-a", "^?", "lldb-delete-previous-char",
             NULL);

      // Escape is absorbed exiting edit mode, so re-register important
      // sequences without the prefix
      el_set(m_editline, EL_BIND, "-a", "[A", "lldb-previous-line", NULL);
      el_set(m_editline, EL_BIND, "-a", "[B", "lldb-next-line", NULL);
      el_set(m_editline, EL_BIND, "-a", "[\\^", "lldb-revert-line", NULL);
    }
  }
}

// Editline public methods

Editline *Editline::InstanceFor(EditLine *editline) {
  Editline *editor;
  el_get(editline, EL_CLIENTDATA, &editor);
  return editor;
}

Editline::Editline(const char *editline_name, FILE *input_file,
                   FILE *output_file, FILE *error_file,
                   std::recursive_mutex &output_mutex)
    : m_editor_status(EditorStatus::Complete), m_input_file(input_file),
      m_output_file(output_file), m_error_file(error_file),
      m_input_connection(fileno(input_file), false),
      m_output_mutex(output_mutex) {
  // Get a shared history instance
  m_editor_name = (editline_name == nullptr) ? "lldb-tmp" : editline_name;
  m_history_sp = EditlineHistory::GetHistory(m_editor_name);
}

Editline::~Editline() {
  if (m_editline) {
    // Disable edit mode to stop the terminal from flushing all input during
    // the call to el_end() since we expect to have multiple editline instances
    // in this program.
    el_set(m_editline, EL_EDITMODE, 0);
    el_end(m_editline);
    m_editline = nullptr;
  }

  // EditlineHistory objects are sometimes shared between multiple Editline
  // instances with the same program name. So just release our shared pointer
  // and if we are the last owner, it will save the history to the history save
  // file automatically.
  m_history_sp.reset();
}

void Editline::SetPrompt(const char *prompt) {
  m_set_prompt = prompt == nullptr ? "" : prompt;
}

void Editline::SetContinuationPrompt(const char *continuation_prompt) {
  m_set_continuation_prompt =
      continuation_prompt == nullptr ? "" : continuation_prompt;
}

void Editline::TerminalSizeChanged() { m_terminal_size_has_changed = 1; }

void Editline::ApplyTerminalSizeChange() {
  if (!m_editline)
    return;

  m_terminal_size_has_changed = 0;
  el_resize(m_editline);
  int columns;
  // This function is documenting as taking (const char *, void *) for the
  // vararg part, but in reality in was consuming arguments until the first
  // null pointer. This was fixed in libedit in April 2019
  // <http://mail-index.netbsd.org/source-changes/2019/04/26/msg105454.html>,
  // but we're keeping the workaround until a version with that fix is more
  // widely available.
  if (el_get(m_editline, EL_GETTC, "co", &columns, nullptr) == 0) {
    m_terminal_width = columns;
    if (m_current_line_rows != -1) {
      const LineInfoW *info = el_wline(m_editline);
      int lineLength =
          (int)((info->lastchar - info->buffer) + GetPromptWidth());
      m_current_line_rows = (lineLength / columns) + 1;
    }
  } else {
    m_terminal_width = INT_MAX;
    m_current_line_rows = 1;
  }
}

const char *Editline::GetPrompt() { return m_set_prompt.c_str(); }

uint32_t Editline::GetCurrentLine() { return m_current_line_index; }

bool Editline::Interrupt() {
  bool result = true;
  std::lock_guard<std::recursive_mutex> guard(m_output_mutex);
  if (m_editor_status == EditorStatus::Editing) {
    fprintf(m_output_file, "^C\n");
    result = m_input_connection.InterruptRead();
  }
  m_editor_status = EditorStatus::Interrupted;
  return result;
}

bool Editline::Cancel() {
  bool result = true;
  std::lock_guard<std::recursive_mutex> guard(m_output_mutex);
  if (m_editor_status == EditorStatus::Editing) {
    MoveCursor(CursorLocation::EditingCursor, CursorLocation::BlockStart);
    fprintf(m_output_file, ANSI_CLEAR_BELOW);
    result = m_input_connection.InterruptRead();
  }
  m_editor_status = EditorStatus::Interrupted;
  return result;
}

bool Editline::GetLine(std::string &line, bool &interrupted) {
  ConfigureEditor(false);
  m_input_lines = std::vector<EditLineStringType>();
  m_input_lines.insert(m_input_lines.begin(), EditLineConstString(""));

  std::lock_guard<std::recursive_mutex> guard(m_output_mutex);

  lldbassert(m_editor_status != EditorStatus::Editing);
  if (m_editor_status == EditorStatus::Interrupted) {
    m_editor_status = EditorStatus::Complete;
    interrupted = true;
    return true;
  }

  SetCurrentLine(0);
  m_in_history = false;
  m_editor_status = EditorStatus::Editing;
  m_revert_cursor_index = -1;

  int count;
  auto input = el_wgets(m_editline, &count);

  interrupted = m_editor_status == EditorStatus::Interrupted;
  if (!interrupted) {
    if (input == nullptr) {
      fprintf(m_output_file, "\n");
      m_editor_status = EditorStatus::EndOfInput;
    } else {
      m_history_sp->Enter(input);
#if LLDB_EDITLINE_USE_WCHAR
      line = m_utf8conv.to_bytes(SplitLines(input)[0]);
#else
      line = SplitLines(input)[0];
#endif
      m_editor_status = EditorStatus::Complete;
    }
  }
  return m_editor_status != EditorStatus::EndOfInput;
}

bool Editline::GetLines(int first_line_number, StringList &lines,
                        bool &interrupted) {
  ConfigureEditor(true);

  // Print the initial input lines, then move the cursor back up to the start
  // of input
  SetBaseLineNumber(first_line_number);
  m_input_lines = std::vector<EditLineStringType>();
  m_input_lines.insert(m_input_lines.begin(), EditLineConstString(""));

  std::lock_guard<std::recursive_mutex> guard(m_output_mutex);
  // Begin the line editing loop
  DisplayInput();
  SetCurrentLine(0);
  MoveCursor(CursorLocation::BlockEnd, CursorLocation::BlockStart);
  m_editor_status = EditorStatus::Editing;
  m_in_history = false;

  m_revert_cursor_index = -1;
  while (m_editor_status == EditorStatus::Editing) {
    int count;
    m_current_line_rows = -1;
    el_wpush(m_editline, EditLineConstString(
                             "\x1b[^")); // Revert to the existing line content
    el_wgets(m_editline, &count);
  }

  interrupted = m_editor_status == EditorStatus::Interrupted;
  if (!interrupted) {
    // Save the completed entry in history before returning. Don't save empty
    // input as that just clutters the command history.
    if (!m_input_lines.empty())
      m_history_sp->Enter(CombineLines(m_input_lines).c_str());

    lines = GetInputAsStringList();
  }
  return m_editor_status != EditorStatus::EndOfInput;
}

void Editline::PrintAsync(Stream *stream, const char *s, size_t len) {
  std::lock_guard<std::recursive_mutex> guard(m_output_mutex);
  if (m_editor_status == EditorStatus::Editing) {
    SaveEditedLine();
    MoveCursor(CursorLocation::EditingCursor, CursorLocation::BlockStart);
    fprintf(m_output_file, ANSI_CLEAR_BELOW);
  }
  stream->Write(s, len);
  stream->Flush();
  if (m_editor_status == EditorStatus::Editing) {
    DisplayInput();
    MoveCursor(CursorLocation::BlockEnd, CursorLocation::EditingCursor);
  }
}

bool Editline::CompleteCharacter(char ch, EditLineGetCharType &out) {
#if !LLDB_EDITLINE_USE_WCHAR
  if (ch == (char)EOF)
    return false;

  out = (unsigned char)ch;
  return true;
#else
  std::codecvt_utf8<wchar_t> cvt;
  llvm::SmallString<4> input;
  for (;;) {
    const char *from_next;
    wchar_t *to_next;
    std::mbstate_t state = std::mbstate_t();
    input.push_back(ch);
    switch (cvt.in(state, input.begin(), input.end(), from_next, &out, &out + 1,
                   to_next)) {
    case std::codecvt_base::ok:
      return out != (EditLineGetCharType)WEOF;

    case std::codecvt_base::error:
    case std::codecvt_base::noconv:
      return false;

    case std::codecvt_base::partial:
      lldb::ConnectionStatus status;
      size_t read_count = m_input_connection.Read(
          &ch, 1, std::chrono::seconds(0), status, nullptr);
      if (read_count == 0)
        return false;
      break;
    }
  }
#endif
}
