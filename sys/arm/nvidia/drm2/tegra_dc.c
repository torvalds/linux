/*-
 * Copyright (c) 2015 Michal Meloun
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/gpio.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/sysctl.h>

#include <machine/bus.h>

#include <dev/extres/clk/clk.h>
#include <dev/extres/hwreset/hwreset.h>
#include <dev/drm2/drmP.h>
#include <dev/drm2/drm_crtc_helper.h>
#include <dev/drm2/drm_fb_helper.h>
#include <dev/drm2/drm_fixed.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/nvidia/drm2/tegra_dc_reg.h>
#include <arm/nvidia/drm2/tegra_drm.h>
#include <arm/nvidia/tegra_pmc.h>

#include "tegra_drm_if.h"
#include "tegra_dc_if.h"

#define	WR4(_sc, _r, _v)	bus_write_4((_sc)->mem_res, 4 * (_r), (_v))
#define	RD4(_sc, _r)		bus_read_4((_sc)->mem_res, 4 * (_r))

#define	LOCK(_sc)		mtx_lock(&(_sc)->mtx)
#define	UNLOCK(_sc)		mtx_unlock(&(_sc)->mtx)
#define	SLEEP(_sc, timeout)						\
	mtx_sleep(sc, &sc->mtx, 0, "tegra_dc_wait", timeout);
#define	LOCK_INIT(_sc)							\
	mtx_init(&_sc->mtx, device_get_nameunit(_sc->dev), "tegra_dc", MTX_DEF)
#define	LOCK_DESTROY(_sc)	mtx_destroy(&_sc->mtx)
#define	ASSERT_LOCKED(_sc)	mtx_assert(&_sc->mtx, MA_OWNED)
#define	ASSERT_UNLOCKED(_sc)	mtx_assert(&_sc->mtx, MA_NOTOWNED)


#define	SYNCPT_VBLANK0 26
#define	SYNCPT_VBLANK1 27

#define	DC_MAX_PLANES 2		/* Maximum planes */

/* DRM Formats supported by DC */
/* XXXX expand me */
static uint32_t dc_plane_formats[] = {
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YUV420,
	DRM_FORMAT_YUV422,
};


/* Complete description of one window (plane) */
struct dc_window {
	/* Source (in framebuffer) rectangle, in pixels */
	u_int			src_x;
	u_int			src_y;
	u_int			src_w;
	u_int			src_h;

	/* Destination (on display) rectangle, in pixels */
	u_int			dst_x;
	u_int			dst_y;
	u_int			dst_w;
	u_int			dst_h;

	/* Parsed pixel format */
	u_int			bits_per_pixel;
	bool			is_yuv;		/* any YUV mode */
	bool			is_yuv_planar;	/* planar YUV mode */
	uint32_t 		color_mode;	/* DC_WIN_COLOR_DEPTH */
	uint32_t		swap;		/* DC_WIN_BYTE_SWAP */
	uint32_t		surface_kind;	/* DC_WINBUF_SURFACE_KIND */
	uint32_t		block_height;	/* DC_WINBUF_SURFACE_KIND */

	/* Parsed flipping, rotation is not supported for pitched modes */
	bool			flip_x;		/* inverted X-axis */
	bool			flip_y;		/* inverted Y-axis */
	bool			transpose_xy;	/* swap X and Y-axis */

	/* Color planes base addresses and strides */
	bus_size_t		base[3];
	uint32_t		stride[3];	/* stride[2] isn't used by HW */
};

struct dc_softc {
	device_t		dev;
	struct resource		*mem_res;
	struct resource		*irq_res;
	void			*irq_ih;
	struct mtx		mtx;

	clk_t			clk_parent;
	clk_t			clk_dc;
	hwreset_t		hwreset_dc;

	int			pitch_align;

	struct tegra_crtc 	tegra_crtc;
	struct drm_pending_vblank_event *event;
	struct drm_gem_object 	*cursor_gem;
};


static struct ofw_compat_data compat_data[] = {
	{"nvidia,tegra124-dc",	1},
	{NULL,			0},
};

/* Convert standard drm pixel format to tegra windows parameters. */
static int
dc_parse_drm_format(struct tegra_fb *fb, struct dc_window *win)
{
	struct tegra_bo *bo;
	uint32_t cm;
	uint32_t sw;
	bool is_yuv, is_yuv_planar;
	int nplanes, i;

	switch (fb->drm_fb.pixel_format) {
	case DRM_FORMAT_XBGR8888:
		sw = BYTE_SWAP(NOSWAP);
		cm = WIN_COLOR_DEPTH_R8G8B8A8;
		is_yuv = false;
		is_yuv_planar = false;
		break;

	case DRM_FORMAT_XRGB8888:
		sw = BYTE_SWAP(NOSWAP);
		cm = WIN_COLOR_DEPTH_B8G8R8A8;
		is_yuv = false;
		is_yuv_planar = false;
		break;

	case DRM_FORMAT_RGB565:
		sw = BYTE_SWAP(NOSWAP);
		cm = WIN_COLOR_DEPTH_B5G6R5;
		is_yuv = false;
		is_yuv_planar = false;
		break;

	case DRM_FORMAT_UYVY:
		sw = BYTE_SWAP(NOSWAP);
		cm = WIN_COLOR_DEPTH_YCbCr422;
		is_yuv = true;
		is_yuv_planar = false;
		break;

	case DRM_FORMAT_YUYV:
		sw = BYTE_SWAP(SWAP2);
		cm = WIN_COLOR_DEPTH_YCbCr422;
		is_yuv = true;
		is_yuv_planar = false;
		break;

	case DRM_FORMAT_YUV420:
		sw = BYTE_SWAP(NOSWAP);
		cm = WIN_COLOR_DEPTH_YCbCr420P;
		is_yuv = true;
		is_yuv_planar = true;
		break;

	case DRM_FORMAT_YUV422:
		sw = BYTE_SWAP(NOSWAP);
		cm = WIN_COLOR_DEPTH_YCbCr422P;
		is_yuv = true;
		is_yuv_planar = true;
		break;

	default:
		/* Unsupported format */
		return (-EINVAL);
	}

	/* Basic check of arguments. */
	switch (fb->rotation) {
	case 0:
	case 180:
		break;

	case 90: 		/* Rotation is supported only */
	case 270:		/*  for block linear surfaces */
		if (!fb->block_linear)
			return (-EINVAL);
		break;

	default:
		return (-EINVAL);
	}
	/* XXX Add more checks (sizes, scaling...) */

	if (win == NULL)
		return (0);

	win->surface_kind =
	    fb->block_linear ? SURFACE_KIND_BL_16B2: SURFACE_KIND_PITCH;
	win->block_height = fb->block_height;
	switch (fb->rotation) {
	case 0:					/* (0,0,0) */
		win->transpose_xy = false;
		win->flip_x = false;
		win->flip_y = false;
		break;

	case 90:				/* (1,0,1) */
		win->transpose_xy = true;
		win->flip_x = false;
		win->flip_y = true;
		break;

	case 180:				/* (0,1,1) */
		win->transpose_xy = false;
		win->flip_x = true;
		win->flip_y = true;
		break;

	case 270:				/* (1,1,0) */
		win->transpose_xy = true;
		win->flip_x = true;
		win->flip_y = false;
		break;
	}
	win->flip_x ^= fb->flip_x;
	win->flip_y ^= fb->flip_y;

	win->color_mode = cm;
	win->swap = sw;
	win->bits_per_pixel = fb->drm_fb.bits_per_pixel;
	win->is_yuv = is_yuv;
	win->is_yuv_planar = is_yuv_planar;

	nplanes = drm_format_num_planes(fb->drm_fb.pixel_format);
	for (i = 0; i < nplanes; i++) {
		bo = fb->planes[i];
		win->base[i] = bo->pbase + fb->drm_fb.offsets[i];
		win->stride[i] = fb->drm_fb.pitches[i];
	}
	return (0);
}

