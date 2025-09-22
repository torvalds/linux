/*	$OpenBSD: vgafb.c,v 1.70 2024/05/13 01:15:50 jsg Exp $	*/

/*
 * Copyright (c) 2001 Jason L. Wright (jason@thought.net)
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
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Effort sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F30602-01-2-0537.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/malloc.h>
#include <sys/pciio.h>

#include <uvm/uvm_extern.h>

#include <machine/autoconf.h>
#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/openfirm.h>

#include <dev/pci/pcidevs.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/vga_pcivar.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/rasops/rasops.h>

#include <machine/fbvar.h>

struct vgafb_softc {
	struct sunfb sc_sunfb;
	int sc_nscreens;
	int sc_node, sc_ofhandle;
	bus_space_tag_t sc_mem_t;
	bus_space_tag_t sc_io_t;
	pcitag_t sc_pcitag;
	bus_space_handle_t sc_mem_h;
	bus_addr_t sc_io_addr, sc_mem_addr, sc_mmio_addr;
	bus_size_t sc_io_size, sc_mem_size, sc_mmio_size;
	int sc_console;
	u_int sc_mode;
	u_int8_t sc_cmap_red[256];
	u_int8_t sc_cmap_green[256];
	u_int8_t sc_cmap_blue[256];
};

int vgafb_mapregs(struct vgafb_softc *, struct pci_attach_args *);
int vgafb_ioctl(void *, u_long, caddr_t, int, struct proc *);
paddr_t vgafb_mmap(void *, off_t, int);
int vgafb_is_console(int);
int vgafb_getcmap(struct vgafb_softc *, struct wsdisplay_cmap *);
int vgafb_putcmap(struct vgafb_softc *, struct wsdisplay_cmap *);
void vgafb_setcolor(void *, u_int, u_int8_t, u_int8_t, u_int8_t);

struct wsdisplay_accessops vgafb_accessops = {
	.ioctl = vgafb_ioctl,
	.mmap = vgafb_mmap
};

int	vgafbmatch(struct device *, void *, void *);
void	vgafbattach(struct device *, struct device *, void *);

const struct cfattach vgafb_ca = {
	sizeof (struct vgafb_softc), vgafbmatch, vgafbattach
};

struct cfdriver vgafb_cd = {
	NULL, "vgafb", DV_DULL
};

#ifdef APERTURE
extern int allowaperture;
#endif

int
vgafbmatch(struct device *parent, void *vcf, void *aux)
{
	struct pci_attach_args *pa = aux;
	int node;

	/*
	 * Do not match on Expert3D devices, which are driven by ifb(4).
	 */
	if (ifb_ident(aux) != 0)
		return (0);

	/*
	 * XXX Non-console devices do not get configured by the PROM,
	 * XXX so do not attach them yet.
	 */
	node = PCITAG_NODE(pa->pa_tag);
	if (!vgafb_is_console(node))
		return (0);

	if (PCI_CLASS(pa->pa_class) == PCI_CLASS_PREHISTORIC &&
	    PCI_SUBCLASS(pa->pa_class) == PCI_SUBCLASS_PREHISTORIC_VGA)
		return (1);

	if (PCI_CLASS(pa->pa_class) == PCI_CLASS_DISPLAY &&
	    PCI_SUBCLASS(pa->pa_class) == PCI_SUBCLASS_DISPLAY_VGA)
		return (1);

	if (PCI_CLASS(pa->pa_class) == PCI_CLASS_DISPLAY &&
	    PCI_SUBCLASS(pa->pa_class) == PCI_SUBCLASS_DISPLAY_MISC)
		return (1);

	return (0);
}

