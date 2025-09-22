/*	$OpenBSD: viogpu.c,v 1.12 2025/01/16 10:33:27 sf Exp $ */

/*
 * Copyright (c) 2021-2023 joshua stein <jcs@openbsd.org>
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
#include <sys/timeout.h>

#include <uvm/uvm_extern.h>

#include <dev/pv/virtioreg.h>
#include <dev/pv/virtiovar.h>
#include <dev/pv/viogpu.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/rasops/rasops.h>

#if VIRTIO_DEBUG
#define DPRINTF(x...) printf(x)
#else
#define DPRINTF(x...)
#endif

struct viogpu_softc;

int	viogpu_match(struct device *, void *, void *);
void	viogpu_attach(struct device *, struct device *, void *);
int	viogpu_send_cmd(struct viogpu_softc *, void *, size_t, void *, size_t);
int	viogpu_vq_done(struct virtqueue *vq);
void	viogpu_rx_soft(void *arg);

int	viogpu_get_display_info(struct viogpu_softc *);
int	viogpu_create_2d(struct viogpu_softc *, int, int, int);
int	viogpu_set_scanout(struct viogpu_softc *, int, int, int, int);
int	viogpu_attach_backing(struct viogpu_softc *, int, bus_dmamap_t);
int	viogpu_transfer_to_host_2d(struct viogpu_softc *sc, int, uint32_t,
	    uint32_t);
int	viogpu_flush_resource(struct viogpu_softc *, int, uint32_t, uint32_t);

void	viogpu_repaint(void *);

int	viogpu_wsioctl(void *, u_long, caddr_t, int, struct proc *);
paddr_t	viogpu_wsmmap(void *, off_t, int);
int	viogpu_alloc_screen(void *, const struct wsscreen_descr *, void **,
	    int *, int *, uint32_t *);

#define VIOGPU_HEIGHT		160
#define VIOGPU_WIDTH		160

struct viogpu_softc {
	struct device		sc_dev;
	struct virtio_softc	*sc_virtio;
#define	VQCTRL	0
#define	VQCURS	1
	struct virtqueue	sc_vqs[2];

	bus_dma_segment_t	sc_dma_seg;
	bus_dmamap_t		sc_dma_map;
	size_t			sc_dma_size;
	void			*sc_cmd;
	int			sc_fence_id;

	int			sc_fb_width;
	int			sc_fb_height;
	bus_dma_segment_t	sc_fb_dma_seg;
	bus_dmamap_t		sc_fb_dma_map;
	size_t			sc_fb_dma_size;
	caddr_t			sc_fb_dma_kva;

	struct rasops_info	sc_ri;
	struct wsscreen_descr	sc_wsd;
	struct wsscreen_list	sc_wsl;
	struct wsscreen_descr	*sc_scrlist[1];
	int			console;
	int			primary;

	struct timeout		sc_timo;
};

static const struct virtio_feature_name viogpu_feature_names[] = {
#if VIRTIO_DEBUG
	{ VIRTIO_GPU_F_VIRGL,		"VirGL" },
	{ VIRTIO_GPU_F_EDID,		"EDID" },
#endif
	{ 0, 				NULL },
};

struct wsscreen_descr viogpu_stdscreen = { "std" };

const struct wsscreen_descr *viogpu_scrlist[] = {
	&viogpu_stdscreen,
};

struct wsscreen_list viogpu_screenlist = {
	nitems(viogpu_scrlist), viogpu_scrlist
};

struct wsdisplay_accessops viogpu_accessops = {
	.ioctl = viogpu_wsioctl,
	.mmap = viogpu_wsmmap,
	.alloc_screen = viogpu_alloc_screen,
	.free_screen = rasops_free_screen,
	.show_screen = rasops_show_screen,
	.getchar = rasops_getchar,
	.load_font = rasops_load_font,
	.list_font = rasops_list_font,
	.scrollback = rasops_scrollback,
};

const struct cfattach viogpu_ca = {
	sizeof(struct viogpu_softc),
	viogpu_match,
	viogpu_attach,
	NULL
};

struct cfdriver viogpu_cd = {
	NULL, "viogpu", DV_DULL
};

int
viogpu_match(struct device *parent, void *match, void *aux)
{
	struct virtio_attach_args *va = aux;

	if (va->va_devid == PCI_PRODUCT_VIRTIO_GPU)
		return 1;

	return 0;
}

void
viogpu_attach(struct device *parent, struct device *self, void *aux)
{
	struct viogpu_softc *sc = (struct viogpu_softc *)self;
	struct virtio_softc *vsc = (struct virtio_softc *)parent;
	struct virtio_attach_args *va = aux;
	struct wsemuldisplaydev_attach_args waa;
	struct rasops_info *ri = &sc->sc_ri;
	uint32_t defattr;
	int nsegs;

	if (vsc->sc_child != NULL) {
		printf(": child already attached for %s\n", parent->dv_xname);
		return;
	}
	vsc->sc_child = self;

	if (virtio_negotiate_features(vsc, viogpu_feature_names) != 0)
		goto err;
	if (!vsc->sc_version_1) {
		printf(": requires virtio version 1\n");
		goto err;
	}

	vsc->sc_ipl = IPL_TTY;
	softintr_establish(IPL_TTY, viogpu_rx_soft, vsc);
	sc->sc_virtio = vsc;

	/* allocate command and cursor virtqueues */
	vsc->sc_vqs = sc->sc_vqs;
	if (virtio_alloc_vq(vsc, &sc->sc_vqs[VQCTRL], VQCTRL, 1, "control")) {
		printf(": alloc_vq failed\n");
		goto err;
	}
	sc->sc_vqs[VQCTRL].vq_done = viogpu_vq_done;

	if (virtio_alloc_vq(vsc, &sc->sc_vqs[VQCURS], VQCURS, 1, "cursor")) {
		printf(": alloc_vq failed\n");
		goto err;
	}
	vsc->sc_nvqs = nitems(sc->sc_vqs);

	/* setup DMA space for sending commands */
	sc->sc_dma_size = NBPG;
	if (bus_dmamap_create(vsc->sc_dmat, sc->sc_dma_size, 1,
	    sc->sc_dma_size, 0, BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW,
	    &sc->sc_dma_map) != 0) {
		printf(": create failed");
		goto errdma;
	}
	if (bus_dmamem_alloc(vsc->sc_dmat, sc->sc_dma_size, 16, 0,
	    &sc->sc_dma_seg, 1, &nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO) != 0) {
		printf(": alloc failed");
		goto destroy;
	}
	if (bus_dmamem_map(vsc->sc_dmat, &sc->sc_dma_seg, nsegs,
	    sc->sc_dma_size, (caddr_t *)&sc->sc_cmd, BUS_DMA_NOWAIT) != 0) {
		printf(": map failed");
		goto free;
	}
	if (bus_dmamap_load(vsc->sc_dmat, sc->sc_dma_map, sc->sc_cmd,
	    sc->sc_dma_size, NULL, BUS_DMA_NOWAIT) != 0) {
		printf(": load failed");
		goto unmap;
	}

	if (virtio_attach_finish(vsc, va) != 0)
		goto unmap;

	if (viogpu_get_display_info(sc) != 0)
		goto unmap;

	/* setup DMA space for actual framebuffer */
	sc->sc_fb_dma_size = sc->sc_fb_width * sc->sc_fb_height * 4;
	if (bus_dmamap_create(vsc->sc_dmat, sc->sc_fb_dma_size, 1,
	    sc->sc_fb_dma_size, 0, BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW,
	    &sc->sc_fb_dma_map) != 0)
		goto unmap;
	if (bus_dmamem_alloc(vsc->sc_dmat, sc->sc_fb_dma_size, 1024, 0,
	    &sc->sc_fb_dma_seg, 1, &nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO) != 0)
		goto fb_destroy;
	if (bus_dmamem_map(vsc->sc_dmat, &sc->sc_fb_dma_seg, nsegs,
	    sc->sc_fb_dma_size, &sc->sc_fb_dma_kva, BUS_DMA_NOWAIT) != 0)
		goto fb_free;
	if (bus_dmamap_load(vsc->sc_dmat, sc->sc_fb_dma_map,
	    sc->sc_fb_dma_kva, sc->sc_fb_dma_size, NULL, BUS_DMA_NOWAIT) != 0)
		goto fb_unmap;

	if (viogpu_create_2d(sc, 1, sc->sc_fb_width, sc->sc_fb_height) != 0)
		goto fb_unmap;

	if (viogpu_attach_backing(sc, 1, sc->sc_fb_dma_map) != 0)
		goto fb_unmap;

	if (viogpu_set_scanout(sc, 0, 1, sc->sc_fb_width,
	    sc->sc_fb_height) != 0)
		goto fb_unmap;

	sc->console = 1;

	ri->ri_hw = sc;
	ri->ri_bits = sc->sc_fb_dma_kva;
	ri->ri_flg = RI_VCONS | RI_CENTER | RI_CLEAR | RI_WRONLY;
	ri->ri_depth = 32;
	ri->ri_width = sc->sc_fb_width;
	ri->ri_height = sc->sc_fb_height;
	ri->ri_stride = ri->ri_width * ri->ri_depth / 8;
	ri->ri_bpos = 0;	/* B8G8R8X8 */
	ri->ri_bnum = 8;
	ri->ri_gpos = 8;
	ri->ri_gnum = 8;
	ri->ri_rpos = 16;
	ri->ri_rnum = 8;
	rasops_init(ri, VIOGPU_HEIGHT, VIOGPU_WIDTH);

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

	printf(": %dx%d, %dbpp\n", ri->ri_width, ri->ri_height, ri->ri_depth);

	timeout_set(&sc->sc_timo, viogpu_repaint, sc);
	viogpu_repaint(sc);

	if (sc->console) {
		ri->ri_ops.pack_attr(ri->ri_active, 0, 0, 0, &defattr);
		wsdisplay_cnattach(&sc->sc_wsd, ri->ri_active, 0, 0, defattr);
	}

	memset(&waa, 0, sizeof(waa));
	waa.scrdata = &sc->sc_wsl;
	waa.accessops = &viogpu_accessops;
	waa.accesscookie = ri;
	waa.console = sc->console;

	config_found_sm(self, &waa, wsemuldisplaydevprint,
	    wsemuldisplaydevsubmatch);
	return;

