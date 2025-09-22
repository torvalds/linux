/* $OpenBSD: exuart.c,v 1.13 2025/07/03 21:06:51 kettenis Exp $ */
/*
 * Copyright (c) 2005 Dale Rahn <drahn@motorola.com>
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
#include <sys/ioctl.h>
#include <sys/proc.h>
#include <sys/tty.h>
#include <sys/uio.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/device.h>
#include <sys/syslog.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/cons.h>

#ifdef DDB
#include <ddb/db_var.h>
#endif

#include <dev/fdt/exuartreg.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_power.h>
#include <dev/ofw/fdt.h>

#define DEVUNIT(x)      (minor(x) & 0x7f)
#define DEVCUA(x)       (minor(x) & 0x80)

struct exuart_softc {
	struct device	sc_dev;
	bus_space_tag_t sc_iot;
	bus_space_handle_t sc_ioh;
	struct soft_intrhand *sc_si;
	void *sc_irq;
	struct tty	*sc_tty;
	struct timeout	sc_diag_tmo;
	struct timeout	sc_dtr_tmo;

	uint32_t	sc_rx_fifo_cnt_mask;
	uint32_t	sc_rx_fifo_full;
	uint32_t	sc_tx_fifo_full;
	int		sc_type;
#define EXUART_TYPE_EXYNOS	0
#define EXUART_TYPE_S5L		1

	int		sc_fifo;
	int		sc_overflows;
	int		sc_floods;
	int		sc_errors;
	int		sc_halt;
	u_int32_t	sc_ulcon;
	u_int32_t	sc_ucon;
	u_int32_t	sc_ufcon;
	u_int32_t	sc_umcon;
	u_int32_t	sc_uintm;
	u_int8_t	sc_hwflags;
#define COM_HW_NOIEN    0x01
#define COM_HW_FIFO     0x02
#define COM_HW_SIR      0x20
#define COM_HW_CONSOLE  0x40
	u_int8_t	sc_swflags;
#define COM_SW_SOFTCAR  0x01
#define COM_SW_CLOCAL   0x02
#define COM_SW_CRTSCTS  0x04
#define COM_SW_MDMBUF   0x08
#define COM_SW_PPS      0x10

	u_int8_t	sc_initialize;
	u_int8_t	sc_cua;
	u_int16_t 	*sc_ibuf, *sc_ibufp, *sc_ibufhigh, *sc_ibufend;
#define EXUART_IBUFSIZE 128
#define EXUART_IHIGHWATER 100
	u_int16_t		sc_ibufs[2][EXUART_IBUFSIZE];
};


int	 exuart_match(struct device *, void *, void *);
void	 exuart_attach(struct device *, struct device *, void *);

void exuartcnprobe(struct consdev *cp);
void exuartcninit(struct consdev *cp);
int exuartcnattach(bus_space_tag_t iot, bus_addr_t iobase, int rate,
    tcflag_t cflag);
int exuartcngetc(dev_t dev);
void exuartcnputc(dev_t dev, int c);
void exuartcnpollc(dev_t dev, int on);
int  exuart_param(struct tty *tp, struct termios *t);
void exuart_start(struct tty *);
void exuart_diag(void *arg);
void exuart_raisedtr(void *arg);
void exuart_softint(void *arg);
struct exuart_softc *exuart_sc(dev_t dev);

int exuart_intr(void *);
int exuart_s5l_intr(void *);

/* XXX - we imitate 'com' serial ports and take over their entry points */
/* XXX: These belong elsewhere */
cdev_decl(com);
cdev_decl(exuart);

struct cfdriver exuart_cd = {
	NULL, "exuart", DV_TTY
};

const struct cfattach exuart_ca = {
	sizeof(struct exuart_softc), exuart_match, exuart_attach
};

bus_space_tag_t	exuartconsiot;
bus_space_handle_t exuartconsioh;
bus_addr_t	exuartconsaddr;
tcflag_t	exuartconscflag = TTYDEF_CFLAG;
int		exuartdefaultrate = B115200;

uint32_t	exuart_rx_fifo_cnt_mask;
uint32_t	exuart_rx_fifo_full;
uint32_t	exuart_tx_fifo_full;

struct cdevsw exuartdev =
	cdev_tty_init(3/*XXX NEXUART */ ,exuart);		/* 12: serial port */

