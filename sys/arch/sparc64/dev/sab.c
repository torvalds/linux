/*	$OpenBSD: sab.c,v 1.41 2023/04/10 23:18:08 jsg Exp $	*/

/*
 * Copyright (c) 2001 Jason L. Wright (jason@thought.net)
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Effort sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F30602-01-2-0537.
 *
 */

/*
 * SAB82532 Dual UART driver
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/tty.h>
#include <sys/time.h>
#include <sys/syslog.h>

#include <machine/autoconf.h>
#include <machine/openfirm.h>
#include <machine/conf.h>
#include <machine/cpu.h>
#include <machine/psl.h>

#include <dev/cons.h>
#include <ddb/db_output.h>

#include <sparc64/dev/ebusreg.h>
#include <sparc64/dev/ebusvar.h>
#include <sparc64/dev/cons.h>
#include <sparc64/dev/sab82532reg.h>

#define	SAB_CARD(x)	((minor(x) >> 6) & 3)
#define	SAB_PORT(x)	(minor(x) & 7)
#define	SAB_DIALOUT(x)	(minor(x) & 0x10)
#define	SABTTY_RBUF_SIZE	1024	/* must be divisible by 2 */

struct sab_softc {
	struct device		sc_dv;
	struct intrhand *	sc_ih;
	bus_space_tag_t		sc_bt;
	bus_space_handle_t	sc_bh;
	struct sabtty_softc *	sc_child[SAB_NCHAN];
	u_int			sc_nchild;
	void *			sc_softintr;
	int			sc_node;
};

struct sabtty_attach_args {
	u_int sbt_portno;
};

struct sabtty_softc {
	struct device		sc_dv;
	struct sab_softc *	sc_parent;
	bus_space_tag_t		sc_bt;
	bus_space_handle_t	sc_bh;
	struct tty *		sc_tty;
	u_int			sc_portno;
	u_int8_t		sc_pvr_dtr, sc_pvr_dsr;
	u_int8_t		sc_imr0, sc_imr1;
	int			sc_openflags;
	u_char *		sc_txp;
	int			sc_txc;
	int			sc_flags;
#define SABTTYF_STOP		0x01
#define	SABTTYF_DONE		0x02
#define	SABTTYF_RINGOVERFLOW	0x04
#define	SABTTYF_CDCHG		0x08
#define	SABTTYF_CONS_IN		0x10
#define	SABTTYF_CONS_OUT	0x20
#define	SABTTYF_TXDRAIN		0x40
#define	SABTTYF_DONTDDB		0x80
	int			sc_speed;
	u_int8_t		sc_rbuf[SABTTY_RBUF_SIZE];
	u_int8_t		*sc_rend, *sc_rput, *sc_rget;
	u_int8_t		sc_polling, sc_pollrfc;
};

struct sabtty_softc *sabtty_cons_input;
struct sabtty_softc *sabtty_cons_output;

#define	SAB_READ(sc,r)		\
    bus_space_read_1((sc)->sc_bt, (sc)->sc_bh, (r))
#define	SAB_WRITE(sc,r,v)	\
    bus_space_write_1((sc)->sc_bt, (sc)->sc_bh, (r), (v))
#define	SAB_WRITE_BLOCK(sc,r,p,c)	\
    bus_space_write_region_1((sc)->sc_bt, (sc)->sc_bh, (r), (p), (c))

int sab_match(struct device *, void *, void *);
void sab_attach(struct device *, struct device *, void *);

int sab_print(void *, const char *);
int sab_intr(void *);
void sab_softintr(void *);
void sab_cnputc(dev_t, int);
int sab_cngetc(dev_t);
void sab_cnpollc(dev_t, int);

int sabtty_match(struct device *, void *, void *);
void sabtty_attach(struct device *, struct device *, void *);
int sabtty_activate(struct device *, int);

void sabtty_start(struct tty *);
int sabtty_param(struct tty *, struct termios *);
int sabtty_intr(struct sabtty_softc *, int *);
void sabtty_softintr(struct sabtty_softc *);
int sabtty_mdmctrl(struct sabtty_softc *, int, int);
int sabtty_cec_wait(struct sabtty_softc *);
int sabtty_tec_wait(struct sabtty_softc *);
void sabtty_reset(struct sabtty_softc *);
void sabtty_flush(struct sabtty_softc *);
int sabtty_speed(int);
void sabtty_console_flags(struct sabtty_softc *);
void sabtty_console_speed(struct sabtty_softc *);
void sabtty_cnpollc(struct sabtty_softc *, int);
void sabtty_shutdown(struct sabtty_softc *);
int sabttyparam(struct sabtty_softc *, struct tty *, struct termios *);

int sabttyopen(dev_t, int, int, struct proc *);
int sabttyclose(dev_t, int, int, struct proc *);
int sabttyread(dev_t, struct uio *, int);
int sabttywrite(dev_t, struct uio *, int);
int sabttyioctl(dev_t, u_long, caddr_t, int, struct proc *);
int sabttystop(struct tty *, int);
struct tty *sabttytty(dev_t);
void sabtty_cnputc(struct sabtty_softc *, int);
int sabtty_cngetc(struct sabtty_softc *);
void sabtty_abort(struct sabtty_softc *);

const struct cfattach sab_ca = {
	sizeof(struct sab_softc), sab_match, sab_attach
};

struct cfdriver sab_cd = {
	NULL, "sab", DV_DULL
};

const struct cfattach sabtty_ca = {
	sizeof(struct sabtty_softc), sabtty_match, sabtty_attach,
	NULL, sabtty_activate
};

struct cfdriver sabtty_cd = {
	NULL, "sabtty", DV_TTY
};

struct sabtty_rate {
	int baud;
	int n, m;
};

