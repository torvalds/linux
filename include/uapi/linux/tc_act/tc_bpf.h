/*
 * Copyright (c) 2015 Jiri Pirko <jiri@resnulli.us>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __LINUX_TC_BPF_H
#define __LINUX_TC_BPF_H

#include <linux/pkt_cls.h>

#define TCA_ACT_BPF 13

struct tc_act_bpf {
	tc_gen;
};

enum {
	TCA_ACT_BPF_UNSPEC,
	TCA_ACT_BPF_TM,
	TCA_ACT_BPF_PARMS,
	TCA_ACT_BPF_OPS_LEN,
	TCA_ACT_BPF_OPS,
	TCA_ACT_BPF_FD,
	TCA_ACT_BPF_NAME,
	__TCA_ACT_BPF_MAX,
};
#define TCA_ACT_BPF_MAX (__TCA_ACT_BPF_MAX - 1)

#endif
