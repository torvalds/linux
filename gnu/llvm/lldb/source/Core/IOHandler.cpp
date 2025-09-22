//===-- IOHandler.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/IOHandler.h"

#if defined(__APPLE__)
#include <deque>
#endif
#include <string>

#include "lldb/Core/Debugger.h"
#include "lldb/Host/Config.h"
#include "lldb/Host/File.h"
#include "lldb/Host/StreamFile.h"
#include "lldb/Utility/AnsiTerminal.h"
#include "lldb/Utility/Predicate.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/StringList.h"
#include "lldb/lldb-forward.h"

#if LLDB_ENABLE_LIBEDIT
#include "lldb/Host/Editline.h"
#endif
#include "lldb/Interpreter/CommandCompletions.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "llvm/ADT/StringRef.h"

#ifdef _WIN32
#include "lldb/Host/windows/windows.h"
#endif

#include <memory>
#include <mutex>
#include <optional>

#include <cassert>
#include <cctype>
#include <cerrno>
#include <clocale>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

using namespace lldb;
using namespace lldb_private;
using llvm::StringRef;

IOHandler::IOHandler(Debugger &debugger, IOHandler::Type type)
    : IOHandler(debugger, type,
                FileSP(),       // Adopt STDIN from top input reader
                StreamFileSP(), // Adopt STDOUT from top input reader
                StreamFileSP(), // Adopt STDERR from top input reader
                0               // Flags

      ) {}

IOHandler::IOHandler(Debugger &debugger, IOHandler::Type type,
                     const lldb::FileSP &input_sp,
                     const lldb::StreamFileSP &output_sp,
                     const lldb::StreamFileSP &error_sp, uint32_t flags)
    : m_debugger(debugger), m_input_sp(input_sp), m_output_sp(output_sp),
      m_error_sp(error_sp), m_popped(false), m_flags(flags), m_type(type),
      m_user_data(nullptr), m_done(false), m_active(false) {
  // If any files are not specified, then adopt them from the top input reader.
  if (!m_input_sp || !m_output_sp || !m_error_sp)
    debugger.AdoptTopIOHandlerFilesIfInvalid(m_input_sp, m_output_sp,
                                             m_error_sp);
}

IOHandler::~IOHandler() = default;

int IOHandler::GetInputFD() {
  return (m_input_sp ? m_input_sp->GetDescriptor() : -1);
}

int IOHandler::GetOutputFD() {
  return (m_output_sp ? m_output_sp->GetFile().GetDescriptor() : -1);
}

int IOHandler::GetErrorFD() {
  return (m_error_sp ? m_error_sp->GetFile().GetDescriptor() : -1);
}

FILE *IOHandler::GetInputFILE() {
  return (m_input_sp ? m_input_sp->GetStream() : nullptr);
}

FILE *IOHandler::GetOutputFILE() {
  return (m_output_sp ? m_output_sp->GetFile().GetStream() : nullptr);
}

FILE *IOHandler::GetErrorFILE() {
  return (m_error_sp ? m_error_sp->GetFile().GetStream() : nullptr);
}

FileSP IOHandler::GetInputFileSP() { return m_input_sp; }

StreamFileSP IOHandler::GetOutputStreamFileSP() { return m_output_sp; }

StreamFileSP IOHandler::GetErrorStreamFileSP() { return m_error_sp; }

bool IOHandler::GetIsInteractive() {
  return GetInputFileSP() ? GetInputFileSP()->GetIsInteractive() : false;
}

bool IOHandler::GetIsRealTerminal() {
  return GetInputFileSP() ? GetInputFileSP()->GetIsRealTerminal() : false;
}

void IOHandler::SetPopped(bool b) { m_popped.SetValue(b, eBroadcastOnChange); }

void IOHandler::WaitForPop() { m_popped.WaitForValueEqualTo(true); }

void IOHandler::PrintAsync(const char *s, size_t len, bool is_stdout) {
  std::lock_guard<std::recursive_mutex> guard(m_output_mutex);
  lldb::StreamFileSP stream = is_stdout ? m_output_sp : m_error_sp;
  stream->Write(s, len);
  stream->Flush();
}

bool IOHandlerStack::PrintAsync(const char *s, size_t len, bool is_stdout) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  if (!m_top)
    return false;
  m_top->PrintAsync(s, len, is_stdout);
  return true;
}

