/* $OpenBSD: amd64_mem.c,v 1.15 2025/07/16 07:15:41 jsg Exp $ */
/*
 * Copyright (c) 1999 Michael Smith <msmith@freebsd.org>
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
 *
 * $FreeBSD: src/sys/i386/i386/i686_mem.c,v 1.8 1999/10/12 22:53:05 green Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/memrange.h>

#include <machine/cpufunc.h>
#include <machine/intr.h>
#include <machine/specialreg.h>

/*
 * This code implements a set of MSRs known as MTRR which define caching
 * modes/behavior for various memory ranges.
 */

char *mem_owner_bios = "BIOS";

#define MR_FIXMTRR	(1<<0)

#define mrwithin(mr, a) \
    (((a) >= (mr)->mr_base) && ((a) < ((mr)->mr_base + (mr)->mr_len)))
#define mroverlap(mra, mrb) \
    (mrwithin(mra, mrb->mr_base) || mrwithin(mrb, mra->mr_base))

#define mrvalid(base, len) 						\
    ((!(base & ((1 << 12) - 1))) && 	/* base is multiple of 4k */	\
     ((len) >= (1 << 12)) && 		/* length is >= 4k */		\
     powerof2((len)) && 		/* ... and power of two */	\
     !((base) & ((len) - 1)))		/* range is not discontinuous */

#define mrcopyflags(curr, new) (((curr) & ~MDF_ATTRMASK) | \
	((new) & MDF_ATTRMASK))

#define FIXTOP	((MTRR_N64K * 0x10000) + (MTRR_N16K * 0x4000) + \
	(MTRR_N4K * 0x1000))

void	mrinit(struct mem_range_softc *sc);
int	mrset(struct mem_range_softc *sc,
	    struct mem_range_desc *mrd, int *arg);
void	mrinit_cpu(struct mem_range_softc *sc);
void	mrreload_cpu(struct mem_range_softc *sc);

struct mem_range_ops mrops = {
	mrinit,
	mrset,
	mrinit_cpu,
	mrreload_cpu
};

u_int64_t	mtrrcap, mtrrdef;
u_int64_t	mtrrmask = 0x0000000ffffff000ULL;

struct mem_range_desc	*mem_range_match(struct mem_range_softc *sc,
			     struct mem_range_desc *mrd);
void			 mrfetch(struct mem_range_softc *sc);
int			 mtrrtype(u_int64_t flags);
int			 mrt2mtrr(u_int64_t flags);
int			 mtrr2mrt(int val);
int			 mtrrconflict(u_int64_t flag1, u_int64_t flag2);
void			 mrstore(struct mem_range_softc *sc);
void			 mrstoreone(struct mem_range_softc *sc);
struct mem_range_desc	*mtrrfixsearch(struct mem_range_softc *sc,
			     u_int64_t addr);
int			 mrsetlow(struct mem_range_softc *sc,
			     struct mem_range_desc *mrd, int *arg);
int			 mrsetvariable(struct mem_range_softc *sc,
			     struct mem_range_desc *mrd, int *arg);

/* MTRR type to memory range type conversion */
int mtrrtomrt[] = {
	MDF_UNCACHEABLE,
	MDF_WRITECOMBINE,
	MDF_UNKNOWN,
	MDF_UNKNOWN,
	MDF_WRITETHROUGH,
	MDF_WRITEPROTECT,
	MDF_WRITEBACK
};

int
mtrr2mrt(int val)
{
	if (val < 0 || val >= nitems(mtrrtomrt))
		return MDF_UNKNOWN;
	return mtrrtomrt[val];
}

/*
 * MTRR conflicts. Writeback and uncachable may overlap.
 */
int
mtrrconflict(u_int64_t flag1, u_int64_t flag2)
{
	flag1 &= MDF_ATTRMASK;
	flag2 &= MDF_ATTRMASK;
	if (flag1 == flag2 ||
	    (flag1 == MDF_WRITEBACK && flag2 == MDF_UNCACHEABLE) ||
	    (flag2 == MDF_WRITEBACK && flag1 == MDF_UNCACHEABLE))
		return 0;
	return 1;
}

/*
 * Look for an exactly-matching range.
 */
struct mem_range_desc *
mem_range_match(struct mem_range_softc *sc, struct mem_range_desc *mrd)
{
	struct mem_range_desc	*cand;
	int			 i;
	
	for (i = 0, cand = sc->mr_desc; i < sc->mr_ndesc; i++, cand++)
		if ((cand->mr_base == mrd->mr_base) &&
		    (cand->mr_len == mrd->mr_len))
			return(cand);
	return(NULL);
}

