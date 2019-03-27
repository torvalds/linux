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
#include <sys/conf.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/resource.h>
#include <sys/rman.h>
#include <sys/time.h>
#include <sys/timetc.h>
#include <sys/fbio.h>
#include <sys/consio.h>
#include <sys/eventhandler.h>

#include <sys/kdb.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <machine/frame.h>
#include <machine/intr.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/vt/vt.h>
#include <dev/vt/colors/vt_termcolors.h>

#include <arm/freescale/imx/imx51_ccmvar.h>

#include <arm/freescale/imx/imx51_ipuv3reg.h>

#include "fb_if.h"

#define	IMX51_IPU_HSP_CLOCK	665000000

struct ipu3sc_softc {
	device_t		dev;
	device_t		sc_fbd;		/* fbd child */
	struct fb_info		sc_info;

	bus_space_tag_t		iot;
	bus_space_handle_t	ioh;
	bus_space_handle_t	cm_ioh;
	bus_space_handle_t	dp_ioh;
	bus_space_handle_t	di0_ioh;
	bus_space_handle_t	di1_ioh;
	bus_space_handle_t	dctmpl_ioh;
	bus_space_handle_t	dc_ioh;
	bus_space_handle_t	dmfc_ioh;
	bus_space_handle_t	idmac_ioh;
	bus_space_handle_t	cpmem_ioh;
};

static struct ipu3sc_softc *ipu3sc_softc;

#define	IPUV3_READ(ipuv3, module, reg)					\
	bus_space_read_4((ipuv3)->iot, (ipuv3)->module##_ioh, (reg))
#define	IPUV3_WRITE(ipuv3, module, reg, val)				\
	bus_space_write_4((ipuv3)->iot, (ipuv3)->module##_ioh, (reg), (val))

#define	CPMEM_CHANNEL_OFFSET(_c)	((_c) * 0x40)
#define	CPMEM_WORD_OFFSET(_w)		((_w) * 0x20)
#define	CPMEM_DP_OFFSET(_d)		((_d) * 0x10000)
#define	IMX_IPU_DP0		0
#define	IMX_IPU_DP1		1
#define	CPMEM_CHANNEL(_dp, _ch, _w)					\
	    (CPMEM_DP_OFFSET(_dp) + CPMEM_CHANNEL_OFFSET(_ch) +		\
		CPMEM_WORD_OFFSET(_w))
#define	CPMEM_OFFSET(_dp, _ch, _w, _o)					\
	    (CPMEM_CHANNEL((_dp), (_ch), (_w)) + (_o))

static int	ipu3_fb_probe(device_t);
static int	ipu3_fb_attach(device_t);

static void
ipu3_fb_init(struct ipu3sc_softc *sc)
{
	uint64_t w0sh96;
	uint32_t w1sh96;

	/* FW W0[137:125] - 96 = [41:29] */
	/* FH W0[149:138] - 96 = [53:42] */
	w0sh96 = IPUV3_READ(sc, cpmem, CPMEM_OFFSET(IMX_IPU_DP1, 23, 0, 16));
	w0sh96 <<= 32;
	w0sh96 |= IPUV3_READ(sc, cpmem, CPMEM_OFFSET(IMX_IPU_DP1, 23, 0, 12));

	sc->sc_info.fb_width = ((w0sh96 >> 29) & 0x1fff) + 1;
	sc->sc_info.fb_height = ((w0sh96 >> 42) & 0x0fff) + 1;

	/* SLY W1[115:102] - 96 = [19:6] */
	w1sh96 = IPUV3_READ(sc, cpmem, CPMEM_OFFSET(IMX_IPU_DP1, 23, 1, 12));
	sc->sc_info.fb_stride = ((w1sh96 >> 6) & 0x3fff) + 1;

	printf("%dx%d [%d]\n", sc->sc_info.fb_width, sc->sc_info.fb_height,
	    sc->sc_info.fb_stride);
	sc->sc_info.fb_size = sc->sc_info.fb_height * sc->sc_info.fb_stride;

	sc->sc_info.fb_vbase = (intptr_t)contigmalloc(sc->sc_info.fb_size,
	    M_DEVBUF, M_ZERO, 0, ~0, PAGE_SIZE, 0);
	sc->sc_info.fb_pbase = (intptr_t)vtophys(sc->sc_info.fb_vbase);

	/* DP1 + config_ch_23 + word_2 */
	IPUV3_WRITE(sc, cpmem, CPMEM_OFFSET(IMX_IPU_DP1, 23, 1, 0),
	    (((uint32_t)sc->sc_info.fb_pbase >> 3) |
	    (((uint32_t)sc->sc_info.fb_pbase >> 3) << 29)) & 0xffffffff);

	IPUV3_WRITE(sc, cpmem, CPMEM_OFFSET(IMX_IPU_DP1, 23, 1, 4),
	    (((uint32_t)sc->sc_info.fb_pbase >> 3) >> 3) & 0xffffffff);

	/* XXX: fetch or set it from/to IPU. */
	sc->sc_info.fb_bpp = sc->sc_info.fb_depth = sc->sc_info.fb_stride /
	    sc->sc_info.fb_width * 8;
}

/* Use own color map, because of different RGB offset. */
static int
ipu3_fb_init_cmap(uint32_t *cmap, int bytespp)
{

	switch (bytespp) {
	case 8:
		return (vt_generate_cons_palette(cmap, COLOR_FORMAT_RGB,
		    0x7, 5, 0x7, 2, 0x3, 0));
	case 15:
		return (vt_generate_cons_palette(cmap, COLOR_FORMAT_RGB,
		    0x1f, 10, 0x1f, 5, 0x1f, 0));
	case 16:
		return (vt_generate_cons_palette(cmap, COLOR_FORMAT_RGB,
		    0x1f, 11, 0x3f, 5, 0x1f, 0));
	case 24:
	case 32: /* Ignore alpha. */
		return (vt_generate_cons_palette(cmap, COLOR_FORMAT_RGB,
		    0xff, 0, 0xff, 8, 0xff, 16));
	default:
		return (1);
	}
}

static int
ipu3_fb_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "fsl,ipu3"))
		return (ENXIO);

	device_set_desc(dev, "i.MX5x Image Processing Unit v3 (FB)");

	return (BUS_PROBE_DEFAULT);
}

