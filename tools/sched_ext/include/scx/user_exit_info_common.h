/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Define struct user_exit_info which is shared between BPF and userspace parts
 * to communicate exit status and other information.
 *
 * Copyright (c) 2022 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2022 Tejun Heo <tj@kernel.org>
 * Copyright (c) 2022 David Vernet <dvernet@meta.com>
 */
#ifndef __USER_EXIT_INFO_COMMON_H
#define __USER_EXIT_INFO_COMMON_H

#ifdef LSP
#include "../vmlinux.h"
#endif

enum uei_sizes {
	UEI_REASON_LEN		= 128,
	UEI_MSG_LEN		= 1024,
	UEI_DUMP_DFL_LEN	= 32768,
};

struct user_exit_info {
	int		kind;
	s64		exit_code;
	char		reason[UEI_REASON_LEN];
	char		msg[UEI_MSG_LEN];
};

#endif /* __USER_EXIT_INFO_COMMON_H */
