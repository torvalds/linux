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

#include <sys/kdb.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/bus.h>
#include <machine/fdt.h>
#include <machine/resource.h>
#include <machine/intr.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/fb/fbreg.h>
#include <dev/syscons/syscons.h>

#include <arm/freescale/imx/imx51_ccmvar.h>

#include <arm/freescale/imx/imx51_ipuv3reg.h>

#define	IMX51_IPU_HSP_CLOCK	665000000
#define	IPU3FB_FONT_HEIGHT	16

struct ipu3sc_softc {
	device_t		dev;
	bus_addr_t		pbase;
	bus_addr_t		vbase;

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

struct video_adapter_softc {
	/* Videoadpater part */
	video_adapter_t	va;

	intptr_t	fb_addr;
	intptr_t	fb_paddr;
	unsigned int	fb_size;

	int		bpp;
	int		depth;
	unsigned int	height;
	unsigned int	width;
	unsigned int	stride;

	unsigned int	xmargin;
	unsigned int	ymargin;

	unsigned char	*font;
	int		initialized;
};

static struct ipu3sc_softc *ipu3sc_softc;
static struct video_adapter_softc va_softc;

/* FIXME: not only 2 bytes color supported */
static uint16_t colors[16] = {
	0x0000,	/* black */
	0x001f,	/* blue */
	0x07e0,	/* green */
	0x07ff,	/* cyan */
	0xf800,	/* red */
	0xf81f,	/* magenta */
	0x3800,	/* brown */
	0xc618,	/* light grey */
	0xc618,	/* XXX: dark grey */
	0x001f,	/* XXX: light blue */
	0x07e0,	/* XXX: light green */
	0x07ff,	/* XXX: light cyan */
	0xf800,	/* XXX: light red */
	0xf81f,	/* XXX: light magenta */
	0xffe0,	/* yellow */
	0xffff,	/* white */
};
static uint32_t colors_24[16] = {
	0x000000,/* Black	*/
	0x000080,/* Blue	*/
	0x008000,/* Green 	*/
	0x008080,/* Cyan 	*/
	0x800000,/* Red 	*/
	0x800080,/* Magenta	*/
	0xcc6600,/* brown	*/
	0xC0C0C0,/* Silver 	*/
	0x808080,/* Gray 	*/
	0x0000FF,/* Light Blue 	*/
	0x00FF00,/* Light Green */
	0x00FFFF,/* Light Cyan 	*/
	0xFF0000,/* Light Red 	*/
	0xFF00FF,/* Light Magenta */
	0xFFFF00,/* Yellow 	*/
	0xFFFFFF,/* White 	*/


};

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

#define	IPUV3_DEBUG 100

#ifdef IPUV3_DEBUG
#define	SUBMOD_DUMP_REG(_sc, _m, _l)					\
	{								\
		int i;							\
		printf("*** " #_m " ***\n");				\
		for (i = 0; i <= (_l); i += 4) {			\
			if ((i % 32) == 0)				\
				printf("%04x: ", i & 0xffff);		\
			printf("0x%08x%c", IPUV3_READ((_sc), _m, i),	\
			    ((i + 4) % 32)?' ':'\n');			\
		}							\
		printf("\n");						\
	}
#endif

#ifdef IPUV3_DEBUG
int ipuv3_debug = IPUV3_DEBUG;
#define	DPRINTFN(n,x)   if (ipuv3_debug>(n)) printf x; else
#else
#define	DPRINTFN(n,x)
#endif

static int	ipu3_fb_probe(device_t);
static int	ipu3_fb_attach(device_t);

static int
ipu3_fb_malloc(struct ipu3sc_softc *sc, size_t size)
{

	sc->vbase = (uint32_t)contigmalloc(size, M_DEVBUF, M_ZERO, 0, ~0,
	    PAGE_SIZE, 0);
	sc->pbase = vtophys(sc->vbase);

	return (0);
}

static void
ipu3_fb_init(void *arg)
{
	struct ipu3sc_softc *sc = arg;
	struct video_adapter_softc *va_sc = &va_softc;
	uint64_t w0sh96;
	uint32_t w1sh96;

	/* FW W0[137:125] - 96 = [41:29] */
	/* FH W0[149:138] - 96 = [53:42] */
	w0sh96 = IPUV3_READ(sc, cpmem, CPMEM_OFFSET(IMX_IPU_DP1, 23, 0, 16));
	w0sh96 <<= 32;
	w0sh96 |= IPUV3_READ(sc, cpmem, CPMEM_OFFSET(IMX_IPU_DP1, 23, 0, 12));

	va_sc->width = ((w0sh96 >> 29) & 0x1fff) + 1;
	va_sc->height = ((w0sh96 >> 42) & 0x0fff) + 1;

	/* SLY W1[115:102] - 96 = [19:6] */
	w1sh96 = IPUV3_READ(sc, cpmem, CPMEM_OFFSET(IMX_IPU_DP1, 23, 1, 12));
	va_sc->stride = ((w1sh96 >> 6) & 0x3fff) + 1;

	printf("%dx%d [%d]\n", va_sc->width, va_sc->height, va_sc->stride);
	va_sc->fb_size = va_sc->height * va_sc->stride;

	ipu3_fb_malloc(sc, va_sc->fb_size);

	/* DP1 + config_ch_23 + word_2 */
	IPUV3_WRITE(sc, cpmem, CPMEM_OFFSET(IMX_IPU_DP1, 23, 1, 0),
	    ((sc->pbase >> 3) | ((sc->pbase >> 3) << 29)) & 0xffffffff);

	IPUV3_WRITE(sc, cpmem, CPMEM_OFFSET(IMX_IPU_DP1, 23, 1, 4),
	    ((sc->pbase >> 3) >> 3) & 0xffffffff);

	va_sc->fb_addr = (intptr_t)sc->vbase;
	va_sc->fb_paddr = (intptr_t)sc->pbase;
	va_sc->bpp = va_sc->stride / va_sc->width;
	va_sc->depth = va_sc->bpp * 8;
}

static int
ipu3_fb_probe(device_t dev)
{
	int error;

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "fsl,ipu3"))
		return (ENXIO);