static int
ipu3_fb_attach(device_t dev)
{
	struct ipu3sc_softc *sc = device_get_softc(dev);
	bus_space_tag_t iot;
	bus_space_handle_t ioh;
	phandle_t node;
	pcell_t reg;
 	int err;
	uintptr_t base;

	ipu3sc_softc = sc;

	if (bootverbose)
		device_printf(dev, "clock gate status is %d\n",
		    imx51_get_clk_gating(IMX51CLK_IPU_HSP_CLK_ROOT));

	sc->dev = dev;

	sc = device_get_softc(dev);
	sc->iot = iot = fdtbus_bs_tag;

	/*
	 * Retrieve the device address based on the start address in the
	 * DTS.  The DTS for i.MX51 specifies 0x5e000000 as the first register
	 * address, so we just subtract IPU_CM_BASE to get the offset at which
	 * the IPU device was memory mapped.
	 * On i.MX53, the offset is 0.
	 */
	node = ofw_bus_get_node(dev);
	if ((OF_getencprop(node, "reg", &reg, sizeof(reg))) <= 0)
		base = 0;
	else
		base = reg - IPU_CM_BASE(0);
	/* map controller registers */
	err = bus_space_map(iot, IPU_CM_BASE(base), IPU_CM_SIZE, 0, &ioh);
	if (err)
		goto fail_retarn_cm;
	sc->cm_ioh = ioh;

	/* map Display Multi FIFO Controller registers */
	err = bus_space_map(iot, IPU_DMFC_BASE(base), IPU_DMFC_SIZE, 0, &ioh);
	if (err)
		goto fail_retarn_dmfc;
	sc->dmfc_ioh = ioh;

	/* map Display Interface 0 registers */
	err = bus_space_map(iot, IPU_DI0_BASE(base), IPU_DI0_SIZE, 0, &ioh);
	if (err)
		goto fail_retarn_di0;
	sc->di0_ioh = ioh;

	/* map Display Interface 1 registers */
	err = bus_space_map(iot, IPU_DI1_BASE(base), IPU_DI0_SIZE, 0, &ioh);
	if (err)
		goto fail_retarn_di1;
	sc->di1_ioh = ioh;

	/* map Display Processor registers */
	err = bus_space_map(iot, IPU_DP_BASE(base), IPU_DP_SIZE, 0, &ioh);
	if (err)
		goto fail_retarn_dp;
	sc->dp_ioh = ioh;

	/* map Display Controller registers */
	err = bus_space_map(iot, IPU_DC_BASE(base), IPU_DC_SIZE, 0, &ioh);
	if (err)
		goto fail_retarn_dc;
	sc->dc_ioh = ioh;

	/* map Image DMA Controller registers */
	err = bus_space_map(iot, IPU_IDMAC_BASE(base), IPU_IDMAC_SIZE, 0,
	    &ioh);
	if (err)
		goto fail_retarn_idmac;
	sc->idmac_ioh = ioh;

	/* map CPMEM registers */
	err = bus_space_map(iot, IPU_CPMEM_BASE(base), IPU_CPMEM_SIZE, 0,
	    &ioh);
	if (err)
		goto fail_retarn_cpmem;
	sc->cpmem_ioh = ioh;

	/* map DCTEMPL registers */
	err = bus_space_map(iot, IPU_DCTMPL_BASE(base), IPU_DCTMPL_SIZE, 0,
	    &ioh);
	if (err)
		goto fail_retarn_dctmpl;
	sc->dctmpl_ioh = ioh;

#ifdef notyet
	sc->ih = imx51_ipuv3_intr_establish(IMX51_INT_IPUV3, IPL_BIO,
	    ipuv3intr, sc);
	if (sc->ih == NULL) {
		device_printf(sc->dev,
		    "unable to establish interrupt at irq %d\n",
		    IMX51_INT_IPUV3);
		return (ENXIO);
	}
#endif

	/*
	 * We have to wait until interrupts are enabled. 
	 * Mailbox relies on it to get data from VideoCore
	 */
	ipu3_fb_init(sc);

	sc->sc_info.fb_name = device_get_nameunit(dev);

	ipu3_fb_init_cmap(sc->sc_info.fb_cmap, sc->sc_info.fb_depth);
	sc->sc_info.fb_cmsize = 16;

	/* Ask newbus to attach framebuffer device to me. */
	sc->sc_fbd = device_add_child(dev, "fbd", device_get_unit(dev));
	if (sc->sc_fbd == NULL)
		device_printf(dev, "Can't attach fbd device\n");

	return (bus_generic_attach(dev));

fail_retarn_dctmpl:
	bus_space_unmap(sc->iot, sc->cpmem_ioh, IPU_CPMEM_SIZE);
fail_retarn_cpmem:
	bus_space_unmap(sc->iot, sc->idmac_ioh, IPU_IDMAC_SIZE);
fail_retarn_idmac:
	bus_space_unmap(sc->iot, sc->dc_ioh, IPU_DC_SIZE);
fail_retarn_dp:
	bus_space_unmap(sc->iot, sc->dp_ioh, IPU_DP_SIZE);
fail_retarn_dc:
	bus_space_unmap(sc->iot, sc->di1_ioh, IPU_DI1_SIZE);
fail_retarn_di1:
	bus_space_unmap(sc->iot, sc->di0_ioh, IPU_DI0_SIZE);
fail_retarn_di0:
	bus_space_unmap(sc->iot, sc->dmfc_ioh, IPU_DMFC_SIZE);
fail_retarn_dmfc:
	bus_space_unmap(sc->iot, sc->dc_ioh, IPU_CM_SIZE);
fail_retarn_cm:
	device_printf(sc->dev,
	    "failed to map registers (errno=%d)\n", err);
	return (err);
}

static struct fb_info *
ipu3_fb_getinfo(device_t dev)
{
	struct ipu3sc_softc *sc = device_get_softc(dev);

	return (&sc->sc_info);
}

static device_method_t ipu3_fb_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ipu3_fb_probe),
	DEVMETHOD(device_attach,	ipu3_fb_attach),

	/* Framebuffer service methods */
	DEVMETHOD(fb_getinfo,		ipu3_fb_getinfo),
	{ 0, 0 }
};

static devclass_t ipu3_fb_devclass;

static driver_t ipu3_fb_driver = {
	"fb",
	ipu3_fb_methods,
	sizeof(struct ipu3sc_softc),
};

DRIVER_MODULE(fb, simplebus, ipu3_fb_driver, ipu3_fb_devclass, 0, 0);
