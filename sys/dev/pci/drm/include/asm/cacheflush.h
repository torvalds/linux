/* Public domain. */

#ifndef _ASM_CACHEFLUSH_H
#define _ASM_CACHEFLUSH_H

#if defined(__i386__) || defined(__amd64__)
#include <uvm/uvm_extern.h>
#include <machine/pmap.h>

#define clflush_cache_range(va, len)	pmap_flush_cache((vaddr_t)(va), len)

#endif

#endif
