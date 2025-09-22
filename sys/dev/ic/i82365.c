/*	$OpenBSD: i82365.c,v 1.40 2021/03/07 06:21:38 jsg Exp $	*/
/*	$NetBSD: i82365.c,v 1.10 1998/06/09 07:36:55 thorpej Exp $	*/

/*
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
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/kthread.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/pcmcia/pcmciareg.h>
#include <dev/pcmcia/pcmciavar.h>

#include <dev/ic/i82365reg.h>
#include <dev/ic/i82365var.h>

#ifdef PCICDEBUG
#define	DPRINTF(arg)	printf arg;
#else
#define	DPRINTF(arg)
#endif

#define	PCIC_VENDOR_UNKNOWN		0
#define	PCIC_VENDOR_I82365SLR0		1
#define	PCIC_VENDOR_I82365SLR1		2
#define	PCIC_VENDOR_I82365SLR2		3
#define	PCIC_VENDOR_CIRRUS_PD6710	4
#define	PCIC_VENDOR_CIRRUS_PD672X	5
#define	PCIC_VENDOR_VADEM_VG468		6
#define	PCIC_VENDOR_VADEM_VG469		7

static char *pcic_vendor_to_string[] = {
	"Unknown",
	"Intel 82365SL rev 0",
	"Intel 82365SL rev 1",
	"Intel 82365SL rev 2",
	"Cirrus PD6710",
	"Cirrus PD672X",
	"Vadem VG468",
	"Vadem VG469",
};

/*
 * Individual drivers will allocate their own memory and io regions. Memory
 * regions must be a multiple of 4k, aligned on a 4k boundary.
 */

#define	PCIC_MEM_ALIGN	PCIC_MEM_PAGESIZE

void	pcic_attach_socket(struct pcic_handle *);
void	pcic_init_socket(struct pcic_handle *);

int	pcic_submatch(struct device *, void *, void *);
int	pcic_print(void *arg, const char *pnp);
int	pcic_intr_socket(struct pcic_handle *);

void	pcic_attach_card(struct pcic_handle *);
void	pcic_detach_card(struct pcic_handle *, int);
void	pcic_deactivate_card(struct pcic_handle *);

void	pcic_chip_do_mem_map(struct pcic_handle *, int);
void	pcic_chip_do_io_map(struct pcic_handle *, int);

void	pcic_create_event_thread(void *);
void	pcic_event_thread(void *);
void	pcic_event_process(struct pcic_handle *, struct pcic_event *);
void	pcic_queue_event(struct pcic_handle *, int);

void	pcic_wait_ready(struct pcic_handle *);

u_int8_t st_pcic_read(struct pcic_handle *, int);
void	st_pcic_write(struct pcic_handle *, int, int);

struct cfdriver pcic_cd = {
	NULL, "pcic", DV_DULL
};

int
pcic_ident_ok(int ident)
{
	/* this is very empirical and heuristic */

	if (ident == 0 || ident == 0xff || (ident & PCIC_IDENT_ZERO))
		return (0);

	if ((ident & PCIC_IDENT_IFTYPE_MASK) != PCIC_IDENT_IFTYPE_MEM_AND_IO) {
#ifdef DEBUG
		printf("pcic: does not support memory and I/O cards, "
		    "ignored (ident=%0x)\n", ident);
#endif
		return (0);
	}
	return (1);
}

int
pcic_vendor(struct pcic_handle *h)
{
	int vendor, reg;

	/*
	 * the chip_id of the cirrus toggles between 11 and 00 after a write.
	 * weird.
	 */

	pcic_write(h, PCIC_CIRRUS_CHIP_INFO, 0);
	reg = pcic_read(h, -1);

	if ((reg & PCIC_CIRRUS_CHIP_INFO_CHIP_ID) ==
	    PCIC_CIRRUS_CHIP_INFO_CHIP_ID) {
		reg = pcic_read(h, -1);
		if ((reg & PCIC_CIRRUS_CHIP_INFO_CHIP_ID) == 0) {
			if (reg & PCIC_CIRRUS_CHIP_INFO_SLOTS)
				return (PCIC_VENDOR_CIRRUS_PD672X);
			else
				return (PCIC_VENDOR_CIRRUS_PD6710);
		}
	}

	reg = pcic_read(h, PCIC_IDENT);

	switch (reg) {
	case PCIC_IDENT_REV_I82365SLR0:
		vendor = PCIC_VENDOR_I82365SLR0;
		break;
	case PCIC_IDENT_REV_I82365SLR1:
		vendor = PCIC_VENDOR_I82365SLR1;
		break;
	case PCIC_IDENT_REV_I82365SLR2:
		vendor = PCIC_VENDOR_I82365SLR2;
		break;
	default:
		vendor = PCIC_VENDOR_UNKNOWN;
		break;
	}

	pcic_write(h, 0x0e, -1);
	pcic_write(h, 0x37, -1);

	reg = pcic_read(h, PCIC_VG468_MISC);
	reg |= PCIC_VG468_MISC_VADEMREV;
	pcic_write(h, PCIC_VG468_MISC, reg);

	reg = pcic_read(h, PCIC_IDENT);

	if (reg & PCIC_IDENT_VADEM_MASK) {
		if ((reg & 7) >= 4)
			vendor = PCIC_VENDOR_VADEM_VG469;
		else
			vendor = PCIC_VENDOR_VADEM_VG468;

		reg = pcic_read(h, PCIC_VG468_MISC);
		reg &= ~PCIC_VG468_MISC_VADEMREV;
		pcic_write(h, PCIC_VG468_MISC, reg);
	}

	return (vendor);
}