/*
 * Fetch the current mtrr settings from the current CPU (assumed to all
 * be in sync in the SMP case).  Note that if we are here, we assume
 * that MTRRs are enabled, and we may or may not have fixed MTRRs.
 */
void
mrfetch(struct mem_range_softc *sc)
{
	struct mem_range_desc	*mrd;
	u_int64_t		 msrv;
	int			 i, j, msr, mrt;

	mrd = sc->mr_desc;

	/* We should never be fetching MTRRs from an AP */
	KASSERT(CPU_IS_PRIMARY(curcpu()));
	
	/* Get fixed-range MTRRs, if the CPU supports them */
	if (sc->mr_cap & MR_FIXMTRR) {
		msr = MSR_MTRRfix64K_00000;
		for (i = 0; i < (MTRR_N64K / 8); i++, msr++) {
			msrv = rdmsr(msr);
			for (j = 0; j < 8; j++, mrd++) {
				mrt = mtrr2mrt(msrv & 0xff);
				if (mrt == MDF_UNKNOWN)
					mrt = MDF_UNCACHEABLE;
				mrd->mr_flags = (mrd->mr_flags & ~MDF_ATTRMASK) |
					mrt | MDF_ACTIVE;
				if (mrd->mr_owner[0] == 0)
					strlcpy(mrd->mr_owner, mem_owner_bios,
					    sizeof(mrd->mr_owner));
				msrv = msrv >> 8;
			}
		}

		msr = MSR_MTRRfix16K_80000;
		for (i = 0; i < (MTRR_N16K / 8); i++, msr++) {
			msrv = rdmsr(msr);
			for (j = 0; j < 8; j++, mrd++) {
				mrt = mtrr2mrt(msrv & 0xff);
				if (mrt == MDF_UNKNOWN)
					mrt = MDF_UNCACHEABLE;
				mrd->mr_flags = (mrd->mr_flags & ~MDF_ATTRMASK) |
					mrt | MDF_ACTIVE;
				if (mrd->mr_owner[0] == 0)
					strlcpy(mrd->mr_owner, mem_owner_bios,
					    sizeof(mrd->mr_owner));
				msrv = msrv >> 8;
			}
		}

		msr = MSR_MTRRfix4K_C0000;
		for (i = 0; i < (MTRR_N4K / 8); i++, msr++) {
			msrv = rdmsr(msr);
			for (j = 0; j < 8; j++, mrd++) {
				mrt = mtrr2mrt(msrv & 0xff);
				if (mrt == MDF_UNKNOWN)
					mrt = MDF_UNCACHEABLE;
				mrd->mr_flags = (mrd->mr_flags & ~MDF_ATTRMASK) |
					mrt | MDF_ACTIVE;
				if (mrd->mr_owner[0] == 0)
					strlcpy(mrd->mr_owner, mem_owner_bios,
					    sizeof(mrd->mr_owner));
				msrv = msrv >> 8;
			}
		}
	}

	/* Get remainder which must be variable MTRRs */
	msr = MSR_MTRRvarBase;
	for (; (mrd - sc->mr_desc) < sc->mr_ndesc; msr += 2, mrd++) {
		msrv = rdmsr(msr);
		mrt = mtrr2mrt(msrv & 0xff);
		if (mrt == MDF_UNKNOWN)
			mrt = MDF_UNCACHEABLE;
		mrd->mr_flags = (mrd->mr_flags & ~MDF_ATTRMASK) | mrt;
		mrd->mr_base = msrv & mtrrmask;
		msrv = rdmsr(msr + 1);
		mrd->mr_flags = (msrv & 0x800) ?
			(mrd->mr_flags | MDF_ACTIVE) :
			(mrd->mr_flags & ~MDF_ACTIVE);
		/* Compute the range from the mask. Ick. */
		mrd->mr_len = (~(msrv & mtrrmask) & mtrrmask) + 0x1000;
		if (!mrvalid(mrd->mr_base, mrd->mr_len))
			mrd->mr_flags |= MDF_BOGUS;
		/* If unclaimed and active, must be the BIOS */
		if ((mrd->mr_flags & MDF_ACTIVE) && (mrd->mr_owner[0] == 0))
			strlcpy(mrd->mr_owner, mem_owner_bios,
			    sizeof(mrd->mr_owner));
	}
}

/*
 * Return the MTRR memory type matching a region's flags
 */
