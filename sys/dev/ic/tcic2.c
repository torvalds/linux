/*	$OpenBSD: tcic2.c,v 1.14 2021/03/07 06:21:38 jsg Exp $	*/
/*	$NetBSD: tcic2.c,v 1.3 2000/01/13 09:38:17 joda Exp $	*/

#undef	TCICDEBUG

/*
 * Copyright (c) 1998, 1999 Christoph Badura.  All rights reserved.
 * Copyright (c) 1997 Marc Horowitz.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Marc Horowitz.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/extent.h>
#include <sys/malloc.h>
#include <sys/kthread.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/pcmcia/pcmciareg.h>
#include <dev/pcmcia/pcmciavar.h>

#include <dev/ic/tcic2reg.h>
#include <dev/ic/tcic2var.h>

#ifdef TCICDEBUG
int	tcic_debug = 1;
#define	DPRINTF(arg) if (tcic_debug) printf arg;
#else
#define	DPRINTF(arg)
#endif

/*
 * Individual drivers will allocate their own memory and io regions. Memory
 * regions must be a multiple of 4k, aligned on a 4k boundary.
 */

#define	TCIC_MEM_ALIGN	TCIC_MEM_PAGESIZE

void	tcic_attach_socket(struct tcic_handle *);
void	tcic_init_socket(struct tcic_handle *);

int	tcic_submatch(struct device *, void *, void *);
int	tcic_print(void *arg, const char *pnp);
int	tcic_intr_socket(struct tcic_handle *);

void	tcic_attach_card(struct tcic_handle *);
void	tcic_detach_card(struct tcic_handle *, int);
void	tcic_deactivate_card(struct tcic_handle *);

void	tcic_chip_do_mem_map(struct tcic_handle *, int);
void	tcic_chip_do_io_map(struct tcic_handle *, int);

void	tcic_create_event_thread(void *);
void	tcic_event_thread(void *);

void	tcic_queue_event(struct tcic_handle *, int);

struct cfdriver tcic_cd = {
	NULL, "tcic", DV_DULL
};

/* Map between irq numbers and internal representation */
#if 1
int tcic_irqmap[] =
    { 0, 0, 0, 3, 4, 5, 6, 7, 0, 0, 10, 1, 0, 0, 14, 0 };
int tcic_valid_irqs = 0x4cf8;
#else
int tcic_irqmap[] =	/* irqs 9 and 6 switched, some ISA cards */
    { 0, 0, 0, 3, 4, 5, 0, 7, 0, 6, 10, 1, 0, 0, 14, 0 };
int tcic_valid_irqs = 0x4eb8;
#endif

int tcic_mem_speed = 250;	/* memory access time in nanoseconds */
int tcic_io_speed = 165;	/* io access time in nanoseconds */

/*
 * Check various reserved and otherwise in their value restricted bits.
 */
int
tcic_check_reserved_bits(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	int val, auxreg;

	DPRINTF(("tcic: chkrsvd 1\n"));
	/* R_ADDR bit 30:28 have a restricted range. */
	val = (bus_space_read_2(iot, ioh, TCIC_R_ADDR2) & TCIC_SS_MASK)
	    >> TCIC_SS_SHIFT;
	if (val > 1)
		return 0;

	DPRINTF(("tcic: chkrsvd 2\n"));
	/* R_SCTRL bits 6,2,1 are reserved. */
	val = bus_space_read_1(iot, ioh, TCIC_R_SCTRL);
	if (val & TCIC_SCTRL_RSVD)
		return 0;

	DPRINTF(("tcic: chkrsvd 3\n"));
	/* R_ICSR bit 2 must be same as bit 3. */
	val = bus_space_read_1(iot, ioh, TCIC_R_ICSR);
	if (((val >> 1) & 1) != ((val >> 2) & 1))
		return 0;

	DPRINTF(("tcic: chkrsvd 4\n"));
	/* R_IENA bits 7,2 are reserved. */
	val = bus_space_read_1(iot, ioh, TCIC_R_IENA);
	if (val & TCIC_IENA_RSVD)
		return 0;

	DPRINTF(("tcic: chkrsvd 5\n"));
	/* Some aux registers have reserved bits. */
	/* Which are we looking at? */
	auxreg = bus_space_read_1(iot, ioh, TCIC_R_MODE)
	    & TCIC_AR_MASK;
	val = bus_space_read_2(iot, ioh, TCIC_R_AUX);
	DPRINTF(("tcic: auxreg 0x%02x val 0x%04x\n", auxreg, val));
	switch (auxreg) {
	case TCIC_AR_SYSCFG:
		if (INVALID_AR_SYSCFG(val))
			return 0;
		break;
	case TCIC_AR_ILOCK:
		if (INVALID_AR_ILOCK(val))
			return 0;
		break;
	case TCIC_AR_TEST:
		if (INVALID_AR_TEST(val))
			return 0;
		break;
	}

	DPRINTF(("tcic: chkrsvd 6\n"));
	/* XXX fails if pcmcia bios is enabled. */
	/* Various bits set or not depending if in RESET mode. */
	val = bus_space_read_1(iot, ioh, TCIC_R_SCTRL);
	if (val & TCIC_SCTRL_RESET) {
		DPRINTF(("tcic: chkrsvd 7\n"));
		/* Address bits must be 0 */
		val = bus_space_read_2(iot, ioh, TCIC_R_ADDR);
		if (val != 0)
			return 0;
		val = bus_space_read_2(iot, ioh, TCIC_R_ADDR2);
		if (val != 0)
			return 0;
		DPRINTF(("tcic: chkrsvd 8\n"));
		/* EDC bits must be 0 */
		val = bus_space_read_2(iot, ioh, TCIC_R_EDC);
		if (val != 0)
			return 0;
		/* We're OK, so take it out of reset. XXX -chb */
		bus_space_write_1(iot, ioh, TCIC_R_SCTRL, 0);
	}
	else {	/* not in RESET mode */
		int omode;
		int val1, val2;
		DPRINTF(("tcic: chkrsvd 9\n"));
		/* Programming timers must have expired. */
		val = bus_space_read_1(iot, ioh, TCIC_R_SSTAT);
		if ((val & (TCIC_SSTAT_6US|TCIC_SSTAT_10US|TCIC_SSTAT_PROGTIME))
		    != (TCIC_SSTAT_6US|TCIC_SSTAT_10US|TCIC_SSTAT_PROGTIME))
			return 0;
		DPRINTF(("tcic: chkrsvd 10\n"));
		/*
		 * EDC bits should change on read from data space
		 * as long as either EDC or the data are nonzero.
		 */
		 if ((bus_space_read_2(iot, ioh, TCIC_R_ADDR2)
		     & TCIC_ADDR2_INDREG) != 0) {
			val1 = bus_space_read_2(iot, ioh, TCIC_R_EDC);
			val2 = bus_space_read_2(iot, ioh, TCIC_R_DATA);
			if (val1 | val2) {
				val1 = bus_space_read_2(iot, ioh, TCIC_R_EDC);
				if (val1 == val2)
					return 0;
			}
		}
		DPRINTF(("tcic: chkrsvd 11\n"));
		/* XXX what does this check? -chb */
		omode = bus_space_read_1(iot, ioh, TCIC_R_MODE);
		val1 = omode ^ TCIC_AR_MASK;
		bus_space_write_1(iot, ioh, TCIC_R_MODE, val1);
		val2 = bus_space_read_1(iot, ioh, TCIC_R_MODE);
		bus_space_write_1(iot, ioh, TCIC_R_MODE, omode);
		if ( val1 != val2)
			return 0;
	}
	/* All tests passed */
	return 1;
}

