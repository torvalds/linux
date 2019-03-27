/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998 Kazutaka YOKOTA and Michael Smith
 * Copyright (c) 2009-2013 Jung-uk Kim <jkim@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_vga.h"
#include "opt_vesa.h"

#ifndef VGA_NO_MODE_CHANGE

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/fbio.h>
#include <sys/sysctl.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>

#include <machine/pc/bios.h>
#include <dev/fb/vesa.h>

#include <dev/fb/fbreg.h>
#include <dev/fb/vgareg.h>

#include <dev/pci/pcivar.h>

#include <isa/isareg.h>

#include <compat/x86bios/x86bios.h>

#define	VESA_BIOS_OFFSET	0xc0000
#define	VESA_PALETTE_SIZE	(256 * 4)
#define	VESA_VIA_CLE266		"VIA CLE266\r\n"

#ifndef VESA_DEBUG
#define VESA_DEBUG	0
#endif

/* VESA video adapter state buffer stub */
struct adp_state {
	int		sig;
#define V_STATE_SIG	0x61736576
	u_char		regs[1];
};
typedef struct adp_state adp_state_t;

static struct mtx vesa_lock;
MTX_SYSINIT(vesa_lock, &vesa_lock, "VESA lock", MTX_DEF);

static int vesa_state;
static void *vesa_state_buf;
static uint32_t vesa_state_buf_offs;
static size_t vesa_state_buf_size;

static u_char *vesa_palette;
static uint32_t vesa_palette_offs;

static void *vesa_vmem_buf;
static size_t vesa_vmem_max;

static void *vesa_bios;
static uint32_t vesa_bios_offs;
static uint32_t vesa_bios_int10;
static size_t vesa_bios_size;

/* VESA video adapter */
static video_adapter_t *vesa_adp;

static SYSCTL_NODE(_debug, OID_AUTO, vesa, CTLFLAG_RD, NULL, "VESA debugging");
static int vesa_shadow_rom;
SYSCTL_INT(_debug_vesa, OID_AUTO, shadow_rom, CTLFLAG_RDTUN, &vesa_shadow_rom,
    0, "Enable video BIOS shadow");

/* VESA functions */
#if 0
static int			vesa_nop(void);
#endif
static int			vesa_error(void);
static vi_probe_t		vesa_probe;
static vi_init_t		vesa_init;
static vi_get_info_t		vesa_get_info;
static vi_query_mode_t		vesa_query_mode;
static vi_set_mode_t		vesa_set_mode;
static vi_save_font_t		vesa_save_font;
static vi_load_font_t		vesa_load_font;
static vi_show_font_t		vesa_show_font;
static vi_save_palette_t	vesa_save_palette;
static vi_load_palette_t	vesa_load_palette;
static vi_set_border_t		vesa_set_border;
static vi_save_state_t		vesa_save_state;
static vi_load_state_t		vesa_load_state;
static vi_set_win_org_t		vesa_set_origin;
static vi_read_hw_cursor_t	vesa_read_hw_cursor;
static vi_set_hw_cursor_t	vesa_set_hw_cursor;
static vi_set_hw_cursor_shape_t	vesa_set_hw_cursor_shape;
static vi_blank_display_t	vesa_blank_display;
static vi_mmap_t		vesa_mmap;
static vi_ioctl_t		vesa_ioctl;
static vi_clear_t		vesa_clear;
static vi_fill_rect_t		vesa_fill_rect;
static vi_bitblt_t		vesa_bitblt;
static vi_diag_t		vesa_diag;
static int			vesa_bios_info(int level);

static video_switch_t vesavidsw = {
	vesa_probe,
	vesa_init,
	vesa_get_info,
	vesa_query_mode,
	vesa_set_mode,
	vesa_save_font,
	vesa_load_font,
	vesa_show_font,
	vesa_save_palette,
	vesa_load_palette,
	vesa_set_border,
	vesa_save_state,
	vesa_load_state,
	vesa_set_origin,
	vesa_read_hw_cursor,
	vesa_set_hw_cursor,
	vesa_set_hw_cursor_shape,
	vesa_blank_display,
	vesa_mmap,
	vesa_ioctl,
	vesa_clear,
	vesa_fill_rect,
	vesa_bitblt,
	vesa_error,
	vesa_error,
	vesa_diag,
};

static video_switch_t *prevvidsw;

/* VESA BIOS video modes */
#define VESA_MAXMODES	64
#define EOT		(-1)
#define NA		(-2)

#define MODE_TABLE_DELTA 8

static int vesa_vmode_max;
static video_info_t *vesa_vmode;

static int vesa_init_done;
static struct vesa_info *vesa_adp_info;
static u_int16_t *vesa_vmodetab;
static char *vesa_oemstr;
static char *vesa_venderstr;
static char *vesa_prodstr;
static char *vesa_revstr;

/* local macros and functions */
#define BIOS_SADDRTOLADDR(p) ((((p) & 0xffff0000) >> 12) + ((p) & 0x0000ffff))

static int int10_set_mode(int mode);
static int vesa_bios_post(void);
static int vesa_bios_get_mode(int mode, struct vesa_mode *vmode, int flags);
static int vesa_bios_set_mode(int mode);
#if 0
static int vesa_bios_get_dac(void);
#endif
static int vesa_bios_set_dac(int bits);
static int vesa_bios_save_palette(int start, int colors, u_char *palette,
				  int bits);
static int vesa_bios_save_palette2(int start, int colors, u_char *r, u_char *g,
				   u_char *b, int bits);
static int vesa_bios_load_palette(int start, int colors, u_char *palette,
				  int bits);
static int vesa_bios_load_palette2(int start, int colors, u_char *r, u_char *g,
				   u_char *b, int bits);
#define STATE_SIZE	0
#define STATE_SAVE	1
#define STATE_LOAD	2
static size_t vesa_bios_state_buf_size(int);
static int vesa_bios_save_restore(int code, void *p);
#ifdef MODE_TABLE_BROKEN
static int vesa_bios_get_line_length(void);
#endif
static int vesa_bios_set_line_length(int pixel, int *bytes, int *lines);
#if 0
static int vesa_bios_get_start(int *x, int *y);
#endif
static int vesa_bios_set_start(int x, int y);
static int vesa_map_gen_mode_num(int type, int color, int mode);
static int vesa_translate_flags(u_int16_t vflags);
static int vesa_translate_mmodel(u_int8_t vmodel);
static int vesa_get_bpscanline(struct vesa_mode *vmode);
static int vesa_bios_init(void);
static void vesa_bios_uninit(void);
static void vesa_clear_modes(video_info_t *info, int color);

#if 0
static int vesa_get_origin(video_adapter_t *adp, off_t *offset);
#endif

/* INT 10 BIOS calls */
static int
int10_set_mode(int mode)
{
	x86regs_t regs;

	x86bios_init_regs(&regs);
	regs.R_AL = mode;

	x86bios_intr(&regs, 0x10);

	return (0);
}

static int
vesa_bios_post(void)
{
	x86regs_t regs;
	devclass_t dc;
	device_t *devs;
	device_t dev;
	int count, i, is_pci;

	if (x86bios_get_orm(vesa_bios_offs) == NULL)
		return (1);

	dev = NULL;
	is_pci = 0;

	/* Find the matching PCI video controller. */
	dc = devclass_find("vgapci");
	if (dc != NULL && devclass_get_devices(dc, &devs, &count) == 0) {
		for (i = 0; i < count; i++)
			if (device_get_flags(devs[i]) != 0 &&
			    x86bios_match_device(vesa_bios_offs, devs[i])) {
				dev = devs[i];
				is_pci = 1;
				break;
			}
		free(devs, M_TEMP);
	}

	/* Try VGA if a PCI device is not found. */
	if (dev == NULL) {
		dc = devclass_find(VGA_DRIVER_NAME);
		if (dc != NULL)
			dev = devclass_get_device(dc, 0);
	}

	if (bootverbose)
		printf("%s: calling BIOS POST\n",
		    dev == NULL ? "VESA" : device_get_nameunit(dev));

	x86bios_init_regs(&regs);
	if (is_pci) {
		regs.R_AH = pci_get_bus(dev);
		regs.R_AL = (pci_get_slot(dev) << 3) |
		    (pci_get_function(dev) & 0x07);
	}
	regs.R_DL = 0x80;
	x86bios_call(&regs, X86BIOS_PHYSTOSEG(vesa_bios_offs + 3),
	    X86BIOS_PHYSTOOFF(vesa_bios_offs + 3));

	if (x86bios_get_intr(0x10) == 0)
		return (1);

	return (0);
}

