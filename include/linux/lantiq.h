/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __LINUX_LANTIQ_H
#define __LINUX_LANTIQ_H

#ifdef CONFIG_LANTIQ
#include <lantiq_soc.h>
#else

#ifndef LTQ_EARLY_ASC
#define LTQ_EARLY_ASC 0
#endif

#ifndef CPHYSADDR
#define CPHYSADDR(a) 0
#endif

static inline struct clk *clk_get_fpi(void)
{
	return NULL;
}
#endif /* CONFIG_LANTIQ */
#endif /* __LINUX_LANTIQ_H */
