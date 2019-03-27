/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Aleksandr Rybalko under sponsorship from the
 * FreeBSD Foundation.
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
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/fbio.h>

#include "opt_platform.h"

#ifdef	FDT
#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/ofw_pci.h>
#include <machine/fdt.h>
#endif

#include <dev/vt/vt.h>
#include <dev/vt/hw/fb/vt_fb.h>
#include <dev/vt/colors/vt_termcolors.h>

static vd_init_t vt_efb_init;
static vd_probe_t vt_efb_probe;

static struct vt_driver vt_fb_early_driver = {
	.vd_name = "efb",
	.vd_probe = vt_efb_probe,
	.vd_init = vt_efb_init,
	.vd_blank = vt_fb_blank,
	.vd_bitblt_text = vt_fb_bitblt_text,
	.vd_invalidate_text = vt_fb_invalidate_text,
	.vd_bitblt_bmp = vt_fb_bitblt_bitmap,
	.vd_drawrect = vt_fb_drawrect,
	.vd_setpixel = vt_fb_setpixel,
	.vd_priority = VD_PRIORITY_GENERIC,
};

static struct fb_info local_info;
VT_DRIVER_DECLARE(vt_efb, vt_fb_early_driver);

static void
#ifdef	FDT
vt_efb_initialize(struct fb_info *info, phandle_t node)
#else
vt_efb_initialize(struct fb_info *info)
#endif
{
#ifdef	FDT
	char name[64];
	cell_t retval;
	ihandle_t ih;
	int i;

	/* Open display device, thereby initializing it */
	memset(name, 0, sizeof(name));
	OF_package_to_path(node, name, sizeof(name));
	ih = OF_open(name);
#endif

	/*
	 * Set up the color map
	 */
	switch (info->fb_depth) {
	case 8:
		vt_generate_cons_palette(info->fb_cmap, COLOR_FORMAT_RGB,
		    0x7, 5, 0x7, 2, 0x3, 0);
		break;
	case 15:
		vt_generate_cons_palette(info->fb_cmap, COLOR_FORMAT_RGB,
		    0x1f, 10, 0x1f, 5, 0x1f, 0);
		break;
	case 16:
		vt_generate_cons_palette(info->fb_cmap, COLOR_FORMAT_RGB,
		    0x1f, 11, 0x3f, 5, 0x1f, 0);
		break;
	case 24:
	case 32:
#if BYTE_ORDER == BIG_ENDIAN
		vt_generate_cons_palette(info->fb_cmap,
		    COLOR_FORMAT_RGB, 255, 0, 255, 8, 255, 16);
#else
		vt_generate_cons_palette(info->fb_cmap,
		    COLOR_FORMAT_RGB, 255, 16, 255, 8, 255, 0);
#endif
#ifdef	FDT
		for (i = 0; i < 16; i++) {
			OF_call_method("color!", ih, 4, 1,
			    (cell_t)((info->fb_cmap[i] >> 16) & 0xff),
			    (cell_t)((info->fb_cmap[i] >> 8) & 0xff),
			    (cell_t)((info->fb_cmap[i] >> 0) & 0xff),
			    (cell_t)i, &retval);
		}
#endif
		break;

	default:
		panic("Unknown color space fb_depth %d", info->fb_depth);
		break;
	}
}

static phandle_t
vt_efb_get_fbnode()
{
	phandle_t chosen, node;
	ihandle_t stdout;
	char type[64];

	chosen = OF_finddevice("/chosen");
	OF_getprop(chosen, "stdout", &stdout, sizeof(stdout));
	node = OF_instance_to_package(stdout);
	if (node != -1) {
		/* The "/chosen/stdout" present. */
		OF_getprop(node, "device_type", type, sizeof(type));
		/* Check if it has "display" type. */
		if (strcmp(type, "display") == 0)
			return (node);
	}
	/* Try device with name "screen". */
	node = OF_finddevice("screen");

	return (node);
}

static int
vt_efb_probe(struct vt_device *vd)
{
	phandle_t node;

	node = vt_efb_get_fbnode();
	if (node == -1)
		return (CN_DEAD);

	if ((OF_getproplen(node, "height") <= 0) ||
	    (OF_getproplen(node, "width") <= 0) ||
	    (OF_getproplen(node, "depth") <= 0) ||
	    (OF_getproplen(node, "linebytes") <= 0))
		return (CN_DEAD);

	return (CN_INTERNAL);
}

