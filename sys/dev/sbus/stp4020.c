/*	$OpenBSD: stp4020.c,v 1.24 2025/05/11 19:41:05 miod Exp $	*/
/*	$NetBSD: stp4020.c,v 1.23 2002/06/01 23:51:03 lukem Exp $	*/

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Paul Kranenburg.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * STP4020: SBus/PCMCIA bridge supporting one Type-3 PCMCIA card, or up to
 * two Type-1 and Type-2 PCMCIA cards..
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/extent.h>
#include <sys/proc.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/device.h>

#include <dev/pcmcia/pcmciareg.h>
#include <dev/pcmcia/pcmciavar.h>
#include <dev/pcmcia/pcmciachip.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/sbus/stp4020reg.h>
#include <dev/sbus/stp4020var.h>

/*
 * We use the three available windows per socket in a simple, fixed
 * arrangement. Each window maps (at full 1 MB size) one of the pcmcia
 * spaces into sbus space.
 */
#define STP_WIN_ATTR	0	/* index of the attribute memory space window */
#define	STP_WIN_MEM	1	/* index of the common memory space window */
#define	STP_WIN_IO	2	/* index of the io space window */

#ifdef STP4020_DEBUG
int stp4020_debug = 0;
#define DPRINTF(x)	do { if (stp4020_debug) printf x; } while(0)
#else
#define DPRINTF(x)
#endif

int	stp4020print(void *, const char *);
void	stp4020_map_window(struct stp4020_socket *, int, int);
void	stp4020_calc_speed(int, int, int *, int *);
void	stp4020_intr_dispatch(void *);

struct	cfdriver stp_cd = {
	NULL, "stp", DV_DULL
};

#ifdef STP4020_DEBUG
static void	stp4020_dump_regs(struct stp4020_socket *);
#endif

static u_int16_t stp4020_rd_sockctl(struct stp4020_socket *, int);
static void	stp4020_wr_sockctl(struct stp4020_socket *, int, u_int16_t);
static void	stp4020_wr_winctl(struct stp4020_socket *, int, int, u_int16_t);

void	stp4020_delay(unsigned int);
void	stp4020_attach_socket(struct stp4020_socket *, int);
void	stp4020_create_event_thread(void *);
void	stp4020_event_thread(void *);
void	stp4020_queue_event(struct stp4020_softc *, int);

int	stp4020_chip_mem_alloc(pcmcia_chipset_handle_t, bus_size_t,
	    struct pcmcia_mem_handle *);
void	stp4020_chip_mem_free(pcmcia_chipset_handle_t,
	    struct pcmcia_mem_handle *);
int	stp4020_chip_mem_map(pcmcia_chipset_handle_t, int, bus_addr_t,
	    bus_size_t, struct pcmcia_mem_handle *, bus_size_t *, int *);
void	stp4020_chip_mem_unmap(pcmcia_chipset_handle_t, int);

int	stp4020_chip_io_alloc(pcmcia_chipset_handle_t,
	    bus_addr_t, bus_size_t, bus_size_t, struct pcmcia_io_handle *);
void	stp4020_chip_io_free(pcmcia_chipset_handle_t,
	    struct pcmcia_io_handle *);
int	stp4020_chip_io_map(pcmcia_chipset_handle_t, int, bus_addr_t,
	    bus_size_t, struct pcmcia_io_handle *, int *);
void	stp4020_chip_io_unmap(pcmcia_chipset_handle_t, int);

void	stp4020_chip_socket_enable(pcmcia_chipset_handle_t);
void	stp4020_chip_socket_disable(pcmcia_chipset_handle_t);
void	*stp4020_chip_intr_establish(pcmcia_chipset_handle_t,
	    struct pcmcia_function *, int, int (*) (void *), void *, char *);
void	stp4020_chip_intr_disestablish(pcmcia_chipset_handle_t, void *);
const char *stp4020_chip_intr_string(pcmcia_chipset_handle_t, void *);

/* Our PCMCIA chipset methods */
static struct pcmcia_chip_functions stp4020_functions = {
	stp4020_chip_mem_alloc,
	stp4020_chip_mem_free,
	stp4020_chip_mem_map,
	stp4020_chip_mem_unmap,

