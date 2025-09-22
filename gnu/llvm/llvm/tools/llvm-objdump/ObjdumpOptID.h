#ifndef LLVM_TOOLS_LLVM_OBJDUMP_OBJDUMP_OPT_ID_H
#define LLVM_TOOLS_LLVM_OBJDUMP_OBJDUMP_OPT_ID_H

#include "llvm/Option/OptTable.h"

enum ObjdumpOptID {
  OBJDUMP_INVALID = 0, // This is not an option ID.
#define OPTION(...) LLVM_MAKE_OPT_ID_WITH_ID_PREFIX(OBJDUMP_, __VA_ARGS__),
#include "ObjdumpOpts.inc"
#undef OPTION
};

#endif // LLVM_TOOLS_LLVM_OBJDUMP_OBJDUMP_OPT_ID_H
