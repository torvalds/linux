/*	$OpenBSD: viocon.c,v 1.18 2025/09/16 12:18:10 hshoexer Exp $	*/

/*
 * Copyright (c) 2013-2015 Stefan Fritsch <sf@sfritsch.de>
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
#include <sys/malloc.h>
#include <machine/bus.h>
#include <sys/device.h>
#include <sys/conf.h>
#include <sys/tty.h>
#include <dev/pv/virtioreg.h>
#include <dev/pv/virtiovar.h>


/* features */
#define	VIRTIO_CONSOLE_F_SIZE		(1ULL<<0)
#define	VIRTIO_CONSOLE_F_MULTIPORT	(1ULL<<1)
#define	VIRTIO_CONSOLE_F_EMERG_WRITE 	(1ULL<<2)

/* config space */
#define VIRTIO_CONSOLE_COLS		0	/* 16 bits */
#define VIRTIO_CONSOLE_ROWS		2	/* 16 bits */
#define VIRTIO_CONSOLE_MAX_NR_PORTS	4	/* 32 bits */
#define VIRTIO_CONSOLE_EMERG_WR		8	/* 32 bits */

#define VIOCON_DEBUG	0

#if VIOCON_DEBUG
#define DPRINTF(x...) printf(x)
#else
#define DPRINTF(x...)
#endif

static const struct virtio_feature_name viocon_feature_names[] = {
#if VIRTIO_DEBUG
	{ VIRTIO_CONSOLE_F_SIZE,	"Size" },
	{ VIRTIO_CONSOLE_F_MULTIPORT,	"MultiPort" },
	{ VIRTIO_CONSOLE_F_EMERG_WRITE,	"EmergWrite" },
#endif
	{ 0, 				NULL },
};

struct virtio_console_control {
	uint32_t id;	/* Port number */

#define	VIRTIO_CONSOLE_DEVICE_READY	0
#define	VIRTIO_CONSOLE_PORT_ADD		1
#define	VIRTIO_CONSOLE_PORT_REMOVE	2
#define	VIRTIO_CONSOLE_PORT_READY	3
#define	VIRTIO_CONSOLE_CONSOLE_PORT	4
#define	VIRTIO_CONSOLE_RESIZE		5
#define	VIRTIO_CONSOLE_PORT_OPEN	6
#define	VIRTIO_CONSOLE_PORT_NAME	7
	uint16_t event;

	uint16_t value;
};

struct virtio_console_control_resize {
	/* yes, the order is different than in config space */
	uint16_t rows;
	uint16_t cols;
};

#define	BUFSIZE		128
CTASSERT(BUFSIZE < TTHIWATMINSPACE);

#define VIOCONUNIT(x)	(minor(x) >> 4)
#define VIOCONPORT(x)	(minor(x) & 0x0f)

struct viocon_port {
	struct viocon_softc	*vp_sc;
	struct virtqueue	*vp_rx;
	struct virtqueue	*vp_tx;
	void			*vp_si;
	struct tty		*vp_tty;
	const char 		*vp_name;
	bus_dma_segment_t	 vp_dmaseg;
	bus_dmamap_t		 vp_dmamap;
#ifdef NOTYET
	unsigned int		 vp_host_open:1;	/* XXX needs F_MULTIPORT */
	unsigned int		 vp_guest_open:1;	/* XXX needs F_MULTIPORT */
	unsigned int		 vp_is_console:1;	/* XXX needs F_MULTIPORT */
#endif
	unsigned int		 vp_iflow:1;		/* rx flow control */
	uint16_t		 vp_rows;
	uint16_t		 vp_cols;
	u_char			*vp_rx_buf;
	u_char			*vp_tx_buf;
};

struct viocon_softc {
	struct device		 sc_dev;
	struct virtio_softc	*sc_virtio;

	struct virtqueue        *sc_c_vq_rx;
	struct virtqueue        *sc_c_vq_tx;

	unsigned int		 sc_max_ports;
	struct viocon_port	**sc_ports;

	bus_dmamap_t		 sc_dmamap;
};

