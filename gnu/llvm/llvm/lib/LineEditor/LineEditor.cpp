//===-- LineEditor.cpp - line editor --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/LineEditor/LineEditor.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Config/config.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#ifdef HAVE_LIBEDIT
#include <histedit.h>
#endif

using namespace llvm;

std::string LineEditor::getDefaultHistoryPath(StringRef ProgName) {
  SmallString<32> Path;
  if (sys::path::home_directory(Path)) {
    sys::path::append(Path, "." + ProgName + "-history");
    return std::string(Path);
  }
  return std::string();
}

LineEditor::CompleterConcept::~CompleterConcept() = default;
LineEditor::ListCompleterConcept::~ListCompleterConcept() = default;

std::string LineEditor::ListCompleterConcept::getCommonPrefix(
    const std::vector<Completion> &Comps) {
  assert(!Comps.empty());

  std::string CommonPrefix = Comps[0].TypedText;
  for (const Completion &C : llvm::drop_begin(Comps)) {
    size_t Len = std::min(CommonPrefix.size(), C.TypedText.size());
    size_t CommonLen = 0;
    for (; CommonLen != Len; ++CommonLen) {
      if (CommonPrefix[CommonLen] != C.TypedText[CommonLen])
        break;
    }
    CommonPrefix.resize(CommonLen);
  }
  return CommonPrefix;
}

LineEditor::CompletionAction
LineEditor::ListCompleterConcept::complete(StringRef Buffer, size_t Pos) const {
  CompletionAction Action;
  std::vector<Completion> Comps = getCompletions(Buffer, Pos);
  if (Comps.empty()) {
    Action.Kind = CompletionAction::AK_ShowCompletions;
    return Action;
  }

  std::string CommonPrefix = getCommonPrefix(Comps);

  // If the common prefix is non-empty we can simply insert it. If there is a
  // single completion, this will insert the full completion. If there is more
  // than one, this might be enough information to jog the user's memory but if
  // not the user can also hit tab again to see the completions because the
  // common prefix will then be empty.
  if (CommonPrefix.empty()) {
    Action.Kind = CompletionAction::AK_ShowCompletions;
    for (const Completion &Comp : Comps)
      Action.Completions.push_back(Comp.DisplayText);
  } else {
    Action.Kind = CompletionAction::AK_Insert;
    Action.Text = CommonPrefix;
  }

  return Action;
}

LineEditor::CompletionAction LineEditor::getCompletionAction(StringRef Buffer,
                                                             size_t Pos) const {
  if (!Completer) {
    CompletionAction Action;
    Action.Kind = CompletionAction::AK_ShowCompletions;
    return Action;
  }

  return Completer->complete(Buffer, Pos);
}

#ifdef HAVE_LIBEDIT

// libedit-based implementation.

struct LineEditor::InternalData {
  LineEditor *LE;

  History *Hist;
  EditLine *EL;

  unsigned PrevCount;
  std::string ContinuationOutput;

  FILE *Out;
};

namespace {

const char *ElGetPromptFn(EditLine *EL) {
  LineEditor::InternalData *Data;
  if (el_get(EL, EL_CLIENTDATA, &Data) == 0)
    return Data->LE->getPrompt().c_str();
  return "> ";
}

// Handles tab completion.
//
// This function is really horrible. But since the alternative is to get into
// the line editor business, here we are.
unsigned char ElCompletionFn(EditLine *EL, int ch) {
  LineEditor::InternalData *Data;
  if (el_get(EL, EL_CLIENTDATA, &Data) == 0) {
    if (!Data->ContinuationOutput.empty()) {
      // This is the continuation of the AK_ShowCompletions branch below.
      FILE *Out = Data->Out;

      // Print the required output (see below).
      ::fwrite(Data->ContinuationOutput.c_str(),
               Data->ContinuationOutput.size(), 1, Out);

      // Push a sequence of Ctrl-B characters to move the cursor back to its
      // original position.
      std::string Prevs(Data->PrevCount, '\02');
      ::el_push(EL, const_cast<char *>(Prevs.c_str()));

      Data->ContinuationOutput.clear();

      return CC_REFRESH;
    }

    const LineInfo *LI = ::el_line(EL);
    LineEditor::CompletionAction Action = Data->LE->getCompletionAction(
        StringRef(LI->buffer, LI->lastchar - LI->buffer),
        LI->cursor - LI->buffer);
    switch (Action.Kind) {
    case LineEditor::CompletionAction::AK_Insert:
      ::el_insertstr(EL, Action.Text.c_str());
      return CC_REFRESH;

    case LineEditor::CompletionAction::AK_ShowCompletions:
      if (Action.Completions.empty()) {
        return CC_REFRESH_BEEP;
      } else {
        // Push a Ctrl-E and a tab. The Ctrl-E causes libedit to move the cursor
        // to the end of the line, so that when we emit a newline we will be on
        // a new blank line. The tab causes libedit to call this function again
        // after moving the cursor. There doesn't seem to be anything we can do
        // from here to cause libedit to move the cursor immediately. This will
        // break horribly if the user has rebound their keys, so for now we do
        // not permit user rebinding.
        ::el_push(EL, const_cast<char *>("\05\t"));

        // This assembles the output for the continuation block above.
        raw_string_ostream OS(Data->ContinuationOutput);

        // Move cursor to a blank line.
        OS << "\n";

        // Emit the completions.
        for (const std::string &Completion : Action.Completions)
          OS << Completion << "\n";

        // Fool libedit into thinking nothing has changed. Reprint its prompt
        // and the user input. Note that the cursor will remain at the end of
        // the line after this.
        OS << Data->LE->getPrompt()
           << StringRef(LI->buffer, LI->lastchar - LI->buffer);

        // This is the number of characters we need to tell libedit to go back:
        // the distance between end of line and the original cursor position.
        Data->PrevCount = LI->lastchar - LI->cursor;

        return CC_REFRESH;
      }
    }
  }
  return CC_ERROR;
}

} // end anonymous namespace

