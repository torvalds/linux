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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/consio.h>
#include <sys/fbio.h>
#include <sys/kbio.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/systm.h>
#include <sys/uio.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <machine/vm.h>

#include <dev/fb/fbreg.h>

#include <dev/kbd/kbdreg.h>

#include <dev/syscons/syscons.h>

#include <dev/terasic/mtl/terasic_mtl.h>

/*
 * Terasic Multitouch LCD (MTL) syscons driver.  Implement syscons(4)'s
 * video_switch_t KPI using MTL's text frame buffer.  In principle, we could
 * actually implement sc_rndr_sw_t, since the MTL text frame buffer implements
 * a VGA-like memory mapping.  However, this requires a lot more book-keeping
 * with only minor performance improvements (avoiding indirection), as well as
 * introducing potential endianness issues.  Instead we accept one additional
 * memory copy between a software frame buffer and the hardware frame buffer
 * and the generic frame buffer (gfb) framework.
 */

MALLOC_DEFINE(M_TERASIC_MTL, "mtl_syscons", "MTL syscons frame buffer");

/*
 * Run early so that boot-time console support can be initialised before
 * newbus gets around to configuring syscons.
 *
 * XXXRW: We may need to do more here in order to see earlier boot messages.
 */
static int
terasic_mtl_syscons_configure(int flags)
{

	printf("%s: not yet\n", __func__);
	return (0);
}

static int
terasic_mtl_vidsw_probe(int unit, video_adapter_t **adp, void *args,
    int flags)
{

	printf("%s: not yet\n", __func__);
	return (0);
}

static int
terasic_mtl_vidsw_init(int unit, video_adapter_t *adp, int flags)
{
	struct terasic_mtl_softc *sc;
	video_info_t *vi;

	sc = (struct terasic_mtl_softc *)adp;

	vi = &adp->va_info;
	vid_init_struct(adp, "terasic_mtl_syscons", -1, unit);

	vi->vi_width = TERASIC_MTL_COLS;
	if (vi->vi_width > COL)
		vi->vi_width = COL;
	vi->vi_height = TERASIC_MTL_ROWS;
	if (vi->vi_height > ROW)
		vi->vi_height = ROW;

	/*
	 * XXXRW: It's not quite clear how these should be initialised.
	 */
	vi->vi_cwidth = 0;
	vi->vi_cheight = 0;
	vi->vi_flags = V_INFO_COLOR;
	vi->vi_mem_model = V_INFO_MM_OTHER;

	/*
	 * Software text frame buffer from which we update the actual MTL
	 * frame buffer when asked to.
	 */
	adp->va_window = (vm_offset_t)sc->mtl_text_soft;

	/*
	 * Declare video adapter capabilities -- at this point, simply color
	 * support, as MTL doesn't support screen borders, font loading, or
	 * mode changes.
	 *
	 * XXXRW: It's unclear if V_ADP_INITIALIZED is needed here; other
	 * syscons(4) drivers are inconsistent about this and
	 * V_ADP_REGISTERED.
	 */
	adp->va_flags |= V_ADP_COLOR | V_ADP_INITIALIZED;
	if (vid_register(adp) < 0) {
		device_printf(sc->mtl_dev, "%s: vid_register failed\n",
		    __func__);
		return (ENXIO);
	}
	adp->va_flags |= V_ADP_REGISTERED;
	return (0);
}

static int
terasic_mtl_vidsw_get_info(video_adapter_t *adp, int mode, video_info_t *info)
{

	bcopy(&adp->va_info, info, sizeof(*info));
	return (0);
}

static int
terasic_mtl_vidsw_query_mode(video_adapter_t *adp, video_info_t *info)
{

	printf("%s: not yet\n", __func__);
	return (ENODEV);
}

static int
terasic_mtl_vidsw_set_mode(video_adapter_t *adp, int mode)
{

	printf("%s: not yet\n", __func__);
	return (ENODEV);
}

static int
terasic_mtl_vidsw_save_font(video_adapter_t *adp, int page, int size,
    int width, u_char *data, int c, int count)
{

	printf("%s: not yet\n", __func__);
	return (ENODEV);
}

static int
terasic_mtl_vidsw_load_font(video_adapter_t *adp, int page, int size,
    int width, u_char *data, int c, int count)
{

	printf("%s: not yet\n", __func__);
	return (ENODEV);
}

static int
terasic_mtl_vidsw_show_font(video_adapter_t *adp, int page)
{

	printf("%s: not yet\n", __func__);
	return (ENODEV);
}

static int
terasic_mtl_vidsw_save_palette(video_adapter_t *adp, u_char *palette)
{

	printf("%s: not yet\n", __func__);
	return (ENODEV);
}