void
exuart_init_cons(void)
{
	struct fdt_reg reg;
	void *node, *root;

	if ((node = fdt_find_cons("apple,s5l-uart")) == NULL &&
	    (node = fdt_find_cons("samsung,exynos4210-uart")) == NULL)
		return;

	/* dtb uses serial2, qemu uses serial0 */
	root = fdt_find_node("/");
	if (root == NULL)
		panic("%s: could not get fdt root node", __func__);
	if (fdt_is_compatible(root, "samsung,universal_c210")) {
		if ((node = fdt_find_node("/serial@13800000")) == NULL) {
			return;
		}
		stdout_node = OF_finddevice("/serial@13800000");
	}

	if (fdt_get_reg(node, 0, &reg))
		return;

	if (fdt_is_compatible(node, "apple,s5l-uart")) {
		exuart_rx_fifo_cnt_mask = EXUART_S5L_UFSTAT_RX_FIFO_CNT_MASK;
		exuart_rx_fifo_full = EXUART_S5L_UFSTAT_RX_FIFO_FULL;
		exuart_tx_fifo_full = EXUART_S5L_UFSTAT_TX_FIFO_FULL;
	} else {
		exuart_rx_fifo_cnt_mask = EXUART_UFSTAT_RX_FIFO_CNT_MASK;
		exuart_rx_fifo_full = EXUART_UFSTAT_RX_FIFO_FULL;
		exuart_tx_fifo_full = EXUART_UFSTAT_TX_FIFO_FULL;
	}

	exuartcnattach(fdt_cons_bs_tag, reg.addr, B115200, TTYDEF_CFLAG);
}

int
exuart_match(struct device *parent, void *self, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "apple,s5l-uart") ||
	    OF_is_compatible(faa->fa_node, "samsung,exynos4210-uart"));
}

void
exuart_attach(struct device *parent, struct device *self, void *aux)
{
	struct exuart_softc *sc = (struct exuart_softc *) self;
	struct fdt_attach_args *faa = aux;
	int maj;

	if (faa->fa_nreg < 1)
		return;

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr, faa->fa_reg[0].size,
	    0, &sc->sc_ioh))
		panic("%s: bus_space_map failed!", __func__);

	if (stdout_node == faa->fa_node) {
		/* Locate the major number. */
		for (maj = 0; maj < nchrdev; maj++)
			if (cdevsw[maj].d_open == exuartopen)
				break;
		cn_tab->cn_dev = makedev(maj, sc->sc_dev.dv_unit);

		printf(": console");
		SET(sc->sc_hwflags, COM_HW_CONSOLE);
	}

	printf("\n");

	power_domain_enable(faa->fa_node);

	if (OF_is_compatible(faa->fa_node, "apple,s5l-uart")) {
		sc->sc_type = EXUART_TYPE_S5L;
		sc->sc_rx_fifo_cnt_mask = EXUART_S5L_UFSTAT_RX_FIFO_CNT_MASK;
		sc->sc_rx_fifo_full = EXUART_S5L_UFSTAT_RX_FIFO_FULL;
		sc->sc_tx_fifo_full = EXUART_S5L_UFSTAT_TX_FIFO_FULL;

		/* Mask and clear interrupts. */
		sc->sc_ucon = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
		    EXUART_UCON);
		CLR(sc->sc_ucon, EXUART_S5L_UCON_RX_TIMEOUT);
		CLR(sc->sc_ucon, EXUART_S5L_UCON_RXTHRESH);
		CLR(sc->sc_ucon, EXUART_S5L_UCON_TXTHRESH);
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, EXUART_UCON,
		    sc->sc_ucon);
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, EXUART_UTRSTAT,
		    EXUART_S5L_UTRSTAT_RX_TIMEOUT |
		    EXUART_S5L_UTRSTAT_RXTHRESH |
		    EXUART_S5L_UTRSTAT_TXTHRESH);

		sc->sc_ucon = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
		    EXUART_UCON);
		SET(sc->sc_ucon, EXUART_UCON_RX_TIMEOUT);
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, EXUART_UCON,
		    sc->sc_ucon);

		sc->sc_irq = fdt_intr_establish(faa->fa_node, IPL_TTY,
		    exuart_s5l_intr, sc, sc->sc_dev.dv_xname);
	} else {
		sc->sc_type = EXUART_TYPE_EXYNOS;
		sc->sc_rx_fifo_cnt_mask = EXUART_UFSTAT_RX_FIFO_CNT_MASK;
		sc->sc_rx_fifo_full = EXUART_UFSTAT_RX_FIFO_FULL;
		sc->sc_tx_fifo_full = EXUART_UFSTAT_TX_FIFO_FULL;

		/* Mask and clear interrupts. */
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, EXUART_UINTM,
		    EXUART_UINTM_RXD | EXUART_UINTM_ERROR |
		    EXUART_UINTM_TXD | EXUART_UINTM_MODEM);
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, EXUART_UINTP,
		    EXUART_UINTP_RXD | EXUART_UINTP_ERROR |
		    EXUART_UINTP_TXD | EXUART_UINTP_MODEM);

		sc->sc_ucon = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
		    EXUART_UCON);
		CLR(sc->sc_ucon, EXUART_UCON_RX_TIMEOUT_EMPTY_FIFO);
		SET(sc->sc_ucon, EXUART_UCON_RX_INT_TYPE_LEVEL);
		SET(sc->sc_ucon, EXUART_UCON_RX_TIMEOUT);
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, EXUART_UCON,
		    sc->sc_ucon);

		sc->sc_irq = fdt_intr_establish(faa->fa_node, IPL_TTY,
		    exuart_intr, sc, sc->sc_dev.dv_xname);
	}

	timeout_set(&sc->sc_diag_tmo, exuart_diag, sc);
	timeout_set(&sc->sc_dtr_tmo, exuart_raisedtr, sc);
	sc->sc_si = softintr_establish(IPL_TTY, exuart_softint, sc);

	if(sc->sc_si == NULL)
		panic("%s: can't establish soft interrupt.",
		    sc->sc_dev.dv_xname);
}

