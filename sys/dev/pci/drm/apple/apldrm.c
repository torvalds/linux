/*	$OpenBSD: apldrm.c,v 1.2 2024/01/29 14:52:25 kettenis Exp $	*/
/*
 * Copyright (c) 2023 Mark Kettenis <kettenis@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/rasops/rasops.h>

#include <linux/platform_device.h>

#include <drm/drm_drv.h>
#include <drm/drm_framebuffer.h>

struct apldrm_softc {
	struct platform_device	sc_dev;
	struct drm_device	sc_ddev;

	int			sc_node;

	struct rasops_info	sc_ri;
	struct wsscreen_descr	sc_wsd;
	struct wsscreen_list	sc_wsl;
	struct wsscreen_descr	*sc_scrlist[1];

	void			(*sc_switchcb)(void *, int, int);
	void			*sc_switchcbarg;
	void			*sc_switchcookie;
	struct task		sc_switchtask;

	int			sc_burner_fblank;
	struct task		sc_burner_task;
};

#include "apple_drv.c"

int	apldrm_match(struct device *, void *, void *);
void	apldrm_attach(struct device *, struct device *, void *);
int	apldrm_activate(struct device *, int);

const struct cfattach apldrm_ca = {
	sizeof (struct apldrm_softc), apldrm_match, apldrm_attach,
	NULL, apldrm_activate
};

struct cfdriver apldrm_cd = {
	NULL, "apldrm", DV_DULL
};

void	apldrm_attachhook(struct device *);

int
apldrm_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "apple,display-subsystem");
}

void
apldrm_attach(struct device *parent, struct device *self, void *aux)
{
	struct apldrm_softc *sc = (struct apldrm_softc *)self;
	struct fdt_attach_args *faa = aux;

	sc->sc_node = faa->fa_node;

	printf("\n");

	sc->sc_dev.faa = faa;
	platform_device_register(&sc->sc_dev);

	drm_attach_platform((struct drm_driver *)&apple_drm_driver,
	    faa->fa_iot, faa->fa_dmat, self, &sc->sc_ddev);
	config_mountroot(self, apldrm_attachhook);
}

int
apldrm_activate(struct device *self, int act)
{
	int rv;

	switch (act) {
	case DVACT_QUIESCE:
		rv = config_activate_children(self, act);
		apple_platform_suspend(self);
		break;
	case DVACT_WAKEUP:
		apple_platform_resume(self);
		rv = config_activate_children(self, act);
		break;
	default:
		rv = config_activate_children(self, act);
		break;
	}

	return rv;
}

int
apldrm_wsioctl(void *v, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct rasops_info *ri = v;
	struct apldrm_softc *sc = ri->ri_hw;
	struct wsdisplay_param *dp = (struct wsdisplay_param *)data;
	struct wsdisplay_fbinfo *wdf;
	struct backlight_device *bd;

	bd = backlight_device_get_by_name("apple-panel-bl");

	switch (cmd) {
	case WSDISPLAYIO_GTYPE:
		*(u_int *)data = WSDISPLAY_TYPE_KMS;
		return 0;
	case WSDISPLAYIO_GINFO:
		wdf = (struct wsdisplay_fbinfo *)data;
		wdf->width = ri->ri_width;
		wdf->height = ri->ri_height;
		wdf->depth = ri->ri_depth;
		wdf->stride = ri->ri_stride;
		wdf->offset = 0; /* XXX */
		wdf->cmsize = 0;
		return 0;
	case WSDISPLAYIO_GETPARAM:
		if (bd == NULL)
			return -1;

		switch (dp->param) {
		case WSDISPLAYIO_PARAM_BRIGHTNESS:
			dp->min = 0;
			dp->max = bd->props.max_brightness;
			dp->curval = bd->props.brightness;
			return (dp->max > dp->min) ? 0 : -1;
		}
		break;
	case WSDISPLAYIO_SETPARAM:
		if (bd == NULL)
			return -1;

		switch (dp->param) {
		case WSDISPLAYIO_PARAM_BRIGHTNESS:
			bd->props.brightness = dp->curval;
			backlight_update_status(bd);
			knote_locked(&sc->sc_ddev.note, NOTE_CHANGE);
			return 0;
		}
		break;
	case WSDISPLAYIO_SVIDEO:
	case WSDISPLAYIO_GVIDEO:
		return 0;
	}

	return (-1);
}

