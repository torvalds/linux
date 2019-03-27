/*-
 * Copyright 1992-2015 Michal Meloun
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
 *
 * $FreeBSD$
 */
#ifndef _TEGRA_DRM_H_
#define _TEGRA_DRM_H_

#include <dev/gpio/gpiobusvar.h>

struct tegra_bo {
 	struct drm_gem_object	gem_obj;
	/* mapped memory buffer */
	vm_paddr_t		pbase;
	vm_offset_t		vbase;
	size_t			npages;
	vm_page_t 		*m;
	vm_object_t		cdev_pager;
};

struct tegra_plane {
	struct drm_plane	drm_plane;
	int			index;		/* Window index */
};

struct tegra_fb {
	struct drm_framebuffer	drm_fb;
	struct drm_fb_helper	fb_helper;
	struct tegra_bo		**planes;	/* Attached planes */
	int			nplanes;

	/* Surface and display geometry */
	bool			block_linear;	/* Surface_kind */
	uint32_t		block_height;
	int			rotation; 	/* In degrees */
	bool			flip_x;		/* Inverted X-axis */
	bool			flip_y;		/* Inverted Y-axis */
};

struct tegra_crtc {
	struct drm_crtc 	drm_crtc;
	device_t		dev;
	int			nvidia_head;
	vm_paddr_t		cursor_pbase;	/* Cursor buffer */
	vm_offset_t		cursor_vbase;
};

struct tegra_drm_encoder {
	device_t 		dev;

	void 			*panel;		/* XXX For LVDS panel */
	device_t  		ddc;
	struct edid 		*edid;

	gpio_pin_t		gpio_hpd;

	struct drm_encoder 	encoder;
	struct drm_connector 	connector;
	int			(*setup_clock)(struct tegra_drm_encoder *output,
				    clk_t clk, uint64_t pclk);
};

struct tegra_drm {
	struct drm_device 	drm_dev;
	struct tegra_fb 	*fb;		/* Prime framebuffer */
	int			pitch_align;
};

/* tegra_drm_subr.c */
int tegra_drm_encoder_attach(struct tegra_drm_encoder *output, phandle_t node);
int tegra_drm_encoder_init(struct tegra_drm_encoder *output,
    struct tegra_drm *drm);
int tegra_drm_encoder_exit(struct tegra_drm_encoder *output,
    struct tegra_drm *drm);
enum drm_connector_status tegra_drm_connector_detect(
    struct drm_connector *connector, bool force);
int tegra_drm_connector_get_modes(struct drm_connector *connector);
struct drm_encoder *tegra_drm_connector_best_encoder(
    struct drm_connector *connector);

/* tegra_dc.c */
void tegra_dc_cancel_page_flip(struct drm_crtc *drm_crtc,
    struct drm_file *file);
void tegra_dc_enable_vblank(struct drm_crtc *drm_crtc);
void tegra_dc_disable_vblank(struct drm_crtc *drm_crtc);
int tegra_dc_get_pipe(struct drm_crtc *drm_crtc);

/* tegra_fb.c */
struct fb_info *tegra_drm_fb_getinfo(struct drm_device *drm);
struct tegra_bo *tegra_fb_get_plane(struct tegra_fb *fb, int idx);
int tegra_drm_fb_create(struct drm_device *drm, struct drm_file *file,
    struct drm_mode_fb_cmd2 *cmd, struct drm_framebuffer **fb_res);
int tegra_drm_fb_init(struct drm_device *drm);
void tegra_drm_fb_destroy(struct drm_device *drm);


/* tegra_bo.c */
struct tegra_bo;
int tegra_bo_create(struct drm_device *drm, size_t size,
    struct tegra_bo **res_bo);
void tegra_bo_driver_register(struct drm_driver *drm_drv);

#endif /* _TEGRA_DRM_H_ */