void
exuart_rx_intr(struct exuart_softc *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	u_int16_t *p;
	u_int16_t c;

	p = sc->sc_ibufp;

	while (bus_space_read_4(iot, ioh, EXUART_UFSTAT) &
	    (sc->sc_rx_fifo_cnt_mask | sc->sc_rx_fifo_full)) {
		c = bus_space_read_4(iot, ioh, EXUART_URXH);
		if (p >= sc->sc_ibufend) {
			sc->sc_floods++;
			if (sc->sc_errors++ == 0)
				timeout_add_sec(&sc->sc_diag_tmo, 60);
		} else {
			*p++ = c;
#if 0
			if (p == sc->sc_ibufhigh &&
			    ISSET(tp->t_cflag, CRTSCTS)) {
				/* XXX */
			}
#endif
		}
	}

	sc->sc_ibufp = p;

	softintr_schedule(sc->sc_si);
}

void
exuart_tx_intr(struct exuart_softc *sc)
{
	struct tty *tp = sc->sc_tty;

	if (ISSET(tp->t_state, TS_BUSY)) {
		CLR(tp->t_state, TS_BUSY | TS_FLUSH);
		if (sc->sc_halt > 0)
			wakeup(&tp->t_outq);
		(*linesw[tp->t_line].l_start)(tp);
	}
}

int
exuart_intr(void *arg)
{
	struct exuart_softc *sc = arg;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	u_int32_t uintp;

	uintp = bus_space_read_4(iot, ioh, EXUART_UINTP);
	if (uintp == 0)
		return (0);

	if (sc->sc_tty == NULL)
		return (0);

	if (ISSET(uintp, EXUART_UINTP_RXD)) {
		exuart_rx_intr(sc);
		bus_space_write_4(iot, ioh, EXUART_UINTP, EXUART_UINTP_RXD);
	}

	if (ISSET(uintp, EXUART_UINTP_TXD)) {
		exuart_tx_intr(sc);
		bus_space_write_4(iot, ioh, EXUART_UINTP, EXUART_UINTP_TXD);
	}

#if 0
	if(!ISSET(bus_space_read_2(iot, ioh, EXUART_USR2), EXUART_SR2_RDR))
		return 0;

	p = sc->sc_ibufp;

	while(ISSET(bus_space_read_2(iot, ioh, EXUART_USR2), EXUART_SR2_RDR)) {
		c = bus_space_read_4(iot, ioh, EXUART_URXH);
		if (p >= sc->sc_ibufend) {
			sc->sc_floods++;
			if (sc->sc_errors++ == 0)
				timeout_add_sec(&sc->sc_diag_tmo, 60);
		} else {
			*p++ = c;
			if (p == sc->sc_ibufhigh && ISSET(tp->t_cflag, CRTSCTS))
				/* XXX */
				//CLR(sc->sc_ucr3, EXUART_CR3_DSR);
				//bus_space_write_2(iot, ioh, EXUART_UCR3,
				//    sc->sc_ucr3);

		}
		/* XXX - msr stuff ? */
	}
	sc->sc_ibufp = p;

	softintr_schedule(sc->sc_si);
#endif

	return 1;
}