/*
 * Scaling functions.
 *
 * It's unclear if we want/must program the fractional portion
 * (aka bias) of init_dda registers, mainly when mirrored axis
 * modes are used.
 * For now, we use 1.0 as recommended by TRM.
 */
static inline uint32_t
dc_scaling_init(uint32_t start)
{

	return (1 << 12);
}

static inline uint32_t
dc_scaling_incr(uint32_t src, uint32_t dst, uint32_t maxscale)
{
	uint32_t val;

	val = (src - 1) << 12 ; /* 4.12 fixed float */
	val /= (dst - 1);
	if (val  > (maxscale << 12))
		val = maxscale << 12;
	return val;
}

/* -------------------------------------------------------------------
 *
 *    HW Access.
 *
 */

/*
 * Setup pixel clock.
 * Minimal frequency is pixel clock, but output is free to select
 * any higher.
 */
static int
dc_setup_clk(struct dc_softc *sc, struct drm_crtc *crtc,
    struct drm_display_mode *mode, uint32_t *div)
{
	uint64_t pclk, freq;
	struct tegra_drm_encoder *output;
	struct drm_encoder *encoder;
	long rv;

	pclk = mode->clock * 1000;

	/* Find attached encoder */
	output = NULL;
	list_for_each_entry(encoder, &crtc->dev->mode_config.encoder_list,
	    head) {
		if (encoder->crtc == crtc) {
			output = container_of(encoder, struct tegra_drm_encoder,
			    encoder);
			break;
		}
	}
	if (output == NULL)
		return (-ENODEV);

	if (output->setup_clock == NULL)
		panic("Output have not setup_clock function.\n");
	rv = output->setup_clock(output, sc->clk_dc, pclk);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot setup pixel clock: %llu\n",
		    pclk);
		return (rv);
	}

	rv = clk_get_freq(sc->clk_dc, &freq);
	*div = (freq * 2 / pclk) - 2;

	DRM_DEBUG_KMS("frequency: %llu, DC divider: %u\n", freq, *div);

	return 0;
}

