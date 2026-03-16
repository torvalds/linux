/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TOOLS_INCLUDE_LINUX_GFP_H
#define _TOOLS_INCLUDE_LINUX_GFP_H

#include <linux/types.h>
#include <linux/gfp_types.h>

/* Helper macro to avoid gfp flags if they are the default one */
#define __default_gfp(a,...) a
#define default_gfp(...) __default_gfp(__VA_ARGS__ __VA_OPT__(,) GFP_KERNEL)

static inline bool gfpflags_allow_blocking(const gfp_t gfp_flags)
{
	return !!(gfp_flags & __GFP_DIRECT_RECLAIM);
}

#endif /* _TOOLS_INCLUDE_LINUX_GFP_H */