/* VESA BIOS calls */
static int
vesa_bios_get_mode(int mode, struct vesa_mode *vmode, int flags)
{
	x86regs_t regs;
	uint32_t offs;
	void *buf;

	buf = x86bios_alloc(&offs, sizeof(*vmode), flags);
	if (buf == NULL)
		return (1);

	x86bios_init_regs(&regs);
	regs.R_AX = 0x4f01;
	regs.R_CX = mode;

	regs.R_ES = X86BIOS_PHYSTOSEG(offs);
	regs.R_DI = X86BIOS_PHYSTOOFF(offs);

	x86bios_intr(&regs, 0x10);

	if (regs.R_AX != 0x004f) {
		x86bios_free(buf, sizeof(*vmode));
		return (1);
	}

	bcopy(buf, vmode, sizeof(*vmode));
	x86bios_free(buf, sizeof(*vmode));

	return (0);
}

static int
vesa_bios_set_mode(int mode)
{
	x86regs_t regs;

	x86bios_init_regs(&regs);
	regs.R_AX = 0x4f02;
	regs.R_BX = mode;

	x86bios_intr(&regs, 0x10);

	return (regs.R_AX != 0x004f);
}

#if 0
static int
vesa_bios_get_dac(void)
{
	x86regs_t regs;

	x86bios_init_regs(&regs);
	regs.R_AX = 0x4f08;
	regs.R_BL = 1;

	x86bios_intr(&regs, 0x10);

	if (regs.R_AX != 0x004f)
		return (6);

	return (regs.R_BH);
}
#endif

static int
vesa_bios_set_dac(int bits)
{
	x86regs_t regs;

	x86bios_init_regs(&regs);
	regs.R_AX = 0x4f08;
	/* regs.R_BL = 0; */
	regs.R_BH = bits;

	x86bios_intr(&regs, 0x10);

	if (regs.R_AX != 0x004f)
		return (6);

	return (regs.R_BH);
}

static int
vesa_bios_save_palette(int start, int colors, u_char *palette, int bits)
{
	x86regs_t regs;
	int i;

	x86bios_init_regs(&regs);
	regs.R_AX = 0x4f09;
	regs.R_BL = 1;
	regs.R_CX = colors;
	regs.R_DX = start;

	regs.R_ES = X86BIOS_PHYSTOSEG(vesa_palette_offs);
	regs.R_DI = X86BIOS_PHYSTOOFF(vesa_palette_offs);

	bits = 8 - bits;
	mtx_lock(&vesa_lock);
	x86bios_intr(&regs, 0x10);
	if (regs.R_AX != 0x004f) {
		mtx_unlock(&vesa_lock);
		return (1);
	}
	for (i = 0; i < colors; ++i) {
		palette[i * 3] = vesa_palette[i * 4 + 2] << bits;
		palette[i * 3 + 1] = vesa_palette[i * 4 + 1] << bits;
		palette[i * 3 + 2] = vesa_palette[i * 4] << bits;
	}
	mtx_unlock(&vesa_lock);

	return (0);
}

static int
vesa_bios_save_palette2(int start, int colors, u_char *r, u_char *g, u_char *b,
			int bits)
{
	x86regs_t regs;
	int i;

	x86bios_init_regs(&regs);
	regs.R_AX = 0x4f09;
	regs.R_BL = 1;
	regs.R_CX = colors;
	regs.R_DX = start;

	regs.R_ES = X86BIOS_PHYSTOSEG(vesa_palette_offs);
	regs.R_DI = X86BIOS_PHYSTOOFF(vesa_palette_offs);

	bits = 8 - bits;
	mtx_lock(&vesa_lock);
	x86bios_intr(&regs, 0x10);
	if (regs.R_AX != 0x004f) {
		mtx_unlock(&vesa_lock);
		return (1);
	}
	for (i = 0; i < colors; ++i) {
		r[i] = vesa_palette[i * 4 + 2] << bits;
		g[i] = vesa_palette[i * 4 + 1] << bits;
		b[i] = vesa_palette[i * 4] << bits;
	}
	mtx_unlock(&vesa_lock);

	return (0);
}

static int
vesa_bios_load_palette(int start, int colors, u_char *palette, int bits)
{
	x86regs_t regs;
	int i;

	x86bios_init_regs(&regs);
	regs.R_AX = 0x4f09;
	/* regs.R_BL = 0; */
	regs.R_CX = colors;
	regs.R_DX = start;

	regs.R_ES = X86BIOS_PHYSTOSEG(vesa_palette_offs);
	regs.R_DI = X86BIOS_PHYSTOOFF(vesa_palette_offs);

	bits = 8 - bits;
	mtx_lock(&vesa_lock);
	for (i = 0; i < colors; ++i) {
		vesa_palette[i * 4] = palette[i * 3 + 2] >> bits;
		vesa_palette[i * 4 + 1] = palette[i * 3 + 1] >> bits;
		vesa_palette[i * 4 + 2] = palette[i * 3] >> bits;
		vesa_palette[i * 4 + 3] = 0;
	}
	x86bios_intr(&regs, 0x10);
	mtx_unlock(&vesa_lock);

	return (regs.R_AX != 0x004f);
}

static int
vesa_bios_load_palette2(int start, int colors, u_char *r, u_char *g, u_char *b,
			int bits)
{
	x86regs_t regs;
	int i;

	x86bios_init_regs(&regs);
	regs.R_AX = 0x4f09;
	/* regs.R_BL = 0; */
	regs.R_CX = colors;
	regs.R_DX = start;

	regs.R_ES = X86BIOS_PHYSTOSEG(vesa_palette_offs);
	regs.R_DI = X86BIOS_PHYSTOOFF(vesa_palette_offs);

	bits = 8 - bits;
	mtx_lock(&vesa_lock);
	for (i = 0; i < colors; ++i) {
		vesa_palette[i * 4] = b[i] >> bits;
		vesa_palette[i * 4 + 1] = g[i] >> bits;
		vesa_palette[i * 4 + 2] = r[i] >> bits;
		vesa_palette[i * 4 + 3] = 0;
	}
	x86bios_intr(&regs, 0x10);
	mtx_unlock(&vesa_lock);

	return (regs.R_AX != 0x004f);
}

static size_t
vesa_bios_state_buf_size(int state)
{
	x86regs_t regs;

	x86bios_init_regs(&regs);
	regs.R_AX = 0x4f04;
	/* regs.R_DL = STATE_SIZE; */
	regs.R_CX = state;

	x86bios_intr(&regs, 0x10);

	if (regs.R_AX != 0x004f)
		return (0);

	return (regs.R_BX * 64);
}

static int
vesa_bios_save_restore(int code, void *p)
{
	x86regs_t regs;

	if (code != STATE_SAVE && code != STATE_LOAD)
		return (1);

	x86bios_init_regs(&regs);
	regs.R_AX = 0x4f04;
	regs.R_DL = code;
	regs.R_CX = vesa_state;

	regs.R_ES = X86BIOS_PHYSTOSEG(vesa_state_buf_offs);
	regs.R_BX = X86BIOS_PHYSTOOFF(vesa_state_buf_offs);

	mtx_lock(&vesa_lock);
	switch (code) {
	case STATE_SAVE:
		x86bios_intr(&regs, 0x10);
		if (regs.R_AX == 0x004f)
			bcopy(vesa_state_buf, p, vesa_state_buf_size);
		break;
	case STATE_LOAD:
		bcopy(p, vesa_state_buf, vesa_state_buf_size);
		x86bios_intr(&regs, 0x10);
		break;
	}
	mtx_unlock(&vesa_lock);

	return (regs.R_AX != 0x004f);
}