struct sabtty_rate sabtty_baudtable[] = {
	{      50,	35,     10 },
	{      75,	47,	9 },
	{     110,	32,	9 },
	{     134,	53,	8 },
	{     150,	47,	8 },
	{     200,	35,	8 },
	{     300,	47,	7 },
	{     600,	47,	6 },
	{    1200,	47,	5 },
	{    1800,	31,	5 },
	{    2400,	47,	4 },
	{    4800,	47,	3 },
	{    9600,	47,	2 },
	{   19200,	47,	1 },
	{   38400,	23,	1 },
	{   57600,	15,	1 },
	{  115200,	 7,	1 },
	{  230400,	 3,	1 },
	{  460800,	 1,	1 },
	{   76800,	11,	1 },
	{  153600,	 5,	1 },
	{  307200,	 3,	1 },
	{  614400,	 3,	0 },
	{  921600,	 0,	1 },
};

int
sab_match(struct device *parent, void *match, void *aux)
{
	struct ebus_attach_args *ea = aux;
	char *compat;

	if (strcmp(ea->ea_name, "se") == 0 ||
	    strcmp(ea->ea_name, "FJSV,se") == 0)
		return (1);
	compat = getpropstring(ea->ea_node, "compatible");
	if (compat != NULL && !strcmp(compat, "sab82532"))
		return (1);
	return (0);
}

void
sab_attach(struct device *parent, struct device *self, void *aux)
{
	struct sab_softc *sc = (struct sab_softc *)self;
	struct ebus_attach_args *ea = aux;
	u_int8_t r;
	u_int i;

	sc->sc_bt = ea->ea_memtag;
	sc->sc_node = ea->ea_node;

	/* Use prom mapping, if available. */
	if (ea->ea_nvaddrs) {
		if (bus_space_map(sc->sc_bt, ea->ea_vaddrs[0],
		    0, BUS_SPACE_MAP_PROMADDRESS, &sc->sc_bh) != 0) {
			printf(": can't map register space\n");
			return;
		}
	} else if (ebus_bus_map(sc->sc_bt, 0,
	    EBUS_PADDR_FROM_REG(&ea->ea_regs[0]), ea->ea_regs[0].size, 0, 0,
	    &sc->sc_bh) != 0) {
		printf(": can't map register space\n");
		return;
	}

	BUS_SPACE_SET_FLAGS(sc->sc_bt, sc->sc_bh, BSHDB_NO_ACCESS);

	sc->sc_ih = bus_intr_establish(sc->sc_bt, ea->ea_intrs[0],
	    IPL_TTY, 0, sab_intr, sc, self->dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't map interrupt\n");
		return;
	}

	sc->sc_softintr = softintr_establish(IPL_TTY, sab_softintr, sc);
	if (sc->sc_softintr == NULL) {
		printf(": can't get soft intr\n");
		return;
	}

	printf(": rev ");
	r = SAB_READ(sc, SAB_VSTR) & SAB_VSTR_VMASK;
	switch (r) {
	case SAB_VSTR_V_1:
		printf("1");
		break;
	case SAB_VSTR_V_2:
		printf("2");
		break;
	case SAB_VSTR_V_32:
		printf("3.2");
		break;
	default:
		printf("unknown(0x%x)", r);
		break;
	}
	printf("\n");

	/* Let current output drain */
	DELAY(100000);

	/* Set all pins, except DTR pins to be inputs */
	SAB_WRITE(sc, SAB_PCR, ~(SAB_PVR_DTR_A | SAB_PVR_DTR_B));
	/* Disable port interrupts */
	SAB_WRITE(sc, SAB_PIM, 0xff);
	SAB_WRITE(sc, SAB_PVR, SAB_PVR_DTR_A | SAB_PVR_DTR_B | SAB_PVR_MAGIC);
	SAB_WRITE(sc, SAB_IPC, SAB_IPC_ICPL);

	for (i = 0; i < SAB_NCHAN; i++) {
		struct sabtty_attach_args sta;

		sta.sbt_portno = i;
		sc->sc_child[i] = (struct sabtty_softc *)config_found_sm(self,
		    &sta, sab_print, sabtty_match);
		if (sc->sc_child[i] != NULL)
			sc->sc_nchild++;
	}
}

int
sabtty_activate(struct device *self, int act)
{
	struct sabtty_softc *sc = (struct sabtty_softc *)self;
	int ret = 0;

	switch (act) {
	case DVACT_POWERDOWN:
		if (sc->sc_flags & SABTTYF_CONS_IN)
			sabtty_shutdown(sc);
		break;
	}

	return (ret);
}

int
sab_print(void *args, const char *name)
{
	struct sabtty_attach_args *sa = args;

	if (name)
		printf("sabtty at %s", name);
	printf(" port %d", sa->sbt_portno);
	return (UNCONF);
}

int
sab_intr(void *vsc)
{
	struct sab_softc *sc = vsc;
	int r = 0, needsoft = 0;
	u_int8_t gis;

	gis = SAB_READ(sc, SAB_GIS);

	/* channel A */
	if ((gis & (SAB_GIS_ISA1 | SAB_GIS_ISA0)) && sc->sc_child[0] &&
	    sc->sc_child[0]->sc_tty)
		r |= sabtty_intr(sc->sc_child[0], &needsoft);

	/* channel B */
	if ((gis & (SAB_GIS_ISB1 | SAB_GIS_ISB0)) && sc->sc_child[1] &&
	    sc->sc_child[1]->sc_tty)
		r |= sabtty_intr(sc->sc_child[1], &needsoft);

	if (needsoft)
		softintr_schedule(sc->sc_softintr);

	return (r);
}

void
sab_softintr(void *vsc)
{
	struct sab_softc *sc = vsc;

	if (sc->sc_child[0] && sc->sc_child[0]->sc_tty)
		sabtty_softintr(sc->sc_child[0]);
	if (sc->sc_child[1] && sc->sc_child[1]->sc_tty)
		sabtty_softintr(sc->sc_child[1]);
}