static int
vt_efb_init(struct vt_device *vd)
{
	struct ofw_pci_register pciaddrs[8];
	struct fb_info *info;
	int i, len, n_pciaddrs;
	phandle_t node;

	if (vd->vd_softc == NULL)
		vd->vd_softc = (void *)&local_info;

	info = vd->vd_softc;

	node = vt_efb_get_fbnode();
	if (node == -1)
		return (CN_DEAD);

#define	GET(name, var)							\
	if (OF_getproplen(node, (name)) != sizeof(info->fb_##var))	\
		return (CN_DEAD);					\
	OF_getencprop(node, (name), &info->fb_##var, sizeof(info->fb_##var)); \
	if (info->fb_##var == 0)					\
		return (CN_DEAD);

	GET("height", height)
	GET("width", width)
	GET("depth", depth)
	GET("linebytes", stride)
#undef GET

	info->fb_size = info->fb_height * info->fb_stride;

	/*
	 * Get the PCI addresses of the adapter, if present. The node may be the
	 * child of the PCI device: in that case, try the parent for
	 * the assigned-addresses property.
	 */
	len = OF_getprop(node, "assigned-addresses", pciaddrs,
	    sizeof(pciaddrs));
	if (len == -1) {
		len = OF_getprop(OF_parent(node), "assigned-addresses",
		    pciaddrs, sizeof(pciaddrs));
	}
	if (len == -1)
		len = 0;
	n_pciaddrs = len / sizeof(struct ofw_pci_register);

	/*
	 * Grab the physical address of the framebuffer, and then map it
	 * into our memory space. If the MMU is not yet up, it will be
	 * remapped for us when relocation turns on.
	 */
	if (OF_getproplen(node, "address") == sizeof(info->fb_pbase)) {
		/* XXX We assume #address-cells is 1 at this point. */
		OF_getencprop(node, "address", &info->fb_pbase,
		    sizeof(info->fb_pbase));

	#if defined(__powerpc__)
		sc->sc_memt = &bs_be_tag;
		bus_space_map(sc->sc_memt, info->fb_pbase, info->fb_size,
		    BUS_SPACE_MAP_PREFETCHABLE, &info->fb_vbase);
	#elif defined(__sparc64__)
		OF_decode_addr(node, 0, &space, &phys);
		sc->sc_memt = &vt_efb_memt[0];
		info->addr = sparc64_fake_bustag(space, fb_phys, sc->sc_memt);
	#else
		bus_space_map(fdtbus_bs_tag, info->fb_pbase, info->fb_size,
		    BUS_SPACE_MAP_PREFETCHABLE,
		    (bus_space_handle_t *)&info->fb_vbase);
	#endif
	} else {
		/*
		 * Some IBM systems don't have an address property. Try to
		 * guess the framebuffer region from the assigned addresses.
		 * This is ugly, but there doesn't seem to be an alternative.
		 * Linux does the same thing.
		 */

		info->fb_pbase = n_pciaddrs;
		for (i = 0; i < n_pciaddrs; i++) {
			/* If it is too small, not the framebuffer */
			if (pciaddrs[i].size_lo < info->fb_size)
				continue;
			/* If it is not memory, it isn't either */
			if (!(pciaddrs[i].phys_hi &
			    OFW_PCI_PHYS_HI_SPACE_MEM32))
				continue;

			/* This could be the framebuffer */
			info->fb_pbase = i;

			/* If it is prefetchable, it certainly is */
			if (pciaddrs[i].phys_hi & OFW_PCI_PHYS_HI_PREFETCHABLE)
				break;
		}

		if (info->fb_pbase == n_pciaddrs) /* No candidates found */
			return (CN_DEAD);

	#if defined(__powerpc__)
		OF_decode_addr(node, info->fb_pbase, &sc->sc_memt,
		    &info->fb_vbase);
	#elif defined(__sparc64__)
		OF_decode_addr(node, info->fb_pbase, &space, &info->fb_pbase);
		sc->sc_memt = &vt_efb_memt[0];
		info->fb_vbase = sparc64_fake_bustag(space, info->fb_pbase,
		    sc->sc_memt);
	#else
		bus_space_map(fdtbus_bs_tag, info->fb_pbase, info->fb_size,
		    BUS_SPACE_MAP_PREFETCHABLE,
		    (bus_space_handle_t *)&info->fb_vbase);
	#endif
	}

	/* blank full size */
	len = info->fb_size / 4;
	for (i = 0; i < len; i++) {
		((uint32_t *)info->fb_vbase)[i] = 0;
	}

	/* Get pixel storage size. */
	info->fb_bpp = info->fb_stride / info->fb_width * 8;

#ifdef	FDT
	vt_efb_initialize(info, node);
#else
	vt_efb_initialize(info);
#endif
	vt_fb_init(vd);

	return (CN_INTERNAL);
}
