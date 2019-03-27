/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Oleksandr Tymoshenko <gonzo@freebsd.org>
 * Copyright (c) 2012, 2013 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Oleksandr Rybalko
 * under sponsorship from the FreeBSD Foundation.
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
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bio.h>
#include <sys/bus.h>
#include <sys/fbio.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/fb/fbreg.h>
#include <dev/vt/vt.h>
#include <dev/vt/colors/vt_termcolors.h>

#include <arm/broadcom/bcm2835/bcm2835_mbox_prop.h>

#include "fb_if.h"
#include "mbox_if.h"

#define	FB_DEPTH		24

struct bcmsc_softc {
	struct fb_info 			info;
	int				fbswap;
	struct bcm2835_fb_config	fb;
	device_t			dev;
};

static struct ofw_compat_data compat_data[] = {
	{"broadcom,bcm2835-fb",		1},
	{"brcm,bcm2708-fb",		1},
	{NULL,				0}
};

static int bcm_fb_probe(device_t);
static int bcm_fb_attach(device_t);

static int
bcm_fb_init(struct bcmsc_softc *sc, struct bcm2835_fb_config *fb)
{
	int err;

	err = 0;

	memset(fb, 0, sizeof(*fb));
	if (bcm2835_mbox_fb_get_w_h(fb) != 0)
		return (ENXIO);
	fb->bpp = FB_DEPTH;

	fb->vxres = fb->xres;
	fb->vyres = fb->yres;
	fb->xoffset = fb->yoffset = 0;

	if ((err = bcm2835_mbox_fb_init(fb)) != 0) {
		device_printf(sc->dev, "bcm2835_mbox_fb_init failed, err=%d\n", err);
		return (ENXIO);
	}

	return (0);
}

static int
bcm_fb_setup_fbd(struct bcmsc_softc *sc)
{
	struct bcm2835_fb_config fb;
	device_t fbd;
	int err;

	err = bcm_fb_init(sc, &fb);
	if (err)
		return (err);

	memset(&sc->info, 0, sizeof(sc->info));
	sc->info.fb_name = device_get_nameunit(sc->dev);

	sc->info.fb_vbase = (intptr_t)pmap_mapdev(fb.base, fb.size);
	sc->info.fb_pbase = fb.base;
	sc->info.fb_size = fb.size;
	sc->info.fb_bpp = sc->info.fb_depth = fb.bpp;
	sc->info.fb_stride = fb.pitch;
	sc->info.fb_width = fb.xres;
	sc->info.fb_height = fb.yres;
#ifdef VM_MEMATTR_WRITE_COMBINING
	sc->info.fb_flags = FB_FLAG_MEMATTR;
	sc->info.fb_memattr = VM_MEMATTR_WRITE_COMBINING;
#endif

	if (sc->fbswap) {
		switch (sc->info.fb_bpp) {
		case 24:
			vt_generate_cons_palette(sc->info.fb_cmap,
			    COLOR_FORMAT_RGB, 0xff, 0, 0xff, 8, 0xff, 16);
			sc->info.fb_cmsize = 16;
			break;
		case 32:
			vt_generate_cons_palette(sc->info.fb_cmap,
			    COLOR_FORMAT_RGB, 0xff, 16, 0xff, 8, 0xff, 0);
			sc->info.fb_cmsize = 16;
			break;
		}
	}

	fbd = device_add_child(sc->dev, "fbd", device_get_unit(sc->dev));
	if (fbd == NULL) {
		device_printf(sc->dev, "Failed to add fbd child\n");
		pmap_unmapdev(sc->info.fb_vbase, sc->info.fb_size);
		return (ENXIO);
	} else if (device_probe_and_attach(fbd) != 0) {
		device_printf(sc->dev, "Failed to attach fbd device\n");
		device_delete_child(sc->dev, fbd);
		pmap_unmapdev(sc->info.fb_vbase, sc->info.fb_size);
		return (ENXIO);
	}

	device_printf(sc->dev, "%dx%d(%dx%d@%d,%d) %dbpp\n", fb.xres, fb.yres,
	    fb.vxres, fb.vyres, fb.xoffset, fb.yoffset, fb.bpp);
	device_printf(sc->dev,
	    "fbswap: %d, pitch %d, base 0x%08x, screen_size %d\n",
	    sc->fbswap, fb.pitch, fb.base, fb.size);

	return (0);
}

static int
bcm_fb_resync_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct bcmsc_softc *sc = arg1;
	struct bcm2835_fb_config fb;
	int val;
	int err;

	val = 0;
	err = sysctl_handle_int(oidp, &val, 0, req);
	if (err || !req->newptr) /* error || read request */
		return (err);

	bcm_fb_init(sc, &fb);

	return (0);
}

static void
bcm_fb_sysctl_init(struct bcmsc_softc *sc)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree_node;
	struct sysctl_oid_list *tree;

	/*
	 * Add system sysctl tree/handlers.
	 */
	ctx = device_get_sysctl_ctx(sc->dev);
	tree_node = device_get_sysctl_tree(sc->dev);
	tree = SYSCTL_CHILDREN(tree_node);
	SYSCTL_ADD_PROC(ctx, tree, OID_AUTO, "resync",
	    CTLFLAG_RW | CTLTYPE_UINT, sc, sizeof(*sc),
	    bcm_fb_resync_sysctl, "IU", "Set to resync framebuffer with VC");
}

static int
bcm_fb_probe(device_t dev)
{

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "BCM2835 VT framebuffer driver");

	return (BUS_PROBE_DEFAULT);
}

static int
bcm_fb_attach(device_t dev)
{
	char bootargs[2048], *n, *p, *v;
	int err;
	phandle_t chosen;
	struct bcmsc_softc *sc;

	sc = device_get_softc(dev);
	sc->dev = dev;

	/* Newer firmware versions needs an inverted color palette. */
	sc->fbswap = 0;
	chosen = OF_finddevice("/chosen");
	if (chosen != -1 &&
	    OF_getprop(chosen, "bootargs", &bootargs, sizeof(bootargs)) > 0) {
		p = bootargs;
		while ((v = strsep(&p, " ")) != NULL) {
			if (*v == '\0')
				continue;
			n = strsep(&v, "=");
			if (strcmp(n, "bcm2708_fb.fbswap") == 0 && v != NULL)
				if (*v == '1')
					sc->fbswap = 1;
                }
        }

	bcm_fb_sysctl_init(sc);

	err = bcm_fb_setup_fbd(sc);
	if (err)
		return (err);

	return (0);
}

static struct fb_info *
bcm_fb_helper_getinfo(device_t dev)
{
	struct bcmsc_softc *sc;

	sc = device_get_softc(dev);

	return (&sc->info);
}

static device_method_t bcm_fb_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		bcm_fb_probe),
	DEVMETHOD(device_attach,	bcm_fb_attach),

	/* Framebuffer service methods */
	DEVMETHOD(fb_getinfo,		bcm_fb_helper_getinfo),

	DEVMETHOD_END
};

static devclass_t bcm_fb_devclass;

static driver_t bcm_fb_driver = {
	"fb",
	bcm_fb_methods,
	sizeof(struct bcmsc_softc),
};

DRIVER_MODULE(bcm2835fb, ofwbus, bcm_fb_driver, bcm_fb_devclass, 0, 0);
DRIVER_MODULE(bcm2835fb, simplebus, bcm_fb_driver, bcm_fb_devclass, 0, 0);