int
mtrrtype(u_int64_t flags)
{
	int i;
	
	flags &= MDF_ATTRMASK;
	
	for (i = 0; i < nitems(mtrrtomrt); i++) {
		if (mtrrtomrt[i] == MDF_UNKNOWN)
			continue;
		if (flags == mtrrtomrt[i])
			return(i);
	}
	return MDF_UNCACHEABLE;
}

int
mrt2mtrr(u_int64_t flags)
{
	int val;

	val = mtrrtype(flags);

	return val & 0xff;
}

/*
 * Update running CPU(s) MTRRs to match the ranges in the descriptor
 * list.
 *
 * XXX Must be called with interrupts enabled.
 */
void
mrstore(struct mem_range_softc *sc)
{
	u_long s;

	s = intr_disable();
#ifdef MULTIPROCESSOR
	x86_broadcast_ipi(X86_IPI_MTRR);
#endif
	mrstoreone(sc);
	intr_restore(s);
}

/*
 * Update the current CPU's MTRRs with those represented in the
 * descriptor list.  Note that we do this wholesale rather than
 * just stuffing one entry; this is simpler (but slower, of course).
 */
void
mrstoreone(struct mem_range_softc *sc)
{
	struct mem_range_desc	*mrd;
	u_int64_t		 msrv;
	int			 i, j, msr;
	u_int			 cr4save;
	
	mrd = sc->mr_desc;
	
	cr4save = rcr4();	/* save cr4 */
	if (cr4save & CR4_PGE)
		lcr4(cr4save & ~CR4_PGE);

	/* Flush caches, then disable caches, then disable MTRRs */
	wbinvd();
	lcr0((rcr0() & ~CR0_NW) | CR0_CD);
	wrmsr(MSR_MTRRdefType, rdmsr(MSR_MTRRdefType) & ~0x800);
	
	/* Set fixed-range MTRRs */
	if (sc->mr_cap & MR_FIXMTRR) {
		msr = MSR_MTRRfix64K_00000;
		for (i = 0; i < (MTRR_N64K / 8); i++, msr++) {
			msrv = 0;
			for (j = 7; j >= 0; j--) {
				msrv = msrv << 8;
				msrv |= mrt2mtrr((mrd + j)->mr_flags);
			}
			wrmsr(msr, msrv);
			mrd += 8;
		}

		msr = MSR_MTRRfix16K_80000;
		for (i = 0, msrv = 0; i < (MTRR_N16K / 8); i++, msr++) {
			for (j = 7; j >= 0; j--) {
				msrv = msrv << 8;
				msrv |= mrt2mtrr((mrd + j)->mr_flags);
			}
			wrmsr(msr, msrv);
			mrd += 8;
		}

		msr = MSR_MTRRfix4K_C0000;
		for (i = 0, msrv = 0; i < (MTRR_N4K / 8); i++, msr++) {
			for (j = 7; j >= 0; j--) {
				msrv = msrv << 8;
				msrv |= mrt2mtrr((mrd + j)->mr_flags);
			}
			wrmsr(msr, msrv);
			mrd += 8;
		}
	}
	
	/* Set remainder which must be variable MTRRs */
	msr = MSR_MTRRvarBase;
	for (; (mrd - sc->mr_desc) < sc->mr_ndesc; msr += 2, mrd++) {
		if (mrd->mr_flags & MDF_ACTIVE) {
			msrv = mrd->mr_base & mtrrmask;
			msrv |= mrt2mtrr(mrd->mr_flags);
		} else
			msrv = 0;

		wrmsr(msr, msrv);	
		
		/* mask/active register */
		if (mrd->mr_flags & MDF_ACTIVE) {
			msrv = 0x800 | (~(mrd->mr_len - 1) & mtrrmask);
		} else
			msrv = 0;

		wrmsr(msr + 1, msrv);
	}

	/* Re-enable caches and MTRRs */
	wrmsr(MSR_MTRRdefType, mtrrdef | 0x800);
	lcr0(rcr0() & ~(CR0_CD | CR0_NW));
	lcr4(cr4save);
}

/*
 * Hunt for the fixed MTRR referencing (addr)
 */
struct mem_range_desc *
mtrrfixsearch(struct mem_range_softc *sc, u_int64_t addr)
{
	struct mem_range_desc *mrd;
	int			i;
	
	for (i = 0, mrd = sc->mr_desc; i < (MTRR_N64K + MTRR_N16K + MTRR_N4K); i++, mrd++)
		if ((addr >= mrd->mr_base) && (addr < (mrd->mr_base + mrd->mr_len)))
			return(mrd);
	return(NULL);
}

