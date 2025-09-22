/*	$OpenBSD: vcctty.c,v 1.15 2021/10/24 17:05:04 mpi Exp $	*/
/*
 * Copyright (c) 2009 Mark Kettenis
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
#include <sys/tty.h>

#include <machine/autoconf.h>
#include <machine/conf.h>
#include <machine/hypervisor.h>
#include <machine/mdesc.h>

#include <dev/cons.h>
#include <sparc64/dev/cbusvar.h>
#include <sparc64/dev/ldcvar.h>

#ifdef VCCTTY_DEBUG
#define DPRINTF(x)	printf x
#else
#define DPRINTF(x)
#endif

#define VCCTTY_TX_ENTRIES	128
#define VCCTTY_RX_ENTRIES	128

struct vcctty_msg {
	uint8_t		type;
	uint8_t		size;
	uint16_t	rsvd;
	uint32_t	ctrl_msg;
	uint8_t		data[56];
};

/* Packet types. */
#define LDC_CONSOLE_CTRL	0x01
#define LDC_CONSOLE_DATA	0x02

struct vcctty_softc {
	struct device	sc_dv;
	bus_space_tag_t	sc_bustag;
	bus_dma_tag_t	sc_dmatag;

	uint64_t	sc_tx_ino;
	uint64_t	sc_rx_ino;
	void		*sc_tx_ih;
	void		*sc_rx_ih;

	struct ldc_conn	sc_lc;

	struct tty	*sc_tty;
};

int	vcctty_match(struct device *, void *, void *);
void	vcctty_attach(struct device *, struct device *, void *);

const struct cfattach vcctty_ca = {
	sizeof(struct vcctty_softc), vcctty_match, vcctty_attach
};

struct cfdriver vcctty_cd = {
	NULL, "vcctty", DV_DULL
};

int	vcctty_tx_intr(void *);
int	vcctty_rx_intr(void *);

void	vcctty_send_data(struct vcctty_softc *, struct tty *);
void	vcctty_send_break(struct vcctty_softc *);

void	vccttystart(struct tty *);
int	vccttyparam(struct tty *, struct termios *);
int	vccttyhwiflow(struct tty *, int);

int
vcctty_match(struct device *parent, void *match, void *aux)
{
	return (1);
}

void
vcctty_attach(struct device *parent, struct device *self, void *aux)
{
	struct vcctty_softc *sc = (struct vcctty_softc *)self;
	struct cbus_attach_args *ca = aux;
	struct ldc_conn *lc;
	int err;

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
	    IPL_TTY, 0, vcctty_tx_intr, sc, sc->sc_dv.dv_xname);
	sc->sc_rx_ih = bus_intr_establish(ca->ca_bustag, sc->sc_rx_ino,
	    IPL_TTY, 0, vcctty_rx_intr, sc, sc->sc_dv.dv_xname);
	if (sc->sc_tx_ih == NULL || sc->sc_rx_ih == NULL) {
		printf(", can't establish interrupt\n");
		return;
	}

	lc = &sc->sc_lc;
	lc->lc_id = ca->ca_id;
	lc->lc_sc = sc;

	lc->lc_txq = ldc_queue_alloc(sc->sc_dmatag, VCCTTY_TX_ENTRIES);
	if (lc->lc_txq == NULL) {
		printf(", can't allocate tx queue\n");
		return;
	}

	lc->lc_rxq = ldc_queue_alloc(sc->sc_dmatag, VCCTTY_RX_ENTRIES);
	if (lc->lc_rxq == NULL) {
		printf(", can't allocate rx queue\n");
		goto free_txqueue;
	}

	err = hv_ldc_tx_qconf(lc->lc_id,
	    lc->lc_txq->lq_map->dm_segs[0].ds_addr, lc->lc_txq->lq_nentries);
	if (err != H_EOK)
		printf("%s: hv_ldc_tx_qconf %d\n", __func__, err);

	err = hv_ldc_rx_qconf(lc->lc_id,
	    lc->lc_rxq->lq_map->dm_segs[0].ds_addr, lc->lc_rxq->lq_nentries);
	if (err != H_EOK)
		printf("%s: hv_ldc_rx_qconf %d\n", __func__, err);

	cbus_intr_setenabled(sc->sc_bustag, sc->sc_tx_ino, INTR_ENABLED);
	cbus_intr_setenabled(sc->sc_bustag, sc->sc_rx_ino, INTR_ENABLED);

	printf(" domain \"%s\"\n", ca->ca_name);
	return;

free_txqueue:
	ldc_queue_free(sc->sc_dmatag, lc->lc_txq);
}

int
vcctty_tx_intr(void *arg)
{
	struct vcctty_softc *sc = arg;
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

	return (1);
}