void    
vgafbattach(struct device *parent, struct device *self, void *aux)
{
	struct vgafb_softc *sc = (struct vgafb_softc *)self;
	struct pci_attach_args *pa = aux;

	sc->sc_mem_t = pa->pa_memt;
	sc->sc_io_t = pa->pa_iot;
	sc->sc_node = PCITAG_NODE(pa->pa_tag);
	sc->sc_pcitag = pa->pa_tag;

	printf("\n");

	if (vgafb_mapregs(sc, pa))
		return;

	sc->sc_console = vgafb_is_console(sc->sc_node);

	fb_setsize(&sc->sc_sunfb, 8, 1152, 900, sc->sc_node, 0);
	if (sc->sc_sunfb.sf_depth == 24) {
		sc->sc_sunfb.sf_depth = 32;
		sc->sc_sunfb.sf_linebytes =
		    (sc->sc_sunfb.sf_depth / 8) * sc->sc_sunfb.sf_width;
		sc->sc_sunfb.sf_fbsize =
		    sc->sc_sunfb.sf_height * sc->sc_sunfb.sf_linebytes;
	}

	sc->sc_sunfb.sf_ro.ri_bits = (void *)bus_space_vaddr(sc->sc_mem_t,
	    sc->sc_mem_h);
	sc->sc_sunfb.sf_ro.ri_hw = sc;

	fbwscons_init(&sc->sc_sunfb,
	    RI_BSWAP | (sc->sc_console ? 0 : RI_FORCEMONO), sc->sc_console);

	if (sc->sc_console) {
		sc->sc_ofhandle = OF_stdout();
		fbwscons_setcolormap(&sc->sc_sunfb, vgafb_setcolor);
		fbwscons_console_init(&sc->sc_sunfb, -1);
	} else {
		/* sc->sc_ofhandle = PCITAG_NODE(sc->sc_pcitag); */
	}

#ifdef RAMDISK_HOOKS
	if (vga_aperture_needed(pa))
		printf("%s: aperture needed\n", sc->sc_sunfb.sf_dev.dv_xname);
#endif

	fbwscons_attach(&sc->sc_sunfb, &vgafb_accessops, sc->sc_console);
}

int
vgafb_ioctl(void *v, u_long cmd, caddr_t data, int flags, struct proc *p)
{
	struct vgafb_softc *sc = v;
	struct wsdisplay_fbinfo *wdf;
	struct pcisel *sel;

	switch (cmd) {
	case WSDISPLAYIO_GTYPE:
		*(u_int *)data = WSDISPLAY_TYPE_PCIVGA;
		break;
	case WSDISPLAYIO_SMODE:
		sc->sc_mode = *(u_int *)data;
		if (sc->sc_mode == WSDISPLAYIO_MODE_EMUL) {
			if (sc->sc_console)	/* XXX needs sc_ofhandle */
				fbwscons_setcolormap(&sc->sc_sunfb,
				    vgafb_setcolor);
		}
		break;
	case WSDISPLAYIO_GINFO:
		wdf = (void *)data;
		wdf->height = sc->sc_sunfb.sf_height;
		wdf->width  = sc->sc_sunfb.sf_width;
		wdf->depth  = sc->sc_sunfb.sf_depth;
		wdf->stride = sc->sc_sunfb.sf_linebytes;
		wdf->offset = 0;
		wdf->cmsize = 256;
		break;
	case WSDISPLAYIO_GETSUPPORTEDDEPTH:
		if (sc->sc_sunfb.sf_depth == 32)
			*(u_int *)data = WSDISPLAYIO_DEPTH_24_32;
		else
			return (-1);
		break;
	case WSDISPLAYIO_LINEBYTES:
		*(u_int *)data = sc->sc_sunfb.sf_linebytes;
		break;
		
	case WSDISPLAYIO_GETCMAP:
		if (sc->sc_console == 0)
			return (EINVAL);
		return vgafb_getcmap(sc, (struct wsdisplay_cmap *)data);
	case WSDISPLAYIO_PUTCMAP:
		if (sc->sc_console == 0)
			return (EINVAL);
		return vgafb_putcmap(sc, (struct wsdisplay_cmap *)data);

	case WSDISPLAYIO_GPCIID:
		sel = (struct pcisel *)data;
		sel->pc_bus = PCITAG_BUS(sc->sc_pcitag);
		sel->pc_dev = PCITAG_DEV(sc->sc_pcitag);
		sel->pc_func = PCITAG_FUN(sc->sc_pcitag);
		break;

	case WSDISPLAYIO_SVIDEO:
	case WSDISPLAYIO_GVIDEO:
		break;

	case WSDISPLAYIO_GCURPOS:
	case WSDISPLAYIO_SCURPOS:
	case WSDISPLAYIO_GCURMAX:
	case WSDISPLAYIO_GCURSOR:
	case WSDISPLAYIO_SCURSOR:
	default:
		return -1; /* not supported yet */
        }

	return (0);
}