int
sabtty_match(struct device *parent, void *match, void *aux)
{
	struct sabtty_attach_args *sa = aux;

	if (sa->sbt_portno < SAB_NCHAN)
		return (1);
	return (0);
}

void
sabtty_attach(struct device *parent, struct device *self, void *aux)
{
	struct sabtty_softc *sc = (struct sabtty_softc *)self;
	struct sabtty_attach_args *sa = aux;
	int r;

	sc->sc_tty = ttymalloc(0);
	sc->sc_tty->t_oproc = sabtty_start;
	sc->sc_tty->t_param = sabtty_param;

	sc->sc_parent = (struct sab_softc *)parent;
	sc->sc_bt = sc->sc_parent->sc_bt;
	sc->sc_portno = sa->sbt_portno;
	sc->sc_rend = sc->sc_rbuf + SABTTY_RBUF_SIZE;

	switch (sa->sbt_portno) {
	case 0:	/* port A */
		sc->sc_pvr_dtr = SAB_PVR_DTR_A;
		sc->sc_pvr_dsr = SAB_PVR_DSR_A;
		r = bus_space_subregion(sc->sc_bt, sc->sc_parent->sc_bh,
		    SAB_CHAN_A, SAB_CHANLEN, &sc->sc_bh);
		break;
	case 1:	/* port B */
		sc->sc_pvr_dtr = SAB_PVR_DTR_B;
		sc->sc_pvr_dsr = SAB_PVR_DSR_B;
		r = bus_space_subregion(sc->sc_bt, sc->sc_parent->sc_bh,
		    SAB_CHAN_B, SAB_CHANLEN, &sc->sc_bh);
		break;
	default:
		printf(": invalid channel: %u\n", sa->sbt_portno);
		return;
	}
	if (r != 0) {
		printf(": failed to allocate register subregion\n");
		return;
	}

	sabtty_console_flags(sc);
	sabtty_console_speed(sc);

	if (sc->sc_flags & (SABTTYF_CONS_IN | SABTTYF_CONS_OUT)) {
		struct termios t;
		char *acc;

		switch (sc->sc_flags & (SABTTYF_CONS_IN | SABTTYF_CONS_OUT)) {
		case SABTTYF_CONS_IN:
			acc = " input";
			break;
		case SABTTYF_CONS_OUT:
			acc = " output";
			break;
		case SABTTYF_CONS_IN|SABTTYF_CONS_OUT:
		default:
			acc = "";
			break;
		}

		if (sc->sc_flags & SABTTYF_CONS_OUT) {
			/* Let current output drain */
			DELAY(100000);
		}

		t.c_ispeed = 0;
		t.c_ospeed = sc->sc_speed;
		t.c_cflag = CREAD | CS8 | HUPCL;
		sc->sc_tty->t_ospeed = 0;
		sabttyparam(sc, sc->sc_tty, &t);

		if (sc->sc_flags & SABTTYF_CONS_IN) {
			sabtty_cons_input = sc;
			cn_tab->cn_pollc = sab_cnpollc;
			cn_tab->cn_getc = sab_cngetc;
			cn_tab->cn_dev = makedev(77/*XXX*/, self->dv_unit);
		}

		if (sc->sc_flags & SABTTYF_CONS_OUT) {
			sabtty_cons_output = sc;
			cn_tab->cn_putc = sab_cnputc;
			cn_tab->cn_dev = makedev(77/*XXX*/, self->dv_unit);
		}
		printf(": console%s", acc);
	} else {
		/* Not a console... */
		sabtty_reset(sc);
	}

	printf("\n");
}

int
sabtty_intr(struct sabtty_softc *sc, int *needsoftp)
{
	u_int8_t isr0, isr1;
	int i, len = 0, needsoft = 0, r = 0, clearfifo = 0;

	isr0 = SAB_READ(sc, SAB_ISR0);
	isr1 = SAB_READ(sc, SAB_ISR1);

	if (isr0 || isr1)
		r = 1;

	if (isr0 & SAB_ISR0_RPF) {
		len = 32;
		clearfifo = 1;
	}
	if (isr0 & SAB_ISR0_TCD) {
		len = (32 - 1) & SAB_READ(sc, SAB_RBCL);
		clearfifo = 1;
	}
	if (isr0 & SAB_ISR0_TIME) {
		sabtty_cec_wait(sc);
		SAB_WRITE(sc, SAB_CMDR, SAB_CMDR_RFRD);
	}
	if (isr0 & SAB_ISR0_RFO) {
		sc->sc_flags |= SABTTYF_RINGOVERFLOW;
		clearfifo = 1;
	}
	if (len != 0) {
		u_int8_t *ptr;

		ptr = sc->sc_rput;
		for (i = 0; i < len; i++) {
			*ptr++ = SAB_READ(sc, SAB_RFIFO);
			if (ptr == sc->sc_rend)
				ptr = sc->sc_rbuf;
			if (ptr == sc->sc_rget) {
				if (ptr == sc->sc_rbuf)
					ptr = sc->sc_rend;
				ptr--;
				sc->sc_flags |= SABTTYF_RINGOVERFLOW;
			}
		}
		sc->sc_rput = ptr;
		needsoft = 1;
	}

	if (clearfifo) {
		sabtty_cec_wait(sc);
		SAB_WRITE(sc, SAB_CMDR, SAB_CMDR_RMC);
	}

	if (isr0 & SAB_ISR0_CDSC) {
		sc->sc_flags |= SABTTYF_CDCHG;
		needsoft = 1;
	}

	if (isr1 & SAB_ISR1_BRKT)
		sabtty_abort(sc);

	if (isr1 & (SAB_ISR1_XPR | SAB_ISR1_ALLS)) {
		if ((SAB_READ(sc, SAB_STAR) & SAB_STAR_XFW) &&
		    (sc->sc_flags & SABTTYF_STOP) == 0) {
			if (sc->sc_txc < 32)
				len = sc->sc_txc;
			else
				len = 32;

			if (len > 0) {
				SAB_WRITE_BLOCK(sc, SAB_XFIFO, sc->sc_txp, len);
				sc->sc_txp += len; 
				sc->sc_txc -= len;

				sabtty_cec_wait(sc);
				SAB_WRITE(sc, SAB_CMDR, SAB_CMDR_XF);

				/*
				 * Prevent the false end of xmit from
				 * confusing things below.
				 */
				isr1 &= ~SAB_ISR1_ALLS;
			}
		}

		if ((sc->sc_txc == 0) || (sc->sc_flags & SABTTYF_STOP)) {
			if ((sc->sc_imr1 & SAB_IMR1_XPR) == 0) {
				sc->sc_imr1 |= SAB_IMR1_XPR;
				sc->sc_imr1 &= ~SAB_IMR1_ALLS;
				SAB_WRITE(sc, SAB_IMR1, sc->sc_imr1);
			}
		}
	}

	if ((isr1 & SAB_ISR1_ALLS) && ((sc->sc_txc == 0) ||
	    (sc->sc_flags & SABTTYF_STOP))) {
		if (sc->sc_flags & SABTTYF_TXDRAIN)
			wakeup(sc);
		sc->sc_flags &= ~SABTTYF_STOP;
		sc->sc_flags |= SABTTYF_DONE;
		sc->sc_imr1 |= SAB_IMR1_ALLS;
		SAB_WRITE(sc, SAB_IMR1, sc->sc_imr1);
		needsoft = 1;
	}

	if (needsoft)
		*needsoftp = needsoft;
	return (r);
}