/*
 * Read chip ID from AR_ILOCK in test mode.
 */
int
tcic_chipid(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	unsigned id, otest;

	otest = tcic_read_aux_2(iot, ioh, TCIC_AR_TEST);
	tcic_write_aux_2(iot, ioh, TCIC_AR_TEST, TCIC_TEST_DIAG);
	id = tcic_read_aux_2(iot, ioh, TCIC_AR_ILOCK);
	tcic_write_aux_2(iot, ioh, TCIC_AR_TEST, otest);
	id &= TCIC_ILOCKTEST_ID_MASK;
	id >>= TCIC_ILOCKTEST_ID_SHFT;

	/* clear up IRQs inside tcic. XXX -chb */
	while (bus_space_read_1(iot, ioh, TCIC_R_ICSR))
		bus_space_write_1(iot, ioh, TCIC_R_ICSR, TCIC_ICSR_JAM);

	return id;
}
/*
 * Indicate whether the driver can handle the chip.
 */
int
tcic_chipid_known(int id)
{
	/* XXX only know how to handle DB86082 -chb */
	switch (id) {
	case TCIC_CHIPID_DB86082_1:
	case TCIC_CHIPID_DB86082A:
	case TCIC_CHIPID_DB86082B_ES:
	case TCIC_CHIPID_DB86082B:
	case TCIC_CHIPID_DB86084_1:
	case TCIC_CHIPID_DB86084A:
	case TCIC_CHIPID_DB86184_1:
	case TCIC_CHIPID_DB86072_1_ES:
	case TCIC_CHIPID_DB86072_1:
		return 1;
	}

	return 0;
}

char *
tcic_chipid_to_string(int id)
{
	switch (id) {
	case TCIC_CHIPID_DB86082_1:
		return ("Databook DB86082");
	case TCIC_CHIPID_DB86082A:
		return ("Databook DB86082A");
	case TCIC_CHIPID_DB86082B_ES:
		return ("Databook DB86082B-es");
	case TCIC_CHIPID_DB86082B:
		return ("Databook DB86082B");
	case TCIC_CHIPID_DB86084_1:
		return ("Databook DB86084");
	case TCIC_CHIPID_DB86084A:
		return ("Databook DB86084A");
	case TCIC_CHIPID_DB86184_1:
		return ("Databook DB86184");
	case TCIC_CHIPID_DB86072_1_ES:
		return ("Databook DB86072-es");
	case TCIC_CHIPID_DB86072_1:
		return ("Databook DB86072");
	}

	return ("Unknown controller");
}
/*
 * Return bitmask of IRQs that the chip can handle.
 * XXX should be table driven.
 */
int
tcic_validirqs(int chipid)
{
	switch (chipid) {
	case TCIC_CHIPID_DB86082_1:
	case TCIC_CHIPID_DB86082A:
	case TCIC_CHIPID_DB86082B_ES:
	case TCIC_CHIPID_DB86082B:
	case TCIC_CHIPID_DB86084_1:
	case TCIC_CHIPID_DB86084A:
	case TCIC_CHIPID_DB86184_1:
	case TCIC_CHIPID_DB86072_1_ES:
	case TCIC_CHIPID_DB86072_1:
		return tcic_valid_irqs;
	}
	return 0;
}

void
tcic_attach(struct tcic_softc *sc)
{
	int i, reg;

	/* set more chipset-dependent parameters in the softc. */
	switch (sc->chipid) {
	case TCIC_CHIPID_DB86084_1:
	case TCIC_CHIPID_DB86084A:
	case TCIC_CHIPID_DB86184_1:
		sc->pwrena = TCIC_PWR_ENA;
		break;
	default:
		sc->pwrena = 0;
		break;
	}

	/* set up global config registers */
	reg = TCIC_WAIT_SYNC | TCIC_WAIT_CCLK | TCIC_WAIT_RISING;
	reg |= (tcic_ns2wscnt(250) & TCIC_WAIT_COUNT_MASK);
	tcic_write_aux_1(sc->iot, sc->ioh, TCIC_AR_WCTL, TCIC_R_WCTL_WAIT, reg);
	reg = TCIC_SYSCFG_MPSEL_RI | TCIC_SYSCFG_MCSFULL;
	tcic_write_aux_2(sc->iot, sc->ioh, TCIC_AR_SYSCFG, reg);
	reg = tcic_read_aux_2(sc->iot, sc->ioh, TCIC_AR_ILOCK);
	reg |= TCIC_ILOCK_HOLD_CCLK;
	tcic_write_aux_2(sc->iot, sc->ioh, TCIC_AR_ILOCK, reg);

	/* the TCIC has two sockets */
	/* XXX should i check for actual presence of sockets? -chb */
	for (i = 0; i < TCIC_NSLOTS; i++) {
		sc->handle[i].sc = sc;
		sc->handle[i].sock = i;
		sc->handle[i].flags = TCIC_FLAG_SOCKETP;
		sc->handle[i].memwins
		    = sc->chipid == TCIC_CHIPID_DB86082_1 ?  4 : 5;
	}

	/* establish the interrupt */
	reg = tcic_read_1(&sc->handle[0], TCIC_R_IENA);
	tcic_write_1(&sc->handle[0], TCIC_R_IENA,
	    (reg & ~TCIC_IENA_CFG_MASK) | TCIC_IENA_CFG_HIGH);
	reg = tcic_read_aux_2(sc->iot, sc->ioh, TCIC_AR_SYSCFG);
	tcic_write_aux_2(sc->iot, sc->ioh, TCIC_AR_SYSCFG,
	    (reg & ~TCIC_SYSCFG_IRQ_MASK) | tcic_irqmap[sc->irq]);

	/* XXX block interrupts? */

	for (i = 0; i < TCIC_NSLOTS; i++) {
		/* XXX make more clear what happens here -chb */
		tcic_sel_sock(&sc->handle[i]);
		tcic_write_ind_2(&sc->handle[i], TCIC_IR_SCF1_N(i), 0);
		tcic_write_ind_2(&sc->handle[i], TCIC_IR_SCF2_N(i), 
		    (TCIC_SCF2_MCD|TCIC_SCF2_MWP|TCIC_SCF2_MRDY
#if 1		/* XXX explain byte routing issue */
		    |TCIC_SCF2_MLBAT2|TCIC_SCF2_MLBAT1|TCIC_SCF2_IDBR));
#else
		    |TCIC_SCF2_MLBAT2|TCIC_SCF2_MLBAT1));
