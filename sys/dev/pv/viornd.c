/*	$OpenBSD: viornd.c,v 1.13 2025/09/16 12:18:10 hshoexer Exp $	*/

/*
 * Copyright (c) 2014 Stefan Fritsch <sf@sfritsch.de>
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
#include <sys/timeout.h>
#include <machine/bus.h>
#include <sys/device.h>
#include <sys/pool.h>
#include <dev/pv/virtioreg.h>
#include <dev/pv/virtiovar.h>

/*
 * The host may not have an unlimited supply of entropy. Therefore, we must
 * not blindly get as much entropy as we can. Instead, we just request
 * VIORND_BUFSIZE bytes at boot and every 15 * (1 << interval_shift) seconds.
 * XXX There should be an API to check if we actually need more entropy.
 *
 * The lowest byte in the flags is used for transport specific settings.
 * Therefore we use the second byte.
 */
#define	VIORND_INTERVAL_SHIFT(f)	((f >> 8) & 0xf)
#define	VIORND_INTERVAL_SHIFT_DEFAULT	5
#define	VIORND_ONESHOT			0x1000
#define	VIORND_BUFSIZE			16

#define VIORND_DEBUG 0

struct viornd_softc {
	struct device		 sc_dev;
	struct virtio_softc	*sc_virtio;

	struct virtqueue         sc_vq;
	int			*sc_buf;
	bus_dmamap_t		 sc_dmamap;

	unsigned int		 sc_interval;
	struct timeout		 sc_tick;
};

int	viornd_match(struct device *, void *, void *);
void	viornd_attach(struct device *, struct device *, void *);
int	viornd_vq_done(struct virtqueue *);
void	viornd_tick(void *);

const struct cfattach viornd_ca = {
	sizeof(struct viornd_softc),
	viornd_match,
	viornd_attach,
	NULL
};

struct cfdriver viornd_cd = {
	NULL, "viornd", DV_DULL, CD_COCOVM
};

int
viornd_match(struct device *parent, void *match, void *aux)
{
	struct virtio_attach_args *va = aux;
	if (va->va_devid == PCI_PRODUCT_VIRTIO_ENTROPY)
		return 1;
	return 0;
}

void
viornd_attach(struct device *parent, struct device *self, void *aux)
{
	struct viornd_softc *sc = (struct viornd_softc *)self;
	struct virtio_softc *vsc = (struct virtio_softc *)parent;
	struct virtio_attach_args *va = aux;
	unsigned int shift;

	vsc->sc_vqs = &sc->sc_vq;
	vsc->sc_nvqs = 1;
	if (vsc->sc_child != NULL)
		panic("already attached to something else");
	vsc->sc_child = self;
	vsc->sc_ipl = IPL_NET;
	sc->sc_virtio = vsc;

	if (virtio_negotiate_features(vsc, NULL) != 0)
		goto err;

	if (sc->sc_dev.dv_cfdata->cf_flags & VIORND_ONESHOT) {
		sc->sc_interval = 0;
	} else {
		shift = VIORND_INTERVAL_SHIFT(sc->sc_dev.dv_cfdata->cf_flags);
		if (shift == 0)
			shift = VIORND_INTERVAL_SHIFT_DEFAULT;
		sc->sc_interval = 15 * (1 << shift);
	}
#if VIORND_DEBUG
	printf(": request interval: %us\n", sc->sc_interval);
#endif

	sc->sc_buf = dma_alloc(VIORND_BUFSIZE, PR_NOWAIT|PR_ZERO);
	if (sc->sc_buf == NULL) {
		printf(": Can't alloc dma buffer\n");
		goto err;
	}
	if (bus_dmamap_create(sc->sc_virtio->sc_dmat, VIORND_BUFSIZE, 1,
	    VIORND_BUFSIZE, 0, BUS_DMA_NOWAIT|BUS_DMA_ALLOCNOW,
	    &sc->sc_dmamap)) {
		printf(": Can't alloc dmamap\n");
		goto err;
	}
	if (bus_dmamap_load(sc->sc_virtio->sc_dmat, sc->sc_dmamap,
	    sc->sc_buf, VIORND_BUFSIZE, NULL, BUS_DMA_NOWAIT|BUS_DMA_READ)) {
		printf(": Can't load dmamap\n");
		goto err2;
	}

	if (virtio_alloc_vq(vsc, &sc->sc_vq, 0, 1, "Entropy request") != 0) {
		printf(": Can't alloc virtqueue\n");
		goto err2;
	}

	sc->sc_vq.vq_done = viornd_vq_done;
	virtio_start_vq_intr(vsc, &sc->sc_vq);
	timeout_set(&sc->sc_tick, viornd_tick, sc);
	timeout_add(&sc->sc_tick, 1);

	printf("\n");
	if (virtio_attach_finish(vsc, va) != 0)
		goto err2;
	return;
err2:
	bus_dmamap_destroy(vsc->sc_dmat, sc->sc_dmamap);
err:
	vsc->sc_child = VIRTIO_CHILD_ERROR;
	if (sc->sc_buf != NULL) {
		dma_free(sc->sc_buf, VIORND_BUFSIZE);
		sc->sc_buf = NULL;
	}
	return;
}

int
viornd_vq_done(struct virtqueue *vq)
{
	struct virtio_softc *vsc = vq->vq_owner;
	struct viornd_softc *sc = (struct viornd_softc *)vsc->sc_child;
	int slot, len, i;

	if (virtio_dequeue(vsc, vq, &slot, &len) != 0)
		return 0;
	bus_dmamap_sync(vsc->sc_dmat, sc->sc_dmamap, 0, VIORND_BUFSIZE,
	    BUS_DMASYNC_POSTREAD);
	if (len > VIORND_BUFSIZE) {
		printf("%s: inconsistent descriptor length %d > %d\n",
		    sc->sc_dev.dv_xname, len, VIORND_BUFSIZE);
		goto out;
	}

#if VIORND_DEBUG
	printf("%s: got %d bytes of entropy\n", __func__, len);
#endif
	for (i = 0; (i + 1) * sizeof(int) <= len; i++)
		enqueue_randomness(sc->sc_buf[i]);

	if (sc->sc_interval)
		timeout_add_sec(&sc->sc_tick, sc->sc_interval);

out:
	virtio_dequeue_commit(vq, slot);
	return 1;
}

void
viornd_tick(void *arg)
{
	struct viornd_softc *sc = arg;
	struct virtio_softc *vsc = sc->sc_virtio;
	struct virtqueue *vq = &sc->sc_vq;
	int slot;

	bus_dmamap_sync(vsc->sc_dmat, sc->sc_dmamap, 0, VIORND_BUFSIZE,
	    BUS_DMASYNC_PREREAD);
	if (virtio_enqueue_prep(vq, &slot) != 0 ||
	    virtio_enqueue_reserve(vq, slot, 1) != 0) {
		panic("%s: virtqueue enqueue failed", sc->sc_dev.dv_xname);
	}
	virtio_enqueue(vq, slot, sc->sc_dmamap, 0);
	virtio_enqueue_commit(vsc, vq, slot, 1);
}
