/* $OpenBSD: rkdrm.c,v 1.23 2024/08/21 11:24:12 jsg Exp $ */
/* $NetBSD: rk_drm.c,v 1.3 2019/12/15 01:00:58 mrg Exp $ */
/*-
 * Copyright (c) 2019 Jared D. McNeill <jmcneill@invisible.ca>
 * Copyright (c) 2020 Patrick Wildt <patrick@blueri.se>
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

#include <sys/param.h>
#include <sys/device.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_misc.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem.h>

#include <dev/fdt/rkdrm.h>

#define	RK_DRM_MAX_WIDTH	3840
#define	RK_DRM_MAX_HEIGHT	2160

int	rkdrm_match(struct device *, void *, void *);
void	rkdrm_attach(struct device *, struct device *, void *);
void	rkdrm_attachhook(struct device *);

int	rkdrm_unload(struct drm_device *);

struct drm_driver rkdrm_driver = {
	.driver_features = DRIVER_ATOMIC | DRIVER_MODESET | DRIVER_GEM,

	.dumb_create = drm_gem_dma_dumb_create,
	.dumb_map_offset = drm_gem_dumb_map_offset,

	.gem_fault = drm_gem_dma_fault,

	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};

const struct drm_gem_object_funcs rkdrm_gem_object_funcs = {
	.free = drm_gem_dma_free_object,
};

const struct cfattach rkdrm_ca = {
	sizeof (struct rkdrm_softc), rkdrm_match, rkdrm_attach
};

struct cfdriver rkdrm_cd = {
	NULL, "rkdrm", DV_DULL
};

int
rkdrm_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "rockchip,display-subsystem");
}

void
rkdrm_attach(struct device *parent, struct device *self, void *aux)
{
	struct rkdrm_softc *sc = (struct rkdrm_softc *)self;
	struct fdt_attach_args *faa = aux;

	sc->sc_dmat = faa->fa_dmat;
	sc->sc_iot = faa->fa_iot;
	sc->sc_node = faa->fa_node;

	printf("\n");

	/*
	 * Update our understanding of the console output node if
	 * we're using the framebuffer console.
	 */
	if (OF_is_compatible(stdout_node, "simple-framebuffer"))
		stdout_node = sc->sc_node;

	drm_attach_platform(&rkdrm_driver, faa->fa_iot, faa->fa_dmat, self,
	    &sc->sc_ddev);
	config_mountroot(self, rkdrm_attachhook);
}

int
rkdrm_fb_create_handle(struct drm_framebuffer *fb,
    struct drm_file *file, unsigned int *handle)
{
	struct rkdrm_framebuffer *sfb = to_rkdrm_framebuffer(fb);

	return drm_gem_handle_create(file, &sfb->obj->base, handle);
}

void
rkdrm_fb_destroy(struct drm_framebuffer *fb)
{
	struct rkdrm_framebuffer *sfb = to_rkdrm_framebuffer(fb);

	drm_framebuffer_cleanup(fb);
	drm_gem_object_put(&sfb->obj->base);
	free(sfb, M_DRM, sizeof(*sfb));
}

struct drm_framebuffer_funcs rkdrm_framebuffer_funcs = {
	.create_handle = rkdrm_fb_create_handle,
	.destroy = rkdrm_fb_destroy,
};

struct drm_framebuffer *
rkdrm_fb_create(struct drm_device *ddev, struct drm_file *file,
    const struct drm_mode_fb_cmd2 *cmd)
{
	struct rkdrm_framebuffer *fb;
	struct drm_gem_object *gem_obj;
	int error;

	if (cmd->flags)
		return NULL;

	gem_obj = drm_gem_object_lookup(file, cmd->handles[0]);
	if (gem_obj == NULL)
		return NULL;

	fb = malloc(sizeof(*fb), M_DRM, M_ZERO | M_WAITOK);
	drm_helper_mode_fill_fb_struct(ddev, &fb->base, cmd);
	fb->base.format = drm_format_info(DRM_FORMAT_ARGB8888);
	fb->base.obj[0] = gem_obj;
	fb->obj = to_drm_gem_dma_obj(gem_obj);

	error = drm_framebuffer_init(ddev, &fb->base, &rkdrm_framebuffer_funcs);
	if (error != 0)
		goto dealloc;

	return &fb->base;

dealloc:
	drm_framebuffer_cleanup(&fb->base);
	free(fb, M_DRM, sizeof(*fb));
	drm_gem_object_put(gem_obj);

	return NULL;
}

