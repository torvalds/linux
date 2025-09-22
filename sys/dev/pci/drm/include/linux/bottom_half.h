/* Public domain. */

#ifndef _LINUX_BOTTOM_HALF_H
#define _LINUX_BOTTOM_HALF_H

#include <linux/preempt.h>

static inline void
local_bh_disable(void)
{
}

static inline void
local_bh_enable(void)
{
}

#endif