#ifdef MODE_TABLE_BROKEN
static int
vesa_bios_get_line_length(void)
{
	x86regs_t regs;

	x86bios_init_regs(&regs);
	regs.R_AX = 0x4f06;
	regs.R_BL = 1;

	x86bios_intr(&regs, 0x10);

	if (regs.R_AX != 0x004f)
		return (-1);

	return (regs.R_BX);
}
#endif

static int
vesa_bios_set_line_length(int pixel, int *bytes, int *lines)
{
	x86regs_t regs;

	x86bios_init_regs(&regs);
	regs.R_AX = 0x4f06;
	/* regs.R_BL = 0; */
	regs.R_CX = pixel;

	x86bios_intr(&regs, 0x10);

#if VESA_DEBUG > 1
	printf("bx:%d, cx:%d, dx:%d\n", regs.R_BX, regs.R_CX, regs.R_DX);
#endif
	if (regs.R_AX != 0x004f)
		return (-1);

	if (bytes != NULL)
		*bytes = regs.R_BX;
	if (lines != NULL)
		*lines = regs.R_DX;

	return (0);
}

#if 0
static int
vesa_bios_get_start(int *x, int *y)
{
	x86regs_t regs;

	x86bios_init_regs(&regs);
	regs.R_AX = 0x4f07;
	regs.R_BL = 1;

	x86bios_intr(&regs, 0x10);

	if (regs.R_AX != 0x004f)
		return (-1);

	*x = regs.R_CX;
	*y = regs.R_DX;

	return (0);
}
#endif

static int
vesa_bios_set_start(int x, int y)
{
	x86regs_t regs;

	x86bios_init_regs(&regs);
	regs.R_AX = 0x4f07;
	regs.R_BL = 0x80;
	regs.R_CX = x;
	regs.R_DX = y;

	x86bios_intr(&regs, 0x10);

	return (regs.R_AX != 0x004f);
}

/* map a generic video mode to a known mode */
static int
vesa_map_gen_mode_num(int type, int color, int mode)
{
    static struct {
	int from;
	int to;
    } mode_map[] = {
	{ M_TEXT_132x25, M_VESA_C132x25 },
	{ M_TEXT_132x43, M_VESA_C132x43 },
	{ M_TEXT_132x50, M_VESA_C132x50 },
	{ M_TEXT_132x60, M_VESA_C132x60 },
    };
    int i;

    for (i = 0; i < nitems(mode_map); ++i) {
        if (mode_map[i].from == mode)
            return (mode_map[i].to);
    }
    return (mode);
}

static int
vesa_translate_flags(u_int16_t vflags)
{
	static struct {
		u_int16_t mask;
		int set;
		int reset;
	} ftable[] = {
		{ V_MODECOLOR, V_INFO_COLOR, 0 },
		{ V_MODEGRAPHICS, V_INFO_GRAPHICS, 0 },
		{ V_MODELFB, V_INFO_LINEAR, 0 },
		{ V_MODENONVGA, V_INFO_NONVGA, 0 },
	};
	int flags;
	int i;

	for (flags = 0, i = 0; i < nitems(ftable); ++i) {
		flags |= (vflags & ftable[i].mask) ? 
			 ftable[i].set : ftable[i].reset;
	}
	return (flags);
}

static int
vesa_translate_mmodel(u_int8_t vmodel)
{
	static struct {
		u_int8_t vmodel;
		int mmodel;
	} mtable[] = {
		{ V_MMTEXT,	V_INFO_MM_TEXT },
		{ V_MMCGA,	V_INFO_MM_CGA },
		{ V_MMHGC,	V_INFO_MM_HGC },
		{ V_MMEGA,	V_INFO_MM_PLANAR },
		{ V_MMPACKED,	V_INFO_MM_PACKED },
		{ V_MMDIRCOLOR,	V_INFO_MM_DIRECT },
	};
	int i;

	for (i = 0; mtable[i].mmodel >= 0; ++i) {
		if (mtable[i].vmodel == vmodel)
			return (mtable[i].mmodel);
	}
	return (V_INFO_MM_OTHER);
}

static int
vesa_get_bpscanline(struct vesa_mode *vmode)
{
	int bpsl;

	if ((vmode->v_modeattr & V_MODEGRAPHICS) != 0) {
		/* Find the minimum length. */
		switch (vmode->v_bpp / vmode->v_planes) {
		case 1:
			bpsl = vmode->v_width / 8;
			break;
		case 2:
			bpsl = vmode->v_width / 4;
			break;
		case 4:
			bpsl = vmode->v_width / 2;
			break;
		default:
			bpsl = vmode->v_width * ((vmode->v_bpp + 7) / 8);
			bpsl /= vmode->v_planes;
			break;
		}

		/* Use VBE 3.0 information if it looks sane. */
		if ((vmode->v_modeattr & V_MODELFB) != 0 &&
		    vesa_adp_info->v_version >= 0x0300 &&
		    vmode->v_linbpscanline > bpsl)
			return (vmode->v_linbpscanline);

		/* Return the minimum if the mode table looks absurd. */
		if (vmode->v_bpscanline < bpsl)
			return (bpsl);
	}

	return (vmode->v_bpscanline);
}

#define	VESA_MAXSTR		256

#define	VESA_STRCPY(dst, src)	do {				\
	char *str;						\
	int i;							\
	dst = malloc(VESA_MAXSTR, M_DEVBUF, M_WAITOK);		\
	str = x86bios_offset(BIOS_SADDRTOLADDR(src));		\
	for (i = 0; i < VESA_MAXSTR - 1 && str[i] != '\0'; i++)	\
		dst[i] = str[i];				\
	dst[i] = '\0';						\
} while (0)

