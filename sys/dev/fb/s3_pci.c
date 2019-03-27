/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Alcove - Nicolas Souchu <nsouch@freebsd.org>
 * All rights reserved.
 *
 * Code based on Peter Horton <pdh@colonel-panic.com> patch.
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

/* Enable LFB on S3 cards that has only VESA 1.2 BIOS */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <machine/bus.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/pmap.h>

#include <sys/uio.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <sys/malloc.h>
#include <sys/fbio.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <machine/md_var.h>
#include <machine/pc/bios.h>
#include <dev/fb/vesa.h>

#include <dev/fb/fbreg.h>
#include <dev/fb/vgareg.h>

#define S3PCI_DEBUG 1

#define PCI_S3_VENDOR_ID	0x5333

#define S3_CONFIG_IO		0x3c0	/* VGA standard config io ports */
#define S3_CONFIG_IO_SIZE	0x20

#define S3_ENHANCED_IO		0x4ae8	/* Extended config register */
#define S3_ENHANCED_IO_SIZE	1

#define S3_CRTC_ADDR		0x14
#define S3_CRTC_VALUE		0x15

#define PCI_BASE_MEMORY		0x10

#define outb_p(value, offset) bus_space_write_1(sc->st, sc->sh, offset, value)
#define inb_p(offset) (bus_space_read_1(sc->st, sc->sh, offset))
#define outb_enh(value, offset) bus_space_write_1(sc->enh_st, sc->enh_sh, \
								offset, value)
#define inb_enh(offset) (bus_space_read_1(sc->enh_st, sc->enh_sh, offset))

struct s3pci_softc {
	bus_space_tag_t st;
	bus_space_handle_t sh;
	bus_space_tag_t enh_st;
	bus_space_handle_t enh_sh;
	struct resource *port_res;
	struct resource *enh_res;
	struct resource *mem_res;
	u_long mem_base;
	u_long mem_size;
};

static int			s3lfb_error(void);
static vi_probe_t		s3lfb_probe;
static vi_init_t		s3lfb_init;
static vi_get_info_t		s3lfb_get_info;
static vi_query_mode_t		s3lfb_query_mode;
static vi_set_mode_t		s3lfb_set_mode;
static vi_save_font_t		s3lfb_save_font;
static vi_load_font_t		s3lfb_load_font;
static vi_show_font_t		s3lfb_show_font;
static vi_save_palette_t	s3lfb_save_palette;
static vi_load_palette_t	s3lfb_load_palette;
static vi_set_border_t		s3lfb_set_border;
static vi_save_state_t		s3lfb_save_state;
static vi_load_state_t		s3lfb_load_state;
static vi_set_win_org_t		s3lfb_set_origin;
static vi_read_hw_cursor_t	s3lfb_read_hw_cursor;
static vi_set_hw_cursor_t	s3lfb_set_hw_cursor;
static vi_set_hw_cursor_shape_t	s3lfb_set_hw_cursor_shape;
static vi_blank_display_t	s3lfb_blank_display;
static vi_mmap_t		s3lfb_mmap;
static vi_ioctl_t		s3lfb_ioctl;
static vi_clear_t		s3lfb_clear;
static vi_fill_rect_t		s3lfb_fill_rect;
static vi_bitblt_t		s3lfb_bitblt;
static vi_diag_t		s3lfb_diag;

static video_switch_t s3lfbvidsw = {
	s3lfb_probe,
	s3lfb_init,
	s3lfb_get_info,
	s3lfb_query_mode,
	s3lfb_set_mode,
	s3lfb_save_font,
	s3lfb_load_font,
	s3lfb_show_font,
	s3lfb_save_palette,
	s3lfb_load_palette,
	s3lfb_set_border,
	s3lfb_save_state,
	s3lfb_load_state,
	s3lfb_set_origin,
	s3lfb_read_hw_cursor,
	s3lfb_set_hw_cursor,
	s3lfb_set_hw_cursor_shape,
	s3lfb_blank_display,
	s3lfb_mmap,
	s3lfb_ioctl,
	s3lfb_clear,
	s3lfb_fill_rect,
	s3lfb_bitblt,
	s3lfb_error,
	s3lfb_error,
	s3lfb_diag,
};

static video_switch_t *prevvidsw;
static device_t s3pci_dev = NULL;