static int
terasic_mtl_vidsw_load_palette(video_adapter_t *adp, u_char *palette)
{

	printf("%s: not yet\n", __func__);
	return (ENODEV);
}

static int
terasic_mtl_vidsw_set_border(video_adapter_t *adp, int border)
{

	printf("%s: not yet\n", __func__);
	return (ENODEV);
}

static int
terasic_mtl_vidsw_save_state(video_adapter_t *adp, void *p, size_t size)
{

	printf("%s: not yet\n", __func__);
	return (ENODEV);
}

static int
terasic_mtl_vidsw_load_state(video_adapter_t *adp, void *p)
{

	printf("%s: not yet\n", __func__);
	return (ENODEV);
}

static int
terasic_mtl_vidsw_set_win_org(video_adapter_t *adp, off_t offset)
{

	printf("%s: not yet\n", __func__);
	return (ENODEV);
}

static int
terasic_mtl_vidsw_read_hw_cursor(video_adapter_t *adp, int *colp, int *rowp)
{
	struct terasic_mtl_softc *sc;
	uint8_t col, row;

	sc = (struct terasic_mtl_softc *)adp;
	terasic_mtl_reg_textcursor_get(sc, &col, &row);
	*colp = col;
	*rowp = row;
	return (0);
}

static int
terasic_mtl_vidsw_set_hw_cursor(video_adapter_t *adp, int col, int row)
{
	struct terasic_mtl_softc *sc;

	sc = (struct terasic_mtl_softc *)adp;
	terasic_mtl_reg_textcursor_set(sc, col, row);
	return (0);
}

static int
terasic_mtl_vidsw_set_hw_cursor_shape(video_adapter_t *adp, int base,
    int height, int celsize, int blink)
{

	printf("%s: not yet\n", __func__);
	return (ENODEV);
}

static int
terasic_mtl_vidsw_blank_display(video_adapter_t *adp, int mode)
{
	struct terasic_mtl_softc *sc;

	sc = (struct terasic_mtl_softc *)adp;
	terasic_mtl_reg_blank(sc);
	return (0);
}

static int
terasic_mtl_vidsw_mmap(video_adapter_t *adp, vm_ooffset_t offset,
    vm_paddr_t *paddr, int prot, vm_memattr_t *memattr)
{

	printf("%s: not yet\n", __func__);
	return (ENODEV);
}

static int
terasic_mtl_vidsw_ioctl(video_adapter_t *adp, u_long cmd, caddr_t data)
{

	return (fb_commonioctl(adp, cmd, data));
}

static int
terasic_mtl_vidsw_clear(video_adapter_t *adp)
{
	struct terasic_mtl_softc *sc;

	sc = (struct terasic_mtl_softc *)adp;
	printf("%s: not yet terasic_mtl_io_clear(sc);\n", __func__);
	return (0);
}

static int
terasic_mtl_vidsw_fill_rect(video_adapter_t *adp, int val, int x, int y,
    int cx, int cy)
{

	printf("%s: not yet\n", __func__);
	return (ENODEV);
}

static int
terasic_mtl_vidsw_bitblt(video_adapter_t *adp, ...)
{

	printf("%s: not yet\n", __func__);
	return (ENODEV);
}

static int
terasic_mtl_vidsw_diag(video_adapter_t *adp, int level)
{

	printf("%s: not yet\n", __func__);
	return (ENODEV);
}

static int
terasic_mtl_vidsw_save_cursor_palette(video_adapter_t *adp, u_char *palette)
{

	printf("%s: not yet\n", __func__);
	return (ENODEV);
}

static int
terasic_mtl_vidsw_load_cursor_palette(video_adapter_t *adp, u_char *palette)
{

	printf("%s: not yet\n", __func__);
	return (ENODEV);
}

static int
terasic_mtl_vidsw_copy(video_adapter_t *adp, vm_offset_t src, vm_offset_t dst,
    int n)
{

	printf("%s: not yet\n", __func__);
	return (ENODEV);
}

static int
terasic_mtl_vidsw_putp(video_adapter_t *adp, vm_offset_t off, uint32_t p,
    uint32_t a, int size, int bpp, int bit_ltor, int byte_ltor)
{

	printf("%s: not yet\n", __func__);
	return (ENODEV);
}

static int
terasic_mtl_vidsw_putc(video_adapter_t *adp, vm_offset_t off, uint8_t c,
    uint8_t a)
{
	struct terasic_mtl_softc *sc;
	u_int col, row;

	sc = (struct terasic_mtl_softc *)adp;
	col = (off % adp->va_info.vi_width);
	row = (off / adp->va_info.vi_width);
	terasic_mtl_text_putc(sc, col, row, c, a);
	return (0);
}

