/*	$OpenBSD: mainbus.c,v 1.92 2025/06/28 13:24:21 miod Exp $	*/

/*
 * Copyright (c) 1998-2004 Michael Shalayeff
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "lcd.h"
#include "power.h"

#undef BTLBDEBUG

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/device.h>
#include <sys/reboot.h>
#include <sys/extent.h>
#include <sys/mbuf.h>

#include <uvm/uvm_extern.h>

#include <machine/bus.h>
#include <machine/pdc.h>
#include <machine/iomod.h>
#include <machine/autoconf.h>

#include <hppa/dev/cpudevs.h>

struct mainbus_softc {
	struct  device sc_dv;

	hppa_hpa_t sc_hpa;
};

int	mbmatch(struct device *, void *, void *);
void	mbattach(struct device *, struct device *, void *);

const struct cfattach mainbus_ca = {
	sizeof(struct mainbus_softc), mbmatch, mbattach
};

struct cfdriver mainbus_cd = {
	NULL, "mainbus", DV_DULL
};

struct pdc_hpa pdc_hpa PDC_ALIGNMENT;
struct pdc_power_info pdc_power_info PDC_ALIGNMENT;
struct pdc_chassis_info pdc_chassis_info PDC_ALIGNMENT;
struct pdc_chassis_lcd pdc_chassis_lcd PDC_ALIGNMENT;

/* from machdep.c */
extern struct extent *hppa_ex;
extern struct pdc_btlb pdc_btlb;

int		 mbus_add_mapping(bus_addr_t bpa, bus_size_t size, int flags,
		    bus_space_handle_t *bshp);
int		 mbus_map(void *v, bus_addr_t bpa, bus_size_t size,
		    int flags, bus_space_handle_t *bshp);
void		 mbus_unmap(void *v, bus_space_handle_t bsh, bus_size_t size);
int		 mbus_alloc(void *v, bus_addr_t rstart, bus_addr_t rend,
		    bus_size_t size, bus_size_t align, bus_size_t boundary,
		    int flags, bus_addr_t *addrp, bus_space_handle_t *bshp);
void		 mbus_free(void *v, bus_space_handle_t h, bus_size_t size);
int		 mbus_subregion(void *v, bus_space_handle_t bsh,
		    bus_size_t offset, bus_size_t size,
		    bus_space_handle_t *nbshp);
void		 mbus_barrier(void *v, bus_space_handle_t h, bus_size_t o,
		    bus_size_t l, int op);
void		*mbus_vaddr(void *v, bus_space_handle_t h);
u_int8_t	 mbus_r1(void *v, bus_space_handle_t h, bus_size_t o);
u_int16_t	 mbus_r2(void *v, bus_space_handle_t h, bus_size_t o);
u_int32_t	 mbus_r4(void *v, bus_space_handle_t h, bus_size_t o);
u_int64_t	 mbus_r8(void *v, bus_space_handle_t h, bus_size_t o);
void		 mbus_w1(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int8_t vv);
void		 mbus_w2(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int16_t vv);
void		 mbus_w4(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int32_t vv);
void		 mbus_w8(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int64_t vv);
void		 mbus_rm_1(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int8_t *a, bus_size_t c);
void		 mbus_rm_2(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int16_t *a, bus_size_t c);
void		 mbus_rm_4(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int32_t *a, bus_size_t c);
void		 mbus_rm_8(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int64_t *a, bus_size_t c);
void		 mbus_wm_1(void *v, bus_space_handle_t h, bus_size_t o,
		    const u_int8_t *a, bus_size_t c);
void		 mbus_wm_2(void *v, bus_space_handle_t h, bus_size_t o,
		    const u_int16_t *a, bus_size_t c);
void		 mbus_wm_4(void *v, bus_space_handle_t h, bus_size_t o,
		    const u_int32_t *a, bus_size_t c);
void		 mbus_wm_8(void *v, bus_space_handle_t h, bus_size_t o,
		    const u_int64_t *a, bus_size_t c);
void		 mbus_sm_1(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int8_t vv, bus_size_t c);
void		 mbus_sm_2(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int16_t vv, bus_size_t c);
void		 mbus_sm_4(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int32_t vv, bus_size_t c);
void		 mbus_sm_8(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int64_t vv, bus_size_t c);

void		 mbus_rr_1(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int8_t *a, bus_size_t c);
void		 mbus_rr_2(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int16_t *a, bus_size_t c);
void		 mbus_rr_4(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int32_t *a, bus_size_t c);
void		 mbus_rr_8(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int64_t *a, bus_size_t c);
void		 mbus_wr_1(void *v, bus_space_handle_t h, bus_size_t o,
		    const u_int8_t *a, bus_size_t c);
void		 mbus_wr_2(void *v, bus_space_handle_t h, bus_size_t o,
		    const u_int16_t *a, bus_size_t c);
void		 mbus_wr_4(void *v, bus_space_handle_t h, bus_size_t o,
		    const u_int32_t *a, bus_size_t c);
void		 mbus_wr_8(void *v, bus_space_handle_t h, bus_size_t o,
		    const u_int64_t *a, bus_size_t c);
void		 mbus_sr_1(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int8_t vv, bus_size_t c);
void		 mbus_sr_2(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int16_t vv, bus_size_t c);
void		 mbus_sr_4(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int32_t vv, bus_size_t c);
void		 mbus_sr_8(void *v, bus_space_handle_t h, bus_size_t o,
		    u_int64_t vv, bus_size_t c);
void		 mbus_cp_1(void *v, bus_space_handle_t h1, bus_size_t o1,
		    bus_space_handle_t h2, bus_size_t o2, bus_size_t c);
