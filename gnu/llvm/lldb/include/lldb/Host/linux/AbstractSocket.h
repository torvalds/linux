//===-- AbstractSocket.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_AbstractSocket_h_
#define liblldb_AbstractSocket_h_

#include "lldb/Host/posix/DomainSocket.h"

namespace lldb_private {
class AbstractSocket : public DomainSocket {
public:
  AbstractSocket(bool child_processes_inherit);

protected:
  size_t GetNameOffset() const override;
  void DeleteSocketFile(llvm::StringRef name) override;
};
}

#endif // ifndef liblldb_AbstractSocket_h_
