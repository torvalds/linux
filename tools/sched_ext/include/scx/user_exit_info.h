/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Define struct user_exit_info which is shared between BPF and userspace parts
 * to communicate exit status and other information.
 *
 * Copyright (c) 2022 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2022 Tejun Heo <tj@kernel.org>
 * Copyright (c) 2022 David Vernet <dvernet@meta.com>
 */
#ifndef __USER_EXIT_INFO_H
#define __USER_EXIT_INFO_H

#include <stdio.h>
#include <stdbool.h>

#include "user_exit_info_common.h"

/* no need to call the following explicitly if SCX_OPS_LOAD() is used */
#define UEI_SET_SIZE(__skel, __ops_name, __uei_name) ({					\
	u32 __len = (__skel)->struct_ops.__ops_name->exit_dump_len ?: UEI_DUMP_DFL_LEN;	\
	(__skel)->rodata->__uei_name##_dump_len = __len;				\
	RESIZE_ARRAY((__skel), data, __uei_name##_dump, __len);				\
})

#define UEI_EXITED(__skel, __uei_name) ({					\
	/* use __sync to force memory barrier */				\
	__sync_val_compare_and_swap(&(__skel)->data->__uei_name.kind, -1, -1);	\
})

#define UEI_REPORT(__skel, __uei_name) ({					\
	struct user_exit_info *__uei = &(__skel)->data->__uei_name;		\
	char *__uei_dump = (__skel)->data_##__uei_name##_dump->__uei_name##_dump; \
	if (__uei_dump[0] != '\0') {						\
		fputs("\nDEBUG DUMP\n", stderr);				\
		fputs("================================================================================\n\n", stderr); \
		fputs(__uei_dump, stderr);					\
		fputs("\n================================================================================\n\n", stderr); \
	}									\
	fprintf(stderr, "EXIT: %s", __uei->reason);				\
	if (__uei->msg[0] != '\0')						\
		fprintf(stderr, " (%s)", __uei->msg);				\
	fputs("\n", stderr);							\
	__uei->exit_code;							\
})

/*
 * We can't import vmlinux.h while compiling user C code. Let's duplicate
 * scx_exit_code definition.
 */
enum scx_exit_code {
	/* Reasons */
	SCX_ECODE_RSN_HOTPLUG		= 1LLU << 32,

	/* Actions */
	SCX_ECODE_ACT_RESTART		= 1LLU << 48,
};

enum uei_ecode_mask {
	UEI_ECODE_USER_MASK		= ((1LLU << 32) - 1),
	UEI_ECODE_SYS_RSN_MASK		= ((1LLU << 16) - 1) << 32,
	UEI_ECODE_SYS_ACT_MASK		= ((1LLU << 16) - 1) << 48,
};

/*
 * These macro interpret the ecode returned from UEI_REPORT().
 */
#define UEI_ECODE_USER(__ecode)		((__ecode) & UEI_ECODE_USER_MASK)
#define UEI_ECODE_SYS_RSN(__ecode)	((__ecode) & UEI_ECODE_SYS_RSN_MASK)
#define UEI_ECODE_SYS_ACT(__ecode)	((__ecode) & UEI_ECODE_SYS_ACT_MASK)

#define UEI_ECODE_RESTART(__ecode)	(UEI_ECODE_SYS_ACT((__ecode)) == SCX_ECODE_ACT_RESTART)

#endif	/* __USER_EXIT_INFO_H */
