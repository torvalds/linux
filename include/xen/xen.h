/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _XEN_XEN_H
#define _XEN_XEN_H

#include <linux/types.h>

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

#ifdef CONFIG_XEN_UNPOPULATED_ALLOC
int xen_alloc_unpopulated_pages(unsigned int nr_pages, struct page **pages);
void xen_free_unpopulated_pages(unsigned int nr_pages, struct page **pages);
#include <linux/ioport.h>
int arch_xen_unpopulated_init(struct resource **res);
#else
#include <xen/balloon.h>
static inline int xen_alloc_unpopulated_pages(unsigned int nr_pages,
		struct page **pages)
{
	return xen_alloc_ballooned_pages(nr_pages, pages);
}
static inline void xen_free_unpopulated_pages(unsigned int nr_pages,
		struct page **pages)
{
	xen_free_ballooned_pages(nr_pages, pages);
}
#endif

#if defined(CONFIG_XEN_DOM0) && defined(CONFIG_ACPI) && defined(CONFIG_X86)
bool __init xen_processor_present(uint32_t acpi_id);
#else
#include <linux/bug.h>
static inline bool xen_processor_present(uint32_t acpi_id)
{
	BUG();
	return false;
}
#endif

#endif	/* _XEN_XEN_H */