	stp4020_chip_io_alloc,
	stp4020_chip_io_free,
	stp4020_chip_io_map,
	stp4020_chip_io_unmap,

	stp4020_chip_intr_establish,
	stp4020_chip_intr_disestablish,
	stp4020_chip_intr_string,

	stp4020_chip_socket_enable,
	stp4020_chip_socket_disable
};


static __inline__ u_int16_t
stp4020_rd_sockctl(struct stp4020_socket *h, int idx)
{
	int o = ((STP4020_SOCKREGS_SIZE * (h->sock)) + idx);
	return (bus_space_read_2(h->tag, h->regs, o));
}

static __inline__ void
stp4020_wr_sockctl(struct stp4020_socket *h, int idx, u_int16_t v)
{
	int o = (STP4020_SOCKREGS_SIZE * (h->sock)) + idx;
	bus_space_write_2(h->tag, h->regs, o, v);
}

static __inline__ void
stp4020_wr_winctl(struct stp4020_socket *h, int win, int idx, u_int16_t v)
{
	int o = (STP4020_SOCKREGS_SIZE * (h->sock)) +
	    (STP4020_WINREGS_SIZE * win) + idx;
	bus_space_write_2(h->tag, h->regs, o, v);
}


int
stp4020print(void *aux, const char *busname)
{
	struct pcmciabus_attach_args *paa = aux;
	struct stp4020_socket *h = paa->pch;

	printf(" socket %d", h->sock);
	return (UNCONF);
}

/*
 * Attach all the sub-devices we can find
 */
void
stpattach_common(struct stp4020_softc *sc, int clockfreq)
{
	int i, rev;

	rev = stp4020_rd_sockctl(&sc->sc_socks[0], STP4020_ISR1_IDX) &
	    STP4020_ISR1_REV_M;
	printf(": rev %x\n", rev);

	sc->sc_pct = (pcmcia_chipset_tag_t)&stp4020_functions;

	/*
	 * Arrange that a kernel thread be created to handle
	 * insert/removal events.
	 */
	sc->events = 0;
	kthread_create_deferred(stp4020_create_event_thread, sc);

	for (i = 0; i < STP4020_NSOCK; i++) {
		struct stp4020_socket *h = &sc->sc_socks[i];
		h->sock = i;
		h->sc = sc;
#ifdef STP4020_DEBUG
		if (stp4020_debug)
			stp4020_dump_regs(h);
#endif
		stp4020_attach_socket(h, clockfreq);
	}
}

void
stp4020_attach_socket(struct stp4020_socket *h, int speed)
{
	struct pcmciabus_attach_args paa;
	int v;

	/* no interrupt handlers yet */
	h->intrhandler = NULL;
	h->intrarg = NULL;
	h->softint = NULL;
	h->int_enable = h->int_disable = 0;

	/* Map all three windows */
	stp4020_map_window(h, STP_WIN_ATTR, speed);
	stp4020_map_window(h, STP_WIN_MEM, speed);
	stp4020_map_window(h, STP_WIN_IO, speed);

	/* Configure one pcmcia device per socket */
	paa.paa_busname = "pcmcia";
	paa.pct = (pcmcia_chipset_tag_t)h->sc->sc_pct;
	paa.pch = (pcmcia_chipset_handle_t)h;
	paa.iobase = 0;
	paa.iosize = STP4020_WINDOW_SIZE;

	h->pcmcia = config_found(&h->sc->sc_dev, &paa, stp4020print);

	if (h->pcmcia == NULL)
		return;

	/*
	 * There's actually a pcmcia bus attached; initialize the slot.
	 */

	/*
	 * Clear things up before we enable status change interrupts.
	 * This seems to not be fully initialized by the PROM.
	 */
	stp4020_wr_sockctl(h, STP4020_ICR1_IDX, 0);
	stp4020_wr_sockctl(h, STP4020_ICR0_IDX, 0);
	stp4020_wr_sockctl(h, STP4020_ISR1_IDX, 0x3fff);
	stp4020_wr_sockctl(h, STP4020_ISR0_IDX, 0x3fff);

	/*
	 * Enable socket status change interrupts.
	 * We use SB_INT[1] for status change interrupts.
	 */
	v = STP4020_ICR0_ALL_STATUS_IE | STP4020_ICR0_SCILVL_SB1;
	stp4020_wr_sockctl(h, STP4020_ICR0_IDX, v);

	/* Get live status bits from ISR0 */
	v = stp4020_rd_sockctl(h, STP4020_ISR0_IDX);
	h->sense = v & (STP4020_ISR0_CD1ST | STP4020_ISR0_CD2ST);
	if (h->sense != 0) {
		h->flags |= STP4020_SOCKET_BUSY;
		pcmcia_card_attach(h->pcmcia);
	}
}