void
pcic_attach(struct pcic_softc *sc)
{
	int vendor, count, i, reg;

	/* now check for each controller/socket */

	/*
	 * this could be done with a loop, but it would violate the
	 * abstraction
	 */

	count = 0;

	DPRINTF(("pcic ident regs:"));

	sc->handle[0].ph_parent = (struct device *)sc;
	sc->handle[0].sock = C0SA;
	/* initialise pcic_read and pcic_write functions */
	sc->handle[0].ph_read = st_pcic_read;
	sc->handle[0].ph_write = st_pcic_write;
	sc->handle[0].ph_bus_t = sc->iot;
	sc->handle[0].ph_bus_h = sc->ioh;
	if (pcic_ident_ok(reg = pcic_read(&sc->handle[0], PCIC_IDENT))) {
		sc->handle[0].flags = PCIC_FLAG_SOCKETP;
		count++;
	} else {
		sc->handle[0].flags = 0;
	}
	sc->handle[0].laststate = PCIC_LASTSTATE_EMPTY;

	DPRINTF((" 0x%02x", reg));

	sc->handle[1].ph_parent = (struct device *)sc;
	sc->handle[1].sock = C0SB;
	/* initialise pcic_read and pcic_write functions */
	sc->handle[1].ph_read = st_pcic_read;
	sc->handle[1].ph_write = st_pcic_write;
	sc->handle[1].ph_bus_t = sc->iot;
	sc->handle[1].ph_bus_h = sc->ioh;
	if (pcic_ident_ok(reg = pcic_read(&sc->handle[1], PCIC_IDENT))) {
		sc->handle[1].flags = PCIC_FLAG_SOCKETP;
		count++;
	} else {
		sc->handle[1].flags = 0;
	}
	sc->handle[1].laststate = PCIC_LASTSTATE_EMPTY;

	DPRINTF((" 0x%02x", reg));

	/*
	 * The CL-PD6729 has only one controller and always returns 0
	 * if you try to read from the second one. Maybe pcic_ident_ok
	 * shouldn't accept 0?
	 */
	sc->handle[2].ph_parent = (struct device *)sc;
	sc->handle[2].sock = C1SA;
	/* initialise pcic_read and pcic_write functions */
	sc->handle[2].ph_read = st_pcic_read;
	sc->handle[2].ph_write = st_pcic_write;
	sc->handle[2].ph_bus_t = sc->iot;
	sc->handle[2].ph_bus_h = sc->ioh;
	if (pcic_vendor(&sc->handle[0]) != PCIC_VENDOR_CIRRUS_PD672X ||
	    pcic_read(&sc->handle[2], PCIC_IDENT) != 0) {
		if (pcic_ident_ok(reg = pcic_read(&sc->handle[2],
		    PCIC_IDENT))) {
			sc->handle[2].flags = PCIC_FLAG_SOCKETP;
			count++;
		} else {
			sc->handle[2].flags = 0;
		}
		sc->handle[2].laststate = PCIC_LASTSTATE_EMPTY;

		DPRINTF((" 0x%02x", reg));

		sc->handle[3].ph_parent = (struct device *)sc;
		sc->handle[3].sock = C1SB;
		/* initialise pcic_read and pcic_write functions */
		sc->handle[3].ph_read = st_pcic_read;
		sc->handle[3].ph_write = st_pcic_write;
		sc->handle[3].ph_bus_t = sc->iot;
		sc->handle[3].ph_bus_h = sc->ioh;
		if (pcic_ident_ok(reg = pcic_read(&sc->handle[3],
		    PCIC_IDENT))) {
			sc->handle[3].flags = PCIC_FLAG_SOCKETP;
			count++;
		} else {
			sc->handle[3].flags = 0;
		}
		sc->handle[3].laststate = PCIC_LASTSTATE_EMPTY;

		DPRINTF((" 0x%02x\n", reg));
	} else {
		sc->handle[2].flags = 0;
		sc->handle[3].flags = 0;
	}

	if (count == 0)
		return;

	/* establish the interrupt */

	/* XXX block interrupts? */

	for (i = 0; i < PCIC_NSLOTS; i++) {
		/*
		 * this should work, but w/o it, setting tty flags hangs at
		 * boot time.
		 */
		if (sc->handle[i].flags & PCIC_FLAG_SOCKETP) {
			SIMPLEQ_INIT(&sc->handle[i].events);
			pcic_write(&sc->handle[i], PCIC_CSC_INTR, 0);
			pcic_read(&sc->handle[i], PCIC_CSC);
		}
	}

	for (i = 0; i < PCIC_NSLOTS; i += 2) {
		if ((sc->handle[i+0].flags & PCIC_FLAG_SOCKETP) ||
		    (sc->handle[i+1].flags & PCIC_FLAG_SOCKETP)) {
			vendor = pcic_vendor(&sc->handle[i]);

			printf("%s controller %d: <%s> has socket",
			    sc->dev.dv_xname, i/2,
			    pcic_vendor_to_string[vendor]);

			if ((sc->handle[i+0].flags & PCIC_FLAG_SOCKETP) &&
			    (sc->handle[i+1].flags & PCIC_FLAG_SOCKETP))
				printf("s A and B\n");
			else if (sc->handle[i+0].flags & PCIC_FLAG_SOCKETP)
				printf(" A only\n");
			else
				printf(" B only\n");

			if (sc->handle[i+0].flags & PCIC_FLAG_SOCKETP)
				sc->handle[i+0].vendor = vendor;
			if (sc->handle[i+1].flags & PCIC_FLAG_SOCKETP)
				sc->handle[i+1].vendor = vendor;
		}
	}
}

void
pcic_attach_sockets(struct pcic_softc *sc)
{
	int i;

	for (i = 0; i < PCIC_NSLOTS; i++)
		if (sc->handle[i].flags & PCIC_FLAG_SOCKETP)
			pcic_attach_socket(&sc->handle[i]);
}

void
pcic_attach_socket(struct pcic_handle *h)
{
	struct pcmciabus_attach_args paa;
	struct pcic_softc *sc = (struct pcic_softc *)(h->ph_parent);

	/* initialize the rest of the handle */

	h->shutdown = 0;
	h->memalloc = 0;
	h->ioalloc = 0;
	h->ih_irq = 0;

	/* now, config one pcmcia device per socket */

	paa.paa_busname = "pcmcia";
	paa.pct = (pcmcia_chipset_tag_t) sc->pct;
	paa.pch = (pcmcia_chipset_handle_t) h;
	paa.iobase = sc->iobase;
	paa.iosize = sc->iosize;

	h->pcmcia = config_found_sm(&sc->dev, &paa, pcic_print,
	    pcic_submatch);

	/* if there's actually a pcmcia device attached, initialize the slot */

	if (h->pcmcia)
		pcic_init_socket(h);
	else
		h->flags &= ~PCIC_FLAG_SOCKETP;
}

void
pcic_create_event_thread(void *arg)
{
	struct pcic_handle *h = arg;
	char name[MAXCOMLEN+1];
	const char *cs;

	switch (h->sock) {
	case C0SA:
		cs = "0,0";
		break;
	case C0SB:
		cs = "0,1";
		break;
	case C1SA:
		cs = "1,0";
		break;
	case C1SB:
		cs = "1,1";
		break;
	default:
		panic("pcic_create_event_thread: unknown pcic socket");
	}

	snprintf(name, sizeof name, "%s,%s", h->ph_parent->dv_xname, cs);
	if (kthread_create(pcic_event_thread, h, &h->event_thread, name)) {
		printf("%s: unable to create event thread for sock 0x%02x\n",
		    h->ph_parent->dv_xname, h->sock);
		panic("pcic_create_event_thread");
	}
}

