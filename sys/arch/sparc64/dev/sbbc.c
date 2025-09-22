/*	$OpenBSD: sbbc.c,v 1.15 2022/10/12 13:39:50 kettenis Exp $	*/
/*
 * Copyright (c) 2008 Mark Kettenis
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
#include <sys/timeout.h>
#include <sys/tty.h>
#include <sys/systm.h>

#ifdef DDB
#include <ddb/db_var.h>
#endif

#include <machine/autoconf.h>
#include <machine/conf.h>
#include <machine/openfirm.h>
#include <machine/sparc64.h>

#include <dev/cons.h>

#include <dev/pci/pcidevs.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/clock_subr.h>

extern todr_chip_handle_t todr_handle;

#define SBBC_PCI_BAR	PCI_MAPREG_START

#define SBBC_REGS_OFFSET	0x800000
#define SBBC_REGS_SIZE		0x6230
#define SBBC_EPLD_OFFSET	0x8e0000
#define SBBC_EPLD_SIZE		0x20
#define SBBC_SRAM_OFFSET	0x900000
#define SBBC_SRAM_SIZE		0x20000	/* 128KB SRAM */

#define SBBC_PCI_INT_STATUS	0x2320
#define SBBC_PCI_INT_ENABLE	0x2330
#define SBBC_PCI_ENABLE_INT_A	0x11

#define SBBC_EPLD_INTERRUPT	0x13
#define SBBC_EPLD_INTERRUPT_ON	0x01

#define SBBC_SRAM_CONS_IN		0x00000001
#define SBBC_SRAM_CONS_OUT		0x00000002
#define SBBC_SRAM_CONS_BRK		0x00000004
#define SBBC_SRAM_CONS_SPACE_IN		0x00000008
#define SBBC_SRAM_CONS_SPACE_OUT	0x00000010

#define SBBC_MAX_TAGS	32

struct sbbc_sram_tag {
	char		tag_key[8];
	uint32_t	tag_size;
	uint32_t	tag_offset;
};

struct sbbc_sram_toc {
	char			toc_magic[8];
	uint8_t			toc_reserved;
	uint8_t			toc_type;
	uint16_t		toc_version;
	uint32_t		toc_ntags;
	struct sbbc_sram_tag 	toc_tag[SBBC_MAX_TAGS];
};

/* Time of day service. */
struct sbbc_sram_tod {
	uint32_t	tod_magic;
	uint32_t	tod_version;
	uint64_t	tod_time;
	uint64_t	tod_skew;
	uint32_t	tod_reserved;
	uint32_t	tod_heartbeat;
	uint32_t	tod_timeout;
};

#define SBBC_TOD_MAGIC		0x54443100 /* "TD1" */
#define SBBC_TOD_VERSION	1

/* Console service. */
struct sbbc_sram_cons {
	uint32_t cons_magic;
	uint32_t cons_version;
	uint32_t cons_size;

	uint32_t cons_in_begin;
	uint32_t cons_in_end;
	uint32_t cons_in_rdptr;
	uint32_t cons_in_wrptr;

	uint32_t cons_out_begin;
	uint32_t cons_out_end;
	uint32_t cons_out_rdptr;
	uint32_t cons_out_wrptr;
};

#define SBBC_CONS_MAGIC		0x434f4e00 /* "CON" */
#define SBBC_CONS_VERSION	1

struct sbbc_softc {
	struct device		sc_dv;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_regs_ioh;
	bus_space_handle_t	sc_epld_ioh;
	bus_space_handle_t	sc_sram_ioh;
	caddr_t			sc_sram;
	uint32_t		sc_sram_toc;
	void *			sc_ih;

	struct sparc_bus_space_tag sc_bbt;

	struct tty		*sc_tty;
	caddr_t			sc_sram_cons;
	uint32_t		*sc_sram_solscie;
	uint32_t		*sc_sram_solscir;
	uint32_t		*sc_sram_scsolie;
	uint32_t		*sc_sram_scsolir;
	void			*sc_cons_si;
};

struct sbbc_softc *sbbc_cons_input;
struct sbbc_softc *sbbc_cons_output;

int	sbbc_match(struct device *, void *, void *);
void	sbbc_attach(struct device *, struct device *, void *);

const struct cfattach sbbc_ca = {
	sizeof(struct sbbc_softc), sbbc_match, sbbc_attach
};

struct cfdriver sbbc_cd = {
	NULL, "sbbc", DV_DULL
};