void
sabtty_softintr(struct sabtty_softc *sc)
{
	struct tty *tp = sc->sc_tty;
	int s, flags;
	u_int8_t r;

	if (tp == NULL)
		return;

	if ((tp->t_state & TS_ISOPEN) == 0)
		return;

	while (sc->sc_rget != sc->sc_rput) {
		int data;
		u_int8_t stat;

		data = sc->sc_rget[0];
		stat = sc->sc_rget[1];
		sc->sc_rget += 2;
		if (stat & SAB_RSTAT_PE)
			data |= TTY_PE;
		if (stat & SAB_RSTAT_FE)
			data |= TTY_FE;
		if (sc->sc_rget == sc->sc_rend)
			sc->sc_rget = sc->sc_rbuf;

		(*linesw[tp->t_line].l_rint)(data, tp);
	}

	s = splhigh();
	flags = sc->sc_flags;
	sc->sc_flags &= ~(SABTTYF_DONE|SABTTYF_CDCHG|SABTTYF_RINGOVERFLOW);
	splx(s);

	if (flags & SABTTYF_CDCHG) {
		s = spltty();
		r = SAB_READ(sc, SAB_VSTR) & SAB_VSTR_CD;
		splx(s);

		(*linesw[tp->t_line].l_modem)(tp, r);
	}

	if (flags & SABTTYF_RINGOVERFLOW)
		log(LOG_WARNING, "%s: ring overflow\n", sc->sc_dv.dv_xname);

	if (flags & SABTTYF_DONE) {
		ndflush(&tp->t_outq, sc->sc_txp - tp->t_outq.c_cf);
		tp->t_state &= ~TS_BUSY;
		(*linesw[tp->t_line].l_start)(tp);
	}
}

int
sabttyopen(dev_t dev, int flags, int mode, struct proc *p)
{
	struct sab_softc *bc;
	struct sabtty_softc *sc;
	struct tty *tp;
	int card = SAB_CARD(dev), port = SAB_PORT(dev), s, s1;

	if (card >= sab_cd.cd_ndevs)
		return (ENXIO);
	bc = sab_cd.cd_devs[card];
	if (bc == NULL)
		return (ENXIO);

	if (port >= bc->sc_nchild)
		return (ENXIO);
	sc = bc->sc_child[port];
	if (sc == NULL)
		return (ENXIO);

	tp = sc->sc_tty;
	tp->t_dev = dev;

	if ((tp->t_state & TS_ISOPEN) == 0) {
		tp->t_state |= TS_WOPEN;

		ttychars(tp);
		tp->t_iflag = TTYDEF_IFLAG;
		tp->t_oflag = TTYDEF_OFLAG;
		tp->t_cflag = TTYDEF_CFLAG;
		if (sc->sc_openflags & TIOCFLAG_CLOCAL)
			tp->t_cflag |= CLOCAL;
		if (sc->sc_openflags & TIOCFLAG_CRTSCTS)
			tp->t_cflag |= CRTSCTS;
		if (sc->sc_openflags & TIOCFLAG_MDMBUF)
			tp->t_cflag |= MDMBUF;
		tp->t_lflag = TTYDEF_LFLAG;
		if (sc->sc_flags & (SABTTYF_CONS_IN | SABTTYF_CONS_OUT))
			tp->t_ispeed = tp->t_ospeed = sc->sc_speed;
		else
			tp->t_ispeed = tp->t_ospeed = TTYDEF_SPEED;

		sc->sc_rput = sc->sc_rget = sc->sc_rbuf;

		s = spltty();

		ttsetwater(tp);

		s1 = splhigh();
		sabtty_reset(sc);
		sabtty_param(tp, &tp->t_termios);
		sc->sc_imr0 = SAB_IMR0_PERR | SAB_IMR0_FERR | SAB_IMR0_PLLA;
		SAB_WRITE(sc, SAB_IMR0, sc->sc_imr0);
		sc->sc_imr1 = SAB_IMR1_BRK | SAB_IMR1_ALLS | SAB_IMR1_XDU |
		    SAB_IMR1_TIN | SAB_IMR1_CSC | SAB_IMR1_XMR | SAB_IMR1_XPR;
		SAB_WRITE(sc, SAB_IMR1, sc->sc_imr1);
		SAB_WRITE(sc, SAB_CCR0, SAB_READ(sc, SAB_CCR0) | SAB_CCR0_PU);
		sabtty_cec_wait(sc);
		SAB_WRITE(sc, SAB_CMDR, SAB_CMDR_XRES);
		sabtty_cec_wait(sc);
		SAB_WRITE(sc, SAB_CMDR, SAB_CMDR_RRES);
		sabtty_cec_wait(sc);
		splx(s1);

		sabtty_flush(sc);

		if ((sc->sc_openflags & TIOCFLAG_SOFTCAR) ||
		    (SAB_READ(sc, SAB_VSTR) & SAB_VSTR_CD))
			tp->t_state |= TS_CARR_ON;
		else
			tp->t_state &= ~TS_CARR_ON;
	} else if ((tp->t_state & TS_XCLUDE) &&
	    (!suser(p))) {
		return (EBUSY);
	} else {
		s = spltty();
	}

	if ((flags & O_NONBLOCK) == 0) {
		while ((tp->t_cflag & CLOCAL) == 0 &&
		    (tp->t_state & TS_CARR_ON) == 0) {
			int error;

			tp->t_state |= TS_WOPEN;
			error = ttysleep(tp, &tp->t_rawq, TTIPRI | PCATCH,
			    ttopen);
			if (error != 0) {
				splx(s);
				tp->t_state &= ~TS_WOPEN;
				return (error);
			}
		}
	}

	splx(s);

	s = (*linesw[tp->t_line].l_open)(dev, tp, p);
	if (s != 0) {
		if (tp->t_state & TS_ISOPEN)
			return (s);

		if (tp->t_cflag & HUPCL) {
			sabtty_mdmctrl(sc, 0, DMSET);
			tsleep_nsec(sc, TTIPRI, ttclos, SEC_TO_NSEC(1));
		}

		if ((sc->sc_flags & (SABTTYF_CONS_IN | SABTTYF_CONS_OUT)) == 0) {
			/* Flush and power down if we're not the console */
			sabtty_flush(sc);
			sabtty_reset(sc);
		}
	}
	return (s);
}

