// SPDX-License-Identifier: GPL-2.0

#include <linux/processor.h>

__rust_helper void rust_helper_cpu_relax(void)
{
	cpu_relax();
}