int
exuart_s5l_intr(void *arg)
{
	struct exuart_softc *sc = arg;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	u_int32_t utrstat, uerstat;

	utrstat = bus_space_read_4(iot, ioh, EXUART_UTRSTAT);
	uerstat = bus_space_read_4(iot, ioh, EXUART_UERSTAT);

	if (sc->sc_tty == NULL)
		return (0);

#ifdef DDB
	if (uerstat & EXUART_UERSTAT_BREAK) {
		if (ISSET(sc->sc_hwflags, COM_HW_CONSOLE)) {
			if (db_console)
				db_enter();
		}
	}
#endif

	if (utrstat & (EXUART_S5L_UTRSTAT_RXTHRESH |
	    EXUART_S5L_UTRSTAT_RX_TIMEOUT))
		exuart_rx_intr(sc);

	if (utrstat & EXUART_S5L_UTRSTAT_TXTHRESH)
		exuart_tx_intr(sc);

	bus_space_write_4(iot, ioh, EXUART_UTRSTAT, utrstat);

	return 1;
}

int
exuart_param(struct tty *tp, struct termios *t)
{
	struct exuart_softc *sc = exuart_cd.cd_devs[DEVUNIT(tp->t_dev)];
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	int ospeed = t->c_ospeed;
	int error;
	tcflag_t oldcflag;


	if (t->c_ospeed < 0 || (t->c_ispeed && t->c_ispeed != t->c_ospeed))
		return EINVAL;

	switch (ISSET(t->c_cflag, CSIZE)) {
	case CS5:
		CLR(sc->sc_ulcon, EXUART_ULCON_WORD_MASK);
		SET(sc->sc_ulcon, EXUART_ULCON_WORD_FIVE);
		break;
	case CS6:
		CLR(sc->sc_ulcon, EXUART_ULCON_WORD_MASK);
		SET(sc->sc_ulcon, EXUART_ULCON_WORD_SIX);
		break;
	case CS7:
		CLR(sc->sc_ulcon, EXUART_ULCON_WORD_MASK);
		SET(sc->sc_ulcon, EXUART_ULCON_WORD_SEVEN);
		break;
	case CS8:
		CLR(sc->sc_ulcon, EXUART_ULCON_WORD_MASK);
		SET(sc->sc_ulcon, EXUART_ULCON_WORD_EIGHT);
		break;
	}

	CLR(sc->sc_ulcon, EXUART_ULCON_PARITY_MASK);
	if (ISSET(t->c_cflag, PARENB)) {
		if (ISSET(t->c_cflag, PARODD))
			SET(sc->sc_ulcon, EXUART_ULCON_PARITY_ODD);
		else
			SET(sc->sc_ulcon, EXUART_ULCON_PARITY_EVEN);
	}

	if (ISSET(t->c_cflag, CSTOPB))
		SET(sc->sc_ulcon, EXUART_ULCON_STOP_TWO);
	else
		CLR(sc->sc_ulcon, EXUART_ULCON_STOP_ONE);

	bus_space_write_4(iot, ioh, EXUART_ULCON, sc->sc_ulcon);

	if (ospeed == 0) {
		/* lower dtr */
	}

	if (ospeed != 0) {
		while (ISSET(tp->t_state, TS_BUSY)) {
			++sc->sc_halt;
			error = ttysleep(tp, &tp->t_outq,
			    TTOPRI | PCATCH, "exuartprm");
			--sc->sc_halt;
			if (error) {
				exuart_start(tp);
				return (error);
			}
		}
		/* set speed */
	}

	/* setup fifo */

	/* When not using CRTSCTS, RTS follows DTR. */
	/* sc->sc_dtr = MCR_DTR; */


	/* and copy to tty */
	tp->t_ispeed = t->c_ispeed;
	tp->t_ospeed = t->c_ospeed;
	oldcflag = tp->t_cflag;
	tp->t_cflag = t->c_cflag;

        /*
	 * If DCD is off and MDMBUF is changed, ask the tty layer if we should
	 * stop the device.
	 */
	 /* XXX */

	exuart_start(tp);

	return 0;
}