#endif
		tcic_write_1(&sc->handle[i], TCIC_R_MODE, 0);
		reg = tcic_read_aux_2(sc->iot, sc->ioh, TCIC_AR_SYSCFG);
		reg &= ~TCIC_SYSCFG_AUTOBUSY;
		tcic_write_aux_2(sc->iot, sc->ioh, TCIC_AR_SYSCFG, reg);
		SIMPLEQ_INIT(&sc->handle[i].events);
	}

	if ((sc->handle[0].flags & TCIC_FLAG_SOCKETP) ||
	    (sc->handle[1].flags & TCIC_FLAG_SOCKETP)) {
		printf("%s: %s has ", sc->dev.dv_xname,
		       tcic_chipid_to_string(sc->chipid));

		if ((sc->handle[0].flags & TCIC_FLAG_SOCKETP) &&
		    (sc->handle[1].flags & TCIC_FLAG_SOCKETP))
			printf("sockets A and B\n");
		else if (sc->handle[0].flags & TCIC_FLAG_SOCKETP)
			printf("socket A only\n");
		else
			printf("socket B only\n");

	}
}

void
tcic_attach_sockets(struct tcic_softc *sc)
{
	int i;

	for (i = 0; i < TCIC_NSLOTS; i++)
		if (sc->handle[i].flags & TCIC_FLAG_SOCKETP)
			tcic_attach_socket(&sc->handle[i]);
}

void
tcic_attach_socket(struct tcic_handle *h)
{
	struct pcmciabus_attach_args paa;

	/* initialize the rest of the handle */

	h->shutdown = 0;
	h->memalloc = 0;
	h->ioalloc = 0;
	h->ih_irq = 0;

	/* now, config one pcmcia device per socket */

	paa.paa_busname = "pcmcia";
	paa.pct = (pcmcia_chipset_tag_t) h->sc->pct;
	paa.pch = (pcmcia_chipset_handle_t) h;
	paa.iobase = h->sc->iobase;
	paa.iosize = h->sc->iosize;

	h->pcmcia = config_found_sm(&h->sc->dev, &paa, tcic_print,
	    tcic_submatch);

	/* if there's actually a pcmcia device attached, initialize the slot */

	if (h->pcmcia)
		tcic_init_socket(h);
	else
		h->flags &= ~TCIC_FLAG_SOCKETP;
}

void
tcic_create_event_thread(void *arg)
{
	struct tcic_handle *h = arg;
	char name[MAXCOMLEN+1];
	const char *cs;

	switch (h->sock) {
	case 0:
		cs = "0";
		break;
	case 1:
		cs = "1";
		break;
	default:
		panic("tcic_create_event_thread: unknown tcic socket");
	}

	snprintf(name, sizeof name, "%s,%s", h->sc->dev.dv_xname, cs);
	if (kthread_create(tcic_event_thread, h, &h->event_thread, name)) {
		printf("%s: unable to create event thread for sock 0x%02x\n",
		    h->sc->dev.dv_xname, h->sock);
		panic("tcic_create_event_thread");
	}
}

void
tcic_event_thread(void *arg)
{
	struct tcic_handle *h = arg;
	struct tcic_event *pe;
	int s;

	while (h->shutdown == 0) {
		s = splhigh();
		if ((pe = SIMPLEQ_FIRST(&h->events)) == NULL) {
			splx(s);
			tsleep_nsec(&h->events, PWAIT, "tcicev", INFSLP);
			continue;
		}
		SIMPLEQ_REMOVE_HEAD(&h->events, pe_q);
		splx(s);

		switch (pe->pe_type) {
		case TCIC_EVENT_INSERTION:
			DPRINTF(("%s: insertion event\n", h->sc->dev.dv_xname));
			tcic_attach_card(h);
			break;

		case TCIC_EVENT_REMOVAL:
			DPRINTF(("%s: removal event\n", h->sc->dev.dv_xname));
			tcic_detach_card(h, DETACH_FORCE);
			break;

		default:
			panic("tcic_event_thread: unknown event %d",
			    pe->pe_type);
		}
		free(pe, M_TEMP, 0);
	}

	h->event_thread = NULL;

	/* In case parent is waiting for us to exit. */
	wakeup(h->sc);

	kthread_exit(0);
}


void
tcic_init_socket(struct tcic_handle *h)
{
	int reg;

	/* select this socket's config registers */
	tcic_sel_sock(h);

	/* set up the socket to interrupt on card detect */
	reg = tcic_read_ind_2(h, TCIC_IR_SCF2_N(h->sock));
	tcic_write_ind_2(h, TCIC_IR_SCF2_N(h->sock), reg & ~TCIC_SCF2_MCD);

	/* enable CD irq in R_IENA */
	reg = tcic_read_2(h, TCIC_R_IENA);
	tcic_write_2(h, TCIC_R_IENA, reg |= TCIC_IENA_CDCHG);

	/* if there's a card there, then attach it. also save sstat */
	h->sstat = reg = tcic_read_1(h, TCIC_R_SSTAT) & TCIC_SSTAT_STAT_MASK;
	if (reg & TCIC_SSTAT_CD)
		tcic_attach_card(h);
}