IOHandlerConfirm::IOHandlerConfirm(Debugger &debugger, llvm::StringRef prompt,
                                   bool default_response)
    : IOHandlerEditline(
          debugger, IOHandler::Type::Confirm,
          nullptr, // nullptr editline_name means no history loaded/saved
          llvm::StringRef(), // No prompt
          llvm::StringRef(), // No continuation prompt
          false,             // Multi-line
          false, // Don't colorize the prompt (i.e. the confirm message.)
          0, *this),
      m_default_response(default_response), m_user_response(default_response) {
  StreamString prompt_stream;
  prompt_stream.PutCString(prompt);
  if (m_default_response)
    prompt_stream.Printf(": [Y/n] ");
  else
    prompt_stream.Printf(": [y/N] ");

  SetPrompt(prompt_stream.GetString());
}

IOHandlerConfirm::~IOHandlerConfirm() = default;

void IOHandlerConfirm::IOHandlerComplete(IOHandler &io_handler,
                                         CompletionRequest &request) {
  if (request.GetRawCursorPos() != 0)
    return;
  request.AddCompletion(m_default_response ? "y" : "n");
}

void IOHandlerConfirm::IOHandlerInputComplete(IOHandler &io_handler,
                                              std::string &line) {
  if (line.empty()) {
    // User just hit enter, set the response to the default
    m_user_response = m_default_response;
    io_handler.SetIsDone(true);
    return;
  }

  if (line.size() == 1) {
    switch (line[0]) {
    case 'y':
    case 'Y':
      m_user_response = true;
      io_handler.SetIsDone(true);
      return;
    case 'n':
    case 'N':
      m_user_response = false;
      io_handler.SetIsDone(true);
      return;
    default:
      break;
    }
  }

  if (line == "yes" || line == "YES" || line == "Yes") {
    m_user_response = true;
    io_handler.SetIsDone(true);
  } else if (line == "no" || line == "NO" || line == "No") {
    m_user_response = false;
    io_handler.SetIsDone(true);
  }
}

std::optional<std::string>
IOHandlerDelegate::IOHandlerSuggestion(IOHandler &io_handler,
                                       llvm::StringRef line) {
  return io_handler.GetDebugger()
      .GetCommandInterpreter()
      .GetAutoSuggestionForCommand(line);
}

void IOHandlerDelegate::IOHandlerComplete(IOHandler &io_handler,
                                          CompletionRequest &request) {
  switch (m_completion) {
  case Completion::None:
    break;
  case Completion::LLDBCommand:
    io_handler.GetDebugger().GetCommandInterpreter().HandleCompletion(request);
    break;
  case Completion::Expression:
    lldb_private::CommandCompletions::InvokeCommonCompletionCallbacks(
        io_handler.GetDebugger().GetCommandInterpreter(),
        lldb::eVariablePathCompletion, request, nullptr);
    break;
  }
}

IOHandlerEditline::IOHandlerEditline(
    Debugger &debugger, IOHandler::Type type,
    const char *editline_name, // Used for saving history files
    llvm::StringRef prompt, llvm::StringRef continuation_prompt,
    bool multi_line, bool color, uint32_t line_number_start,
    IOHandlerDelegate &delegate)
    : IOHandlerEditline(debugger, type,
                        FileSP(),       // Inherit input from top input reader
                        StreamFileSP(), // Inherit output from top input reader
                        StreamFileSP(), // Inherit error from top input reader
                        0,              // Flags
                        editline_name,  // Used for saving history files
                        prompt, continuation_prompt, multi_line, color,
                        line_number_start, delegate) {}

IOHandlerEditline::IOHandlerEditline(
    Debugger &debugger, IOHandler::Type type, const lldb::FileSP &input_sp,
    const lldb::StreamFileSP &output_sp, const lldb::StreamFileSP &error_sp,
    uint32_t flags,
    const char *editline_name, // Used for saving history files
    llvm::StringRef prompt, llvm::StringRef continuation_prompt,
    bool multi_line, bool color, uint32_t line_number_start,
    IOHandlerDelegate &delegate)
    : IOHandler(debugger, type, input_sp, output_sp, error_sp, flags),
#if LLDB_ENABLE_LIBEDIT
      m_editline_up(),
