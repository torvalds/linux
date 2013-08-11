/*
 * linux/include/linux/netfilter/xt_IDLETIMER.h
 *
 * Header file for Xtables timer target module.
 *
 * Copyright (C) 2004, 2010 Nokia Corporation
 *
 * Written by Timo Teras <ext-timo.teras@nokia.com>
 *
 * Converted to x_tables and forward-ported to 2.6.34
 * by Luciano Coelho <luciano.coelho@nokia.com>
 *
 * Contact: Luciano Coelho <luciano.coelho@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#ifndef _XT_IDLETIMER_H
#define _XT_IDLETIMER_H

#include <linux/types.h>

#define MAX_IDLETIMER_LABEL_SIZE 28
#define NLMSG_MAX_SIZE 64

#define NL_EVENT_TYPE_INACTIVE 0
#define NL_EVENT_TYPE_ACTIVE 1

struct idletimer_tg_info {
	__u32 timeout;

	char label[MAX_IDLETIMER_LABEL_SIZE];

	/* Use netlink messages for notification in addition to sysfs */
	__u8 send_nl_msg;

	/* for kernel module internal use only */
	struct idletimer_tg *timer __attribute__((aligned(8)));
};

#endif