	device_set_desc(dev, "i.MX5x Image Processing Unit v3 (FB)");

	error = sc_probe_unit(device_get_unit(dev), 
	    device_get_flags(dev) | SC_AUTODETECT_KBD);

	if (error != 0)
		return (error);

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

	if (ipu3sc_softc)
		return (ENXIO);

	ipu3sc_softc = sc;

	if (bootverbose)
		device_printf(dev, "clock gate status is %d\n",
		    imx51_get_clk_gating(IMX51CLK_IPU_HSP_CLK_ROOT));

	sc->dev = dev;

	err = (sc_attach_unit(device_get_unit(dev),
	    device_get_flags(dev) | SC_AUTODETECT_KBD));

	if (err) {
		device_printf(dev, "failed to attach syscons\n");
		goto fail;
	}

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

	return (0);

fail:
	return (ENXIO);
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

static device_method_t ipu3_fb_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ipu3_fb_probe),
	DEVMETHOD(device_attach,	ipu3_fb_attach),

	{ 0, 0 }
};

static devclass_t ipu3_fb_devclass;

static driver_t ipu3_fb_driver = {
	"fb",
	ipu3_fb_methods,
	sizeof(struct ipu3sc_softc),
};

DRIVER_MODULE(ipu3fb, simplebus, ipu3_fb_driver, ipu3_fb_devclass, 0, 0);

/*
 * Video driver routines and glue.
 */
