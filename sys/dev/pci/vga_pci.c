/* $OpenBSD: vga_pci.c,v 1.92 2025/06/12 09:17:46 jsg Exp $ */
/* $NetBSD: vga_pci.c,v 1.3 1998/06/08 06:55:58 thorpej Exp $ */

/*
 * Copyright (c) 2001 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Frank van der Linden for Wasabi Systems, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed for the NetBSD Project by
 *	Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
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

#include "vga.h"
#if defined(__i386__) || defined(__amd64__)
#include "acpi.h"
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <machine/bus.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/ic/mc6845reg.h>
#include <dev/ic/pcdisplayvar.h>
#include <dev/ic/vgareg.h>
#include <dev/pci/vga_pcivar.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/ic/vgavar.h>

#ifdef X86EMU
#include <machine/vga_post.h>
#endif

int	vga_pci_match(struct device *, void *, void *);
void	vga_pci_attach(struct device *, struct device *, void *);
int	vga_pci_activate(struct device *, int);

#if !defined(SMALL_KERNEL) && NACPI > 0
void	vga_save_state(struct vga_pci_softc *);
void	vga_restore_state(struct vga_pci_softc *);
#endif

const struct cfattach vga_pci_ca = {
	sizeof(struct vga_pci_softc), vga_pci_match, vga_pci_attach,
	NULL, vga_pci_activate
};

#if !defined(SMALL_KERNEL) && NACPI > 0
int vga_pci_do_post;

struct vga_device_description {
	u_int16_t	rval[4];
	u_int16_t	rmask[4];
	char		vga_pci_post;
};

static const struct vga_device_description vga_devs[] = {
	/*
	 * Header description:
	 *
	 * First entry is a list of the pci video information in the following
	 * order: VENDOR, PRODUCT, SUBVENDOR, SUBPRODUCT
	 *
	 * The next entry is a list of corresponding masks.
	 *
	 * Finally the last value indicates if we should repost via 
	 * vga_pci (i.e. the x86emulator) * bios.
	 */
	{	/* All machines with GMA500/Poulsbo */
	    {	PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_US15W_IGD,
	    	0x0000, 0x0000 },
	    {	0xffff, 0xffff, 0x0000, 0x0000 }, 1
	},
	{	/* All machines with GMA500/Poulsbo */
	    {	PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_US15L_IGD,
	    	0x0000, 0x0000 },
	    {	0xffff, 0xffff, 0x0000, 0x0000 }, 1
	},
	{	/* All machines with GMA600/Oaktrail, 0x4100:4107 */
	    {	PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_GMA600_0,
	    	0x0000, 0x0000 },
	    {	0xffff, 0xfff8, 0x0000, 0x0000 }, 1
	},
	{	/* All machines with GMA600/Oaktrail, 0x4108 */
	    {	PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_GMA600_8,
	    	0x0000, 0x0000 },
	    {	0xffff, 0xffff, 0x0000, 0x0000 }, 1
	},
	{	/* All machines with Medfield, 0x0130:0x0137 */
	    {	PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_MDFLD_IGD_0,
	    	0x0000, 0x0000 },
	    {	0xffff, 0xfff8, 0x0000, 0x0000 }, 1
	},
	{	/* All machines with GMA36x0/Cedartrail, 0x0be0:0x0bef */
	    {	PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_GMA3600_0,
	    	0x0000, 0x0000 },
	    {	0xffff, 0xfff0, 0x0000, 0x0000 }, 1
	},
};
#endif

int
vga_pci_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;

	if (DEVICE_IS_VGA_PCI(pa->pa_class) == 0)
		return (0);

	/* check whether it is disabled by firmware */
	if ((pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_COMMAND_STATUS_REG)
	    & (PCI_COMMAND_IO_ENABLE | PCI_COMMAND_MEM_ENABLE))
	    != (PCI_COMMAND_IO_ENABLE | PCI_COMMAND_MEM_ENABLE))
		return (0);

	/* If it's the console, we have a winner! */
	if (vga_is_console(pa->pa_iot, WSDISPLAY_TYPE_PCIVGA))
		return (1);

	/*
	 * If we might match, make sure that the card actually looks OK.
	 */
	if (!vga_common_probe(pa->pa_iot, pa->pa_memt))
		return (0);

	return (1);
}