static int
s3lfb_probe(int unit, video_adapter_t **adpp, void *arg, int flags)
{
	return (*prevvidsw->probe)(unit, adpp, arg, flags);
}

static int
s3lfb_init(int unit, video_adapter_t *adp, int flags)
{
	return (*prevvidsw->init)(unit, adp, flags);
}

static int
s3lfb_get_info(video_adapter_t *adp, int mode, video_info_t *info)
{
#if 0
	device_t dev = s3pci_dev;			/* XXX */
	struct s3pci_softc *sc = (struct s3pci_softc *)device_get_softc(dev);
#endif
	int error;

	if ((error = (*prevvidsw->get_info)(adp, mode, info)))
		return error;

#if 0
	/* Don't use linear addressing with text modes
	 */
	if ((mode > M_VESA_BASE) &&
		(info->vi_flags & V_INFO_GRAPHICS) &&
		!(info->vi_flags & V_INFO_LINEAR)) {

		info->vi_flags |= V_INFO_LINEAR;
		info->vi_buffer = sc->mem_base;

	} else {
		info->vi_buffer = 0;
	}
#endif

	return 0;
}

static int
s3lfb_query_mode(video_adapter_t *adp, video_info_t *info)
{
	return (*prevvidsw->query_mode)(adp, info);
}

static vm_offset_t
s3lfb_map_buffer(u_int paddr, size_t size)
{
	vm_offset_t vaddr;
	u_int off;

	off = paddr - trunc_page(paddr);
	vaddr = (vm_offset_t)pmap_mapdev(paddr - off, size + off);

	return (vaddr + off);
}

static int
s3lfb_set_mode(video_adapter_t *adp, int mode)
{
	device_t dev = s3pci_dev;			/* XXX */
	struct s3pci_softc *sc = (struct s3pci_softc *)device_get_softc(dev);
#if 0
	unsigned char tmp;
#endif
	int error;

	/* First, set the mode as if it was a classic VESA card
	 */
	if ((error = (*prevvidsw->set_mode)(adp, mode)))
		return error;

	/* If not in a linear mode (according to s3lfb_get_info() called
	 * by vesa_set_mode in the (*vidsw[adp->va_index]->get_info)...
	 * sequence, return with no error
	 */
#if 0
	if (!(adp->va_info.vi_flags & V_INFO_LINEAR))
		return 0;
#endif

	if ((mode <= M_VESA_BASE) ||
		!(adp->va_info.vi_flags & V_INFO_GRAPHICS) ||
		(adp->va_info.vi_flags & V_INFO_LINEAR))
		return 0;

	/* Ok, now apply the configuration to the card */

	outb_p(0x38, S3_CRTC_ADDR); outb_p(0x48, S3_CRTC_VALUE);
	outb_p(0x39, S3_CRTC_ADDR); outb_p(0xa5, S3_CRTC_VALUE);
       
       /* check that CR47 is read/write */
       
#if 0
	outb_p(0x47, S3_CRTC_ADDR); outb_p(0xff, S3_CRTC_VALUE);
	tmp = inb_p(S3_CRTC_VALUE);
	outb_p(0x00, S3_CRTC_VALUE);
	if ((tmp != 0xff) || (inb_p(S3_CRTC_VALUE)))
	{
		/* lock S3 registers */

		outb_p(0x39, S3_CRTC_ADDR); outb_p(0x5a, S3_CRTC_VALUE);
		outb_p(0x38, S3_CRTC_ADDR); outb_p(0x00, S3_CRTC_VALUE);

		return ENXIO;
	}
#endif

	/* enable enhanced register access */

	outb_p(0x40, S3_CRTC_ADDR);
	outb_p(inb_p(S3_CRTC_VALUE) | 1, S3_CRTC_VALUE);

	/* enable enhanced functions */

	outb_enh(inb_enh(0) | 1, 0x0);

	/* enable enhanced mode memory mapping */

	outb_p(0x31, S3_CRTC_ADDR);
	outb_p(inb_p(S3_CRTC_VALUE) | 8, S3_CRTC_VALUE);

	/* enable linear frame buffer and set address window to max */

	outb_p(0x58, S3_CRTC_ADDR);
	outb_p(inb_p(S3_CRTC_VALUE) | 0x13, S3_CRTC_VALUE);

	/* disabled enhanced register access */

	outb_p(0x40, S3_CRTC_ADDR);
	outb_p(inb_p(S3_CRTC_VALUE) & ~1, S3_CRTC_VALUE);

	/* lock S3 registers */

	outb_p(0x39, S3_CRTC_ADDR); outb_p(0x5a, S3_CRTC_VALUE);
	outb_p(0x38, S3_CRTC_ADDR); outb_p(0x00, S3_CRTC_VALUE);

	adp->va_info.vi_flags |= V_INFO_LINEAR;
	adp->va_info.vi_buffer = sc->mem_base;
	adp->va_buffer = s3lfb_map_buffer(adp->va_info.vi_buffer,
				adp->va_info.vi_buffer_size);
	adp->va_buffer_size = adp->va_info.vi_buffer_size;
	adp->va_window = adp->va_buffer;
	adp->va_window_size = adp->va_info.vi_buffer_size/adp->va_info.vi_planes;
	adp->va_window_gran = adp->va_info.vi_buffer_size/adp->va_info.vi_planes;

	return 0;
}