int	viocon_match(struct device *, void *, void *);
void	viocon_attach(struct device *, struct device *, void *);
int	viocon_tx_intr(struct virtqueue *);
int	viocon_tx_drain(struct viocon_port *, struct virtqueue *vq);
int	viocon_rx_intr(struct virtqueue *);
void	viocon_rx_soft(void *);
void	viocon_rx_fill(struct viocon_port *);
int	viocon_port_create(struct viocon_softc *, int);
void	vioconstart(struct tty *);
int	vioconhwiflow(struct tty *, int);
int	vioconparam(struct tty *, struct termios *);
int	vioconopen(dev_t, int, int, struct proc *);
int	vioconclose(dev_t, int, int, struct proc *);
int	vioconread(dev_t, struct uio *, int);
int	vioconwrite(dev_t, struct uio *, int);
int	vioconstop(struct tty *, int);
int	vioconioctl(dev_t, u_long, caddr_t, int, struct proc *);
struct tty	*viocontty(dev_t dev);

const struct cfattach viocon_ca = {
	sizeof(struct viocon_softc),
	viocon_match,
	viocon_attach,
	NULL
};

struct cfdriver viocon_cd = {
	NULL, "viocon", DV_TTY, CD_COCOVM
};

static inline struct viocon_softc *
dev2sc(dev_t dev)
{
	return viocon_cd.cd_devs[VIOCONUNIT(dev)];
}

static inline struct viocon_port *
dev2port(dev_t dev)
{
	return dev2sc(dev)->sc_ports[VIOCONPORT(dev)];
}

int
viocon_match(struct device *parent, void *match, void *aux)
{
	struct virtio_attach_args *va = aux;
	if (va->va_devid == PCI_PRODUCT_VIRTIO_CONSOLE)
		return 1;
	return 0;
}

void
viocon_attach(struct device *parent, struct device *self, void *aux)
{
	struct viocon_softc *sc = (struct viocon_softc *)self;
	struct virtio_softc *vsc = (struct virtio_softc *)parent;
	int maxports = 1;

	if (vsc->sc_child)
		panic("already attached to something else");
	vsc->sc_child = self;
	vsc->sc_ipl = IPL_TTY;
	sc->sc_virtio = vsc;
	sc->sc_max_ports = maxports;

	vsc->sc_vqs = malloc(2 * (maxports + 1) * sizeof(struct virtqueue), M_DEVBUF,
	    M_WAITOK|M_CANFAIL|M_ZERO);
	sc->sc_ports = malloc(maxports * sizeof(sc->sc_ports[0]), M_DEVBUF,
	    M_WAITOK|M_CANFAIL|M_ZERO);
	if (vsc->sc_vqs == NULL || sc->sc_ports == NULL) {
		printf("\n%s: Cannot allocate memory\n", __func__);
		goto err;
	}

	vsc->sc_driver_features = VIRTIO_CONSOLE_F_SIZE;
	if (virtio_negotiate_features(vsc, viocon_feature_names) != 0)
		goto err;

	printf("\n");
	DPRINTF("%s: softc: %p\n", __func__, sc);
	if (viocon_port_create(sc, 0) != 0) {
		printf("\n%s: viocon_port_create failed\n", __func__);
		goto err;
	}
	if (virtio_attach_finish(vsc, va) != 0)
		goto err;
	viocon_rx_fill(sc->sc_ports[0]);
	return;

err:
	vsc->sc_child = VIRTIO_CHILD_ERROR;
	free(vsc->sc_vqs, M_DEVBUF, 2 * (maxports + 1) * sizeof(struct virtqueue));
	free(sc->sc_ports, M_DEVBUF, maxports * sizeof(sc->sc_ports[0]));
}