fb_unmap:
	bus_dmamem_unmap(vsc->sc_dmat, (caddr_t)&sc->sc_fb_dma_kva,
	    sc->sc_fb_dma_size);
fb_free:
	bus_dmamem_free(vsc->sc_dmat, &sc->sc_fb_dma_seg, 1);
fb_destroy:
	bus_dmamap_destroy(vsc->sc_dmat, sc->sc_fb_dma_map);
unmap:
	bus_dmamem_unmap(vsc->sc_dmat, (caddr_t)&sc->sc_cmd, sc->sc_dma_size);
free:
	bus_dmamem_free(vsc->sc_dmat, &sc->sc_dma_seg, 1);
destroy:
	bus_dmamap_destroy(vsc->sc_dmat, sc->sc_dma_map);
errdma:
	printf(": DMA setup failed\n");
err:
	vsc->sc_child = VIRTIO_CHILD_ERROR;
	return;
}

void
viogpu_repaint(void *arg)
{
	struct viogpu_softc *sc = (struct viogpu_softc *)arg;
	int s;

	s = spltty();

	viogpu_transfer_to_host_2d(sc, 1, sc->sc_fb_width, sc->sc_fb_height);
	viogpu_flush_resource(sc, 1, sc->sc_fb_width, sc->sc_fb_height);

	timeout_add_msec(&sc->sc_timo, 10);
	splx(s);
}

