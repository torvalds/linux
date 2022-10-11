/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (c) 2008, Intel Corporation.
 *
 * Author: Alexander Duyck <alexander.h.duyck@intel.com>
 */

#ifndef __LINUX_TC_SKBEDIT_H
#define __LINUX_TC_SKBEDIT_H

#include <linux/pkt_cls.h>

#define SKBEDIT_F_PRIORITY		0x1
#define SKBEDIT_F_QUEUE_MAPPING		0x2
#define SKBEDIT_F_MARK			0x4
#define SKBEDIT_F_PTYPE			0x8
#define SKBEDIT_F_MASK			0x10
#define SKBEDIT_F_INHERITDSFIELD	0x20
#define SKBEDIT_F_TXQ_SKBHASH		0x40

struct tc_skbedit {
	tc_gen;
};

enum {
	TCA_SKBEDIT_UNSPEC,
	TCA_SKBEDIT_TM,
	TCA_SKBEDIT_PARMS,
	TCA_SKBEDIT_PRIORITY,
	TCA_SKBEDIT_QUEUE_MAPPING,
	TCA_SKBEDIT_MARK,
	TCA_SKBEDIT_PAD,
	TCA_SKBEDIT_PTYPE,
	TCA_SKBEDIT_MASK,
	TCA_SKBEDIT_FLAGS,
	TCA_SKBEDIT_QUEUE_MAPPING_MAX,
	__TCA_SKBEDIT_MAX
};
#define TCA_SKBEDIT_MAX (__TCA_SKBEDIT_MAX - 1)

#endif