void
pcic_event_thread(void *arg)
{
	struct pcic_handle *h = arg;
	struct pcic_event *pe;
	int s;
	struct pcic_softc *sc = (struct pcic_softc *)(h->ph_parent);

	while (h->shutdown == 0) {
		s = splhigh();
		if ((pe = SIMPLEQ_FIRST(&h->events)) == NULL) {
			splx(s);
			tsleep_nsec(&h->events, PWAIT, "pcicev", INFSLP);
			continue;
		} else {
			splx(s);
			/* sleep .25s to be enqueued chatterling interrupts */
			tsleep_nsec(pcic_event_thread, PWAIT, "pcicss",
			    MSEC_TO_NSEC(250));
		}
		pcic_event_process(h, pe);
	}

	h->event_thread = NULL;

	/* In case parent is waiting for us to exit. */
	wakeup(sc);

	kthread_exit(0);
}

void
pcic_event_process(struct pcic_handle *h, struct pcic_event *pe)
{
	int s;

	s = splhigh();
	SIMPLEQ_REMOVE_HEAD(&h->events, pe_q);
	splx(s);

	switch (pe->pe_type) {
	case PCIC_EVENT_INSERTION:
		s = splhigh();
		while (1) {
			struct pcic_event *pe1, *pe2;

			if ((pe1 = SIMPLEQ_FIRST(&h->events)) == NULL)
				break;
			if (pe1->pe_type != PCIC_EVENT_REMOVAL)
				break;
			if ((pe2 = SIMPLEQ_NEXT(pe1, pe_q)) == NULL)
				break;
			if (pe2->pe_type == PCIC_EVENT_INSERTION) {
				SIMPLEQ_REMOVE_HEAD(&h->events, pe_q);
				free(pe1, M_TEMP, sizeof *pe1);
				SIMPLEQ_REMOVE_HEAD(&h->events, pe_q);
				free(pe2, M_TEMP, sizeof *pe2);
			}
		}
		splx(s);
				
		DPRINTF(("%s: insertion event\n", h->ph_parent->dv_xname));
		pcic_attach_card(h);
		break;

	case PCIC_EVENT_REMOVAL:
		s = splhigh();
		while (1) {
			struct pcic_event *pe1, *pe2;

			if ((pe1 = SIMPLEQ_FIRST(&h->events)) == NULL)
				break;
			if (pe1->pe_type != PCIC_EVENT_INSERTION)
				break;
			if ((pe2 = SIMPLEQ_NEXT(pe1, pe_q)) == NULL)
				break;
			if (pe2->pe_type == PCIC_EVENT_REMOVAL) {
				SIMPLEQ_REMOVE_HEAD(&h->events, pe_q);
				free(pe1, M_TEMP, sizeof *pe1);
				SIMPLEQ_REMOVE_HEAD(&h->events, pe_q);
				free(pe2, M_TEMP, sizeof *pe1);
			}
		}
		splx(s);

		DPRINTF(("%s: removal event\n", h->ph_parent->dv_xname));
		pcic_detach_card(h, DETACH_FORCE);
		break;

	default:
		panic("pcic_event_thread: unknown event %d", pe->pe_type);
	}
	free(pe, M_TEMP, sizeof *pe);
}

void
pcic_init_socket(struct pcic_handle *h)
{
	int reg;
	struct pcic_softc *sc = (struct pcic_softc *)(h->ph_parent);

	/*
	 * queue creation of a kernel thread to handle insert/removal events.
	 */
#ifdef DIAGNOSTIC
	if (h->event_thread != NULL)
		panic("pcic_attach_socket: event thread");
#endif
	kthread_create_deferred(pcic_create_event_thread, h);

	/* set up the card to interrupt on card detect */

	pcic_write(h, PCIC_CSC_INTR, (sc->irq << PCIC_CSC_INTR_IRQ_SHIFT) |
	    PCIC_CSC_INTR_CD_ENABLE);
	pcic_write(h, PCIC_INTR, 0);
	pcic_read(h, PCIC_CSC);

	/* unsleep the cirrus controller */

	if ((h->vendor == PCIC_VENDOR_CIRRUS_PD6710) ||
	    (h->vendor == PCIC_VENDOR_CIRRUS_PD672X)) {
		reg = pcic_read(h, PCIC_CIRRUS_MISC_CTL_2);
		if (reg & PCIC_CIRRUS_MISC_CTL_2_SUSPEND) {
			DPRINTF(("%s: socket %02x was suspended\n",
			    h->ph_parent->dv_xname, h->sock));
			reg &= ~PCIC_CIRRUS_MISC_CTL_2_SUSPEND;
			pcic_write(h, PCIC_CIRRUS_MISC_CTL_2, reg);
		}
	}
	/* if there's a card there, then attach it. */

	reg = pcic_read(h, PCIC_IF_STATUS);

	if ((reg & PCIC_IF_STATUS_CARDDETECT_MASK) ==
	    PCIC_IF_STATUS_CARDDETECT_PRESENT) {
		pcic_attach_card(h);
		h->laststate = PCIC_LASTSTATE_PRESENT;
	} else
		h->laststate = PCIC_LASTSTATE_EMPTY;
}

