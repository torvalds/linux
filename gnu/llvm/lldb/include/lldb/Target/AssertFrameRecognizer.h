//===-- AssertFrameRecognizer.cpp -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_ASSERTFRAMERECOGNIZER_H
#define LLDB_TARGET_ASSERTFRAMERECOGNIZER_H

#include "lldb/Target/Process.h"
#include "lldb/Target/StackFrameRecognizer.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/FileSpec.h"

#include <tuple>

namespace lldb_private {

/// Registers the assert stack frame recognizer.
///
/// \param[in] process
///    The process that is currently asserting. This will give us information on
///    the target and the platform.
void RegisterAssertFrameRecognizer(Process *process);

/// \class AssertRecognizedStackFrame
///
/// Holds the stack frame where the assert is called from.
class AssertRecognizedStackFrame : public RecognizedStackFrame {
public:
  AssertRecognizedStackFrame(lldb::StackFrameSP most_relevant_frame_sp);
  lldb::StackFrameSP GetMostRelevantFrame() override;

private:
  lldb::StackFrameSP m_most_relevant_frame;
};

/// \class AssertFrameRecognizer
///
/// When a thread stops, it checks depending on the platform if the top frame is
/// an abort stack frame. If so, it looks for an assert stack frame in the upper
/// frames and set it as the most relavant frame when found.
class AssertFrameRecognizer : public StackFrameRecognizer {
public:
  std::string GetName() override { return "Assert StackFrame Recognizer"; }
  lldb::RecognizedStackFrameSP
  RecognizeFrame(lldb::StackFrameSP frame_sp) override;
};

} // namespace lldb_private

#endif // LLDB_TARGET_ASSERTFRAMERECOGNIZER_H
