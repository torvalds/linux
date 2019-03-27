/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Robert N. M. Watson
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
 *
 * $FreeBSD$
 */

#ifndef _DEV_TERASIC_MTL_H_
#define	_DEV_TERASIC_MTL_H_

#include "opt_syscons.h"

struct terasic_mtl_softc {
#if defined(DEV_SC)
	/*
	 * syscons requires that its video_adapter_t be at the front of the
	 * softc, so place syscons fields first, which we otherwise would
	 * probably not do.
	 */
	video_adapter_t	 mtl_va;
#endif

	/*
	 * Bus-related fields.
	 */
	device_t	 mtl_dev;
	int		 mtl_unit;

	/*
	 * The MTL driver doesn't require a lot of synchronisation; however,
	 * the lock is used to protect read-modify-write operations on MTL
	 * registers.
	 */
	struct mtx	 mtl_lock;

	/*
	 * Control register device -- mappable from userspace.
	 */
	struct cdev	*mtl_reg_cdev;
	struct resource	*mtl_reg_res;
	int		 mtl_reg_rid;

	/*
	 * Graphics frame buffer device -- mappable from userspace, and used
	 * by the vt framebuffer interface.
	 */
	struct cdev	*mtl_pixel_cdev;
	struct resource	*mtl_pixel_res;
	int		 mtl_pixel_rid;

	/*
	 * Text frame buffer device -- mappable from userspace, and syscons
	 * hookup.
	 */
	struct cdev	*mtl_text_cdev;
	struct resource	*mtl_text_res;
	int		 mtl_text_rid;
	uint16_t	*mtl_text_soft;

	/*
	 * Framebuffer hookup for vt(4).
	 */
	struct fb_info	 mtl_fb_info;
};

#define	TERASIC_MTL_LOCK(sc)		mtx_lock(&(sc)->mtl_lock)
#define	TERASIC_MTL_LOCK_ASSERT(sc)	mtx_assert(&(sc)->mtl_lock, MA_OWNED)
#define	TERASIC_MTL_LOCK_DESTROY(sc)	mtx_destroy(&(sc)->mtl_lock)
#define	TERASIC_MTL_LOCK_INIT(sc)	mtx_init(&(sc)->mtl_lock,	\
					    "terasic_mtl", NULL, MTX_DEF)
#define	TERASIC_MTL_UNLOCK(sc)		mtx_unlock(&(sc)->mtl_lock)

/*
 * Constant properties of the MTL text frame buffer.
 */
#define	TERASIC_MTL_COLS	100
#define	TERASIC_MTL_ROWS	40

/*
 * MTL control register offsets.
 */
#define	TERASIC_MTL_OFF_BLEND			0
#define	TERASIC_MTL_OFF_TEXTCURSOR		4
#define	TERASIC_MTL_OFF_TEXTFRAMEBUFADDR	8
#define	TERASIC_MTL_OFF_TOUCHPOINT_X1		12
#define	TERASIC_MTL_OFF_TOUCHPOINT_Y1		16
#define	TERASIC_MTL_OFF_TOUCHPOINT_X2		20
#define	TERASIC_MTL_OFF_TOUCHPOINT_Y2		24
#define	TERASIC_MTL_OFF_TOUCHGESTURE		28

/*
 * Constants to help interpret various control registers.
 */
#define	TERASIC_MTL_BLEND_PIXEL_ENDIAN_SWAP	0x10000000
#define	TERASIC_MTL_BLEND_DEFAULT_MASK		0x0f000000
#define	TERASIC_MTL_BLEND_DEFAULT_SHIFT		24
#define	TERASIC_MTL_BLEND_PIXEL_MASK		0x00ff0000
#define	TERASIC_MTL_BLEND_PIXEL_SHIFT		16
#define	TERASIC_MTL_BLEND_TEXTFG_MASK		0x0000ff00
#define	TERASIC_MTL_BLEND_TEXTFG_SHIFT		8
#define	TERASIC_MTL_BLEND_TEXTBG_MASK		0x000000ff
#define	TERASIC_MTL_BLEND_TEXTBG_SHIFT		0
#define	TERASIC_MTL_TEXTCURSOR_COL_MASK		0xff00
#define	TERASIC_MTL_TEXTCURSOR_COL_SHIFT	8
#define	TERASIC_MTL_TEXTCURSOR_ROW_MASK		0xff

/*
 * Colours used both by VGA-like text rendering, and for the default display
 * colour.
 */