int
tcic_submatch(struct device *parent, void *match, void *aux)
{
	struct cfdata *cf = match;

	struct pcmciabus_attach_args *paa = aux;
	struct tcic_handle *h = (struct tcic_handle *) paa->pch;

	switch (h->sock) {
	case 0:
		if (cf->cf_loc[0 /* PCMCIABUSCF_CONTROLLER */] !=
		    -1 /* PCMCIABUSCF_CONTROLLER_DEFAULT */ &&
		    cf->cf_loc[0 /* PCMCIABUSCF_CONTROLLER */] != 0)
			return 0;
		if (cf->cf_loc[1 /* PCMCIABUSCF_SOCKET */] !=
		    -1 /* PCMCIABUSCF_SOCKET_DEFAULT */ &&
		    cf->cf_loc[1 /* PCMCIABUSCF_SOCKET */] != 0)
			return 0;

		break;
	case 1:
		if (cf->cf_loc[0 /* PCMCIABUSCF_CONTROLLER */] !=
		    -1 /* PCMCIABUSCF_CONTROLLER_DEFAULT */ &&
		    cf->cf_loc[0 /* PCMCIABUSCF_CONTROLLER */] != 0)
			return 0;
		if (cf->cf_loc[1 /* PCMCIABUSCF_SOCKET */] !=
		    -1 /* PCMCIABUSCF_SOCKET_DEFAULT */ &&
		    cf->cf_loc[1 /* PCMCIABUSCF_SOCKET */] != 1)
			return 0;

		break;
	default:
		panic("unknown tcic socket");
	}

	return ((*cf->cf_attach->ca_match)(parent, cf, aux));
}

int
tcic_print(void *arg, const char *pnp)
{
	struct pcmciabus_attach_args *paa = arg;
	struct tcic_handle *h = (struct tcic_handle *) paa->pch;

	/* Only "pcmcia"s can attach to "tcic"s... easy. */
	if (pnp)
		printf("pcmcia at %s", pnp);

	switch (h->sock) {
	case 0:
		printf(" socket 0");
		break;
	case 1:
		printf(" socket 1");
		break;
	default:
		panic("unknown tcic socket");
	}
	return (UNCONF);
}

int
tcic_intr(void *arg)
{
	struct tcic_softc *sc = arg;
	int i, ret = 0;

	DPRINTF(("%s: intr\n", sc->dev.dv_xname));

	for (i = 0; i < TCIC_NSLOTS; i++)
		if (sc->handle[i].flags & TCIC_FLAG_SOCKETP)
			ret += tcic_intr_socket(&sc->handle[i]);

	return (ret ? 1 : 0);
}

int
tcic_intr_socket(struct tcic_handle *h)
{
	int icsr, rv;

	rv = 0;
	tcic_sel_sock(h);
	icsr = tcic_read_1(h, TCIC_R_ICSR);

	DPRINTF(("%s: %d icsr: 0x%02x \n", h->sc->dev.dv_xname, h->sock, icsr));

	/* XXX or should the next three be handled in tcic_intr? -chb */
	if (icsr & TCIC_ICSR_PROGTIME) {
		DPRINTF(("%s: %02x PROGTIME\n", h->sc->dev.dv_xname, h->sock));
		rv = 1;
	}
	if (icsr & TCIC_ICSR_ILOCK) {
		DPRINTF(("%s: %02x ILOCK\n", h->sc->dev.dv_xname, h->sock));
		rv = 1;
	}
	if (icsr & TCIC_ICSR_ERR) {
		DPRINTF(("%s: %02x ERR\n", h->sc->dev.dv_xname, h->sock));
		rv = 1;
	}
	if (icsr & TCIC_ICSR_CDCHG) {
		int sstat, delta;

		/* compute what changed since last interrupt */
		sstat = tcic_read_aux_1(h->sc->iot, h->sc->ioh,
		    TCIC_AR_WCTL, TCIC_R_WCTL_XCSR) & TCIC_XCSR_STAT_MASK;
		delta = h->sstat ^ sstat;
		h->sstat = sstat;

		if (delta)
			rv = 1;

		DPRINTF(("%s: %02x CDCHG %x\n", h->sc->dev.dv_xname, h->sock,
		    delta));

		/*
		 * XXX This should probably schedule something to happen
		 * after the interrupt handler completes
		 */

		if (delta & TCIC_SSTAT_CD) {
			if (sstat & TCIC_SSTAT_CD) {
				if (!(h->flags & TCIC_FLAG_CARDP)) {
					DPRINTF(("%s: enqueuing INSERTION event\n",
					    h->sc->dev.dv_xname));
					tcic_queue_event(h, TCIC_EVENT_INSERTION);
				}
			} else {
				if (h->flags & TCIC_FLAG_CARDP) {
					/* Deactivate the card now. */
					DPRINTF(("%s: deactivating card\n",
					    h->sc->dev.dv_xname));
					tcic_deactivate_card(h);

					DPRINTF(("%s: enqueuing REMOVAL event\n",
					    h->sc->dev.dv_xname));
					tcic_queue_event(h, TCIC_EVENT_REMOVAL);
				}
			}
		}
		if (delta & TCIC_SSTAT_RDY) {
			DPRINTF(("%s: %02x READY\n", h->sc->dev.dv_xname, h->sock));
			/* shouldn't happen */
		}
		if (delta & TCIC_SSTAT_LBAT1) {
			DPRINTF(("%s: %02x LBAT1\n", h->sc->dev.dv_xname, h->sock));
		}
		if (delta & TCIC_SSTAT_LBAT2) {
			DPRINTF(("%s: %02x LBAT2\n", h->sc->dev.dv_xname, h->sock));
		}
		if (delta & TCIC_SSTAT_WP) {
			DPRINTF(("%s: %02x WP\n", h->sc->dev.dv_xname, h->sock));
		}
	}
	return rv;
}

