/*	$OpenBSD: memprobe.c,v 1.2 2021/01/28 18:54:50 deraadt Exp $	*/

/*
 * Copyright (c) 1997-1999 Michael Shalayeff
 * Copyright (c) 1997-1999 Tobias Weingartner
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/param.h>
#include <machine/biosvar.h>
#include <dev/isa/isareg.h>
#include <stand/boot/bootarg.h>
#include "libsa.h"

u_int cnvmem, extmem;		/* XXX - compatibility */

extern bios_memmap_t bios_memmap[64];	/* This is easier */

void
dump_biosmem(bios_memmap_t *tm)
{
	register bios_memmap_t *p;
	register u_int total = 0;

	if (tm == NULL)
		tm = bios_memmap;

	for (p = tm; p->type != BIOS_MAP_END; p++) {
		printf("Region %ld: type %u at 0x%llx for %uKB\n",
		    (long)(p - tm), p->type, p->addr,
		    (u_int)(p->size / 1024));

		if (p->type == BIOS_MAP_FREE)
			total += p->size / 1024;
	}

	printf("Low ram: %dKB  High ram: %dKB\n", cnvmem, extmem);
	printf("Total free memory: %uKB\n", total);
}

int
mem_limit(long long ml)
{
	register bios_memmap_t *p;

	for (p = bios_memmap; p->type != BIOS_MAP_END; p++) {
		register int64_t sp = p->addr, ep = p->addr + p->size;

		if (p->type != BIOS_MAP_FREE)
			continue;

		/* Wholly above limit, nuke it */
		if ((sp >= ml) && (ep >= ml)) {
			bcopy (p + 1, p, (char *)bios_memmap +
			       sizeof(bios_memmap) - (char *)p);
		} else if ((sp < ml) && (ep >= ml)) {
			p->size -= (ep - ml);
		}
	}
	return 0;
}

int
mem_delete(long long sa, long long ea)
{
	register bios_memmap_t *p;

	for (p = bios_memmap; p->type != BIOS_MAP_END; p++) {
		if (p->type == BIOS_MAP_FREE) {
			register int64_t sp = p->addr, ep = p->addr + p->size;

			/* can we eat it as a whole? */
			if ((sa - sp) <= PAGE_SIZE && (ep - ea) <= PAGE_SIZE) {
				bcopy(p + 1, p, (char *)bios_memmap +
				    sizeof(bios_memmap) - (char *)p);
				break;
			/* eat head or legs */
			} else if (sa <= sp && sp < ea) {
				p->addr = ea;
				p->size = ep - ea;
				break;
			} else if (sa < ep && ep <= ea) {
				p->size = sa - sp;
				break;
			} else if (sp < sa && ea < ep) {
				/* bite in half */
				bcopy(p, p + 1, (char *)bios_memmap +
				    sizeof(bios_memmap) - (char *)p -
				    sizeof(bios_memmap[0]));
				p[1].addr = ea;
				p[1].size = ep - ea;
				p->size = sa - sp;
				break;
			}
		}
	}
	return 0;
}

int
mem_add(long long sa, long long ea)
{
	register bios_memmap_t *p;

	for (p = bios_memmap; p->type != BIOS_MAP_END; p++) {
		if (p->type == BIOS_MAP_FREE) {
			register int64_t sp = p->addr, ep = p->addr + p->size;

			/* is it already there? */
			if (sp <= sa && ea <= ep) {
				break;
			/* join head or legs */
			} else if (sa < sp && sp <= ea) {
				p->addr = sa;
				p->size = ep - sa;
				break;
			} else if (sa <= ep && ep < ea) {
				p->size = ea - sp;
				break;
			} else if (ea < sp) {
				/* insert before */
				bcopy(p, p + 1, (char *)bios_memmap +
				    sizeof(bios_memmap) - (char *)(p - 1));
				p->addr = sa;
				p->size = ea - sa;
				break;
			}
		}
	}

	/* meaning add new item at the end of the list */
	if (p->type == BIOS_MAP_END) {
		p[1] = p[0];
		p->type = BIOS_MAP_FREE;
		p->addr = sa;
		p->size = ea - sa;
	}

	return 0;
}

void
mem_pass(void)
{
	bios_memmap_t *p;

	for (p = bios_memmap; p->type != BIOS_MAP_END; p++)
		;
	addbootarg(BOOTARG_MEMMAP, (p - bios_memmap + 1) * sizeof *bios_memmap,
	    bios_memmap);
}
