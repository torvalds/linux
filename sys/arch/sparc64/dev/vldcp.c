/*	$OpenBSD: vldcp.c,v 1.26 2024/05/14 08:26:13 jsg Exp $	*/
/*
 * Copyright (c) 2009, 2012 Mark Kettenis
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
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/systm.h>

#include <machine/autoconf.h>
#include <machine/hypervisor.h>
#include <machine/mdesc.h>

#include <uvm/uvm_extern.h>

#include <sparc64/dev/cbusvar.h>
#include <sparc64/dev/ldcvar.h>

#ifdef VLDCP_DEBUG
#define DPRINTF(x)	printf x
#else
#define DPRINTF(x)
#endif

#include <sys/ioccom.h>

struct hv_io {
	uint64_t	hi_cookie;
	void		*hi_addr;
	size_t		hi_len;
};

#define HVIOCREAD	_IOW('h', 0, struct hv_io)
#define HVIOCWRITE	_IOW('h', 1, struct hv_io)

#define VLDCP_TX_ENTRIES	128
#define VLDCP_RX_ENTRIES	128

struct vldcp_softc {
	struct device	sc_dv;
	bus_space_tag_t	sc_bustag;
	bus_dma_tag_t	sc_dmatag;

	uint64_t	sc_tx_ino;
	uint64_t	sc_rx_ino;
	void		*sc_tx_ih;
	void		*sc_rx_ih;

	struct ldc_conn	sc_lc;

	struct selinfo	sc_rsel;
	struct selinfo	sc_wsel;
};

int	vldcp_match(struct device *, void *, void *);
void	vldcp_attach(struct device *, struct device *, void *);
void	filt_vldcprdetach(struct knote *);
void	filt_vldcpwdetach(struct knote *);
int	filt_vldcpread(struct knote *, long);
int	vldcpkqfilter(dev_t, struct knote *);

const struct cfattach vldcp_ca = {
	sizeof(struct vldcp_softc), vldcp_match, vldcp_attach
};

struct cfdriver vldcp_cd = {
	NULL, "vldcp", DV_DULL
};

int	vldcp_tx_intr(void *);
int	vldcp_rx_intr(void *);

/*
 * We attach to certain well-known channels.  These are assigned fixed
 * device minor device numbers through their index in the table below.
 * So "hvctl" gets minor 0, "spds" gets minor 1, etc. etc.
 *
 * We also attach to the domain services channels.  These are named
 * "ldom-<guestname>" and get assigned a device minor starting at
 * VLDC_LDOM_OFFSET.
 */
#define VLDC_NUM_SERVICES	64
#define VLDC_LDOM_OFFSET	32
int vldc_num_ldoms;

struct vldc_svc {
	const char *vs_name;
	struct vldcp_softc *vs_sc;
};

struct vldc_svc vldc_svc[VLDC_NUM_SERVICES] = {
	{ "hvctl" },
	{ "spds" },
	{ NULL }
};

int
vldcp_match(struct device *parent, void *match, void *aux)
{
	struct cbus_attach_args *ca = aux;
	struct vldc_svc *svc;

	for (svc = vldc_svc; svc->vs_name != NULL; svc++)
		if (strcmp(ca->ca_name, svc->vs_name) == 0)
			return (1);

	if (strncmp(ca->ca_name, "ldom-", 5) == 0 &&
	    strcmp(ca->ca_name, "ldom-primary") != 0)
		return (1);

	return (0);
}

