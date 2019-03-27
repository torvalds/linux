/*-
 * Copyright (c) 2014 Ed Maste
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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
#include <sys/bus.h>
#include <sys/fbio.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/terasic/mtl/terasic_mtl.h>

#include <dev/vt/colors/vt_termcolors.h>

/*
 * Terasic Multitouch LCD (MTL) vt(4) framebuffer driver.
 */

#include <vm/vm.h>
#include <vm/pmap.h>

static int
terasic_mtl_fbd_panel_info(struct terasic_mtl_softc *sc, struct fb_info *info)
{
	phandle_t node;
	pcell_t dts_value[2];
	int len;

	if ((node = ofw_bus_get_node(sc->mtl_dev)) == -1)
		return (ENXIO);

	/* panel size */
	if ((len = OF_getproplen(node, "panel-size")) != sizeof(dts_value))
		return (ENXIO);
	OF_getencprop(node, "panel-size", dts_value, len);
	info->fb_width = dts_value[0];
	info->fb_height = dts_value[1];
	info->fb_bpp = info->fb_depth = 32;
	info->fb_stride = info->fb_width * (info->fb_depth / 8);

	/*
	 * Safety belt to ensure framebuffer params are as expected.  May be
	 * removed when we have full confidence in fdt / hints params.
	 */
	if (info->fb_width != TERASIC_MTL_FB_WIDTH ||
	    info->fb_height != TERASIC_MTL_FB_HEIGHT ||
	    info->fb_stride != 3200 ||
	    info->fb_bpp != 32 || info->fb_depth != 32) {
		device_printf(sc->mtl_dev,
		    "rejecting invalid panel params width=%u height=%u\n",
		    (unsigned)info->fb_width, (unsigned)info->fb_height);
		return (EINVAL);
	}

	return (0);
}

int
terasic_mtl_fbd_attach(struct terasic_mtl_softc *sc)
{
	struct fb_info *info;
	device_t fbd;

	info = &sc->mtl_fb_info;
	info->fb_name = device_get_nameunit(sc->mtl_dev);
	info->fb_pbase = rman_get_start(sc->mtl_pixel_res);
	info->fb_size = rman_get_size(sc->mtl_pixel_res);
	info->fb_vbase = (intptr_t)pmap_mapdev(info->fb_pbase, info->fb_size);
	if (terasic_mtl_fbd_panel_info(sc, info) != 0) {
		device_printf(sc->mtl_dev, "using default panel params\n");
		info->fb_bpp = info->fb_depth = 32;
		info->fb_width = 800;
		info->fb_height = 480;
		info->fb_stride = info->fb_width * (info->fb_depth / 8);
	}

	fbd = device_add_child(sc->mtl_dev, "fbd",
	    device_get_unit(sc->mtl_dev));
	if (fbd == NULL) {
		device_printf(sc->mtl_dev, "Failed to attach fbd child\n");
		return (ENXIO);
	}
	if (device_probe_and_attach(fbd) != 0) {
		device_printf(sc->mtl_dev,
		    "Failed to attach fbd device\n");
		return (ENXIO);
	}
	return (0);
}

void
terasic_mtl_fbd_detach(struct terasic_mtl_softc *sc)
{
	panic("%s: detach not implemented", __func__);
}

extern device_t fbd_driver;
extern devclass_t fbd_devclass;
DRIVER_MODULE(fbd, terasic_mtl, fbd_driver, fbd_devclass, 0, 0);