void
exuart_start(struct tty *tp)
{
        struct exuart_softc *sc = exuart_cd.cd_devs[DEVUNIT(tp->t_dev)];
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	int s;

	s = spltty();
	if (ISSET(tp->t_state, TS_BUSY))
		goto out;
	if (ISSET(tp->t_state, TS_TIMEOUT | TS_TTSTOP) || sc->sc_halt > 0)
		goto stopped;
#ifdef DAMNFUCKSHIT
	/* clear to send (IE the RTS pin on this shit) is not directly \
	 * readable - skip check for now
	 */
	if (ISSET(tp->t_cflag, CRTSCTS) && !ISSET(sc->sc_msr, EXUART_CTS))
		goto stopped;
#endif
	ttwakeupwr(tp);
	if (tp->t_outq.c_cc == 0)
		goto stopped;
	SET(tp->t_state, TS_BUSY);

	{
		u_char buffer[16];
		int i, n;

		n = q_to_b(&tp->t_outq, buffer, sizeof buffer);
		for (i = 0; i < n; i++)
			bus_space_write_4(iot, ioh, EXUART_UTXH, buffer[i]);
		bzero(buffer, n);
	}

	if (sc->sc_type == EXUART_TYPE_S5L) {
		if (!ISSET(sc->sc_ucon, EXUART_S5L_UCON_TXTHRESH)) {
			SET(sc->sc_ucon, EXUART_S5L_UCON_TXTHRESH);
			bus_space_write_4(iot, ioh, EXUART_UCON, sc->sc_ucon);
		}
	} else {
		if (ISSET(sc->sc_uintm, EXUART_UINTM_TXD)) {
			CLR(sc->sc_uintm, EXUART_UINTM_TXD);
			bus_space_write_4(iot, ioh, EXUART_UINTM, sc->sc_uintm);
		}
	}

out:
	splx(s);
	return;
stopped:
	if (sc->sc_type == EXUART_TYPE_S5L) {
		if (ISSET(sc->sc_ucon, EXUART_S5L_UCON_TXTHRESH)) {
			CLR(sc->sc_ucon, EXUART_S5L_UCON_TXTHRESH);
			bus_space_write_4(iot, ioh, EXUART_UCON, sc->sc_ucon);
		}
	} else {
		if (!ISSET(sc->sc_uintm, EXUART_UINTM_TXD)) {
			SET(sc->sc_uintm, EXUART_UINTM_TXD);
			bus_space_write_4(iot, ioh, EXUART_UINTM, sc->sc_uintm);
		}
	}
	splx(s);
}

void
exuart_diag(void *arg)
{
	struct exuart_softc *sc = arg;
	int overflows, floods;
	int s;

	s = spltty();
	sc->sc_errors = 0;
	overflows = sc->sc_overflows;
	sc->sc_overflows = 0;
	floods = sc->sc_floods;
	sc->sc_floods = 0;
	splx(s);
	log(LOG_WARNING, "%s: %d silo overflow%s, %d ibuf overflow%s\n",
	    sc->sc_dev.dv_xname,
	    overflows, overflows == 1 ? "" : "s",
	    floods, floods == 1 ? "" : "s");
}

void
exuart_raisedtr(void *arg)
{
	//struct exuart_softc *sc = arg;

	//SET(sc->sc_ucr3, EXUART_CR3_DSR); /* XXX */
	//bus_space_write_2(sc->sc_iot, sc->sc_ioh, EXUART_UCR3, sc->sc_ucr3);
}

void
exuart_softint(void *arg)
{
	struct exuart_softc *sc = arg;
	struct tty *tp;
	u_int16_t *ibufp;
	u_int16_t *ibufend;
	int c;
	int err;
	int s;

	if (sc == NULL || sc->sc_ibufp == sc->sc_ibuf)
		return;

	tp = sc->sc_tty;

	s = spltty();

	ibufp = sc->sc_ibuf;
	ibufend = sc->sc_ibufp;

	if (ibufp == ibufend) {
		splx(s);
		return;
	}

	sc->sc_ibufp = sc->sc_ibuf = (ibufp == sc->sc_ibufs[0]) ?
	    sc->sc_ibufs[1] : sc->sc_ibufs[0];
	sc->sc_ibufhigh = sc->sc_ibuf + EXUART_IHIGHWATER;
	sc->sc_ibufend = sc->sc_ibuf + EXUART_IBUFSIZE;

	if (tp == NULL || !ISSET(tp->t_state, TS_ISOPEN)) {
		splx(s);
		return;
	}

#if 0
	if (ISSET(tp->t_cflag, CRTSCTS) &&
	    !ISSET(sc->sc_ucr3, EXUART_CR3_DSR)) {
		/* XXX */
		SET(sc->sc_ucr3, EXUART_CR3_DSR);
		bus_space_write_2(sc->sc_iot, sc->sc_ioh, EXUART_UCR3,
		    sc->sc_ucr3);
	}
#endif

	splx(s);

	while (ibufp < ibufend) {
		c = *ibufp++;
#if 0
		if (ISSET(c, EXUART_UERSTAT_OVERRUN)) {
			sc->sc_overflows++;
			if (sc->sc_errors++ == 0)
				timeout_add_sec(&sc->sc_diag_tmo, 60);
		}
#endif
		err = 0;
#if 0
		if (ISSET(c, EXUART_UERSTAT_PARITY))
			err |= TTY_PE;
		if (ISSET(c, EXUART_UERSTAT_FRAME))
			err |= TTY_FE;
#endif
		c = (c & 0xff) | err;
		(*linesw[tp->t_line].l_rint)(c, tp);
	}
}