void		 mbus_cp_2(void *v, bus_space_handle_t h1, bus_size_t o1,
		    bus_space_handle_t h2, bus_size_t o2, bus_size_t c);
void		 mbus_cp_4(void *v, bus_space_handle_t h1, bus_size_t o1,
		    bus_space_handle_t h2, bus_size_t o2, bus_size_t c);
void		 mbus_cp_8(void *v, bus_space_handle_t h1, bus_size_t o1,
		    bus_space_handle_t h2, bus_size_t o2, bus_size_t c);

int
mbus_add_mapping(bus_addr_t bpa, bus_size_t size, int flags,
    bus_space_handle_t *bshp)
{
	static u_int32_t bmm[0x4000/32];
	int bank, off, flex;
	vaddr_t pa, spa, epa;
	vsize_t len;

#ifdef BTLBDEBUG
	printf("bus_mem_add_mapping(%lx,%lx,%scachable,%p)\n",
	    bpa, size, flags? "" : "non", bshp);
#endif

	if ((bank = vm_physseg_find(atop(bpa), &off)) >= 0)
		panic("mbus_add_mapping: mapping real memory @0x%lx", bpa);

#ifdef DEBUG
	if (flags & BUS_SPACE_MAP_CACHEABLE) {
		printf("WARNING: mapping I/O space cacheable\n");
		flags &= ~BUS_SPACE_MAP_CACHEABLE;
	}
#endif

	/*
	 * Mappings are established in HPPA_FLEX_SIZE units,
	 * either with BTLB, or regular mappings of the whole area.
	 */
	pa = bpa;
	while (size != 0) {
		flex = HPPA_FLEX(pa);
		spa = pa & HPPA_FLEX_MASK;
		epa = spa + HPPA_FLEX_SIZE; /* may wrap to 0... */

		size -= min(size, HPPA_FLEX_SIZE - (pa - spa));

		/* do need a new mapping? */
		if (!(bmm[flex / 32] & (1U << (flex % 32)))) {
#ifdef BTLBDEBUG
			printf("bus_mem_add_mapping: adding flex=%x "
			    "%lx-%lx, ", flex, spa, epa - 1);
#endif
			while (spa != epa) {
				len = epa - spa;

				/*
				 * Try to map with a BTLB first (might map
				 * much more than what we are requesting
				 * for, and cross HPPA_FLEX boundaries).
				 *
				 * Note that this code assumes that
				 * BTLB size are a power of two, so if
				 * the size is larger than HPPA_FLEX_SIZE
				 * it will span an integral number of
				 * HPPA_FLEX_SIZE slots.
				 */
				if (len > pdc_btlb.max_size << PGSHIFT)
					len = pdc_btlb.max_size << PGSHIFT;

				if (btlb_insert(HPPA_SID_KERNEL, spa, spa, &len,
				    pmap_sid2pid(HPPA_SID_KERNEL) |
				    pmap_prot(pmap_kernel(), PROT_READ | PROT_WRITE))
				    >= 0) {
					pa = spa + len;	/* may wrap to 0... */
#ifdef BTLBDEBUG
					printf("--- %x/%lx, %lx-%lx ",
					    flex, HPPA_FLEX(pa - 1),
					    spa, pa - 1);
#endif
					/* register all ranges */
					for (; flex <= HPPA_FLEX(pa - 1);
					    flex++) {
#ifdef BTLBDEBUG
						printf("mask %x ", flex);
#endif
						bmm[flex / 32] |=
						    (1U << (flex % 32));
					}
					if (len > epa - spa)
						spa = epa;
					else
						spa = pa;
				} else {
#ifdef BTLBDEBUG
					printf("kenter 0x%lx-0x%lx", spa, epa);
#endif
					for (; spa != epa; spa += PAGE_SIZE)
						pmap_kenter_pa(spa, spa,
						    PROT_READ | PROT_WRITE);
				}
#ifdef BTLBDEBUG
				printf("\n");
#endif
			}
		}
#ifdef BTLBDEBUG
		else {
			printf("+++ already b-mapped flex=%x, mask=%x\n",
			    flex, bmm[flex / 32]);
		}
#endif

		pa = epa;
	}

	*bshp = bpa;
	return (0);
}

int
mbus_map(void *v, bus_addr_t bpa, bus_size_t size,
    int flags, bus_space_handle_t *bshp)
{
	int error;

	if ((error = extent_alloc_region(hppa_ex, bpa, size, EX_NOWAIT)))
		return (error);

	if ((error = mbus_add_mapping(bpa, size, flags, bshp))) {
		if (extent_free(hppa_ex, bpa, size, EX_NOWAIT)) {
			printf("bus_space_map: pa 0x%lx, size 0x%lx\n",
				bpa, size);
			printf("bus_space_map: can't free region\n");
		}
	}

	return error;
}

void
mbus_unmap(void *v, bus_space_handle_t bsh, bus_size_t size)
{
	u_long sva, eva;

	sva = trunc_page(bsh);
	eva = round_page(bsh + size);

#ifdef DIAGNOSTIC
	if (eva <= sva)
		panic("bus_space_unmap: overflow");
#endif

	if (pmap_extract(pmap_kernel(), bsh, NULL))
		pmap_kremove(sva, eva - sva);
	else
		;	/* XXX assuming equ b-mapping been done */

	if (extent_free(hppa_ex, bsh, size, EX_NOWAIT)) {
		printf("bus_space_unmap: ps 0x%lx, size 0x%lx\n",
		    bsh, size);
		printf("bus_space_unmap: can't free region\n");
	}
}