static void
dc_setup_window(struct dc_softc *sc, unsigned int index, struct dc_window *win)
{
	uint32_t h_offset, v_offset, h_size, v_size, bpp;
	uint32_t h_init_dda, v_init_dda, h_incr_dda, v_incr_dda;
	uint32_t val;

#ifdef DMR_DEBUG_WINDOW
	printf("%s window: %d\n", __func__, index);
	printf("  src: x: %d, y: %d, w: %d, h: %d\n",
	   win->src_x, win->src_y, win->src_w, win->src_h);
	printf("  dst: x: %d, y: %d, w: %d, h: %d\n",
	   win->dst_x, win->dst_y, win->dst_w, win->dst_h);
	printf("  bpp: %d, color_mode: %d, swap: %d\n",
	   win->bits_per_pixel, win->color_mode, win->swap);
#endif

	if (win->is_yuv)
		bpp = win->is_yuv_planar ? 1 : 2;
	else
		bpp = (win->bits_per_pixel + 7) / 8;

	if (!win->transpose_xy) {
		h_size = win->src_w * bpp;
		v_size = win->src_h;
	} else {
		h_size = win->src_h * bpp;
		v_size = win->src_w;
	}

	h_offset = win->src_x * bpp;;
	v_offset = win->src_y;
	if (win->flip_x) {
		h_offset += win->src_w * bpp - 1;
	}
	if (win->flip_y)
		v_offset += win->src_h - 1;

	/* Adjust offsets for planar yuv modes */
	if (win->is_yuv_planar) {
		h_offset &= ~1;
		if (win->flip_x )
			h_offset |= 1;
		v_offset &= ~1;
		if (win->flip_y )
			v_offset |= 1;
	}

	/* Setup scaling. */
	if (!win->transpose_xy) {
		h_init_dda = dc_scaling_init(win->src_x);
		v_init_dda = dc_scaling_init(win->src_y);
		h_incr_dda = dc_scaling_incr(win->src_w, win->dst_w, 4);
		v_incr_dda = dc_scaling_incr(win->src_h, win->dst_h, 15);
	} else {
		h_init_dda =  dc_scaling_init(win->src_y);
		v_init_dda =  dc_scaling_init(win->src_x);
		h_incr_dda = dc_scaling_incr(win->src_h, win->dst_h, 4);
		v_incr_dda = dc_scaling_incr(win->src_w, win->dst_w, 15);
	}
#ifdef DMR_DEBUG_WINDOW
	printf("\n");
	printf("  bpp: %d, size: h: %d v: %d, offset: h:%d v: %d\n",
	   bpp, h_size, v_size, h_offset, v_offset);
	printf("  init_dda: h: %d v: %d, incr_dda: h: %d v: %d\n",
	   h_init_dda, v_init_dda, h_incr_dda, v_incr_dda);
#endif

	LOCK(sc);

	/* Select target window  */
	val = WINDOW_A_SELECT << index;
	WR4(sc, DC_CMD_DISPLAY_WINDOW_HEADER, val);

	/* Sizes */
	WR4(sc, DC_WIN_POSITION, WIN_POSITION(win->dst_x, win->dst_y));
	WR4(sc, DC_WIN_SIZE, WIN_SIZE(win->dst_w, win->dst_h));
	WR4(sc, DC_WIN_PRESCALED_SIZE, WIN_PRESCALED_SIZE(h_size, v_size));

	/* DDA */
	WR4(sc, DC_WIN_DDA_INCREMENT,
	    WIN_DDA_INCREMENT(h_incr_dda, v_incr_dda));
	WR4(sc, DC_WIN_H_INITIAL_DDA, h_init_dda);
	WR4(sc, DC_WIN_V_INITIAL_DDA, v_init_dda);

	/* Color planes base addresses and strides */
	WR4(sc, DC_WINBUF_START_ADDR, win->base[0]);
	if (win->is_yuv_planar) {
		WR4(sc, DC_WINBUF_START_ADDR_U, win->base[1]);
		WR4(sc, DC_WINBUF_START_ADDR_V, win->base[2]);
		WR4(sc, DC_WIN_LINE_STRIDE,
		     win->stride[1] << 16 | win->stride[0]);
	} else {
		WR4(sc, DC_WIN_LINE_STRIDE, win->stride[0]);
	}

	/* Offsets for rotation and axis flip */
	WR4(sc, DC_WINBUF_ADDR_H_OFFSET, h_offset);
	WR4(sc, DC_WINBUF_ADDR_V_OFFSET, v_offset);

	/* Color format */
	WR4(sc, DC_WIN_COLOR_DEPTH, win->color_mode);
	WR4(sc, DC_WIN_BYTE_SWAP, win->swap);

	/* Tiling */
	val = win->surface_kind;
	if (win->surface_kind == SURFACE_KIND_BL_16B2)
		val |= SURFACE_KIND_BLOCK_HEIGHT(win->block_height);
	WR4(sc, DC_WINBUF_SURFACE_KIND, val);

	/* Color space coefs for YUV modes */
	if (win->is_yuv) {
		WR4(sc, DC_WINC_CSC_YOF,   0x00f0);
		WR4(sc, DC_WINC_CSC_KYRGB, 0x012a);
		WR4(sc, DC_WINC_CSC_KUR,   0x0000);
		WR4(sc, DC_WINC_CSC_KVR,   0x0198);
		WR4(sc, DC_WINC_CSC_KUG,   0x039b);
		WR4(sc, DC_WINC_CSC_KVG,   0x032f);
		WR4(sc, DC_WINC_CSC_KUB,   0x0204);
		WR4(sc, DC_WINC_CSC_KVB,   0x0000);
	}

	val = WIN_ENABLE;
	if (win->is_yuv)
		val |= CSC_ENABLE;
	else if (win->bits_per_pixel < 24)
		val |= COLOR_EXPAND;
	if (win->flip_y)
		val |= V_DIRECTION;
	if (win->flip_x)
		val |= H_DIRECTION;
	if (win->transpose_xy)
		val |= SCAN_COLUMN;
	WR4(sc, DC_WINC_WIN_OPTIONS, val);

#ifdef DMR_DEBUG_WINDOW
	/* Set underflow debug mode -> highlight missing pixels. */
	WR4(sc, DC_WINBUF_UFLOW_CTRL, UFLOW_CTR_ENABLE);
	WR4(sc, DC_WINBUF_UFLOW_DBG_PIXEL, 0xFFFF0000);
#endif

	UNLOCK(sc);
}

/* -------------------------------------------------------------------
 *
 *    Plane functions.
 *
 */
static int
dc_plane_update(struct drm_plane *drm_plane, struct drm_crtc *drm_crtc,
    struct drm_framebuffer *drm_fb,
    int crtc_x, int crtc_y, unsigned int crtc_w, unsigned int crtc_h,
    uint32_t src_x, uint32_t src_y, uint32_t src_w, uint32_t src_h)
{
	struct tegra_plane *plane;
	struct tegra_crtc *crtc;
	struct tegra_fb *fb;
	struct dc_softc *sc;
	struct dc_window win;
	int rv;

	plane = container_of(drm_plane, struct tegra_plane, drm_plane);
	fb = container_of(drm_fb, struct tegra_fb, drm_fb);
	crtc = container_of(drm_crtc, struct tegra_crtc, drm_crtc);
	sc = device_get_softc(crtc->dev);

	memset(&win, 0, sizeof(win));
	win.src_x = src_x >> 16;
	win.src_y = src_y >> 16;
	win.src_w = src_w >> 16;
	win.src_h = src_h >> 16;
	win.dst_x = crtc_x;
	win.dst_y = crtc_y;
	win.dst_w = crtc_w;
	win.dst_h = crtc_h;

	rv = dc_parse_drm_format(fb, &win);
	if (rv != 0) {
		DRM_WARNING("unsupported pixel format %d\n",
		    fb->drm_fb.pixel_format);
		return (rv);
	}

	dc_setup_window(sc, plane->index, &win);

	WR4(sc, DC_CMD_STATE_CONTROL, WIN_A_UPDATE << plane->index);
	WR4(sc, DC_CMD_STATE_CONTROL, WIN_A_ACT_REQ << plane->index);

	return (0);
}