int
exuartopen(dev_t dev, int flag, int mode, struct proc *p)
{
	int unit = DEVUNIT(dev);
	struct exuart_softc *sc;
	bus_space_tag_t iot;
	bus_space_handle_t ioh;
	struct tty *tp;
	int s;
	int error = 0;

	if (unit >= exuart_cd.cd_ndevs)
		return ENXIO;
	sc = exuart_cd.cd_devs[unit];
	if (sc == NULL)
		return ENXIO;

	s = spltty();
	if (sc->sc_tty == NULL)
		tp = sc->sc_tty = ttymalloc(0);
	else
		tp = sc->sc_tty;
	splx(s);

	tp->t_oproc = exuart_start;
	tp->t_param = exuart_param;
	tp->t_dev = dev;
	if (!ISSET(tp->t_state, TS_ISOPEN)) {
		SET(tp->t_state, TS_WOPEN);
		ttychars(tp);
		tp->t_iflag = TTYDEF_IFLAG;
		tp->t_oflag = TTYDEF_OFLAG;

		if (ISSET(sc->sc_hwflags, COM_HW_CONSOLE))
			tp->t_cflag = exuartconscflag;
		else
			tp->t_cflag = TTYDEF_CFLAG;
		if (ISSET(sc->sc_swflags, COM_SW_CLOCAL))
			SET(tp->t_cflag, CLOCAL);
		if (ISSET(sc->sc_swflags, COM_SW_CRTSCTS))
			SET(tp->t_cflag, CRTSCTS);
		if (ISSET(sc->sc_swflags, COM_SW_MDMBUF))
			SET(tp->t_cflag, MDMBUF);
		tp->t_lflag = TTYDEF_LFLAG;
		tp->t_ispeed = tp->t_ospeed = exuartdefaultrate;

		s = spltty();

		sc->sc_initialize = 1;
		exuart_param(tp, &tp->t_termios);
		ttsetwater(tp);

		sc->sc_ibufp = sc->sc_ibuf = sc->sc_ibufs[0];
		sc->sc_ibufhigh = sc->sc_ibuf + EXUART_IHIGHWATER;
		sc->sc_ibufend = sc->sc_ibuf + EXUART_IBUFSIZE;

		iot = sc->sc_iot;
		ioh = sc->sc_ioh;

		sc->sc_ulcon = bus_space_read_4(iot, ioh, EXUART_ULCON);
		sc->sc_ucon = bus_space_read_4(iot, ioh, EXUART_UCON);
		sc->sc_ufcon = bus_space_read_4(iot, ioh, EXUART_UFCON);
		sc->sc_umcon = bus_space_read_4(iot, ioh, EXUART_UMCON);

		if (sc->sc_type == EXUART_TYPE_S5L) {
			SET(sc->sc_ucon, EXUART_UCON_RX_TIMEOUT);
			SET(sc->sc_ucon, EXUART_S5L_UCON_RXTHRESH);
			SET(sc->sc_ucon, EXUART_S5L_UCON_RX_TIMEOUT);
			bus_space_write_4(iot, ioh, EXUART_UCON, sc->sc_ucon);
		} else {
			sc->sc_uintm = bus_space_read_4(iot, ioh, EXUART_UINTM);
			CLR(sc->sc_uintm, EXUART_UINTM_RXD);
			bus_space_write_4(iot, ioh, EXUART_UINTM, sc->sc_uintm);
		}

#if 0
		/* interrupt after one char on tx/rx */
		/* reference frequency divider: 1 */
		bus_space_write_2(iot, ioh, EXUART_UFCR,
		    1 << EXUART_FCR_TXTL_SH |
		    5 << EXUART_FCR_RFDIV_SH |
		    1 << EXUART_FCR_RXTL_SH);

		bus_space_write_2(iot, ioh, EXUART_UBIR,
		    (exuartdefaultrate / 100) - 1);

		/* formula: clk / (rfdiv * 1600) */
		bus_space_write_2(iot, ioh, EXUART_UBMR,
		    (exccm_get_uartclk() * 1000) / 1600);

		SET(sc->sc_ucr1, EXUART_CR1_EN|EXUART_CR1_RRDYEN);
		SET(sc->sc_ucr2, EXUART_CR2_TXEN|EXUART_CR2_RXEN);
		bus_space_write_2(iot, ioh, EXUART_UCR1, sc->sc_ucr1);
		bus_space_write_2(iot, ioh, EXUART_UCR2, sc->sc_ucr2);

		/* sc->sc_mcr = MCR_DTR | MCR_RTS;  XXX */
		SET(sc->sc_ucr3, EXUART_CR3_DSR); /* XXX */
		bus_space_write_2(iot, ioh, EXUART_UCR3, sc->sc_ucr3);
#endif

		SET(tp->t_state, TS_CARR_ON); /* XXX */


	} else if (ISSET(tp->t_state, TS_XCLUDE) && suser(p) != 0)
		return EBUSY;
	else
		s = spltty();

	if (DEVCUA(dev)) {
		if (ISSET(tp->t_state, TS_ISOPEN)) {
			splx(s);
			return EBUSY;
		}
		sc->sc_cua = 1;
	} else {
		/* tty (not cua) device; wait for carrier if necessary */
		if (ISSET(flag, O_NONBLOCK)) {
			if (sc->sc_cua) {
				/* Opening TTY non-blocking... but the CUA is busy */
				splx(s);
				return EBUSY;
			}
		} else {
			while (sc->sc_cua ||
			    (!ISSET(tp->t_cflag, CLOCAL) &&
				!ISSET(tp->t_state, TS_CARR_ON))) {
				SET(tp->t_state, TS_WOPEN);
				error = ttysleep(tp, &tp->t_rawq,
				    TTIPRI | PCATCH, ttopen);
				/*
				 * If TS_WOPEN has been reset, that means the
				 * cua device has been closed.  We don't want
				 * to fail in that case,
				 * so just go around again.
				 */
				if (error && ISSET(tp->t_state, TS_WOPEN)) {
					CLR(tp->t_state, TS_WOPEN);
					splx(s);
					return error;
				}
			}
		}
	}
	splx(s);

	return (*linesw[tp->t_line].l_open)(dev,tp,p);
}