#define	TERASIC_MTL_COLOR_BLACK		0
#define	TERASIC_MTL_COLOR_DARKBLUE	1
#define	TERASIC_MTL_COLOR_DARKGREEN	2
#define	TERASIC_MTL_COLOR_DARKCYAN	3
#define	TERASIC_MTL_COLOR_DARKRED	4
#define	TERASIC_MTL_COLOR_DARKMAGENTA	5
#define	TERASIC_MTL_COLOR_BROWN		6
#define	TERASIC_MTL_COLOR_LIGHTGREY	7
#define	TERASIC_MTL_COLOR_DARKGREY	8
#define	TERASIC_MTL_COLOR_LIGHTBLUE	9
#define	TERASIC_MTL_COLOR_LIGHTGREEN	10
#define	TERASIC_MTL_COLOR_LIGHTCYAN	11
#define	TERASIC_MTL_COLOR_LIGHTRED	12
#define	TERASIC_MTL_COLOR_LIGHTMAGENTA	13
#define	TERASIC_MTL_COLOR_LIGHTYELLOW	14
#define	TERASIC_MTL_COLOR_WHITE		15
#define	TERASIC_MTL_COLORMASK_BLINK	0x80

/*
 * Constants to help interpret the text frame buffer.
 */
#define	TERASIC_MTL_TEXTFRAMEBUF_EXPECTED_ADDR	0x0177000
#define	TERASIC_MTL_TEXTFRAMEBUF_CHAR_SHIFT	0
#define	TERASIC_MTL_TEXTFRAMEBUF_ATTR_SHIFT	8

/*
 * Framebuffer constants.
 */
#define	TERASIC_MTL_FB_WIDTH		800
#define	TERASIC_MTL_FB_HEIGHT		640

/*
 * Alpha-blending constants.
 */
#define	TERASIC_MTL_ALPHA_TRANSPARENT	0
#define	TERASIC_MTL_ALPHA_OPAQUE	255

/*
 * Driver setup routines from the bus attachment/teardown.
 */
int	terasic_mtl_attach(struct terasic_mtl_softc *sc);
void	terasic_mtl_detach(struct terasic_mtl_softc *sc);

extern devclass_t	terasic_mtl_devclass;

/*
 * Sub-driver setup routines.
 */
int	terasic_mtl_fbd_attach(struct terasic_mtl_softc *sc);
void	terasic_mtl_fbd_detach(struct terasic_mtl_softc *sc);
int	terasic_mtl_pixel_attach(struct terasic_mtl_softc *sc);
void	terasic_mtl_pixel_detach(struct terasic_mtl_softc *sc);
int	terasic_mtl_reg_attach(struct terasic_mtl_softc *sc);
void	terasic_mtl_reg_detach(struct terasic_mtl_softc *sc);
int	terasic_mtl_syscons_attach(struct terasic_mtl_softc *sc);
void	terasic_mtl_syscons_detach(struct terasic_mtl_softc *sc);
int	terasic_mtl_text_attach(struct terasic_mtl_softc *sc);
void	terasic_mtl_text_detach(struct terasic_mtl_softc *sc);

/*
 * Control register I/O routines.
 */
void	terasic_mtl_reg_blank(struct terasic_mtl_softc *sc);

void	terasic_mtl_reg_blend_get(struct terasic_mtl_softc *sc,
	    uint32_t *blendp);
void	terasic_mtl_reg_blend_set(struct terasic_mtl_softc *sc,
	    uint32_t blend);
void	terasic_mtl_reg_textcursor_get(struct terasic_mtl_softc *sc,
	    uint8_t *colp, uint8_t *rowp);
void	terasic_mtl_reg_textcursor_set(struct terasic_mtl_softc *sc,
	    uint8_t col, uint8_t row);
void	terasic_mtl_reg_textframebufaddr_get(struct terasic_mtl_softc *sc,
	    uint32_t *addrp);
void	terasic_mtl_reg_textframebufaddr_set(struct terasic_mtl_softc *sc,
	    uint32_t addr);

/*
 * Read-modify-write updates of sub-bytes of the blend register.
 */
void	terasic_mtl_blend_default_set(struct terasic_mtl_softc *sc,
	    uint8_t colour);
void	terasic_mtl_blend_pixel_set(struct terasic_mtl_softc *sc,
	    uint8_t alpha);
void	terasic_mtl_blend_textfg_set(struct terasic_mtl_softc *sc,
	    uint8_t alpha);
void	terasic_mtl_blend_textbg_set(struct terasic_mtl_softc *sc,
	    uint8_t alpha);
void	terasic_mtl_reg_pixel_endian_set(struct terasic_mtl_softc *sc,
	    int endian_swap);

/*
 * Text frame buffer I/O routines.
 */
void	terasic_mtl_text_putc(struct terasic_mtl_softc *sc, u_int x, u_int y,
	    uint8_t c, uint8_t a);

#endif /* _DEV_TERASIC_MTL_H_ */