static int			ipu3fb_configure(int);
static vi_probe_t		ipu3fb_probe;
static vi_init_t		ipu3fb_init;
static vi_get_info_t		ipu3fb_get_info;
static vi_query_mode_t		ipu3fb_query_mode;
static vi_set_mode_t		ipu3fb_set_mode;
static vi_save_font_t		ipu3fb_save_font;
static vi_load_font_t		ipu3fb_load_font;
static vi_show_font_t		ipu3fb_show_font;
static vi_save_palette_t	ipu3fb_save_palette;
static vi_load_palette_t	ipu3fb_load_palette;
static vi_set_border_t		ipu3fb_set_border;
static vi_save_state_t		ipu3fb_save_state;
static vi_load_state_t		ipu3fb_load_state;
static vi_set_win_org_t		ipu3fb_set_win_org;
static vi_read_hw_cursor_t	ipu3fb_read_hw_cursor;
static vi_set_hw_cursor_t	ipu3fb_set_hw_cursor;
static vi_set_hw_cursor_shape_t	ipu3fb_set_hw_cursor_shape;
static vi_blank_display_t	ipu3fb_blank_display;
static vi_mmap_t		ipu3fb_mmap;
static vi_ioctl_t		ipu3fb_ioctl;
static vi_clear_t		ipu3fb_clear;
static vi_fill_rect_t		ipu3fb_fill_rect;
static vi_bitblt_t		ipu3fb_bitblt;
static vi_diag_t		ipu3fb_diag;
static vi_save_cursor_palette_t	ipu3fb_save_cursor_palette;
static vi_load_cursor_palette_t	ipu3fb_load_cursor_palette;
static vi_copy_t		ipu3fb_copy;
static vi_putp_t		ipu3fb_putp;
static vi_putc_t		ipu3fb_putc;
static vi_puts_t		ipu3fb_puts;
static vi_putm_t		ipu3fb_putm;

static video_switch_t ipu3fbvidsw = {
	.probe			= ipu3fb_probe,
	.init			= ipu3fb_init,
	.get_info		= ipu3fb_get_info,
	.query_mode		= ipu3fb_query_mode,
	.set_mode		= ipu3fb_set_mode,
	.save_font		= ipu3fb_save_font,
	.load_font		= ipu3fb_load_font,
	.show_font		= ipu3fb_show_font,
	.save_palette		= ipu3fb_save_palette,
	.load_palette		= ipu3fb_load_palette,
	.set_border		= ipu3fb_set_border,
	.save_state		= ipu3fb_save_state,
	.load_state		= ipu3fb_load_state,
	.set_win_org		= ipu3fb_set_win_org,
	.read_hw_cursor		= ipu3fb_read_hw_cursor,
	.set_hw_cursor		= ipu3fb_set_hw_cursor,
	.set_hw_cursor_shape	= ipu3fb_set_hw_cursor_shape,
	.blank_display		= ipu3fb_blank_display,
	.mmap			= ipu3fb_mmap,
	.ioctl			= ipu3fb_ioctl,
	.clear			= ipu3fb_clear,
	.fill_rect		= ipu3fb_fill_rect,
	.bitblt			= ipu3fb_bitblt,
	.diag			= ipu3fb_diag,
	.save_cursor_palette	= ipu3fb_save_cursor_palette,
	.load_cursor_palette	= ipu3fb_load_cursor_palette,
	.copy			= ipu3fb_copy,
	.putp			= ipu3fb_putp,
	.putc			= ipu3fb_putc,
	.puts			= ipu3fb_puts,
	.putm			= ipu3fb_putm,
};

VIDEO_DRIVER(ipu3fb, ipu3fbvidsw, ipu3fb_configure);

extern sc_rndr_sw_t txtrndrsw;
RENDERER(ipu3fb, 0, txtrndrsw, gfb_set);
RENDERER_MODULE(ipu3fb, gfb_set);

static uint16_t ipu3fb_static_window[ROW*COL];
extern u_char dflt_font_16[];

static int
ipu3fb_configure(int flags)
{
	struct video_adapter_softc *sc;

	sc = &va_softc;

	if (sc->initialized)
		return 0;

	sc->width = 640;
	sc->height = 480;
	sc->bpp = 2;
	sc->stride = sc->width * sc->bpp;

	ipu3fb_init(0, &sc->va, 0);

	sc->initialized = 1;

	return (0);
}

static int
ipu3fb_probe(int unit, video_adapter_t **adp, void *arg, int flags)
{

	return (0);
}

