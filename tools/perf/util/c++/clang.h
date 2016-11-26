#ifndef PERF_UTIL_CLANG_H
#define PERF_UTIL_CLANG_H

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include <memory>
namespace perf {

using namespace llvm;

std::unique_ptr<Module>
getModuleFromSource(StringRef Name, StringRef Content);

std::unique_ptr<Module>
getModuleFromSource(StringRef Path);

}
#endif
