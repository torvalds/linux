// SPDX-License-Identifier: GPL-2.0

#include <linux/completion.h>

void rust_helper_init_completion(struct completion *x)
{
	init_completion(x);
}