static int
vesa_bios_init(void)
{
	struct vesa_mode vmode;
	struct vesa_info *buf;
	video_info_t *p;
	x86regs_t regs;
	size_t bsize;
	size_t msize;
	void *vmbuf;
	uint8_t *vbios;
	uint32_t offs;
	uint16_t vers;
	int is_via_cle266;
	int modes;
	int i;

	if (vesa_init_done)
		return (0);

	vesa_bios_offs = VESA_BIOS_OFFSET;

	/*
	 * If the VBE real mode interrupt vector is not found, try BIOS POST.
	 */
	vesa_bios_int10 = x86bios_get_intr(0x10);
	if (vesa_bios_int10 == 0) {
		if (vesa_bios_post() != 0)
			return (1);
		vesa_bios_int10 = x86bios_get_intr(0x10);
		if (vesa_bios_int10 == 0)
			return (1);
	}

	/*
	 * Shadow video ROM.
	 */
	offs = vesa_bios_int10;
	if (vesa_shadow_rom) {
		vbios = x86bios_get_orm(vesa_bios_offs);
		if (vbios != NULL) {
			vesa_bios_size = vbios[2] * 512;
			if (((VESA_BIOS_OFFSET << 12) & 0xffff0000) ==
			    (vesa_bios_int10 & 0xffff0000) &&
			    vesa_bios_size > (vesa_bios_int10 & 0xffff)) {
				vesa_bios = x86bios_alloc(&vesa_bios_offs,
				    vesa_bios_size, M_WAITOK);
				bcopy(vbios, vesa_bios, vesa_bios_size);
				offs = ((vesa_bios_offs << 12) & 0xffff0000) +
				    (vesa_bios_int10 & 0xffff);
				x86bios_set_intr(0x10, offs);
			}
		}
		if (vesa_bios == NULL)
			printf("VESA: failed to shadow video ROM\n");
	}
	if (bootverbose)
		printf("VESA: INT 0x10 vector 0x%04x:0x%04x\n",
		    (offs >> 16) & 0xffff, offs & 0xffff);

	x86bios_init_regs(&regs);
	regs.R_AX = 0x4f00;

	vmbuf = x86bios_alloc(&offs, sizeof(*buf), M_WAITOK);

	regs.R_ES = X86BIOS_PHYSTOSEG(offs);
	regs.R_DI = X86BIOS_PHYSTOOFF(offs);

	bcopy("VBE2", vmbuf, 4);	/* try for VBE2 data */
	x86bios_intr(&regs, 0x10);

	if (regs.R_AX != 0x004f || bcmp("VESA", vmbuf, 4) != 0)
		goto fail;

	vesa_adp_info = buf = malloc(sizeof(*buf), M_DEVBUF, M_WAITOK);
	bcopy(vmbuf, buf, sizeof(*buf));

	if (bootverbose) {
		printf("VESA: information block\n");
		hexdump(buf, sizeof(*buf), NULL, HD_OMIT_CHARS);
	}

	vers = buf->v_version = le16toh(buf->v_version);
	buf->v_oemstr = le32toh(buf->v_oemstr);
	buf->v_flags = le32toh(buf->v_flags);
	buf->v_modetable = le32toh(buf->v_modetable);
	buf->v_memsize = le16toh(buf->v_memsize);
	buf->v_revision = le16toh(buf->v_revision);
	buf->v_venderstr = le32toh(buf->v_venderstr);
	buf->v_prodstr = le32toh(buf->v_prodstr);
	buf->v_revstr = le32toh(buf->v_revstr);

	if (vers < 0x0102) {
		printf("VESA: VBE version %d.%d is not supported; "
		    "version 1.2 or later is required.\n",
		    ((vers & 0xf000) >> 12) * 10 + ((vers & 0x0f00) >> 8),
		    ((vers & 0x00f0) >> 4) * 10 + (vers & 0x000f));
		goto fail;
	}

	VESA_STRCPY(vesa_oemstr, buf->v_oemstr);
	if (vers >= 0x0200) {
		VESA_STRCPY(vesa_venderstr, buf->v_venderstr);
		VESA_STRCPY(vesa_prodstr, buf->v_prodstr);
		VESA_STRCPY(vesa_revstr, buf->v_revstr);
	}
	is_via_cle266 = strncmp(vesa_oemstr, VESA_VIA_CLE266,
	    sizeof(VESA_VIA_CLE266)) == 0;

	if (buf->v_modetable == 0)
		goto fail;

	msize = (size_t)buf->v_memsize * 64 * 1024;

	vesa_vmodetab = x86bios_offset(BIOS_SADDRTOLADDR(buf->v_modetable));

	for (i = 0, modes = 0; (i < (M_VESA_MODE_MAX - M_VESA_BASE + 1)) &&
	    (vesa_vmodetab[i] != 0xffff); ++i) {
		vesa_vmodetab[i] = le16toh(vesa_vmodetab[i]);
		if (vesa_bios_get_mode(vesa_vmodetab[i], &vmode, M_WAITOK))
			continue;

		vmode.v_modeattr = le16toh(vmode.v_modeattr);
		vmode.v_wgran = le16toh(vmode.v_wgran);
		vmode.v_wsize = le16toh(vmode.v_wsize);
		vmode.v_waseg = le16toh(vmode.v_waseg);
		vmode.v_wbseg = le16toh(vmode.v_wbseg);
		vmode.v_posfunc = le32toh(vmode.v_posfunc);
		vmode.v_bpscanline = le16toh(vmode.v_bpscanline);
		vmode.v_width = le16toh(vmode.v_width);
		vmode.v_height = le16toh(vmode.v_height);
		vmode.v_lfb = le32toh(vmode.v_lfb);
		vmode.v_offscreen = le32toh(vmode.v_offscreen);
		vmode.v_offscreensize = le16toh(vmode.v_offscreensize);
		vmode.v_linbpscanline = le16toh(vmode.v_linbpscanline);
		vmode.v_maxpixelclock = le32toh(vmode.v_maxpixelclock);

		/* reject unsupported modes */
#if 0
		if ((vmode.v_modeattr &
		    (V_MODESUPP | V_MODEOPTINFO | V_MODENONVGA)) !=
		    (V_MODESUPP | V_MODEOPTINFO))
			continue;
#else
		if ((vmode.v_modeattr & V_MODEOPTINFO) == 0) {
#if VESA_DEBUG > 1
			printf("Rejecting VESA %s mode: %d x %d x %d bpp "
			    " attr = %x\n",
			    vmode.v_modeattr & V_MODEGRAPHICS ?
			    "graphics" : "text",
			    vmode.v_width, vmode.v_height, vmode.v_bpp,
			    vmode.v_modeattr);
#endif
			continue;
		}
#endif

		bsize = vesa_get_bpscanline(&vmode) * vmode.v_height;
		if ((vmode.v_modeattr & V_MODEGRAPHICS) != 0)
			bsize *= vmode.v_planes;

		/* Does it have enough memory to support this mode? */
		if (msize < bsize) {
#if VESA_DEBUG > 1
			printf("Rejecting VESA %s mode: %d x %d x %d bpp "
			    " attr = %x, not enough memory\n",
			    vmode.v_modeattr & V_MODEGRAPHICS ?
			    "graphics" : "text",
			    vmode.v_width, vmode.v_height, vmode.v_bpp,
			    vmode.v_modeattr);
#endif
			continue;
		}
		if (bsize > vesa_vmem_max)
			vesa_vmem_max = bsize;

		/* expand the array if necessary */
		if (modes >= vesa_vmode_max) {
			vesa_vmode_max += MODE_TABLE_DELTA;
			p = malloc(sizeof(*vesa_vmode) * (vesa_vmode_max + 1),
			    M_DEVBUF, M_WAITOK);
#if VESA_DEBUG > 1
			printf("vesa_bios_init(): modes:%d, vesa_mode_max:%d\n",
			    modes, vesa_vmode_max);
#endif
			if (modes > 0) {
				bcopy(vesa_vmode, p, sizeof(*vesa_vmode)*modes);
				free(vesa_vmode, M_DEVBUF);
			}
			vesa_vmode = p;
		}

#if VESA_DEBUG > 1
		printf("Found VESA %s mode: %d x %d x %d bpp\n",
		    vmode.v_modeattr & V_MODEGRAPHICS ? "graphics" : "text",
		    vmode.v_width, vmode.v_height, vmode.v_bpp);
#endif
		if (is_via_cle266) {
		    if ((vmode.v_width & 0xff00) >> 8 == vmode.v_height - 1) {
			vmode.v_width &= 0xff;
			vmode.v_waseg = 0xb8000 >> 4;
		    }
		}

		/* copy some fields */
		bzero(&vesa_vmode[modes], sizeof(vesa_vmode[modes]));
		vesa_vmode[modes].vi_mode = vesa_vmodetab[i];
		vesa_vmode[modes].vi_width = vmode.v_width;
		vesa_vmode[modes].vi_height = vmode.v_height;
		vesa_vmode[modes].vi_depth = vmode.v_bpp;
		vesa_vmode[modes].vi_planes = vmode.v_planes;
		vesa_vmode[modes].vi_cwidth = vmode.v_cwidth;
		vesa_vmode[modes].vi_cheight = vmode.v_cheight;
		vesa_vmode[modes].vi_window = (vm_offset_t)vmode.v_waseg << 4;
		/* XXX window B */
		vesa_vmode[modes].vi_window_size = vmode.v_wsize * 1024;
		vesa_vmode[modes].vi_window_gran = vmode.v_wgran * 1024;
		if (vmode.v_modeattr & V_MODELFB)
			vesa_vmode[modes].vi_buffer = vmode.v_lfb;
		vesa_vmode[modes].vi_buffer_size = bsize;
		vesa_vmode[modes].vi_mem_model =
		    vesa_translate_mmodel(vmode.v_memmodel);
		switch (vesa_vmode[modes].vi_mem_model) {
		case V_INFO_MM_DIRECT:
			if ((vmode.v_modeattr & V_MODELFB) != 0 &&
			    vers >= 0x0300) {
				vesa_vmode[modes].vi_pixel_fields[0] =
				    vmode.v_linredfieldpos;
				vesa_vmode[modes].vi_pixel_fields[1] =
				    vmode.v_lingreenfieldpos;
				vesa_vmode[modes].vi_pixel_fields[2] =
				    vmode.v_linbluefieldpos;
				vesa_vmode[modes].vi_pixel_fields[3] =
				    vmode.v_linresfieldpos;
				vesa_vmode[modes].vi_pixel_fsizes[0] =
				    vmode.v_linredmasksize;
				vesa_vmode[modes].vi_pixel_fsizes[1] =
				    vmode.v_lingreenmasksize;
				vesa_vmode[modes].vi_pixel_fsizes[2] =
				    vmode.v_linbluemasksize;
				vesa_vmode[modes].vi_pixel_fsizes[3] =
				    vmode.v_linresmasksize;
			} else {
				vesa_vmode[modes].vi_pixel_fields[0] =
				    vmode.v_redfieldpos;
				vesa_vmode[modes].vi_pixel_fields[1] =
				    vmode.v_greenfieldpos;
				vesa_vmode[modes].vi_pixel_fields[2] =
				    vmode.v_bluefieldpos;
				vesa_vmode[modes].vi_pixel_fields[3] =
				    vmode.v_resfieldpos;
				vesa_vmode[modes].vi_pixel_fsizes[0] =
				    vmode.v_redmasksize;
				vesa_vmode[modes].vi_pixel_fsizes[1] =
				    vmode.v_greenmasksize;
				vesa_vmode[modes].vi_pixel_fsizes[2] =
				    vmode.v_bluemasksize;
				vesa_vmode[modes].vi_pixel_fsizes[3] =
				    vmode.v_resmasksize;
			}
			/* FALLTHROUGH */
		case V_INFO_MM_PACKED:
			vesa_vmode[modes].vi_pixel_size = (vmode.v_bpp + 7) / 8;
			break;
		}
		vesa_vmode[modes].vi_flags =
		    vesa_translate_flags(vmode.v_modeattr) | V_INFO_VESA;

		++modes;
	}
	if (vesa_vmode != NULL)
		vesa_vmode[modes].vi_mode = EOT;

	if (bootverbose)
		printf("VESA: %d mode(s) found\n", modes);

	if (modes == 0)
		goto fail;

	x86bios_free(vmbuf, sizeof(*buf));

	/* Probe supported save/restore states. */
	for (i = 0; i < 4; i++)
		if (vesa_bios_state_buf_size(1 << i) > 0)
			vesa_state |= 1 << i;
	if (vesa_state != 0)
		vesa_state_buf_size = vesa_bios_state_buf_size(vesa_state);
	vesa_palette = x86bios_alloc(&vesa_palette_offs,
	    VESA_PALETTE_SIZE + vesa_state_buf_size, M_WAITOK);
	if (vesa_state_buf_size > 0) {
		vesa_state_buf = vesa_palette + VESA_PALETTE_SIZE;
		vesa_state_buf_offs = vesa_palette_offs + VESA_PALETTE_SIZE;
	}

	return (0);

fail:
	x86bios_free(vmbuf, sizeof(buf));
	vesa_bios_uninit();
	return (1);
}

