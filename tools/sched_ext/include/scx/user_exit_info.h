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

enum uei_sizes {
	UEI_REASON_LEN		= 128,
	UEI_MSG_LEN		= 1024,
};

struct user_exit_info {
	int		kind;
	s64		exit_code;
	char		reason[UEI_REASON_LEN];
	char		msg[UEI_MSG_LEN];
};

#ifdef __bpf__

#include "vmlinux.h"
#include <bpf/bpf_core_read.h>

#define UEI_DEFINE(__name)							\
	struct user_exit_info __name SEC(".data")

#define UEI_RECORD(__uei_name, __ei) ({						\
	bpf_probe_read_kernel_str(__uei_name.reason,				\
				  sizeof(__uei_name.reason), (__ei)->reason);	\
	bpf_probe_read_kernel_str(__uei_name.msg,				\
				  sizeof(__uei_name.msg), (__ei)->msg);		\
	if (bpf_core_field_exists((__ei)->exit_code))				\
		__uei_name.exit_code = (__ei)->exit_code;			\
	/* use __sync to force memory barrier */				\
	__sync_val_compare_and_swap(&__uei_name.kind, __uei_name.kind,		\
				    (__ei)->kind);				\
})

#else	/* !__bpf__ */

#include <stdio.h>
#include <stdbool.h>

#define UEI_EXITED(__skel, __uei_name) ({					\
	/* use __sync to force memory barrier */				\
	__sync_val_compare_and_swap(&(__skel)->data->__uei_name.kind, -1, -1);	\
})

#define UEI_REPORT(__skel, __uei_name) ({					\
	struct user_exit_info *__uei = &(__skel)->data->__uei_name;		\
	fprintf(stderr, "EXIT: %s", __uei->reason);				\
	if (__uei->msg[0] != '\0')						\
		fprintf(stderr, " (%s)", __uei->msg);				\
	fputs("\n", stderr);							\
})

#endif	/* __bpf__ */
#endif	/* __USER_EXIT_INFO_H */
