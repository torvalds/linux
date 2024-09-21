/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/types.h>
#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/bitops.h>

#include <linux/gfp.h>
#include <linux/rcupdate.h>

#ifndef module_init
#define module_init(x)
#endif

#ifndef module_exit
#define module_exit(x)
#endif

#ifndef MODULE_AUTHOR
#define MODULE_AUTHOR(x)
#endif

#ifndef MODULE_LICENSE
#define MODULE_LICENSE(x)
#endif

#ifndef MODULE_DESCRIPTION
#define MODULE_DESCRIPTION(x)
#endif

#ifndef dump_stack
#define dump_stack()	assert(0)
#endif