static void
vesa_bios_uninit(void)
{

	if (vesa_bios != NULL) {
		x86bios_set_intr(0x10, vesa_bios_int10);
		vesa_bios_offs = VESA_BIOS_OFFSET;
		x86bios_free(vesa_bios, vesa_bios_size);
		vesa_bios = NULL;
	}
	if (vesa_adp_info != NULL) {
		free(vesa_adp_info, M_DEVBUF);
		vesa_adp_info = NULL;
	}
	if (vesa_oemstr != NULL) {
		free(vesa_oemstr, M_DEVBUF);
		vesa_oemstr = NULL;
	}
	if (vesa_venderstr != NULL) {
		free(vesa_venderstr, M_DEVBUF);
		vesa_venderstr = NULL;
	}
	if (vesa_prodstr != NULL) {
		free(vesa_prodstr, M_DEVBUF);
		vesa_prodstr = NULL;
	}
	if (vesa_revstr != NULL) {
		free(vesa_revstr, M_DEVBUF);
		vesa_revstr = NULL;
	}
	if (vesa_vmode != NULL) {
		free(vesa_vmode, M_DEVBUF);
		vesa_vmode = NULL;
	}
	if (vesa_palette != NULL) {
		x86bios_free(vesa_palette,
		    VESA_PALETTE_SIZE + vesa_state_buf_size);
		vesa_palette = NULL;
	}
}

static void
vesa_clear_modes(video_info_t *info, int color)
{
	while (info->vi_mode != EOT) {
		if ((info->vi_flags & V_INFO_COLOR) != color)
			info->vi_mode = NA;
		++info;
	}
}

/* entry points */

static int
vesa_configure(int flags)
{
	video_adapter_t *adp;
	int adapters;
	int error;
	int i;

	if (vesa_init_done)
		return (0);
	if (flags & VIO_PROBE_ONLY)
		return (0);

	/*
	 * If the VESA module has already been loaded, abort loading 
	 * the module this time.
	 */
	for (i = 0; (adp = vid_get_adapter(i)) != NULL; ++i) {
		if (adp->va_flags & V_ADP_VESA)
			return (ENXIO);
		if (adp->va_type == KD_VGA)
			break;
	}

	/*
	 * The VGA adapter is not found.  This is because either 
	 * 1) the VGA driver has not been initialized, or 2) the VGA card
	 * is not present.  If 1) is the case, we shall defer
	 * initialization for now and try again later.
	 */
	if (adp == NULL) {
		vga_sub_configure = vesa_configure;
		return (ENODEV);
	}

	/* count number of registered adapters */
	for (++i; vid_get_adapter(i) != NULL; ++i)
		;
	adapters = i;

	/* call VESA BIOS */
	vesa_adp = adp;
	if (vesa_bios_init()) {
		vesa_adp = NULL;
		return (ENXIO);
	}
	vesa_adp->va_flags |= V_ADP_VESA;

	/* remove conflicting modes if we have more than one adapter */
	if (adapters > 1) {
		vesa_clear_modes(vesa_vmode,
				 (vesa_adp->va_flags & V_ADP_COLOR) ? 
				     V_INFO_COLOR : 0);
	}

	if ((error = vesa_load_ioctl()) == 0) {
		prevvidsw = vidsw[vesa_adp->va_index];
		vidsw[vesa_adp->va_index] = &vesavidsw;
		vesa_init_done = TRUE;
	} else {
		vesa_adp = NULL;
		return (error);
	}

	return (0);
}

#if 0
static int
vesa_nop(void)
{

	return (0);
}
#endif

static int
vesa_error(void)
{

	return (1);
}

static int
vesa_probe(int unit, video_adapter_t **adpp, void *arg, int flags)
{

	return ((*prevvidsw->probe)(unit, adpp, arg, flags));
}

static int
vesa_init(int unit, video_adapter_t *adp, int flags)
{

	return ((*prevvidsw->init)(unit, adp, flags));
}

static int
vesa_get_info(video_adapter_t *adp, int mode, video_info_t *info)
{
	int i;

	if ((*prevvidsw->get_info)(adp, mode, info) == 0)
		return (0);

	if (adp != vesa_adp)
		return (1);

	mode = vesa_map_gen_mode_num(vesa_adp->va_type, 
				     vesa_adp->va_flags & V_ADP_COLOR, mode);
	for (i = 0; vesa_vmode[i].vi_mode != EOT; ++i) {
		if (vesa_vmode[i].vi_mode == NA)
			continue;
		if (vesa_vmode[i].vi_mode == mode) {
			*info = vesa_vmode[i];
			return (0);
		}
	}
	return (1);
}

static int
vesa_query_mode(video_adapter_t *adp, video_info_t *info)
{
	int i;

	if ((*prevvidsw->query_mode)(adp, info) == 0)
		return (0);
	if (adp != vesa_adp)
		return (ENODEV);

	for (i = 0; vesa_vmode[i].vi_mode != EOT; ++i) {
		if ((info->vi_width != 0)
		    && (info->vi_width != vesa_vmode[i].vi_width))
			continue;
		if ((info->vi_height != 0)
		    && (info->vi_height != vesa_vmode[i].vi_height))
			continue;
		if ((info->vi_cwidth != 0)
		    && (info->vi_cwidth != vesa_vmode[i].vi_cwidth))
			continue;
		if ((info->vi_cheight != 0)
		    && (info->vi_cheight != vesa_vmode[i].vi_cheight))
			continue;
		if ((info->vi_depth != 0)
		    && (info->vi_depth != vesa_vmode[i].vi_depth))
			continue;
		if ((info->vi_planes != 0)
		    && (info->vi_planes != vesa_vmode[i].vi_planes))
			continue;
		/* pixel format, memory model */
		if ((info->vi_flags != 0)
		    && (info->vi_flags != vesa_vmode[i].vi_flags))
			continue;
		*info = vesa_vmode[i];
		return (0);
	}
	return (ENODEV);
}