static int
dc_plane_disable(struct drm_plane *drm_plane)
{
	struct tegra_plane *plane;
	struct tegra_crtc *crtc;
	struct dc_softc *sc;
	uint32_t val, idx;

	if (drm_plane->crtc == NULL)
		return (0);
	plane = container_of(drm_plane, struct tegra_plane, drm_plane);
	crtc = container_of(drm_plane->crtc, struct tegra_crtc, drm_crtc);

	sc = device_get_softc(crtc->dev);
	idx = plane->index;

	LOCK(sc);

	WR4(sc, DC_CMD_DISPLAY_WINDOW_HEADER, WINDOW_A_SELECT << idx);

	val = RD4(sc, DC_WINC_WIN_OPTIONS);
	val &= ~WIN_ENABLE;
	WR4(sc, DC_WINC_WIN_OPTIONS, val);

	UNLOCK(sc);

	WR4(sc, DC_CMD_STATE_CONTROL, WIN_A_UPDATE << idx);
	WR4(sc, DC_CMD_STATE_CONTROL, WIN_A_ACT_REQ << idx);

	return (0);
}

static void
dc_plane_destroy(struct drm_plane *plane)
{

	dc_plane_disable(plane);
	drm_plane_cleanup(plane);
	free(plane, DRM_MEM_KMS);
}

static const struct drm_plane_funcs dc_plane_funcs = {
	.update_plane = dc_plane_update,
	.disable_plane = dc_plane_disable,
	.destroy = dc_plane_destroy,
};

/* -------------------------------------------------------------------
 *
 *    CRTC helper functions.
 *
 */
static void
dc_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	/* Empty function */
}

static bool
dc_crtc_mode_fixup(struct drm_crtc *crtc, const struct drm_display_mode *mode,
    struct drm_display_mode *adjusted)
{

	return (true);
}

static int
dc_set_base(struct dc_softc *sc, int x, int y, struct tegra_fb *fb)
{
	struct dc_window win;
	int rv;

	memset(&win, 0, sizeof(win));
	win.src_x = x;
	win.src_y = y;
	win.src_w = fb->drm_fb.width;
	win.src_h = fb->drm_fb.height;
	win.dst_x = x;
	win.dst_y = y;
	win.dst_w = fb->drm_fb.width;
	win.dst_h = fb->drm_fb.height;

	rv = dc_parse_drm_format(fb, &win);
	if (rv != 0) {
		DRM_WARNING("unsupported pixel format %d\n",
		    fb->drm_fb.pixel_format);
		return (rv);
	}
	dc_setup_window(sc, 0, &win);

	return (0);
}

static int
dc_crtc_mode_set(struct drm_crtc *drm_crtc, struct drm_display_mode *mode,
    struct drm_display_mode *adjusted, int x, int y,
    struct drm_framebuffer *old_fb)
{
	struct dc_softc *sc;
	struct tegra_crtc *crtc;
	struct tegra_fb *fb;
	struct dc_window win;
	uint32_t div, h_ref_to_sync, v_ref_to_sync;
	int rv;

	crtc = container_of(drm_crtc, struct tegra_crtc, drm_crtc);
	sc = device_get_softc(crtc->dev);
	fb = container_of(drm_crtc->fb, struct tegra_fb, drm_fb);


	h_ref_to_sync = 1;
	v_ref_to_sync = 1;
	/* Setup timing */
	rv = dc_setup_clk(sc, drm_crtc, mode, &div);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot set pixel clock\n");
		return (rv);
	}

	/* Timing */
	WR4(sc, DC_DISP_DISP_TIMING_OPTIONS, 0);

	WR4(sc, DC_DISP_REF_TO_SYNC,
	    (v_ref_to_sync << 16) |
	     h_ref_to_sync);

	WR4(sc, DC_DISP_SYNC_WIDTH,
	    ((mode->vsync_end - mode->vsync_start) << 16) |
	    ((mode->hsync_end - mode->hsync_start) <<  0));

	WR4(sc, DC_DISP_BACK_PORCH,
	    ((mode->vtotal - mode->vsync_end) << 16) |
	    ((mode->htotal - mode->hsync_end) <<  0));

	WR4(sc, DC_DISP_FRONT_PORCH,
	    ((mode->vsync_start - mode->vdisplay) << 16) |
	    ((mode->hsync_start - mode->hdisplay) <<  0));

	WR4(sc, DC_DISP_DISP_ACTIVE,
	    (mode->vdisplay << 16) | mode->hdisplay);

	WR4(sc, DC_DISP_DISP_INTERFACE_CONTROL, DISP_DATA_FORMAT(DF1P1C));

	WR4(sc,DC_DISP_DISP_CLOCK_CONTROL,
	    SHIFT_CLK_DIVIDER(div) | PIXEL_CLK_DIVIDER(PCD1));

	memset(&win, 0, sizeof(win));
	win.src_x = x;
	win.src_y = y;
	win.src_w = mode->hdisplay;
	win.src_h = mode->vdisplay;
	win.dst_x = x;
	win.dst_y = y;
	win.dst_w = mode->hdisplay;
	win.dst_h = mode->vdisplay;

	rv = dc_parse_drm_format(fb, &win);
	if (rv != 0) {
		DRM_WARNING("unsupported pixel format %d\n",
		    drm_crtc->fb->pixel_format);
		return (rv);
	}

	dc_setup_window(sc, 0, &win);

	return (0);

}

static int
dc_crtc_mode_set_base(struct drm_crtc *drm_crtc, int x, int y,
    struct drm_framebuffer *old_fb)
{
	struct dc_softc *sc;
	struct tegra_crtc *crtc;
	struct tegra_fb *fb;
	int rv;

	crtc = container_of(drm_crtc, struct tegra_crtc, drm_crtc);
	fb = container_of(drm_crtc->fb, struct tegra_fb, drm_fb);
	sc = device_get_softc(crtc->dev);

	rv = dc_set_base(sc, x, y, fb);

	/* Commit */
	WR4(sc, DC_CMD_STATE_CONTROL, GENERAL_UPDATE | WIN_A_UPDATE);
	WR4(sc, DC_CMD_STATE_CONTROL, GENERAL_ACT_REQ | WIN_A_ACT_REQ);
	return (rv);
}