paddr_t
apldrm_wsmmap(void *v, off_t off, int prot)
{
	return (-1);
}

int
apldrm_alloc_screen(void *v, const struct wsscreen_descr *type,
    void **cookiep, int *curxp, int *curyp, uint32_t *attrp)
{
	return rasops_alloc_screen(v, cookiep, curxp, curyp, attrp);
}

void
apldrm_free_screen(void *v, void *cookie)
{
	return rasops_free_screen(v, cookie);
}

void
apldrm_doswitch(void *v)
{
	struct rasops_info *ri = v;
	struct apldrm_softc *sc = ri->ri_hw;
	struct drm_fb_helper *fb_helper = sc->sc_ddev.fb_helper;

	rasops_show_screen(ri, sc->sc_switchcookie, 0, NULL, NULL);
	drm_fb_helper_restore_fbdev_mode_unlocked(fb_helper);

	if (sc->sc_switchcb)
		(sc->sc_switchcb)(sc->sc_switchcbarg, 0, 0);
}

int
apldrm_show_screen(void *v, void *cookie, int waitok,
    void (*cb)(void *, int, int), void *cbarg)
{
	struct rasops_info *ri = v;
	struct apldrm_softc *sc = ri->ri_hw;

	if (cookie == ri->ri_active)
		return (0);

	sc->sc_switchcb = cb;
	sc->sc_switchcbarg = cbarg;
	sc->sc_switchcookie = cookie;
	if (cb) {
		task_add(systq, &sc->sc_switchtask);
		return (EAGAIN);
	}

	apldrm_doswitch(v);

	return (0);
}

void
apldrm_enter_ddb(void *v, void *cookie)
{
	struct rasops_info *ri = v;
	struct apldrm_softc *sc = ri->ri_hw;
	struct drm_fb_helper *fb_helper = sc->sc_ddev.fb_helper;

	if (cookie == ri->ri_active)
		return;

	rasops_show_screen(ri, cookie, 0, NULL, NULL);
	drm_fb_helper_debug_enter(fb_helper->info);
}

void
apldrm_burner(void *v, u_int on, u_int flags)
{
	struct rasops_info *ri = v;
	struct apldrm_softc *sc = ri->ri_hw;

	task_del(systq, &sc->sc_burner_task);

	if (on)
		sc->sc_burner_fblank = FB_BLANK_UNBLANK;
	else {
		if (flags & WSDISPLAY_BURN_VBLANK)
			sc->sc_burner_fblank = FB_BLANK_VSYNC_SUSPEND;
		else
			sc->sc_burner_fblank = FB_BLANK_NORMAL;
	}

	/*
	 * Setting the DPMS mode may sleep while waiting for vblank so
	 * hand things off to a taskq.
	 */
	task_add(systq, &sc->sc_burner_task);
}

void
apldrm_burner_cb(void *arg)
{
	struct apldrm_softc *sc = arg;
	struct drm_fb_helper *fb_helper = sc->sc_ddev.fb_helper;

	drm_fb_helper_blank(sc->sc_burner_fblank, fb_helper->info);
}

struct wsdisplay_accessops apldrm_accessops = {
	.ioctl = apldrm_wsioctl,
	.mmap = apldrm_wsmmap,
	.alloc_screen = apldrm_alloc_screen,
	.free_screen = apldrm_free_screen,
	.show_screen = apldrm_show_screen,
	.enter_ddb = apldrm_enter_ddb,
	.getchar = rasops_getchar,
	.load_font = rasops_load_font,
	.list_font = rasops_list_font,
	.scrollback = rasops_scrollback,
	.burn_screen = apldrm_burner
};