int	sbbc_intr(void *);
void	sbbc_send_intr(struct sbbc_softc *sc);

void	sbbc_attach_tod(struct sbbc_softc *, uint32_t);
int	sbbc_tod_gettime(todr_chip_handle_t, struct timeval *);
int	sbbc_tod_settime(todr_chip_handle_t, struct timeval *);

void	sbbc_attach_cons(struct sbbc_softc *, uint32_t);
void	sbbc_intr_cons(struct sbbc_softc *, uint32_t);
void	sbbc_softintr_cons(void *);
int	sbbc_cnlookc(dev_t, int *);
int	sbbc_cngetc(dev_t);
void	sbbc_cnputc(dev_t, int);
void	sbbcstart(struct tty *);
int	sbbcparam(struct tty *, struct termios *);

int
sbbc_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_SUN &&
	    (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_SUN_SBBC))
		return (1);

	return (0);
}

void
sbbc_attach(struct device *parent, struct device *self, void *aux)
{
	struct sbbc_softc *sc = (void *)self;
	struct pci_attach_args *pa = aux;
	struct sbbc_sram_toc *toc;
	bus_addr_t base;
	bus_size_t size;
	pci_intr_handle_t ih;
	int chosen, iosram;
	int i;

	/* XXX Don't byteswap. */
	sc->sc_bbt = *pa->pa_memt;
	sc->sc_bbt.sasi = ASI_PRIMARY;
	sc->sc_iot = &sc->sc_bbt;

	if (pci_mapreg_info(pa->pa_pc, pa->pa_tag, SBBC_PCI_BAR,
	    PCI_MAPREG_TYPE_MEM, &base, &size, NULL)) {
		printf(": can't find register space\n");
		return;
	}

	if (bus_space_map(sc->sc_iot, base + SBBC_REGS_OFFSET,
	    SBBC_REGS_SIZE, 0, &sc->sc_regs_ioh)) {
		printf(": can't map register space\n");
		return;
	}

	if (bus_space_map(sc->sc_iot, base + SBBC_EPLD_OFFSET,
	    SBBC_EPLD_SIZE, 0, &sc->sc_epld_ioh)) {
		printf(": can't map EPLD registers\n");
		goto unmap_regs;
	}

	if (bus_space_map(sc->sc_iot, base + SBBC_SRAM_OFFSET,
	    SBBC_SRAM_SIZE, 0, &sc->sc_sram_ioh)) {
		printf(": can't map SRAM\n");
		goto unmap_epld;
	}

	if (pci_intr_map(pa, &ih)) {
		printf(": unable to map interrupt\n");
		goto unmap_sram;
	}
	printf(": %s\n", pci_intr_string(pa->pa_pc, ih));

	sc->sc_ih = pci_intr_establish(pa->pa_pc, ih, IPL_TTY,
	    sbbc_intr, sc, sc->sc_dv.dv_xname);
	if (sc->sc_ih == NULL) {
		printf("%s: unable to establish interrupt\n", sc->sc_dv.dv_xname);
		goto unmap_sram;
	}

	bus_space_write_4(sc->sc_iot, sc->sc_regs_ioh,
	    SBBC_PCI_INT_ENABLE, SBBC_PCI_ENABLE_INT_A);

	/* Check if we are the chosen one. */
	chosen = OF_finddevice("/chosen");
	if (OF_getprop(chosen, "iosram", &iosram, sizeof(iosram)) <= 0 ||
	    PCITAG_NODE(pa->pa_tag) != iosram)
		return;

	/* SRAM TOC offset defaults to 0. */
	if (OF_getprop(chosen, "iosram-toc", &sc->sc_sram_toc,
	    sizeof(sc->sc_sram_toc)) <= 0)
		sc->sc_sram_toc = 0;

	sc->sc_sram = bus_space_vaddr(sc->sc_iot, sc->sc_sram_ioh);
	toc = (struct sbbc_sram_toc *)(sc->sc_sram + sc->sc_sram_toc);

	for (i = 0; i < toc->toc_ntags; i++) {
		if (strcmp(toc->toc_tag[i].tag_key, "SOLSCIE") == 0)
			sc->sc_sram_solscie = (uint32_t *)
			    (sc->sc_sram + toc->toc_tag[i].tag_offset);
		if (strcmp(toc->toc_tag[i].tag_key, "SOLSCIR") == 0)
			sc->sc_sram_solscir = (uint32_t *)
			    (sc->sc_sram + toc->toc_tag[i].tag_offset);
		if (strcmp(toc->toc_tag[i].tag_key, "SCSOLIE") == 0)
			sc->sc_sram_scsolie = (uint32_t *)
			    (sc->sc_sram + toc->toc_tag[i].tag_offset);
		if (strcmp(toc->toc_tag[i].tag_key, "SCSOLIR") == 0)
			sc->sc_sram_scsolir = (uint32_t *)
			    (sc->sc_sram + toc->toc_tag[i].tag_offset);
	}

	for (i = 0; i < toc->toc_ntags; i++) {
		if (strcmp(toc->toc_tag[i].tag_key, "TODDATA") == 0)
			sbbc_attach_tod(sc, toc->toc_tag[i].tag_offset);
		if (strcmp(toc->toc_tag[i].tag_key, "SOLCONS") == 0)
			sbbc_attach_cons(sc, toc->toc_tag[i].tag_offset);
	}

	return;

 unmap_sram:
	bus_space_unmap(sc->sc_iot, sc->sc_sram_ioh, SBBC_SRAM_SIZE);
 unmap_epld:
	bus_space_unmap(sc->sc_iot, sc->sc_sram_ioh, SBBC_EPLD_SIZE);
 unmap_regs:
	bus_space_unmap(sc->sc_iot, sc->sc_sram_ioh, SBBC_REGS_SIZE);
}