int
viocon_port_create(struct viocon_softc *sc, int portidx)
{
	struct virtio_softc *vsc = sc->sc_virtio;
	int rxidx, txidx, allocsize, nsegs;
	char name[6];
	struct viocon_port *vp;
	caddr_t kva;
	struct tty *tp;

	vp = malloc(sizeof(*vp), M_DEVBUF, M_WAITOK|M_CANFAIL|M_ZERO);
	if (vp == NULL)
		return ENOMEM;
	sc->sc_ports[portidx] = vp;
	vp->vp_sc = sc;
	DPRINTF("%s: vp: %p\n", __func__, vp);

	if (portidx == 0)
		rxidx = 0;
	else
		rxidx = 2 * (portidx + 1);
	txidx = rxidx + 1;

	snprintf(name, sizeof(name), "p%drx", portidx);
	if (virtio_alloc_vq(vsc, &vsc->sc_vqs[rxidx], rxidx, 1, name) != 0) {
		printf("\nCan't alloc %s virtqueue\n", name);
		goto err;
	}
	vp->vp_rx = &vsc->sc_vqs[rxidx];
	vp->vp_rx->vq_done = viocon_rx_intr;
	vp->vp_si = softintr_establish(IPL_TTY, viocon_rx_soft, vp);
	DPRINTF("%s: rx: %p\n", __func__, vp->vp_rx);

	snprintf(name, sizeof(name), "p%dtx", portidx);
	if (virtio_alloc_vq(vsc, &vsc->sc_vqs[txidx], txidx, 1, name) != 0) {
		printf("\nCan't alloc %s virtqueue\n", name);
		goto err;
	}
	vp->vp_tx = &vsc->sc_vqs[txidx];
	vp->vp_tx->vq_done = viocon_tx_intr;
	DPRINTF("%s: tx: %p\n", __func__, vp->vp_tx);

	vsc->sc_nvqs += 2;

	allocsize = (vp->vp_rx->vq_num + vp->vp_tx->vq_num) * BUFSIZE;

	if (bus_dmamap_create(vsc->sc_dmat, allocsize, 1, allocsize, 0,
	    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW, &vp->vp_dmamap) != 0)
		goto err;
	if (bus_dmamem_alloc(vsc->sc_dmat, allocsize, 8, 0, &vp->vp_dmaseg,
	    1, &nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO) != 0)
		goto err;
	if (bus_dmamem_map(vsc->sc_dmat, &vp->vp_dmaseg, nsegs,
	    allocsize, &kva, BUS_DMA_NOWAIT) != 0)
		goto err;
	if (bus_dmamap_load(vsc->sc_dmat, vp->vp_dmamap, kva,
	    allocsize, NULL, BUS_DMA_NOWAIT) != 0)
		goto err;
	vp->vp_rx_buf = (unsigned char *)kva;
	/*
	 * XXX use only a small circular tx buffer instead of many BUFSIZE buffers?
	 */
	vp->vp_tx_buf = vp->vp_rx_buf + vp->vp_rx->vq_num * BUFSIZE;

	if (virtio_has_feature(vsc, VIRTIO_CONSOLE_F_SIZE)) {
		vp->vp_cols = virtio_read_device_config_2(vsc,
		    VIRTIO_CONSOLE_COLS);
		vp->vp_rows = virtio_read_device_config_2(vsc,
		    VIRTIO_CONSOLE_ROWS);
	}

	tp = ttymalloc(1000000);
	tp->t_oproc = vioconstart;
	tp->t_param = vioconparam;
	tp->t_hwiflow = vioconhwiflow;
	tp->t_dev = (sc->sc_dev.dv_unit << 4) | portidx;
	vp->vp_tty = tp;
	DPRINTF("%s: tty: %p\n", __func__, tp);

	virtio_start_vq_intr(vsc, vp->vp_rx);
	virtio_start_vq_intr(vsc, vp->vp_tx);

	return 0;
err:
	panic("%s failed", __func__);
	return -1;
}

int
viocon_tx_drain(struct viocon_port *vp, struct virtqueue *vq)
{
	struct virtio_softc *vsc = vq->vq_owner;
	int ndone = 0, len, slot;

	splassert(IPL_TTY);
	while (virtio_dequeue(vsc, vq, &slot, &len) == 0) {
		bus_dmamap_sync(vsc->sc_dmat, vp->vp_dmamap,
		    vp->vp_tx_buf - vp->vp_rx_buf + slot * BUFSIZE, BUFSIZE,
		    BUS_DMASYNC_POSTREAD);
		virtio_dequeue_commit(vq, slot);
		ndone++;
	}
	return ndone;
}

int
viocon_tx_intr(struct virtqueue *vq)
{
	struct virtio_softc *vsc = vq->vq_owner;
	struct viocon_softc *sc = (struct viocon_softc *)vsc->sc_child;
	int ndone = 0;
	int portidx = (vq->vq_index - 1) / 2;
	struct viocon_port *vp = sc->sc_ports[portidx];
	struct tty *tp = vp->vp_tty;

	splassert(IPL_TTY);
	ndone = viocon_tx_drain(vp, vq);
	if (ndone && ISSET(tp->t_state, TS_BUSY)) {
		CLR(tp->t_state, TS_BUSY);
		linesw[tp->t_line].l_start(tp);
	}

	return 1;
}

