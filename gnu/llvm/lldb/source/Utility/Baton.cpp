//===-- Baton.cpp ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/Baton.h"

void lldb_private::UntypedBaton::GetDescription(llvm::raw_ostream &s,
                                                lldb::DescriptionLevel level,
                                                unsigned indentation) const {}