int
sabttyclose(dev_t dev, int flags, int mode, struct proc *p)
{
	struct sab_softc *bc = sab_cd.cd_devs[SAB_CARD(dev)];
	struct sabtty_softc *sc = bc->sc_child[SAB_PORT(dev)];
	struct tty *tp = sc->sc_tty;
	int s;

	(*linesw[tp->t_line].l_close)(tp, flags, p);

	s = spltty();

	if ((tp->t_state & TS_ISOPEN) == 0) {
		/* Wait for output drain */
		sc->sc_imr1 &= ~SAB_IMR1_ALLS;
		SAB_WRITE(sc, SAB_IMR1, sc->sc_imr1);
		sc->sc_flags |= SABTTYF_TXDRAIN;
		tsleep_nsec(sc, TTIPRI, ttclos, SEC_TO_NSEC(5));
		sc->sc_imr1 |= SAB_IMR1_ALLS;
		SAB_WRITE(sc, SAB_IMR1, sc->sc_imr1);
		sc->sc_flags &= ~SABTTYF_TXDRAIN;

		if (tp->t_cflag & HUPCL) {
			sabtty_mdmctrl(sc, 0, DMSET);
			tsleep_nsec(bc, TTIPRI, ttclos, SEC_TO_NSEC(1));
		}

		if ((sc->sc_flags & (SABTTYF_CONS_IN | SABTTYF_CONS_OUT)) == 0) {
			/* Flush and power down if we're not the console */
			sabtty_flush(sc);
			sabtty_reset(sc);
		}
	}

	ttyclose(tp);
	splx(s);

	return (0);
}

int
sabttyread(dev_t dev, struct uio *uio, int flags)
{
	struct sab_softc *bc = sab_cd.cd_devs[SAB_CARD(dev)];
	struct sabtty_softc *sc = bc->sc_child[SAB_PORT(dev)];
	struct tty *tp = sc->sc_tty;

	return ((*linesw[tp->t_line].l_read)(tp, uio, flags));
}

int
sabttywrite(dev_t dev, struct uio *uio, int flags)
{
	struct sab_softc *bc = sab_cd.cd_devs[SAB_CARD(dev)];
	struct sabtty_softc *sc = bc->sc_child[SAB_PORT(dev)];
	struct tty *tp = sc->sc_tty;

	return ((*linesw[tp->t_line].l_write)(tp, uio, flags));
}

int
sabttyioctl(dev_t dev, u_long cmd, caddr_t data, int flags, struct proc *p)
{
	struct sab_softc *bc = sab_cd.cd_devs[SAB_CARD(dev)];
	struct sabtty_softc *sc = bc->sc_child[SAB_PORT(dev)];
	struct tty *tp = sc->sc_tty;
	int error;

	error = (*linesw[tp->t_line].l_ioctl)(tp, cmd, data, flags, p);
	if (error >= 0)
		return (error);

	error = ttioctl(tp, cmd, data, flags, p);
	if (error >= 0)
		return (error);

	error = 0;

	switch (cmd) {
	case TIOCSBRK:
		SAB_WRITE(sc, SAB_DAFO,
		    SAB_READ(sc, SAB_DAFO) | SAB_DAFO_XBRK);
		break;
	case TIOCCBRK:
		SAB_WRITE(sc, SAB_DAFO,
		    SAB_READ(sc, SAB_DAFO) & ~SAB_DAFO_XBRK);
		break;
	case TIOCSDTR:
		sabtty_mdmctrl(sc, TIOCM_DTR, DMBIS);
		break;
	case TIOCCDTR:
		sabtty_mdmctrl(sc, TIOCM_DTR, DMBIC);
		break;
	case TIOCMBIS:
		sabtty_mdmctrl(sc, *((int *)data), DMBIS);
		break;
	case TIOCMBIC:
		sabtty_mdmctrl(sc, *((int *)data), DMBIC);
		break;
	case TIOCMGET:
		*((int *)data) = sabtty_mdmctrl(sc, 0, DMGET);
		break;
	case TIOCMSET:
		sabtty_mdmctrl(sc, *((int *)data), DMSET);
		break;
	case TIOCGFLAGS:
		*((int *)data) = sc->sc_openflags;
		break;
	case TIOCSFLAGS:
		if (suser(p))
			error = EPERM;
		else
			sc->sc_openflags = *((int *)data) &
			    (TIOCFLAG_SOFTCAR | TIOCFLAG_CLOCAL |
			     TIOCFLAG_CRTSCTS | TIOCFLAG_MDMBUF);
		break;
	default:
		error = ENOTTY;
	}

	return (error);
}