static void
dc_crtc_prepare(struct drm_crtc *drm_crtc)
{

	struct dc_softc *sc;
	struct tegra_crtc *crtc;
	uint32_t val;

	crtc = container_of(drm_crtc, struct tegra_crtc, drm_crtc);
	sc = device_get_softc(crtc->dev);

	WR4(sc, DC_CMD_GENERAL_INCR_SYNCPT_CNTRL, SYNCPT_CNTRL_NO_STALL);
	/* XXX allocate syncpoint from host1x */
	WR4(sc, DC_CMD_CONT_SYNCPT_VSYNC, SYNCPT_VSYNC_ENABLE |
	    (sc->tegra_crtc.nvidia_head == 0 ? SYNCPT_VBLANK0: SYNCPT_VBLANK1));

	WR4(sc, DC_CMD_DISPLAY_POWER_CONTROL,
	    PW0_ENABLE | PW1_ENABLE | PW2_ENABLE | PW3_ENABLE |
	    PW4_ENABLE | PM0_ENABLE | PM1_ENABLE);

	val = RD4(sc, DC_CMD_DISPLAY_COMMAND);
	val |= DISPLAY_CTRL_MODE(CTRL_MODE_C_DISPLAY);
	WR4(sc, DC_CMD_DISPLAY_COMMAND, val);

	WR4(sc, DC_CMD_INT_MASK,
	    WIN_A_UF_INT | WIN_B_UF_INT | WIN_C_UF_INT |
	    WIN_A_OF_INT | WIN_B_OF_INT | WIN_C_OF_INT);

	WR4(sc, DC_CMD_INT_ENABLE,
	    VBLANK_INT | WIN_A_UF_INT | WIN_B_UF_INT | WIN_C_UF_INT |
	    WIN_A_OF_INT | WIN_B_OF_INT | WIN_C_OF_INT);
}

static void
dc_crtc_commit(struct drm_crtc *drm_crtc)
{
	struct dc_softc *sc;
	struct tegra_crtc *crtc;
	uint32_t val;

	crtc = container_of(drm_crtc, struct tegra_crtc, drm_crtc);
	sc = device_get_softc(crtc->dev);

	WR4(sc, DC_CMD_STATE_CONTROL, GENERAL_UPDATE | WIN_A_UPDATE);

	val = RD4(sc, DC_CMD_INT_MASK);
	val |= FRAME_END_INT;
	WR4(sc, DC_CMD_INT_MASK, val);

	val = RD4(sc, DC_CMD_INT_ENABLE);
	val |= FRAME_END_INT;
	WR4(sc, DC_CMD_INT_ENABLE, val);

	WR4(sc, DC_CMD_STATE_CONTROL,  GENERAL_ACT_REQ | WIN_A_ACT_REQ);
}

static void
dc_crtc_load_lut(struct drm_crtc *crtc)
{

	/* empty function */
}

static const struct drm_crtc_helper_funcs dc_crtc_helper_funcs = {
	.dpms = dc_crtc_dpms,
	.mode_fixup = dc_crtc_mode_fixup,
	.mode_set = dc_crtc_mode_set,
	.mode_set_base = dc_crtc_mode_set_base,
	.prepare = dc_crtc_prepare,
	.commit = dc_crtc_commit,
	.load_lut = dc_crtc_load_lut,
};

static int
drm_crtc_index(struct drm_crtc *crtc)
{
	int idx;
	struct drm_crtc *tmp;

	idx = 0;
	list_for_each_entry(tmp, &crtc->dev->mode_config.crtc_list, head) {
		if (tmp == crtc)
			return (idx);
		idx++;
	}
	panic("Cannot find CRTC");
}

/* -------------------------------------------------------------------
 *
 *   Exported functions (mainly vsync related).
 *
 * XXX revisit this -> convert to bus methods?
 */
int
tegra_dc_get_pipe(struct drm_crtc *drm_crtc)
{
	struct tegra_crtc *crtc;

	crtc = container_of(drm_crtc, struct tegra_crtc, drm_crtc);
	return (crtc->nvidia_head);
}

void
tegra_dc_enable_vblank(struct drm_crtc *drm_crtc)
{
	struct dc_softc *sc;
	struct tegra_crtc *crtc;
	uint32_t val;

	crtc = container_of(drm_crtc, struct tegra_crtc, drm_crtc);
	sc = device_get_softc(crtc->dev);

	LOCK(sc);
	val = RD4(sc, DC_CMD_INT_MASK);
	val |= VBLANK_INT;
	WR4(sc, DC_CMD_INT_MASK, val);
	UNLOCK(sc);
}

void
tegra_dc_disable_vblank(struct drm_crtc *drm_crtc)
{
	struct dc_softc *sc;
	struct tegra_crtc *crtc;
	uint32_t val;

	crtc = container_of(drm_crtc, struct tegra_crtc, drm_crtc);
	sc = device_get_softc(crtc->dev);

	LOCK(sc);
	val = RD4(sc, DC_CMD_INT_MASK);
	val &= ~VBLANK_INT;
	WR4(sc, DC_CMD_INT_MASK, val);
	UNLOCK(sc);
}

static void
dc_finish_page_flip(struct dc_softc *sc)
{
	struct drm_crtc *drm_crtc;
	struct drm_device *drm;
	struct tegra_fb *fb;
	struct tegra_bo *bo;
	uint32_t base;
	int idx;

	drm_crtc = &sc->tegra_crtc.drm_crtc;
	drm = drm_crtc->dev;
	fb = container_of(drm_crtc->fb, struct tegra_fb, drm_fb);

	mtx_lock(&drm->event_lock);

	if (sc->event == NULL) {
		mtx_unlock(&drm->event_lock);
		return;
	}

	LOCK(sc);
	/* Read active copy of WINBUF_START_ADDR */
	WR4(sc, DC_CMD_DISPLAY_WINDOW_HEADER, WINDOW_A_SELECT);
	WR4(sc, DC_CMD_STATE_ACCESS, READ_MUX);
	base = RD4(sc, DC_WINBUF_START_ADDR);
	WR4(sc, DC_CMD_STATE_ACCESS, 0);
	UNLOCK(sc);

	/* Is already active */
	bo = tegra_fb_get_plane(fb, 0);
	if (base == (bo->pbase + fb->drm_fb.offsets[0])) {
		idx = drm_crtc_index(drm_crtc);
		drm_send_vblank_event(drm, idx, sc->event);
		drm_vblank_put(drm, idx);
		sc->event = NULL;
	}

	mtx_unlock(&drm->event_lock);
}


