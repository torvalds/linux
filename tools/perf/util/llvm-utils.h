/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2015, Wang Nan <wangnan0@huawei.com>
 * Copyright (C) 2015, Huawei Inc.
 */
#ifndef __LLVM_UTILS_H
#define __LLVM_UTILS_H

#include "debug.h"

struct llvm_param {
	/* Path of clang executable */
	const char *clang_path;
	/*
	 * Template of clang bpf compiling. 5 env variables
	 * can be used:
	 *   $CLANG_EXEC:		Path to clang.
	 *   $CLANG_OPTIONS:		Extra options to clang.
	 *   $KERNEL_INC_OPTIONS:	Kernel include directories.
	 *   $WORKING_DIR:		Kernel source directory.
	 *   $CLANG_SOURCE:		Source file to be compiled.
	 */
	const char *clang_bpf_cmd_template;
	/* Will be filled in $CLANG_OPTIONS */
	const char *clang_opt;
	/* Where to find kbuild system */
	const char *kbuild_dir;
	/*
	 * Arguments passed to make, like 'ARCH=arm' if doing cross
	 * compiling. Should not be used for dynamic compiling.
	 */
	const char *kbuild_opts;
	/*
	 * Default is false. If set to true, write compiling result
	 * to object file.
	 */
	bool dump_obj;
	/*
	 * Default is false. If one of the above fields is set by user
	 * explicitly then user_set_llvm is set to true. This is used
	 * for perf test. If user doesn't set anything in .perfconfig
	 * and clang is not found, don't trigger llvm test.
	 */
	bool user_set_param;
};

extern struct llvm_param llvm_param;
int perf_llvm_config(const char *var, const char *value);

int llvm__compile_bpf(const char *path, void **p_obj_buf, size_t *p_obj_buf_sz);

/* This function is for test__llvm() use only */
int llvm__search_clang(void);

/* Following functions are reused by builtin clang support */
void llvm__get_kbuild_opts(char **kbuild_dir, char **kbuild_include_opts);
int llvm__get_nr_cpus(void);

void llvm__dump_obj(const char *path, void *obj_buf, size_t size);
#endif