void
vldcp_attach(struct device *parent, struct device *self, void *aux)
{
	struct vldcp_softc *sc = (struct vldcp_softc *)self;
	struct cbus_attach_args *ca = aux;
	struct vldc_svc *svc;
	struct ldc_conn *lc;

	sc->sc_bustag = ca->ca_bustag;
	sc->sc_dmatag = ca->ca_dmatag;
	sc->sc_tx_ino = ca->ca_tx_ino;
	sc->sc_rx_ino = ca->ca_rx_ino;

	printf(": ivec 0x%llx, 0x%llx", sc->sc_tx_ino, sc->sc_rx_ino);

	/*
	 * Un-configure queues before registering interrupt handlers,
	 * such that we dont get any stale LDC packets or events.
	 */
	hv_ldc_tx_qconf(ca->ca_id, 0, 0);
	hv_ldc_rx_qconf(ca->ca_id, 0, 0);

	sc->sc_tx_ih = bus_intr_establish(ca->ca_bustag, sc->sc_tx_ino,
	    IPL_TTY, 0, vldcp_tx_intr, sc, sc->sc_dv.dv_xname);
	sc->sc_rx_ih = bus_intr_establish(ca->ca_bustag, sc->sc_rx_ino,
	    IPL_TTY, 0, vldcp_rx_intr, sc, sc->sc_dv.dv_xname);
	if (sc->sc_tx_ih == NULL || sc->sc_rx_ih == NULL) {
		printf(", can't establish interrupt\n");
		return;
	}

	lc = &sc->sc_lc;
	lc->lc_id = ca->ca_id;
	lc->lc_sc = sc;

	lc->lc_txq = ldc_queue_alloc(sc->sc_dmatag, VLDCP_TX_ENTRIES);
	if (lc->lc_txq == NULL) {
		printf(", can't allocate tx queue\n");
		return;
	}

	lc->lc_rxq = ldc_queue_alloc(sc->sc_dmatag, VLDCP_RX_ENTRIES);
	if (lc->lc_rxq == NULL) {
		printf(", can't allocate rx queue\n");
		goto free_txqueue;
	}
	lc->lc_rx_state = LDC_CHANNEL_INIT;

	for (svc = vldc_svc; svc->vs_name != NULL; svc++) {
		if (strcmp(ca->ca_name, svc->vs_name) == 0) {
			svc->vs_sc = sc;
			break;
		}
	}

	if (strncmp(ca->ca_name, "ldom-", 5) == 0 &&
	    strcmp(ca->ca_name, "ldom-primary") != 0) {
		int minor = VLDC_LDOM_OFFSET + vldc_num_ldoms++;
		if (minor < nitems(vldc_svc))
			vldc_svc[minor].vs_sc = sc;
	}

	printf(" channel \"%s\"\n", ca->ca_name);
	return;

#if 0
free_rxqueue:
	ldc_queue_free(sc->sc_dmatag, lc->lc_rxq);
#endif
free_txqueue:
	ldc_queue_free(sc->sc_dmatag, lc->lc_txq);
}

int
vldcp_tx_intr(void *arg)
{
	struct vldcp_softc *sc = arg;
	struct ldc_conn *lc = &sc->sc_lc;
	uint64_t tx_head, tx_tail, tx_state;
	int err;

	err = hv_ldc_tx_get_state(lc->lc_id, &tx_head, &tx_tail, &tx_state);
	if (err != H_EOK) {
		printf("%s: hv_ldc_tx_get_state %d\n", __func__, err);
		return (0);
	}

	if (tx_state != lc->lc_tx_state) {
		switch (tx_state) {
		case LDC_CHANNEL_DOWN:
			DPRINTF(("%s: Tx link down\n", __func__));
			break;
		case LDC_CHANNEL_UP:
			DPRINTF(("%s: Tx link up\n", __func__));
			break;
		case LDC_CHANNEL_RESET:
			DPRINTF(("%s: Tx link reset\n", __func__));
			break;
		}
		lc->lc_tx_state = tx_state;
	}

	cbus_intr_setenabled(sc->sc_bustag, sc->sc_tx_ino, INTR_DISABLED);
	selwakeup(&sc->sc_wsel);
	wakeup(lc->lc_txq);
	return (1);
}

int
vldcp_rx_intr(void *arg)
{
	struct vldcp_softc *sc = arg;
	struct ldc_conn *lc = &sc->sc_lc;
	uint64_t rx_head, rx_tail, rx_state;
	int err;

	err = hv_ldc_rx_get_state(lc->lc_id, &rx_head, &rx_tail, &rx_state);
	if (err != H_EOK) {
		printf("%s: hv_ldc_rx_get_state %d\n", __func__, err);
		return (0);
	}

	if (rx_state != lc->lc_rx_state) {
		switch (rx_state) {
		case LDC_CHANNEL_DOWN:
			DPRINTF(("%s: Rx link down\n", __func__));
			if (rx_head == rx_tail)
				break;
			/* Discard and ack pending I/O. */
			DPRINTF(("setting rx qhead to %llx\n", rx_tail));
			err = hv_ldc_rx_set_qhead(lc->lc_id, rx_tail);
			if (err == H_EOK)
				break;
			printf("%s: hv_ldc_rx_set_qhead %d\n", __func__, err);
			break;
		case LDC_CHANNEL_UP:
			DPRINTF(("%s: Rx link up\n", __func__));
			break;
		case LDC_CHANNEL_RESET:
			DPRINTF(("%s: Rx link reset\n", __func__));
			if (rx_head == rx_tail)
				break;
			/* Discard and ack pending I/O. */
			DPRINTF(("setting rx qhead to %llx\n", rx_tail));
			err = hv_ldc_rx_set_qhead(lc->lc_id, rx_tail);
			if (err == H_EOK)
				break;
			printf("%s: hv_ldc_rx_set_qhead %d\n", __func__, err);
			break;
		}
		lc->lc_rx_state = rx_state;
		cbus_intr_setenabled(sc->sc_bustag, sc->sc_rx_ino,
		    INTR_DISABLED);
		selwakeup(&sc->sc_rsel);
		wakeup(lc->lc_rxq);
		return (1);
	}

	if (rx_head == rx_tail)
		return (0);

	cbus_intr_setenabled(sc->sc_bustag, sc->sc_rx_ino, INTR_DISABLED);
	selwakeup(&sc->sc_rsel);
	wakeup(lc->lc_rxq);
	return (1);
}

