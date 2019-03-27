/*-
 * Copyright 2013-2014 John Wehle <john@feith.com>
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

/*
 * Amlogic aml8726 frame buffer driver.
 *
 * The current implementation has limited flexibility.
 * For example only progressive scan is supported when
 * using HDMI and the resolution / frame rate is not
 * negotiated.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/resource.h>
#include <sys/rman.h>

#include <sys/fbio.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/fdt.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/fb/fbreg.h>
#include <dev/vt/vt.h>

#include <arm/amlogic/aml8726/aml8726_fb.h>

#include "fb_if.h"


enum aml8726_fb_output {
	aml8726_unknown_fb_output,
	aml8726_cvbs_fb_output,
	aml8726_hdmi_fb_output,
	aml8726_lcd_fb_output
};

struct aml8726_fb_clk {
	uint32_t	freq;
	uint32_t	video_pre;
	uint32_t	video_post;
	uint32_t	video_x;
	uint32_t	hdmi_tx;
	uint32_t	encp;
	uint32_t	enci;
	uint32_t	enct;
	uint32_t	encl;
	uint32_t	vdac0;
	uint32_t	vdac1;
};

struct aml8726_fb_softc {
	device_t		dev;
	struct resource		*res[4];
	struct mtx		mtx;
	void			*ih_cookie;
	struct fb_info		info;
	enum aml8726_fb_output	output;
	struct aml8726_fb_clk	clk;
};

static struct resource_spec aml8726_fb_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },	/* CANVAS */
	{ SYS_RES_MEMORY,	1,	RF_ACTIVE },	/* VIU */
	{ SYS_RES_MEMORY,	2,	RF_ACTIVE },	/* VPP */
	{ SYS_RES_IRQ,		1,	RF_ACTIVE },	/* INT_VIU_VSYNC */
	{ -1, 0 }
};

#define	AML_FB_LOCK(sc)			mtx_lock(&(sc)->mtx)
#define	AML_FB_UNLOCK(sc)		mtx_unlock(&(sc)->mtx)
#define	AML_FB_LOCK_INIT(sc)		\
    mtx_init(&(sc)->mtx, device_get_nameunit((sc)->dev),	\
    "fb", MTX_DEF)
#define	AML_FB_LOCK_DESTROY(sc)		mtx_destroy(&(sc)->mtx);

#define	CAV_WRITE_4(sc, reg, val)	bus_write_4((sc)->res[0], reg, (val))
#define	CAV_READ_4(sc, reg)		bus_read_4((sc)->res[0], reg)
#define	CAV_BARRIER(sc, reg)		bus_barrier((sc)->res[0], reg, 4, \
    (BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE))

#define	VIU_WRITE_4(sc, reg, val)	bus_write_4((sc)->res[1], reg, (val))
#define	VIU_READ_4(sc, reg)		bus_read_4((sc)->res[1], reg)

#define	VPP_WRITE_4(sc, reg, val)	bus_write_4((sc)->res[2], reg, (val))
#define	VPP_READ_4(sc, reg)		bus_read_4((sc)->res[2], reg)

#define	CLK_WRITE_4(sc, reg, val)	bus_write_4((sc)->res[X], reg, (val))
#define	CLK_READ_4(sc, reg)		bus_read_4((sc)->res[X], reg)

#define	AML_FB_CLK_FREQ_SD		1080
#define	AML_FB_CLK_FREQ_HD		1488

static void
aml8726_fb_cfg_output(struct aml8726_fb_softc *sc)
{
	/* XXX */
}

