//===-- ASTUtils.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ASTUtils.h"

lldb_private::ExternalASTSourceWrapper::~ExternalASTSourceWrapper() = default;

void lldb_private::ExternalASTSourceWrapper::PrintStats() {
  m_Source->PrintStats();
}

lldb_private::ASTConsumerForwarder::~ASTConsumerForwarder() = default;

void lldb_private::ASTConsumerForwarder::PrintStats() { m_c->PrintStats(); }

lldb_private::SemaSourceWithPriorities::~SemaSourceWithPriorities() = default;

void lldb_private::SemaSourceWithPriorities::PrintStats() {
  for (size_t i = 0; i < Sources.size(); ++i)
    Sources[i]->PrintStats();
}
