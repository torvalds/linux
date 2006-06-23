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

#endif /* CONFIG_DISCONTIGMEM */

#endif /* _ASM_MMZONE_H_ */