void
tegra_dc_cancel_page_flip(struct drm_crtc *drm_crtc, struct drm_file *file)
{
	struct dc_softc *sc;
	struct tegra_crtc *crtc;
	struct drm_device *drm;

	crtc = container_of(drm_crtc, struct tegra_crtc, drm_crtc);
	sc = device_get_softc(crtc->dev);
	drm = drm_crtc->dev;
	mtx_lock(&drm->event_lock);

	if ((sc->event != NULL) && (sc->event->base.file_priv == file)) {
		sc->event->base.destroy(&sc->event->base);
		drm_vblank_put(drm, drm_crtc_index(drm_crtc));
		sc->event = NULL;
	}
	mtx_unlock(&drm->event_lock);
}

/* -------------------------------------------------------------------
 *
 *    CRTC functions.
 *
 */
static int
dc_page_flip(struct drm_crtc *drm_crtc, struct drm_framebuffer *drm_fb,
    struct drm_pending_vblank_event *event)
{
	struct dc_softc *sc;
	struct tegra_crtc *crtc;
	struct tegra_fb *fb;
	struct drm_device *drm;

	crtc = container_of(drm_crtc, struct tegra_crtc, drm_crtc);
	sc = device_get_softc(crtc->dev);
	fb = container_of(drm_crtc->fb, struct tegra_fb, drm_fb);
	drm = drm_crtc->dev;

	if (sc->event != NULL)
		return (-EBUSY);

	if (event != NULL) {
		event->pipe = sc->tegra_crtc.nvidia_head;
		sc->event = event;
		drm_vblank_get(drm, event->pipe);
	}

	dc_set_base(sc, drm_crtc->x, drm_crtc->y, fb);
	drm_crtc->fb = drm_fb;

	/* Commit */
	WR4(sc, DC_CMD_STATE_CONTROL, GENERAL_UPDATE | WIN_A_UPDATE);

	return (0);
}

static int
dc_cursor_set(struct drm_crtc *drm_crtc, struct drm_file *file,
    uint32_t handle, uint32_t width, uint32_t height)
{

	struct dc_softc *sc;
	struct tegra_crtc *crtc;
	struct drm_gem_object *gem;
	struct tegra_bo *bo;
	int i;
	uint32_t val, *src, *dst;

	crtc = container_of(drm_crtc, struct tegra_crtc, drm_crtc);
	sc = device_get_softc(crtc->dev);

	if (width != height)
		return (-EINVAL);

	switch (width) {
	case 32:
		val = CURSOR_SIZE(C32x32);
		break;
	case 64:
		val = CURSOR_SIZE(C64x64);
		break;
	case 128:
		val = CURSOR_SIZE(C128x128);
		break;
	case 256:
		val = CURSOR_SIZE(C256x256);
		break;
	default:
		return (-EINVAL);
	}

	bo = NULL;
	gem = NULL;
	if (handle != 0) {
		gem = drm_gem_object_lookup(drm_crtc->dev, file, handle);
		if (gem == NULL)
			return (-ENOENT);
		bo = container_of(gem, struct tegra_bo, gem_obj);
	}

	if (sc->cursor_gem != NULL) {
		drm_gem_object_unreference(sc->cursor_gem);
	}
	sc->cursor_gem = gem;

	if (bo != NULL) {
		/*
		 * Copy cursor into cache and convert it from ARGB to RGBA.
		 * XXXX - this is broken by design - client can write to BO at
		 * any time. We can dedicate other window for cursor or switch
		 * to sw cursor in worst case.
		 */
		src = (uint32_t *)bo->vbase;
		dst = (uint32_t *)crtc->cursor_vbase;
		for (i = 0; i < width * height; i++)
			dst[i] = (src[i] << 8) | (src[i] >> 24);

		val |= CURSOR_CLIP(CC_DISPLAY);
		val |= CURSOR_START_ADDR(crtc->cursor_pbase);
		WR4(sc, DC_DISP_CURSOR_START_ADDR, val);

		val = RD4(sc, DC_DISP_BLEND_CURSOR_CONTROL);
		val &= ~CURSOR_DST_BLEND_FACTOR_SELECT(~0);
		val &= ~CURSOR_SRC_BLEND_FACTOR_SELECT(~0);
		val |= CURSOR_MODE_SELECT;
		val |= CURSOR_DST_BLEND_FACTOR_SELECT(DST_NEG_K1_TIMES_SRC);
		val |= CURSOR_SRC_BLEND_FACTOR_SELECT(SRC_BLEND_K1_TIMES_SRC);
		val |= CURSOR_ALPHA(~0);
		WR4(sc, DC_DISP_BLEND_CURSOR_CONTROL, val);

		val = RD4(sc, DC_DISP_DISP_WIN_OPTIONS);
		val |= CURSOR_ENABLE;
		WR4(sc, DC_DISP_DISP_WIN_OPTIONS, val);
	} else {
		val = RD4(sc, DC_DISP_DISP_WIN_OPTIONS);
		val &= ~CURSOR_ENABLE;
		WR4(sc, DC_DISP_DISP_WIN_OPTIONS, val);
	}

	/* XXX This fixes cursor underflow issues, but why ?  */
	WR4(sc, DC_DISP_CURSOR_UNDERFLOW_CTRL, CURSOR_UFLOW_CYA);

	WR4(sc, DC_CMD_STATE_CONTROL, GENERAL_UPDATE | CURSOR_UPDATE );
	WR4(sc, DC_CMD_STATE_CONTROL, GENERAL_ACT_REQ | CURSOR_ACT_REQ);
	return (0);
}

