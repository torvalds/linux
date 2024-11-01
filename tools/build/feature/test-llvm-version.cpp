// SPDX-License-Identifier: GPL-2.0
#include <cstdio>
#include "llvm/Config/llvm-config.h"

#define NUM_VERSION (((LLVM_VERSION_MAJOR) << 16) + (LLVM_VERSION_MINOR << 8) + LLVM_VERSION_PATCH)
#define pass int main() {printf("%x\n", NUM_VERSION); return 0;}

#if NUM_VERSION >= 0x030900
pass
#else
# error This LLVM is not tested yet.
#endif