int
exuartclose(dev_t dev, int flag, int mode, struct proc *p)
{
	int unit = DEVUNIT(dev);
	struct exuart_softc *sc = exuart_cd.cd_devs[unit];
	//bus_space_tag_t iot = sc->sc_iot;
	//bus_space_handle_t ioh = sc->sc_ioh;
	struct tty *tp = sc->sc_tty;
	int s;

	/* XXX This is for cons.c. */
	if (!ISSET(tp->t_state, TS_ISOPEN))
		return 0;

	(*linesw[tp->t_line].l_close)(tp, flag, p);
	s = spltty();
	if (ISSET(tp->t_state, TS_WOPEN)) {
		/* tty device is waiting for carrier; drop dtr then re-raise */
		//CLR(sc->sc_ucr3, EXUART_CR3_DSR);
		//bus_space_write_2(iot, ioh, EXUART_UCR3, sc->sc_ucr3);
		timeout_add_sec(&sc->sc_dtr_tmo, 2);
	}
	CLR(tp->t_state, TS_BUSY | TS_FLUSH);
	sc->sc_cua = 0;
	splx(s);
	ttyclose(tp);

	return 0;
}

int
exuartread(dev_t dev, struct uio *uio, int flag)
{
	struct tty *tty;

	tty = exuarttty(dev);
	if (tty == NULL)
		return ENODEV;

	return((*linesw[tty->t_line].l_read)(tty, uio, flag));
}

int
exuartwrite(dev_t dev, struct uio *uio, int flag)
{
	struct tty *tty;

	tty = exuarttty(dev);
	if (tty == NULL)
		return ENODEV;

	return((*linesw[tty->t_line].l_write)(tty, uio, flag));
}