static void
aml8726_fb_cfg_video(struct aml8726_fb_softc *sc)
{
	uint32_t value;

	/*
	 * basic initialization
	 *
	 * The fifo depth is in units of 8 so programming 32
	 * sets the depth to 256.
	 */

	value = (32 << AML_VIU_OSD_FIFO_CTRL_DEPTH_SHIFT);
	value |= AML_VIU_OSD_FIFO_CTRL_BURST_LEN_64;
	value |= (4 << AML_VIU_OSD_FIFO_CTRL_HOLD_LINES_SHIFT);

	VIU_WRITE_4(sc, AML_VIU_OSD1_FIFO_CTRL_REG, value);
	VIU_WRITE_4(sc, AML_VIU_OSD2_FIFO_CTRL_REG, value);

	value = VPP_READ_4(sc, AML_VPP_MISC_REG);

	value &= ~AML_VPP_MISC_PREBLEND_EN;
	value |= AML_VPP_MISC_POSTBLEND_EN;
	value &= ~(AML_VPP_MISC_OSD1_POSTBLEND | AML_VPP_MISC_OSD2_POSTBLEND
	    | AML_VPP_MISC_VD1_POSTBLEND | AML_VPP_MISC_VD2_POSTBLEND);

	VPP_WRITE_4(sc, AML_VPP_MISC_REG, value);

	value = AML_VIU_OSD_CTRL_OSD_EN;
	value |= (0xff << AML_VIU_OSD_CTRL_GLOBAL_ALPHA_SHIFT);

	VIU_WRITE_4(sc, AML_VIU_OSD1_CTRL_REG, value);
	VIU_WRITE_4(sc, AML_VIU_OSD2_CTRL_REG, value);

	/* color mode for OSD1 block 0 */

	value = (AML_CAV_OSD1_INDEX << AML_VIU_OSD_BLK_CFG_W0_INDEX_SHIFT)
	    | AML_VIU_OSD_BLK_CFG_W0_LITTLE_ENDIAN
	    | AML_VIU_OSD_BLK_CFG_W0_BLKMODE_24
	    | AML_VIU_OSD_BLK_CFG_W0_RGB_EN
	    | AML_VIU_OSD_BLK_CFG_W0_CMATRIX_RGB;

	VIU_WRITE_4(sc, AML_VIU_OSD1_BLK0_CFG_W0_REG, value);

	/* geometry / scaling for OSD1 block 0 */

	value = ((sc->info.fb_width - 1) << AML_VIU_OSD_BLK_CFG_W1_X_END_SHIFT)
	    & AML_VIU_OSD_BLK_CFG_W1_X_END_MASK;
	value |= (0 << AML_VIU_OSD_BLK_CFG_W1_X_START_SHIFT)
	    & AML_VIU_OSD_BLK_CFG_W1_X_START_MASK;

	VIU_WRITE_4(sc, AML_VIU_OSD1_BLK0_CFG_W1_REG, value);

	value = ((sc->info.fb_height - 1) << AML_VIU_OSD_BLK_CFG_W2_Y_END_SHIFT)
	    & AML_VIU_OSD_BLK_CFG_W2_Y_END_MASK;
	value |= (0 << AML_VIU_OSD_BLK_CFG_W2_Y_START_SHIFT)
	    & AML_VIU_OSD_BLK_CFG_W2_Y_START_MASK;

	VIU_WRITE_4(sc, AML_VIU_OSD1_BLK0_CFG_W2_REG, value);

	value = ((sc->info.fb_width - 1) << AML_VIU_OSD_BLK_CFG_W3_H_END_SHIFT)
	    & AML_VIU_OSD_BLK_CFG_W3_H_END_MASK;
	value |= (0 << AML_VIU_OSD_BLK_CFG_W3_H_START_SHIFT)
	    & AML_VIU_OSD_BLK_CFG_W3_H_START_MASK;

	VIU_WRITE_4(sc, AML_VIU_OSD1_BLK0_CFG_W3_REG, value);

	value = ((sc->info.fb_height - 1) << AML_VIU_OSD_BLK_CFG_W4_V_END_SHIFT)
	    & AML_VIU_OSD_BLK_CFG_W4_V_END_MASK;
	value |= (0 << AML_VIU_OSD_BLK_CFG_W4_V_START_SHIFT)
	    & AML_VIU_OSD_BLK_CFG_W4_V_START_MASK;

	VIU_WRITE_4(sc, AML_VIU_OSD1_BLK0_CFG_W4_REG, value);

	/* Enable the OSD block now that it's fully configured */

	value = VIU_READ_4(sc, AML_VIU_OSD1_CTRL_REG);

	value &= ~AML_VIU_OSD_CTRL_OSD_BLK_EN_MASK;
	value |= 1 << AML_VIU_OSD_CTRL_OSD_BLK_EN_SHIFT;

	VIU_WRITE_4(sc, AML_VIU_OSD1_CTRL_REG, value);

	/* enable video processing of OSD1 */

	value = VPP_READ_4(sc, AML_VPP_MISC_REG);

	value |= AML_VPP_MISC_OSD1_POSTBLEND;

	VPP_WRITE_4(sc, AML_VPP_MISC_REG, value);
}