int
mbus_alloc(void *v, bus_addr_t rstart, bus_addr_t rend, bus_size_t size,
    bus_size_t align, bus_size_t boundary, int flags,
    bus_addr_t *addrp, bus_space_handle_t *bshp)
{
	u_long bpa;
	int error;

	if (rstart < hppa_ex->ex_start || rend > hppa_ex->ex_end)
		panic("bus_space_alloc: bad region start/end");

	if ((error = extent_alloc_subregion(hppa_ex, rstart, rend, size,
	    align, 0, boundary, EX_NOWAIT, &bpa)))
		return (error);

	if ((error = mbus_add_mapping(bpa, size, flags, bshp))) {
		if (extent_free(hppa_ex, bpa, size, EX_NOWAIT)) {
			printf("bus_space_alloc: pa 0x%lx, size 0x%lx\n",
				bpa, size);
			printf("bus_space_alloc: can't free region\n");
		}
	}

	*addrp = bpa;
	return (error);
}

void
mbus_free(void *v, bus_space_handle_t h, bus_size_t size)
{
	/* bus_space_unmap() does all that we need to do. */
	mbus_unmap(v, h, size);
}

int
mbus_subregion(void *v, bus_space_handle_t bsh, bus_size_t offset,
    bus_size_t size, bus_space_handle_t *nbshp)
{
	*nbshp = bsh + offset;
	return (0);
}

void
mbus_barrier(void *v, bus_space_handle_t h, bus_size_t o, bus_size_t l, int op)
{
	sync_caches();
}

void *
mbus_vaddr(void *v, bus_space_handle_t h)
{
	return ((void *)h);
}

u_int8_t
mbus_r1(void *v, bus_space_handle_t h, bus_size_t o)
{
	return *((volatile u_int8_t *)(h + o));
}

u_int16_t
mbus_r2(void *v, bus_space_handle_t h, bus_size_t o)
{
	return *((volatile u_int16_t *)(h + o));
}

u_int32_t
mbus_r4(void *v, bus_space_handle_t h, bus_size_t o)
{
	return *((volatile u_int32_t *)(h + o));
}

u_int64_t
mbus_r8(void *v, bus_space_handle_t h, bus_size_t o)
{
	return *((volatile u_int64_t *)(h + o));
}

void
mbus_w1(void *v, bus_space_handle_t h, bus_size_t o, u_int8_t vv)
{
	*((volatile u_int8_t *)(h + o)) = vv;
}

void
mbus_w2(void *v, bus_space_handle_t h, bus_size_t o, u_int16_t vv)
{
	*((volatile u_int16_t *)(h + o)) = vv;
}

void
mbus_w4(void *v, bus_space_handle_t h, bus_size_t o, u_int32_t vv)
{
	*((volatile u_int32_t *)(h + o)) = vv;
}

void
mbus_w8(void *v, bus_space_handle_t h, bus_size_t o, u_int64_t vv)
{
	*((volatile u_int64_t *)(h + o)) = vv;
}


void
mbus_rm_1(void *v, bus_space_handle_t h, bus_size_t o, u_int8_t *a, bus_size_t c)
{
	h += o;
	while (c--)
		*(a++) = *(volatile u_int8_t *)h;
}

void
mbus_rm_2(void *v, bus_space_handle_t h, bus_size_t o, u_int16_t *a, bus_size_t c)
{
	h += o;
	while (c--)
		*(a++) = *(volatile u_int16_t *)h;
}

void
mbus_rm_4(void *v, bus_space_handle_t h, bus_size_t o, u_int32_t *a, bus_size_t c)
{
	h += o;
	while (c--)
		*(a++) = *(volatile u_int32_t *)h;
}

void
mbus_rm_8(void *v, bus_space_handle_t h, bus_size_t o, u_int64_t *a, bus_size_t c)
{
	h += o;
	while (c--)
		*(a++) = *(volatile u_int64_t *)h;
}

void
mbus_wm_1(void *v, bus_space_handle_t h, bus_size_t o, const u_int8_t *a, bus_size_t c)
{
	h += o;
	while (c--)
		*(volatile u_int8_t *)h = *(a++);
}

void
mbus_wm_2(void *v, bus_space_handle_t h, bus_size_t o, const u_int16_t *a, bus_size_t c)
{
	h += o;
	while (c--)
		*(volatile u_int16_t *)h = *(a++);
}

void
mbus_wm_4(void *v, bus_space_handle_t h, bus_size_t o, const u_int32_t *a, bus_size_t c)
{
	h += o;
	while (c--)
		*(volatile u_int32_t *)h = *(a++);
}

void
mbus_wm_8(void *v, bus_space_handle_t h, bus_size_t o, const u_int64_t *a, bus_size_t c)
{
	h += o;
	while (c--)
		*(volatile u_int64_t *)h = *(a++);
}

void
mbus_sm_1(void *v, bus_space_handle_t h, bus_size_t o, u_int8_t vv, bus_size_t c)
{
	h += o;
	while (c--)
		*(volatile u_int8_t *)h = vv;
}

void
mbus_sm_2(void *v, bus_space_handle_t h, bus_size_t o, u_int16_t vv, bus_size_t c)
{
	h += o;
	while (c--)
		*(volatile u_int16_t *)h = vv;
}

void
mbus_sm_4(void *v, bus_space_handle_t h, bus_size_t o, u_int32_t vv, bus_size_t c)
{
	h += o;
	while (c--)
		*(volatile u_int32_t *)h = vv;
}

void
mbus_sm_8(void *v, bus_space_handle_t h, bus_size_t o, u_int64_t vv, bus_size_t c)
{
	h += o;
	while (c--)
		*(volatile u_int64_t *)h = vv;
}

