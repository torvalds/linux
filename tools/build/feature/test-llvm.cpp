#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/raw_ostream.h"
int main()
{
	llvm::errs() << "Hello World!\n";
	llvm::llvm_shutdown();
	return 0;
}