struct tty *
sabttytty(dev_t dev)
{
	struct sab_softc *bc = sab_cd.cd_devs[SAB_CARD(dev)];
	struct sabtty_softc *sc = bc->sc_child[SAB_PORT(dev)];

	return (sc->sc_tty);
}

int
sabttystop(struct tty *tp, int flags)
{
	struct sab_softc *bc = sab_cd.cd_devs[SAB_CARD(tp->t_dev)];
	struct sabtty_softc *sc = bc->sc_child[SAB_PORT(tp->t_dev)];
	int s;

	s = spltty();
	if (tp->t_state & TS_BUSY) {
		if ((tp->t_state & TS_TTSTOP) == 0)
			tp->t_state |= TS_FLUSH;
		sc->sc_flags |= SABTTYF_STOP;
		sc->sc_imr1 &= ~SAB_IMR1_ALLS;
		SAB_WRITE(sc, SAB_IMR1, sc->sc_imr1);
	}
	splx(s);
	return (0);
}

int
sabtty_mdmctrl(struct sabtty_softc *sc, int bits, int how)
{
	u_int8_t r;
	int s;

	s = spltty();
	switch (how) {
	case DMGET:
		bits = 0;
		if (SAB_READ(sc, SAB_STAR) & SAB_STAR_CTS)
			bits |= TIOCM_CTS;
		if ((SAB_READ(sc, SAB_VSTR) & SAB_VSTR_CD) == 0)
			bits |= TIOCM_CD;

		r = SAB_READ(sc, SAB_PVR);
		if ((r & sc->sc_pvr_dtr) == 0)
			bits |= TIOCM_DTR;
		if ((r & sc->sc_pvr_dsr) == 0)
			bits |= TIOCM_DSR;

		r = SAB_READ(sc, SAB_MODE);
		if ((r & (SAB_MODE_RTS|SAB_MODE_FRTS)) == SAB_MODE_RTS)
			bits |= TIOCM_RTS;
		break;
	case DMSET:
		r = SAB_READ(sc, SAB_MODE);
		if (bits & TIOCM_RTS) {
			r &= ~SAB_MODE_FRTS;
			r |= SAB_MODE_RTS;
		} else
			r |= SAB_MODE_FRTS | SAB_MODE_RTS;
		SAB_WRITE(sc, SAB_MODE, r);

		r = SAB_READ(sc, SAB_PVR);
		if (bits & TIOCM_DTR)
			r &= ~sc->sc_pvr_dtr;
		else
			r |= sc->sc_pvr_dtr;
		SAB_WRITE(sc, SAB_PVR, r);
		break;
	case DMBIS:
		if (bits & TIOCM_RTS) {
			r = SAB_READ(sc, SAB_MODE);
			r &= ~SAB_MODE_FRTS;
			r |= SAB_MODE_RTS;
			SAB_WRITE(sc, SAB_MODE, r);
		}
		if (bits & TIOCM_DTR) {
			r = SAB_READ(sc, SAB_PVR);
			r &= ~sc->sc_pvr_dtr;
			SAB_WRITE(sc, SAB_PVR, r);
		}
		break;
	case DMBIC:
		if (bits & TIOCM_RTS) {
			r = SAB_READ(sc, SAB_MODE);
			r |= SAB_MODE_FRTS | SAB_MODE_RTS;
			SAB_WRITE(sc, SAB_MODE, r);
		}
		if (bits & TIOCM_DTR) {
			r = SAB_READ(sc, SAB_PVR);
			r |= sc->sc_pvr_dtr;
			SAB_WRITE(sc, SAB_PVR, r);
		}
		break;
	}
	splx(s);
	return (bits);
}

int
sabttyparam(struct sabtty_softc *sc, struct tty *tp, struct termios *t)
{
	int s, ospeed;
	tcflag_t cflag;
	u_int8_t dafo, r;

	ospeed = sabtty_speed(t->c_ospeed);
	if (ospeed < 0 || (t->c_ispeed && t->c_ispeed != t->c_ospeed))
		return (EINVAL);

	s = spltty();

	/* hang up line if ospeed is zero, otherwise raise dtr */
	sabtty_mdmctrl(sc, TIOCM_DTR,
	    (t->c_ospeed == 0) ? DMBIC : DMBIS);

	dafo = SAB_READ(sc, SAB_DAFO);

	cflag = t->c_cflag;

	if (sc->sc_flags & (SABTTYF_CONS_IN | SABTTYF_CONS_OUT)) {
		cflag |= CLOCAL;
		cflag &= ~HUPCL;
	}

	if (cflag & CSTOPB)
		dafo |= SAB_DAFO_STOP;
	else
		dafo &= ~SAB_DAFO_STOP;

	dafo &= ~SAB_DAFO_CHL_CSIZE;
	switch (cflag & CSIZE) {
	case CS5:
		dafo |= SAB_DAFO_CHL_CS5;
		break;
	case CS6:
		dafo |= SAB_DAFO_CHL_CS6;
		break;
	case CS7:
		dafo |= SAB_DAFO_CHL_CS7;
		break;
	default:
		dafo |= SAB_DAFO_CHL_CS8;
		break;
	}

	dafo &= ~SAB_DAFO_PARMASK;
	if (cflag & PARENB) {
		if (cflag & PARODD)
			dafo |= SAB_DAFO_PAR_ODD;
		else
			dafo |= SAB_DAFO_PAR_EVEN;
	} else
		dafo |= SAB_DAFO_PAR_NONE;

	SAB_WRITE(sc, SAB_DAFO, dafo);

	if (ospeed != 0) {
		SAB_WRITE(sc, SAB_BGR, ospeed & 0xff);
		r = SAB_READ(sc, SAB_CCR2);
		r &= ~(SAB_CCR2_BR9 | SAB_CCR2_BR8);
		r |= (ospeed >> 2) & (SAB_CCR2_BR9 | SAB_CCR2_BR8);
		SAB_WRITE(sc, SAB_CCR2, r);
	}

	r = SAB_READ(sc, SAB_MODE);
	r |= SAB_MODE_RAC;
	if (cflag & CRTSCTS) {
		r &= ~(SAB_MODE_RTS | SAB_MODE_FCTS);
		r |= SAB_MODE_FRTS;
		sc->sc_imr1 &= ~SAB_IMR1_CSC;
	} else {
		r |= SAB_MODE_RTS | SAB_MODE_FCTS;
		r &= ~SAB_MODE_FRTS;
		sc->sc_imr1 |= SAB_IMR1_CSC;
	}
	SAB_WRITE(sc, SAB_MODE, r);
	SAB_WRITE(sc, SAB_IMR1, sc->sc_imr1);

	tp->t_cflag = cflag;

	splx(s);
	return (0);
}