int
sbbc_intr(void *arg)
{
	struct sbbc_softc *sc = arg;
	uint32_t status, reason;

	status = bus_space_read_4(sc->sc_iot, sc->sc_regs_ioh,
	    SBBC_PCI_INT_STATUS);
	if (status == 0)
		return (0);

	/* Sigh, we cannot use compare and swap for non-cachable memory. */
	reason = *sc->sc_sram_scsolir;
	*sc->sc_sram_scsolir = 0;

	sbbc_intr_cons(sc, reason);

	/* Ack interrupt. */
	bus_space_write_4(sc->sc_iot, sc->sc_regs_ioh,
	    SBBC_PCI_INT_STATUS, status);
	return (1);
}

void
sbbc_send_intr(struct sbbc_softc *sc)
{
	bus_space_write_1(sc->sc_iot, sc->sc_epld_ioh,
	    SBBC_EPLD_INTERRUPT, SBBC_EPLD_INTERRUPT_ON);
}

void
sbbc_attach_tod(struct sbbc_softc *sc, uint32_t offset)
{
	struct sbbc_sram_tod *tod;
	todr_chip_handle_t handle;

	tod = (struct sbbc_sram_tod *)(sc->sc_sram + offset);
	if (tod->tod_magic != SBBC_TOD_MAGIC ||
	    tod->tod_version < SBBC_TOD_VERSION)
		return;

	handle = malloc(sizeof(struct todr_chip_handle), M_DEVBUF, M_NOWAIT);
	if (handle == NULL)
		panic("couldn't allocate todr_handle");

	handle->cookie = tod;
	handle->todr_gettime = sbbc_tod_gettime;
	handle->todr_settime = sbbc_tod_settime;
	handle->bus_cookie = NULL;
	handle->todr_setwen = NULL;
	handle->todr_quality = 0;
	todr_handle = handle;
}

int
sbbc_tod_gettime(todr_chip_handle_t handle, struct timeval *tv)
{
	struct sbbc_sram_tod *tod = handle->cookie;

	tv->tv_sec = tod->tod_time + tod->tod_skew;
	tv->tv_usec = 0;
	return (0);
}

int
sbbc_tod_settime(todr_chip_handle_t handle, struct timeval *tv)
{
	struct sbbc_sram_tod *tod = handle->cookie;

	tod->tod_skew = tv->tv_sec - tod->tod_time;
	return (0);
}