static void
aml8726_fb_cfg_canvas(struct aml8726_fb_softc *sc)
{
	uint32_t value;
	uint32_t width;

	/*
	 * The frame buffer address and width are programmed in units of 8
	 * (meaning they need to be aligned and the actual values divided
	 * by 8 prior to programming the hardware).
	 */

	width = (uint32_t)sc->info.fb_stride / 8;

	/* lower bits of the width */
	value = (width << AML_CAV_LUT_DATAL_WIDTH_SHIFT) &
	    AML_CAV_LUT_DATAL_WIDTH_MASK;

	/* physical address */
	value |= (uint32_t)sc->info.fb_pbase / 8;

	CAV_WRITE_4(sc, AML_CAV_LUT_DATAL_REG, value);

	/* upper bits of the width */
	value = ((width >> AML_CAV_LUT_DATAL_WIDTH_WIDTH) <<
	    AML_CAV_LUT_DATAH_WIDTH_SHIFT) & AML_CAV_LUT_DATAH_WIDTH_MASK;

	/* height */
	value |= ((uint32_t)sc->info.fb_height <<
	    AML_CAV_LUT_DATAH_HEIGHT_SHIFT) & AML_CAV_LUT_DATAH_HEIGHT_MASK;

	/* mode */
	value |= AML_CAV_LUT_DATAH_BLKMODE_LINEAR;

	CAV_WRITE_4(sc, AML_CAV_LUT_DATAH_REG, value);

	CAV_WRITE_4(sc, AML_CAV_LUT_ADDR_REG, (AML_CAV_LUT_ADDR_WR_EN |
	    (AML_CAV_OSD1_INDEX << AML_CAV_LUT_ADDR_INDEX_SHIFT)));

	CAV_BARRIER(sc, AML_CAV_LUT_ADDR_REG);
}

static void
aml8726_fb_intr(void *arg)
{
	struct aml8726_fb_softc *sc = (struct aml8726_fb_softc *)arg;

	AML_FB_LOCK(sc);

	AML_FB_UNLOCK(sc);
}

static int
aml8726_fb_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "amlogic,aml8726-fb"))
		return (ENXIO);

	device_set_desc(dev, "Amlogic aml8726 FB");

	return (BUS_PROBE_DEFAULT);
}