#endif
      m_delegate(delegate), m_prompt(), m_continuation_prompt(),
      m_current_lines_ptr(nullptr), m_base_line_number(line_number_start),
      m_curr_line_idx(UINT32_MAX), m_multi_line(multi_line), m_color(color),
      m_interrupt_exits(true) {
  SetPrompt(prompt);

#if LLDB_ENABLE_LIBEDIT
  bool use_editline = false;

  use_editline = GetInputFILE() && GetOutputFILE() && GetErrorFILE() &&
                 m_input_sp && m_input_sp->GetIsRealTerminal();

  if (use_editline) {
    m_editline_up = std::make_unique<Editline>(editline_name, GetInputFILE(),
                                               GetOutputFILE(), GetErrorFILE(),
                                               GetOutputMutex());
    m_editline_up->SetIsInputCompleteCallback(
        [this](Editline *editline, StringList &lines) {
          return this->IsInputCompleteCallback(editline, lines);
        });

    m_editline_up->SetAutoCompleteCallback([this](CompletionRequest &request) {
      this->AutoCompleteCallback(request);
    });

    if (debugger.GetUseAutosuggestion()) {
      m_editline_up->SetSuggestionCallback([this](llvm::StringRef line) {
        return this->SuggestionCallback(line);
      });
      if (m_color) {
        m_editline_up->SetSuggestionAnsiPrefix(ansi::FormatAnsiTerminalCodes(
            debugger.GetAutosuggestionAnsiPrefix()));
        m_editline_up->SetSuggestionAnsiSuffix(ansi::FormatAnsiTerminalCodes(
            debugger.GetAutosuggestionAnsiSuffix()));
      }
    }
    // See if the delegate supports fixing indentation
    const char *indent_chars = delegate.IOHandlerGetFixIndentationCharacters();
    if (indent_chars) {
      // The delegate does support indentation, hook it up so when any
      // indentation character is typed, the delegate gets a chance to fix it
      FixIndentationCallbackType f = [this](Editline *editline,
                                            const StringList &lines,
                                            int cursor_position) {
        return this->FixIndentationCallback(editline, lines, cursor_position);
      };
      m_editline_up->SetFixIndentationCallback(std::move(f), indent_chars);
    }
  }
#endif
  SetBaseLineNumber(m_base_line_number);
  SetPrompt(prompt);
  SetContinuationPrompt(continuation_prompt);
}

IOHandlerEditline::~IOHandlerEditline() {
#if LLDB_ENABLE_LIBEDIT
  m_editline_up.reset();
#endif
}

void IOHandlerEditline::Activate() {
  IOHandler::Activate();
  m_delegate.IOHandlerActivated(*this, GetIsInteractive());
}

void IOHandlerEditline::Deactivate() {
  IOHandler::Deactivate();
  m_delegate.IOHandlerDeactivated(*this);
}

void IOHandlerEditline::TerminalSizeChanged() {
#if LLDB_ENABLE_LIBEDIT
  if (m_editline_up)
    m_editline_up->TerminalSizeChanged();
#endif
}

// Split out a line from the buffer, if there is a full one to get.
static std::optional<std::string> SplitLine(std::string &line_buffer) {
  size_t pos = line_buffer.find('\n');
  if (pos == std::string::npos)
    return std::nullopt;
  std::string line =
      std::string(StringRef(line_buffer.c_str(), pos).rtrim("\n\r"));
  line_buffer = line_buffer.substr(pos + 1);
  return line;
}

// If the final line of the file ends without a end-of-line, return
// it as a line anyway.
static std::optional<std::string> SplitLineEOF(std::string &line_buffer) {
  if (llvm::all_of(line_buffer, llvm::isSpace))
    return std::nullopt;
  std::string line = std::move(line_buffer);
  line_buffer.clear();
  return line;
}