/*
 * Deferred thread creation callback.
 */
void
stp4020_create_event_thread(void *arg)
{
	struct stp4020_softc *sc = arg;

	if (kthread_create(stp4020_event_thread, sc, &sc->event_thread,
	    sc->sc_dev.dv_xname)) {
		panic("%s: unable to create event thread", sc->sc_dev.dv_xname);
	}
}

/*
 * The actual event handling thread.
 */
void
stp4020_event_thread(void *arg)
{
	struct stp4020_softc *sc = arg;
	int s, sense;
	unsigned int socket;

	for (;;) {
		struct stp4020_socket *h;

		s = splhigh();
		if ((socket = ffs(sc->events)) == 0) {
			splx(s);
			tsleep_nsec(&sc->events, PWAIT, "stp4020_ev", INFSLP);
			continue;
		}
		socket--;
		sc->events &= ~(1 << socket);
		splx(s);

		if (socket >= STP4020_NSOCK) {
#ifdef DEBUG
			printf("stp4020_event_thread: wayward socket number %d\n",
			    socket);
#endif
			continue;
		}

		h = &sc->sc_socks[socket];

		/* Read socket's ISR0 for the interrupt status bits */
		sense = stp4020_rd_sockctl(h, STP4020_ISR0_IDX) &
		    (STP4020_ISR0_CD1ST | STP4020_ISR0_CD2ST);

		if (sense > h->sense) {
			/*
			 * If at least one more sensor is asserted, this is
			 * a card insertion.
			 */
			h->sense = sense;
			if ((h->flags & STP4020_SOCKET_BUSY) == 0) {
				h->flags |= STP4020_SOCKET_BUSY;
				pcmcia_card_attach(h->pcmcia);
			}
		} else if (sense < h->sense) {
			/*
			 * If at least one less sensor is asserted, this is
			 * a card removal.
			 */
			h->sense = sense;
			if (h->flags & STP4020_SOCKET_BUSY) {
				h->flags &= ~STP4020_SOCKET_BUSY;
				pcmcia_card_detach(h->pcmcia, DETACH_FORCE);
			}
		}
	}
}

void
stp4020_queue_event(struct stp4020_softc *sc, int sock)
{
	int s;

	s = splhigh();
	sc->events |= (1 << sock);
	splx(s);
	wakeup(&sc->events);
}

/*
 * Software interrupt called to invoke the real driver interrupt handler.
 */
void
stp4020_intr_dispatch(void *arg)
{
	struct stp4020_socket *h = (struct stp4020_socket *)arg;
	int s;

	/* invoke driver handler */
	h->intrhandler(h->intrarg);

	/* enable SBUS interrupts for PCMCIA interrupts again */
	s = splhigh();
	stp4020_wr_sockctl(h, STP4020_ICR0_IDX, h->int_enable);
	splx(s);
}