static int
dc_cursor_move(struct drm_crtc *drm_crtc, int x, int y)
{
	struct dc_softc *sc;
	struct tegra_crtc *crtc;

	crtc = container_of(drm_crtc, struct tegra_crtc, drm_crtc);
	sc = device_get_softc(crtc->dev);
	WR4(sc, DC_DISP_CURSOR_POSITION, CURSOR_POSITION(x, y));

	WR4(sc, DC_CMD_STATE_CONTROL, CURSOR_UPDATE);
	WR4(sc, DC_CMD_STATE_CONTROL, CURSOR_ACT_REQ);

	return (0);
}

static void
dc_destroy(struct drm_crtc *crtc)
{

	drm_crtc_cleanup(crtc);
	memset(crtc, 0, sizeof(*crtc));
}

static const struct drm_crtc_funcs dc_crtc_funcs = {
	.page_flip = dc_page_flip,
	.cursor_set = dc_cursor_set,
	.cursor_move = dc_cursor_move,
	.set_config = drm_crtc_helper_set_config,
	.destroy = dc_destroy,
};

/* -------------------------------------------------------------------
 *
 *    Bus and infrastructure.
 *
 */
static int
dc_init_planes(struct dc_softc *sc, struct tegra_drm *drm)
{
	int i, rv;
	struct tegra_plane *plane;

	rv = 0;
	for (i = 0; i < DC_MAX_PLANES; i++) {
		plane = malloc(sizeof(*plane), DRM_MEM_KMS, M_WAITOK | M_ZERO);
		plane->index = i + 1;
		rv = drm_plane_init(&drm->drm_dev, &plane->drm_plane,
		    1 << sc->tegra_crtc.nvidia_head, &dc_plane_funcs,
		    dc_plane_formats, nitems(dc_plane_formats), false);
		if (rv != 0) {
			free(plane, DRM_MEM_KMS);
			return (rv);
		}
	}
	return 0;
}

static void
dc_display_enable(device_t dev, bool enable)
{
	struct dc_softc *sc;
	uint32_t val;

	sc = device_get_softc(dev);

	/* Set display mode */
	val = enable ? CTRL_MODE_C_DISPLAY: CTRL_MODE_STOP;
	WR4(sc, DC_CMD_DISPLAY_COMMAND, DISPLAY_CTRL_MODE(val));

	/* and commit it*/
	WR4(sc, DC_CMD_STATE_CONTROL, GENERAL_UPDATE);
	WR4(sc, DC_CMD_STATE_CONTROL, GENERAL_ACT_REQ);
}

static void
dc_hdmi_enable(device_t dev, bool enable)
{
	struct dc_softc *sc;
	uint32_t val;

	sc = device_get_softc(dev);

	val = RD4(sc, DC_DISP_DISP_WIN_OPTIONS);
	if (enable)
		val |= HDMI_ENABLE;
	else
		val &= ~HDMI_ENABLE;
	WR4(sc, DC_DISP_DISP_WIN_OPTIONS, val);

}

static void
dc_setup_timing(device_t dev, int h_pulse_start)
{
	struct dc_softc *sc;

	sc = device_get_softc(dev);

	/* Setup display timing */
	WR4(sc, DC_DISP_DISP_TIMING_OPTIONS, VSYNC_H_POSITION(1));
	WR4(sc, DC_DISP_DISP_COLOR_CONTROL,
	    DITHER_CONTROL(DITHER_DISABLE) | BASE_COLOR_SIZE(SIZE_BASE888));

	WR4(sc, DC_DISP_DISP_SIGNAL_OPTIONS0, H_PULSE2_ENABLE);
	WR4(sc, DC_DISP_H_PULSE2_CONTROL,
	    PULSE_CONTROL_QUAL(QUAL_VACTIVE) | PULSE_CONTROL_LAST(LAST_END_A));

	WR4(sc, DC_DISP_H_PULSE2_POSITION_A,
	    PULSE_START(h_pulse_start) | PULSE_END(h_pulse_start + 8));
}

static void
dc_intr(void *arg)
{
	struct dc_softc *sc;
	uint32_t status;

	sc = arg;

	/* Confirm interrupt */
	status = RD4(sc, DC_CMD_INT_STATUS);
	WR4(sc, DC_CMD_INT_STATUS, status);
	if (status & VBLANK_INT) {
		drm_handle_vblank(sc->tegra_crtc.drm_crtc.dev,
		    sc->tegra_crtc.nvidia_head);
		dc_finish_page_flip(sc);
	}
}

static int
dc_init_client(device_t dev, device_t host1x, struct tegra_drm *drm)
{
	struct dc_softc *sc;
	int rv;

	sc = device_get_softc(dev);

	if (drm->pitch_align < sc->pitch_align)
		drm->pitch_align = sc->pitch_align;

	drm_crtc_init(&drm->drm_dev, &sc->tegra_crtc.drm_crtc, &dc_crtc_funcs);
	drm_mode_crtc_set_gamma_size(&sc->tegra_crtc.drm_crtc, 256);
	drm_crtc_helper_add(&sc->tegra_crtc.drm_crtc, &dc_crtc_helper_funcs);

	rv = dc_init_planes(sc, drm);
	if (rv!= 0){
		device_printf(dev, "Cannot init planes\n");
		return (rv);
	}

	WR4(sc, DC_CMD_INT_TYPE,
	    WIN_A_UF_INT | WIN_B_UF_INT | WIN_C_UF_INT |
	    WIN_A_OF_INT | WIN_B_OF_INT | WIN_C_OF_INT);

	WR4(sc, DC_CMD_INT_POLARITY,
	    WIN_A_UF_INT | WIN_B_UF_INT | WIN_C_UF_INT |
	    WIN_A_OF_INT | WIN_B_OF_INT | WIN_C_OF_INT);

	WR4(sc, DC_CMD_INT_ENABLE, 0);
	WR4(sc, DC_CMD_INT_MASK, 0);

	rv = bus_setup_intr(dev, sc->irq_res, INTR_TYPE_MISC | INTR_MPSAFE,
	    NULL, dc_intr, sc, &sc->irq_ih);
	if (rv != 0) {
		device_printf(dev, "Cannot register interrupt handler\n");
		return (rv);
	}

	/* allocate memory for cursor cache */
	sc->tegra_crtc.cursor_vbase = kmem_alloc_contig(256 * 256 * 4,
	    M_WAITOK | M_ZERO, 0, -1UL, PAGE_SIZE, 0,
	    VM_MEMATTR_WRITE_COMBINING);
	sc->tegra_crtc.cursor_pbase = vtophys(sc->tegra_crtc.cursor_vbase);
	return (0);
}

