// SPDX-License-Identifier: GPL-2.0
#include "clang.h"
#include "clang-c.h"
extern "C" {
#include "../util.h"
}
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"

#include <tests/llvm.h>
#include <string>

class perf_clang_scope {
public:
	explicit perf_clang_scope() {perf_clang__init();}
	~perf_clang_scope() {perf_clang__cleanup();}
};

static std::unique_ptr<llvm::Module>
__test__clang_to_IR(void)
{
	unsigned int kernel_version;

	if (fetch_kernel_version(&kernel_version, NULL, 0))
		return std::unique_ptr<llvm::Module>(nullptr);

	std::string cflag_kver("-DLINUX_VERSION_CODE=" +
				std::to_string(kernel_version));

	std::unique_ptr<llvm::Module> M =
		perf::getModuleFromSource({cflag_kver.c_str()},
					  "perf-test.c",
					  test_llvm__bpf_base_prog);
	return M;
}

extern "C" {
int test__clang_to_IR(void)
{
	perf_clang_scope _scope;

	auto M = __test__clang_to_IR();
	if (!M)
		return -1;
	for (llvm::Function& F : *M)
		if (F.getName() == "bpf_func__SyS_epoll_pwait")
			return 0;
	return -1;
}

int test__clang_to_obj(void)
{
	perf_clang_scope _scope;

	auto M = __test__clang_to_IR();
	if (!M)
		return -1;

	auto Buffer = perf::getBPFObjectFromModule(&*M);
	if (!Buffer)
		return -1;
	return 0;
}

}
