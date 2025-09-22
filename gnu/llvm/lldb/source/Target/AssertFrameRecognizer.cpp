#include "lldb/Target/AssertFrameRecognizer.h"
#include "lldb/Core/Module.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/SymbolLocation.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/StackFrameList.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/LLDBLog.h"

using namespace llvm;
using namespace lldb;
using namespace lldb_private;

namespace lldb_private {
/// Fetches the abort frame location depending on the current platform.
///
/// \param[in] os
///    The target's os type.
/// \param[in,out] location
///    The struct that will contain the abort module spec and symbol names.
/// \return
///    \b true, if the platform is supported
///    \b false, otherwise.
bool GetAbortLocation(llvm::Triple::OSType os, SymbolLocation &location) {
  switch (os) {
  case llvm::Triple::Darwin:
  case llvm::Triple::MacOSX:
    location.module_spec = FileSpec("libsystem_kernel.dylib");
    location.symbols.push_back(ConstString("__pthread_kill"));
    break;
  case llvm::Triple::Linux:
    location.module_spec = FileSpec("libc.so.6");
    location.symbols.push_back(ConstString("raise"));
    location.symbols.push_back(ConstString("__GI_raise"));
    location.symbols.push_back(ConstString("gsignal"));
    location.symbols.push_back(ConstString("pthread_kill"));
    location.symbols_are_regex = true;
    break;
  default:
    Log *log = GetLog(LLDBLog::Unwind);
    LLDB_LOG(log, "AssertFrameRecognizer::GetAbortLocation Unsupported OS");
    return false;
  }

  return true;
}

/// Fetches the assert frame location depending on the current platform.
///
/// \param[in] os
///    The target's os type.
/// \param[in,out] location
///    The struct that will contain the assert module spec and symbol names.
/// \return
///    \b true, if the platform is supported
///    \b false, otherwise.
bool GetAssertLocation(llvm::Triple::OSType os, SymbolLocation &location) {
  switch (os) {
  case llvm::Triple::Darwin:
  case llvm::Triple::MacOSX:
    location.module_spec = FileSpec("libsystem_c.dylib");
    location.symbols.push_back(ConstString("__assert_rtn"));
    break;
  case llvm::Triple::Linux:
    location.module_spec = FileSpec("libc.so.6");
    location.symbols.push_back(ConstString("__assert_fail"));
    location.symbols.push_back(ConstString("__GI___assert_fail"));
    break;
  default:
    Log *log = GetLog(LLDBLog::Unwind);
    LLDB_LOG(log, "AssertFrameRecognizer::GetAssertLocation Unsupported OS");
    return false;
  }

  return true;
}

void RegisterAssertFrameRecognizer(Process *process) {
  Target &target = process->GetTarget();
  llvm::Triple::OSType os = target.GetArchitecture().GetTriple().getOS();
  SymbolLocation location;

  if (!GetAbortLocation(os, location))
    return;

  if (!location.symbols_are_regex) {
    target.GetFrameRecognizerManager().AddRecognizer(
        std::make_shared<AssertFrameRecognizer>(),
        location.module_spec.GetFilename(), location.symbols,
        /*first_instruction_only*/ false);
    return;
  }
  std::string module_re = "^";
  for (char c : location.module_spec.GetFilename().GetStringRef()) {
    if (c == '.')
      module_re += '\\';
    module_re += c;
  }
  module_re += '$';
  std::string symbol_re = "^(";
  for (auto it = location.symbols.cbegin(); it != location.symbols.cend();
       ++it) {
    if (it != location.symbols.cbegin())
      symbol_re += '|';
    symbol_re += it->GetStringRef();
  }
  // Strip the trailing @VER symbol version.
  symbol_re += ")(@.*)?$";
  target.GetFrameRecognizerManager().AddRecognizer(
      std::make_shared<AssertFrameRecognizer>(),
      std::make_shared<RegularExpression>(std::move(module_re)),
      std::make_shared<RegularExpression>(std::move(symbol_re)),
      /*first_instruction_only*/ false);
}

} // namespace lldb_private

lldb::RecognizedStackFrameSP
AssertFrameRecognizer::RecognizeFrame(lldb::StackFrameSP frame_sp) {
  ThreadSP thread_sp = frame_sp->GetThread();
  ProcessSP process_sp = thread_sp->GetProcess();
  Target &target = process_sp->GetTarget();
  llvm::Triple::OSType os = target.GetArchitecture().GetTriple().getOS();
  SymbolLocation location;

  if (!GetAssertLocation(os, location))
    return RecognizedStackFrameSP();

  const uint32_t frames_to_fetch = 6;
  const uint32_t last_frame_index = frames_to_fetch - 1;
  StackFrameSP prev_frame_sp = nullptr;

  // Fetch most relevant frame
  for (uint32_t frame_index = 0; frame_index < frames_to_fetch; frame_index++) {
    prev_frame_sp = thread_sp->GetStackFrameAtIndex(frame_index);

    if (!prev_frame_sp) {
      Log *log = GetLog(LLDBLog::Unwind);
      LLDB_LOG(log, "Abort Recognizer: Hit unwinding bound ({1} frames)!",
               frames_to_fetch);
      break;
    }

    SymbolContext sym_ctx =
        prev_frame_sp->GetSymbolContext(eSymbolContextEverything);

    if (!sym_ctx.module_sp ||
        !sym_ctx.module_sp->GetFileSpec().FileEquals(location.module_spec))
      continue;

    ConstString func_name = sym_ctx.GetFunctionName();

    if (llvm::is_contained(location.symbols, func_name)) {
      // We go a frame beyond the assert location because the most relevant
      // frame for the user is the one in which the assert function was called.
      // If the assert location is the last frame fetched, then it is set as
      // the most relevant frame.

      StackFrameSP most_relevant_frame_sp = thread_sp->GetStackFrameAtIndex(
          std::min(frame_index + 1, last_frame_index));

      // Pass assert location to AbortRecognizedStackFrame to set as most
      // relevant frame.
      return lldb::RecognizedStackFrameSP(
          new AssertRecognizedStackFrame(most_relevant_frame_sp));
    }
  }

  return RecognizedStackFrameSP();
}

AssertRecognizedStackFrame::AssertRecognizedStackFrame(
    StackFrameSP most_relevant_frame_sp)
    : m_most_relevant_frame(most_relevant_frame_sp) {
  m_stop_desc = "hit program assert";
}

lldb::StackFrameSP AssertRecognizedStackFrame::GetMostRelevantFrame() {
  return m_most_relevant_frame;
}
