/* $OpenBSD: rkdrm.h,v 1.5 2024/08/21 11:24:12 jsg Exp $ */
/* $NetBSD: rk_drm.h,v 1.1 2019/11/09 23:30:14 jmcneill Exp $ */
/*-
 * Copyright (c) 2019 Jared D. McNeill <jmcneill@invisible.ca>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _ARM_RK_DRM_H
#define _ARM_RK_DRM_H

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/rasops/rasops.h>

#include <drm/drm_framebuffer.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_dma_helper.h>

#define DRIVER_AUTHOR		"Jared McNeill"

#define DRIVER_NAME		"rk"
#define DRIVER_DESC		"Rockchip Display Subsystem"
#define DRIVER_DATE		"20191109"

#define DRIVER_MAJOR		1
#define DRIVER_MINOR		0
#define DRIVER_PATCHLEVEL	0

#define	RK_DRM_MAX_CRTC	2

struct rkdrm_vblank {
	void			*priv;
	void			(*enable_vblank)(void *);
	void			(*disable_vblank)(void *);
	uint32_t		(*get_vblank_counter)(void *);
};

struct rkdrm_softc {
	struct device		sc_dev;
	struct drm_device	sc_ddev;

	bus_space_tag_t		sc_iot;
	bus_dma_tag_t		sc_dmat;

	int			sc_node;

	struct rkdrm_vblank	sc_vbl[RK_DRM_MAX_CRTC];

	void			(*switchcb)(void *, int, int);
	void			*switchcbarg;
	void			*switchcookie;
	struct task		switchtask;
	struct rasops_info	ro;
	int			console;
	int			primary;

	struct drm_fb_helper	helper;
};

struct rkdrm_framebuffer {
	struct drm_framebuffer	base;
	struct drm_gem_dma_object *obj;
};

#define to_rkdrm_framebuffer(x)	container_of(x, struct rkdrm_framebuffer, base)

#endif /* _ARM_RK_DRM_H */