int
vgafb_getcmap(struct vgafb_softc *sc, struct wsdisplay_cmap *cm)
{
	u_int index = cm->index;
	u_int count = cm->count;
	int error;

	if (index >= 256 || count > 256 - index)
		return (EINVAL);

	error = copyout(&sc->sc_cmap_red[index], cm->red, count);
	if (error)
		return (error);
	error = copyout(&sc->sc_cmap_green[index], cm->green, count);
	if (error)
		return (error);
	error = copyout(&sc->sc_cmap_blue[index], cm->blue, count);
	if (error)
		return (error);
	return (0);
}

int
vgafb_putcmap(struct vgafb_softc *sc, struct wsdisplay_cmap *cm)
{
	u_int index = cm->index;
	u_int count = cm->count;
	u_int i;
	int error;
	u_char *r, *g, *b;

	if (index >= 256 || count > 256 - index)
		return (EINVAL);

	if ((error = copyin(cm->red, &sc->sc_cmap_red[index], count)) != 0)
		return (error);
	if ((error = copyin(cm->green, &sc->sc_cmap_green[index], count)) != 0)
		return (error);
	if ((error = copyin(cm->blue, &sc->sc_cmap_blue[index], count)) != 0)
		return (error);

	r = &sc->sc_cmap_red[index];
	g = &sc->sc_cmap_green[index];
	b = &sc->sc_cmap_blue[index];

	for (i = 0; i < count; i++) {
		OF_call_method("color!", sc->sc_ofhandle, 4, 0, *r, *g, *b,
		    index);
		r++, g++, b++, index++;
	}
	return (0);
}

void
vgafb_setcolor(void *v, u_int index, u_int8_t r, u_int8_t g, u_int8_t b)
{
	struct vgafb_softc *sc = v;

	sc->sc_cmap_red[index] = r;
	sc->sc_cmap_green[index] = g;
	sc->sc_cmap_blue[index] = b;
	OF_call_method("color!", sc->sc_ofhandle, 4, 0, r, g, b, index);
}

paddr_t
vgafb_mmap(void *v, off_t off, int prot)
{
	struct vgafb_softc *sc = v;

	if (off & PGOFSET)
		return (-1);

	switch (sc->sc_mode) {
	case WSDISPLAYIO_MODE_MAPPED:
#ifdef APERTURE
		if (allowaperture == 0)
			return (-1);
#endif

		if (sc->sc_mmio_size == 0)
			return (-1);

		if (off >= sc->sc_mem_addr &&
		    off < (sc->sc_mem_addr + sc->sc_mem_size))
			return (bus_space_mmap(sc->sc_mem_t,
			    sc->sc_mem_addr, off - sc->sc_mem_addr,
			    prot, BUS_SPACE_MAP_LINEAR));

		if (off >= sc->sc_mmio_addr &&
		    off < (sc->sc_mmio_addr + sc->sc_mmio_size))
			return (bus_space_mmap(sc->sc_mem_t,
			    sc->sc_mmio_addr, off - sc->sc_mmio_addr,
			    prot, BUS_SPACE_MAP_LINEAR));
		break;

	case WSDISPLAYIO_MODE_DUMBFB:
		if (off >= 0 && off < sc->sc_mem_size)
			return (bus_space_mmap(sc->sc_mem_t, sc->sc_mem_addr,
			    off, prot, BUS_SPACE_MAP_LINEAR));
		break;
	}

	return (-1);
}