bool IOHandlerEditline::GetLine(std::string &line, bool &interrupted) {
#if LLDB_ENABLE_LIBEDIT
  if (m_editline_up) {
    return m_editline_up->GetLine(line, interrupted);
  }
#endif

  line.clear();

  if (GetIsInteractive()) {
    const char *prompt = nullptr;

    if (m_multi_line && m_curr_line_idx > 0)
      prompt = GetContinuationPrompt();

    if (prompt == nullptr)
      prompt = GetPrompt();

    if (prompt && prompt[0]) {
      if (m_output_sp) {
        m_output_sp->Printf("%s", prompt);
        m_output_sp->Flush();
      }
    }
  }

  std::optional<std::string> got_line = SplitLine(m_line_buffer);

  if (!got_line && !m_input_sp) {
    // No more input file, we are done...
    SetIsDone(true);
    return false;
  }

  FILE *in = GetInputFILE();
  char buffer[256];

  if (!got_line && !in && m_input_sp) {
    // there is no FILE*, fall back on just reading bytes from the stream.
    while (!got_line) {
      size_t bytes_read = sizeof(buffer);
      Status error = m_input_sp->Read((void *)buffer, bytes_read);
      if (error.Success() && !bytes_read) {
        got_line = SplitLineEOF(m_line_buffer);
        break;
      }
      if (error.Fail())
        break;
      m_line_buffer += StringRef(buffer, bytes_read);
      got_line = SplitLine(m_line_buffer);
    }
  }

  if (!got_line && in) {
    while (!got_line) {
      char *r = fgets(buffer, sizeof(buffer), in);
#ifdef _WIN32
      // ReadFile on Windows is supposed to set ERROR_OPERATION_ABORTED
      // according to the docs on MSDN. However, this has evidently been a
      // known bug since Windows 8. Therefore, we can't detect if a signal
      // interrupted in the fgets. So pressing ctrl-c causes the repl to end
      // and the process to exit. A temporary workaround is just to attempt to
      // fgets twice until this bug is fixed.
      if (r == nullptr)
        r = fgets(buffer, sizeof(buffer), in);
      // this is the equivalent of EINTR for Windows
      if (r == nullptr && GetLastError() == ERROR_OPERATION_ABORTED)
        continue;
#endif
      if (r == nullptr) {
        if (ferror(in) && errno == EINTR)
          continue;
        if (feof(in))
          got_line = SplitLineEOF(m_line_buffer);
        break;
      }
      m_line_buffer += buffer;
      got_line = SplitLine(m_line_buffer);
    }
  }

  if (got_line) {
    line = *got_line;
  }

  return (bool)got_line;
}

#if LLDB_ENABLE_LIBEDIT
bool IOHandlerEditline::IsInputCompleteCallback(Editline *editline,
                                                StringList &lines) {
  return m_delegate.IOHandlerIsInputComplete(*this, lines);
}

int IOHandlerEditline::FixIndentationCallback(Editline *editline,
                                              const StringList &lines,
                                              int cursor_position) {
  return m_delegate.IOHandlerFixIndentation(*this, lines, cursor_position);
}

std::optional<std::string>
IOHandlerEditline::SuggestionCallback(llvm::StringRef line) {
  return m_delegate.IOHandlerSuggestion(*this, line);
}

void IOHandlerEditline::AutoCompleteCallback(CompletionRequest &request) {
  m_delegate.IOHandlerComplete(*this, request);
}
#endif

const char *IOHandlerEditline::GetPrompt() {
#if LLDB_ENABLE_LIBEDIT
  if (m_editline_up) {
    return m_editline_up->GetPrompt();
  } else {
#endif
    if (m_prompt.empty())
      return nullptr;
#if LLDB_ENABLE_LIBEDIT
  }
#endif
  return m_prompt.c_str();
}

bool IOHandlerEditline::SetPrompt(llvm::StringRef prompt) {
  m_prompt = std::string(prompt);

#if LLDB_ENABLE_LIBEDIT
  if (m_editline_up) {
    m_editline_up->SetPrompt(m_prompt.empty() ? nullptr : m_prompt.c_str());
    if (m_color) {
      m_editline_up->SetPromptAnsiPrefix(
          ansi::FormatAnsiTerminalCodes(m_debugger.GetPromptAnsiPrefix()));
      m_editline_up->SetPromptAnsiSuffix(
          ansi::FormatAnsiTerminalCodes(m_debugger.GetPromptAnsiSuffix()));
    }
  }
#endif
  return true;
}

const char *IOHandlerEditline::GetContinuationPrompt() {
  return (m_continuation_prompt.empty() ? nullptr
                                        : m_continuation_prompt.c_str());
}

void IOHandlerEditline::SetContinuationPrompt(llvm::StringRef prompt) {
  m_continuation_prompt = std::string(prompt);

#if LLDB_ENABLE_LIBEDIT
  if (m_editline_up)
    m_editline_up->SetContinuationPrompt(m_continuation_prompt.empty()
                                             ? nullptr
                                             : m_continuation_prompt.c_str());
#endif
}

void IOHandlerEditline::SetBaseLineNumber(uint32_t line) {
  m_base_line_number = line;
}

uint32_t IOHandlerEditline::GetCurrentLineIndex() const {
#if LLDB_ENABLE_LIBEDIT
  if (m_editline_up)
    return m_editline_up->GetCurrentLine();
#endif
  return m_curr_line_idx;
}