int
pcic_submatch(struct device *parent, void *match, void *aux)
{
	struct cfdata *cf = match;
	struct pcmciabus_attach_args *paa = aux;
	struct pcic_handle *h = (struct pcic_handle *) paa->pch;

	switch (h->sock) {
	case C0SA:
		if (cf->cf_loc[0 /* PCICCF_CONTROLLER */] !=
		    -1 /* PCICCF_CONTROLLER_DEFAULT */ &&
		    cf->cf_loc[0 /* PCICCF_CONTROLLER */] != 0)
			return 0;
		if (cf->cf_loc[1 /* PCICCF_SOCKET */] !=
		    -1 /* PCICCF_SOCKET_DEFAULT */ &&
		    cf->cf_loc[1 /* PCICCF_SOCKET */] != 0)
			return 0;

		break;
	case C0SB:
		if (cf->cf_loc[0 /* PCICCF_CONTROLLER */] !=
		    -1 /* PCICCF_CONTROLLER_DEFAULT */ &&
		    cf->cf_loc[0 /* PCICCF_CONTROLLER */] != 0)
			return 0;
		if (cf->cf_loc[1 /* PCICCF_SOCKET */] !=
		    -1 /* PCICCF_SOCKET_DEFAULT */ &&
		    cf->cf_loc[1 /* PCICCF_SOCKET */] != 1)
			return 0;

		break;
	case C1SA:
		if (cf->cf_loc[0 /* PCICCF_CONTROLLER */] !=
		    -1 /* PCICCF_CONTROLLER_DEFAULT */ &&
		    cf->cf_loc[0 /* PCICCF_CONTROLLER */] != 1)
			return 0;
		if (cf->cf_loc[1 /* PCICCF_SOCKET */] !=
		    -1 /* PCICCF_SOCKET_DEFAULT */ &&
		    cf->cf_loc[1 /* PCICCF_SOCKET */] != 0)
			return 0;

		break;
	case C1SB:
		if (cf->cf_loc[0 /* PCICCF_CONTROLLER */] !=
		    -1 /* PCICCF_CONTROLLER_DEFAULT */ &&
		    cf->cf_loc[0 /* PCICCF_CONTROLLER */] != 1)
			return 0;
		if (cf->cf_loc[1 /* PCICCF_SOCKET */] !=
		    -1 /* PCICCF_SOCKET_DEFAULT */ &&
		    cf->cf_loc[1 /* PCICCF_SOCKET */] != 1)
			return 0;

		break;
	default:
		panic("unknown pcic socket");
	}

	return ((*cf->cf_attach->ca_match)(parent, cf, aux));
}

int
pcic_print(void *arg, const char *pnp)
{
	struct pcmciabus_attach_args *paa = arg;
	struct pcic_handle *h = (struct pcic_handle *) paa->pch;

	/* Only "pcmcia"s can attach to "pcic"s... easy. */
	if (pnp)
		printf("pcmcia at %s", pnp);

	switch (h->sock) {
	case C0SA:
		printf(" controller 0 socket 0");
		break;
	case C0SB:
		printf(" controller 0 socket 1");
		break;
	case C1SA:
		printf(" controller 1 socket 0");
		break;
	case C1SB:
		printf(" controller 1 socket 1");
		break;
	default:
		panic("unknown pcic socket");
	}

	return (UNCONF);
}

int
pcic_intr(void *arg)
{
	struct pcic_softc *sc = arg;
	int i, ret = 0;

	DPRINTF(("%s: intr\n", sc->dev.dv_xname));

	for (i = 0; i < PCIC_NSLOTS; i++)
		if (sc->handle[i].flags & PCIC_FLAG_SOCKETP)
			ret += pcic_intr_socket(&sc->handle[i]);

	return (ret ? 1 : 0);
}

void
pcic_poll_intr(void *arg)
{
	struct pcic_softc *sc = arg;
	int i, s;

	/*
	 * Since we're polling, we aren't in interrupt context, so block any
	 * actual interrupts coming from the pcic.
	 */
	s = spltty();

	for (i = 0; i < PCIC_NSLOTS; i++)
		if (sc->handle[i].flags & PCIC_FLAG_SOCKETP)
			pcic_intr_socket(&sc->handle[i]);

	timeout_add_msec(&sc->poll_timeout, 500);

	splx(s);
}

int
pcic_intr_socket(struct pcic_handle *h)
{
	int cscreg;

	cscreg = pcic_read(h, PCIC_CSC);

	cscreg &= (PCIC_CSC_GPI |
		   PCIC_CSC_CD |
		   PCIC_CSC_READY |
		   PCIC_CSC_BATTWARN |
		   PCIC_CSC_BATTDEAD);

	if (cscreg & PCIC_CSC_GPI) {
		DPRINTF(("%s: %02x GPI\n", h->ph_parent->dv_xname, h->sock));
	}
	if (cscreg & PCIC_CSC_CD) {
		int statreg;

		statreg = pcic_read(h, PCIC_IF_STATUS);

		DPRINTF(("%s: %02x CD %x\n", h->ph_parent->dv_xname, h->sock,
		    statreg));

		if ((statreg & PCIC_IF_STATUS_CARDDETECT_MASK) ==
		    PCIC_IF_STATUS_CARDDETECT_PRESENT) {
			if (h->laststate != PCIC_LASTSTATE_PRESENT) {
				DPRINTF(("%s: enqueuing INSERTION event\n",
				    h->ph_parent->dv_xname));
				pcic_queue_event(h, PCIC_EVENT_INSERTION);
			}
			h->laststate = PCIC_LASTSTATE_PRESENT;
		} else {
			if (h->laststate == PCIC_LASTSTATE_PRESENT) {
				/* Deactivate the card now. */
				DPRINTF(("%s: deactivating card\n",
				    h->ph_parent->dv_xname));
				pcic_deactivate_card(h);

				DPRINTF(("%s: enqueuing REMOVAL event\n",
				    h->ph_parent->dv_xname));
				pcic_queue_event(h, PCIC_EVENT_REMOVAL);
			}
			h->laststate =
			    ((statreg & PCIC_IF_STATUS_CARDDETECT_MASK) == 0)
			    ? PCIC_LASTSTATE_EMPTY : PCIC_LASTSTATE_HALF;
		}
	}
	if (cscreg & PCIC_CSC_READY) {
		DPRINTF(("%s: %02x READY\n", h->ph_parent->dv_xname, h->sock));
		/* shouldn't happen */
	}
	if (cscreg & PCIC_CSC_BATTWARN) {
		DPRINTF(("%s: %02x BATTWARN\n", h->ph_parent->dv_xname,
		    h->sock));
	}
	if (cscreg & PCIC_CSC_BATTDEAD) {
		DPRINTF(("%s: %02x BATTDEAD\n", h->ph_parent->dv_xname,
		    h->sock));
	}
	return (cscreg ? 1 : 0);
}

void
pcic_queue_event(struct pcic_handle *h, int event)
{
	struct pcic_event *pe;
	int s;

	pe = malloc(sizeof(*pe), M_TEMP, M_NOWAIT);
	if (pe == NULL)
		panic("pcic_queue_event: can't allocate event");

	pe->pe_type = event;
	s = splhigh();
	SIMPLEQ_INSERT_TAIL(&h->events, pe, pe_q);
	splx(s);
	wakeup(&h->events);
}

void
pcic_attach_card(struct pcic_handle *h)
{
	if (h->flags & PCIC_FLAG_CARDP)
		panic("pcic_attach_card: already attached");

	/* call the MI attach function */
	pcmcia_card_attach(h->pcmcia);

	h->flags |= PCIC_FLAG_CARDP;
}

