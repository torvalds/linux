// SPDX-License-Identifier: GPL-2.0

#include <linux/err.h>
#include <linux/export.h>

__force void *rust_helper_ERR_PTR(long err)
{
	return ERR_PTR(err);
}

bool rust_helper_IS_ERR(__force const void *ptr)
{
	return IS_ERR(ptr);
}

long rust_helper_PTR_ERR(__force const void *ptr)
{
	return PTR_ERR(ptr);
}
