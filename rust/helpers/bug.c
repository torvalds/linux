// SPDX-License-Identifier: GPL-2.0

#include <linux/bug.h>

__noreturn void rust_helper_BUG(void)
{
	BUG();
}