int
sabtty_param(struct tty *tp, struct termios *t)
{
	struct sab_softc *bc = sab_cd.cd_devs[SAB_CARD(tp->t_dev)];
	struct sabtty_softc *sc = bc->sc_child[SAB_PORT(tp->t_dev)];

	return (sabttyparam(sc, tp, t));
}

void
sabtty_start(struct tty *tp)
{
	struct sab_softc *bc = sab_cd.cd_devs[SAB_CARD(tp->t_dev)];
	struct sabtty_softc *sc = bc->sc_child[SAB_PORT(tp->t_dev)];
	int s;

	s = spltty();
	if ((tp->t_state & (TS_TTSTOP | TS_TIMEOUT | TS_BUSY)) == 0) {
		ttwakeupwr(tp);
		if (tp->t_outq.c_cc) {
			sc->sc_txc = ndqb(&tp->t_outq, 0);
			sc->sc_txp = tp->t_outq.c_cf;
			tp->t_state |= TS_BUSY;
			sc->sc_imr1 &= ~(SAB_ISR1_XPR | SAB_ISR1_ALLS);
			SAB_WRITE(sc, SAB_IMR1, sc->sc_imr1);
		}
	}
	splx(s);
}

int
sabtty_cec_wait(struct sabtty_softc *sc)
{
	int i = 50000;

	for (;;) {
		if ((SAB_READ(sc, SAB_STAR) & SAB_STAR_CEC) == 0)
			return (0);
		if (--i == 0)
			return (1);
		DELAY(1);
	}
}

int
sabtty_tec_wait(struct sabtty_softc *sc)
{
	int i = 200000;

	for (;;) {
		if ((SAB_READ(sc, SAB_STAR) & SAB_STAR_TEC) == 0)
			return (0);
		if (--i == 0)
			return (1);
		DELAY(1);
	}
}

void
sabtty_reset(struct sabtty_softc *sc)
{
	/* power down */
	SAB_WRITE(sc, SAB_CCR0, 0);

	/* set basic configuration */
	SAB_WRITE(sc, SAB_CCR0,
	    SAB_CCR0_MCE | SAB_CCR0_SC_NRZ | SAB_CCR0_SM_ASYNC);
	SAB_WRITE(sc, SAB_CCR1, SAB_CCR1_ODS | SAB_CCR1_BCR | SAB_CCR1_CM_7);
	SAB_WRITE(sc, SAB_CCR2, SAB_CCR2_BDF | SAB_CCR2_SSEL | SAB_CCR2_TOE);
	SAB_WRITE(sc, SAB_CCR3, 0);
	SAB_WRITE(sc, SAB_CCR4, SAB_CCR4_MCK4 | SAB_CCR4_EBRG);
	SAB_WRITE(sc, SAB_MODE, SAB_MODE_RTS | SAB_MODE_FCTS | SAB_MODE_RAC);
	SAB_WRITE(sc, SAB_RFC,
	    SAB_RFC_DPS | SAB_RFC_RFDF | SAB_RFC_RFTH_32CHAR);

	/* clear interrupts */
	sc->sc_imr0 = sc->sc_imr1 = 0xff;
	SAB_WRITE(sc, SAB_IMR0, sc->sc_imr0);
	SAB_WRITE(sc, SAB_IMR1, sc->sc_imr1);
	SAB_READ(sc, SAB_ISR0);
	SAB_READ(sc, SAB_ISR1);
}

void
sabtty_flush(struct sabtty_softc *sc)
{
	/* clear rx fifo */
	sabtty_cec_wait(sc);
	SAB_WRITE(sc, SAB_CMDR, SAB_CMDR_RRES);

	/* clear tx fifo */
	sabtty_cec_wait(sc);
	SAB_WRITE(sc, SAB_CMDR, SAB_CMDR_XRES);
}

int
sabtty_speed(int rate)
{
	int i, len, r;

	if (rate == 0)
		return (0);
	len = sizeof(sabtty_baudtable)/sizeof(sabtty_baudtable[0]);
	for (i = 0; i < len; i++) {
		if (rate == sabtty_baudtable[i].baud) {
			r = sabtty_baudtable[i].n |
			    (sabtty_baudtable[i].m << 6);
			return (r);
		}
	}
	return (-1);
}

void
sabtty_cnputc(struct sabtty_softc *sc, int c)
{
	sabtty_tec_wait(sc);
	SAB_WRITE(sc, SAB_TIC, c);
	sabtty_tec_wait(sc);
}