int
viogpu_vq_done(struct virtqueue *vq)
{
	struct virtio_softc *vsc = vq->vq_owner;
	struct viogpu_softc *sc = (struct viogpu_softc *)vsc->sc_child;
	int slot, len;

	if (virtio_dequeue(vsc, vq, &slot, &len) != 0)
		return 0;

	bus_dmamap_sync(vsc->sc_dmat, sc->sc_dma_map, 0, sc->sc_dma_size,
	    BUS_DMASYNC_POSTREAD);

	virtio_dequeue_commit(vq, slot);

	return 1;
}

void
viogpu_rx_soft(void *arg)
{
	struct virtio_softc *vsc = (struct virtio_softc *)arg;
	struct viogpu_softc *sc = (struct viogpu_softc *)vsc->sc_child;
	struct virtqueue *vq = &sc->sc_vqs[VQCTRL];
	int slot, len;

	while (virtio_dequeue(vsc, vq, &slot, &len) == 0) {
		bus_dmamap_sync(vsc->sc_dmat, sc->sc_dma_map,
		    slot, len, BUS_DMASYNC_POSTREAD);
		virtio_dequeue_commit(vq, slot);
	}
}

int
viogpu_send_cmd(struct viogpu_softc *sc, void *cmd, size_t cmd_size, void *ret,
    size_t ret_size)
{
	struct virtio_softc *vsc = sc->sc_virtio;
	struct virtqueue *vq = &vsc->sc_vqs[VQCTRL];
	struct virtio_gpu_ctrl_hdr *hdr =
	    (struct virtio_gpu_ctrl_hdr *)sc->sc_cmd;
	struct virtio_gpu_ctrl_hdr *ret_hdr = (struct virtio_gpu_ctrl_hdr *)ret;
	int slot, r;

	memcpy(sc->sc_cmd, cmd, cmd_size);
	memset(sc->sc_cmd + cmd_size, 0, ret_size);

#if VIRTIO_DEBUG >= 3
	printf("%s: [%ld -> %ld]: ", __func__, cmd_size, ret_size);
	for (int i = 0; i < cmd_size; i++) {
		printf(" %02x", ((unsigned char *)sc->sc_cmd)[i]);
	}
	printf("\n");
#endif

	hdr->flags |= VIRTIO_GPU_FLAG_FENCE;
	hdr->fence_id = ++sc->sc_fence_id;

	r = virtio_enqueue_prep(vq, &slot);
	if (r != 0)
		panic("%s: control vq busy", sc->sc_dev.dv_xname);

 	r = bus_dmamap_load(vsc->sc_dmat, sc->sc_dma_map, sc->sc_cmd,
	    cmd_size + ret_size, NULL, BUS_DMA_NOWAIT);
	if (r != 0)
		panic("%s: dmamap load failed", sc->sc_dev.dv_xname);

	r = virtio_enqueue_reserve(vq, slot, sc->sc_dma_map->dm_nsegs + 1);
	if (r != 0)
		panic("%s: control vq busy", sc->sc_dev.dv_xname);

	bus_dmamap_sync(vsc->sc_dmat, sc->sc_dma_map, 0, cmd_size,
	    BUS_DMASYNC_PREWRITE);

	virtio_enqueue_p(vq, slot, sc->sc_dma_map, 0, cmd_size, 1);
	virtio_enqueue_p(vq, slot, sc->sc_dma_map, cmd_size, ret_size, 0);
	virtio_enqueue_commit(vsc, vq, slot, 1);

	while (virtio_check_vq(vsc, vq) == 0)
		;

	bus_dmamap_sync(vsc->sc_dmat, sc->sc_dma_map, 0, cmd_size,
	    BUS_DMASYNC_POSTWRITE);
	bus_dmamap_sync(vsc->sc_dmat, sc->sc_dma_map, cmd_size, ret_size,
	    BUS_DMASYNC_POSTREAD);

	memcpy(ret, sc->sc_cmd + cmd_size, ret_size);

	if (ret_hdr->fence_id != sc->sc_fence_id)
		printf("%s: return fence id not right (0x%llx != 0x%x)\n",
		    __func__, ret_hdr->fence_id, sc->sc_fence_id);

	return 0;
}