int
stp4020_statintr(void *arg)
{
	struct stp4020_softc *sc = arg;
	int i, sense, r = 0;
	int s;

	/* protect hardware access against soft interrupts */
	s = splhigh();

	/*
	 * Check each socket for pending requests.
	 */
	for (i = 0 ; i < STP4020_NSOCK; i++) {
		struct stp4020_socket *h;
		int v;

		h = &sc->sc_socks[i];

		/* Read socket's ISR0 for the interrupt status bits */
		v = stp4020_rd_sockctl(h, STP4020_ISR0_IDX);
		sense = v & (STP4020_ISR0_CD1ST | STP4020_ISR0_CD2ST);

#ifdef STP4020_DEBUG
		if (stp4020_debug != 0)
			printf("stp4020_statintr: ISR0=%b\n",
			    v, STP4020_ISR0_IOBITS);
#endif

		/* Ack all interrupts at once */
		stp4020_wr_sockctl(h, STP4020_ISR0_IDX,
		    STP4020_ISR0_ALL_STATUS_IRQ);

		if ((v & STP4020_ISR0_CDCHG) != 0) {
			r = 1;

			/*
			 * Card detect status changed. In an ideal world,
			 * both card detect sensors should be set if a card
			 * is in the slot, and clear if it is not.
			 *
			 * Unfortunately, it turns out that we can get the
			 * notification before both sensors are set (or
			 * clear).
			 *
			 * This can be very funny if only one sensor is set.
			 * Is this a removal or an insertion operation?
			 * Defer appropriate action to the worker thread.
			 */
			if (sense != h->sense)
				stp4020_queue_event(sc, i);

		}

		/* informational messages */
		if ((v & STP4020_ISR0_BVD1CHG) != 0) {
			DPRINTF(("stp4020[%d]: Battery change 1\n",
			    h->sock));
			r = 1;
		}

		if ((v & STP4020_ISR0_BVD2CHG) != 0) {
			DPRINTF(("stp4020[%d]: Battery change 2\n",
			    h->sock));
			r = 1;
		}

		if ((v & STP4020_ISR0_RDYCHG) != 0) {
			DPRINTF(("stp4020[%d]: Ready/Busy change\n",
			    h->sock));
			r = 1;
		}

		if ((v & STP4020_ISR0_WPCHG) != 0) {
			DPRINTF(("stp4020[%d]: Write protect change\n",
			    h->sock));
			r = 1;
		}

		if ((v & STP4020_ISR0_PCTO) != 0) {
			DPRINTF(("stp4020[%d]: Card access timeout\n",
			    h->sock));
			r = 1;
		}

		if ((v & STP4020_ISR0_SCINT) != 0) {
			DPRINTF(("stp4020[%d]: Status change\n",
			    h->sock));
			r = 1;
		}

		/*
		 * Not interrupts flag per se, but interrupts can occur when
		 * they are asserted, at least during our slot enable routine.
		 */
		if ((h->flags & STP4020_SOCKET_ENABLING) &&
		    (v & (STP4020_ISR0_WAITST | STP4020_ISR0_PWRON)))
			r = 1;
	}

	splx(s);

	return (r);
}

int
stp4020_iointr(void *arg)
{
	struct stp4020_softc *sc = arg;
	int i, r = 0;
	int s;

	/* protect hardware access against soft interrupts */
	s = splhigh();

	/*
	 * Check each socket for pending requests.
	 */
	for (i = 0 ; i < STP4020_NSOCK; i++) {
		struct stp4020_socket *h;
		int v;

		h = &sc->sc_socks[i];
		v = stp4020_rd_sockctl(h, STP4020_ISR0_IDX);

		if ((v & STP4020_ISR0_IOINT) != 0) {
			/* we can not deny this is ours, no matter what the
			   card driver says. */
			r = 1;

			/* ack interrupt */
			stp4020_wr_sockctl(h, STP4020_ISR0_IDX, v);

			/* It's a card interrupt */
			if ((h->flags & STP4020_SOCKET_BUSY) == 0) {
				printf("stp4020[%d]: spurious interrupt?\n",
				    h->sock);
				continue;
			}
			/* Call card handler, if any */
			if (h->softint != NULL) {
				softintr_schedule_raw(h->softint);

				/*
				 * Disable this sbus interrupt, until the
				 * softintr handler had a chance to run.
				 */
				stp4020_wr_sockctl(h, STP4020_ICR0_IDX,
				    h->int_disable);
			}
		}

	}

	splx(s);

	return (r);
}

/*
 * The function gets the sbus speed and a access time and calculates
 * values for the CMDLNG and CMDDLAY registers.
 */