/*
 * Try to satisfy the given range request by manipulating the fixed MTRRs that
 * cover low memory.
 *
 * Note that we try to be generous here; we'll bloat the range out to the
 * next higher/lower boundary to avoid the consumer having to know too much
 * about the mechanisms here.
 */
int
mrsetlow(struct mem_range_softc *sc, struct mem_range_desc *mrd, int *arg)
{
	struct mem_range_desc	*first_md, *last_md, *curr_md;

	/* range check */
	if (((first_md = mtrrfixsearch(sc, mrd->mr_base)) == NULL) ||
	    ((last_md = mtrrfixsearch(sc, mrd->mr_base + mrd->mr_len - 1)) == NULL))
		return(EINVAL);
	
	/* check we aren't doing something risky */
	if (!(mrd->mr_flags & MDF_FORCE))
		for (curr_md = first_md; curr_md <= last_md; curr_md++) {
			if ((curr_md->mr_flags & MDF_ATTRMASK) == MDF_UNKNOWN)
				return (EACCES);
		}

	/* set flags, clear set-by-firmware flag */
	for (curr_md = first_md; curr_md <= last_md; curr_md++) {
		curr_md->mr_flags = mrcopyflags(curr_md->mr_flags & ~MDF_FIRMWARE, mrd->mr_flags);
		memcpy(curr_md->mr_owner, mrd->mr_owner, sizeof(mrd->mr_owner));
	}
	
	return(0);
}


/*
 * Modify/add a variable MTRR to satisfy the request.
 */
int
mrsetvariable(struct mem_range_softc *sc, struct mem_range_desc *mrd, int *arg)
{
	struct mem_range_desc	*curr_md, *free_md;
	int			 i;

	/*
	 * Scan the currently active variable descriptors, look for
	 * one we exactly match (straight takeover) and for possible
	 * accidental overlaps.
	 * Keep track of the first empty variable descriptor in case we
	 * can't perform a takeover.
	 */
	i = (sc->mr_cap & MR_FIXMTRR) ? MTRR_N64K + MTRR_N16K + MTRR_N4K : 0;
	curr_md = sc->mr_desc + i;
	free_md = NULL;
	for (; i < sc->mr_ndesc; i++, curr_md++) {
		if (curr_md->mr_flags & MDF_ACTIVE) {
			/* exact match? */
			if ((curr_md->mr_base == mrd->mr_base) &&
			    (curr_md->mr_len == mrd->mr_len)) {
				/* check we aren't doing something risky */
				if (!(mrd->mr_flags & MDF_FORCE) &&
				    ((curr_md->mr_flags & MDF_ATTRMASK)
				    == MDF_UNKNOWN))
					return (EACCES);
				/* Ok, just hijack this entry */
				free_md = curr_md;
				break;
			}
			/* non-exact overlap ? */
			if (mroverlap(curr_md, mrd)) {
				/* between conflicting region types? */
				if (mtrrconflict(curr_md->mr_flags,
						      mrd->mr_flags))
					return(EINVAL);
			}
		} else if (free_md == NULL) {
			free_md = curr_md;
		}
	}
	/* got somewhere to put it? */
	if (free_md == NULL)
		return(ENOSPC);
	
	/* Set up new descriptor */
	free_md->mr_base = mrd->mr_base;
	free_md->mr_len = mrd->mr_len;
	free_md->mr_flags = mrcopyflags(MDF_ACTIVE, mrd->mr_flags);
	memcpy(free_md->mr_owner, mrd->mr_owner, sizeof(mrd->mr_owner));
	return(0);
}

/*
 * Handle requests to set memory range attributes by manipulating MTRRs.
 */
int
mrset(struct mem_range_softc *sc, struct mem_range_desc *mrd, int *arg)
{
	struct mem_range_desc	*targ;
	int			 error = 0;

	switch(*arg) {
	case MEMRANGE_SET_UPDATE:
		/* make sure that what's being asked for is possible */
		if (!mrvalid(mrd->mr_base, mrd->mr_len) ||
		    mtrrtype(mrd->mr_flags) == -1)
			return(EINVAL);
		
		/* are the "low memory" conditions applicable? */
		if ((sc->mr_cap & MR_FIXMTRR) &&
		    ((mrd->mr_base + mrd->mr_len) <= FIXTOP)) {
			if ((error = mrsetlow(sc, mrd, arg)) != 0)
				return(error);
		} else {
			/* it's time to play with variable MTRRs */
			if ((error = mrsetvariable(sc, mrd, arg)) != 0)
				return(error);
		}
		break;
		
	case MEMRANGE_SET_REMOVE:
		if ((targ = mem_range_match(sc, mrd)) == NULL)
			return(ENOENT);
		if (targ->mr_flags & MDF_FIXACTIVE)
			return(EPERM);
		targ->mr_flags &= ~MDF_ACTIVE;
		targ->mr_owner[0] = 0;
		break;
		
	default:
		return(EOPNOTSUPP);
	}
	
	/* update the hardware */
	mrstore(sc);
	return(0);
}