void
mbus_rr_1(void *v, bus_space_handle_t h, bus_size_t o, u_int8_t *a, bus_size_t c)
{
	volatile u_int8_t *p = (u_int8_t *)(h + o);

	while (c--)
		*a++ = *p++;
}

void
mbus_rr_2(void *v, bus_space_handle_t h, bus_size_t o, u_int16_t *a, bus_size_t c)
{
	volatile u_int16_t *p = (u_int16_t *)(h + o);

	while (c--)
		*a++ = *p++;
}

void
mbus_rr_4(void *v, bus_space_handle_t h, bus_size_t o, u_int32_t *a, bus_size_t c)
{
	volatile u_int32_t *p = (u_int32_t *)(h + o);

	while (c--)
		*a++ = *p++;
}

void
mbus_rr_8(void *v, bus_space_handle_t h, bus_size_t o, u_int64_t *a, bus_size_t c)
{
	volatile u_int64_t *p = (u_int64_t *)(h + o);

	while (c--)
		*a++ = *p++;
}

void
mbus_wr_1(void *v, bus_space_handle_t h, bus_size_t o, const u_int8_t *a, bus_size_t c)
{
	volatile u_int8_t *p = (u_int8_t *)(h + o);

	while (c--)
		*p++ = *a++;
}

void
mbus_wr_2(void *v, bus_space_handle_t h, bus_size_t o, const u_int16_t *a, bus_size_t c)
{
	volatile u_int16_t *p = (u_int16_t *)(h + o);

	while (c--)
		*p++ = *a++;
}

void
mbus_wr_4(void *v, bus_space_handle_t h, bus_size_t o, const u_int32_t *a, bus_size_t c)
{
	volatile u_int32_t *p = (u_int32_t *)(h + o);

	while (c--)
		*p++ = *a++;
}

void
mbus_wr_8(void *v, bus_space_handle_t h, bus_size_t o, const u_int64_t *a, bus_size_t c)
{
	volatile u_int64_t *p = (u_int64_t *)(h + o);

	while (c--)
		*p++ = *a++;
}

void
mbus_sr_1(void *v, bus_space_handle_t h, bus_size_t o, u_int8_t vv, bus_size_t c)
{
	volatile u_int8_t *p = (u_int8_t *)(h + o);

	while (c--)
		*p++ = vv;
}

void
mbus_sr_2(void *v, bus_space_handle_t h, bus_size_t o, u_int16_t vv, bus_size_t c)
{
	volatile u_int16_t *p = (u_int16_t *)(h + o);

	while (c--)
		*p++ = vv;
}

void
mbus_sr_4(void *v, bus_space_handle_t h, bus_size_t o, u_int32_t vv, bus_size_t c)
{
	volatile u_int32_t *p = (u_int32_t *)(h + o);

	while (c--)
		*p++ = vv;
}

void
mbus_sr_8(void *v, bus_space_handle_t h, bus_size_t o, u_int64_t vv, bus_size_t c)
{
	volatile u_int64_t *p = (u_int64_t *)(h + o);

	while (c--)
		*p++ = vv;
}

void
mbus_cp_1(void *v, bus_space_handle_t h1, bus_size_t o1,
	  bus_space_handle_t h2, bus_size_t o2, bus_size_t c)
{
	volatile u_int8_t *p1 = (u_int8_t *)(h1 + o1);
	volatile u_int8_t *p2 = (u_int8_t *)(h2 + o2);

	while (c--)
		*p1++ = *p2++;
}

void
mbus_cp_2(void *v, bus_space_handle_t h1, bus_size_t o1,
	  bus_space_handle_t h2, bus_size_t o2, bus_size_t c)
{
	volatile u_int16_t *p1 = (u_int16_t *)(h1 + o1);
	volatile u_int16_t *p2 = (u_int16_t *)(h2 + o2);

	while (c--)
		*p1++ = *p2++;
}

void
mbus_cp_4(void *v, bus_space_handle_t h1, bus_size_t o1,
	  bus_space_handle_t h2, bus_size_t o2, bus_size_t c)
{
	volatile u_int32_t *p1 = (u_int32_t *)(h1 + o1);
	volatile u_int32_t *p2 = (u_int32_t *)(h2 + o2);

	while (c--)
		*p1++ = *p2++;
}

void
mbus_cp_8(void *v, bus_space_handle_t h1, bus_size_t o1,
	  bus_space_handle_t h2, bus_size_t o2, bus_size_t c)
{
	volatile u_int64_t *p1 = (u_int64_t *)(h1 + o1);
	volatile u_int64_t *p2 = (u_int64_t *)(h2 + o2);

	while (c--)
		*p1++ = *p2++;
}


/* ugly typecast macro */
#define	crr(n)	((void (*)(void *, bus_space_handle_t, bus_size_t, u_int8_t *, bus_size_t))(n))
#define	cwr(n)	((void (*)(void *, bus_space_handle_t, bus_size_t, const u_int8_t *, bus_size_t))(n))