void
tcic_queue_event(struct tcic_handle *h, int event)
{
	struct tcic_event *pe;
	int s;

	pe = malloc(sizeof(*pe), M_TEMP, M_NOWAIT);
	if (pe == NULL)
		panic("tcic_queue_event: can't allocate event");

	pe->pe_type = event;
	s = splhigh();
	SIMPLEQ_INSERT_TAIL(&h->events, pe, pe_q);
	splx(s);
	wakeup(&h->events);
}

void
tcic_attach_card(struct tcic_handle *h)
{
	DPRINTF(("tcic_attach_card\n"));

	if (h->flags & TCIC_FLAG_CARDP)
		panic("tcic_attach_card: already attached");

	/* call the MI attach function */

	pcmcia_card_attach(h->pcmcia);

	h->flags |= TCIC_FLAG_CARDP;
}

void
tcic_detach_card(struct tcic_handle *h, int flags)
{
	DPRINTF(("tcic_detach_card\n"));

	if (!(h->flags & TCIC_FLAG_CARDP))
		panic("tcic_detach_card: already detached");

	h->flags &= ~TCIC_FLAG_CARDP;

	/* call the MI detach function */

	pcmcia_card_detach(h->pcmcia, flags);

}

void
tcic_deactivate_card(struct tcic_handle *h)
{
	int val, reg;

	if (!(h->flags & TCIC_FLAG_CARDP))
		 panic("tcic_deactivate_card: already detached");

	/* call the MI deactivate function */
	pcmcia_card_deactivate(h->pcmcia);

	tcic_sel_sock(h);

	/* XXX disable card detect resume and configuration reset??? */

	/* power down the socket */
	tcic_write_1(h, TCIC_R_PWR, 0);

	/* reset the card XXX ? -chb */

	/* turn off irq's for this socket */
	reg = TCIC_IR_SCF1_N(h->sock);
	val = tcic_read_ind_2(h, reg);
	tcic_write_ind_2(h, reg, (val & ~TCIC_SCF1_IRQ_MASK)|TCIC_SCF1_IRQOFF);
	reg = TCIC_IR_SCF2_N(h->sock);
	val = tcic_read_ind_2(h, reg);
	tcic_write_ind_2(h, reg,
	    (val | (TCIC_SCF2_MLBAT1|TCIC_SCF2_MLBAT2|TCIC_SCF2_MRDY
		|TCIC_SCF2_MWP|TCIC_SCF2_MCD)));
}

/* XXX the following routine may need to be rewritten. -chb */
int 
tcic_chip_mem_alloc(pcmcia_chipset_handle_t pch, bus_size_t size,
    struct pcmcia_mem_handle *pcmhp)
{
	struct tcic_handle *h = (struct tcic_handle *) pch;
	bus_space_handle_t memh;
	bus_addr_t addr;
	bus_size_t sizepg;
	int i, mask, mhandle;

	/* out of sc->memh, allocate as many pages as necessary */

	/*
	 * The TCIC can map memory only in sizes that are
	 * powers of two, aligned at the natural boundary for the size.
	 */
	i = tcic_log2((u_int)size);
	if ((1<<i) < size)
		i++;
	sizepg = max(i, TCIC_MEM_SHIFT) - (TCIC_MEM_SHIFT-1);

	DPRINTF(("tcic_chip_mem_alloc: size %ld sizepg %ld\n", size, sizepg));

	/* can't allocate that much anyway */
	if (sizepg > TCIC_MEM_PAGES)	/* XXX -chb */
		return 1;

	mask = (1 << sizepg) - 1;

	addr = 0;		/* XXX gcc -Wuninitialized */
	mhandle = 0;		/* XXX gcc -Wuninitialized */

	/* XXX i should be initialised to always lay on boundary. -chb */
	for (i = 0; i < (TCIC_MEM_PAGES + 1 - sizepg); i += sizepg) {
		if ((h->sc->subregionmask & (mask << i)) == (mask << i)) {
			if (bus_space_subregion(h->sc->memt, h->sc->memh,
			    i * TCIC_MEM_PAGESIZE,
			    sizepg * TCIC_MEM_PAGESIZE, &memh))
				return (1);
			mhandle = mask << i;
			addr = h->sc->membase + (i * TCIC_MEM_PAGESIZE);
			h->sc->subregionmask &= ~(mhandle);
			break;
		}
	}

	if (i == (TCIC_MEM_PAGES + 1 - sizepg))
		return (1);

	DPRINTF(("tcic_chip_mem_alloc bus addr 0x%lx+0x%lx\n", (u_long) addr,
		 (u_long) size));

	pcmhp->memt = h->sc->memt;
	pcmhp->memh = memh;
	pcmhp->addr = addr;
	pcmhp->size = size;
	pcmhp->mhandle = mhandle;
	pcmhp->realsize = sizepg * TCIC_MEM_PAGESIZE;

	return (0);
}

/* XXX the following routine may need to be rewritten. -chb */
void 
tcic_chip_mem_free(pcmcia_chipset_handle_t pch, struct pcmcia_mem_handle *pcmhp)
{
	struct tcic_handle *h = (struct tcic_handle *) pch;

	h->sc->subregionmask |= pcmhp->mhandle;
}