StringList IOHandlerEditline::GetCurrentLines() const {
#if LLDB_ENABLE_LIBEDIT
  if (m_editline_up)
    return m_editline_up->GetInputAsStringList();
#endif
  // When libedit is not used, the current lines can be gotten from
  // `m_current_lines_ptr`, which is updated whenever a new line is processed.
  // This doesn't happen when libedit is used, in which case
  // `m_current_lines_ptr` is only updated when the full input is terminated.

  if (m_current_lines_ptr)
    return *m_current_lines_ptr;
  return StringList();
}

bool IOHandlerEditline::GetLines(StringList &lines, bool &interrupted) {
  m_current_lines_ptr = &lines;

  bool success = false;
#if LLDB_ENABLE_LIBEDIT
  if (m_editline_up) {
    return m_editline_up->GetLines(m_base_line_number, lines, interrupted);
  } else {
#endif
    bool done = false;
    Status error;

    while (!done) {
      // Show line numbers if we are asked to
      std::string line;
      if (m_base_line_number > 0 && GetIsInteractive()) {
        if (m_output_sp) {
          m_output_sp->Printf("%u%s",
                              m_base_line_number + (uint32_t)lines.GetSize(),
                              GetPrompt() == nullptr ? " " : "");
        }
      }

      m_curr_line_idx = lines.GetSize();

      bool interrupted = false;
      if (GetLine(line, interrupted) && !interrupted) {
        lines.AppendString(line);
        done = m_delegate.IOHandlerIsInputComplete(*this, lines);
      } else {
        done = true;
      }
    }
    success = lines.GetSize() > 0;
#if LLDB_ENABLE_LIBEDIT
  }
#endif
  return success;
}

// Each IOHandler gets to run until it is done. It should read data from the
// "in" and place output into "out" and "err and return when done.
void IOHandlerEditline::Run() {
  std::string line;
  while (IsActive()) {
    bool interrupted = false;
    if (m_multi_line) {
      StringList lines;
      if (GetLines(lines, interrupted)) {
        if (interrupted) {
          m_done = m_interrupt_exits;
          m_delegate.IOHandlerInputInterrupted(*this, line);

        } else {
          line = lines.CopyList();
          m_delegate.IOHandlerInputComplete(*this, line);
        }
      } else {
        m_done = true;
      }
    } else {
      if (GetLine(line, interrupted)) {
        if (interrupted)
          m_delegate.IOHandlerInputInterrupted(*this, line);
        else
          m_delegate.IOHandlerInputComplete(*this, line);
      } else {
        m_done = true;
      }
    }
  }
}

void IOHandlerEditline::Cancel() {
#if LLDB_ENABLE_LIBEDIT
  if (m_editline_up)
    m_editline_up->Cancel();
#endif
}

bool IOHandlerEditline::Interrupt() {
  // Let the delgate handle it first
  if (m_delegate.IOHandlerInterrupt(*this))
    return true;

#if LLDB_ENABLE_LIBEDIT
  if (m_editline_up)
    return m_editline_up->Interrupt();
#endif
  return false;
}

void IOHandlerEditline::GotEOF() {
#if LLDB_ENABLE_LIBEDIT
  if (m_editline_up)
    m_editline_up->Interrupt();
#endif
}

void IOHandlerEditline::PrintAsync(const char *s, size_t len, bool is_stdout) {
#if LLDB_ENABLE_LIBEDIT
  if (m_editline_up) {
    std::lock_guard<std::recursive_mutex> guard(m_output_mutex);
    lldb::StreamFileSP stream = is_stdout ? m_output_sp : m_error_sp;
    m_editline_up->PrintAsync(stream.get(), s, len);
  } else
#endif
  {
#ifdef _WIN32
    const char *prompt = GetPrompt();
    if (prompt) {
      // Back up over previous prompt using Windows API
      CONSOLE_SCREEN_BUFFER_INFO screen_buffer_info;
      HANDLE console_handle = GetStdHandle(STD_OUTPUT_HANDLE);
      GetConsoleScreenBufferInfo(console_handle, &screen_buffer_info);
      COORD coord = screen_buffer_info.dwCursorPosition;
      coord.X -= strlen(prompt);
      if (coord.X < 0)
        coord.X = 0;
      SetConsoleCursorPosition(console_handle, coord);
    }
#endif
    IOHandler::PrintAsync(s, len, is_stdout);
#ifdef _WIN32
    if (prompt)
      IOHandler::PrintAsync(prompt, strlen(prompt), is_stdout);
#endif
  }
}
