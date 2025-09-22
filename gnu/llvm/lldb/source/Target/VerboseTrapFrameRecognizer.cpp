#include "lldb/Target/VerboseTrapFrameRecognizer.h"

#include "lldb/Core/Module.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/StackFrameRecognizer.h"
#include "lldb/Target/Target.h"

#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"

#include "clang/CodeGen/ModuleBuilder.h"

using namespace llvm;
using namespace lldb;
using namespace lldb_private;

VerboseTrapRecognizedStackFrame::VerboseTrapRecognizedStackFrame(
    StackFrameSP most_relevant_frame_sp, std::string stop_desc)
    : m_most_relevant_frame(most_relevant_frame_sp) {
  m_stop_desc = std::move(stop_desc);
}

lldb::RecognizedStackFrameSP
VerboseTrapFrameRecognizer::RecognizeFrame(lldb::StackFrameSP frame_sp) {
  if (frame_sp->GetFrameIndex())
    return {};

  ThreadSP thread_sp = frame_sp->GetThread();
  ProcessSP process_sp = thread_sp->GetProcess();

  StackFrameSP most_relevant_frame_sp = thread_sp->GetStackFrameAtIndex(1);

  if (!most_relevant_frame_sp) {
    Log *log = GetLog(LLDBLog::Unwind);
    LLDB_LOG(
        log,
        "Failed to find most relevant frame: Hit unwinding bound (1 frame)!");
    return {};
  }

  SymbolContext sc = frame_sp->GetSymbolContext(eSymbolContextEverything);

  if (!sc.block)
    return {};

  // The runtime error is set as the function name in the inlined function info
  // of frame #0 by the compiler
  const InlineFunctionInfo *inline_info = nullptr;
  Block *inline_block = sc.block->GetContainingInlinedBlock();

  if (!inline_block)
    return {};

  inline_info = sc.block->GetInlinedFunctionInfo();

  if (!inline_info)
    return {};

  auto func_name = inline_info->GetName().GetStringRef();
  if (func_name.empty())
    return {};

  static auto trap_regex =
      llvm::Regex(llvm::formatv("^{0}\\$(.*)\\$(.*)$", ClangTrapPrefix).str());
  SmallVector<llvm::StringRef, 3> matches;
  std::string regex_err_msg;
  if (!trap_regex.match(func_name, &matches, &regex_err_msg)) {
    LLDB_LOGF(GetLog(LLDBLog::Unwind),
              "Failed to parse match trap regex for '%s': %s", func_name.data(),
              regex_err_msg.c_str());

    return {};
  }

  // For `__clang_trap_msg$category$message$` we expect 3 matches:
  // 1. entire string
  // 2. category
  // 3. message
  if (matches.size() != 3) {
    LLDB_LOGF(GetLog(LLDBLog::Unwind),
              "Unexpected function name format. Expected '<trap prefix>$<trap "
              "category>$<trap message>'$ but got: '%s'.",
              func_name.data());

    return {};
  }

  auto category = matches[1];
  auto message = matches[2];

  std::string stop_reason =
      category.empty() ? "<empty category>" : category.str();
  if (!message.empty()) {
    stop_reason += ": ";
    stop_reason += message.str();
  }

  return std::make_shared<VerboseTrapRecognizedStackFrame>(
      most_relevant_frame_sp, std::move(stop_reason));
}

lldb::StackFrameSP VerboseTrapRecognizedStackFrame::GetMostRelevantFrame() {
  return m_most_relevant_frame;
}

namespace lldb_private {

void RegisterVerboseTrapFrameRecognizer(Process &process) {
  RegularExpressionSP module_regex_sp = nullptr;
  auto symbol_regex_sp = std::make_shared<RegularExpression>(
      llvm::formatv("^{0}", ClangTrapPrefix).str());

  StackFrameRecognizerSP srf_recognizer_sp =
      std::make_shared<VerboseTrapFrameRecognizer>();

  process.GetTarget().GetFrameRecognizerManager().AddRecognizer(
      srf_recognizer_sp, module_regex_sp, symbol_regex_sp, false);
}

} // namespace lldb_private