void
viocon_rx_fill(struct viocon_port *vp)
{
	struct virtqueue *vq = vp->vp_rx;
	struct virtio_softc *vsc = vp->vp_sc->sc_virtio;
	int r, slot, ndone = 0;

	while ((r = virtio_enqueue_prep(vq, &slot)) == 0) {
		if (virtio_enqueue_reserve(vq, slot, 1) != 0)
			break;
		bus_dmamap_sync(vsc->sc_dmat, vp->vp_dmamap, slot * BUFSIZE,
		    BUFSIZE, BUS_DMASYNC_PREREAD);
		virtio_enqueue_p(vq, slot, vp->vp_dmamap, slot * BUFSIZE,
		    BUFSIZE, 0);
		virtio_enqueue_commit(vsc, vq, slot, 0);
		ndone++;
	}
	KASSERT(r == 0 || r == EAGAIN);
	if (ndone > 0)
		virtio_notify(vsc, vq);
}

int
viocon_rx_intr(struct virtqueue *vq)
{
	struct virtio_softc *vsc = vq->vq_owner;
	struct viocon_softc *sc = (struct viocon_softc *)vsc->sc_child;
	int portidx = (vq->vq_index - 1) / 2;
	struct viocon_port *vp = sc->sc_ports[portidx];

	softintr_schedule(vp->vp_si);
	return 1;
}

void
viocon_rx_soft(void *arg)
{
	struct viocon_port *vp = arg;
	struct virtqueue *vq = vp->vp_rx;
	struct virtio_softc *vsc = vq->vq_owner;
	struct tty *tp = vp->vp_tty;
	int slot, len, i;
	u_char *p;

	while (!vp->vp_iflow && virtio_dequeue(vsc, vq, &slot, &len) == 0) {
		bus_dmamap_sync(vsc->sc_dmat, vp->vp_dmamap,
		    slot * BUFSIZE, BUFSIZE, BUS_DMASYNC_POSTREAD);
		p = vp->vp_rx_buf + slot * BUFSIZE;
		for (i = 0; i < len; i++)
			(*linesw[tp->t_line].l_rint)(*p++, tp);
		virtio_dequeue_commit(vq, slot);
	}

	viocon_rx_fill(vp);

	return;
}

void
vioconstart(struct tty *tp)
{
	struct viocon_softc *sc = dev2sc(tp->t_dev);
	struct virtio_softc *vsc;
	struct viocon_port *vp = dev2port(tp->t_dev);
	struct virtqueue *vq;
	u_char *buf;
	int s, cnt, slot, ret, ndone;

	vsc = sc->sc_virtio;
	vq = vp->vp_tx;

	s = spltty();

	ndone = viocon_tx_drain(vp, vq);
	if (ISSET(tp->t_state, TS_BUSY)) {
		if (ndone > 0)
			CLR(tp->t_state, TS_BUSY);
		else
			goto out;
	}
	if (ISSET(tp->t_state, TS_TIMEOUT | TS_TTSTOP))
		goto out;

	if (tp->t_outq.c_cc == 0)
		goto out;
	ndone = 0;

	while (tp->t_outq.c_cc > 0) {
		ret = virtio_enqueue_prep(vq, &slot);
		if (ret == EAGAIN)
			break;
		KASSERT(ret == 0);
		ret = virtio_enqueue_reserve(vq, slot, 1);
		KASSERT(ret == 0);
		buf = vp->vp_tx_buf + slot * BUFSIZE;
		cnt = q_to_b(&tp->t_outq, buf, BUFSIZE);
		bus_dmamap_sync(vsc->sc_dmat, vp->vp_dmamap,
		    vp->vp_tx_buf - vp->vp_rx_buf + slot * BUFSIZE, cnt,
		    BUS_DMASYNC_PREWRITE);
		virtio_enqueue_p(vq, slot, vp->vp_dmamap,
		    vp->vp_tx_buf - vp->vp_rx_buf + slot * BUFSIZE, cnt, 1);
		virtio_enqueue_commit(vsc, vq, slot, 0);
		ndone++;
	}
	if (ret == EAGAIN)
		SET(tp->t_state, TS_BUSY);
	if (ndone > 0)
		virtio_notify(vsc, vq);
	ttwakeupwr(tp);
out:
	splx(s);
}

int
vioconhwiflow(struct tty *tp, int stop)
{
	struct viocon_port *vp = dev2port(tp->t_dev);
	int s;

	s = spltty();
	vp->vp_iflow = stop;
	if (stop) {
		virtio_stop_vq_intr(vp->vp_sc->sc_virtio, vp->vp_rx);
	} else {
		virtio_start_vq_intr(vp->vp_sc->sc_virtio, vp->vp_rx);
		virtio_check_vq(vp->vp_sc->sc_virtio, vp->vp_rx);
	}
	splx(s);
	return 1;
}