void 
tcic_chip_do_mem_map(struct tcic_handle *h, int win)
{
	int reg, hwwin, wscnt;

	int kind = h->mem[win].kind & ~PCMCIA_WIDTH_MEM_MASK;
	int mem8 = (h->mem[win].kind & PCMCIA_WIDTH_MEM_MASK) == PCMCIA_WIDTH_MEM8;
	DPRINTF(("tcic_chip_do_mem_map window %d: 0x%lx+0x%lx 0x%lx\n",
		win, (u_long)h->mem[win].addr, (u_long)h->mem[win].size,
		(u_long)h->mem[win].offset));
	/*
	 * the even windows are used for socket 0,
	 * the odd ones for socket 1.
	 */
	hwwin = (win << 1) + h->sock;

	/* the WR_MEXT register is MBZ */
	tcic_write_ind_2(h, TCIC_WR_MEXT_N(hwwin), 0);

	/* set the host base address and window size */
	if (h->mem[win].size2 <= 1) {
		reg = ((h->mem[win].addr >> TCIC_MEM_SHIFT) &
		    TCIC_MBASE_ADDR_MASK) | TCIC_MBASE_4K;
	} else {
		reg = ((h->mem[win].addr >> TCIC_MEM_SHIFT) &
		    TCIC_MBASE_ADDR_MASK) | (h->mem[win].size2 >> 1);
	}
	tcic_write_ind_2(h, TCIC_WR_MBASE_N(hwwin), reg);

	/* set the card address and address space */
	reg = 0;
	reg = ((h->mem[win].offset >> TCIC_MEM_SHIFT) & TCIC_MMAP_ADDR_MASK);
	reg |= (kind == PCMCIA_MEM_ATTR) ? TCIC_MMAP_ATTR : 0;
	DPRINTF(("tcic_chip_do_map_mem window %d(%d) mmap 0x%04x\n",
	    win, hwwin, reg));
	tcic_write_ind_2(h, TCIC_WR_MMAP_N(hwwin), reg);

	/* set the MCTL register */
	/* must save WSCNT field in case this is a DB86082 rev 0 */
	/* XXX why can't I do the following two in one statement? */
	reg = tcic_read_ind_2(h, TCIC_WR_MCTL_N(hwwin)) & TCIC_MCTL_WSCNT_MASK;
	reg |= TCIC_MCTL_ENA|TCIC_MCTL_QUIET;
	reg |= mem8 ? TCIC_MCTL_B8 : 0;
	reg |= (h->sock << TCIC_MCTL_SS_SHIFT) & TCIC_MCTL_SS_MASK;
#ifdef notyet	/* XXX must get speed from CIS somehow. -chb */
	wscnt = tcic_ns2wscnt(h->mem[win].speed);
#else
	wscnt = tcic_ns2wscnt(tcic_mem_speed);	/*  300 is "save" default for CIS memory */
#endif
	if (h->sc->chipid == TCIC_CHIPID_DB86082_1) {
		/*
		 * this chip has the wait state count in window
		 * register 7 - hwwin.
		 */
		int reg2;
		reg2 = tcic_read_ind_2(h, TCIC_WR_MCTL_N(7-hwwin));
		reg2 &= ~TCIC_MCTL_WSCNT_MASK;
		reg2 |= wscnt & TCIC_MCTL_WSCNT_MASK;
		tcic_write_ind_2(h, TCIC_WR_MCTL_N(7-hwwin), reg2);
	} else {
		reg |= wscnt & TCIC_MCTL_WSCNT_MASK;
	}
	tcic_write_ind_2(h, TCIC_WR_MCTL_N(hwwin), reg);

#ifdef TCICDEBUG
	{
		int r1, r2, r3;

		r1 = tcic_read_ind_2(h, TCIC_WR_MBASE_N(hwwin));
		r2 = tcic_read_ind_2(h, TCIC_WR_MMAP_N(hwwin));
		r3 = tcic_read_ind_2(h, TCIC_WR_MCTL_N(hwwin));

		DPRINTF(("tcic_chip_do_mem_map window %d(%d): %04x %04x %04x\n",
		    win, hwwin, r1, r2, r3));
	}
#endif
}

/* XXX needs work */
int 
tcic_chip_mem_map(pcmcia_chipset_handle_t pch, int kind, bus_addr_t card_addr,
    bus_size_t size, struct pcmcia_mem_handle *pcmhp, bus_size_t *offsetp,
    int *windowp)
{
	struct tcic_handle *h = (struct tcic_handle *) pch;
	bus_addr_t busaddr;
	long card_offset;
	int i, win;

	win = -1;
	for (i = 0; i < h->memwins; i++) {
		if ((h->memalloc & (1 << i)) == 0) {
			win = i;
			h->memalloc |= (1 << i);
			break;
		}
	}

	if (win == -1)
		return (1);

	*windowp = win;

	/* XXX this is pretty gross */

	if (h->sc->memt != pcmhp->memt)
		panic("tcic_chip_mem_map memt is bogus");

	busaddr = pcmhp->addr;

	/*
	 * compute the address offset to the pcmcia address space for the
	 * tcic.  this is intentionally signed.  The masks and shifts below
	 * will cause TRT to happen in the tcic registers.  Deal with making
	 * sure the address is aligned, and return the alignment offset.
	 */

	*offsetp = card_addr % TCIC_MEM_ALIGN;
	card_addr -= *offsetp;

	DPRINTF(("tcic_chip_mem_map window %d bus %lx+%lx+%lx at card addr "
	    "%lx\n", win, (u_long) busaddr, (u_long) * offsetp, (u_long) size,
	    (u_long) card_addr));

	/* XXX we can't use size. -chb */
	/*
	 * include the offset in the size, and decrement size by one, since
	 * the hw wants start/stop
	 */
	size += *offsetp - 1;

	card_offset = (((long) card_addr) - ((long) busaddr));

	DPRINTF(("tcic_chip_mem_map window %d card_offset 0x%lx\n",
	    win, (u_long)card_offset));

	h->mem[win].addr = busaddr;
	h->mem[win].size = size;
	h->mem[win].size2 = tcic_log2((u_int)pcmhp->realsize) - TCIC_MEM_SHIFT;
	h->mem[win].offset = card_offset;
	h->mem[win].kind = kind;

	tcic_chip_do_mem_map(h, win);

	return (0);
}

void 
tcic_chip_mem_unmap(pcmcia_chipset_handle_t pch, int window)
{
	struct tcic_handle *h = (struct tcic_handle *) pch;
	int reg, hwwin;

	if (window >= h->memwins)
		panic("tcic_chip_mem_unmap: window out of range");

	hwwin = (window << 1) + h->sock;
	reg = tcic_read_ind_2(h, TCIC_WR_MCTL_N(hwwin));
	reg &= ~TCIC_MCTL_ENA;
	tcic_write_ind_2(h, TCIC_WR_MCTL_N(hwwin), reg);

	h->memalloc &= ~(1 << window);
}

