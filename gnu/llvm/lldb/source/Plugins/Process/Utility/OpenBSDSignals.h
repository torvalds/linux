//===-- OpenBSDSignals.h ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_OpenBSDSignals_H_
#define liblldb_OpenBSDSignals_H_

#include "lldb/Target/UnixSignals.h"

namespace lldb_private {

class OpenBSDSignals : public UnixSignals {
public:
  OpenBSDSignals();

private:
  void Reset() override;
};

} // namespace lldb_private

#endif // liblldb_OpenBSDSignals_H_