cdev_decl(vldcp);
struct vldcp_softc *vldcp_lookup(dev_t);

struct vldcp_softc *
vldcp_lookup(dev_t dev)
{
	struct vldcp_softc *sc = NULL;

	if (minor(dev) < nitems(vldc_svc))
		sc = vldc_svc[minor(dev)].vs_sc;

	if (sc)
		device_ref(&sc->sc_dv);

	return (sc);
}

int
vldcpopen(dev_t dev, int flag, int mode, struct proc *p)
{
	struct vldcp_softc *sc;
	struct ldc_conn *lc;
	uint64_t rx_head, rx_tail, rx_state;
	int err;

	sc = vldcp_lookup(dev);
	if (sc == NULL)
		return (ENXIO);
	lc = &sc->sc_lc;

	err = hv_ldc_tx_qconf(lc->lc_id,
	    lc->lc_txq->lq_map->dm_segs[0].ds_addr, lc->lc_txq->lq_nentries);
	if (err != H_EOK)
		printf("%s: hv_ldc_tx_qconf %d\n", __func__, err);

	err = hv_ldc_rx_qconf(lc->lc_id,
	    lc->lc_rxq->lq_map->dm_segs[0].ds_addr, lc->lc_rxq->lq_nentries);
	if (err != H_EOK)
		printf("%s: hv_ldc_rx_qconf %d\n", __func__, err);

	/* Clear a pending channel reset.  */
	err = hv_ldc_rx_get_state(lc->lc_id, &rx_head, &rx_tail, &rx_state);
	if (err != H_EOK)
		printf("%s: hv_ldc_rx_get_state %d\n", __func__, err);

	device_unref(&sc->sc_dv);
	return (0);
}

int
vldcpclose(dev_t dev, int flag, int mode, struct proc *p)
{
	struct vldcp_softc *sc;

	sc = vldcp_lookup(dev);
	if (sc == NULL)
		return (ENXIO);

	cbus_intr_setenabled(sc->sc_bustag, sc->sc_tx_ino, INTR_DISABLED);
	cbus_intr_setenabled(sc->sc_bustag, sc->sc_rx_ino, INTR_DISABLED);

	hv_ldc_tx_qconf(sc->sc_lc.lc_id, 0, 0);
	hv_ldc_rx_qconf(sc->sc_lc.lc_id, 0, 0);

	device_unref(&sc->sc_dv);
	return (0);
}

int
vldcpread(dev_t dev, struct uio *uio, int ioflag)
{
	struct vldcp_softc *sc;
	struct ldc_conn *lc;
	uint64_t rx_head, rx_tail, rx_state;
	int err, ret;
	int s;

	sc = vldcp_lookup(dev);
	if (sc == NULL)
		return (ENXIO);
	lc = &sc->sc_lc;

	if (uio->uio_resid != 64) {
		device_unref(&sc->sc_dv);
		return (EINVAL);
	}

	s = spltty();
retry:
	err = hv_ldc_rx_get_state(lc->lc_id, &rx_head, &rx_tail, &rx_state);
	if (err != H_EOK) {
		splx(s);
		printf("%s: hv_ldc_rx_get_state %d\n", __func__, err);
		device_unref(&sc->sc_dv);
		return (EIO);
	}

	DPRINTF(("rx head %llx, rx tail %llx, state %lld\n", rx_head, rx_tail, rx_state));

	if (rx_state != LDC_CHANNEL_UP) {
		splx(s);
		device_unref(&sc->sc_dv);
		return (EIO);
	}

	if (rx_head == rx_tail) {
		cbus_intr_setenabled(sc->sc_bustag, sc->sc_rx_ino,
		    INTR_ENABLED);
		ret = tsleep_nsec(lc->lc_rxq, PWAIT | PCATCH, "hvrd", INFSLP);
		if (ret) {
			splx(s);
			device_unref(&sc->sc_dv);
			return (ret);
		}
		goto retry;
	}

	ret = uiomove(lc->lc_rxq->lq_va + rx_head, 64, uio);

	rx_head += 64;
	rx_head &= ((lc->lc_rxq->lq_nentries * 64) - 1);
	err = hv_ldc_rx_set_qhead(lc->lc_id, rx_head);
	if (err != H_EOK)
		printf("%s: hv_ldc_rx_set_qhead %d\n", __func__, err);

	splx(s);
	device_unref(&sc->sc_dv);
	return (ret);
}