int
vioconparam(struct tty *tp, struct termios *t)
{
	tp->t_ispeed = t->c_ispeed;
	tp->t_ospeed = t->c_ospeed;
	tp->t_cflag = t->c_cflag;

	vioconstart(tp);
	return 0;
}

int
vioconopen(dev_t dev, int flag, int mode, struct proc *p)
{
	int unit = VIOCONUNIT(dev);
	int port = VIOCONPORT(dev);
	struct viocon_softc *sc;
	struct viocon_port *vp;
	struct tty *tp;
	int s, error;

	if (unit >= viocon_cd.cd_ndevs)
		return (ENXIO);
	sc = viocon_cd.cd_devs[unit];
	if (sc == NULL)
		return (ENXIO);
	if (ISSET(sc->sc_dev.dv_flags, DVF_ACTIVE) == 0)
		return (ENXIO);

	s = spltty();
	if (port >= sc->sc_max_ports) {
		splx(s);
		return (ENXIO);
	}
	vp = sc->sc_ports[port];
	tp = vp->vp_tty;
#ifdef NOTYET
	vp->vp_guest_open = 1;
#endif
	splx(s);

	if (!ISSET(tp->t_state, TS_ISOPEN)) {
		SET(tp->t_state, TS_WOPEN);
		ttychars(tp);
		tp->t_ispeed = 1000000;
		tp->t_ospeed = 1000000;
		tp->t_cflag = TTYDEF_CFLAG|CLOCAL|CRTSCTS;
		tp->t_iflag = TTYDEF_IFLAG;
		tp->t_oflag = TTYDEF_OFLAG;
		tp->t_lflag = TTYDEF_LFLAG;
		if (vp->vp_cols != 0) {
			tp->t_winsize.ws_col = vp->vp_cols;
			tp->t_winsize.ws_row = vp->vp_rows;
		}

		s = spltty();
		vioconparam(tp, &tp->t_termios);
		ttsetwater(tp);
		splx(s);
	}
	else if (ISSET(tp->t_state, TS_XCLUDE) && suser(p) != 0) {
		return (EBUSY);
	}

	error = linesw[tp->t_line].l_open(dev, tp, p);
	return error;
}

int
vioconclose(dev_t dev, int flag, int mode, struct proc *p)
{
	struct viocon_port *vp = dev2port(dev);
	struct tty *tp = vp->vp_tty;
	int s;

	if (!ISSET(tp->t_state, TS_ISOPEN))
		return 0;

	linesw[tp->t_line].l_close(tp, flag, p);
	s = spltty();
#ifdef NOTYET
	vp->vp_guest_open = 0;
#endif
	CLR(tp->t_state, TS_BUSY | TS_FLUSH);
	ttyclose(tp);
	splx(s);

	return 0;
}

int
vioconread(dev_t dev, struct uio *uio, int flag)
{
	struct viocon_port *vp = dev2port(dev);
	struct tty *tp = vp->vp_tty;

	return linesw[tp->t_line].l_read(tp, uio, flag);
}

int
vioconwrite(dev_t dev, struct uio *uio, int flag)
{
	struct viocon_port *vp = dev2port(dev);
	struct tty *tp = vp->vp_tty;

	return linesw[tp->t_line].l_write(tp, uio, flag);
}

struct tty *
viocontty(dev_t dev)
{
	struct viocon_port *vp = dev2port(dev);

	return vp->vp_tty;
}

int
vioconstop(struct tty *tp, int flag)
{
	int s;

	s = spltty();
	if (ISSET(tp->t_state, TS_BUSY))
		if (!ISSET(tp->t_state, TS_TTSTOP))
			SET(tp->t_state, TS_FLUSH);
	splx(s);
	return 0;
}

int
vioconioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct viocon_port *vp = dev2port(dev);
	struct tty *tp;
	int error1, error2;

	tp = vp->vp_tty;

	error1 = (*linesw[tp->t_line].l_ioctl)(tp, cmd, data, flag, p);
	if (error1 >= 0)
		return error1;
	error2 = ttioctl(tp, cmd, data, flag, p);
	if (error2 >= 0)
		return error2;
	return ENOTTY;
}