/*
 * Work out how many ranges we support, initialise storage for them,
 * fetch the initial settings.
 */
void
mrinit(struct mem_range_softc *sc)
{
	struct mem_range_desc	*mrd;
	uint32_t		 regs[4];
	int			 nmdesc = 0;
	int			 i;

	mtrrcap = rdmsr(MSR_MTRRcap);
	mtrrdef = rdmsr(MSR_MTRRdefType);
	
	/* For now, bail out if MTRRs are not enabled */
	if (!(mtrrdef & MTRRdefType_ENABLE)) {
		printf("mtrr: CPU supports MTRRs but not enabled by BIOS\n");
		return;
	}
	nmdesc = mtrrcap & 0xff;
	printf("mtrr: Pentium Pro MTRR support, %d var ranges", nmdesc);
	
	/* If fixed MTRRs supported and enabled */
	if ((mtrrcap & MTRRcap_FIXED) &&
	    (mtrrdef & MTRRdefType_FIXED_ENABLE)) {
		sc->mr_cap = MR_FIXMTRR;
		nmdesc += MTRR_N64K + MTRR_N16K + MTRR_N4K;
		printf(", %d fixed ranges", MTRR_N64K + MTRR_N16K + MTRR_N4K);
	}

	printf("\n");
	
	sc->mr_desc = mallocarray(nmdesc, sizeof(struct mem_range_desc),
	     M_MEMDESC, M_WAITOK|M_ZERO);
	sc->mr_ndesc = nmdesc;
	
	mrd = sc->mr_desc;
	
	/* Populate the fixed MTRR entries' base/length */
	if (sc->mr_cap & MR_FIXMTRR) {
		for (i = 0; i < MTRR_N64K; i++, mrd++) {
			mrd->mr_base = i * 0x10000;
			mrd->mr_len = 0x10000;
			mrd->mr_flags = MDF_FIXBASE | MDF_FIXLEN | MDF_FIXACTIVE;
		}

		for (i = 0; i < MTRR_N16K; i++, mrd++) {
			mrd->mr_base = i * 0x4000 + 0x80000;
			mrd->mr_len = 0x4000;
			mrd->mr_flags = MDF_FIXBASE | MDF_FIXLEN | MDF_FIXACTIVE;
		}

		for (i = 0; i < MTRR_N4K; i++, mrd++) {
			mrd->mr_base = i * 0x1000 + 0xc0000;
			mrd->mr_len = 0x1000;
			mrd->mr_flags = MDF_FIXBASE | MDF_FIXLEN | MDF_FIXACTIVE;
		}
	}
	
	/*
	 * Fetch maximum physical address size supported by the
	 * processor as supported by CPUID leaf function 0x80000008.
	 * If CPUID does not support leaf function 0x80000008, use the
	 * default 36-bit address size.
	 */
	if (curcpu()->ci_pnfeatset >= 0x80000008) {
		CPUID(0x80000008, regs[0], regs[1], regs[2], regs[3]);
		if (regs[0] & 0xff) {
			mtrrmask = (1ULL << (regs[0] & 0xff)) - 1;
			mtrrmask &= ~0x0000000000000fffULL;
		}
	}

	/*
	 * Get current settings, anything set now is considered to have
	 * been set by the firmware.
	 */
	mrfetch(sc);
	mrd = sc->mr_desc;
	for (i = 0; i < sc->mr_ndesc; i++, mrd++) {
		if (mrd->mr_flags & MDF_ACTIVE)
			mrd->mr_flags |= MDF_FIRMWARE;
	}
}

/*
 * Initialise MTRRs on a cpu from the software state.
 */
void
mrinit_cpu(struct mem_range_softc *sc)
{
	mrstoreone(sc); /* set MTRRs to match BSP */
}

void
mrreload_cpu(struct mem_range_softc *sc)
{
	u_long s;

	s = intr_disable();
	mrstoreone(sc); /* set MTRRs to match BSP */
	intr_restore(s);
}
