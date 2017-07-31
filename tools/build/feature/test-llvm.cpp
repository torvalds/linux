#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/raw_ostream.h"
#define NUM_VERSION (((LLVM_VERSION_MAJOR) << 16) + (LLVM_VERSION_MINOR << 8) + LLVM_VERSION_PATCH)

#if NUM_VERSION < 0x030900
# error "LLVM version too low"
#endif
int main()
{
	llvm::errs() << "Hello World!\n";
	llvm::llvm_shutdown();
	return 0;
}