static int
aml8726_fb_attach(device_t dev)
{
	struct aml8726_fb_softc *sc = device_get_softc(dev);
	int error;
	device_t child;
	pcell_t prop;
	phandle_t node;

	sc->dev = dev;

	sc->info.fb_name = device_get_nameunit(sc->dev);

	node = ofw_bus_get_node(dev);

	if (OF_getencprop(node, "width", &prop, sizeof(prop)) <= 0) {
		device_printf(dev, "missing width attribute in FDT\n");
		return (ENXIO);
	}
	if ((prop % 8) != 0) {
		device_printf(dev,
		    "width attribute in FDT must be a multiple of 8\n");
		return (ENXIO);
	}
	sc->info.fb_width = prop;

	if (OF_getencprop(node, "height", &prop, sizeof(prop)) <= 0) {
		device_printf(dev, "missing height attribute in FDT\n");
		return (ENXIO);
	}
	sc->info.fb_height = prop;

	if (OF_getencprop(node, "depth", &prop, sizeof(prop)) <= 0) {
		device_printf(dev, "missing depth attribute in FDT\n");
		return (ENXIO);
	}
	if (prop != 24) {
		device_printf(dev,
		    "depth attribute in FDT is an unsupported value\n");
		return (ENXIO);
	}
	sc->info.fb_depth = prop;
	sc->info.fb_bpp = prop;

	if (OF_getencprop(node, "linebytes", &prop, sizeof(prop)) <= 0) {
		device_printf(dev, "missing linebytes attribute in FDT\n");
		return (ENXIO);
	}
	if ((prop % 8) != 0) {
		device_printf(dev,
		    "linebytes attribute in FDT must be a multiple of 8\n");
		return (ENXIO);
	}
	if (prop < (sc->info.fb_width * 3)) {
		device_printf(dev,
		    "linebytes attribute in FDT is too small\n");
		return (ENXIO);
	}
	sc->info.fb_stride = prop;

	if (OF_getencprop(node, "address", &prop, sizeof(prop)) <= 0) {
		device_printf(dev, "missing address attribute in FDT\n");
		return (ENXIO);
	}
	if ((prop % 8) != 0) {
		device_printf(dev,
		    "address attribute in FDT must be a multiple of 8\n");
		return (ENXIO);
	}
	sc->info.fb_pbase = prop;
	sc->info.fb_size = sc->info.fb_height * sc->info.fb_stride;
	sc->info.fb_vbase = (intptr_t)pmap_mapdev(sc->info.fb_pbase,
	    sc->info.fb_size);

	if (bus_alloc_resources(dev, aml8726_fb_spec, sc->res)) {
		device_printf(dev, "could not allocate resources for device\n");
		pmap_unmapdev(sc->info.fb_vbase, sc->info.fb_size);
		return (ENXIO);
	}

	aml8726_fb_cfg_output(sc);

	aml8726_fb_cfg_video(sc);

	aml8726_fb_cfg_canvas(sc);

	AML_FB_LOCK_INIT(sc);

	error = bus_setup_intr(dev, sc->res[3], INTR_TYPE_MISC | INTR_MPSAFE,
	    NULL, aml8726_fb_intr, sc, &sc->ih_cookie);

	if (error) {
		device_printf(dev, "could not setup interrupt handler\n");
		goto fail;
	}

	child = device_add_child(dev, "fbd", device_get_unit(dev));

	if (!child) {
		device_printf(dev, "could not add fbd\n");
		error = ENXIO;
		goto fail;
	}

	error = device_probe_and_attach(child);

	if (error) {
		device_printf(dev, "could not attach fbd\n");
		goto fail;
	}

	return (0);

fail:
	if (sc->ih_cookie)
		bus_teardown_intr(dev, sc->res[3], sc->ih_cookie);

	AML_FB_LOCK_DESTROY(sc);

	bus_release_resources(dev, aml8726_fb_spec, sc->res);

	pmap_unmapdev(sc->info.fb_vbase, sc->info.fb_size);

	return (error);
}

static int
aml8726_fb_detach(device_t dev)
{
	struct aml8726_fb_softc *sc = device_get_softc(dev);

	bus_generic_detach(dev);

	bus_teardown_intr(dev, sc->res[3], sc->ih_cookie);

	AML_FB_LOCK_DESTROY(sc);

	bus_release_resources(dev, aml8726_fb_spec, sc->res);

	pmap_unmapdev(sc->info.fb_vbase, sc->info.fb_size);

	return (0);
}

static struct fb_info *
aml8726_fb_getinfo(device_t dev)
{
	struct aml8726_fb_softc *sc = device_get_softc(dev);

	return (&sc->info);
}

static device_method_t aml8726_fb_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		aml8726_fb_probe),
	DEVMETHOD(device_attach,	aml8726_fb_attach),
	DEVMETHOD(device_detach,	aml8726_fb_detach),

	/* FB interface */
	DEVMETHOD(fb_getinfo,		aml8726_fb_getinfo),

	DEVMETHOD_END
};

static driver_t aml8726_fb_driver = {
	"fb",
	aml8726_fb_methods,
	sizeof(struct aml8726_fb_softc),
};

static devclass_t aml8726_fb_devclass;

DRIVER_MODULE(fb, simplebus, aml8726_fb_driver, aml8726_fb_devclass, 0, 0);