static int
dc_exit_client(device_t dev, device_t host1x, struct tegra_drm *drm)
{
	struct dc_softc *sc;

	sc = device_get_softc(dev);

	if (sc->irq_ih != NULL)
		bus_teardown_intr(dev, sc->irq_res, sc->irq_ih);
	sc->irq_ih = NULL;

	return (0);
}

static int
get_fdt_resources(struct dc_softc *sc, phandle_t node)
{
	int rv;

	rv = hwreset_get_by_ofw_name(sc->dev, 0, "dc", &sc->hwreset_dc);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'dc' reset\n");
		return (rv);
	}
	rv = clk_get_by_ofw_name(sc->dev, 0, "parent", &sc->clk_parent);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'parent' clock\n");
		return (rv);
	}
	rv = clk_get_by_ofw_name(sc->dev, 0, "dc", &sc->clk_dc);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'dc' clock\n");
		return (rv);
	}

	rv = OF_getencprop(node, "nvidia,head", &sc->tegra_crtc.nvidia_head,
	    sizeof(sc->tegra_crtc.nvidia_head));
	if (rv <= 0) {
		device_printf(sc->dev,
		    "Cannot get 'nvidia,head' property\n");
		return (rv);
	}
	return (0);
}

static int
enable_fdt_resources(struct dc_softc *sc)
{
	int id, rv;

	rv = clk_set_parent_by_clk(sc->clk_dc, sc->clk_parent);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot set parent for 'dc' clock\n");
		return (rv);
	}

	id = (sc->tegra_crtc.nvidia_head == 0) ?
	    TEGRA_POWERGATE_DIS: TEGRA_POWERGATE_DISB;
	rv = tegra_powergate_sequence_power_up(id, sc->clk_dc, sc->hwreset_dc);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot enable 'DIS' powergate\n");
		return (rv);
	}

	return (0);
}

static int
dc_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "Tegra Display Controller");
	return (BUS_PROBE_DEFAULT);
}

static int
dc_attach(device_t dev)
{
	struct dc_softc *sc;
	phandle_t node;
	int rid, rv;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->tegra_crtc.dev = dev;

	node = ofw_bus_get_node(sc->dev);
	LOCK_INIT(sc);

	rid = 0;
	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->mem_res == NULL) {
		device_printf(dev, "Cannot allocate memory resources\n");
		goto fail;
	}

	rid = 0;
	sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, RF_ACTIVE);
	if (sc->irq_res == NULL) {
		device_printf(dev, "Cannot allocate IRQ resources\n");
		goto fail;
	}

	rv = get_fdt_resources(sc, node);
	if (rv != 0) {
		device_printf(dev, "Cannot parse FDT resources\n");
		goto fail;
	}
	rv = enable_fdt_resources(sc);
	if (rv != 0) {
		device_printf(dev, "Cannot enable FDT resources\n");
		goto fail;
	}

	/*
	 * Tegra124
	 *  -  64 for RGB modes
	 *  - 128 for YUV planar modes
	 *  - 256 for block linear modes
	 */
	sc->pitch_align = 256;

	rv = TEGRA_DRM_REGISTER_CLIENT(device_get_parent(sc->dev), sc->dev);
	if (rv != 0) {
		device_printf(dev, "Cannot register DRM device\n");
		goto fail;
	}

	return (bus_generic_attach(dev));

fail:
	TEGRA_DRM_DEREGISTER_CLIENT(device_get_parent(sc->dev), sc->dev);
	if (sc->irq_ih != NULL)
		bus_teardown_intr(dev, sc->irq_res, sc->irq_ih);
	if (sc->clk_parent != NULL)
		clk_release(sc->clk_parent);
	if (sc->clk_dc != NULL)
		clk_release(sc->clk_dc);
	if (sc->hwreset_dc != NULL)
		hwreset_release(sc->hwreset_dc);
	if (sc->irq_res != NULL)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->irq_res);
	if (sc->mem_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->mem_res);
	LOCK_DESTROY(sc);

	return (ENXIO);
}

static int
dc_detach(device_t dev)
{
	struct dc_softc *sc;

	sc = device_get_softc(dev);

	TEGRA_DRM_DEREGISTER_CLIENT(device_get_parent(sc->dev), sc->dev);

	if (sc->irq_ih != NULL)
		bus_teardown_intr(dev, sc->irq_res, sc->irq_ih);
	if (sc->clk_parent != NULL)
		clk_release(sc->clk_parent);
	if (sc->clk_dc != NULL)
		clk_release(sc->clk_dc);
	if (sc->hwreset_dc != NULL)
		hwreset_release(sc->hwreset_dc);
	if (sc->irq_res != NULL)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->irq_res);
	if (sc->mem_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->mem_res);
	LOCK_DESTROY(sc);

	return (bus_generic_detach(dev));
}

static device_method_t tegra_dc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,			dc_probe),
	DEVMETHOD(device_attach,		dc_attach),
	DEVMETHOD(device_detach,		dc_detach),

	/* tegra drm interface */
	DEVMETHOD(tegra_drm_init_client,	dc_init_client),
	DEVMETHOD(tegra_drm_exit_client,	dc_exit_client),

	/* tegra dc interface */
	DEVMETHOD(tegra_dc_display_enable,	dc_display_enable),
	DEVMETHOD(tegra_dc_hdmi_enable,		dc_hdmi_enable),
	DEVMETHOD(tegra_dc_setup_timing,	dc_setup_timing),

	DEVMETHOD_END
};

static devclass_t tegra_dc_devclass;
DEFINE_CLASS_0(tegra_dc, tegra_dc_driver, tegra_dc_methods,
    sizeof(struct dc_softc));
DRIVER_MODULE(tegra_dc, host1x, tegra_dc_driver, tegra_dc_devclass, NULL, NULL);
