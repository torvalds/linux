/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _XT_CPU_H
#define _XT_CPU_H

#include <linux/types.h>

struct xt_cpu_info {
	__u32	cpu;
	__u32	invert;
};

#endif /*_XT_CPU_H*/
