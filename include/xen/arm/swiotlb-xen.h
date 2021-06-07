/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ARM_SWIOTLB_XEN_H
#define _ASM_ARM_SWIOTLB_XEN_H

#include <xen/features.h>
#include <xen/xen.h>

static inline int xen_swiotlb_detect(void)
{
	if (!xen_domain())
		return 0;
	if (xen_feature(XENFEAT_direct_mapped))
		return 1;
	/* legacy case */
	if (!xen_feature(XENFEAT_not_direct_mapped) && xen_initial_domain())
		return 1;
	return 0;
}

#endif /* _ASM_ARM_SWIOTLB_XEN_H */