int
viogpu_get_display_info(struct viogpu_softc *sc)
{
	struct virtio_gpu_ctrl_hdr hdr = { 0 };
	struct virtio_gpu_resp_display_info info = { 0 };

	hdr.type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;

	viogpu_send_cmd(sc, &hdr, sizeof(hdr), &info, sizeof(info));

	if (info.hdr.type != VIRTIO_GPU_RESP_OK_DISPLAY_INFO) {
		printf("%s: failed getting display info\n",
		    sc->sc_dev.dv_xname);
		return 1;
	}

	if (!info.pmodes[0].enabled) {
		printf("%s: pmodes[0] is not enabled\n", sc->sc_dev.dv_xname);
		return 1;
	}

	sc->sc_fb_width = info.pmodes[0].r.width;
	sc->sc_fb_height = info.pmodes[0].r.height;

	return 0;
}

int
viogpu_create_2d(struct viogpu_softc *sc, int resource_id, int width,
    int height)
{
	struct virtio_gpu_resource_create_2d res = { 0 };
	struct virtio_gpu_ctrl_hdr resp = { 0 };

	res.hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
	res.resource_id = resource_id;
	res.format = VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM;
	res.width = width;
	res.height = height;

	viogpu_send_cmd(sc, &res, sizeof(res), &resp, sizeof(resp));

	if (resp.type != VIRTIO_GPU_RESP_OK_NODATA) {
		printf("%s: failed CREATE_2D: %d\n", sc->sc_dev.dv_xname,
		    resp.type);
		return 1;
	}

	return 0;
}

