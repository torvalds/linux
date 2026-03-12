// SPDX-License-Identifier: GPL-2.0

#include <asm/barrier.h>

__rust_helper void rust_helper_smp_mb(void)
{
	smp_mb();
}

__rust_helper void rust_helper_smp_wmb(void)
{
	smp_wmb();
}

__rust_helper void rust_helper_smp_rmb(void)
{
	smp_rmb();
}