void
pcic_detach_card(struct pcic_handle *h, int flags)
{

	if (h->flags & PCIC_FLAG_CARDP) {
		h->flags &= ~PCIC_FLAG_CARDP;

		/* call the MI detach function */
		pcmcia_card_detach(h->pcmcia, flags);
	} else {
		DPRINTF(("pcic_detach_card: already detached"));
	}
}

void
pcic_deactivate_card(struct pcic_handle *h)
{
	struct device *dev = (struct device *)h->pcmcia;

	/*
	 * At suspend, apm deactivates any connected cards. If we've woken up
	 * to find a previously-connected device missing, and we're detaching
	 * it, we don't want to deactivate it again.
	 */
	if (dev->dv_flags & DVF_ACTIVE)
		pcmcia_card_deactivate(h->pcmcia);

	/* power down the socket */
	pcic_write(h, PCIC_PWRCTL, 0);

	/* reset the socket */
	pcic_write(h, PCIC_INTR, 0);
}

/*
 * The pcic_power() function must execute BEFORE the pcmcia_power() hooks.
 * During suspend, a card may have been ejected. If so, we must detach it
 * completely before pcmcia_power() tries to activate it. Attempting to
 * activate a card that isn't there is bad news.
 */
void
pcic_power(int why, void *arg)
{
	struct pcic_handle *h = (struct pcic_handle *)arg;
	struct pcic_softc *sc = (struct pcic_softc *)h->ph_parent;
	struct pcic_event *pe;

	if (why != DVACT_RESUME) {
		if (sc->poll_established)
			timeout_del(&sc->poll_timeout);
	} else {
		pcic_intr_socket(h);

		while ((pe = SIMPLEQ_FIRST(&h->events)))
			pcic_event_process(h, pe);

		if (sc->poll_established)
			timeout_add_msec(&sc->poll_timeout, 500);
	}
}

int 
pcic_chip_mem_alloc(pcmcia_chipset_handle_t pch, bus_size_t size,
    struct pcmcia_mem_handle *pcmhp)
{
	struct pcic_handle *h = (struct pcic_handle *) pch;
	bus_space_handle_t memh;
	bus_addr_t addr;
	bus_size_t sizepg;
	int i, mask, mhandle;
	struct pcic_softc *sc = (struct pcic_softc *)(h->ph_parent);

	/* out of sc->memh, allocate as many pages as necessary */

	/* convert size to PCIC pages */
	sizepg = (size + (PCIC_MEM_ALIGN - 1)) / PCIC_MEM_ALIGN;
	if (sizepg > PCIC_MAX_MEM_PAGES)
		return (1);

	mask = (1 << sizepg) - 1;

	addr = 0;		/* XXX gcc -Wuninitialized */
	mhandle = 0;		/* XXX gcc -Wuninitialized */

	for (i = 0; i <= PCIC_MAX_MEM_PAGES - sizepg; i++) {
		if ((sc->subregionmask & (mask << i)) == (mask << i)) {
			if (bus_space_subregion(sc->memt, sc->memh,
			    i * PCIC_MEM_PAGESIZE,
			    sizepg * PCIC_MEM_PAGESIZE, &memh))
				return (1);
			mhandle = mask << i;
			addr = sc->membase + (i * PCIC_MEM_PAGESIZE);
			sc->subregionmask &= ~(mhandle);
			pcmhp->memt = sc->memt;
			pcmhp->memh = memh;
			pcmhp->addr = addr;
			pcmhp->size = size;
			pcmhp->mhandle = mhandle;
			pcmhp->realsize = sizepg * PCIC_MEM_PAGESIZE;
	
			DPRINTF(("pcic_chip_mem_alloc bus addr 0x%lx+0x%lx\n",
			    (u_long) addr, (u_long) size));

			return (0);
		}
	}

	return (1);
}

void 
pcic_chip_mem_free(pcmcia_chipset_handle_t pch, struct pcmcia_mem_handle *pcmhp)
{
	struct pcic_handle *h = (struct pcic_handle *) pch;
	struct pcic_softc *sc = (struct pcic_softc *)(h->ph_parent);

	sc->subregionmask |= pcmhp->mhandle;
}

static struct mem_map_index_st {
	int	sysmem_start_lsb;
	int	sysmem_start_msb;
	int	sysmem_stop_lsb;
	int	sysmem_stop_msb;
	int	cardmem_lsb;
	int	cardmem_msb;
	int	memenable;
} mem_map_index[] = {
	{
		PCIC_SYSMEM_ADDR0_START_LSB,
		PCIC_SYSMEM_ADDR0_START_MSB,
		PCIC_SYSMEM_ADDR0_STOP_LSB,
		PCIC_SYSMEM_ADDR0_STOP_MSB,
		PCIC_CARDMEM_ADDR0_LSB,
		PCIC_CARDMEM_ADDR0_MSB,
		PCIC_ADDRWIN_ENABLE_MEM0,
	},
	{
		PCIC_SYSMEM_ADDR1_START_LSB,
		PCIC_SYSMEM_ADDR1_START_MSB,
		PCIC_SYSMEM_ADDR1_STOP_LSB,
		PCIC_SYSMEM_ADDR1_STOP_MSB,
		PCIC_CARDMEM_ADDR1_LSB,
		PCIC_CARDMEM_ADDR1_MSB,
		PCIC_ADDRWIN_ENABLE_MEM1,
	},
	{
		PCIC_SYSMEM_ADDR2_START_LSB,
		PCIC_SYSMEM_ADDR2_START_MSB,
		PCIC_SYSMEM_ADDR2_STOP_LSB,
		PCIC_SYSMEM_ADDR2_STOP_MSB,
		PCIC_CARDMEM_ADDR2_LSB,
		PCIC_CARDMEM_ADDR2_MSB,
		PCIC_ADDRWIN_ENABLE_MEM2,
	},
	{
		PCIC_SYSMEM_ADDR3_START_LSB,
		PCIC_SYSMEM_ADDR3_START_MSB,
		PCIC_SYSMEM_ADDR3_STOP_LSB,
		PCIC_SYSMEM_ADDR3_STOP_MSB,
		PCIC_CARDMEM_ADDR3_LSB,
		PCIC_CARDMEM_ADDR3_MSB,
		PCIC_ADDRWIN_ENABLE_MEM3,
	},
	{
		PCIC_SYSMEM_ADDR4_START_LSB,
		PCIC_SYSMEM_ADDR4_START_MSB,
		PCIC_SYSMEM_ADDR4_STOP_LSB,
		PCIC_SYSMEM_ADDR4_STOP_MSB,
		PCIC_CARDMEM_ADDR4_LSB,
		PCIC_CARDMEM_ADDR4_MSB,
		PCIC_ADDRWIN_ENABLE_MEM4,
	},
};

