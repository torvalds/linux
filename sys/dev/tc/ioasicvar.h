/*	$OpenBSD: ioasicvar.h,v 1.10 2009/11/07 23:01:38 miod Exp $	*/
/*	$NetBSD: ioasicvar.h,v 1.14 2000/10/17 09:45:49 nisimura Exp $	*/

/*
 * Copyright (c) 1995 Carnegie-Mellon University.
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

#ifndef _DEV_TC_IOASICVAR_H_
#define _DEV_TC_IOASICVAR_H_

struct ioasic_dev {
	char		*iad_modname;
	tc_offset_t	iad_offset;
	void		*iad_cookie;
	u_int32_t	iad_intrbits;
};

struct ioasicdev_attach_args {
	char	iada_modname[TC_ROM_LLEN + 1];
	tc_offset_t iada_offset;
	tc_addr_t iada_addr;
	void	*iada_cookie;
};

/* Device locators. */
#define	ioasiccf_offset	cf_loc[0]		/* offset */

#define	IOASIC_OFFSET_UNKNOWN	-1

struct ioasic_softc {
	struct	device sc_dv;
	bus_space_tag_t sc_bst;
	bus_space_handle_t sc_bsh;
	bus_dma_tag_t sc_dmat;

	tc_addr_t sc_base;		/* XXX offset XXX */
};

extern struct cfdriver ioasic_cd;

/*
 * XXX Some drivers need direct access to IOASIC registers.
 */
extern tc_addr_t ioasic_base;

void    ioasic_intr_establish(struct device *, void *,
	    int, int (*)(void *), void *, const char *);
void    ioasic_intr_disestablish(struct device *, void *);
int	ioasic_submatch(void *, struct ioasicdev_attach_args *);
void	ioasic_attach_devs(struct ioasic_softc *,
	    struct ioasic_dev *, int);
void	ioasic_led_blink(void *);

#endif /* _DEV_TC_IOASICVAR_ */