void
vga_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct pci_attach_args *pa = aux;
	pcireg_t reg;
	struct vga_pci_softc *sc = (struct vga_pci_softc *)self;
#if !defined(SMALL_KERNEL) && NACPI > 0
	int prod, vend, subid, subprod, subvend, i;
#endif

	/*
	 * Enable bus master; X might need this for accelerated graphics.
	 */
	reg = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_COMMAND_STATUS_REG);
	reg |= PCI_COMMAND_MASTER_ENABLE;
	pci_conf_write(pa->pa_pc, pa->pa_tag, PCI_COMMAND_STATUS_REG, reg);

	sc->sc_type = WSDISPLAY_TYPE_PCIVGA;

	printf("\n");

#if !defined(SMALL_KERNEL) && NACPI > 0

#ifdef X86EMU
	if ((sc->sc_posth = vga_post_init(pa->pa_bus, pa->pa_device,
	    pa->pa_function)) == NULL)
		printf("couldn't set up vga POST handler\n");
#endif

	vend = PCI_VENDOR(pa->pa_id);
	prod = PCI_PRODUCT(pa->pa_id);
	subid = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_SUBSYS_ID_REG);
	subvend = PCI_VENDOR(subid);
	subprod = PCI_PRODUCT(subid);

	for (i = 0; i < nitems(vga_devs); i++)
		if ((vend & vga_devs[i].rmask[0]) == vga_devs[i].rval[0] &&
		    (prod & vga_devs[i].rmask[1]) == vga_devs[i].rval[1] &&
		    (subvend & vga_devs[i].rmask[2]) == vga_devs[i].rval[2] &&
		    (subprod & vga_devs[i].rmask[3]) == vga_devs[i].rval[3]) {
			vga_pci_do_post = vga_devs[i].vga_pci_post;
			break;
		}
#endif

#ifdef RAMDISK_HOOKS
	if (vga_aperture_needed(pa))
		printf("%s: aperture needed\n", sc->sc_dev.dv_xname);
#endif

	sc->sc_vc = vga_common_attach(self, pa->pa_iot, pa->pa_memt,
	    sc->sc_type);
}

int
vga_pci_activate(struct device *self, int act)
{
	int rv = 0;

#if !defined(SMALL_KERNEL) && NACPI > 0
	struct vga_pci_softc *sc = (struct vga_pci_softc *)self;
#endif

	switch (act) {
	case DVACT_SUSPEND:
		rv = config_activate_children(self, act);
#if !defined(SMALL_KERNEL) && NACPI > 0
		/*
		 * Save the common vga state. This should theoretically only
		 * be necessary if we intend to POST, but it is preferable
		 * to do it unconditionally, as many systems do not restore
		 * this state correctly upon resume.
		 */
		vga_save_state(sc);
#endif
		break;
	case DVACT_RESUME:
#if !defined(SMALL_KERNEL) && NACPI > 0
#if defined (X86EMU)
		if (vga_pci_do_post)
			vga_post_call(sc->sc_posth);
#endif
		vga_restore_state(sc);
#endif
		rv = config_activate_children(self, act);
		break;
	default:
		rv = config_activate_children(self, act);
		break;
	}

	return (rv);
}

int
vga_pci_cnattach(bus_space_tag_t iot, bus_space_tag_t memt,
    pci_chipset_tag_t pc, int bus, int device, int function)
{
	return (vga_cnattach(iot, memt, WSDISPLAY_TYPE_PCIVGA, 0));
}

int
vga_pci_ioctl(void *v, u_long cmd, caddr_t addr, int flag, struct proc *pb)
{
	int error = 0;

	switch (cmd) {
	case WSDISPLAYIO_GETPARAM:
		if (ws_get_param != NULL)
			return (*ws_get_param)((struct wsdisplay_param *)addr);
		else
			error = ENOTTY;
		break;
	case WSDISPLAYIO_SETPARAM:
		if (ws_set_param != NULL)
			return (*ws_set_param)((struct wsdisplay_param *)addr);
		else
			error = ENOTTY;
		break;
	default:
		error = ENOTTY;
	}

	return (error);
}

