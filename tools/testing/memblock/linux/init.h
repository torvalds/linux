/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_INIT_H
#define _LINUX_INIT_H

#include <linux/compiler.h>
#include <asm/export.h>
#include <linux/memory_hotplug.h>

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

#define early_param(str, fn)						\
	__setup_param(str, fn, fn, 1)

#endif
