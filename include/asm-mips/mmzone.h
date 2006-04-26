/*
 * Written by Kanoj Sarcar (kanoj@sgi.com) Aug 99
 * Rewritten for Linux 2.6 by Christoph Hellwig (hch@lst.de) Jan 2004
 */
#ifndef _ASM_MMZONE_H_
#define _ASM_MMZONE_H_

#include <asm/page.h>
#include <mmzone.h>

#ifdef CONFIG_DISCONTIGMEM

#define kvaddr_to_nid(kvaddr)	pa_to_nid(__pa(kvaddr))
#define pfn_to_nid(pfn)		pa_to_nid((pfn) << PAGE_SHIFT)

#define pfn_valid(pfn)						\
({								\
 	unsigned long __pfn = (pfn);				\
 	int __n = pfn_to_nid(__pfn);				\
	((__n >= 0) ? (__pfn < NODE_DATA(__n)->node_start_pfn +	\
		       NODE_DATA(__n)->node_spanned_pages) : 0);\
})

/* XXX: FIXME -- wli */
#define kern_addr_valid(addr)	(0)

#endif /* CONFIG_DISCONTIGMEM */

#endif /* _ASM_MMZONE_H_ */
