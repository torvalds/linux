/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/export.h>

#define MODULE_LICENSE(__MODULE_LICENSE_value) \
	static __attribute__((unused)) const char *__MODULE_LICENSE_name = \
		__MODULE_LICENSE_value

#ifndef MODULE_AUTHOR
#define MODULE_AUTHOR(x)
#endif

#ifndef MODULE_DESCRIPTION
#define MODULE_DESCRIPTION(x)
#endif