const struct hppa_bus_space_tag hppa_bustag = {
	NULL,

	mbus_map, mbus_unmap, mbus_subregion, mbus_alloc, mbus_free,
	mbus_barrier, mbus_vaddr,
	mbus_r1,    mbus_r2,   mbus_r4,   mbus_r8,
	mbus_w1,    mbus_w2,   mbus_w4,   mbus_w8,
	mbus_rm_1,  mbus_rm_2, mbus_rm_4, mbus_rm_8,
	mbus_wm_1,  mbus_wm_2, mbus_wm_4, mbus_wm_8,
	mbus_sm_1,  mbus_sm_2, mbus_sm_4, mbus_sm_8,
	/* *_raw_* are the same as non-raw for native busses */
	            crr(mbus_rm_1), crr(mbus_rm_1), crr(mbus_rm_1),
	            cwr(mbus_wm_1), cwr(mbus_wm_1), cwr(mbus_wm_1),
	mbus_rr_1,  mbus_rr_2, mbus_rr_4, mbus_rr_8,
	mbus_wr_1,  mbus_wr_2, mbus_wr_4, mbus_wr_8,
	/* *_raw_* are the same as non-raw for native busses */
	            crr(mbus_rr_1), crr(mbus_rr_1), crr(mbus_rr_1),
	            cwr(mbus_wr_1), cwr(mbus_wr_1), cwr(mbus_wr_1),
	mbus_sr_1,  mbus_sr_2, mbus_sr_4, mbus_sr_8,
	mbus_cp_1,  mbus_cp_2, mbus_cp_4, mbus_cp_8
};

int		 mbus_dmamap_create(void *v, bus_size_t size, int nsegments,
		   bus_size_t maxsegsz, bus_size_t boundary, int flags,
		   bus_dmamap_t *dmamp);
void		 mbus_dmamap_unload(void *v, bus_dmamap_t map);
void		 mbus_dmamap_destroy(void *v, bus_dmamap_t map);
int		 _bus_dmamap_load_buffer(bus_dma_tag_t t, bus_dmamap_t map,
		    void *buf, bus_size_t buflen, struct proc *p, int flags,
		    paddr_t *lastaddrp, int *segp, int first);
int		 mbus_dmamap_load(void *v, bus_dmamap_t map, void *addr,
		    bus_size_t size, struct proc *p, int flags);
int		 mbus_dmamap_load_mbuf(void *v, bus_dmamap_t map,
		    struct mbuf *m0, int flags);
int		 mbus_dmamap_load_uio(void *v, bus_dmamap_t map,
		    struct uio *uio, int flags);
int		 mbus_dmamap_load_raw(void *v, bus_dmamap_t map,
		    bus_dma_segment_t *segs, int nsegs, bus_size_t size,
		    int flags);
void		 mbus_dmamap_sync(void *v, bus_dmamap_t map, bus_addr_t off,
		    bus_size_t len, int ops);
int		 mbus_dmamem_alloc(void *v, bus_size_t size,
		    bus_size_t alignment, bus_size_t boundary,
		    bus_dma_segment_t *segs, int nsegs, int *rsegs, int flags);
void		 mbus_dmamem_free(void *v, bus_dma_segment_t *segs, int nsegs);
int		 mbus_dmamem_map(void *v, bus_dma_segment_t *segs, int nsegs,
		    size_t size, caddr_t *kvap, int flags);
void		 mbus_dmamem_unmap(void *v, caddr_t kva, size_t size);
paddr_t		 mbus_dmamem_mmap(void *v, bus_dma_segment_t *segs, int nsegs,
		    off_t off, int prot, int flags);

int
mbus_dmamap_create(void *v, bus_size_t size, int nsegments,
		   bus_size_t maxsegsz, bus_size_t boundary, int flags,
		   bus_dmamap_t *dmamp)
{
	struct hppa_bus_dmamap *map;
	size_t mapsize;

	mapsize = sizeof(struct hppa_bus_dmamap) +
	    (sizeof(bus_dma_segment_t) * (nsegments - 1));
	map = malloc(mapsize, M_DEVBUF, (flags & BUS_DMA_NOWAIT) ?
	    (M_NOWAIT | M_ZERO) : (M_WAITOK | M_ZERO));
	if (!map)
		return (ENOMEM);

	map->_dm_size = size;
	map->_dm_segcnt = nsegments;
	map->_dm_maxsegsz = maxsegsz;
	map->_dm_boundary = boundary;
	map->_dm_flags = flags & ~(BUS_DMA_WAITOK|BUS_DMA_NOWAIT);
	map->dm_mapsize = 0;
	map->dm_nsegs = 0;

	*dmamp = map;
	return (0);
}

void
mbus_dmamap_unload(void *v, bus_dmamap_t map)
{
	map->dm_mapsize = 0;
	map->dm_nsegs = 0;
}

void
mbus_dmamap_destroy(void *v, bus_dmamap_t map)
{
	if (map->dm_mapsize != 0)
		mbus_dmamap_unload(v, map);

	free(map, M_DEVBUF, 0);
}

/*
 * Utility function to load a linear buffer.  lastaddrp holds state
 * between invocations (for multiple-buffer loads).  segp contains
 * the starting segment on entrance, and the ending segment on exit.
 * first indicates if this is the first invocation of this function.
 */
