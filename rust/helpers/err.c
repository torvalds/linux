// SPDX-License-Identifier: GPL-2.0

#include <linux/err.h>

__rust_helper __force void *rust_helper_ERR_PTR(long err)
{
	return ERR_PTR(err);
}

__rust_helper bool rust_helper_IS_ERR(__force const void *ptr)
{
	return IS_ERR(ptr);
}

__rust_helper long rust_helper_PTR_ERR(__force const void *ptr)
{
	return PTR_ERR(ptr);
}
