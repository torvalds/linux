/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PERF_UTIL_CLANG_H
#define PERF_UTIL_CLANG_H

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Option/Option.h"
#include <memory>

namespace perf {

using namespace llvm;

std::unique_ptr<Module>
getModuleFromSource(opt::ArgStringList CFlags,
		    StringRef Name, StringRef Content);

std::unique_ptr<Module>
getModuleFromSource(opt::ArgStringList CFlags,
		    StringRef Path);

std::unique_ptr<llvm::SmallVectorImpl<char>>
getBPFObjectFromModule(llvm::Module *Module);

}
#endif
