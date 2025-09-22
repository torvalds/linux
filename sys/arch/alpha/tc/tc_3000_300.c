/* $OpenBSD: tc_3000_300.c,v 1.21 2025/06/29 15:55:22 miod Exp $ */
/* $NetBSD: tc_3000_300.c,v 1.26 2001/07/27 00:25:21 thorpej Exp $ */

/*
 * Copyright (c) 1994, 1995, 1996 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Chris G. Demetriou
 * 
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" 
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND 
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <machine/autoconf.h>
#include <machine/pte.h>

#include <dev/tc/tcvar.h>
#include <dev/tc/ioasicreg.h>
#include <alpha/tc/tc_conf.h>
#include <alpha/tc/tc_3000_300.h>

#include "wsdisplay.h"

int	tc_3000_300_intrnull(void *);

#define	C(x)	((void *)(u_long)x)
#define	KV(x)	(ALPHA_PHYS_TO_K0SEG(x))

/*
 * We have to read and modify the IOASIC registers directly, because
 * the TC option slot interrupt request and mask bits are stored there,
 * and the ioasic code isn't initted when we need to frob some interrupt
 * bits.
 */
#define	DEC_3000_300_IOASIC_ADDR	KV(0x1a0000000)

struct tc_slotdesc tc_3000_300_slots[] = {
	{ KV(0x100000000), C(TC_3000_300_DEV_OPT0), },	/* 0 - opt slot 0 */
	{ KV(0x120000000), C(TC_3000_300_DEV_OPT1), },	/* 1 - opt slot 1 */
	{ KV(0x140000000), C(TC_3000_300_DEV_BOGUS), }, /* 2 - unused */
	{ KV(0x160000000), C(TC_3000_300_DEV_BOGUS), }, /* 3 - unused */
	{ KV(0x180000000), C(TC_3000_300_DEV_BOGUS), },	/* 4 - TCDS ASIC */
	{ KV(0x1a0000000), C(TC_3000_300_DEV_BOGUS), }, /* 5 - IOCTL ASIC */
	{ KV(0x1c0000000), C(TC_3000_300_DEV_BOGUS), }, /* 6 - CXTurbo */
};
int tc_3000_300_nslots =
    sizeof(tc_3000_300_slots) / sizeof(tc_3000_300_slots[0]);

struct tc_builtin tc_3000_300_builtins[] = {
	{ "PMAGB-BA",	6, 0x02000000, C(TC_3000_300_DEV_CXTURBO),	},
	{ "FLAMG-IO",	5, 0x00000000, C(TC_3000_300_DEV_IOASIC),	},
	{ "PMAZ-DS ",	4, 0x00000000, C(TC_3000_300_DEV_TCDS),		},
};
int tc_3000_300_nbuiltins =
    sizeof(tc_3000_300_builtins) / sizeof(tc_3000_300_builtins[0]);

struct tcintr {
	int	(*tci_func)(void *);
	void	*tci_arg;
	int	tci_level;
	struct evcount tci_count;
} tc_3000_300_intr[TC_3000_300_NCOOKIES];

void
tc_3000_300_intr_setup(void)
{
	volatile u_int32_t *imskp;
	u_long i;

	/*
	 * Disable all interrupts that we can (can't disable builtins).
	 */
	imskp = (volatile u_int32_t *)(DEC_3000_300_IOASIC_ADDR + IOASIC_IMSK);
	*imskp &= ~(IOASIC_INTR_300_OPT0 | IOASIC_INTR_300_OPT1);

	/*
	 * Set up interrupt handlers.
	 */
	for (i = 0; i < TC_3000_300_NCOOKIES; i++) {
		tc_3000_300_intr[i].tci_func = tc_3000_300_intrnull;
		tc_3000_300_intr[i].tci_arg = (void *)i;
		tc_3000_300_intr[i].tci_level = IPL_HIGH;
	}
}

void
tc_3000_300_intr_establish(struct device *tcadev, void *cookie, int level,
    int (*func)(void *), void *arg, const char *name)
{
	volatile u_int32_t *imskp;
	u_long dev = (u_long)cookie;

#ifdef DIAGNOSTIC
	/* XXX bounds-check cookie. */
#endif

	if (tc_3000_300_intr[dev].tci_func != tc_3000_300_intrnull)
		panic("tc_3000_300_intr_establish: cookie %lu twice", dev);

	tc_3000_300_intr[dev].tci_func = func;
	tc_3000_300_intr[dev].tci_arg = arg;
	tc_3000_300_intr[dev].tci_level = level;
	if (name != NULL)
		evcount_attach(&tc_3000_300_intr[dev].tci_count, name, NULL);

	imskp = (volatile u_int32_t *)(DEC_3000_300_IOASIC_ADDR + IOASIC_IMSK);
	switch (dev) {
	case TC_3000_300_DEV_OPT0:
		*imskp |= IOASIC_INTR_300_OPT0;
		break;
	case TC_3000_300_DEV_OPT1:
		*imskp |= IOASIC_INTR_300_OPT1;
		break;
	default:
		/* interrupts for builtins always enabled */
		break;
	}
}