static int
ipu3fb_init(int unit, video_adapter_t *adp, int flags)
{
	struct video_adapter_softc *sc;
	video_info_t *vi;

	sc = (struct video_adapter_softc *)adp;
	vi = &adp->va_info;

	vid_init_struct(adp, "ipu3fb", -1, unit);

	sc->font = dflt_font_16;
	vi->vi_cheight = IPU3FB_FONT_HEIGHT;
	vi->vi_cwidth = 8;
	vi->vi_width = sc->width/8;
	vi->vi_height = sc->height/vi->vi_cheight;

	/*
	 * Clamp width/height to syscons maximums
	 */
	if (vi->vi_width > COL)
		vi->vi_width = COL;
	if (vi->vi_height > ROW)
		vi->vi_height = ROW;

	sc->xmargin = (sc->width - (vi->vi_width * vi->vi_cwidth)) / 2;
	sc->ymargin = (sc->height - (vi->vi_height * vi->vi_cheight))/2;

	adp->va_window = (vm_offset_t) ipu3fb_static_window;
	adp->va_flags |= V_ADP_FONT /* | V_ADP_COLOR | V_ADP_MODECHANGE */;
	adp->va_line_width = sc->stride;
	adp->va_buffer_size = sc->fb_size;

	vid_register(&sc->va);

	return (0);
}

static int
ipu3fb_get_info(video_adapter_t *adp, int mode, video_info_t *info)
{

	bcopy(&adp->va_info, info, sizeof(*info));
	return (0);
}

static int
ipu3fb_query_mode(video_adapter_t *adp, video_info_t *info)
{

	return (0);
}

static int
ipu3fb_set_mode(video_adapter_t *adp, int mode)
{

	return (0);
}

static int
ipu3fb_save_font(video_adapter_t *adp, int page, int size, int width,
    u_char *data, int c, int count)
{

	return (0);
}

static int
ipu3fb_load_font(video_adapter_t *adp, int page, int size, int width,
    u_char *data, int c, int count)
{
	struct video_adapter_softc *sc;

	sc = (struct video_adapter_softc *)adp;
	sc->font = data;

	return (0);
}

static int
ipu3fb_show_font(video_adapter_t *adp, int page)
{

	return (0);
}

static int
ipu3fb_save_palette(video_adapter_t *adp, u_char *palette)
{

	return (0);
}

static int
ipu3fb_load_palette(video_adapter_t *adp, u_char *palette)
{

	return (0);
}

static int
ipu3fb_set_border(video_adapter_t *adp, int border)
{

	return (ipu3fb_blank_display(adp, border));
}

static int
ipu3fb_save_state(video_adapter_t *adp, void *p, size_t size)
{

	return (0);
}

static int
ipu3fb_load_state(video_adapter_t *adp, void *p)
{

	return (0);
}

static int
ipu3fb_set_win_org(video_adapter_t *adp, off_t offset)
{

	return (0);
}

static int
ipu3fb_read_hw_cursor(video_adapter_t *adp, int *col, int *row)
{

	*col = *row = 0;
	return (0);
}

static int
ipu3fb_set_hw_cursor(video_adapter_t *adp, int col, int row)
{

	return (0);
}

static int
ipu3fb_set_hw_cursor_shape(video_adapter_t *adp, int base, int height,
    int celsize, int blink)
{

	return (0);
}

static int
ipu3fb_blank_display(video_adapter_t *adp, int mode)
{

	return (0);
}

static int
ipu3fb_mmap(video_adapter_t *adp, vm_ooffset_t offset, vm_paddr_t *paddr,
    int prot, vm_memattr_t *memattr)
{
	struct video_adapter_softc *sc;

	sc = (struct video_adapter_softc *)adp;

	/*
	 * This might be a legacy VGA mem request: if so, just point it at the
	 * framebuffer, since it shouldn't be touched
	 */
	if (offset < sc->stride * sc->height) {
		*paddr = sc->fb_paddr + offset;
		return (0);
	}

	return (EINVAL);
}

