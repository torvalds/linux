/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _UTIL_H
#define _UTIL_H

#include <objtool/warn.h>

#define snprintf_check(str, size, format, args...)			\
({									\
	int __ret = snprintf(str, size, format, args);			\
	if (__ret < 0)							\
		ERROR_GLIBC("snprintf");				\
	else if (__ret >= size)						\
		ERROR("snprintf() failed for '" format "'", args);	\
	else								\
		__ret = 0;						\
	__ret;								\
})

#endif /* _UTIL_H */