int
exuartioctl( dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct exuart_softc *sc;
	struct tty *tp;
	int error;

	sc = exuart_sc(dev);
	if (sc == NULL)
		return (ENODEV);

	tp = sc->sc_tty;
	if (tp == NULL)
		return (ENXIO);

	error = (*linesw[tp->t_line].l_ioctl)(tp, cmd, data, flag, p);
	if (error >= 0)
		return (error);

	error = ttioctl(tp, cmd, data, flag, p);
	if (error >= 0)
		return (error);

	switch(cmd) {
	case TIOCSBRK:
		/* */
		break;

	case TIOCCBRK:
		/* */
		break;

	case TIOCSDTR:
#if 0
		(void) clmctl(dev, TIOCM_DTR | TIOCM_RTS, DMBIS);
#endif
		break;

	case TIOCCDTR:
#if 0
		(void) clmctl(dev, TIOCM_DTR | TIOCM_RTS, DMBIC);
#endif
		break;

	case TIOCMSET:
#if 0
		(void) clmctl(dev, *(int *) data, DMSET);
#endif
		break;

	case TIOCMBIS:
#if 0
		(void) clmctl(dev, *(int *) data, DMBIS);
#endif
		break;

	case TIOCMBIC:
#if 0
		(void) clmctl(dev, *(int *) data, DMBIC);
#endif
		break;

        case TIOCMGET:
#if 0
		*(int *)data = clmctl(dev, 0, DMGET);
#endif
		break;

	case TIOCGFLAGS:
#if 0
		*(int *)data = cl->cl_swflags;
#endif
		break;

	case TIOCSFLAGS:
		error = suser(p);
		if (error != 0)
			return(EPERM);

#if 0
		cl->cl_swflags = *(int *)data;
		cl->cl_swflags &= /* only allow valid flags */
		    (TIOCFLAG_SOFTCAR | TIOCFLAG_CLOCAL | TIOCFLAG_CRTSCTS);
#endif
		break;
	default:
		return (ENOTTY);
	}

	return 0;
}

int
exuartstop(struct tty *tp, int flag)
{
	return 0;
}

struct tty *
exuarttty(dev_t dev)
{
	int unit;
	struct exuart_softc *sc;
	unit = DEVUNIT(dev);
	if (unit >= exuart_cd.cd_ndevs)
		return NULL;
	sc = (struct exuart_softc *)exuart_cd.cd_devs[unit];
	if (sc == NULL)
		return NULL;
	return sc->sc_tty;
}

struct exuart_softc *
exuart_sc(dev_t dev)
{
	int unit;
	struct exuart_softc *sc;
	unit = DEVUNIT(dev);
	if (unit >= exuart_cd.cd_ndevs)
		return NULL;
	sc = (struct exuart_softc *)exuart_cd.cd_devs[unit];
	return sc;
}


/* serial console */
void
exuartcnprobe(struct consdev *cp)
{
}

void
exuartcninit(struct consdev *cp)
{
}

int
exuartcnattach(bus_space_tag_t iot, bus_addr_t iobase, int rate, tcflag_t cflag)
{
	static struct consdev exuartcons = {
		NULL, NULL, exuartcngetc, exuartcnputc, exuartcnpollc, NULL,
		NODEV, CN_MIDPRI
	};
	int maj;

	if (bus_space_map(iot, iobase, 0x100, 0, &exuartconsioh))
		return ENOMEM;

	/* Look for major of com(4) to replace. */
	for (maj = 0; maj < nchrdev; maj++)
		if (cdevsw[maj].d_open == comopen)
			break;
	if (maj == nchrdev)
		return ENXIO;

	cn_tab = &exuartcons;
	cn_tab->cn_dev = makedev(maj, 0);
	cdevsw[maj] = exuartdev; 	/* KLUDGE */

	exuartconsiot = iot;
	exuartconsaddr = iobase;
	exuartconscflag = cflag;

	return 0;
}

int
exuartcngetc(dev_t dev)
{
	int c;
	int s;
	s = splhigh();
	while((bus_space_read_4(exuartconsiot, exuartconsioh, EXUART_UTRSTAT) &
	    EXUART_UTRSTAT_RXBREADY) == 0 &&
	      (bus_space_read_4(exuartconsiot, exuartconsioh, EXUART_UFSTAT) &
	    (exuart_rx_fifo_cnt_mask | exuart_rx_fifo_full)) == 0)
		;
	c = bus_space_read_4(exuartconsiot, exuartconsioh, EXUART_URXH);
	splx(s);
	return c;
}

void
exuartcnputc(dev_t dev, int c)
{
	int s;
	s = splhigh();
	while (bus_space_read_4(exuartconsiot, exuartconsioh, EXUART_UFSTAT) &
	   exuart_tx_fifo_full)
		;
	bus_space_write_4(exuartconsiot, exuartconsioh, EXUART_UTXH, c);
	splx(s);
}

void
exuartcnpollc(dev_t dev, int on)
{
}