int
viogpu_set_scanout(struct viogpu_softc *sc, int scanout_id, int resource_id,
    int width, int height)
{
	struct virtio_gpu_set_scanout ss = { 0 };
	struct virtio_gpu_ctrl_hdr resp = { 0 };

	ss.hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
	ss.scanout_id = scanout_id;
	ss.resource_id = resource_id;
	ss.r.width = width;
	ss.r.height = height;

	viogpu_send_cmd(sc, &ss, sizeof(ss), &resp, sizeof(resp));

	if (resp.type != VIRTIO_GPU_RESP_OK_NODATA) {
		printf("%s: failed SET_SCANOUT: %d\n", sc->sc_dev.dv_xname,
		    resp.type);
		return 1;
	}

	return 0;
}

int
viogpu_attach_backing(struct viogpu_softc *sc, int resource_id,
    bus_dmamap_t dmamap)
{
	struct virtio_gpu_resource_attach_backing_entries {
		struct virtio_gpu_ctrl_hdr hdr;
		__le32 resource_id;
		__le32 nr_entries;
		struct virtio_gpu_mem_entry entries[1];
	} __packed backing = { 0 };
	struct virtio_gpu_ctrl_hdr resp = { 0 };

	backing.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
	backing.resource_id = resource_id;
	backing.nr_entries = nitems(backing.entries);
	backing.entries[0].addr = dmamap->dm_segs[0].ds_addr;
	backing.entries[0].length = dmamap->dm_segs[0].ds_len;

	if (dmamap->dm_nsegs > 1)
		printf("%s: TODO: send all %d segs\n", __func__,
		    dmamap->dm_nsegs);

#if VIRTIO_DEBUG
	printf("%s: backing addr 0x%llx length %d\n", __func__,
		backing.entries[0].addr, backing.entries[0].length);
#endif

	viogpu_send_cmd(sc, &backing, sizeof(backing), &resp, sizeof(resp));

	if (resp.type != VIRTIO_GPU_RESP_OK_NODATA) {
		printf("%s: failed ATTACH_BACKING: %d\n", sc->sc_dev.dv_xname,
		    resp.type);
		return 1;
	}

	return 0;
}

int
viogpu_transfer_to_host_2d(struct viogpu_softc *sc, int resource_id,
    uint32_t width, uint32_t height)
{
	struct virtio_gpu_transfer_to_host_2d tth = { 0 };
	struct virtio_gpu_ctrl_hdr resp = { 0 };

	tth.hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
	tth.resource_id = resource_id;
	tth.r.width = width;
	tth.r.height = height;

	viogpu_send_cmd(sc, &tth, sizeof(tth), &resp, sizeof(resp));

	if (resp.type != VIRTIO_GPU_RESP_OK_NODATA) {
		printf("%s: failed TRANSFER_TO_HOST: %d\n", sc->sc_dev.dv_xname,
		    resp.type);
		return 1;
	}

	return 0;
}

int
viogpu_flush_resource(struct viogpu_softc *sc, int resource_id, uint32_t width,
    uint32_t height)
{
	struct virtio_gpu_resource_flush flush = { 0 };
	struct virtio_gpu_ctrl_hdr resp = { 0 };

	flush.hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
	flush.resource_id = resource_id;
	flush.r.width = width;
	flush.r.height = height;

	viogpu_send_cmd(sc, &flush, sizeof(flush), &resp, sizeof(resp));

	if (resp.type != VIRTIO_GPU_RESP_OK_NODATA) {
		printf("%s: failed RESOURCE_FLUSH: %d\n", sc->sc_dev.dv_xname,
		    resp.type);
		return 1;
	}

	return 0;
}