static int
s3lfb_save_font(video_adapter_t *adp, int page, int fontsize, int fontwidth,
	       u_char *data, int ch, int count)
{
	return (*prevvidsw->save_font)(adp, page, fontsize, fontwidth, data,
		ch, count);
}

static int
s3lfb_load_font(video_adapter_t *adp, int page, int fontsize, int fontwidth,
	       u_char *data, int ch, int count)
{
	return (*prevvidsw->load_font)(adp, page, fontsize, fontwidth, data,
		ch, count);
}

static int
s3lfb_show_font(video_adapter_t *adp, int page)
{
	return (*prevvidsw->show_font)(adp, page);
}

static int
s3lfb_save_palette(video_adapter_t *adp, u_char *palette)
{
	return (*prevvidsw->save_palette)(adp, palette);
}

static int
s3lfb_load_palette(video_adapter_t *adp, u_char *palette)
{
	return (*prevvidsw->load_palette)(adp, palette);
}

static int
s3lfb_set_border(video_adapter_t *adp, int color)
{
	return (*prevvidsw->set_border)(adp, color);
}

static int
s3lfb_save_state(video_adapter_t *adp, void *p, size_t size)
{
	return (*prevvidsw->save_state)(adp, p, size);
}

static int
s3lfb_load_state(video_adapter_t *adp, void *p)
{
	return (*prevvidsw->load_state)(adp, p);
}

static int
s3lfb_set_origin(video_adapter_t *adp, off_t offset)
{
	return (*prevvidsw->set_win_org)(adp, offset);
}

static int
s3lfb_read_hw_cursor(video_adapter_t *adp, int *col, int *row)
{
	return (*prevvidsw->read_hw_cursor)(adp, col, row);
}

static int
s3lfb_set_hw_cursor(video_adapter_t *adp, int col, int row)
{
	return (*prevvidsw->set_hw_cursor)(adp, col, row);
}

static int
s3lfb_set_hw_cursor_shape(video_adapter_t *adp, int base, int height,
			 int celsize, int blink)
{
	return (*prevvidsw->set_hw_cursor_shape)(adp, base, height,
			celsize, blink);
}

static int
s3lfb_blank_display(video_adapter_t *adp, int mode) 
{
	return (*prevvidsw->blank_display)(adp, mode);
}

static int
s3lfb_mmap(video_adapter_t *adp, vm_ooffset_t offset, vm_paddr_t *paddr,
	  int prot, vm_memattr_t *memattr)
{
	return (*prevvidsw->mmap)(adp, offset, paddr, prot, memattr);
}

static int
s3lfb_clear(video_adapter_t *adp)
{
	return (*prevvidsw->clear)(adp);
}

static int
s3lfb_fill_rect(video_adapter_t *adp, int val, int x, int y, int cx, int cy)
{
	return (*prevvidsw->fill_rect)(adp, val, x, y, cx, cy);
}

static int
s3lfb_bitblt(video_adapter_t *adp,...)
{
	return (*prevvidsw->bitblt)(adp);		/* XXX */
}

static int
s3lfb_ioctl(video_adapter_t *adp, u_long cmd, caddr_t arg)
{
	return (*prevvidsw->ioctl)(adp, cmd, arg);
}

static int
s3lfb_diag(video_adapter_t *adp, int level)
{
	return (*prevvidsw->diag)(adp, level);
}

static int
s3lfb_error(void)
{
	return 1;
}