int
_bus_dmamap_load_buffer(bus_dma_tag_t t, bus_dmamap_t map, void *buf,
    bus_size_t buflen, struct proc *p, int flags, paddr_t *lastaddrp,
    int *segp, int first)
{
	bus_size_t sgsize;
	bus_addr_t curaddr, lastaddr, baddr, bmask;
	vaddr_t vaddr = (vaddr_t)buf;
	int seg;
	pmap_t pmap;

	pmap = p? p->p_vmspace->vm_map.pmap : pmap_kernel();
	lastaddr = *lastaddrp;
	bmask  = ~(map->_dm_boundary - 1);

	for (seg = *segp; buflen > 0 ; ) {
		/*
		 * Get the physical address for this segment.
		 */
		pmap_extract(pmap, vaddr, (paddr_t *)&curaddr);

		/*
		 * Compute the segment size, and adjust counts.
		 */
		sgsize = PAGE_SIZE - ((u_long)vaddr & PGOFSET);
		if (buflen < sgsize)
			sgsize = buflen;

		/*
		 * Make sure we don't cross any boundaries.
		 */
		if (map->_dm_boundary > 0) {
			baddr = (curaddr + map->_dm_boundary) & bmask;
			if (sgsize > (baddr - curaddr))
				sgsize = (baddr - curaddr);
		}

		/*
		 * Insert chunk into a segment, coalescing with
		 * previous segment if possible.
		 */
		if (first) {
			map->dm_segs[seg].ds_addr = curaddr;
			map->dm_segs[seg].ds_len = sgsize;
			map->dm_segs[seg]._ds_va = vaddr;
			first = 0;
		} else {
			if (curaddr == lastaddr &&
			    (map->dm_segs[seg].ds_len + sgsize) <=
			     map->_dm_maxsegsz &&
			    (map->_dm_boundary == 0 ||
			     (map->dm_segs[seg].ds_addr & bmask) ==
			     (curaddr & bmask)))
				map->dm_segs[seg].ds_len += sgsize;
			else {
				if (++seg >= map->_dm_segcnt)
					break;
				map->dm_segs[seg].ds_addr = curaddr;
				map->dm_segs[seg].ds_len = sgsize;
				map->dm_segs[seg]._ds_va = vaddr;
			}
		}

		lastaddr = curaddr + sgsize;
		vaddr += sgsize;
		buflen -= sgsize;
	}

	*segp = seg;
	*lastaddrp = lastaddr;

	/*
	 * Did we fit?
	 */
	if (buflen != 0)
		return (EFBIG);		/* XXX better return value here? */
	return (0);
}

int
mbus_dmamap_load(void *v, bus_dmamap_t map, void *addr, bus_size_t size,
		 struct proc *p, int flags)
{
	paddr_t lastaddr;
	int seg, error;

	/*
	 * Make sure that on error condition we return "no valid mappings".
	 */
	map->dm_nsegs = 0;
	map->dm_mapsize = 0;

	if (size > map->_dm_size)
		return (EINVAL);

	seg = 0;
	lastaddr = 0;
	error = _bus_dmamap_load_buffer(NULL, map, addr, size, p, flags,
	    &lastaddr, &seg, 1);
	if (error == 0) {
		map->dm_mapsize = size;
		map->dm_nsegs = seg + 1;
	}

	return (0);
}

int
mbus_dmamap_load_mbuf(void *v, bus_dmamap_t map, struct mbuf *m0, int flags)
{
	paddr_t lastaddr;
	int seg, error, first;
	struct mbuf *m;

	map->dm_mapsize = 0;
	map->dm_nsegs = 0;

#ifdef DIAGNOSTIC
	if ((m0->m_flags & M_PKTHDR) == 0)
		panic("_bus_dmamap_load_mbuf: no packet header");
#endif  

	if (m0->m_pkthdr.len > map->_dm_size)
		return (EINVAL);

	first = 1;
	seg = 0;
	error = 0;
	lastaddr = 0;
	for (m = m0; m != NULL && error == 0; m = m->m_next) {
		if (m->m_len == 0)
			continue;
		error = _bus_dmamap_load_buffer(NULL, map, m->m_data, m->m_len,
		    NULL, flags, &lastaddr, &seg, first);
		first = 0;
	}
	if (error == 0) {
		map->dm_mapsize = m0->m_pkthdr.len;
		map->dm_nsegs = seg + 1;
	}

	return (error);
}

int
mbus_dmamap_load_uio(void *v, bus_dmamap_t map, struct uio *uio, int flags)
{
	paddr_t lastaddr;
	int seg, i, error, first;
	bus_size_t minlen, resid;
	struct proc *p = NULL;
	struct iovec *iov;
	caddr_t addr;

	/*
	 * Make sure that on error condition we return "no valid mappings".
	 */
	map->dm_mapsize = 0;
	map->dm_nsegs = 0;

	resid = uio->uio_resid;
	iov = uio->uio_iov;

	if (resid > map->_dm_size)
		return (EINVAL);

	if (uio->uio_segflg == UIO_USERSPACE) {
		p = uio->uio_procp;
#ifdef DIAGNOSTIC
		if (p == NULL)
			panic("_bus_dmamap_load_uio: USERSPACE but no proc");
#endif
	}

	first = 1;
	seg = 0;
	error = 0;
	lastaddr = 0;
	for (i = 0; i < uio->uio_iovcnt && resid != 0 && error == 0; i++) {
		/*
		 * Now at the first iovec to load.  Load each iovec
		 * until we have exhausted the residual count.
		 */
		minlen = resid < iov[i].iov_len ? resid : iov[i].iov_len;
		addr = (caddr_t)iov[i].iov_base;

		error = _bus_dmamap_load_buffer(NULL, map, addr, minlen,
		    p, flags, &lastaddr, &seg, first);
		first = 0;

		resid -= minlen;
	}
	if (error == 0) {
		map->dm_mapsize = uio->uio_resid;
		map->dm_nsegs = seg + 1;
	}
	return (error);
}

int
mbus_dmamap_load_raw(void *v, bus_dmamap_t map, bus_dma_segment_t *segs,
    int nsegs, bus_size_t size, int flags)
{
	if (nsegs > map->_dm_segcnt || size > map->_dm_size)
		return (EINVAL);

	/*
	 * Make sure we don't cross any boundaries.
	 */
	if (map->_dm_boundary) {
		bus_addr_t bmask = ~(map->_dm_boundary - 1);
		int i;

		for (i = 0; i < nsegs; i++) {
			if (segs[i].ds_len > map->_dm_maxsegsz)
				return (EINVAL);
			if ((segs[i].ds_addr & bmask) !=
			    ((segs[i].ds_addr + segs[i].ds_len - 1) & bmask))
				return (EINVAL);
		}
	}

	bcopy(segs, map->dm_segs, nsegs * sizeof(*segs));
	map->dm_nsegs = nsegs;
	map->dm_mapsize = size;
	return (0);
}

