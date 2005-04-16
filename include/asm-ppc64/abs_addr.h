#ifndef _ABS_ADDR_H
#define _ABS_ADDR_H

#include <linux/config.h>

/*
 * c 2001 PPC 64 Team, IBM Corp
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <asm/types.h>
#include <asm/page.h>
#include <asm/prom.h>
#include <asm/lmb.h>

typedef u32 msChunks_entry;
struct msChunks {
        unsigned long num_chunks;
        unsigned long chunk_size;
        unsigned long chunk_shift;
        unsigned long chunk_mask;
        msChunks_entry *abs;
};

extern struct msChunks msChunks;

extern unsigned long msChunks_alloc(unsigned long, unsigned long, unsigned long);
extern unsigned long reloc_offset(void);

#ifdef CONFIG_MSCHUNKS

static inline unsigned long
chunk_to_addr(unsigned long chunk)
{
	unsigned long offset = reloc_offset();
	struct msChunks *_msChunks = PTRRELOC(&msChunks);

	return chunk << _msChunks->chunk_shift;
}

static inline unsigned long
addr_to_chunk(unsigned long addr)
{
	unsigned long offset = reloc_offset();
	struct msChunks *_msChunks = PTRRELOC(&msChunks);

	return addr >> _msChunks->chunk_shift;
}

static inline unsigned long
chunk_offset(unsigned long addr)
{
	unsigned long offset = reloc_offset();
	struct msChunks *_msChunks = PTRRELOC(&msChunks);

	return addr & _msChunks->chunk_mask;
}

static inline unsigned long
abs_chunk(unsigned long pchunk)
{
	unsigned long offset = reloc_offset();
	struct msChunks *_msChunks = PTRRELOC(&msChunks);
	if ( pchunk >= _msChunks->num_chunks ) {
		return pchunk;
	}
	return PTRRELOC(_msChunks->abs)[pchunk];
}

/* A macro so it can take pointers or unsigned long. */
#define phys_to_abs(pa)						     \
	({ unsigned long _pa = (unsigned long)(pa);			     \
	   chunk_to_addr(abs_chunk(addr_to_chunk(_pa))) + chunk_offset(_pa); \
	})

static inline unsigned long
physRpn_to_absRpn(unsigned long rpn)
{
	unsigned long pa = rpn << PAGE_SHIFT;
	unsigned long aa = phys_to_abs(pa);
	return (aa >> PAGE_SHIFT);
}

/* A macro so it can take pointers or unsigned long. */
#define abs_to_phys(aa) lmb_abs_to_phys((unsigned long)(aa))

#else  /* !CONFIG_MSCHUNKS */

#define chunk_to_addr(chunk) ((unsigned long)(chunk))
#define addr_to_chunk(addr) (addr)
#define chunk_offset(addr) (0)
#define abs_chunk(pchunk) (pchunk)

#define phys_to_abs(pa) (pa)
#define physRpn_to_absRpn(rpn) (rpn)
#define abs_to_phys(aa) (aa)

#endif /* !CONFIG_MSCHUNKS */

/* Convenience macros */
#define virt_to_abs(va) phys_to_abs(__pa(va))
#define abs_to_virt(aa) __va(abs_to_phys(aa))

#endif /* _ABS_ADDR_H */