int
vldcpwrite(dev_t dev, struct uio *uio, int ioflag)
{
	struct vldcp_softc *sc;
	struct ldc_conn *lc;
	uint64_t tx_head, tx_tail, tx_state;
	uint64_t next_tx_tail;
	int err, ret;
	int s;

	sc = vldcp_lookup(dev);
	if (sc == NULL)
		return (ENXIO);
	lc = &sc->sc_lc;

	if (uio->uio_resid != 64) {
		device_unref(&sc->sc_dv);
		return (EINVAL);
	}

	s = spltty();
retry:
	err = hv_ldc_tx_get_state(lc->lc_id, &tx_head, &tx_tail, &tx_state);
	if (err != H_EOK) {
		splx(s);
		printf("%s: hv_ldc_tx_get_state %d\n", __func__, err);
		device_unref(&sc->sc_dv);
		return (EIO);
	}

	if (tx_state != LDC_CHANNEL_UP) {
		splx(s);
		device_unref(&sc->sc_dv);
		return (EIO);
	}

	DPRINTF(("tx head %llx, tx tail %llx\n", tx_head, tx_tail));

	next_tx_tail = tx_tail + 64;
	next_tx_tail &= ((lc->lc_txq->lq_nentries * 64) - 1);

	if (tx_head == next_tx_tail) {
		cbus_intr_setenabled(sc->sc_bustag, sc->sc_tx_ino,
		    INTR_ENABLED);
		ret = tsleep_nsec(lc->lc_txq, PWAIT | PCATCH, "hvwr", INFSLP);
		if (ret) {
			splx(s);
			device_unref(&sc->sc_dv);
			return (ret);
		}
		goto retry;
	}
	splx(s);

	ret = uiomove(lc->lc_txq->lq_va + tx_tail, 64, uio);

	err = hv_ldc_tx_set_qtail(lc->lc_id, next_tx_tail);
	if (err != H_EOK) {
		printf("%s: hv_ldc_tx_set_qtail: %d\n", __func__, err);
		device_unref(&sc->sc_dv);
		return (EIO);
	}

	device_unref(&sc->sc_dv);
	return (ret);
}

int
vldcpioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct vldcp_softc *sc;
	struct ldc_conn *lc;
	struct hv_io *hi = (struct hv_io *)data;
	paddr_t pa, offset;
	psize_t nbytes;
	caddr_t buf;
	size_t size;
	int err;

	sc = vldcp_lookup(dev);
	if (sc == NULL)
		return (ENXIO);
	lc = &sc->sc_lc;

	switch (cmd) {
	case HVIOCREAD:
	case HVIOCWRITE:
		break;
	default:
		device_unref(&sc->sc_dv);
		return (ENOTTY);
	}

	buf = malloc(PAGE_SIZE, M_DEVBUF, M_WAITOK);

	switch(cmd) {
	case HVIOCREAD:
		size = hi->hi_len;
		offset = 0;
		while (size > 0) {
			pmap_extract(pmap_kernel(), (vaddr_t)buf, &pa);
			nbytes = min(PAGE_SIZE, size);
			err = hv_ldc_copy(lc->lc_id, LDC_COPY_IN,
			    hi->hi_cookie + offset, pa, nbytes, &nbytes);
			if (err != H_EOK) {
				printf("hv_ldc_copy %d\n", err);
				free(buf, M_DEVBUF, 0);
				device_unref(&sc->sc_dv);
				return (EINVAL);
			}
			err = copyout(buf, (caddr_t)hi->hi_addr + offset, nbytes);
			if (err) {
				free(buf, M_DEVBUF, 0);
				device_unref(&sc->sc_dv);
				return (err);
			}
			size -= nbytes;
			offset += nbytes;
		}
		break;
	case HVIOCWRITE:
		size = hi->hi_len;
		offset = 0;
		while (size > 0) {
			pmap_extract(pmap_kernel(), (vaddr_t)buf, &pa);
			nbytes = min(PAGE_SIZE, size);
			err = copyin((caddr_t)hi->hi_addr + offset, buf, nbytes);
			if (err) {
				free(buf, M_DEVBUF, 0);
				device_unref(&sc->sc_dv);
				return (err);
			}
			err = hv_ldc_copy(lc->lc_id, LDC_COPY_OUT,
			    hi->hi_cookie + offset, pa, nbytes, &nbytes);
			if (err != H_EOK) {
				printf("hv_ldc_copy %d\n", err);
				free(buf, M_DEVBUF, 0);
				device_unref(&sc->sc_dv);
				return (EINVAL);
			}
			size -= nbytes;
			offset += nbytes;
		}
		break;

	}

	free(buf, M_DEVBUF, 0);

	device_unref(&sc->sc_dv);
	return (0);
}