int
vcctty_rx_intr(void *arg)
{
	struct vcctty_softc *sc = arg;
	struct tty *tp = sc->sc_tty;
	struct ldc_conn *lc = &sc->sc_lc;
	uint64_t rx_head, rx_tail, rx_state;
	struct vcctty_msg *msg;
	int err;
	int i;

	err = hv_ldc_rx_get_state(lc->lc_id, &rx_head, &rx_tail, &rx_state);
	if (err != H_EOK) {
		printf("%s: hv_ldc_rx_get_state %d\n", __func__, err);
		return (0);
	}

	if (rx_state != lc->lc_rx_state) {
		switch (rx_state) {
		case LDC_CHANNEL_DOWN:
			DPRINTF(("%s: Rx link down\n", __func__));
			break;
		case LDC_CHANNEL_UP:
			DPRINTF(("%s: Rx link up\n", __func__));
			break;
		case LDC_CHANNEL_RESET:
			DPRINTF(("%s: Rx link reset\n", __func__));
			break;
		}
		lc->lc_rx_state = rx_state;
		return (1);
	}

	if (rx_head == rx_tail)
		return (0);

	msg = (struct vcctty_msg *)(lc->lc_rxq->lq_va + rx_head);
	if (tp && msg->type == LDC_CONSOLE_DATA) {
		for (i = 0; i < msg->size; i++) {
			if (tp->t_state & TS_ISOPEN)
				(*linesw[tp->t_line].l_rint)(msg->data[i], tp);
		}
	}

	rx_head += sizeof(*msg);
	rx_head &= ((lc->lc_rxq->lq_nentries * sizeof(*msg)) - 1);
	err = hv_ldc_rx_set_qhead(lc->lc_id, rx_head);
	if (err != H_EOK)
		printf("%s: hv_ldc_rx_set_qhead %d\n", __func__, err);

	return (1);
}

void
vcctty_send_data(struct vcctty_softc *sc, struct tty *tp)
{
	struct ldc_conn *lc = &sc->sc_lc;
	uint64_t tx_head, tx_tail, tx_state;
	uint64_t next_tx_tail;
	struct vcctty_msg *msg;
	int err;

	err = hv_ldc_tx_get_state(lc->lc_id, &tx_head, &tx_tail, &tx_state);
	if (err != H_EOK || tx_state != LDC_CHANNEL_UP)
		return;

	while (tp->t_outq.c_cc > 0) {
		next_tx_tail = tx_tail + sizeof(*msg);
		next_tx_tail &= ((lc->lc_txq->lq_nentries * sizeof(*msg)) - 1);

		if (next_tx_tail == tx_head)
			return;

		msg = (struct vcctty_msg *)(lc->lc_txq->lq_va + tx_tail);
		bzero(msg, sizeof(*msg));
		msg->type = LDC_CONSOLE_DATA;
		msg->size = q_to_b(&tp->t_outq, msg->data, sizeof(msg->data));

		err = hv_ldc_tx_set_qtail(lc->lc_id, next_tx_tail);
		if (err != H_EOK)
			printf("%s: hv_ldc_tx_set_qtail: %d\n", __func__, err);
		tx_tail = next_tx_tail;
	}
}

void
vcctty_send_break(struct vcctty_softc *sc)
{
	struct ldc_conn *lc = &sc->sc_lc;
	uint64_t tx_head, tx_tail, tx_state;
	struct vcctty_msg *msg;
	int err;

	err = hv_ldc_tx_get_state(lc->lc_id, &tx_head, &tx_tail, &tx_state);
	if (err != H_EOK || tx_state != LDC_CHANNEL_UP)
		return;

	msg = (struct vcctty_msg *)(lc->lc_txq->lq_va + tx_tail);
	bzero(msg, sizeof(*msg));
	msg->type = LDC_CONSOLE_CTRL;
	msg->ctrl_msg = CONS_BREAK;

	tx_tail += sizeof(*msg);
	tx_tail &= ((lc->lc_txq->lq_nentries * sizeof(*msg)) - 1);
	err = hv_ldc_tx_set_qtail(lc->lc_id, tx_tail);
	if (err != H_EOK)
		printf("%s: hv_ldc_tx_set_qtail: %d\n", __func__, err);
}

int
vccttyopen(dev_t dev, int flag, int mode, struct proc *p)
{
	struct vcctty_softc *sc;
	struct tty *tp;
	int unit = minor(dev);

	if (unit >= vcctty_cd.cd_ndevs)
		return (ENXIO);
	sc = vcctty_cd.cd_devs[unit];
	if (sc == NULL)
		return (ENXIO);

	if (sc->sc_tty)
		tp = sc->sc_tty;
	else
		tp = sc->sc_tty = ttymalloc(0);

	tp->t_oproc = vccttystart;
	tp->t_param = vccttyparam;
	tp->t_hwiflow = vccttyhwiflow;
	tp->t_dev = dev;
	if ((tp->t_state & TS_ISOPEN) == 0) {
		ttychars(tp);
		tp->t_iflag = TTYDEF_IFLAG;
		tp->t_oflag = TTYDEF_OFLAG;
		tp->t_cflag = TTYDEF_CFLAG | CRTSCTS;
		tp->t_lflag = TTYDEF_LFLAG;
		tp->t_ispeed = tp->t_ospeed = TTYDEF_SPEED;
		ttsetwater(tp);
	} else if ((tp->t_state & TS_XCLUDE) && suser(p))
		return (EBUSY);
	tp->t_state |= TS_CARR_ON;

	return ((*linesw[tp->t_line].l_open)(dev, tp, p));
}

