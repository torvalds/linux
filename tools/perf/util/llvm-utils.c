/*
 * Copyright (C) 2015, Wang Nan <wangnan0@huawei.com>
 * Copyright (C) 2015, Huawei Inc.
 */

#include <stdio.h>
#include "util.h"
#include "debug.h"
#include "llvm-utils.h"
#include "cache.h"

#define CLANG_BPF_CMD_DEFAULT_TEMPLATE				\
		"$CLANG_EXEC -D__KERNEL__ $CLANG_OPTIONS "	\
		"$KERNEL_INC_OPTIONS -Wno-unused-value "	\
		"-Wno-pointer-sign -working-directory "		\
		"$WORKING_DIR -c \"$CLANG_SOURCE\" -target bpf -O2 -o -"

struct llvm_param llvm_param = {
	.clang_path = "clang",
	.clang_bpf_cmd_template = CLANG_BPF_CMD_DEFAULT_TEMPLATE,
	.clang_opt = NULL,
	.kbuild_dir = NULL,
	.kbuild_opts = NULL,
};

int perf_llvm_config(const char *var, const char *value)
{
	if (prefixcmp(var, "llvm."))
		return 0;
	var += sizeof("llvm.") - 1;

	if (!strcmp(var, "clang-path"))
		llvm_param.clang_path = strdup(value);
	else if (!strcmp(var, "clang-bpf-cmd-template"))
		llvm_param.clang_bpf_cmd_template = strdup(value);
	else if (!strcmp(var, "clang-opt"))
		llvm_param.clang_opt = strdup(value);
	else if (!strcmp(var, "kbuild-dir"))
		llvm_param.kbuild_dir = strdup(value);
	else if (!strcmp(var, "kbuild-opts"))
		llvm_param.kbuild_opts = strdup(value);
	else
		return -1;
	return 0;
}