void
apldrm_attachhook(struct device *self)
{
	struct apldrm_softc *sc = (struct apldrm_softc *)self;
	struct drm_fb_helper *fb_helper;
	struct rasops_info *ri = &sc->sc_ri;
	struct wsemuldisplaydev_attach_args waa;
	int idx, len, console = 0;
	uint32_t defattr;
	int error;

	error = apple_platform_probe(&sc->sc_dev);
	if (error)
		return;

	/*
	 * If no display coprocessors were registered with the
	 * component framework, the call above will succeed without
	 * setting up a framebuffer.  Bail if we don't have one.
	 */
	fb_helper = sc->sc_ddev.fb_helper;
	if (fb_helper == NULL)
		return;

	/* Claim framebuffer to prevent attaching other drivers. */
	len = OF_getproplen(sc->sc_node, "memory-region");
	idx = OF_getindex(sc->sc_node, "framebuffer", "memory-region-names");
	if (idx >= 0 && idx < len / sizeof(uint32_t)) {
		uint32_t *phandles;
		uint64_t reg[2];
		int node;

		phandles = malloc(len, M_TEMP, M_WAITOK | M_ZERO);
		OF_getpropintarray(sc->sc_node, "memory-region",
		    phandles, len);
		node = OF_getnodebyphandle(phandles[idx]);
		if (node) {
			if (OF_getpropint64array(node, "reg", reg,
			    sizeof(reg)) == sizeof(reg))
				rasops_claim_framebuffer(reg[0], reg[1], self);
		}
		free(phandles, M_TEMP, len);
	}

	/*
	 * Update our understanding of the console output node if
	 * we're using the framebuffer console.
	 */
	if (OF_is_compatible(stdout_node, "simple-framebuffer"))
		stdout_node = sc->sc_node;

	if (sc->sc_node == stdout_node)
		console = 1;

	ri->ri_hw = sc;
	ri->ri_bits = fb_helper->info->screen_buffer;
	ri->ri_flg = RI_CENTER | RI_VCONS | RI_WRONLY;
	ri->ri_depth = fb_helper->fb->format->cpp[0] * 8;
	ri->ri_stride = fb_helper->fb->pitches[0];
	ri->ri_width = fb_helper->info->var.xres;
	ri->ri_height = fb_helper->info->var.yres;

	switch (fb_helper->fb->format->format) {
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
		ri->ri_rnum = 8;
		ri->ri_rpos = 16;
		ri->ri_gnum = 8;
		ri->ri_gpos = 8;
		ri->ri_bnum = 8;
		ri->ri_bpos = 0;
		break;
	case DRM_FORMAT_XRGB2101010:
		ri->ri_rnum = 10;
		ri->ri_rpos = 20;
		ri->ri_gnum = 10;
		ri->ri_gpos = 10;
		ri->ri_bnum = 10;
		ri->ri_bpos = 0;
		break;
	}

	rasops_init(ri, 160, 160);

	strlcpy(sc->sc_wsd.name, "std", sizeof(sc->sc_wsd.name));
	sc->sc_wsd.capabilities = ri->ri_caps;
	sc->sc_wsd.nrows = ri->ri_rows;
	sc->sc_wsd.ncols = ri->ri_cols;
	sc->sc_wsd.textops = &ri->ri_ops;
	sc->sc_wsd.fontwidth = ri->ri_font->fontwidth;
	sc->sc_wsd.fontheight = ri->ri_font->fontheight;

	sc->sc_scrlist[0] = &sc->sc_wsd;
	sc->sc_wsl.nscreens = 1;
	sc->sc_wsl.screens = (const struct wsscreen_descr **)sc->sc_scrlist;

	task_set(&sc->sc_switchtask, apldrm_doswitch, ri);
	task_set(&sc->sc_burner_task, apldrm_burner_cb, sc);

	if (console) {
		ri->ri_ops.pack_attr(ri->ri_active, 0, 0, 0, &defattr);
		wsdisplay_cnattach(&sc->sc_wsd, ri->ri_active,
		    ri->ri_ccol, ri->ri_crow, defattr);
	}

	memset(&waa, 0, sizeof(waa));
	waa.scrdata = &sc->sc_wsl;
	waa.accessops = &apldrm_accessops;
	waa.accesscookie = ri;
	waa.console = console;

	printf("%s: %dx%d, %dbpp\n", sc->sc_dev.dev.dv_xname,
	    ri->ri_width, ri->ri_height, ri->ri_depth);

	config_found_sm(self, &waa, wsemuldisplaydevprint,
	    wsemuldisplaydevsubmatch);
}
