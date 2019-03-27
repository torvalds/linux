/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Brian Fundakowski Feldman
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/memrange.h>

#include <machine/cputypes.h>
#include <machine/md_var.h>
#include <machine/specialreg.h>

/*
 * A K6-2 MTRR is defined as the highest 15 bits having the address, the next
 * 15 having the mask, the 1st bit being "write-combining" and the 0th bit
 * being "uncacheable".
 *
 *	Address		    Mask	WC  UC
 * | XXXXXXXXXXXXXXX | XXXXXXXXXXXXXXX | X | X |
 *
 * There are two of these in the 64-bit UWCCR.
 */

#define	UWCCR 0xc0000085

#define	K6_REG_GET(reg, addr, mask, wc, uc) do {			\
		addr = (reg) & 0xfffe0000;				\
		mask = ((reg) & 0x1fffc) >> 2;				\
		wc = ((reg) & 0x2) >> 1;				\
		uc = (reg) & 0x1;					\
	} while (0)

#define	K6_REG_MAKE(addr, mask, wc, uc) 				\
	((addr) | ((mask) << 2) | ((wc) << 1) | uc)

static void	k6_mrinit(struct mem_range_softc *sc);
static int	k6_mrset(struct mem_range_softc *, struct mem_range_desc *,
		    int *);
static __inline int k6_mrmake(struct mem_range_desc *, u_int32_t *);
static void	k6_mem_drvinit(void *);

static struct mem_range_ops k6_mrops =
{
	k6_mrinit,
	k6_mrset,
	NULL,
	NULL
};

static __inline int
k6_mrmake(struct mem_range_desc *desc, u_int32_t *mtrr)
{
	u_int32_t len = 0, wc, uc;
	int bit;

	if (desc->mr_base &~ 0xfffe0000)
		return (EINVAL);
	if (desc->mr_len < 131072 || !powerof2(desc->mr_len))
		return (EINVAL);
	if (desc->mr_flags &~ (MDF_WRITECOMBINE|MDF_UNCACHEABLE|MDF_FORCE))
		return (EOPNOTSUPP);

	for (bit = ffs(desc->mr_len >> 17) - 1; bit < 15; bit++)
		len |= 1 << bit; 
	wc = (desc->mr_flags & MDF_WRITECOMBINE) ? 1 : 0;
	uc = (desc->mr_flags & MDF_UNCACHEABLE) ? 1 : 0;

	*mtrr = K6_REG_MAKE(desc->mr_base, len, wc, uc);
	return (0);
}

static void
k6_mrinit(struct mem_range_softc *sc)
{
	u_int64_t reg;
	u_int32_t addr, mask, wc, uc;
	int d;

	sc->mr_cap = 0;
	sc->mr_ndesc = 2; /* XXX (BFF) For now, we only have one msr for this */
	sc->mr_desc = malloc(sc->mr_ndesc * sizeof(struct mem_range_desc),
	    M_MEMDESC, M_NOWAIT | M_ZERO);
	if (sc->mr_desc == NULL)
		panic("k6_mrinit: malloc returns NULL");

	reg = rdmsr(UWCCR);
	for (d = 0; d < sc->mr_ndesc; d++) {
		u_int32_t one = (reg & (0xffffffff << (32 * d))) >> (32 * d);

		K6_REG_GET(one, addr, mask, wc, uc);
		sc->mr_desc[d].mr_base = addr;
		sc->mr_desc[d].mr_len = ffs(mask) << 17;
		if (wc)
			sc->mr_desc[d].mr_flags |= MDF_WRITECOMBINE;
		if (uc)
			sc->mr_desc[d].mr_flags |= MDF_UNCACHEABLE;
	}

	printf("K6-family MTRR support enabled (%d registers)\n", sc->mr_ndesc);
}

static int
k6_mrset(struct mem_range_softc *sc, struct mem_range_desc *desc, int *arg)
{
	u_int64_t reg;
	u_int32_t mtrr;
	int error, d;

	switch (*arg) {
	case MEMRANGE_SET_UPDATE:
		error = k6_mrmake(desc, &mtrr);
		if (error)
			return (error);
		for (d = 0; d < sc->mr_ndesc; d++) {
			if (!sc->mr_desc[d].mr_len) {
				sc->mr_desc[d] = *desc;
				goto out;
			}
			if (sc->mr_desc[d].mr_base == desc->mr_base &&
			    sc->mr_desc[d].mr_len == desc->mr_len)
				return (EEXIST);
		}
		return (ENOSPC);
	case MEMRANGE_SET_REMOVE:
		mtrr = 0;
		for (d = 0; d < sc->mr_ndesc; d++)
			if (sc->mr_desc[d].mr_base == desc->mr_base &&
			    sc->mr_desc[d].mr_len == desc->mr_len) {
				bzero(&sc->mr_desc[d], sizeof(sc->mr_desc[d]));
				goto out;
			}
		return (ENOENT);
	default:
		return (EOPNOTSUPP);
	}
out:	
	disable_intr();
	wbinvd();
	reg = rdmsr(UWCCR);
	reg &= ~(0xffffffff << (32 * d));
	reg |= mtrr << (32 * d);
	wrmsr(UWCCR, reg);
	wbinvd();
	enable_intr();

	return (0);
}

static void
k6_mem_drvinit(void *unused)
{

	if (cpu_vendor_id != CPU_VENDOR_AMD)
		return;
	if ((cpu_id & 0xf00) != 0x500)
		return;
	if ((cpu_id & 0xf0) < 0x80 ||
	    ((cpu_id & 0xf0) == 0x80 && (cpu_id & 0xf) <= 0x7))
		return;
	mem_range_softc.mr_op = &k6_mrops;
}
SYSINIT(k6memdev, SI_SUB_DRIVERS, SI_ORDER_FIRST, k6_mem_drvinit, NULL);
