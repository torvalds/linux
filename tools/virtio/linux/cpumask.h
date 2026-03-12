/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_CPUMASK_H
#define _LINUX_CPUMASK_H

#include <linux/kernel.h>

struct cpumask {
	unsigned long bits[1];
};

#endif /* _LINUX_CPUMASK_H */