static int
vesa_set_mode(video_adapter_t *adp, int mode)
{
	video_info_t info;

	if (adp != vesa_adp)
		return ((*prevvidsw->set_mode)(adp, mode));

	mode = vesa_map_gen_mode_num(adp->va_type, 
				     adp->va_flags & V_ADP_COLOR, mode);
#if VESA_DEBUG > 0
	printf("VESA: set_mode(): %d(%x) -> %d(%x)\n",
		adp->va_mode, adp->va_mode, mode, mode);
#endif
	/* 
	 * If the current mode is a VESA mode and the new mode is not,
	 * restore the state of the adapter first by setting one of the
	 * standard VGA mode, so that non-standard, extended SVGA registers 
	 * are set to the state compatible with the standard VGA modes. 
	 * Otherwise (*prevvidsw->set_mode)() may not be able to set up 
	 * the new mode correctly.
	 */
	if (VESA_MODE(adp->va_mode)) {
		if (!VESA_MODE(mode) &&
		    (*prevvidsw->get_info)(adp, mode, &info) == 0) {
			if ((adp->va_flags & V_ADP_DAC8) != 0) {
				vesa_bios_set_dac(6);
				adp->va_flags &= ~V_ADP_DAC8;
			}
			int10_set_mode(adp->va_initial_bios_mode);
			if (adp->va_info.vi_flags & V_INFO_LINEAR)
				pmap_unmapdev(adp->va_buffer, vesa_vmem_max);
			/* 
			 * Once (*prevvidsw->get_info)() succeeded, 
			 * (*prevvidsw->set_mode)() below won't fail...
			 */
		}
	}

	/* we may not need to handle this mode after all... */
	if (!VESA_MODE(mode) && (*prevvidsw->set_mode)(adp, mode) == 0)
		return (0);

	/* is the new mode supported? */
	if (vesa_get_info(adp, mode, &info))
		return (1);
	/* assert(VESA_MODE(mode)); */

#if VESA_DEBUG > 0
	printf("VESA: about to set a VESA mode...\n");
#endif
	/*
	 * The mode change should reset the palette format to 6 bits, so
	 * we must reset V_ADP_DAC8.  Some BIOSes do an incomplete reset
	 * if we call them with an 8-bit palette, so reset directly.
	 */
	if (adp->va_flags & V_ADP_DAC8) {
	    vesa_bios_set_dac(6);
	    adp->va_flags &= ~V_ADP_DAC8;
	}

	/* don't use the linear frame buffer for text modes. XXX */
	if (!(info.vi_flags & V_INFO_GRAPHICS))
		info.vi_flags &= ~V_INFO_LINEAR;

	if ((info.vi_flags & V_INFO_LINEAR) != 0)
		mode |= 0x4000;
	if (vesa_bios_set_mode(mode | 0x8000))
		return (1);

	if ((vesa_adp_info->v_flags & V_DAC8) != 0 &&
	    (info.vi_flags & V_INFO_GRAPHICS) != 0 &&
	    vesa_bios_set_dac(8) > 6)
		adp->va_flags |= V_ADP_DAC8;

	if (adp->va_info.vi_flags & V_INFO_LINEAR)
		pmap_unmapdev(adp->va_buffer, vesa_vmem_max);

#if VESA_DEBUG > 0
	printf("VESA: mode set!\n");
#endif
	vesa_adp->va_mode = mode & 0x1ff;	/* Mode number is 9-bit. */
	vesa_adp->va_flags &= ~V_ADP_COLOR;
	vesa_adp->va_flags |= 
		(info.vi_flags & V_INFO_COLOR) ? V_ADP_COLOR : 0;
	vesa_adp->va_crtc_addr =
		(vesa_adp->va_flags & V_ADP_COLOR) ? COLOR_CRTC : MONO_CRTC;

	vesa_adp->va_flags &= ~V_ADP_CWIDTH9;
	vesa_adp->va_line_width = info.vi_buffer_size / info.vi_height;
	if ((info.vi_flags & V_INFO_GRAPHICS) != 0)
		vesa_adp->va_line_width /= info.vi_planes;

#ifdef MODE_TABLE_BROKEN
	/* If VBE function returns bigger bytes per scan line, use it. */
	{
		int bpsl = vesa_bios_get_line_length();
		if (bpsl > vesa_adp->va_line_width) {
			vesa_adp->va_line_width = bpsl;
			info.vi_buffer_size = bpsl * info.vi_height;
			if ((info.vi_flags & V_INFO_GRAPHICS) != 0)
				info.vi_buffer_size *= info.vi_planes;
		}
	}
#endif

	if (info.vi_flags & V_INFO_LINEAR) {
#if VESA_DEBUG > 1
		printf("VESA: setting up LFB\n");
#endif
		vesa_adp->va_buffer =
		    (vm_offset_t)pmap_mapdev_attr(info.vi_buffer,
		    vesa_vmem_max, PAT_WRITE_COMBINING);
		vesa_adp->va_window = vesa_adp->va_buffer;
		vesa_adp->va_window_size = info.vi_buffer_size / info.vi_planes;
		vesa_adp->va_window_gran = info.vi_buffer_size / info.vi_planes;
	} else {
		vesa_adp->va_buffer = 0;
		vesa_adp->va_window = (vm_offset_t)x86bios_offset(info.vi_window);
		vesa_adp->va_window_size = info.vi_window_size;
		vesa_adp->va_window_gran = info.vi_window_gran;
	}
	vesa_adp->va_buffer_size = info.vi_buffer_size;
	vesa_adp->va_window_orig = 0;
	vesa_adp->va_disp_start.x = 0;
	vesa_adp->va_disp_start.y = 0;
#if VESA_DEBUG > 0
	printf("vesa_set_mode(): vi_width:%d, line_width:%d\n",
	       info.vi_width, vesa_adp->va_line_width);
#endif
	bcopy(&info, &vesa_adp->va_info, sizeof(vesa_adp->va_info));

	/* move hardware cursor out of the way */
	(*vidsw[vesa_adp->va_index]->set_hw_cursor)(vesa_adp, -1, -1);

	return (0);
}

static int
vesa_save_font(video_adapter_t *adp, int page, int fontsize, int fontwidth,
	       u_char *data, int ch, int count)
{

	return ((*prevvidsw->save_font)(adp, page, fontsize, fontwidth, data,
	    ch, count));
}

static int
vesa_load_font(video_adapter_t *adp, int page, int fontsize, int fontwidth,
	       u_char *data, int ch, int count)
{

	return ((*prevvidsw->load_font)(adp, page, fontsize, fontwidth, data,
		ch, count));
}

static int
vesa_show_font(video_adapter_t *adp, int page)
{

	return ((*prevvidsw->show_font)(adp, page));
}

static int
vesa_save_palette(video_adapter_t *adp, u_char *palette)
{
	int bits;

	if (adp == vesa_adp && VESA_MODE(adp->va_mode)) {
		bits = (adp->va_flags & V_ADP_DAC8) != 0 ? 8 : 6;
		if (vesa_bios_save_palette(0, 256, palette, bits) == 0)
			return (0);
	}

	return ((*prevvidsw->save_palette)(adp, palette));
}

static int
vesa_load_palette(video_adapter_t *adp, u_char *palette)
{
	int bits;

	if (adp == vesa_adp && VESA_MODE(adp->va_mode)) {
		bits = (adp->va_flags & V_ADP_DAC8) != 0 ? 8 : 6;
		if (vesa_bios_load_palette(0, 256, palette, bits) == 0)
			return (0);
	}

	return ((*prevvidsw->load_palette)(adp, palette));
}

static int
vesa_set_border(video_adapter_t *adp, int color)
{

	return ((*prevvidsw->set_border)(adp, color));
}