static int
ipu3fb_ioctl(video_adapter_t *adp, u_long cmd, caddr_t data)
{
	struct video_adapter_softc *sc;
	struct fbtype *fb;

	sc = (struct video_adapter_softc *)adp;

	switch (cmd) {
	case FBIOGTYPE:
		fb = (struct fbtype *)data;
		fb->fb_type = FBTYPE_PCIMISC;
		fb->fb_height = sc->height;
		fb->fb_width = sc->width;
		fb->fb_depth = sc->depth;
		if (sc->depth <= 1 || sc->depth > 8)
			fb->fb_cmsize = 0;
		else
			fb->fb_cmsize = 1 << sc->depth;
		fb->fb_size = sc->fb_size;
		break;
	case FBIOSCURSOR:
		return (ENODEV);
	default:
		return (fb_commonioctl(adp, cmd, data));
	}

	return (0);
}

static int
ipu3fb_clear(video_adapter_t *adp)
{

	return (ipu3fb_blank_display(adp, 0));
}

static int
ipu3fb_fill_rect(video_adapter_t *adp, int val, int x, int y, int cx, int cy)
{

	return (0);
}

static int
ipu3fb_bitblt(video_adapter_t *adp, ...)
{

	return (0);
}

static int
ipu3fb_diag(video_adapter_t *adp, int level)
{

	return (0);
}

static int
ipu3fb_save_cursor_palette(video_adapter_t *adp, u_char *palette)
{

	return (0);
}

static int
ipu3fb_load_cursor_palette(video_adapter_t *adp, u_char *palette)
{

	return (0);
}

static int
ipu3fb_copy(video_adapter_t *adp, vm_offset_t src, vm_offset_t dst, int n)
{

	return (0);
}

static int
ipu3fb_putp(video_adapter_t *adp, vm_offset_t off, uint32_t p, uint32_t a,
    int size, int bpp, int bit_ltor, int byte_ltor)
{

	return (0);
}

static int
ipu3fb_putc(video_adapter_t *adp, vm_offset_t off, uint8_t c, uint8_t a)
{
	struct video_adapter_softc *sc;
	int col, row, bpp;
	int b, i, j, k;
	uint8_t *addr;
	u_char *p;
	uint32_t fg, bg, color;

	sc = (struct video_adapter_softc *)adp;
	bpp = sc->bpp;

	if (sc->fb_addr == 0)
		return (0);
	row = (off / adp->va_info.vi_width) * adp->va_info.vi_cheight;
	col = (off % adp->va_info.vi_width) * adp->va_info.vi_cwidth;
	p = sc->font + c * IPU3FB_FONT_HEIGHT;
	addr = (uint8_t *)sc->fb_addr
	    + (row + sc->ymargin) * (sc->stride)
	    + bpp * (col + sc->xmargin);

	if (bpp == 2) {
		bg = colors[(a >> 4) & 0x0f];
		fg = colors[a & 0x0f];
	} else if (bpp == 3) {
		bg = colors_24[(a >> 4) & 0x0f];
		fg = colors_24[a & 0x0f];
	} else {
		return (ENXIO);
	}

	for (i = 0; i < IPU3FB_FONT_HEIGHT; i++) {
		for (j = 0, k = 7; j < 8; j++, k--) {
			if ((p[i] & (1 << k)) == 0)
				color = bg;
			else
				color = fg;
			/* FIXME: BPP maybe different */
			for (b = 0; b < bpp; b ++)
				addr[bpp * j + b] =
				    (color >> (b << 3)) & 0xff;
		}

		addr += (sc->stride);
	}

        return (0);
}

static int
ipu3fb_puts(video_adapter_t *adp, vm_offset_t off, u_int16_t *s, int len)
{
	int i;

	for (i = 0; i < len; i++) 
		ipu3fb_putc(adp, off + i, s[i] & 0xff, (s[i] & 0xff00) >> 8);

	return (0);
}

static int
ipu3fb_putm(video_adapter_t *adp, int x, int y, uint8_t *pixel_image,
    uint32_t pixel_mask, int size, int width)
{

	return (0);
}

/*
 * Define a stub keyboard driver in case one hasn't been
 * compiled into the kernel
 */
#include <sys/kbio.h>
#include <dev/kbd/kbdreg.h>

static int dummy_kbd_configure(int flags);

keyboard_switch_t ipu3dummysw;

static int
dummy_kbd_configure(int flags)
{

	return (0);
}
KEYBOARD_DRIVER(ipu3dummy, ipu3dummysw, dummy_kbd_configure);