#if !defined(SMALL_KERNEL) && NACPI > 0
void
vga_save_state(struct vga_pci_softc *sc)
{
	struct vga_config *vc = sc->sc_vc;
	struct vga_handle *vh;
	struct vgascreen *scr;
	size_t i;
	char *buf;

	if (vc == NULL)
		return;

	vh = &vc->hdl;

	/*
	 * Save sequencer registers
	 */
	vga_ts_write(vh, syncreset, 1);	/* stop sequencer */
	buf = (char *)&sc->sc_save_ts;
	*buf++ = 0;
	for (i = 1; i < sizeof(sc->sc_save_ts); i++)
		*buf++ = _vga_ts_read(vh, i);
	vga_ts_write(vh, syncreset, 3);	/* start sequencer */
	/* pretend screen is not blanked */
	sc->sc_save_ts.mode &= ~0x20;
	sc->sc_save_ts.mode |= 0x80;

	/*
	 * Save CRTC registers
	 */
	buf = (char *)&sc->sc_save_crtc;
	for (i = 0; i < sizeof(sc->sc_save_crtc); i++)
		*buf++ = _pcdisplay_6845_read(&vh->vh_ph, i);

	/*
	 * Save ATC registers
	 */
	buf = (char *)&sc->sc_save_atc;
	for (i = 0; i < sizeof(sc->sc_save_atc); i++)
		*buf++ = _vga_attr_read(vh, i);

	/*
	 * Save GDC registers
	 */
	buf = (char *)&sc->sc_save_gdc;
	for (i = 0; i < sizeof(sc->sc_save_gdc); i++)
		*buf++ = _vga_gdc_read(vh, i);

	vga_save_palette(vc);

	/* XXX should also save font data */

	/*
	 * Save current screen contents if we have backing store for it,
	 * and intend to POST on resume.
	 * XXX Since we don't allocate backing store unless the second VT is
	 * XXX created, we could theoretically have no backing store available
	 * XXX at this point.
	 */
	if (vga_pci_do_post) {
		scr = vc->active;
		if (scr != NULL && scr->pcs.active && scr->pcs.mem != NULL)
			bus_space_read_region_2(vh->vh_memt, vh->vh_memh,
			    scr->pcs.dispoffset, scr->pcs.mem,
			    scr->pcs.type->ncols * scr->pcs.type->nrows);
	}
}

void
vga_restore_state(struct vga_pci_softc *sc)
{
	struct vga_config *vc = sc->sc_vc;
	struct vga_handle *vh;
	struct vgascreen *scr;
	size_t i;
	char *buf;

	if (vc == NULL)
		return;

	vh = &vc->hdl;

	/*
	 * Restore sequencer registers
	 */
	vga_ts_write(vh, syncreset, 1);	/* stop sequencer */
	buf = (char *)&sc->sc_save_ts + 1;
	for (i = 1; i < sizeof(sc->sc_save_ts); i++)
		_vga_ts_write(vh, i, *buf++);
	vga_ts_write(vh, syncreset, 3);	/* start sequencer */

	/*
	 * Restore CRTC registers
	 */
	/* unprotect registers 00-07 */
	vga_6845_write(vh, vsynce,
	    vga_6845_read(vh, vsynce) & ~0x80);
	buf = (char *)&sc->sc_save_crtc;
	for (i = 0; i < sizeof(sc->sc_save_crtc); i++)
		_pcdisplay_6845_write(&vh->vh_ph, i, *buf++);

	/*
	 * Restore ATC registers
	 */
	buf = (char *)&sc->sc_save_atc;
	for (i = 0; i < sizeof(sc->sc_save_atc); i++)
		_vga_attr_write(vh, i, *buf++);

	/*
	 * Restore GDC registers
	 */
	buf = (char *)&sc->sc_save_gdc;
	for (i = 0; i < sizeof(sc->sc_save_gdc); i++)
		_vga_gdc_write(vh, i, *buf++);

	vga_restore_fonts(vc);
	vga_restore_palette(vc);

	/*
	 * Restore current screen contents if we have backing store for it,
	 * and have POSTed on resume.
	 * XXX Since we don't allocate backing store unless the second VT is
	 * XXX created, we could theoretically have no backing store available
	 * XXX at this point.
	 */
	if (vga_pci_do_post) {
		scr = vc->active;
		if (scr != NULL && scr->pcs.active && scr->pcs.mem != NULL)
			bus_space_write_region_2(vh->vh_memt, vh->vh_memh,
			    scr->pcs.dispoffset, scr->pcs.mem,
			    scr->pcs.type->ncols * scr->pcs.type->nrows);
	}
}
#endif