static int
vesa_save_state(video_adapter_t *adp, void *p, size_t size)
{
	void *buf;
	size_t bsize;

	if (adp != vesa_adp || (size == 0 && vesa_state_buf_size == 0))
		return ((*prevvidsw->save_state)(adp, p, size));

	bsize = offsetof(adp_state_t, regs) + vesa_state_buf_size;
	if (size == 0)
		return (bsize);
	if (vesa_state_buf_size > 0 && size < bsize)
		return (EINVAL);

	if (vesa_vmem_buf != NULL) {
		free(vesa_vmem_buf, M_DEVBUF);
		vesa_vmem_buf = NULL;
	}
	if (VESA_MODE(adp->va_mode)) {
		buf = (void *)adp->va_buffer;
		if (buf != NULL) {
			bsize = adp->va_buffer_size;
			vesa_vmem_buf = malloc(bsize, M_DEVBUF, M_NOWAIT);
			if (vesa_vmem_buf != NULL)
				bcopy(buf, vesa_vmem_buf, bsize);
		}
	}
	if (vesa_state_buf_size == 0)
		return ((*prevvidsw->save_state)(adp, p, size));
	((adp_state_t *)p)->sig = V_STATE_SIG;
	return (vesa_bios_save_restore(STATE_SAVE, ((adp_state_t *)p)->regs));
}

static int
vesa_load_state(video_adapter_t *adp, void *p)
{
	void *buf;
	size_t bsize;
	int error, mode;

	if (adp != vesa_adp)
		return ((*prevvidsw->load_state)(adp, p));

	/* Try BIOS POST to restore a sane state. */
	(void)vesa_bios_post();
	bsize = adp->va_buffer_size;
	mode = adp->va_mode;
	error = vesa_set_mode(adp, adp->va_initial_mode);
	if (mode != adp->va_initial_mode)
		error = vesa_set_mode(adp, mode);

	if (vesa_vmem_buf != NULL) {
		if (error == 0 && VESA_MODE(mode)) {
			buf = (void *)adp->va_buffer;
			if (buf != NULL)
				bcopy(vesa_vmem_buf, buf, bsize);
		}
		free(vesa_vmem_buf, M_DEVBUF);
		vesa_vmem_buf = NULL;
	}
	if (((adp_state_t *)p)->sig != V_STATE_SIG)
		return ((*prevvidsw->load_state)(adp, p));
	return (vesa_bios_save_restore(STATE_LOAD, ((adp_state_t *)p)->regs));
}

#if 0
static int
vesa_get_origin(video_adapter_t *adp, off_t *offset)
{
	x86regs_t regs;

	x86bios_init_regs(&regs);
	regs.R_AX = 0x4f05;
	regs.R_BL = 0x10;

	x86bios_intr(&regs, 0x10);

	if (regs.R_AX != 0x004f)
		return (1);
	*offset = regs.DX * adp->va_window_gran;

	return (0);
}
#endif

static int
vesa_set_origin(video_adapter_t *adp, off_t offset)
{
	x86regs_t regs;

	/*
	 * This function should return as quickly as possible to 
	 * maintain good performance of the system. For this reason,
	 * error checking is kept minimal and let the VESA BIOS to 
	 * detect error.
	 */
	if (adp != vesa_adp) 
		return ((*prevvidsw->set_win_org)(adp, offset));

	/* if this is a linear frame buffer, do nothing */
	if (adp->va_info.vi_flags & V_INFO_LINEAR)
		return (0);
	/* XXX */
	if (adp->va_window_gran == 0)
		return (1);

	x86bios_init_regs(&regs);
	regs.R_AX = 0x4f05;
	regs.R_DX = offset / adp->va_window_gran;
	
	x86bios_intr(&regs, 0x10);

	if (regs.R_AX != 0x004f)
		return (1);

	x86bios_init_regs(&regs);
	regs.R_AX = 0x4f05;
	regs.R_BL = 1;
	regs.R_DX = offset / adp->va_window_gran;
	x86bios_intr(&regs, 0x10);

	adp->va_window_orig = rounddown(offset, adp->va_window_gran);
	return (0);			/* XXX */
}

static int
vesa_read_hw_cursor(video_adapter_t *adp, int *col, int *row)
{

	return ((*prevvidsw->read_hw_cursor)(adp, col, row));
}

static int
vesa_set_hw_cursor(video_adapter_t *adp, int col, int row)
{

	return ((*prevvidsw->set_hw_cursor)(adp, col, row));
}

static int
vesa_set_hw_cursor_shape(video_adapter_t *adp, int base, int height,
			 int celsize, int blink)
{

	return ((*prevvidsw->set_hw_cursor_shape)(adp, base, height, celsize,
	    blink));
}

static int
vesa_blank_display(video_adapter_t *adp, int mode) 
{

	/* XXX: use VESA DPMS */
	return ((*prevvidsw->blank_display)(adp, mode));
}

static int
vesa_mmap(video_adapter_t *adp, vm_ooffset_t offset, vm_paddr_t *paddr,
	  int prot, vm_memattr_t *memattr)
{

#if VESA_DEBUG > 0
	printf("vesa_mmap(): window:0x%tx, buffer:0x%tx, offset:0x%jx\n", 
	       adp->va_info.vi_window, adp->va_info.vi_buffer, offset);
#endif

	if ((adp == vesa_adp) &&
	    (adp->va_info.vi_flags & V_INFO_LINEAR) != 0) {
		/* va_window_size == va_buffer_size/vi_planes */
		/* XXX: is this correct? */
		if (offset > adp->va_window_size - PAGE_SIZE)
			return (-1);
		*paddr = adp->va_info.vi_buffer + offset;
#ifdef VM_MEMATTR_WRITE_COMBINING
		*memattr = VM_MEMATTR_WRITE_COMBINING;
#endif
		return (0);
	}
	return ((*prevvidsw->mmap)(adp, offset, paddr, prot, memattr));
}

static int
vesa_clear(video_adapter_t *adp)
{

	return ((*prevvidsw->clear)(adp));
}

static int
vesa_fill_rect(video_adapter_t *adp, int val, int x, int y, int cx, int cy)
{

	return ((*prevvidsw->fill_rect)(adp, val, x, y, cx, cy));
}

static int
vesa_bitblt(video_adapter_t *adp,...)
{

	/* FIXME */
	return (1);
}

static int
get_palette(video_adapter_t *adp, int base, int count,
	    u_char *red, u_char *green, u_char *blue, u_char *trans)
{
	u_char *r;
	u_char *g;
	u_char *b;
	int bits;
	int error;

	if (base < 0 || base >= 256 || count < 0 || count > 256)
		return (1);
	if ((base + count) > 256)
		return (1);
	if (!VESA_MODE(adp->va_mode))
		return (1);

	bits = (adp->va_flags & V_ADP_DAC8) != 0 ? 8 : 6;
	r = malloc(count * 3, M_DEVBUF, M_WAITOK);
	g = r + count;
	b = g + count;
	error = vesa_bios_save_palette2(base, count, r, g, b, bits);
	if (error == 0) {
		copyout(r, red, count);
		copyout(g, green, count);
		copyout(b, blue, count);
		if (trans != NULL) {
			bzero(r, count);
			copyout(r, trans, count);
		}
	}
	free(r, M_DEVBUF);

	return (error);
}

static int
set_palette(video_adapter_t *adp, int base, int count,
	    u_char *red, u_char *green, u_char *blue, u_char *trans)
{
	u_char *r;
	u_char *g;
	u_char *b;
	int bits;
	int error;

	if (base < 0 || base >= 256 || count < 0 || count > 256)
		return (1);
	if ((base + count) > 256)
		return (1);
	if (!VESA_MODE(adp->va_mode))
		return (1);

	bits = (adp->va_flags & V_ADP_DAC8) != 0 ? 8 : 6;
	r = malloc(count * 3, M_DEVBUF, M_WAITOK);
	g = r + count;
	b = g + count;
	copyin(red, r, count);
	copyin(green, g, count);
	copyin(blue, b, count);

	error = vesa_bios_load_palette2(base, count, r, g, b, bits);
	free(r, M_DEVBUF);

	return (error);
}

