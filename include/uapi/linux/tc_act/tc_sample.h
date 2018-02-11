/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __LINUX_TC_SAMPLE_H
#define __LINUX_TC_SAMPLE_H

#include <linux/types.h>
#include <linux/pkt_cls.h>
#include <linux/if_ether.h>

#define TCA_ACT_SAMPLE 26

struct tc_sample {
	tc_gen;
};

enum {
	TCA_SAMPLE_UNSPEC,
	TCA_SAMPLE_TM,
	TCA_SAMPLE_PARMS,
	TCA_SAMPLE_RATE,
	TCA_SAMPLE_TRUNC_SIZE,
	TCA_SAMPLE_PSAMPLE_GROUP,
	TCA_SAMPLE_PAD,
	__TCA_SAMPLE_MAX
};
#define TCA_SAMPLE_MAX (__TCA_SAMPLE_MAX - 1)

#endif
