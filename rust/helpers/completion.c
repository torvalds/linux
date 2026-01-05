// SPDX-License-Identifier: GPL-2.0

#include <linux/completion.h>

__rust_helper void rust_helper_init_completion(struct completion *x)
{
	init_completion(x);
}