static int
terasic_mtl_vidsw_puts(video_adapter_t *adp, vm_offset_t off, u_int16_t *s,
    int len)
{
	int i;

	for (i = 0; i < len; i++)
		vidd_putc(adp, off + i, s[i] & 0xff, (s[i] & 0xff00) >> 8);
	return (0);
}

static int
terasic_mtl_vidsw_putm(video_adapter_t *adp, int x, int y,
    uint8_t *pixel_image, uint32_t pixel_mask, int size, int width)
{

	printf("%s: not yet\n", __func__);
	return (ENODEV);
}

/*
 * XXXRW: For historical reasons, syscons can't register video consoles
 * without a keyboard implementation.  Provide a dummy.
 */
static keyboard_switch_t	terasic_mtl_keyboard_switch;

static int
terasic_mtl_kbd_configure(int flags)
{

	return (0);
}

KEYBOARD_DRIVER(mtl_kbd, terasic_mtl_keyboard_switch,
    terasic_mtl_kbd_configure);

int
terasic_mtl_syscons_attach(struct terasic_mtl_softc *sc)
{
	int error;

	sc->mtl_text_soft  =
	    malloc(sizeof(uint16_t) * TERASIC_MTL_ROWS * TERASIC_MTL_COLS,
	    M_TERASIC_MTL, M_WAITOK | M_ZERO);
	error = terasic_mtl_vidsw_init(0, &sc->mtl_va, 0);
	if (error)
		goto out;
	error = sc_attach_unit(sc->mtl_unit, device_get_flags(sc->mtl_dev) |
	    SC_AUTODETECT_KBD);
	if (error)
		device_printf(sc->mtl_dev, "%s: sc_attach_unit failed (%d)\n",
		    __func__, error);
out:
	if (error)
		free(sc->mtl_text_soft, M_TERASIC_MTL);
	return (error);
}

void
terasic_mtl_syscons_detach(struct terasic_mtl_softc *sc)
{

	free(sc->mtl_text_soft, M_TERASIC_MTL);
	panic("%s: not supported by syscons", __func__);
}

static video_switch_t terasic_mtl_vidsw = {
	.probe =			terasic_mtl_vidsw_probe,
	.init =				terasic_mtl_vidsw_init,
	.get_info =			terasic_mtl_vidsw_get_info,
	.query_mode =			terasic_mtl_vidsw_query_mode,
	.set_mode =			terasic_mtl_vidsw_set_mode,
	.save_font =			terasic_mtl_vidsw_save_font,
	.load_font =			terasic_mtl_vidsw_load_font,
	.show_font =			terasic_mtl_vidsw_show_font,
	.save_palette =			terasic_mtl_vidsw_save_palette,
	.load_palette =			terasic_mtl_vidsw_load_palette,
	.set_border =			terasic_mtl_vidsw_set_border,
	.save_state =			terasic_mtl_vidsw_save_state,
	.load_state =			terasic_mtl_vidsw_load_state,
	.set_win_org =			terasic_mtl_vidsw_set_win_org,
	.read_hw_cursor =		terasic_mtl_vidsw_read_hw_cursor,
	.set_hw_cursor =		terasic_mtl_vidsw_set_hw_cursor,
	.set_hw_cursor_shape =		terasic_mtl_vidsw_set_hw_cursor_shape,
	.blank_display =		terasic_mtl_vidsw_blank_display,
	.mmap =				terasic_mtl_vidsw_mmap,
	.ioctl =			terasic_mtl_vidsw_ioctl,
	.clear =			terasic_mtl_vidsw_clear,
	.fill_rect =			terasic_mtl_vidsw_fill_rect,
	.bitblt =			terasic_mtl_vidsw_bitblt,
	.diag =				terasic_mtl_vidsw_diag,
	.save_cursor_palette =		terasic_mtl_vidsw_save_cursor_palette,
	.load_cursor_palette =		terasic_mtl_vidsw_load_cursor_palette,
	.copy =				terasic_mtl_vidsw_copy,
	.putp =				terasic_mtl_vidsw_putp,
	.putc =				terasic_mtl_vidsw_putc,
	.puts =				terasic_mtl_vidsw_puts,
	.putm =				terasic_mtl_vidsw_putm,
};
VIDEO_DRIVER(terasic_mtl_syscons, terasic_mtl_vidsw,
    terasic_mtl_syscons_configure);
extern sc_rndr_sw_t txtrndrsw;
RENDERER(terasic_mtl_syscons, 0, txtrndrsw, gfb_set);
RENDERER_MODULE(terasic_mtl_syscons, gfb_set);
