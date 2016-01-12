#ifndef PERF_TEST_LLVM_H
#define PERF_TEST_LLVM_H

#include <stddef.h> /* for size_t */
#include <stdbool.h> /* for bool */

extern const char test_llvm__bpf_base_prog[];
extern const char test_llvm__bpf_test_kbuild_prog[];

enum test_llvm__testcase {
	LLVM_TESTCASE_BASE,
	LLVM_TESTCASE_KBUILD,
	__LLVM_TESTCASE_MAX,
};

int test_llvm__fetch_bpf_obj(void **p_obj_buf, size_t *p_obj_buf_sz,
			     enum test_llvm__testcase index, bool force);
#endif