void
tc_3000_300_intr_disestablish(struct device *tcadev, void *cookie,
    const char *name)
{
	volatile u_int32_t *imskp;
	u_long dev = (u_long)cookie;

#ifdef DIAGNOSTIC
	/* XXX bounds-check cookie. */
#endif

	if (tc_3000_300_intr[dev].tci_func == tc_3000_300_intrnull)
		panic("tc_3000_300_intr_disestablish: cookie %lu bad intr",
		    dev);

	imskp = (volatile u_int32_t *)(DEC_3000_300_IOASIC_ADDR + IOASIC_IMSK);
	switch (dev) {
	case TC_3000_300_DEV_OPT0:
		*imskp &= ~IOASIC_INTR_300_OPT0;
		break;
	case TC_3000_300_DEV_OPT1:
		*imskp &= ~IOASIC_INTR_300_OPT1;
		break;
	default:
		/* interrupts for builtins always enabled */
		break;
	}

	tc_3000_300_intr[dev].tci_func = tc_3000_300_intrnull;
	tc_3000_300_intr[dev].tci_arg = (void *)dev;
	tc_3000_300_intr[dev].tci_level = IPL_HIGH;
	if (name != NULL)
		evcount_detach(&tc_3000_300_intr[dev].tci_count);
}

int
tc_3000_300_intrnull(void *val)
{

	panic("tc_3000_300_intrnull: uncaught TC intr for cookie %ld",
	    (u_long)val);
}

void
tc_3000_300_iointr(void *arg, unsigned long vec)
{
	u_int32_t tcir, ioasicir, ioasicimr;
	int ifound;

	do {
		tc_syncbus();

		/* find out what interrupts/errors occurred */
		tcir = *(volatile u_int32_t *)TC_3000_300_IR;
		ioasicir = *(volatile u_int32_t *)
		    (DEC_3000_300_IOASIC_ADDR + IOASIC_INTR);
		ioasicimr = *(volatile u_int32_t *)
		    (DEC_3000_300_IOASIC_ADDR + IOASIC_IMSK);
		tc_mb();

		/* Ignore interrupts that aren't enabled out. */
		ioasicir &= ioasicimr;

		/* clear the interrupts/errors we found. */
		*(volatile u_int32_t *)TC_3000_300_IR = tcir;
		/* XXX can't clear TC option slot interrupts here? */
		tc_wmb();

		ifound = 0;

#ifdef MULTIPROCESSOR
#define	INTRLOCK(slot)							\
		if (tc_3000_300_intr[slot].tci_level < IPL_CLOCK)	\
			__mp_lock(&kernel_lock)
#define	INTRUNLOCK(slot)						\
		if (tc_3000_300_intr[slot].tci_level < IPL_CLOCK)	\
			__mp_unlock(&kernel_lock)
#else
#define	INTRLOCK(slot)		do { } while (0)
#define	INTRUNLOCK(slot)	do { } while (0)
#endif
#define	CHECKINTR(slot, flag)						\
		if (flag) {						\
			ifound = 1;					\
			INTRLOCK(slot);					\
			(*tc_3000_300_intr[slot].tci_func)		\
			    (tc_3000_300_intr[slot].tci_arg);		\
			tc_3000_300_intr[slot].tci_count.ec_count++;	\
			INTRUNLOCK(slot);				\
		}

		/* Do them in order of priority; highest slot # first. */
		CHECKINTR(TC_3000_300_DEV_CXTURBO,
		    tcir & TC_3000_300_IR_CXTURBO);
		CHECKINTR(TC_3000_300_DEV_IOASIC,
		    (tcir & TC_3000_300_IR_IOASIC) &&
		    (ioasicir & ~(IOASIC_INTR_300_OPT1|IOASIC_INTR_300_OPT0)));
		CHECKINTR(TC_3000_300_DEV_TCDS, tcir & TC_3000_300_IR_TCDS);
		CHECKINTR(TC_3000_300_DEV_OPT1,
		    ioasicir & IOASIC_INTR_300_OPT1);
		CHECKINTR(TC_3000_300_DEV_OPT0,
		    ioasicir & IOASIC_INTR_300_OPT0);

#undef INTRUNLOCK
#undef INTRLOCK
#undef CHECKINTR

#ifdef DIAGNOSTIC
#define PRINTINTR(msg, bits)						\
	if (tcir & bits)						\
		printf(msg);

		PRINTINTR("BCache tag parity error\n",
		    TC_3000_300_IR_BCTAGPARITY);
		PRINTINTR("TC overrun error\n", TC_3000_300_IR_TCOVERRUN);
		PRINTINTR("TC I/O timeout\n", TC_3000_300_IR_TCTIMEOUT);
		PRINTINTR("Bcache parity error\n",
		    TC_3000_300_IR_BCACHEPARITY);
		PRINTINTR("Memory parity error\n", TC_3000_300_IR_MEMPARITY);

#undef PRINTINTR
#endif
	} while (ifound);
}

#if NWSDISPLAY > 0
/*
 * tc_3000_300_fb_cnattach --
 *	Attempt to map the CTB output device to a slot and attach the
 * framebuffer as the output side of the console.
 */
int
tc_3000_300_fb_cnattach(u_int64_t turbo_slot)
{
	u_int32_t output_slot;

	output_slot = turbo_slot & 0xffffffff;

	if (output_slot >= tc_3000_300_nslots) {
		return EINVAL;
	}

	if (output_slot == 0) {
		return ENXIO;
	}

	return tc_fb_cnattach(tc_3000_300_slots[output_slot-1].tcs_addr);
}
#endif /* NWSDISPLAY */
