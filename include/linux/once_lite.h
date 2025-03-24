/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_ONCE_LITE_H
#define _LINUX_ONCE_LITE_H

#include <linux/types.h>

/* Call a function once. Similar to DO_ONCE(), but does not use jump label
 * patching via static keys.
 */
#define DO_ONCE_LITE(func, ...)						\
	DO_ONCE_LITE_IF(true, func, ##__VA_ARGS__)

#define __ONCE_LITE_IF(condition)					\
	({								\
		static bool __section(".data..once") __already_done;	\
		bool __ret_cond = !!(condition);			\
		bool __ret_once = false;				\
									\
		if (unlikely(__ret_cond && !__already_done)) {		\
			__already_done = true;				\
			__ret_once = true;				\
		}							\
		unlikely(__ret_once);					\
	})

#define DO_ONCE_LITE_IF(condition, func, ...)				\
	({								\
		bool __ret_do_once = !!(condition);			\
									\
		if (__ONCE_LITE_IF(__ret_do_once))			\
			func(__VA_ARGS__);				\
									\
		unlikely(__ret_do_once);				\
	})

#endif /* _LINUX_ONCE_LITE_H */
