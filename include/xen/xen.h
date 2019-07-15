/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _XEN_XEN_H
#define _XEN_XEN_H

enum xen_domain_type {
	XEN_NATIVE,		/* running on bare hardware    */
	XEN_PV_DOMAIN,		/* running in a PV domain      */
	XEN_HVM_DOMAIN,		/* running in a Xen hvm domain */
};

#ifdef CONFIG_XEN
extern enum xen_domain_type xen_domain_type;
#else
#define xen_domain_type		XEN_NATIVE
#endif

#ifdef CONFIG_XEN_PVH
extern bool xen_pvh;
#else
#define xen_pvh			0
#endif

#define xen_domain()		(xen_domain_type != XEN_NATIVE)
#define xen_pv_domain()		(xen_domain_type == XEN_PV_DOMAIN)
#define xen_hvm_domain()	(xen_domain_type == XEN_HVM_DOMAIN)
#define xen_pvh_domain()	(xen_pvh)

#include <linux/types.h>

extern uint32_t xen_start_flags;

#include <xen/interface/hvm/start_info.h>
extern struct hvm_start_info pvh_start_info;

#ifdef CONFIG_XEN_DOM0
#include <xen/interface/xen.h>
#include <asm/xen/hypervisor.h>

#define xen_initial_domain()	(xen_domain() && \
				 (xen_start_flags & SIF_INITDOMAIN))
#else  /* !CONFIG_XEN_DOM0 */
#define xen_initial_domain()	(0)
#endif	/* CONFIG_XEN_DOM0 */

struct bio_vec;
struct page;

bool xen_biovec_phys_mergeable(const struct bio_vec *vec1,
		const struct page *page);

#if defined(CONFIG_MEMORY_HOTPLUG) && defined(CONFIG_XEN_BALLOON)
extern u64 xen_saved_max_mem_size;
#endif

#endif	/* _XEN_XEN_H */
