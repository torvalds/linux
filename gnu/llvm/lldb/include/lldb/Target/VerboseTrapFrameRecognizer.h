#ifndef LLDB_TARGET_VERBOSETRAPFRAMERECOGNIZER_H
#define LLDB_TARGET_VERBOSETRAPFRAMERECOGNIZER_H

#include "lldb/Target/StackFrameRecognizer.h"

namespace lldb_private {

void RegisterVerboseTrapFrameRecognizer(Process &process);

/// Holds the stack frame that caused the Verbose trap and the inlined stop
/// reason message.
class VerboseTrapRecognizedStackFrame : public RecognizedStackFrame {
public:
  VerboseTrapRecognizedStackFrame(lldb::StackFrameSP most_relevant_frame_sp,
                                  std::string stop_desc);

  lldb::StackFrameSP GetMostRelevantFrame() override;

private:
  lldb::StackFrameSP m_most_relevant_frame;
};

/// When a thread stops, it checks the current frame contains a
/// Verbose Trap diagnostic. If so, it returns a \a
/// VerboseTrapRecognizedStackFrame holding the diagnostic a stop reason
/// description with and the parent frame as the most relavant frame.
class VerboseTrapFrameRecognizer : public StackFrameRecognizer {
public:
  std::string GetName() override {
    return "Verbose Trap StackFrame Recognizer";
  }

  lldb::RecognizedStackFrameSP
  RecognizeFrame(lldb::StackFrameSP frame) override;
};

} // namespace lldb_private

#endif // LLDB_TARGET_VERBOSETRAPFRAMERECOGNIZER_H