LineEditor::LineEditor(StringRef ProgName, StringRef HistoryPath, FILE *In,
                       FILE *Out, FILE *Err)
    : Prompt((ProgName + "> ").str()), HistoryPath(std::string(HistoryPath)),
      Data(new InternalData) {
  if (HistoryPath.empty())
    this->HistoryPath = getDefaultHistoryPath(ProgName);

  Data->LE = this;
  Data->Out = Out;

  Data->Hist = ::history_init();
  assert(Data->Hist);

  Data->EL = ::el_init(ProgName.str().c_str(), In, Out, Err);
  assert(Data->EL);

  ::el_set(Data->EL, EL_PROMPT, ElGetPromptFn);
  ::el_set(Data->EL, EL_EDITOR, "emacs");
  ::el_set(Data->EL, EL_HIST, history, Data->Hist);
  ::el_set(Data->EL, EL_ADDFN, "tab_complete", "Tab completion function",
           ElCompletionFn);
  ::el_set(Data->EL, EL_BIND, "\t", "tab_complete", NULL);
  ::el_set(Data->EL, EL_BIND, "^r", "em-inc-search-prev",
           NULL); // Cycle through backwards search, entering string
  ::el_set(Data->EL, EL_BIND, "^w", "ed-delete-prev-word",
           NULL); // Delete previous word, behave like bash does.
  ::el_set(Data->EL, EL_BIND, "\033[3~", "ed-delete-next-char",
           NULL); // Fix the delete key.
  ::el_set(Data->EL, EL_CLIENTDATA, Data.get());

  HistEvent HE;
  ::history(Data->Hist, &HE, H_SETSIZE, 800);
  ::history(Data->Hist, &HE, H_SETUNIQUE, 1);
  loadHistory();
}

LineEditor::~LineEditor() {
  saveHistory();

  ::history_end(Data->Hist);
  ::el_end(Data->EL);
  ::fwrite("\n", 1, 1, Data->Out);
}

void LineEditor::saveHistory() {
  if (!HistoryPath.empty()) {
    HistEvent HE;
    ::history(Data->Hist, &HE, H_SAVE, HistoryPath.c_str());
  }
}

void LineEditor::loadHistory() {
  if (!HistoryPath.empty()) {
    HistEvent HE;
    ::history(Data->Hist, &HE, H_LOAD, HistoryPath.c_str());
  }
}

std::optional<std::string> LineEditor::readLine() const {
  // Call el_gets to prompt the user and read the user's input.
  int LineLen = 0;
  const char *Line = ::el_gets(Data->EL, &LineLen);

  // Either of these may mean end-of-file.
  if (!Line || LineLen == 0)
    return std::nullopt;

  // Strip any newlines off the end of the string.
  while (LineLen > 0 &&
         (Line[LineLen - 1] == '\n' || Line[LineLen - 1] == '\r'))
    --LineLen;

  HistEvent HE;
  if (LineLen > 0)
    ::history(Data->Hist, &HE, H_ENTER, Line);

  return std::string(Line, LineLen);
}

#else // HAVE_LIBEDIT

// Simple fgets-based implementation.

struct LineEditor::InternalData {
  FILE *In;
  FILE *Out;
};

LineEditor::LineEditor(StringRef ProgName, StringRef HistoryPath, FILE *In,
                       FILE *Out, FILE *Err)
    : Prompt((ProgName + "> ").str()), Data(new InternalData) {
  Data->In = In;
  Data->Out = Out;
}

LineEditor::~LineEditor() {
  ::fwrite("\n", 1, 1, Data->Out);
}

void LineEditor::saveHistory() {}
void LineEditor::loadHistory() {}

std::optional<std::string> LineEditor::readLine() const {
  ::fprintf(Data->Out, "%s", Prompt.c_str());

  std::string Line;
  do {
    char Buf[64];
    char *Res = ::fgets(Buf, sizeof(Buf), Data->In);
    if (!Res) {
      if (Line.empty())
        return std::nullopt;
      else
        return Line;
    }
    Line.append(Buf);
  } while (Line.empty() ||
           (Line[Line.size() - 1] != '\n' && Line[Line.size() - 1] != '\r'));

  while (!Line.empty() &&
         (Line[Line.size() - 1] == '\n' || Line[Line.size() - 1] == '\r'))
    Line.resize(Line.size() - 1);

  return Line;
}

#endif // HAVE_LIBEDIT
