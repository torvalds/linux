// SPDX-License-Identifier: GPL-2.0
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/raw_ostream.h"

#if LLVM_VERSION_MAJOR < 13
# error "Perf requires llvm-devel/llvm-dev version 13 or greater"
#endif

int main()
{
	llvm::errs() << "Hello World!\n";
	llvm::llvm_shutdown();
	return 0;
}
