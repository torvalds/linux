/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PERF_TEST_LLVM_H
#define PERF_TEST_LLVM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h> /* for size_t */
#include <stdbool.h> /* for bool */

extern const char test_llvm__bpf_base_prog[];
extern const char test_llvm__bpf_test_kbuild_prog[];
extern const char test_llvm__bpf_test_prologue_prog[];
extern const char test_llvm__bpf_test_relocation[];

enum test_llvm__testcase {
	LLVM_TESTCASE_BASE,
	LLVM_TESTCASE_KBUILD,
	LLVM_TESTCASE_BPF_PROLOGUE,
	LLVM_TESTCASE_BPF_RELOCATION,
	__LLVM_TESTCASE_MAX,
};

int test_llvm__fetch_bpf_obj(void **p_obj_buf, size_t *p_obj_buf_sz,
			     enum test_llvm__testcase index, bool force,
			     bool *should_load_fail);
#ifdef __cplusplus
}
#endif
#endif
