// SPDX-License-Identifier: GPL-2.0

#include <linux/processor.h>

void rust_helper_cpu_relax(void)
{
	cpu_relax();
}