void 
pcic_chip_do_mem_map(struct pcic_handle *h, int win)
{
	int reg;
	int kind = h->mem[win].kind & ~PCMCIA_WIDTH_MEM_MASK;
	int mem8 =
	    (h->mem[win].kind & PCMCIA_WIDTH_MEM_MASK) == PCMCIA_WIDTH_MEM8
	    || (kind == PCMCIA_MEM_ATTR);

	pcic_write(h, mem_map_index[win].sysmem_start_lsb,
	    (h->mem[win].addr >> PCIC_SYSMEM_ADDRX_SHIFT) & 0xff);
	pcic_write(h, mem_map_index[win].sysmem_start_msb,
	    ((h->mem[win].addr >> (PCIC_SYSMEM_ADDRX_SHIFT + 8)) &
	    PCIC_SYSMEM_ADDRX_START_MSB_ADDR_MASK) |
	    (mem8 ? 0 : PCIC_SYSMEM_ADDRX_START_MSB_DATASIZE_16BIT));

	pcic_write(h, mem_map_index[win].sysmem_stop_lsb,
	    ((h->mem[win].addr + h->mem[win].size) >>
	    PCIC_SYSMEM_ADDRX_SHIFT) & 0xff);
	pcic_write(h, mem_map_index[win].sysmem_stop_msb,
	    (((h->mem[win].addr + h->mem[win].size) >>
	    (PCIC_SYSMEM_ADDRX_SHIFT + 8)) &
	    PCIC_SYSMEM_ADDRX_STOP_MSB_ADDR_MASK) |
	    PCIC_SYSMEM_ADDRX_STOP_MSB_WAIT2);

	pcic_write(h, mem_map_index[win].cardmem_lsb,
	    (h->mem[win].offset >> PCIC_CARDMEM_ADDRX_SHIFT) & 0xff);
	pcic_write(h, mem_map_index[win].cardmem_msb,
	    ((h->mem[win].offset >> (PCIC_CARDMEM_ADDRX_SHIFT + 8)) &
	    PCIC_CARDMEM_ADDRX_MSB_ADDR_MASK) |
	    ((kind == PCMCIA_MEM_ATTR) ?
	    PCIC_CARDMEM_ADDRX_MSB_REGACTIVE_ATTR : 0));

	reg = pcic_read(h, PCIC_ADDRWIN_ENABLE);
	reg |= (mem_map_index[win].memenable | PCIC_ADDRWIN_ENABLE_MEMCS16);
	pcic_write(h, PCIC_ADDRWIN_ENABLE, reg);

#ifdef PCICDEBUG
	{
		int r1, r2, r3, r4, r5, r6;

		r1 = pcic_read(h, mem_map_index[win].sysmem_start_msb);
		r2 = pcic_read(h, mem_map_index[win].sysmem_start_lsb);
		r3 = pcic_read(h, mem_map_index[win].sysmem_stop_msb);
		r4 = pcic_read(h, mem_map_index[win].sysmem_stop_lsb);
		r5 = pcic_read(h, mem_map_index[win].cardmem_msb);
		r6 = pcic_read(h, mem_map_index[win].cardmem_lsb);

		DPRINTF(("pcic_chip_do_mem_map window %d: %02x%02x %02x%02x "
		    "%02x%02x\n", win, r1, r2, r3, r4, r5, r6));
	}
#endif
}

