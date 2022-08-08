/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_ONCE_LITE_H
#define _LINUX_ONCE_LITE_H

#include <linux/types.h>

/* Call a function once. Similar to DO_ONCE(), but does not use jump label
 * patching via static keys.
 */
#define DO_ONCE_LITE(func, ...)						\
	DO_ONCE_LITE_IF(true, func, ##__VA_ARGS__)
#define DO_ONCE_LITE_IF(condition, func, ...)				\
	({								\
		static bool __section(".data.once") __already_done;	\
		bool __ret_do_once = !!(condition);			\
									\
		if (unlikely(__ret_do_once && !__already_done)) {	\
			__already_done = true;				\
			func(__VA_ARGS__);				\
		}							\
		unlikely(__ret_do_once);				\
	})

#endif /* _LINUX_ONCE_LITE_H */