int
sabtty_cngetc(struct sabtty_softc *sc)
{
	u_int8_t r, len, ipc;

	ipc = SAB_READ(sc, SAB_IPC);
	SAB_WRITE(sc, SAB_IPC, ipc | SAB_IPC_VIS);

again:
	do {
		r = SAB_READ(sc, SAB_STAR);
	} while ((r & SAB_STAR_RFNE) == 0);

	/*
	 * Ok, at least one byte in RFIFO, ask for permission to access RFIFO
	 * (I hate this chip... hate hate hate).
	 */
	sabtty_cec_wait(sc);
	SAB_WRITE(sc, SAB_CMDR, SAB_CMDR_RFRD);

	/* Wait for RFIFO to come ready */
	do {
		r = SAB_READ(sc, SAB_ISR0);
	} while ((r & SAB_ISR0_TCD) == 0);

	len = SAB_READ(sc, SAB_RBCL) & (32 - 1);
	if (len == 0)
		goto again;	/* Shouldn't happen... */

	r = SAB_READ(sc, SAB_RFIFO);

	/*
	 * Blow away everything left in the FIFO...
	 */
	sabtty_cec_wait(sc);
	SAB_WRITE(sc, SAB_CMDR, SAB_CMDR_RMC);
	SAB_WRITE(sc, SAB_IPC, ipc);
	return (r);
}

void
sabtty_cnpollc(struct sabtty_softc *sc, int on)
{
	u_int8_t r;

	if (on) {
		if (sc->sc_polling)
			return;
		SAB_WRITE(sc, SAB_IPC, SAB_READ(sc, SAB_IPC) | SAB_IPC_VIS);
		r = sc->sc_pollrfc = SAB_READ(sc, SAB_RFC);
		r &= ~(SAB_RFC_RFDF);
		SAB_WRITE(sc, SAB_RFC, r);
		sabtty_cec_wait(sc);
		SAB_WRITE(sc, SAB_CMDR, SAB_CMDR_RRES);
		sc->sc_polling = 1;
	} else {
		if (!sc->sc_polling)
			return;
		SAB_WRITE(sc, SAB_IPC, SAB_READ(sc, SAB_IPC) & ~SAB_IPC_VIS);
		SAB_WRITE(sc, SAB_RFC, sc->sc_pollrfc);
		sabtty_cec_wait(sc);
		SAB_WRITE(sc, SAB_CMDR, SAB_CMDR_RRES);
		sc->sc_polling = 0;
	}
}

void
sab_cnputc(dev_t dev, int c)
{
	struct sabtty_softc *sc = sabtty_cons_output;

	if (sc == NULL)
		return;
	sabtty_cnputc(sc, c);
}

void
sab_cnpollc(dev_t dev, int on)
{
	struct sabtty_softc *sc = sabtty_cons_input;

	sabtty_cnpollc(sc, on);
}

int
sab_cngetc(dev_t dev)
{
	struct sabtty_softc *sc = sabtty_cons_input;

	if (sc == NULL)
		return (-1);
	return (sabtty_cngetc(sc));
}

void
sabtty_console_flags(struct sabtty_softc *sc)
{
	int node, channel, options, cookie;
	char buf[255];

	node = sc->sc_parent->sc_node;
	channel = sc->sc_portno;

	options = OF_finddevice("/options");

	/* Default to channel 0 if there are no explicit prom args */
	cookie = 0;

	if (node == OF_instance_to_package(OF_stdin())) {
		if (OF_getprop(options, "input-device", buf,
		    sizeof(buf)) != -1) {
			if (strncmp("ttyb", buf, strlen("ttyb")) == 0)
				cookie = 1;
		}

		if (channel == cookie)
			sc->sc_flags |= SABTTYF_CONS_IN;
	}

	/* Default to same channel if there are no explicit prom args */

	if (node == OF_instance_to_package(OF_stdout())) {
		if (OF_getprop(options, "output-device", buf,
		    sizeof(buf)) != -1) {
			if (strncmp("ttyb", buf, strlen("ttyb")) == 0)
				cookie = 1;
		}

		if (channel == cookie)
			sc->sc_flags |= SABTTYF_CONS_OUT;
	}
}

void
sabtty_console_speed(struct sabtty_softc *sc)
{
	char *name;
	int node, channel, options;

	node = sc->sc_parent->sc_node;
	channel = sc->sc_portno;

	if (getpropint(node, "ssp-console", -1) == channel) {
		sc->sc_speed = getpropspeed(node, "ssp-console-modes");
		return;
	}
	if (getpropint(node, "ssp-control", -1) == channel) {
		sc->sc_speed = getpropspeed(node, "ssp-control-modes");
		return;
	}

	options = OF_finddevice("/options");
	name = sc->sc_portno ? "ttyb-mode" : "ttya-mode";
	sc->sc_speed = getpropspeed(options, name);
}

void
sabtty_abort(struct sabtty_softc *sc)
{

	if (sc->sc_flags & SABTTYF_CONS_IN) {
#ifdef DDB
		extern int db_active, db_console;

		if (db_console == 0)
			return;
		if (db_active == 0)
			db_enter();
		else
			callrom();
#else
		callrom();
#endif
	}
}

void
sabtty_shutdown(struct sabtty_softc *sc)
{
	/* Have to put the chip back into single char mode */
	sc->sc_flags |= SABTTYF_DONTDDB;
	SAB_WRITE(sc, SAB_RFC, SAB_READ(sc, SAB_RFC) & ~SAB_RFC_RFDF);
	sabtty_cec_wait(sc);
	SAB_WRITE(sc, SAB_CMDR, SAB_CMDR_RRES);
	sabtty_cec_wait(sc);
	SAB_WRITE(sc, SAB_IPC, SAB_READ(sc, SAB_IPC) | SAB_IPC_VIS);
}
