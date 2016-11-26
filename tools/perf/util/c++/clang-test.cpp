#include "clang.h"
#include "clang-c.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"

class perf_clang_scope {
public:
	explicit perf_clang_scope() {perf_clang__init();}
	~perf_clang_scope() {perf_clang__cleanup();}
};

extern "C" {

int test__clang_to_IR(void)
{
	perf_clang_scope _scope;

	std::unique_ptr<llvm::Module> M =
		perf::getModuleFromSource({"-DRESULT=1"},
					  "perf-test.c",
					  "int myfunc(void) {return RESULT;}");

	if (!M)
		return -1;

	for (llvm::Function& F : *M)
		if (F.getName() == "myfunc")
			return 0;
	return -1;
}

}