struct drm_mode_config_helper_funcs rkdrm_mode_config_helper_funcs =
{
	.atomic_commit_tail = drm_atomic_helper_commit_tail_rpm,
};

struct drm_mode_config_funcs rkdrm_mode_config_funcs = {
	.fb_create = rkdrm_fb_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

int rkdrm_fb_probe(struct drm_fb_helper *, struct drm_fb_helper_surface_size *);

struct drm_fb_helper_funcs rkdrm_fb_helper_funcs = {
	.fb_probe = rkdrm_fb_probe,
};

int
rkdrm_unload(struct drm_device *ddev)
{
	drm_mode_config_cleanup(ddev);

	return 0;
}

void rkdrm_burner(void *, u_int, u_int);
int rkdrm_wsioctl(void *, u_long, caddr_t, int, struct proc *);
paddr_t rkdrm_wsmmap(void *, off_t, int);
int rkdrm_alloc_screen(void *, const struct wsscreen_descr *,
    void **, int *, int *, uint32_t *);
void rkdrm_free_screen(void *, void *);
int rkdrm_show_screen(void *, void *, int,
    void (*)(void *, int, int), void *);
void rkdrm_doswitch(void *);
void rkdrm_enter_ddb(void *, void *);

struct wsscreen_descr rkdrm_stdscreen = {
	"std",
	0, 0,
	0,
	0, 0,
	WSSCREEN_UNDERLINE | WSSCREEN_HILIT |
	WSSCREEN_REVERSE | WSSCREEN_WSCOLORS
};

const struct wsscreen_descr *rkdrm_scrlist[] = {
	&rkdrm_stdscreen,
};

struct wsscreen_list rkdrm_screenlist = {
	nitems(rkdrm_scrlist), rkdrm_scrlist
};

struct wsdisplay_accessops rkdrm_accessops = {
	.ioctl = rkdrm_wsioctl,
	.mmap = rkdrm_wsmmap,
	.alloc_screen = rkdrm_alloc_screen,
	.free_screen = rkdrm_free_screen,
	.show_screen = rkdrm_show_screen,
	.enter_ddb = rkdrm_enter_ddb,
	.getchar = rasops_getchar,
	.load_font = rasops_load_font,
	.list_font = rasops_list_font,
	.scrollback = rasops_scrollback,
#ifdef notyet
	.burn_screen = rkdrm_burner
#endif
};

int
rkdrm_wsioctl(void *v, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct rasops_info *ri = v;
	struct wsdisplay_param *dp = (struct wsdisplay_param *)data;
	struct wsdisplay_fbinfo *wdf;

	switch (cmd) {
	case WSDISPLAYIO_GETPARAM:
		if (ws_get_param)
			return ws_get_param(dp);
		return -1;
	case WSDISPLAYIO_SETPARAM:
		if (ws_set_param)
			return ws_set_param(dp);
		return -1;
	case WSDISPLAYIO_GTYPE:
		*(u_int *)data = WSDISPLAY_TYPE_KMS;
		return 0;
	case WSDISPLAYIO_GINFO:
		wdf = (struct wsdisplay_fbinfo *)data;
		wdf->width = ri->ri_width;
		wdf->height = ri->ri_height;
		wdf->depth = ri->ri_depth;
		wdf->stride = ri->ri_stride;
		wdf->offset = 0;
		wdf->cmsize = 0;
		return 0;
	case WSDISPLAYIO_LINEBYTES:
		*(u_int *)data = ri->ri_stride;
		return 0;
	case WSDISPLAYIO_SVIDEO:
	case WSDISPLAYIO_GVIDEO:
		return 0;
	}

	return (-1);
}

paddr_t
rkdrm_wsmmap(void *v, off_t off, int prot)
{
	struct rasops_info *ri = v;
	struct rkdrm_softc *sc = ri->ri_hw;
	struct drm_fb_helper *helper = &sc->helper;
	struct rkdrm_framebuffer *sfb = to_rkdrm_framebuffer(helper->fb);
	uint64_t paddr = (uint64_t)sfb->obj->dmamap->dm_segs[0].ds_addr;
	size_t size = sfb->obj->dmamap->dm_segs[0].ds_len;

	if (off < 0 || off >= size)
		return -1;

	return ((paddr + off) | PMAP_NOCACHE);
}

int
rkdrm_alloc_screen(void *v, const struct wsscreen_descr *type,
    void **cookiep, int *curxp, int *curyp, uint32_t *attrp)
{
	return rasops_alloc_screen(v, cookiep, curxp, curyp, attrp);
}

void
rkdrm_free_screen(void *v, void *cookie)
{
	return rasops_free_screen(v, cookie);
}

int
rkdrm_show_screen(void *v, void *cookie, int waitok,
    void (*cb)(void *, int, int), void *cbarg)
{
	struct rasops_info *ri = v;
	struct rkdrm_softc *sc = ri->ri_hw;

	if (cookie == ri->ri_active)
		return (0);

	sc->switchcb = cb;
	sc->switchcbarg = cbarg;
	sc->switchcookie = cookie;
	if (cb) {
		task_add(systq, &sc->switchtask);
		return (EAGAIN);
	}

	rkdrm_doswitch(v);

	return (0);
}

void
rkdrm_doswitch(void *v)
{
	struct rasops_info *ri = v;
	struct rkdrm_softc *sc = ri->ri_hw;

	rasops_show_screen(ri, sc->switchcookie, 0, NULL, NULL);
	drm_fb_helper_restore_fbdev_mode_unlocked(&sc->helper);

	if (sc->switchcb)
		(sc->switchcb)(sc->switchcbarg, 0, 0);
}

void
rkdrm_enter_ddb(void *v, void *cookie)
{
	struct rasops_info *ri = v;
	struct rkdrm_softc *sc = ri->ri_hw;
	struct drm_fb_helper *fb_helper = &sc->helper;

	if (cookie == ri->ri_active)
		return;

	rasops_show_screen(ri, cookie, 0, NULL, NULL);
	drm_fb_helper_debug_enter(fb_helper->info);
}

void
rkdrm_attachhook(struct device *dev)
{
	struct rkdrm_softc *sc = (struct rkdrm_softc *)dev;
	struct wsemuldisplaydev_attach_args aa;
	struct drm_fb_helper *helper = &sc->helper;
	struct rasops_info *ri = &sc->ro;
	struct rkdrm_framebuffer *sfb;
	uint32_t *ports;
	int i, portslen, nports;
	int console = 0;
	uint32_t defattr;
	int error;

	if (sc->sc_node == stdout_node)
		console = 1;

	portslen = OF_getproplen(sc->sc_node, "ports");
	if (portslen < 0) {
		printf("%s: no display interface ports specified\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	drm_mode_config_init(&sc->sc_ddev);
	sc->sc_ddev.mode_config.min_width = 0;
	sc->sc_ddev.mode_config.min_height = 0;
	sc->sc_ddev.mode_config.max_width = RK_DRM_MAX_WIDTH;
	sc->sc_ddev.mode_config.max_height = RK_DRM_MAX_HEIGHT;
	sc->sc_ddev.mode_config.funcs = &rkdrm_mode_config_funcs;
	sc->sc_ddev.mode_config.helper_private =
	    &rkdrm_mode_config_helper_funcs;

	nports = 0;
	ports = malloc(portslen, M_TEMP, M_WAITOK);
	OF_getpropintarray(sc->sc_node, "ports", ports, portslen);
	for (i = 0; i < portslen / sizeof(uint32_t); i++) {
		error = device_port_activate(ports[i], &sc->sc_ddev);
		if (error == 0)
			nports++;
	}
	free(ports, M_TEMP, portslen);

	if (nports == 0) {
		printf("%s: no display interface ports configured\n",
		    sc->sc_dev.dv_xname);
		drm_mode_config_cleanup(&sc->sc_ddev);
		return;
	}

	drm_mode_config_reset(&sc->sc_ddev);

	drm_fb_helper_prepare(&sc->sc_ddev, &sc->helper, 32,
	    &rkdrm_fb_helper_funcs);
	if (drm_fb_helper_init(&sc->sc_ddev, &sc->helper)) {
		printf("%s: can't initialize framebuffer helper\n",
		    sc->sc_dev.dv_xname);
		drm_mode_config_cleanup(&sc->sc_ddev);
		return;
	}

	sc->helper.fb = malloc(sizeof(struct rkdrm_framebuffer),
	    M_DRM, M_WAITOK | M_ZERO);

	drm_fb_helper_initial_config(&sc->helper);

	task_set(&sc->switchtask, rkdrm_doswitch, ri);

	drm_fb_helper_restore_fbdev_mode_unlocked(&sc->helper);

	sfb = to_rkdrm_framebuffer(helper->fb);
	ri->ri_bits = sfb->obj->vaddr;
	ri->ri_flg = RI_CENTER | RI_VCONS;
	ri->ri_depth = helper->fb->format->depth;
	ri->ri_width = helper->fb->width;
	ri->ri_height = helper->fb->height;
	ri->ri_stride = ri->ri_width * ri->ri_depth / 8;
	ri->ri_rnum = 8;	/* ARGB8888 */
	ri->ri_rpos = 16;
	ri->ri_gnum = 8;
	ri->ri_gpos = 8;
	ri->ri_bnum = 8;
	ri->ri_bpos = 0;
	rasops_init(ri, 160, 160);
	ri->ri_hw = sc;

	rkdrm_stdscreen.capabilities = ri->ri_caps;
	rkdrm_stdscreen.nrows = ri->ri_rows;
	rkdrm_stdscreen.ncols = ri->ri_cols;
	rkdrm_stdscreen.textops = &ri->ri_ops;
	rkdrm_stdscreen.fontwidth = ri->ri_font->fontwidth;
	rkdrm_stdscreen.fontheight = ri->ri_font->fontheight;

	if (console) {
		ri->ri_ops.pack_attr(ri->ri_active, 0, 0, 0, &defattr);
		wsdisplay_cnattach(&rkdrm_stdscreen, ri->ri_active,
		    ri->ri_ccol, ri->ri_crow, defattr);
	}

	memset(&aa, 0, sizeof(aa));
	aa.scrdata = &rkdrm_screenlist;
	aa.accessops = &rkdrm_accessops;
	aa.accesscookie = ri;
	aa.console = console;

	printf("%s: %dx%d, %dbpp\n", sc->sc_dev.dv_xname,
	    ri->ri_width, ri->ri_height, ri->ri_depth);

	config_found_sm(&sc->sc_dev, &aa, wsemuldisplaydevprint,
	    wsemuldisplaydevsubmatch);

	drm_dev_register(&sc->sc_ddev, 0);
}

int
rkdrm_fb_probe(struct drm_fb_helper *helper, struct drm_fb_helper_surface_size *sizes)
{
	struct drm_device *ddev = helper->dev;
	struct rkdrm_framebuffer *sfb = to_rkdrm_framebuffer(helper->fb);
	struct drm_mode_fb_cmd2 mode_cmd = { 0 };
	struct drm_framebuffer *fb = helper->fb;
	unsigned int bytes_per_pixel;
	struct fb_info *info;
	size_t size;
	int error;

	bytes_per_pixel = DIV_ROUND_UP(sizes->surface_bpp, 8);

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;
	mode_cmd.pitches[0] = sizes->surface_width * bytes_per_pixel;
	mode_cmd.pixel_format = drm_mode_legacy_fb_format(sizes->surface_bpp,
	    sizes->surface_depth);

	size = roundup(mode_cmd.pitches[0] * mode_cmd.height, PAGE_SIZE);

	/* FIXME: CMA pool? */

	sfb->obj = drm_gem_dma_create(ddev, size);
	if (sfb->obj == NULL) {
		DRM_ERROR("failed to allocate memory for framebuffer\n");
		return -ENOMEM;
	}

	drm_helper_mode_fill_fb_struct(ddev, fb, &mode_cmd);
	fb->format = drm_format_info(DRM_FORMAT_ARGB8888);
	fb->obj[0] = &sfb->obj->base;
	error = drm_framebuffer_init(ddev, fb, &rkdrm_framebuffer_funcs);
	if (error != 0) {
		DRM_ERROR("failed to initialize framebuffer\n");
		return error;
	}

	info = drm_fb_helper_alloc_info(helper);
	if (IS_ERR(info)) {
		DRM_ERROR("Failed to allocate fb_info\n");
		return PTR_ERR(info);
	}
	info->par = helper;
	return 0;
}