int 
tcic_chip_io_alloc(pcmcia_chipset_handle_t pch, bus_addr_t start,
    bus_size_t size, bus_size_t align, struct pcmcia_io_handle *pcihp)
{
	struct tcic_handle *h = (struct tcic_handle *) pch;
	bus_space_tag_t iot;
	bus_space_handle_t ioh;
	bus_addr_t ioaddr;
	int size2, flags = 0;

	/*
	 * Allocate some arbitrary I/O space.
	 */

	DPRINTF(("tcic_chip_io_alloc req 0x%lx %ld %ld\n",
	    (u_long) start, (u_long) size, (u_long) align));
	/*
	 * The TCIC can map I/O space only in sizes that are
	 * powers of two, aligned at the natural boundary for the size.
	 */
	size2 = tcic_log2((u_int)size);
	if ((1 << size2) < size)
		size2++;
	/* can't allocate that much anyway */
	if (size2 > 16)	/* XXX 64K -chb */
		return 1;
	if (align) {
		if ((1 << size2) != align)
			return 1;	/* not suitably  aligned */
	} else {
		align = 1 << size2;	/* no alignment given, make it natural */
	}
	if (start & (align - 1))
		return 1;	/* not suitably aligned */

	iot = h->sc->iot;

	if (start) {
		ioaddr = start;
		if (bus_space_map(iot, start, size, 0, &ioh))
			return (1);
		DPRINTF(("tcic_chip_io_alloc map port %lx+%lx\n",
		    (u_long) ioaddr, (u_long) size));
	} else {
		flags |= PCMCIA_IO_ALLOCATED;
		if (bus_space_alloc(iot, h->sc->iobase,
		    h->sc->iobase + h->sc->iosize, size, align, 0, 0,
		    &ioaddr, &ioh))
			return (1);
		DPRINTF(("tcic_chip_io_alloc alloc port %lx+%lx\n",
		    (u_long) ioaddr, (u_long) size));
	}

	pcihp->iot = iot;
	pcihp->ioh = ioh;
	pcihp->addr = ioaddr;
	pcihp->size = size;
	pcihp->flags = flags;

	return (0);
}

void 
tcic_chip_io_free(pcmcia_chipset_handle_t pch, struct pcmcia_io_handle *pcihp)
{
	bus_space_tag_t iot = pcihp->iot;
	bus_space_handle_t ioh = pcihp->ioh;
	bus_size_t size = pcihp->size;

	if (pcihp->flags & PCMCIA_IO_ALLOCATED)
		bus_space_free(iot, ioh, size);
	else
		bus_space_unmap(iot, ioh, size);
}

static int tcic_iowidth_map[] =
    { TCIC_ICTL_AUTOSZ, TCIC_ICTL_B8, TCIC_ICTL_B16 };

void 
tcic_chip_do_io_map(struct tcic_handle *h, int win)
{
	int reg, size2, iotiny, wbase, hwwin, wscnt;

	DPRINTF(("tcic_chip_do_io_map win %d addr %lx size %lx width %d\n",
	    win, (long) h->io[win].addr, (long) h->io[win].size,
	    h->io[win].width * 8));

	/*
	 * the even windows are used for socket 0,
	 * the odd ones for socket 1.
	 */
	hwwin = (win << 1) + h->sock;

	/* set the WR_BASE register */
	/* XXX what if size isn't power of 2? -chb */
	size2 = tcic_log2((u_int)h->io[win].size);
	DPRINTF(("tcic_chip_do_io_map win %d size2 %d\n", win, size2));
	if (size2 < 1) {
		iotiny = TCIC_ICTL_TINY;
		wbase = h->io[win].addr;
	} else {
		iotiny = 0;
		/* XXX we should do better -chb */
		wbase = h->io[win].addr | (1 << (size2 - 1));
	}
	tcic_write_ind_2(h, TCIC_WR_IBASE_N(hwwin), wbase);

	/* set the WR_ICTL register */
	reg = TCIC_ICTL_ENA | TCIC_ICTL_QUIET;
	reg |= (h->sock << TCIC_ICTL_SS_SHIFT) & TCIC_ICTL_SS_MASK;
	reg |= iotiny | tcic_iowidth_map[h->io[win].width];
	if (h->sc->chipid != TCIC_CHIPID_DB86082_1)
		reg |= TCIC_ICTL_PASS16;
#ifdef notyet	/* XXX must get speed from CIS somehow. -chb */
	wscnt = tcic_ns2wscnt(h->io[win].speed);
#else
	wscnt = tcic_ns2wscnt(tcic_io_speed);	/* linux uses 0 as default */
#endif
	reg |= wscnt & TCIC_ICTL_WSCNT_MASK;
	tcic_write_ind_2(h, TCIC_WR_ICTL_N(hwwin), reg);

#ifdef TCICDEBUG
	{
		int r1, r2;

		r1 = tcic_read_ind_2(h, TCIC_WR_IBASE_N(hwwin));
		r2 = tcic_read_ind_2(h, TCIC_WR_ICTL_N(hwwin));

		DPRINTF(("tcic_chip_do_io_map window %d(%d): %04x %04x\n",
		    win, hwwin, r1, r2));
	}
#endif
}

int 
tcic_chip_io_map(pcmcia_chipset_handle_t pch, int width, bus_addr_t offset,
    bus_size_t size, struct pcmcia_io_handle *pcihp, int *windowp)
{
	struct tcic_handle *h = (struct tcic_handle *) pch;
	bus_addr_t ioaddr = pcihp->addr + offset;
	int i, win;
#ifdef TCICDEBUG
	static char *width_names[] = { "auto", "io8", "io16" };
#endif

	/* XXX Sanity check offset/size. */

	win = -1;
	for (i = 0; i < TCIC_IO_WINS; i++) {
		if ((h->ioalloc & (1 << i)) == 0) {
			win = i;
			h->ioalloc |= (1 << i);
			break;
		}
	}

	if (win == -1)
		return (1);

	*windowp = win;

	/* XXX this is pretty gross */

	if (h->sc->iot != pcihp->iot)
		panic("tcic_chip_io_map iot is bogus");

	DPRINTF(("tcic_chip_io_map window %d %s port %lx+%lx\n",
		 win, width_names[width], (u_long) ioaddr, (u_long) size));

	/* XXX wtf is this doing here? */

	printf(" port 0x%lx", (u_long) ioaddr);
	if (size > 1)
		printf("-0x%lx", (u_long) ioaddr + (u_long) size - 1);

	h->io[win].addr = ioaddr;
	h->io[win].size = size;
	h->io[win].width = width;

	tcic_chip_do_io_map(h, win);

	return (0);
}