int
vgafb_is_console(int node)
{
	extern int fbnode;

	return (fbnode == node);
}

int
vgafb_mapregs(struct vgafb_softc *sc, struct pci_attach_args *pa)
{
	bus_addr_t ba;
	bus_size_t bs;
	int hasio = 0, hasmem = 0, hasmmio = 0; 
	u_int32_t bar, cf;
	int rv;

	for (bar = PCI_MAPREG_START; bar <= PCI_MAPREG_PPB_END; bar += 4) {
		cf = pci_conf_read(pa->pa_pc, pa->pa_tag, bar);
		if (PCI_MAPREG_TYPE(cf) == PCI_MAPREG_TYPE_IO) {
			if (hasio)
				continue;
			rv = pci_mapreg_info(pa->pa_pc, pa->pa_tag, bar,
			    _PCI_MAPREG_TYPEBITS(cf),
			    &sc->sc_io_addr, &sc->sc_io_size, NULL);
			if (rv != 0) {
				if (rv != ENOENT)
					printf("%s: failed to find io at 0x%x\n",
					    sc->sc_sunfb.sf_dev.dv_xname, bar);
				continue;
			}
			hasio = 1;
		} else {
			/* Memory mapping... frame memory or mmio? */
			rv = pci_mapreg_info(pa->pa_pc, pa->pa_tag, bar,
			    _PCI_MAPREG_TYPEBITS(cf), &ba, &bs, NULL);
			if (rv != 0) {
				if (rv != ENOENT)
					printf("%s: failed to find mem at 0x%x\n",
					    sc->sc_sunfb.sf_dev.dv_xname, bar);
				continue;
			}

			if (bs == 0 /* || ba == 0 */) {
				/* ignore this entry */
			} else if (hasmem == 0) {
				/*
				 * first memory slot found goes into memory,
				 * this is for the case of no mmio
				 */
				sc->sc_mem_addr = ba;
				sc->sc_mem_size = bs;
				hasmem = 1;
			} else {
				/*
				 * Oh, we have a second `memory'
				 * region, is this region the vga memory
				 * or mmio, we guess that memory is
				 * the larger of the two.
				 */
				if (sc->sc_mem_size >= bs) {
					/* this is the mmio */
					sc->sc_mmio_addr = ba;
					sc->sc_mmio_size = bs;
					hasmmio = 1;
				} else {
					/* this is the memory */
					sc->sc_mmio_addr = sc->sc_mem_addr;
					sc->sc_mmio_size = sc->sc_mem_size;
					sc->sc_mem_addr = ba;
					sc->sc_mem_size = bs;
				}
			}
			if (PCI_MAPREG_MEM_TYPE(cf) ==
			    PCI_MAPREG_MEM_TYPE_64BIT)
				bar += 4;
		}
	}

	if (hasmem != 0) {
		if (bus_space_map(pa->pa_memt, sc->sc_mem_addr, sc->sc_mem_size,
		    0, &sc->sc_mem_h)) {
			printf("%s: can't map mem space\n",
			    sc->sc_sunfb.sf_dev.dv_xname);
			return (1);
		}
	}

	/* failure to initialize io ports should not prevent attachment */
	if (hasmem == 0) {
		printf("%s: could not find memory space\n",
		    sc->sc_sunfb.sf_dev.dv_xname);
		return (1);
	}

#ifdef DIAGNOSTIC
	if (hasmmio == 0) {
		printf("%s: WARNING: no mmio space configured\n",
		    sc->sc_sunfb.sf_dev.dv_xname);
	}
#endif

	return (0);
}
