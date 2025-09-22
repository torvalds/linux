//===-- sanitizer_stacktrace_libcdep.cpp ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is shared between AddressSanitizer and ThreadSanitizer
// run-time libraries.
//===----------------------------------------------------------------------===//

#include "sanitizer_common.h"
#include "sanitizer_placement_new.h"
#include "sanitizer_stacktrace.h"
#include "sanitizer_stacktrace_printer.h"
#include "sanitizer_symbolizer.h"

namespace __sanitizer {

namespace {

class StackTraceTextPrinter {
 public:
  StackTraceTextPrinter(const char *stack_trace_fmt, char frame_delimiter,
                        InternalScopedString *output,
                        InternalScopedString *dedup_token)
      : stack_trace_fmt_(stack_trace_fmt),
        frame_delimiter_(frame_delimiter),
        output_(output),
        dedup_token_(dedup_token),
        symbolize_(StackTracePrinter::GetOrInit()->RenderNeedsSymbolization(
            stack_trace_fmt)) {}

  bool ProcessAddressFrames(uptr pc) {
    SymbolizedStackHolder symbolized_stack(
        symbolize_ ? Symbolizer::GetOrInit()->SymbolizePC(pc)
                   : SymbolizedStack::New(pc));
    const SymbolizedStack *frames = symbolized_stack.get();
    if (!frames)
      return false;

    for (const SymbolizedStack *cur = frames; cur; cur = cur->next) {
      uptr prev_len = output_->length();
      StackTracePrinter::GetOrInit()->RenderFrame(
          output_, stack_trace_fmt_, frame_num_++, cur->info.address,
          symbolize_ ? &cur->info : nullptr, common_flags()->symbolize_vs_style,
          common_flags()->strip_path_prefix);

      if (prev_len != output_->length())
        output_->AppendF("%c", frame_delimiter_);

      ExtendDedupToken(cur);
    }
    return true;
  }

 private:
  // Extend the dedup token by appending a new frame.
  void ExtendDedupToken(const SymbolizedStack *stack) {
    if (!dedup_token_)
      return;

    if (dedup_frames_-- > 0) {
      if (dedup_token_->length())
        dedup_token_->Append("--");
      if (stack->info.function)
        dedup_token_->Append(stack->info.function);
    }
  }

  const char *stack_trace_fmt_;
  const char frame_delimiter_;
  int dedup_frames_ = common_flags()->dedup_token_length;
  uptr frame_num_ = 0;
  InternalScopedString *output_;
  InternalScopedString *dedup_token_;
  const bool symbolize_ = false;
};

static void CopyStringToBuffer(const InternalScopedString &str, char *out_buf,
                               uptr out_buf_size) {
  if (!out_buf_size)
    return;

  CHECK_GT(out_buf_size, 0);
  uptr copy_size = Min(str.length(), out_buf_size - 1);
  internal_memcpy(out_buf, str.data(), copy_size);
  out_buf[copy_size] = '\0';
}

}  // namespace

void StackTrace::PrintTo(InternalScopedString *output) const {
  CHECK(output);

  InternalScopedString dedup_token;
  StackTraceTextPrinter printer(common_flags()->stack_trace_format, '\n',
                                output, &dedup_token);

  if (trace == nullptr || size == 0) {
    output->Append("    <empty stack>\n\n");
    return;
  }

  for (uptr i = 0; i < size && trace[i]; i++) {
    // PCs in stack traces are actually the return addresses, that is,
    // addresses of the next instructions after the call.
    uptr pc = GetPreviousInstructionPc(trace[i]);
    CHECK(printer.ProcessAddressFrames(pc));
  }

  // Always add a trailing empty line after stack trace.
  output->Append("\n");

  // Append deduplication token, if non-empty.
  if (dedup_token.length())
    output->AppendF("DEDUP_TOKEN: %s\n", dedup_token.data());
}

uptr StackTrace::PrintTo(char *out_buf, uptr out_buf_size) const {
  CHECK(out_buf);

  InternalScopedString output;
  PrintTo(&output);
  CopyStringToBuffer(output, out_buf, out_buf_size);

  return output.length();
}

void StackTrace::Print() const {
  InternalScopedString output;
  PrintTo(&output);
  Printf("%s", output.data());
}

void BufferedStackTrace::Unwind(u32 max_depth, uptr pc, uptr bp, void *context,
                                uptr stack_top, uptr stack_bottom,
                                bool request_fast_unwind) {
  // Ensures all call sites get what they requested.
  CHECK_EQ(request_fast_unwind, WillUseFastUnwind(request_fast_unwind));
  top_frame_bp = (max_depth > 0) ? bp : 0;
  // Avoid doing any work for small max_depth.
  if (max_depth == 0) {
    size = 0;
    return;
  }
  if (max_depth == 1) {
    size = 1;
    trace_buffer[0] = pc;
    return;
  }
  if (!WillUseFastUnwind(request_fast_unwind)) {
#if SANITIZER_CAN_SLOW_UNWIND
    if (context)
      UnwindSlow(pc, context, max_depth);
    else
      UnwindSlow(pc, max_depth);
    // If there are too few frames, the program may be built with
    // -fno-asynchronous-unwind-tables. Fall back to fast unwinder below.
    if (size > 2 || size >= max_depth)
      return;
#else
    UNREACHABLE("slow unwind requested but not available");
#endif
  }
  UnwindFast(pc, bp, stack_top, stack_bottom, max_depth);
}

int GetModuleAndOffsetForPc(uptr pc, char *module_name, uptr module_name_len,
                            uptr *pc_offset) {
  const char *found_module_name = nullptr;
  bool ok = Symbolizer::GetOrInit()->GetModuleNameAndOffsetForPC(
      pc, &found_module_name, pc_offset);

  if (!ok) return false;

  if (module_name && module_name_len) {
    internal_strncpy(module_name, found_module_name, module_name_len);
    module_name[module_name_len - 1] = '\x00';
  }
  return true;
}

}  // namespace __sanitizer
using namespace __sanitizer;

extern "C" {
SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_symbolize_pc(uptr pc, const char *fmt, char *out_buf,
                              uptr out_buf_size) {
  if (!out_buf_size)
    return;

  pc = StackTrace::GetPreviousInstructionPc(pc);

  InternalScopedString output;
  StackTraceTextPrinter printer(fmt, '\0', &output, nullptr);
  if (!printer.ProcessAddressFrames(pc)) {
    output.clear();
    output.Append("<can't symbolize>");
  }
  CopyStringToBuffer(output, out_buf, out_buf_size);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_symbolize_global(uptr data_addr, const char *fmt,
                                  char *out_buf, uptr out_buf_size) {
  if (!out_buf_size) return;
  out_buf[0] = 0;
  DataInfo DI;
  if (!Symbolizer::GetOrInit()->SymbolizeData(data_addr, &DI)) return;
  InternalScopedString data_desc;
  StackTracePrinter::GetOrInit()->RenderData(&data_desc, fmt, &DI,
                                             common_flags()->strip_path_prefix);
  internal_strncpy(out_buf, data_desc.data(), out_buf_size);
  out_buf[out_buf_size - 1] = 0;
}

SANITIZER_INTERFACE_ATTRIBUTE
int __sanitizer_get_module_and_offset_for_pc(void *pc, char *module_name,
                                             uptr module_name_len,
                                             void **pc_offset) {
  return __sanitizer::GetModuleAndOffsetForPc(
      reinterpret_cast<uptr>(pc), module_name, module_name_len,
      reinterpret_cast<uptr *>(pc_offset));
}
}  // extern "C"