void
sbbc_attach_cons(struct sbbc_softc *sc, uint32_t offset)
{
	struct sbbc_sram_cons *cons;
	int sgcn_is_input, sgcn_is_output, node, maj;
	char buf[32];

	if (sc->sc_sram_solscie == NULL || sc->sc_sram_solscir == NULL ||
	    sc->sc_sram_scsolie == NULL || sc->sc_sram_scsolir == NULL)
		return;

	cons = (struct sbbc_sram_cons *)(sc->sc_sram + offset);
	if (cons->cons_magic != SBBC_CONS_MAGIC ||
	    cons->cons_version < SBBC_CONS_VERSION)
		return;

	sc->sc_sram_cons = sc->sc_sram + offset;
	sbbc_cons_input = sbbc_cons_output = sc;
	sgcn_is_input = sgcn_is_output = 0;

	sc->sc_cons_si = softintr_establish(IPL_TTY, sbbc_softintr_cons, sc);
	if (sc->sc_cons_si == NULL)
		panic("%s: can't establish soft interrupt",
		    sc->sc_dv.dv_xname);

	*sc->sc_sram_solscie |= SBBC_SRAM_CONS_OUT;
	*sc->sc_sram_scsolie |= SBBC_SRAM_CONS_IN | SBBC_SRAM_CONS_BRK;

	/* Take over console input. */
	prom_serengeti_set_console_input("CON_CLNT");

	/* Check for console input. */
	node = OF_instance_to_package(OF_stdin());
	if (OF_getprop(node, "name", buf, sizeof(buf)) > 0)
		sgcn_is_input = (strcmp(buf, "sgcn") == 0);

	/* Check for console output. */
	node = OF_instance_to_package(OF_stdout());
	if (OF_getprop(node, "name", buf, sizeof(buf)) > 0)
		sgcn_is_output = (strcmp(buf, "sgcn") == 0);

	if (sgcn_is_input) {
		cn_tab->cn_pollc = nullcnpollc;
		cn_tab->cn_getc = sbbc_cngetc;
	}

	if (sgcn_is_output)
		cn_tab->cn_putc = sbbc_cnputc;

	if (sgcn_is_input || sgcn_is_output) {
		/* Locate the major number. */
		for (maj = 0; maj < nchrdev; maj++)
			if (cdevsw[maj].d_open == sbbcopen)
				break;
		cn_tab->cn_dev = makedev(maj, sc->sc_dv.dv_unit);

		/* Let current output drain. */
		DELAY(2000000);

		printf("%s: console\n", sc->sc_dv.dv_xname);
	}
}

void
sbbc_intr_cons(struct sbbc_softc *sc, uint32_t reason)
{
#ifdef DDB
	if ((reason & SBBC_SRAM_CONS_BRK) && sc == sbbc_cons_input) {
		if (db_console)
			db_enter();
	}
#endif

	if ((reason & SBBC_SRAM_CONS_IN) && sc->sc_tty)
		softintr_schedule(sc->sc_cons_si);
}

void
sbbc_softintr_cons(void *arg)
{
	struct sbbc_softc *sc = arg;
	struct sbbc_sram_cons *cons = (void *)sc->sc_sram_cons;
	uint32_t rdptr = cons->cons_in_rdptr;
	struct tty *tp = sc->sc_tty;
	int c;

	while (rdptr != cons->cons_in_wrptr) {
		if (tp->t_state & TS_ISOPEN) {
			c = *(sc->sc_sram_cons + rdptr);
			(*linesw[tp->t_line].l_rint)(c, tp);
		}

		if (++rdptr == cons->cons_in_end)
			rdptr = cons->cons_in_begin;
	}

	cons->cons_in_rdptr = rdptr;
}

int
sbbc_cnlookc(dev_t dev, int *cp)
{
	struct sbbc_softc *sc = sbbc_cons_input;
	struct sbbc_sram_cons *cons = (void *)sc->sc_sram_cons;
	uint32_t rdptr = cons->cons_in_rdptr;

	if (rdptr == cons->cons_in_wrptr)
		return (0);

	*cp = *(sc->sc_sram_cons + rdptr);
	if (++rdptr == cons->cons_in_end)
		rdptr = cons->cons_in_begin;
	cons->cons_in_rdptr = rdptr;

	return (1);
}

int
sbbc_cngetc(dev_t dev)
{
	int c;

	while(!sbbc_cnlookc(dev, &c))
		;

	return (c);
}

void
sbbc_cnputc(dev_t dev, int c)
{
	struct sbbc_softc *sc = sbbc_cons_output;
	struct sbbc_sram_cons *cons = (void *)sc->sc_sram_cons;
	uint32_t wrptr = cons->cons_out_wrptr;

	*(sc->sc_sram_cons + wrptr) = c;
	if (++wrptr == cons->cons_out_end)
		wrptr = cons->cons_out_begin;
	cons->cons_out_wrptr = wrptr;

	*sc->sc_sram_solscir |= SBBC_SRAM_CONS_OUT;
	sbbc_send_intr(sc);
}

