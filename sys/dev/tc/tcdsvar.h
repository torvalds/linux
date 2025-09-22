/* $OpenBSD: tcdsvar.h,v 1.4 2008/08/09 16:42:30 miod Exp $ */
/* $NetBSD: tcdsvar.h,v 1.2 2001/08/22 05:00:27 nisimura Exp $ */

/*
 * Copyright (c) 1995, 1996 Carnegie-Mellon University.
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

struct tcds_slotconfig {
	/*
	 * Bookkeeping information
	 */
	int	sc_slot;

	bus_space_tag_t sc_bst;			/* to frob TCDS regs */
	bus_space_handle_t sc_bsh;

	int	(*sc_intrhand)(void *);		/* intr. handler */
	void	*sc_intrarg;			/* intr. handler arg. */
	struct evcount sc_count;		/* intr. count */

	/*
	 * Sets of bits in TCDS CIR and IMER that enable/check
	 * various things.
	 */
	u_int32_t sc_resetbits;
	u_int32_t sc_intrmaskbits;
	u_int32_t sc_intrbits;
	u_int32_t sc_dmabits;
	u_int32_t sc_errorbits;

	/*
	 * Offsets to slot-specific DMA resources.
	 */
	bus_size_t sc_sda;
	bus_size_t sc_dic;
	bus_size_t sc_dud0;
	bus_size_t sc_dud1;
};

struct tcdsdev_attach_args {
	bus_space_tag_t tcdsda_bst;		/* bus space tag */
	bus_space_handle_t tcdsda_bsh;		/* bus space handle */
	bus_dma_tag_t tcdsda_dmat;		/* bus dma tag */
	struct tcds_slotconfig *tcdsda_sc;	/* slot configuration */
	int	tcdsda_chip;			/* chip number */
	int	tcdsda_id;			/* SCSI ID */
	u_int	tcdsda_freq;			/* chip frequency */
	int	tcdsda_period;			/* min. sync period */
	int	tcdsda_variant;			/* NCR chip variant */
	int	tcdsda_fast;			/* chip does Fast mode */
};

/*
 * TCDS functions.
 */
void	tcds_intr_establish(struct device *, int,
	    int (*)(void *), void *, const char *);
void	tcds_intr_disestablish(struct device *, int);
void	tcds_dma_enable(struct tcds_slotconfig *, int);
void	tcds_scsi_enable(struct tcds_slotconfig *, int);
int	tcds_scsi_iserr(struct tcds_slotconfig *);
int	tcds_scsi_isintr(struct tcds_slotconfig *, int);
void	tcds_scsi_reset(struct tcds_slotconfig *);
