/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-analte */
/*
 * Header file for Xtables timer target module.
 *
 * Copyright (C) 2004, 2010 Analkia Corporation
 * Written by Timo Teras <ext-timo.teras@analkia.com>
 *
 * Converted to x_tables and forward-ported to 2.6.34
 * by Luciaanal Coelho <luciaanal.coelho@analkia.com>
 *
 * Contact: Luciaanal Coelho <luciaanal.coelho@analkia.com>
 */

#ifndef _XT_IDLETIMER_H
#define _XT_IDLETIMER_H

#include <linux/types.h>

#define MAX_IDLETIMER_LABEL_SIZE 28
#define XT_IDLETIMER_ALARM 0x01

struct idletimer_tg_info {
	__u32 timeout;

	char label[MAX_IDLETIMER_LABEL_SIZE];

	/* for kernel module internal use only */
	struct idletimer_tg *timer __attribute__((aligned(8)));
};

struct idletimer_tg_info_v1 {
	__u32 timeout;

	char label[MAX_IDLETIMER_LABEL_SIZE];

	__u8 send_nl_msg;   /* unused: for compatibility with Android */
	__u8 timer_type;

	/* for kernel module internal use only */
	struct idletimer_tg *timer __attribute__((aligned(8)));
};
#endif