int
sbbcopen(dev_t dev, int flag, int mode, struct proc *p)
{
	struct sbbc_softc *sc;
	struct tty *tp;
	int unit = minor(dev);

	if (unit >= sbbc_cd.cd_ndevs)
		return (ENXIO);
	sc = sbbc_cd.cd_devs[unit];
	if (sc == NULL)
		return (ENXIO);

	if (sc->sc_tty)
		tp = sc->sc_tty;
	else
		tp = sc->sc_tty = ttymalloc(0);

	tp->t_oproc = sbbcstart;
	tp->t_param = sbbcparam;
	tp->t_dev = dev;
	if ((tp->t_state & TS_ISOPEN) == 0) {
		ttychars(tp);
		tp->t_iflag = TTYDEF_IFLAG;
		tp->t_oflag = TTYDEF_OFLAG;
		tp->t_cflag = TTYDEF_CFLAG;
		tp->t_lflag = TTYDEF_LFLAG;
		tp->t_ispeed = tp->t_ospeed = TTYDEF_SPEED;
		ttsetwater(tp);
	} else if ((tp->t_state & TS_XCLUDE) && suser(p))
		return (EBUSY);
	tp->t_state |= TS_CARR_ON;

	return ((*linesw[tp->t_line].l_open)(dev, tp, p));
}

int
sbbcclose(dev_t dev, int flag, int mode, struct proc *p)
{
	struct sbbc_softc *sc;
	struct tty *tp;
	int unit = minor(dev);

	if (unit >= sbbc_cd.cd_ndevs)
		return (ENXIO);
	sc = sbbc_cd.cd_devs[unit];
	if (sc == NULL)
		return (ENXIO);

	tp = sc->sc_tty;
	(*linesw[tp->t_line].l_close)(tp, flag, p);
	ttyclose(tp);
	return (0);
}

int
sbbcread(dev_t dev, struct uio *uio, int flag)
{
	struct sbbc_softc *sc;
	struct tty *tp;
	int unit = minor(dev);

	if (unit >= sbbc_cd.cd_ndevs)
		return (ENXIO);
	sc = sbbc_cd.cd_devs[unit];
	if (sc == NULL)
		return (ENXIO);

	tp = sc->sc_tty;
	return ((*linesw[tp->t_line].l_read)(tp, uio, flag));
}

int
sbbcwrite(dev_t dev, struct uio *uio, int flag)
{
	struct sbbc_softc *sc;
	struct tty *tp;
	int unit = minor(dev);

	if (unit >= sbbc_cd.cd_ndevs)
		return (ENXIO);
	sc = sbbc_cd.cd_devs[unit];
	if (sc == NULL)
		return (ENXIO);

	tp = sc->sc_tty;
	return ((*linesw[tp->t_line].l_write)(tp, uio, flag));
}

int
sbbcioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct sbbc_softc *sc;
	struct tty *tp;
	int unit = minor(dev);
	int error;

	if (unit >= sbbc_cd.cd_ndevs)
		return (ENXIO);
	sc = sbbc_cd.cd_devs[unit];
	if (sc == NULL)
		return (ENXIO);

	tp = sc->sc_tty;
	error = (*linesw[tp->t_line].l_ioctl)(tp, cmd, data, flag, p);
	if (error >= 0)
		return error;
	error = ttioctl(tp, cmd, data, flag, p);
	if (error >= 0)
		return (error);

	return (ENOTTY);
}

void
sbbcstart(struct tty *tp)
{
	int s;

	s = spltty();
	if (tp->t_state & (TS_TTSTOP | TS_BUSY)) {
		splx(s);
		return;
	}
	ttwakeupwr(tp);
	tp->t_state |= TS_BUSY;
	while (tp->t_outq.c_cc != 0)
		sbbc_cnputc(tp->t_dev, getc(&tp->t_outq));
	tp->t_state &= ~TS_BUSY;
	splx(s);
}

int
sbbcstop(struct tty *tp, int flag)
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
sbbctty(dev_t dev)
{
	struct sbbc_softc *sc;
	int unit = minor(dev);

	if (unit >= sbbc_cd.cd_ndevs)
		return (NULL);
	sc = sbbc_cd.cd_devs[unit];
	if (sc == NULL)
		return (NULL);

	return sc->sc_tty;
}

int
sbbcparam(struct tty *tp, struct termios *t)
{
	tp->t_ispeed = t->c_ispeed;
	tp->t_ospeed = t->c_ospeed;
	tp->t_cflag = t->c_cflag;
	return (0);
}
