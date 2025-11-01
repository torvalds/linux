/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Define struct user_exit_info which is shared between BPF and userspace parts
 * to communicate exit status and other information.
 *
 * Copyright (c) 2022 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2022 Tejun Heo <tj@kernel.org>
 * Copyright (c) 2022 David Vernet <dvernet@meta.com>
 */

#ifndef __USER_EXIT_INFO_BPF_H
#define __USER_EXIT_INFO_BPF_H

#ifndef LSP
#include "vmlinux.h"
#endif
#include <bpf/bpf_core_read.h>

#include "user_exit_info_common.h"

#define UEI_DEFINE(__name)							\
	char RESIZABLE_ARRAY(data, __name##_dump);				\
	const volatile u32 __name##_dump_len;					\
	struct user_exit_info __name SEC(".data")

#define UEI_RECORD(__uei_name, __ei) ({						\
	bpf_probe_read_kernel_str(__uei_name.reason,				\
				  sizeof(__uei_name.reason), (__ei)->reason);	\
	bpf_probe_read_kernel_str(__uei_name.msg,				\
				  sizeof(__uei_name.msg), (__ei)->msg);		\
	bpf_probe_read_kernel_str(__uei_name##_dump,				\
				  __uei_name##_dump_len, (__ei)->dump);		\
	if (bpf_core_field_exists((__ei)->exit_code))				\
		__uei_name.exit_code = (__ei)->exit_code;			\
	/* use __sync to force memory barrier */				\
	__sync_val_compare_and_swap(&__uei_name.kind, __uei_name.kind,		\
				    (__ei)->kind);				\
})

#endif /* __USER_EXIT_INFO_BPF_H */