void
stp4020_calc_speed(int bus_speed, int ns, int *length, int *delay)
{
	int result;

	if (ns < STP4020_MEM_SPEED_MIN)
		ns = STP4020_MEM_SPEED_MIN;
	else if (ns > STP4020_MEM_SPEED_MAX)
		ns = STP4020_MEM_SPEED_MAX;
	result = ns * (bus_speed / 1000);
	if (result % 1000000)
		result = result / 1000000 + 1;
	else
		result /= 1000000;
	*length = result;

	/* the sbus frequency range is limited, so we can keep this simple */
	*delay = ns <= STP4020_MEM_SPEED_MIN ? 1 : 2;
}

void
stp4020_map_window(struct stp4020_socket *h, int win, int speed)
{
	int v, length, delay;

	/*
	 * According to the PC Card standard 300ns access timing should be
	 * used for attribute memory access. Our pcmcia framework does not
	 * seem to propagate timing information, so we use that
	 * everywhere.
	 */
	stp4020_calc_speed(speed, 300, &length, &delay);

	/*
	 * Fill in the Address Space Select and Base Address
	 * fields of this windows control register 0.
	 */
	v = ((delay << STP4020_WCR0_CMDDLY_S) & STP4020_WCR0_CMDDLY_M) |
	    ((length << STP4020_WCR0_CMDLNG_S) & STP4020_WCR0_CMDLNG_M);
	switch (win) {
	case STP_WIN_ATTR:
		v |= STP4020_WCR0_ASPSEL_AM;
		break;
	case STP_WIN_MEM:
		v |= STP4020_WCR0_ASPSEL_CM;
		break;
	case STP_WIN_IO:
		v |= STP4020_WCR0_ASPSEL_IO;
		break;
	}
	v |= (STP4020_ADDR2PAGE(0) & STP4020_WCR0_BASE_M);
	stp4020_wr_winctl(h, win, STP4020_WCR0_IDX, v);
	stp4020_wr_winctl(h, win, STP4020_WCR1_IDX,
	    1 << STP4020_WCR1_WAITREQ_S);
}

int
stp4020_chip_mem_alloc(pcmcia_chipset_handle_t pch, bus_size_t size,
    struct pcmcia_mem_handle *pcmhp)
{
	struct stp4020_socket *h = (struct stp4020_socket *)pch;

	/* we can not do much here, defere work to _mem_map */
	pcmhp->memt = h->wintag;
	pcmhp->size = size;
	pcmhp->addr = 0;
	pcmhp->mhandle = 0;
	pcmhp->realsize = size;

	return (0);
}

void
stp4020_chip_mem_free(pcmcia_chipset_handle_t pch,
    struct pcmcia_mem_handle *pcmhp)
{
}

int
stp4020_chip_mem_map(pcmcia_chipset_handle_t pch, int kind,
    bus_addr_t card_addr, bus_size_t size, struct pcmcia_mem_handle *pcmhp,
    bus_size_t *offsetp, int *windowp)
{
	struct stp4020_socket *h = (struct stp4020_socket *)pch;
	int win = (kind & PCMCIA_MEM_ATTR) ? STP_WIN_ATTR : STP_WIN_MEM;

	pcmhp->memt = h->wintag;
	bus_space_subregion(h->wintag, h->windows[win].winaddr,
	    card_addr, size, &pcmhp->memh);
	pcmhp->size = size;
	pcmhp->realsize = STP4020_WINDOW_SIZE - card_addr;
	*offsetp = 0;
	*windowp = win;

	return (0);
}

void
stp4020_chip_mem_unmap(pcmcia_chipset_handle_t pch, int win)
{
}

int
stp4020_chip_io_alloc(pcmcia_chipset_handle_t pch, bus_addr_t start,
    bus_size_t size, bus_size_t align, struct pcmcia_io_handle *pcihp)
{
	struct stp4020_socket *h = (struct stp4020_socket *)pch;

	pcihp->iot = h->wintag;
	pcihp->ioh = h->windows[STP_WIN_IO].winaddr;
	pcihp->size = size;
	return (0);
}

void
stp4020_chip_io_free(pcmcia_chipset_handle_t pch,
    struct pcmcia_io_handle *pcihp)
{
}

