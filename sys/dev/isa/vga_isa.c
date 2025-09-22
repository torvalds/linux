/* $OpenBSD: vga_isa.c,v 1.11 2022/04/06 18:59:29 naddy Exp $ */
/* $NetBSD: vga_isa.c,v 1.3 1998/06/12 18:45:48 drochner Exp $ */

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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <dev/isa/isavar.h>

#include <dev/ic/mc6845reg.h>
#include <dev/ic/pcdisplayvar.h>
#include <dev/ic/vgareg.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>

#include <dev/ic/vgavar.h>
#include <dev/isa/vga_isavar.h>

struct vga_isa_softc {
	struct device sc_dev; 
#if 0
	struct vga_config *sc_vc;	/* VGA configuration */
#endif
};

int	vga_isa_match(struct device *, void *, void *);
void	vga_isa_attach(struct device *, struct device *, void *);

const struct cfattach vga_isa_ca = {
	sizeof(struct vga_isa_softc), vga_isa_match, vga_isa_attach,
};

int
vga_isa_match(struct device *parent, void *match, void *aux)
{
	struct isa_attach_args *ia = aux;

	/* If values are hardwired to something that they can't be, punt. */
	if ((ia->ia_iobase != IOBASEUNK && ia->ia_iobase != 0x3b0) ||
	    /* ia->ia_iosize != 0 || XXX isa.c */
	    (ia->ia_maddr != MADDRUNK && ia->ia_maddr != 0xa0000) ||
	    (ia->ia_msize != 0 && ia->ia_msize != 0x20000) ||
	    ia->ia_irq != IRQUNK || ia->ia_drq != DRQUNK)
		return (0);

	if (!vga_is_console(ia->ia_iot, WSDISPLAY_TYPE_ISAVGA) &&
	    !vga_common_probe(ia->ia_iot, ia->ia_memt))
		return (0);

	ia->ia_iobase = 0x3b0;	/* XXX mono 0x3b0 color 0x3c0 */
	ia->ia_iosize = 0x30;	/* XXX 0x20 */
	ia->ia_maddr = 0xa0000;
	ia->ia_msize = 0x20000;
	return (2);	/* more than generic pcdisplay */
}

void
vga_isa_attach(struct device *parent, struct device *self, void *aux)
{
	struct isa_attach_args *ia = aux;
#if 0
	struct vga_isa_softc *sc = (struct vga_isa_softc *)self;
#endif

	printf("\n");

	vga_common_attach(self, ia->ia_iot, ia->ia_memt,
			  WSDISPLAY_TYPE_ISAVGA);
}

int
vga_isa_cnattach(bus_space_tag_t iot, bus_space_tag_t memt)
{
	return (vga_cnattach(iot, memt, WSDISPLAY_TYPE_ISAVGA, 1));
}
