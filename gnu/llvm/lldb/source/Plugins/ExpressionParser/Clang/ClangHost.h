//===-- ClangHost.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGHOST_H
#define LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGHOST_H

namespace lldb_private {

class FileSpec;

bool ComputeClangResourceDirectory(FileSpec &lldb_shlib_spec,
                                   FileSpec &file_spec, bool verify);

FileSpec GetClangResourceDir();

} // namespace lldb_private

#endif