void 
tcic_chip_io_unmap(pcmcia_chipset_handle_t pch, int window)
{
	struct tcic_handle *h = (struct tcic_handle *) pch;
	int reg, hwwin;

	if (window >= TCIC_IO_WINS)
		panic("tcic_chip_io_unmap: window out of range");

	hwwin = (window << 1) + h->sock;
	reg = tcic_read_ind_2(h, TCIC_WR_ICTL_N(hwwin));
	reg &= ~TCIC_ICTL_ENA;
	tcic_write_ind_2(h, TCIC_WR_ICTL_N(hwwin), reg);

	h->ioalloc &= ~(1 << window);
}

void
tcic_chip_socket_enable(pcmcia_chipset_handle_t pch)
{
	struct tcic_handle *h = (struct tcic_handle *) pch;
	int cardtype, reg, win;

	tcic_sel_sock(h);

	/*
	 * power down the socket to reset it.
	 * put card reset into high-z, put chip outputs to card into high-z
	 */

	tcic_write_1(h, TCIC_R_PWR, 0);
	reg = tcic_read_aux_2(h->sc->iot, h->sc->ioh, TCIC_AR_ILOCK);
	reg |= TCIC_ILOCK_CWAIT;
	reg &= ~(TCIC_ILOCK_CRESET|TCIC_ILOCK_CRESENA);
	tcic_write_aux_2(h->sc->iot, h->sc->ioh, TCIC_AR_ILOCK, reg);
	tcic_write_1(h, TCIC_R_SCTRL, 0);	/* clear TCIC_SCTRL_ENA */

	/* power up the socket */

	/* turn on VCC, turn of VPP */
	reg = TCIC_PWR_VCC_N(h->sock) | TCIC_PWR_VPP_N(h->sock) | h->sc->pwrena;
	if (h->sc->pwrena)		/* this is a '84 type chip */
		reg |= TCIC_PWR_VCC5V;
	tcic_write_1(h, TCIC_R_PWR, reg);
	delay(10000);

	/* enable reset and wiggle it to reset the card */
	reg = tcic_read_aux_2(h->sc->iot, h->sc->ioh, TCIC_AR_ILOCK);
	reg |= TCIC_ILOCK_CRESENA;
	tcic_write_aux_2(h->sc->iot, h->sc->ioh, TCIC_AR_ILOCK, reg);
	/* XXX need bus_space_barrier here */
	reg |= TCIC_ILOCK_CRESET;
	tcic_write_aux_2(h->sc->iot, h->sc->ioh, TCIC_AR_ILOCK, reg);
	/* enable card signals */
	tcic_write_1(h, TCIC_R_SCTRL, TCIC_SCTRL_ENA);
	delay(10);	/* wait 10 us */

	/* clear the reset flag */
	reg = tcic_read_aux_2(h->sc->iot, h->sc->ioh, TCIC_AR_ILOCK);
	reg &= ~(TCIC_ILOCK_CRESET);
	tcic_write_aux_2(h->sc->iot, h->sc->ioh, TCIC_AR_ILOCK, reg);

	/* wait 20ms as per pc card standard (r2.01) section 4.3.6 */
	delay(20000);

	/* wait for the chip to finish initializing */
	tcic_wait_ready(h);

	/* WWW */
	/* zero out the address windows */

	/* writing to WR_MBASE_N disables the window */
	for (win = 0; win < h->memwins; win++) {
		tcic_write_ind_2(h, TCIC_WR_MBASE_N((win<<1)+h->sock), 0);
	}
	/* writing to WR_IBASE_N disables the window */
	for (win = 0; win < TCIC_IO_WINS; win++) {
		tcic_write_ind_2(h, TCIC_WR_IBASE_N((win<<1)+h->sock), 0);
	}

	/* set the card type */

	cardtype = pcmcia_card_gettype(h->pcmcia);

#if 0
	reg = tcic_read_ind_2(h, TCIC_IR_SCF1_N(h->sock));
	reg &= ~TCIC_SCF1_IRQ_MASK;
#else
	reg = 0;
#endif
	reg |= ((cardtype == PCMCIA_IFTYPE_IO) ?
		TCIC_SCF1_IOSTS : 0);
	reg |= tcic_irqmap[h->ih_irq];		/* enable interrupts */
	reg &= ~TCIC_SCF1_IRQOD;
	tcic_write_ind_2(h, TCIC_IR_SCF1_N(h->sock), reg);

	DPRINTF(("%s: tcic_chip_socket_enable %d cardtype %s 0x%02x\n",
	    h->sc->dev.dv_xname, h->sock,
	    ((cardtype == PCMCIA_IFTYPE_IO) ? "io" : "mem"), reg));

	/* reinstall all the memory and io mappings */

	for (win = 0; win < h->memwins; win++)
		if (h->memalloc & (1 << win))
			tcic_chip_do_mem_map(h, win);

	for (win = 0; win < TCIC_IO_WINS; win++)
		if (h->ioalloc & (1 << win))
			tcic_chip_do_io_map(h, win);
}

void
tcic_chip_socket_disable(pcmcia_chipset_handle_t pch)
{
	struct tcic_handle *h = (struct tcic_handle *) pch;
	int val;

	DPRINTF(("tcic_chip_socket_disable\n"));

	tcic_sel_sock(h);

	/* disable interrupts */
	val = tcic_read_ind_2(h, TCIC_IR_SCF1_N(h->sock));
	val &= TCIC_SCF1_IRQ_MASK;
	tcic_write_ind_2(h, TCIC_IR_SCF1_N(h->sock), val);

	/* disable the output signals */
	tcic_write_1(h, TCIC_R_SCTRL, 0);
	val = tcic_read_aux_2(h->sc->iot, h->sc->ioh, TCIC_AR_ILOCK);
	val &= ~TCIC_ILOCK_CRESENA;
	tcic_write_aux_2(h->sc->iot, h->sc->ioh, TCIC_AR_ILOCK, val);

	/* power down the socket */
	tcic_write_1(h, TCIC_R_PWR, 0);
}

/*
 * XXX The following is Linux driver but doesn't match the table
 * in the manual.
 */
int
tcic_ns2wscnt(int ns)
{
	if (ns < 14) {
		return 0;
	} else {
		return (2*(ns-14))/70;	/* XXX assumes 14.31818 MHz clock. */
	}
}

int
tcic_log2(u_int val)
{
	int i, l2;

	l2 = i = 0;
	while (val) {
		if (val & 1)
			l2 = i;
		i++;
		val >>= 1;
	}
	return l2;
}