/***********************************/
/* PCI detection/attachement stuff */
/***********************************/

static int
s3pci_probe(device_t dev)
{
	u_int32_t vendor, class, subclass, device_id;

	device_id = pci_get_devid(dev);
	vendor = device_id & 0xffff;
	class = pci_get_class(dev);
	subclass = pci_get_subclass(dev);

	if ((class != PCIC_DISPLAY) || (subclass != PCIS_DISPLAY_VGA) ||
		(vendor != PCI_S3_VENDOR_ID))
		return ENXIO;

	device_set_desc(dev, "S3 graphic card");

	bus_set_resource(dev, SYS_RES_IOPORT, 0,
				S3_CONFIG_IO, S3_CONFIG_IO_SIZE);
	bus_set_resource(dev, SYS_RES_IOPORT, 1,
				S3_ENHANCED_IO, S3_ENHANCED_IO_SIZE);

	return BUS_PROBE_DEFAULT;

};

static int
s3pci_attach(device_t dev)
{
	struct s3pci_softc* sc = (struct s3pci_softc*)device_get_softc(dev);
	video_adapter_t *adp;

#if 0
	unsigned char tmp;
#endif
	int rid, i;

	if (s3pci_dev) {
		printf("%s: driver already attached!\n", __func__);
		goto error;
	}

	/* Allocate resources
	 */
	rid = 0;
	if (!(sc->port_res = bus_alloc_resource_any(dev, SYS_RES_IOPORT, &rid,
				RF_ACTIVE | RF_SHAREABLE))) {
		printf("%s: port resource allocation failed!\n", __func__);
		goto error;
	}
	sc->st = rman_get_bustag(sc->port_res);
	sc->sh = rman_get_bushandle(sc->port_res);

	rid = 1;
	if (!(sc->enh_res = bus_alloc_resource_any(dev, SYS_RES_IOPORT, &rid,
				RF_ACTIVE | RF_SHAREABLE))) {
		printf("%s: enhanced port resource allocation failed!\n",
			__func__);
		goto error;
	}
	sc->enh_st = rman_get_bustag(sc->enh_res);
	sc->enh_sh = rman_get_bushandle(sc->enh_res);

	rid = PCI_BASE_MEMORY;
	if (!(sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
				 RF_ACTIVE))) {

		printf("%s: mem resource allocation failed!\n", __func__);
		goto error;
	}

	/* The memory base address will be our LFB base address
	 */
	/* sc->mem_base = (u_long)rman_get_virtual(sc->mem_res); */
	sc->mem_base = bus_get_resource_start(dev, SYS_RES_MEMORY, rid);
	sc->mem_size = bus_get_resource_count(dev, SYS_RES_MEMORY, rid);

	/* Attach the driver to the VGA/VESA framework
	 */
	for (i = 0; (adp = vid_get_adapter(i)) != NULL; ++i) {
		if (adp->va_type == KD_VGA)
			break;
	}

	/* If the VESA module hasn't been loaded, or VGA doesn't
	 * exist, abort
	 */
	if ((adp == NULL) || !(adp->va_flags & V_ADP_VESA)) {
		printf("%s: VGA adapter not found or VESA module not loaded!\n",
			__func__);
		goto error;
	}

	/* Replace the VESA video switch by owers
	 */
	prevvidsw = vidsw[adp->va_index];
	vidsw[adp->va_index] = &s3lfbvidsw;

	/* Remember who we are on the bus */
	s3pci_dev = (void *)dev;			/* XXX */

	return 0;

error:
	if (sc->mem_res)
		bus_release_resource(dev, SYS_RES_MEMORY, PCI_BASE_MEMORY, sc->mem_res);

	if (sc->enh_res)
		bus_release_resource(dev, SYS_RES_IOPORT, 1, sc->enh_res);

	if (sc->port_res)
		bus_release_resource(dev, SYS_RES_IOPORT, 0, sc->port_res);

	return ENXIO;
};

static device_method_t s3pci_methods[] = {

        DEVMETHOD(device_probe, s3pci_probe),
        DEVMETHOD(device_attach, s3pci_attach),
        {0,0}
};

static driver_t s3pci_driver = {
	"s3pci",
	s3pci_methods,
	sizeof(struct s3pci_softc),
};

static devclass_t s3pci_devclass;

DRIVER_MODULE(s3pci, pci, s3pci_driver, s3pci_devclass, 0, 0);