void
filt_vldcprdetach(struct knote *kn)
{
	struct vldcp_softc *sc = (void *)kn->kn_hook;
	int s;

	s = spltty();
	klist_remove_locked(&sc->sc_rsel.si_note, kn);
	splx(s);
}

void
filt_vldcpwdetach(struct knote *kn)
{
	struct vldcp_softc *sc = (void *)kn->kn_hook;
	int s;

	s = spltty();
	klist_remove_locked(&sc->sc_wsel.si_note, kn);
	splx(s);
}

int
filt_vldcpread(struct knote *kn, long hint)
{
	struct vldcp_softc *sc = (void *)kn->kn_hook;
	struct ldc_conn *lc = &sc->sc_lc;
	uint64_t head, tail, avail, state;
	int s, err;

	s = spltty();
	err = hv_ldc_rx_get_state(lc->lc_id, &head, &tail, &state);
	if (err == 0 && state == LDC_CHANNEL_UP && head != tail) {
		avail = (tail - head) / sizeof(struct ldc_pkt) +
		    lc->lc_rxq->lq_nentries;
		avail %= lc->lc_rxq->lq_nentries;
		kn->kn_data = avail;
	} else {
		cbus_intr_setenabled(sc->sc_bustag, sc->sc_rx_ino,
		    INTR_ENABLED);
		kn->kn_data = 0;
	}
	splx(s);

	return (kn->kn_data > 0);
}

int
filt_vldcwrite(struct knote *kn, long hint)
{
	struct vldcp_softc *sc = (void *)kn->kn_hook;
	struct ldc_conn *lc = &sc->sc_lc;
	uint64_t head, tail, avail, state;
	int s, err;

	s = spltty();
	err = hv_ldc_tx_get_state(lc->lc_id, &head, &tail, &state);
	if (err == 0 && state == LDC_CHANNEL_UP && head != tail) {
		avail = (head - tail) / sizeof(struct ldc_pkt) +
		    lc->lc_txq->lq_nentries - 1;
		avail %= lc->lc_txq->lq_nentries;
		kn->kn_data = avail;
	} else {
		cbus_intr_setenabled(sc->sc_bustag, sc->sc_tx_ino,
		    INTR_ENABLED);
		kn->kn_data = 0;
	}
	splx(s);

	return (kn->kn_data > 0);
}

const struct filterops vldcpread_filtops = {
	.f_flags	= FILTEROP_ISFD,
	.f_attach	= NULL,
	.f_detach	= filt_vldcprdetach,
	.f_event	= filt_vldcpread,
};

const struct filterops vldcpwrite_filtops = {
	.f_flags	= FILTEROP_ISFD,
	.f_attach	= NULL,
	.f_detach	= filt_vldcpwdetach,
	.f_event	= filt_vldcwrite,
};

int
vldcpkqfilter(dev_t dev, struct knote *kn)
{
	struct vldcp_softc *sc;
	struct klist *klist;
	int s;

	sc = vldcp_lookup(dev);
	if (sc == NULL)
		return (ENXIO);

	switch (kn->kn_filter) {
	case EVFILT_READ:
		klist = &sc->sc_rsel.si_note;
		kn->kn_fop = &vldcpread_filtops;
		break;
	case EVFILT_WRITE:
		klist = &sc->sc_wsel.si_note;
		kn->kn_fop = &vldcpwrite_filtops;
		break;

	default:
		device_unref(&sc->sc_dv);
		return (EINVAL);
	}

	kn->kn_hook = sc;

	s = spltty();
	klist_insert_locked(klist, kn);
	splx(s);

	device_unref(&sc->sc_dv);
	return (0);
}
