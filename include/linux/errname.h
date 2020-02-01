/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_ERRNAME_H
#define _LINUX_ERRNAME_H

#include <linux/stddef.h>

#ifdef CONFIG_SYMBOLIC_ERRNAME
const char *errname(int err);
#else
static inline const char *errname(int err)
{
	return NULL;
}
#endif

#endif /* _LINUX_ERRNAME_H */