void
mbus_dmamap_sync(void *v, bus_dmamap_t map, bus_addr_t off, bus_size_t len,
    int ops)
{
	bus_dma_segment_t *ps = map->dm_segs,
	    *es = &map->dm_segs[map->dm_nsegs];

	if (off >= map->_dm_size)
		return;

	if ((off + len) > map->_dm_size)
		len = map->_dm_size - off;

	for (; len && ps < es; ps++)
		if (off > ps->ds_len)
			off -= ps->ds_len;
		else {
			bus_size_t l = ps->ds_len - off;
			if (l > len)
				l = len;
			fdcache(HPPA_SID_KERNEL, ps->_ds_va + off, l);
			len -= l;
			off = 0;
		}

	/* for either operation sync the shit away */
	__asm volatile ("sync\n\tsyncdma\n\tsync\n\t"
	    "nop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop");
}

int
mbus_dmamem_alloc(void *v, bus_size_t size, bus_size_t alignment,
		  bus_size_t boundary, bus_dma_segment_t *segs, int nsegs,
		  int *rsegs, int flags)
{
	struct pglist pglist;
	struct vm_page *pg;
	int plaflag;

	size = round_page(size);

	plaflag = flags & BUS_DMA_NOWAIT ? UVM_PLA_NOWAIT : UVM_PLA_WAITOK;
	if (flags & BUS_DMA_ZERO)
		plaflag |= UVM_PLA_ZERO;

	TAILQ_INIT(&pglist);
	if (uvm_pglistalloc(size, 0, -1, alignment, boundary,
	    &pglist, 1, plaflag))
		return (ENOMEM);

	pg = TAILQ_FIRST(&pglist);
	segs[0]._ds_va = segs[0].ds_addr = VM_PAGE_TO_PHYS(pg);
	segs[0].ds_len = size;
	*rsegs = 1;

	for(; pg; pg = TAILQ_NEXT(pg, pageq))
		/* XXX for now */
		pmap_changebit(pg, PTE_PROT(TLB_UNCACHABLE), 0);
	pmap_update(pmap_kernel());

	return (0);
}

void
mbus_dmamem_free(void *v, bus_dma_segment_t *segs, int nsegs)
{
	struct pglist pglist;
	paddr_t pa, epa;

	TAILQ_INIT(&pglist);
	for(; nsegs--; segs++)
		for (pa = segs->ds_addr, epa = pa + segs->ds_len;
		     pa < epa; pa += PAGE_SIZE) {
			struct vm_page *pg = PHYS_TO_VM_PAGE(pa);
			if (!pg)
				panic("mbus_dmamem_free: no page for pa");
			TAILQ_INSERT_TAIL(&pglist, pg, pageq);
			pdcache(HPPA_SID_KERNEL, pa, PAGE_SIZE);
			pdtlb(HPPA_SID_KERNEL, pa);
			pitlb(HPPA_SID_KERNEL, pa);
		}
	uvm_pglistfree(&pglist);
}

int
mbus_dmamem_map(void *v, bus_dma_segment_t *segs, int nsegs, size_t size,
		caddr_t *kvap, int flags)
{
	*kvap = (caddr_t)segs[0].ds_addr;
	return 0;
}

void
mbus_dmamem_unmap(void *v, caddr_t kva, size_t size)
{
}

paddr_t
mbus_dmamem_mmap(void *v, bus_dma_segment_t *segs, int nsegs, off_t off,
		 int prot, int flags)
{
	panic("_dmamem_mmap: not implemented");
}

const struct hppa_bus_dma_tag hppa_dmatag = {
	NULL,
	mbus_dmamap_create, mbus_dmamap_destroy,
	mbus_dmamap_load, mbus_dmamap_load_mbuf,
	mbus_dmamap_load_uio, mbus_dmamap_load_raw,
	mbus_dmamap_unload, mbus_dmamap_sync,

	mbus_dmamem_alloc, mbus_dmamem_free, mbus_dmamem_map,
	mbus_dmamem_unmap, mbus_dmamem_mmap
};

int
mbmatch(struct device *parent, void *cfdata, void *aux)
{
	struct cfdata *cf = cfdata;

	/* there will be only one */
	if (cf->cf_unit)
		return 0;

	return 1;
}

