// SPDX-License-Identifier: GPL-2.0

#include <asm/barrier.h>

void rust_helper_smp_mb(void)
{
	smp_mb();
}

void rust_helper_smp_wmb(void)
{
	smp_wmb();
}

void rust_helper_smp_rmb(void)
{
	smp_rmb();
}
