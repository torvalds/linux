// SPDX-License-Identifier: GPL-2.0

#include <linux/maple_tree.h>

void rust_helper_mt_init_flags(struct maple_tree *mt, unsigned int flags)
{
	mt_init_flags(mt, flags);
}