void
mbattach(struct device *parent, struct device *self, void *aux)
{
	struct mainbus_softc *sc = (struct mainbus_softc *)self;
	struct confargs nca;
	bus_space_handle_t ioh;

	/* fetch the "default" cpu hpa */
	if (pdc_call((iodcio_t)pdc, 0, PDC_HPA, PDC_HPA_DFLT, &pdc_hpa) < 0)
		panic("mbattach: PDC_HPA failed");

	printf(" [flex %x]\n", pdc_hpa.hpa & HPPA_FLEX_MASK);

	/* map all the way till the end of the memory */
	if (bus_space_map(&hppa_bustag, pdc_hpa.hpa,
	    (~0LU - pdc_hpa.hpa + 1), 0, &ioh))
		panic("mbattach: cannot map mainbus IO space");

	/*
	 * Local-Broadcast the HPA to all modules on this bus
	 */
	((struct iomod *)HPPA_LBCAST)->io_flex =
	    (pdc_hpa.hpa & HPPA_FLEX_MASK) | DMA_ENABLE;

	sc->sc_hpa = pdc_hpa.hpa;

	/* PDC first */
	bzero(&nca, sizeof(nca));
	nca.ca_name = "pdc";
	nca.ca_iot = &hppa_bustag;
	nca.ca_dmatag = &hppa_dmatag;
	config_found(self, &nca, mbprint);

#if NPOWER > 0
	/* get some power */
	bzero(&nca, sizeof(nca));
	nca.ca_name = "power";
	nca.ca_irq = -1;
	if (!pdc_call((iodcio_t)pdc, 0, PDC_SOFT_POWER,
	    PDC_SOFT_POWER_INFO, &pdc_power_info, 0)) {
		nca.ca_iot = &hppa_bustag;
		nca.ca_hpa = pdc_power_info.addr;
		nca.ca_hpamask = HPPA_IOBEGIN;
	}
	config_found(self, &nca, mbprint);
#endif

#if NLCD > 0
	if (!pdc_call((iodcio_t)pdc, 0, PDC_CHASSIS, PDC_CHASSIS_INFO,
	    &pdc_chassis_info, &pdc_chassis_lcd, sizeof(pdc_chassis_lcd)) &&
	    pdc_chassis_lcd.enabled) {
		bzero(&nca, sizeof(nca));
		nca.ca_name = "lcd";
		nca.ca_irq = -1;
		nca.ca_iot = &hppa_bustag;
		nca.ca_hpa = pdc_chassis_lcd.cmd_addr;
		nca.ca_hpamask = HPPA_IOBEGIN;
		nca.ca_pdc_iodc_read = (void *)&pdc_chassis_lcd;

		config_found(self, &nca, mbprint);
	}
#endif

	bzero(&nca, sizeof(nca));
	nca.ca_hpa = 0;
	nca.ca_irq = -1;
	nca.ca_hpamask = HPPA_IOBEGIN;
	nca.ca_iot = &hppa_bustag;
	nca.ca_dmatag = &hppa_dmatag;
	nca.ca_dp.dp_bc[0] = nca.ca_dp.dp_bc[1] = nca.ca_dp.dp_bc[2] =
	nca.ca_dp.dp_bc[3] = nca.ca_dp.dp_bc[4] = nca.ca_dp.dp_bc[5] = -1;
	nca.ca_dp.dp_mod = -1;
	switch (cpu_hvers) {
	case HPPA_BOARD_HP809:
	case HPPA_BOARD_HP819:
	case HPPA_BOARD_HP829:
	case HPPA_BOARD_HP839:
	case HPPA_BOARD_HP849:
	case HPPA_BOARD_HP859:
	case HPPA_BOARD_HP869:
#if 0
	case HPPA_BOARD_HP770_J200:
	case HPPA_BOARD_HP770_J210:
	case HPPA_BOARD_HP770_J210XC:
	case HPPA_BOARD_HP780_J282:
	case HPPA_BOARD_HP782_J2240:
#endif
	case HPPA_BOARD_HP780_C160:
	case HPPA_BOARD_HP780_C180P:
	case HPPA_BOARD_HP780_C180XP:
	case HPPA_BOARD_HP780_C200:
	case HPPA_BOARD_HP780_C230:
	case HPPA_BOARD_HP780_C240:
	case HPPA_BOARD_HP785_C360:
		/* Attach CPUs first, then everything else... */
		ncpusfound = 0;
		pdc_scanbus(self, &nca, MAXMODBUS, HPPA_FPA, 1);
		pdc_scanbus(self, &nca, MAXMODBUS, HPPA_FPA, 0);
	break;
	default:
		/* Attach CPUs first, then everything else... */
		ncpusfound = 0;
		pdc_scanbus(self, &nca, MAXMODBUS, 0, 1);
		pdc_scanbus(self, &nca, MAXMODBUS, 0, 0);
	}
}

/*
 * Retrieve CPU #N HPA value
 */
hppa_hpa_t
cpu_gethpa(int n)
{
	struct mainbus_softc *sc;

	sc = mainbus_cd.cd_devs[0];

	return sc->sc_hpa;
}

int
mbprint(void *aux, const char *pnp)
{
	struct confargs *ca = aux;

	if (pnp)
		printf("\"%s\" at %s (type %x sv %x mod %x hv %x)",
		    ca->ca_name, pnp,
		    ca->ca_type.iodc_type, ca->ca_type.iodc_sv_model,
		    ca->ca_type.iodc_model, ca->ca_type.iodc_revision);
	if (ca->ca_hpa) {
		if (~ca->ca_hpamask)
			printf(" offset %lx", ca->ca_hpa & ~ca->ca_hpamask);
		if (!pnp && ca->ca_irq >= 0)
			printf(" irq %d", ca->ca_irq);
	}

	return (UNCONF);
}

int
mbsubmatch(struct device *parent, void *match, void *aux)
{
	struct cfdata *cf = match;
	struct confargs *ca = aux;
	int ret;

	if (autoconf_verbose)
		printf(">> hpa %lx off %lx cf_off %lx\n",
		    ca->ca_hpa, ca->ca_hpa & ~ca->ca_hpamask, cf->hppacf_off);

	if (ca->ca_hpa && ~ca->ca_hpamask && cf->hppacf_off != -1 &&
	    ((ca->ca_hpa & ~ca->ca_hpamask) != cf->hppacf_off))
		return (0);

	if ((ret = (*cf->cf_attach->ca_match)(parent, match, aux)))
		ca->ca_irq = cf->hppacf_irq;

	return ret;
}