int
viogpu_wsioctl(void *v, u_long cmd, caddr_t data, int flag, struct proc *p)
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
		*(u_int *)data = WSDISPLAY_TYPE_VIOGPU;
		break;
	case WSDISPLAYIO_GINFO:
		wdf = (struct wsdisplay_fbinfo *)data;
		wdf->width = ri->ri_width;
		wdf->height = ri->ri_height;
		wdf->depth = ri->ri_depth;
		wdf->stride = ri->ri_stride;
		wdf->cmsize = 0;
		wdf->offset = 0;
		break;
	case WSDISPLAYIO_LINEBYTES:
		*(u_int *)data = ri->ri_stride;
		break;
	case WSDISPLAYIO_SMODE:
		break;
	case WSDISPLAYIO_GETSUPPORTEDDEPTH:
		*(u_int *)data = WSDISPLAYIO_DEPTH_24_32;
		break;
	case WSDISPLAYIO_GVIDEO:
	case WSDISPLAYIO_SVIDEO:
		break;
	default:
		return -1;
	}

	return 0;
}

paddr_t
viogpu_wsmmap(void *v, off_t off, int prot)
{
	struct rasops_info *ri = v;
	struct viogpu_softc *sc = ri->ri_hw;
	size_t size = sc->sc_fb_dma_size;

	if (off < 0 || off >= size)
		return -1;

	return (((paddr_t)sc->sc_fb_dma_kva + off) | PMAP_NOCACHE);
}

int
viogpu_alloc_screen(void *v, const struct wsscreen_descr *type,
    void **cookiep, int *curxp, int *curyp, uint32_t *attrp)
{
	return rasops_alloc_screen(v, cookiep, curxp, curyp, attrp);
}

#if 0
int
viogpu_fb_probe(struct drm_fb_helper *helper,
    struct drm_fb_helper_surface_size *sizes)
{
	struct viogpu_softc *sc = helper->dev->dev_private;
	struct drm_device *ddev = helper->dev;
	struct viogpu_framebuffer *sfb = to_viogpu_framebuffer(helper->fb);
	struct drm_mode_fb_cmd2 mode_cmd = { 0 };
	struct drm_framebuffer *fb = helper->fb;
	struct wsemuldisplaydev_attach_args aa;
	struct rasops_info *ri = &sc->ro;
	struct viogpufb_attach_args sfa;
	unsigned int bytes_per_pixel;
	struct fb_info *info;
	size_t size;
	int error;

	if (viogpu_get_display_info(sc) != 0)
		return -1;

	bytes_per_pixel = DIV_ROUND_UP(sizes->surface_bpp, 8);

	mode_cmd.width = sc->sc_fb_width;
	mode_cmd.height = sc->sc_fb_height;
	mode_cmd.pitches[0] = sc->sc_fb_width * bytes_per_pixel;
	mode_cmd.pixel_format = drm_mode_legacy_fb_format(sizes->surface_bpp,
	    sizes->surface_depth);

	size = roundup(mode_cmd.pitches[0] * mode_cmd.height, PAGE_SIZE);

	sfb->obj = drm_gem_cma_create(ddev, size);
	if (sfb->obj == NULL) {
		DRM_ERROR("failed to allocate memory for framebuffer\n");
		return -ENOMEM;
	}

	drm_helper_mode_fill_fb_struct(ddev, fb, &mode_cmd);
	fb->format = drm_format_info(DRM_FORMAT_ARGB8888);
	fb->obj[0] = &sfb->obj->base;
	error = drm_framebuffer_init(ddev, fb, &viogpu_framebuffer_funcs);
	if (error != 0) {
		DRM_ERROR("failed to initialize framebuffer\n");
		return error;
	}

	info = drm_fb_helper_alloc_fbi(helper);
	if (IS_ERR(info)) {
		DRM_ERROR("Failed to allocate fb_info\n");
		return PTR_ERR(info);
	}
	info->par = helper;

	error = viogpu_create_2d(sc, 1, sc->sc_fb_width, sc->sc_fb_height);
	if (error)
		return error;

	error = viogpu_attach_backing(sc, 1, sfb->obj->dmamap);
	if (error)
		return error;

	error = viogpu_set_scanout(sc, 0, 1, sc->sc_fb_width, sc->sc_fb_height);
	if (error)
		return error;

	return 0;
}
#endif
