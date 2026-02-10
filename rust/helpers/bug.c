// SPDX-License-Identifier: GPL-2.0

#include <linux/bug.h>

__rust_helper __noreturn void rust_helper_BUG(void)
{
	BUG();
}

__rust_helper bool rust_helper_WARN_ON(bool cond)
{
	return WARN_ON(cond);
}