static int
vesa_ioctl(video_adapter_t *adp, u_long cmd, caddr_t arg)
{
	int bytes;

	if (adp != vesa_adp)
		return ((*prevvidsw->ioctl)(adp, cmd, arg));

	switch (cmd) {
	case FBIO_SETWINORG:	/* set frame buffer window origin */
		if (!VESA_MODE(adp->va_mode))
			return (*prevvidsw->ioctl)(adp, cmd, arg);
		return (vesa_set_origin(adp, *(off_t *)arg) ? ENODEV : 0);

	case FBIO_SETDISPSTART:	/* set display start address */
		if (!VESA_MODE(adp->va_mode))
			return ((*prevvidsw->ioctl)(adp, cmd, arg));
		if (vesa_bios_set_start(((video_display_start_t *)arg)->x,
					((video_display_start_t *)arg)->y))
			return (ENODEV);
		adp->va_disp_start.x = ((video_display_start_t *)arg)->x;
		adp->va_disp_start.y = ((video_display_start_t *)arg)->y;
		return (0);

	case FBIO_SETLINEWIDTH:	/* set line length in pixel */
		if (!VESA_MODE(adp->va_mode))
			return ((*prevvidsw->ioctl)(adp, cmd, arg));
		if (vesa_bios_set_line_length(*(u_int *)arg, &bytes, NULL))
			return (ENODEV);
		adp->va_line_width = bytes;
#if VESA_DEBUG > 1
		printf("new line width:%d\n", adp->va_line_width);
#endif
		return (0);

	case FBIO_GETPALETTE:	/* get color palette */
		if (get_palette(adp, ((video_color_palette_t *)arg)->index,
				((video_color_palette_t *)arg)->count,
				((video_color_palette_t *)arg)->red,
				((video_color_palette_t *)arg)->green,
				((video_color_palette_t *)arg)->blue,
				((video_color_palette_t *)arg)->transparent))
			return ((*prevvidsw->ioctl)(adp, cmd, arg));
		return (0);


	case FBIO_SETPALETTE:	/* set color palette */
		if (set_palette(adp, ((video_color_palette_t *)arg)->index,
				((video_color_palette_t *)arg)->count,
				((video_color_palette_t *)arg)->red,
				((video_color_palette_t *)arg)->green,
				((video_color_palette_t *)arg)->blue,
				((video_color_palette_t *)arg)->transparent))
			return ((*prevvidsw->ioctl)(adp, cmd, arg));
		return (0);

	case FBIOGETCMAP:	/* get color palette */
		if (get_palette(adp, ((struct fbcmap *)arg)->index,
				((struct fbcmap *)arg)->count,
				((struct fbcmap *)arg)->red,
				((struct fbcmap *)arg)->green,
				((struct fbcmap *)arg)->blue, NULL))
			return ((*prevvidsw->ioctl)(adp, cmd, arg));
		return (0);

	case FBIOPUTCMAP:	/* set color palette */
		if (set_palette(adp, ((struct fbcmap *)arg)->index,
				((struct fbcmap *)arg)->count,
				((struct fbcmap *)arg)->red,
				((struct fbcmap *)arg)->green,
				((struct fbcmap *)arg)->blue, NULL))
			return ((*prevvidsw->ioctl)(adp, cmd, arg));
		return (0);

	default:
		return ((*prevvidsw->ioctl)(adp, cmd, arg));
	}
}

static int
vesa_diag(video_adapter_t *adp, int level)
{
	int error;

	/* call the previous handler first */
	error = (*prevvidsw->diag)(adp, level);
	if (error)
		return (error);

	if (adp != vesa_adp)
		return (1);

	if (level <= 0)
		return (0);

	return (0);
}

static int
vesa_bios_info(int level)
{
#if VESA_DEBUG > 1
	struct vesa_mode vmode;
	int i;
#endif
	uint16_t vers;

	vers = vesa_adp_info->v_version;

	if (bootverbose) {
		/* general adapter information */
		printf(
	"VESA: v%d.%d, %dk memory, flags:0x%x, mode table:%p (%x)\n", 
		    (vers >> 12) * 10 + ((vers & 0x0f00) >> 8),
		    ((vers & 0x00f0) >> 4) * 10 + (vers & 0x000f),
		    vesa_adp_info->v_memsize * 64, vesa_adp_info->v_flags,
		    vesa_vmodetab, vesa_adp_info->v_modetable);

		/* OEM string */
		if (vesa_oemstr != NULL)
			printf("VESA: %s\n", vesa_oemstr);
	}

	if (level <= 0)
		return (0);

	if (vers >= 0x0200 && bootverbose) {
		/* vender name, product name, product revision */
		printf("VESA: %s %s %s\n",
			(vesa_venderstr != NULL) ? vesa_venderstr : "unknown",
			(vesa_prodstr != NULL) ? vesa_prodstr : "unknown",
			(vesa_revstr != NULL) ? vesa_revstr : "?");
	}

#if VESA_DEBUG > 1
	/* mode information */
	for (i = 0;
		(i < (M_VESA_MODE_MAX - M_VESA_BASE + 1))
		&& (vesa_vmodetab[i] != 0xffff); ++i) {
		if (vesa_bios_get_mode(vesa_vmodetab[i], &vmode, M_NOWAIT))
			continue;

		/* print something for diagnostic purpose */
		printf("VESA: mode:0x%03x, flags:0x%04x", 
		       vesa_vmodetab[i], vmode.v_modeattr);
		if (vmode.v_modeattr & V_MODEOPTINFO) {
			if (vmode.v_modeattr & V_MODEGRAPHICS) {
				printf(", G %dx%dx%d %d, ", 
				       vmode.v_width, vmode.v_height,
				       vmode.v_bpp, vmode.v_planes);
			} else {
				printf(", T %dx%d, ", 
				       vmode.v_width, vmode.v_height);
			}
			printf("font:%dx%d, ", 
			       vmode.v_cwidth, vmode.v_cheight);
			printf("pages:%d, mem:%d",
			       vmode.v_ipages + 1, vmode.v_memmodel);
		}
		if (vmode.v_modeattr & V_MODELFB) {
			printf("\nVESA: LFB:0x%x, off:0x%x, off_size:0x%x", 
			       vmode.v_lfb, vmode.v_offscreen,
			       vmode.v_offscreensize*1024);
		}
		printf("\n");
		printf("VESA: window A:0x%x (%x), window B:0x%x (%x), ",
		       vmode.v_waseg, vmode.v_waattr,
		       vmode.v_wbseg, vmode.v_wbattr);
		printf("size:%dk, gran:%dk\n",
		       vmode.v_wsize, vmode.v_wgran);
	}
#endif /* VESA_DEBUG > 1 */

	return (0);
}

/* module loading */

static int
vesa_load(void)
{
	int error;

	if (vesa_init_done)
		return (0);

	/* locate a VGA adapter */
	vesa_adp = NULL;
	error = vesa_configure(0);

	if (error == 0)
		vesa_bios_info(bootverbose);

	return (error);
}

static int
vesa_unload(void)
{
	int error;

	/* if the adapter is currently in a VESA mode, don't unload */
	if ((vesa_adp != NULL) && VESA_MODE(vesa_adp->va_mode))
		return (EBUSY);
	/* 
	 * FIXME: if there is at least one vty which is in a VESA mode,
	 * we shouldn't be unloading! XXX
	 */

	if ((error = vesa_unload_ioctl()) == 0) {
		if (vesa_adp != NULL) {
			if ((vesa_adp->va_flags & V_ADP_DAC8) != 0) {
				vesa_bios_set_dac(6);
				vesa_adp->va_flags &= ~V_ADP_DAC8;
			}
			vesa_adp->va_flags &= ~V_ADP_VESA;
			vidsw[vesa_adp->va_index] = prevvidsw;
		}
	}

	vesa_bios_uninit();

	return (error);
}

static int
vesa_mod_event(module_t mod, int type, void *data)
{

	switch (type) {
	case MOD_LOAD:
		return (vesa_load());
	case MOD_UNLOAD:
		return (vesa_unload());
	}
	return (EOPNOTSUPP);
}

static moduledata_t vesa_mod = {
	"vesa",
	vesa_mod_event,
	NULL,
};

DECLARE_MODULE(vesa, vesa_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_DEPEND(vesa, x86bios, 1, 1, 1);

#endif	/* VGA_NO_MODE_CHANGE */