int
stp4020_chip_io_map(pcmcia_chipset_handle_t pch, int width, bus_addr_t offset,
    bus_size_t size, struct pcmcia_io_handle *pcihp, int *windowp)
{
	struct stp4020_socket *h = (struct stp4020_socket *)pch;

	pcihp->iot = h->wintag;
	bus_space_subregion(h->wintag, h->windows[STP_WIN_IO].winaddr,
	    offset, size, &pcihp->ioh);
	*windowp = 0;
	return (0);
}

void
stp4020_chip_io_unmap(pcmcia_chipset_handle_t pch, int win)
{
}

void
stp4020_chip_socket_enable(pcmcia_chipset_handle_t pch)
{
	struct stp4020_socket *h = (struct stp4020_socket *)pch;
	int i, v;

	h->flags |= STP4020_SOCKET_ENABLING;

	/* this bit is mostly stolen from pcic_attach_card */

	/* Power down the socket to reset it, clear the card reset pin */
	stp4020_wr_sockctl(h, STP4020_ICR1_IDX, 0);

	/*
	 * wait 300ms until power fails (Tpf).  Then, wait 100ms since
	 * we are changing Vcc (Toff).
	 */
	stp4020_delay((300 + 100) * 1000);

	/* Power up the socket */
	v = STP4020_ICR1_MSTPWR;
	stp4020_wr_sockctl(h, STP4020_ICR1_IDX, v);

	/*
	 * wait 100ms until power raise (Tpr) and 20ms to become
	 * stable (Tsu(Vcc)).
	 *
	 * some machines require some more time to be settled
	 * (another 200ms is added here).
	 */
	stp4020_delay((100 + 20 + 200) * 1000);

	v |= STP4020_ICR1_PCIFOE | STP4020_ICR1_VPP1_VCC;
	stp4020_wr_sockctl(h, STP4020_ICR1_IDX, v);

	/*
	 * hold RESET at least 20us.
	 */
	stp4020_wr_sockctl(h, STP4020_ICR0_IDX, 
	    stp4020_rd_sockctl(h, STP4020_ICR0_IDX) | STP4020_ICR0_RESET);
	delay(20);
	stp4020_wr_sockctl(h, STP4020_ICR0_IDX,
	    stp4020_rd_sockctl(h, STP4020_ICR0_IDX) & ~STP4020_ICR0_RESET);

	/* wait 20ms as per pc card standard (r2.01) section 4.3.6 */
	stp4020_delay(20000);

	/* Wait for the chip to finish initializing (5 seconds max) */
	for (i = 10000; i > 0; i--) {
		v = stp4020_rd_sockctl(h, STP4020_ISR0_IDX);
		/* If the card has been removed, abort */
		if ((v & (STP4020_ISR0_CD1ST | STP4020_ISR0_CD2ST)) == 0) {
			h->flags &= ~STP4020_SOCKET_ENABLING;
			return;
		}
		if ((v & STP4020_ISR0_RDYST) != 0)
			break;
		delay(500);
	}
	if (i <= 0) {
#ifdef STP4020_DEBUG
		printf("stp4020_chip_socket_enable: not ready: status %b\n",
		    v, STP4020_ISR0_IOBITS);
#endif
		h->flags &= ~STP4020_SOCKET_ENABLING;
		return;
	}

	v = stp4020_rd_sockctl(h, STP4020_ICR0_IDX);

	/*
	 * Check the card type.
	 * Enable socket I/O interrupts for IO cards.
	 * We use level SB_INT[0] for I/O interrupts.
	 */
	if (pcmcia_card_gettype(h->pcmcia) == PCMCIA_IFTYPE_IO) {
		v &= ~(STP4020_ICR0_IOILVL | STP4020_ICR0_IFTYPE);
		v |= STP4020_ICR0_IFTYPE_IO | STP4020_ICR0_IOIE |
		    STP4020_ICR0_IOILVL_SB0 | STP4020_ICR0_SPKREN;
		h->int_enable = v;
		h->int_disable = v & ~STP4020_ICR0_IOIE;
		DPRINTF(("%s: configuring card for IO usage\n",
		    h->sc->sc_dev.dv_xname));
	} else {
		v &= ~(STP4020_ICR0_IOILVL | STP4020_ICR0_IFTYPE |
		    STP4020_ICR0_SPKREN | STP4020_ICR0_IOIE);
		v |= STP4020_ICR0_IFTYPE_MEM;
		h->int_enable = h->int_disable = v;
		DPRINTF(("%s: configuring card for MEM ONLY usage\n",
		    h->sc->sc_dev.dv_xname));
	}
	stp4020_wr_sockctl(h, STP4020_ICR0_IDX, v);

	h->flags &= ~STP4020_SOCKET_ENABLING;
}

