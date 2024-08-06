/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TOOLS_LINUX_INIT_H_
#define _TOOLS_LINUX_INIT_H_

#include <linux/compiler.h>

#ifndef __init
# define __init
#endif

#ifndef __exit
# define __exit
#endif

#define __section(section)              __attribute__((__section__(section)))

#define __initconst
#define __meminit
#define __meminitdata
#define __refdata
#define __initdata

struct obs_kernel_param {
	const char *str;
	int (*setup_func)(char *st);
	int early;
};

#define __setup_param(str, unique_id, fn, early)			\
	static const char __setup_str_##unique_id[] __initconst		\
		__aligned(1) = str;					\
	static struct obs_kernel_param __setup_##unique_id		\
		__used __section(".init.setup")				\
		__aligned(__alignof__(struct obs_kernel_param)) =	\
		{ __setup_str_##unique_id, fn, early }

#define __setup(str, fn)						\
	__setup_param(str, fn, fn, 0)

#define early_param(str, fn)						\
	__setup_param(str, fn, fn, 1)

#endif /*  _TOOLS_LINUX_INIT_H_ */