int 
pcic_chip_mem_map(pcmcia_chipset_handle_t pch, int kind, bus_addr_t card_addr,
    bus_size_t size, struct pcmcia_mem_handle *pcmhp, bus_size_t *offsetp,
    int *windowp)
{
	struct pcic_handle *h = (struct pcic_handle *) pch;
	bus_addr_t busaddr;
	long card_offset;
	int i, win;
	struct pcic_softc *sc = (struct pcic_softc *)(h->ph_parent);

	win = -1;
	for (i = 0; i < (sizeof(mem_map_index) / sizeof(mem_map_index[0]));
	    i++) {
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

	if (sc->memt != pcmhp->memt)
		panic("pcic_chip_mem_map memt is bogus");

	busaddr = pcmhp->addr;

	/*
	 * Compute the address offset to the pcmcia address space for the
	 * pcic.  This is intentionally signed.  The masks and shifts below
	 * will cause TRT to happen in the pcic registers.  Deal with making
	 * sure the address is aligned, and return the alignment offset.
	 */

	*offsetp = card_addr % PCIC_MEM_ALIGN;
	card_addr -= *offsetp;

	DPRINTF(("pcic_chip_mem_map window %d bus %lx+%lx+%lx at card addr "
	    "%lx\n", win, (u_long) busaddr, (u_long) * offsetp, (u_long) size,
	    (u_long) card_addr));

	/*
	 * include the offset in the size, and decrement size by one, since
	 * the hw wants start/stop
	 */
	size += *offsetp - 1;

	card_offset = (((long) card_addr) - ((long) busaddr));

	h->mem[win].addr = busaddr;
	h->mem[win].size = size;
	h->mem[win].offset = card_offset;
	h->mem[win].kind = kind;

	pcic_chip_do_mem_map(h, win);

	return (0);
}

void 
pcic_chip_mem_unmap(pcmcia_chipset_handle_t pch, int window)
{
	struct pcic_handle *h = (struct pcic_handle *) pch;
	int reg;

	if (window >= (sizeof(mem_map_index) / sizeof(mem_map_index[0])))
		panic("pcic_chip_mem_unmap: window out of range");

	reg = pcic_read(h, PCIC_ADDRWIN_ENABLE);
	reg &= ~mem_map_index[window].memenable;
	pcic_write(h, PCIC_ADDRWIN_ENABLE, reg);

	h->memalloc &= ~(1 << window);
}

int 
pcic_chip_io_alloc(pcmcia_chipset_handle_t pch, bus_addr_t start,
    bus_size_t size, bus_size_t align, struct pcmcia_io_handle *pcihp)
{
	struct pcic_handle *h = (struct pcic_handle *) pch;
	bus_space_tag_t iot;
	bus_space_handle_t ioh;
	bus_addr_t ioaddr, beg, fin;
	int flags = 0;
	struct pcic_softc *sc = (struct pcic_softc *)(h->ph_parent);
	struct pcic_ranges *range;

	/*
	 * Allocate some arbitrary I/O space.
	 */

	iot = sc->iot;

	if (start) {
		ioaddr = start;
		if (bus_space_map(iot, start, size, 0, &ioh))
			return (1);
		DPRINTF(("pcic_chip_io_alloc map port %lx+%lx\n",
		    (u_long)ioaddr, (u_long)size));
	} else if (sc->ranges) {
		flags |= PCMCIA_IO_ALLOCATED;

 		/*
		 * In this case, we know the "size" and "align" that
		 * we want.  So we need to start walking down
		 * sc->ranges, searching for a similar space that
		 * is (1) large enough for the size and alignment
		 * (2) then we need to try to allocate
		 * (3) if it fails to allocate, we try next range.
		 *
		 * We must also check that the start/size of each
		 * allocation we are about to do is within the bounds
		 * of "sc->iobase" and "sc->iosize".
		 * (Some pcmcia controllers handle a 12 bits of addressing,
		 * but we want to use the same range structure)
		 */
		for (range = sc->ranges; range->start; range++) {
			/* Potentially trim the range because of bounds. */
			beg = max(range->start, sc->iobase);
			fin = min(range->start + range->len,
			    sc->iobase + sc->iosize);

			/* Short-circuit easy cases. */
			if (fin < beg || fin - beg < size)
				continue;

			/*
			 * This call magically fulfills our alignment
			 * requirements.
			 */
			DPRINTF(("pcic_chip_io_alloc beg-fin %lx-%lx\n",
			    (u_long)beg, (u_long)fin));
			if (bus_space_alloc(iot, beg, fin, size, align, 0, 0,
			    &ioaddr, &ioh) == 0)
				break;
		}
		if (range->start == 0)
			return (1);
		DPRINTF(("pcic_chip_io_alloc alloc port %lx+%lx\n",
		    (u_long)ioaddr, (u_long)size));

	} else {
		flags |= PCMCIA_IO_ALLOCATED;
		if (bus_space_alloc(iot, sc->iobase,
		    sc->iobase + sc->iosize, size, align, 0, 0,
		    &ioaddr, &ioh))
			return (1);
		DPRINTF(("pcic_chip_io_alloc alloc port %lx+%lx\n",
		    (u_long)ioaddr, (u_long)size));
	}

	pcihp->iot = iot;
	pcihp->ioh = ioh;
	pcihp->addr = ioaddr;
	pcihp->size = size;
	pcihp->flags = flags;

	return (0);
}

void 
pcic_chip_io_free(pcmcia_chipset_handle_t pch, struct pcmcia_io_handle *pcihp)
{
	bus_space_tag_t iot = pcihp->iot;
	bus_space_handle_t ioh = pcihp->ioh;
	bus_size_t size = pcihp->size;

	if (pcihp->flags & PCMCIA_IO_ALLOCATED)
		bus_space_free(iot, ioh, size);
	else
		bus_space_unmap(iot, ioh, size);
}


static struct io_map_index_st {
	int	start_lsb;
	int	start_msb;
	int	stop_lsb;
	int	stop_msb;
	int	ioenable;
	int	ioctlmask;
	int	ioctlbits[3];		/* indexed by PCMCIA_WIDTH_* */
}               io_map_index[] = {
	{
		PCIC_IOADDR0_START_LSB,
		PCIC_IOADDR0_START_MSB,
		PCIC_IOADDR0_STOP_LSB,
		PCIC_IOADDR0_STOP_MSB,
		PCIC_ADDRWIN_ENABLE_IO0,
		PCIC_IOCTL_IO0_WAITSTATE | PCIC_IOCTL_IO0_ZEROWAIT |
		PCIC_IOCTL_IO0_IOCS16SRC_MASK | PCIC_IOCTL_IO0_DATASIZE_MASK,
		{
			PCIC_IOCTL_IO0_IOCS16SRC_CARD,
			PCIC_IOCTL_IO0_IOCS16SRC_DATASIZE |
			    PCIC_IOCTL_IO0_DATASIZE_8BIT,
			PCIC_IOCTL_IO0_IOCS16SRC_DATASIZE |
			    PCIC_IOCTL_IO0_DATASIZE_16BIT,
		},
	},
	{
		PCIC_IOADDR1_START_LSB,
		PCIC_IOADDR1_START_MSB,
		PCIC_IOADDR1_STOP_LSB,
		PCIC_IOADDR1_STOP_MSB,
		PCIC_ADDRWIN_ENABLE_IO1,
		PCIC_IOCTL_IO1_WAITSTATE | PCIC_IOCTL_IO1_ZEROWAIT |
		PCIC_IOCTL_IO1_IOCS16SRC_MASK | PCIC_IOCTL_IO1_DATASIZE_MASK,
		{
			PCIC_IOCTL_IO1_IOCS16SRC_CARD,
			PCIC_IOCTL_IO1_IOCS16SRC_DATASIZE |
			    PCIC_IOCTL_IO1_DATASIZE_8BIT,
			PCIC_IOCTL_IO1_IOCS16SRC_DATASIZE |
			    PCIC_IOCTL_IO1_DATASIZE_16BIT,
		},
	},
};

void 
pcic_chip_do_io_map(struct pcic_handle *h, int win)
{
	int reg;

	DPRINTF(("pcic_chip_do_io_map win %d addr %lx size %lx width %d\n",
	    win, (long) h->io[win].addr, (long) h->io[win].size,
	    h->io[win].width * 8));

	pcic_write(h, io_map_index[win].start_lsb, h->io[win].addr & 0xff);
	pcic_write(h, io_map_index[win].start_msb,
	    (h->io[win].addr >> 8) & 0xff);

	pcic_write(h, io_map_index[win].stop_lsb,
	    (h->io[win].addr + h->io[win].size - 1) & 0xff);
	pcic_write(h, io_map_index[win].stop_msb,
	    ((h->io[win].addr + h->io[win].size - 1) >> 8) & 0xff);

	reg = pcic_read(h, PCIC_IOCTL);
	reg &= ~io_map_index[win].ioctlmask;
	reg |= io_map_index[win].ioctlbits[h->io[win].width];
	pcic_write(h, PCIC_IOCTL, reg);

	reg = pcic_read(h, PCIC_ADDRWIN_ENABLE);
	reg |= io_map_index[win].ioenable;
	pcic_write(h, PCIC_ADDRWIN_ENABLE, reg);
}

int 
pcic_chip_io_map(pcmcia_chipset_handle_t pch, int width, bus_addr_t offset,
    bus_size_t size, struct pcmcia_io_handle *pcihp, int *windowp)
{
	struct pcic_handle *h = (struct pcic_handle *) pch;
	bus_addr_t ioaddr = pcihp->addr + offset;
	int i, win;
#ifdef PCICDEBUG
	static char *width_names[] = { "auto", "io8", "io16" };
#endif
	struct pcic_softc *sc = (struct pcic_softc *)(h->ph_parent);

	/* XXX Sanity check offset/size. */

	win = -1;
	for (i = 0; i < (sizeof(io_map_index) / sizeof(io_map_index[0])); i++) {
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

	if (sc->iot != pcihp->iot)
		panic("pcic_chip_io_map iot is bogus");

	DPRINTF(("pcic_chip_io_map window %d %s port %lx+%lx\n",
		 win, width_names[width], (u_long) ioaddr, (u_long) size));

	h->io[win].addr = ioaddr;
	h->io[win].size = size;
	h->io[win].width = width;

	pcic_chip_do_io_map(h, win);

	return (0);
}

void 
pcic_chip_io_unmap(pcmcia_chipset_handle_t pch, int window)
{
	struct pcic_handle *h = (struct pcic_handle *) pch;
	int reg;

	if (window >= (sizeof(io_map_index) / sizeof(io_map_index[0])))
		panic("pcic_chip_io_unmap: window out of range");

	reg = pcic_read(h, PCIC_ADDRWIN_ENABLE);
	reg &= ~io_map_index[window].ioenable;
	pcic_write(h, PCIC_ADDRWIN_ENABLE, reg);

	h->ioalloc &= ~(1 << window);
}

void
pcic_wait_ready(struct pcic_handle *h)
{
	int i;

	for (i = 0; i < 10000; i++) {
		if (pcic_read(h, PCIC_IF_STATUS) & PCIC_IF_STATUS_READY)
			return;
		delay(500);
#ifdef PCICDEBUG
			if ((i>5000) && (i%100 == 99))
				printf(".");
#endif
	}

#ifdef DIAGNOSTIC
	printf("pcic_wait_ready: ready never happened, status = %02x\n",
	    pcic_read(h, PCIC_IF_STATUS));
#endif
}

void
pcic_chip_socket_enable(pcmcia_chipset_handle_t pch)
{
	struct pcic_handle *h = (struct pcic_handle *) pch;
	int cardtype, reg, win;

	/* this bit is mostly stolen from pcic_attach_card */

	/* power down the socket to reset it, clear the card reset pin */

	pcic_write(h, PCIC_PWRCTL, 0);

	/* 
	 * wait 300ms until power fails (Tpf).  Then, wait 100ms since
	 * we are changing Vcc (Toff).
	 */
	delay((300 + 100) * 1000);

	if (h->vendor == PCIC_VENDOR_VADEM_VG469) {
		reg = pcic_read(h, PCIC_VG469_VSELECT);
		reg &= ~PCIC_VG469_VSELECT_VCC;
		pcic_write(h, PCIC_VG469_VSELECT, reg);
	}

	/* power up the socket */

	pcic_write(h, PCIC_PWRCTL, PCIC_PWRCTL_DISABLE_RESETDRV |
	    PCIC_PWRCTL_PWR_ENABLE);

	/*
	 * wait 100ms until power raise (Tpr) and 20ms to become
	 * stable (Tsu(Vcc)).
	 *
	 * some machines require some more time to be settled
	 * (another 200ms is added here).
	 */
	delay((100 + 20 + 200) * 1000);

	pcic_write(h, PCIC_PWRCTL, PCIC_PWRCTL_DISABLE_RESETDRV |
	    PCIC_PWRCTL_OE | PCIC_PWRCTL_PWR_ENABLE);
	pcic_write(h, PCIC_INTR, 0);

	/*
	 * hold RESET at least 10us.
	 */
	delay(10);

	/* clear the reset flag */

	pcic_write(h, PCIC_INTR, PCIC_INTR_RESET);

	/* wait 20ms as per pc card standard (r2.01) section 4.3.6 */

	delay(20000);

	/* wait for the chip to finish initializing */

#ifdef DIAGNOSTIC
	reg = pcic_read(h, PCIC_IF_STATUS);
	if (!(reg & PCIC_IF_STATUS_POWERACTIVE)) {
		printf("pcic_chip_socket_enable: status %x\n", reg);
	}
#endif

	pcic_wait_ready(h);

	/* zero out the address windows */

	pcic_write(h, PCIC_ADDRWIN_ENABLE, 0);

	/* set the card type */

	cardtype = pcmcia_card_gettype(h->pcmcia);

	reg = pcic_read(h, PCIC_INTR);
	reg &= ~PCIC_INTR_CARDTYPE_MASK;
	reg |= ((cardtype == PCMCIA_IFTYPE_IO) ?
		PCIC_INTR_CARDTYPE_IO :
		PCIC_INTR_CARDTYPE_MEM);
	reg |= h->ih_irq;
	pcic_write(h, PCIC_INTR, reg);

	DPRINTF(("%s: pcic_chip_socket_enable %02x cardtype %s %02x\n",
	    h->ph_parent->dv_xname, h->sock,
	    ((cardtype == PCMCIA_IFTYPE_IO) ? "io" : "mem"), reg));

	/* reinstall all the memory and io mappings */

	for (win = 0; win < PCIC_MEM_WINS; win++)
		if (h->memalloc & (1 << win))
			pcic_chip_do_mem_map(h, win);

	for (win = 0; win < PCIC_IO_WINS; win++)
		if (h->ioalloc & (1 << win))
			pcic_chip_do_io_map(h, win);
}

void
pcic_chip_socket_disable(pcmcia_chipset_handle_t pch)
{
	struct pcic_handle *h = (struct pcic_handle *) pch;

	DPRINTF(("pcic_chip_socket_disable\n"));

	/* power down the socket */

	pcic_write(h, PCIC_PWRCTL, 0);

	/*
	 * wait 300ms until power fails (Tpf).
	 */
	delay(300 * 1000);
}

u_int8_t
st_pcic_read(struct pcic_handle *h, int idx)
{
	if (idx != -1)
		bus_space_write_1(h->ph_bus_t, h->ph_bus_h, PCIC_REG_INDEX,
		    h->sock + idx);
	return bus_space_read_1(h->ph_bus_t, h->ph_bus_h, PCIC_REG_DATA);
}

void
st_pcic_write(struct pcic_handle *h, int idx, int data)
{
	if (idx != -1)
		bus_space_write_1(h->ph_bus_t, h->ph_bus_h, PCIC_REG_INDEX,
		    h->sock + idx);
	if (data != -1)
		bus_space_write_1(h->ph_bus_t, h->ph_bus_h, PCIC_REG_DATA,
		    data);
}
