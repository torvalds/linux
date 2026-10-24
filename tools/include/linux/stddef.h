/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TOOLS_LINUX_STDDEF_H
#define _TOOLS_LINUX_STDDEF_H

#include_next <linux/stddef.h>

#define DECLARE_FLEX_ARRAY(TYPE, NAME) \
	__DECLARE_FLEX_ARRAY(TYPE, NAME)

#endif /* _TOOLS_LINUX_STDDEF_H */
