//===-- IOHandlerCursesGUI.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_IOHANDLERCURSESGUI_H
#define LLDB_CORE_IOHANDLERCURSESGUI_H

#include "lldb/Core/IOHandler.h"

namespace lldb_private {

class IOHandlerCursesGUI : public IOHandler {
public:
  IOHandlerCursesGUI(Debugger &debugger);

  ~IOHandlerCursesGUI() override;

  void Run() override;

  void Cancel() override;

  bool Interrupt() override;

  void GotEOF() override;

  void Activate() override;

  void Deactivate() override;

  void TerminalSizeChanged() override;

protected:
  curses::ApplicationAP m_app_ap;
};

} // namespace lldb_private

#endif // LLDB_CORE_IOHANDLERCURSESGUI_H