void
stp4020_chip_socket_disable(pcmcia_chipset_handle_t pch)
{
	struct stp4020_socket *h = (struct stp4020_socket *)pch;
	int v;

	/*
	 * Disable socket I/O interrupts.
	 */
	v = stp4020_rd_sockctl(h, STP4020_ICR0_IDX);
	v &= ~(STP4020_ICR0_IOILVL | STP4020_ICR0_IFTYPE |
	    STP4020_ICR0_SPKREN | STP4020_ICR0_IOIE);
	stp4020_wr_sockctl(h, STP4020_ICR0_IDX, v);

	/* Power down the socket */
	stp4020_wr_sockctl(h, STP4020_ICR1_IDX, 0);

	/*
	 * wait 300ms until power fails (Tpf).
	 */
	stp4020_delay(300 * 1000);
}

void *
stp4020_chip_intr_establish(pcmcia_chipset_handle_t pch,
    struct pcmcia_function *pf, int ipl, int (*handler) (void *), void *arg,
    char *xname)
{
	struct stp4020_socket *h = (struct stp4020_socket *)pch;

	/*
	 * Note that this code uses softintr_establish_raw() in order
	 * to use real (hardware) ipl values. All platforms with
	 * SBus support this.
	 */
	h->intrhandler = handler;
	h->intrarg = arg;
	h->softint = softintr_establish_raw(ipl, stp4020_intr_dispatch, h);

	return h->softint != NULL ? h : NULL;
}

void
stp4020_chip_intr_disestablish(pcmcia_chipset_handle_t pch, void *ih)
{
	struct stp4020_socket *h = (struct stp4020_socket *)pch;

	if (h->softint != NULL) {
		softintr_disestablish_raw(h->softint);
		h->softint = NULL;
	}
	h->intrhandler = NULL;
	h->intrarg = NULL;
}

const char *
stp4020_chip_intr_string(pcmcia_chipset_handle_t pch, void *ih)
{
	if (ih == NULL)
		return ("couldn't establish interrupt");
	else
		return ("");	/* nothing for now */
}

/*
 * Delay and possibly yield CPU.
 * XXX - assumes a context
 */
void
stp4020_delay(unsigned int usecs)
{
	int chan;

	if (cold || usecs < tick) {
		delay(usecs);
		return;
	}

#ifdef DEBUG
	if (USEC_TO_NSEC(usecs) > SEC_TO_NSEC(60))
		panic("stp4020: preposterous delay: %uus", usecs);
#endif
	tsleep_nsec(&chan, 0, "stp4020_delay", USEC_TO_NSEC(usecs));
}

#ifdef STP4020_DEBUG
void
stp4020_dump_regs(struct stp4020_socket *h)
{
	/*
	 * Dump control and status registers.
	 */
	printf("socket[%d] registers:\n"
	    "\tICR0=%b\n\tICR1=%b\n\tISR0=%b\n\tISR1=%x\n", h->sock,
	    stp4020_rd_sockctl(h, STP4020_ICR0_IDX), STP4020_ICR0_BITS,
	    stp4020_rd_sockctl(h, STP4020_ICR1_IDX), STP4020_ICR1_BITS,
	    stp4020_rd_sockctl(h, STP4020_ISR0_IDX), STP4020_ISR0_IOBITS,
	    stp4020_rd_sockctl(h, STP4020_ISR1_IDX));
}
#endif /* STP4020_DEBUG */