int
vccttyclose(dev_t dev, int flag, int mode, struct proc *p)
{
	struct vcctty_softc *sc;
	struct tty *tp;
	int unit = minor(dev);

	if (unit >= vcctty_cd.cd_ndevs)
		return (ENXIO);
	sc = vcctty_cd.cd_devs[unit];
	if (sc == NULL)
		return (ENXIO);

	tp = sc->sc_tty;
	(*linesw[tp->t_line].l_close)(tp, flag, p);
	ttyclose(tp);
	return (0);
}

int
vccttyread(dev_t dev, struct uio *uio, int flag)
{
	struct vcctty_softc *sc;
	struct tty *tp;
	int unit = minor(dev);

	if (unit >= vcctty_cd.cd_ndevs)
		return (ENXIO);
	sc = vcctty_cd.cd_devs[unit];
	if (sc == NULL)
		return (ENXIO);

	tp = sc->sc_tty;
	return ((*linesw[tp->t_line].l_read)(tp, uio, flag));
}

int
vccttywrite(dev_t dev, struct uio *uio, int flag)
{
	struct vcctty_softc *sc;
	struct tty *tp;
	int unit = minor(dev);

	if (unit >= vcctty_cd.cd_ndevs)
		return (ENXIO);
	sc = vcctty_cd.cd_devs[unit];
	if (sc == NULL)
		return (ENXIO);

	tp = sc->sc_tty;
	return ((*linesw[tp->t_line].l_write)(tp, uio, flag));
}

int
vccttyioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct vcctty_softc *sc;
	struct tty *tp;
	int unit = minor(dev);
	int error;

	if (unit >= vcctty_cd.cd_ndevs)
		return (ENXIO);
	sc = vcctty_cd.cd_devs[unit];
	if (sc == NULL)
		return (ENXIO);

	tp = sc->sc_tty;
	error = (*linesw[tp->t_line].l_ioctl)(tp, cmd, data, flag, p);
	if (error >= 0)
		return error;
	error = ttioctl(tp, cmd, data, flag, p);
	if (error >= 0)
		return (error);

	error = 0;

	switch (cmd) {
	case TIOCSBRK:
		vcctty_send_break(sc);
		break;
	case TIOCCBRK:
		/* BREAK gets cleared automatically. */
		break;
	default:
		error = ENOTTY;
		break;
	}

	return (error);
}

void
vccttystart(struct tty *tp)
{
	struct vcctty_softc *sc = vcctty_cd.cd_devs[minor(tp->t_dev)];
	int s;

	s = spltty();
	if (tp->t_state & (TS_TIMEOUT | TS_BUSY | TS_TTSTOP)) {
		splx(s);
		return;
	}
	ttwakeupwr(tp);
	tp->t_state |= TS_BUSY;
	if (tp->t_outq.c_cc > 0)
		vcctty_send_data(sc, tp);
	tp->t_state &= ~TS_BUSY;
	if (tp->t_outq.c_cc > 0) {
		tp->t_state |= TS_TIMEOUT;
		timeout_add(&tp->t_rstrt_to, 1);
	}
	splx(s);
}

int
vccttystop(struct tty *tp, int flag)
{
	int s;

	s = spltty();
	if (tp->t_state & TS_BUSY)
		if ((tp->t_state & TS_TTSTOP) == 0)
			tp->t_state |= TS_FLUSH;
	splx(s);
	return (0);
}

struct tty *
vccttytty(dev_t dev)
{
	struct vcctty_softc *sc;
	int unit = minor(dev);

	if (unit >= vcctty_cd.cd_ndevs)
		return (NULL);
	sc = vcctty_cd.cd_devs[unit];
	if (sc == NULL)
		return (NULL);

	return sc->sc_tty;
}

int
vccttyparam(struct tty *tp, struct termios *t)
{
	tp->t_ispeed = t->c_ispeed;
	tp->t_ospeed = t->c_ospeed;
	tp->t_cflag = t->c_cflag;
	return (0);
}

int
vccttyhwiflow(struct tty *tp, int stop)
{
	struct vcctty_softc *sc = vcctty_cd.cd_devs[minor(tp->t_dev)];
	uint64_t state = stop ? INTR_DISABLED : INTR_ENABLED;

	cbus_intr_setenabled(sc->sc_bustag, sc->sc_tx_ino, state);
	cbus_intr_setenabled(sc->sc_bustag, sc->sc_rx_ino, state);

	return (1);
}
