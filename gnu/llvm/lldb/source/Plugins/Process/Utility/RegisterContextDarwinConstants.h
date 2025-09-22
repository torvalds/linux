//===-- RegisterContextDarwinConstants.h ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTDARWINCONSTANTS_H
#define LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTDARWINCONSTANTS_H

namespace lldb_private {

/// Constants returned by various RegisterContextDarwin_*** functions.
#ifndef KERN_SUCCESS
#define KERN_SUCCESS 0
#endif

#ifndef KERN_INVALID_ARGUMENT
#define KERN_INVALID_ARGUMENT 4
#endif

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_REGISTERCONTEXTDARWINCONSTANTS_H
