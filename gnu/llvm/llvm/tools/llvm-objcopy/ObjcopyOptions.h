//===- ObjcopyOptions.h ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_OBJCOPY_OBJCOPYOPTIONS_H
#define LLVM_TOOLS_LLVM_OBJCOPY_OBJCOPYOPTIONS_H

#include "llvm/ObjCopy/ConfigManager.h"
#include "llvm/Support/Allocator.h"

namespace llvm {
namespace objcopy {

// Configuration for the overall invocation of this tool. When invoked as
// objcopy, will always contain exactly one CopyConfig. When invoked as strip,
// will contain one or more CopyConfigs.
struct DriverConfig {
  SmallVector<ConfigManager, 1> CopyConfigs;
  BumpPtrAllocator Alloc;
};

// ParseObjcopyOptions returns the config and sets the input arguments. If a
// help flag is set then ParseObjcopyOptions will print the help messege and
// exit. ErrorCallback is used to handle recoverable errors. An Error returned
// by the callback aborts the parsing and is then returned by this function.
Expected<DriverConfig>
parseObjcopyOptions(ArrayRef<const char *> ArgsArr,
                    llvm::function_ref<Error(Error)> ErrorCallback);

// ParseInstallNameToolOptions returns the config and sets the input arguments.
// If a help flag is set then ParseInstallNameToolOptions will print the help
// messege and exit.
Expected<DriverConfig>
parseInstallNameToolOptions(ArrayRef<const char *> ArgsArr);

// ParseBitcodeStripOptions returns the config and sets the input arguments.
// If a help flag is set then ParseBitcodeStripOptions will print the help
// messege and exit.
Expected<DriverConfig>
parseBitcodeStripOptions(ArrayRef<const char *> ArgsArr,
                         llvm::function_ref<Error(Error)> ErrorCallback);

// ParseStripOptions returns the config and sets the input arguments. If a
// help flag is set then ParseStripOptions will print the help messege and
// exit. ErrorCallback is used to handle recoverable errors. An Error returned
// by the callback aborts the parsing and is then returned by this function.
Expected<DriverConfig>
parseStripOptions(ArrayRef<const char *> ArgsArr,
                  llvm::function_ref<Error(Error)> ErrorCallback);
} // namespace objcopy
} // namespace llvm

#endif // LLVM_TOOLS_LLVM_OBJCOPY_OBJCOPYOPTIONS_H
