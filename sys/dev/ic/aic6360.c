/*	$OpenBSD: aic6360.c,v 1.41 2024/09/04 07:54:52 mglocker Exp $	*/
/*	$NetBSD: aic6360.c,v 1.52 1996/12/10 21:27:51 thorpej Exp $	*/

#ifdef DDB
#define	integrate
#else
#define	integrate	static inline
#endif

/*
 * Copyright (c) 1994, 1995, 1996 Charles Hannum.  All rights reserved.
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
 *	This product includes software developed by Charles M. Hannum.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Copyright (c) 1994 Jarle Greipsland
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 */

/*
 * Acknowledgements: Many of the algorithms used in this driver are
 * inspired by the work of Julian Elischer (julian@tfs.com) and
 * Charles Hannum (mycroft@duality.gnu.ai.mit.edu).  Thanks a million!
 */

/* TODO list:
 * 1) Get the DMA stuff working.
 * 2) Get the iov/uio stuff working. Is this a good thing ???
 * 3) Get the synch stuff working.
 * 4) Rewrite it to use malloc for the acb structs instead of static alloc.?
 */

/*
 * A few customizable items:
 */

/* Use doubleword transfers to/from SCSI chip.  Note: This requires
 * motherboard support.  Basically, some motherboard chipsets are able to
 * split a 32 bit I/O operation into two 16 bit I/O operations,
 * transparently to the processor.  This speeds up some things, notably long
 * data transfers.
 */
#define AIC_USE_DWORDS		0

/* Synchronous data transfers? */
#define AIC_USE_SYNCHRONOUS	0
#define AIC_SYNC_REQ_ACK_OFS 	8

/* Wide data transfers? */
#define	AIC_USE_WIDE		0
#define	AIC_MAX_WIDTH		0

/* Max attempts made to transmit a message */
#define AIC_MSG_MAX_ATTEMPT	3 /* Not used now XXX */

/* Use DMA (else we do programmed I/O using string instructions) (not yet!)*/
#define AIC_USE_EISA_DMA	0
#define AIC_USE_ISA_DMA		0

/* How to behave on the (E)ISA bus when/if DMAing (on<<4) + off in us */
#define EISA_BRST_TIM ((15<<4) + 1)	/* 15us on, 1us off */

/* Some spin loop parameters (essentially how long to wait some places)
 * The problem(?) is that sometimes we expect either to be able to transmit a
 * byte or to get a new one from the SCSI bus pretty soon.  In order to avoid
 * returning from the interrupt just to get yanked back for the next byte we
 * may spin in the interrupt routine waiting for this byte to come.  How long?
 * This is really (SCSI) device and processor dependent.  Tuneable, I guess.
 */
#define AIC_MSGIN_SPIN		1 	/* Spin upto ?ms for a new msg byte */
#define AIC_MSGOUT_SPIN		1

/* Include debug functions?  At the end of this file there are a bunch of
 * functions that will print out various information regarding queued SCSI
 * commands, driver state and chip contents.  You can call them from the
 * kernel debugger.  If you set AIC_DEBUG to 0 they are not included (the
 * kernel uses less memory) but you lose the debugging facilities.
 */
#ifndef SMALL_KERNEL
#define AIC_DEBUG		1
#endif

#define	AIC_ABORT_TIMEOUT	2000	/* time to wait for abort */

/* End of customizable parameters */

#if AIC_USE_EISA_DMA || AIC_USE_ISA_DMA
#error "I said not yet! Start paying attention... grumble"
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/device.h>
#include <sys/buf.h>
#include <sys/queue.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <scsi/scsi_all.h>
#include <scsi/scsi_message.h>
#include <scsi/scsiconf.h>

#include <dev/isa/isavar.h>

#include <dev/ic/aic6360reg.h>
#include <dev/ic/aic6360var.h>

#ifndef DDB
#define	db_enter() panic("should call debugger here (aic6360.c)")
#endif /* ! DDB */

#ifdef AIC_DEBUG
int aic_debug = 0x00; /* AIC_SHOWSTART|AIC_SHOWMISC|AIC_SHOWTRACE; */
#endif

void 	aic_init(struct aic_softc *);
void	aic_done(struct aic_softc *, struct aic_acb *);
void	aic_dequeue(struct aic_softc *, struct aic_acb *);
void	aic_scsi_cmd(struct scsi_xfer *);
int	aic_poll(struct aic_softc *, struct scsi_xfer *, int);
integrate void	aic_sched_msgout(struct aic_softc *, u_char);
integrate void	aic_setsync(struct aic_softc *, struct aic_tinfo *);
void	aic_select(struct aic_softc *, struct aic_acb *);
void	aic_timeout(void *);
void	aic_sched(struct aic_softc *);
void	aic_scsi_reset(struct aic_softc *);
void	aic_reset(struct aic_softc *);
void	aic_acb_free(void *, void *);
void	*aic_acb_alloc(void *);
int	aic_reselect(struct aic_softc *, int);
void	aic_sense(struct aic_softc *, struct aic_acb *);
void	aic_msgin(struct aic_softc *);
void	aic_abort(struct aic_softc *, struct aic_acb *);
void	aic_msgout(struct aic_softc *);
int	aic_dataout_pio(struct aic_softc *, u_char *, int);
int	aic_datain_pio(struct aic_softc *, u_char *, int);
#ifdef AIC_DEBUG
void	aic_print_acb(struct aic_acb *);
void	aic_dump_driver(struct aic_softc *);
void	aic_dump6360(struct aic_softc *);
void	aic_show_scsi_cmd(struct aic_acb *);
void	aic_print_active_acb(void);
#endif

struct cfdriver aic_cd = {
	NULL, "aic", DV_DULL
};

const struct scsi_adapter aic_switch = {
	aic_scsi_cmd, NULL, NULL, NULL, NULL
};

/*
 * Do the real search-for-device.
 */
int
aic_find(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	char chip_id[sizeof(IDSTRING)];	/* For chips that support it */
	int i;

	/* Remove aic6360 from possible powerdown mode */
	bus_space_write_1(iot, ioh, DMACNTRL0, 0);

	/* Thanks to mark@aggregate.com for the new method for detecting
	 * whether the chip is present or not.  Bonus: may also work for
	 * the AIC-6260!
 	 */
	AIC_TRACE(("aic: probing for aic-chip\n"));
 	/*
 	 * Linux also init's the stack to 1-16 and then clears it,
     	 *  6260's don't appear to have an ID reg - mpg
 	 */
	/* Push the sequence 0,1,..,15 on the stack */
#define STSIZE 16
	bus_space_write_1(iot, ioh, DMACNTRL1, 0); /* Reset stack pointer */
	for (i = 0; i < STSIZE; i++)
		bus_space_write_1(iot, ioh, STACK, i);

	/* See if we can pull out the same sequence */
	bus_space_write_1(iot, ioh, DMACNTRL1, 0);
 	for (i = 0; i < STSIZE && bus_space_read_1(iot, ioh, STACK) == i; i++)
		;
	if (i != STSIZE) {
		AIC_START(("STACK futzed at %d.\n", i));
		return (0);
	}

	/* See if we can pull the id string out of the ID register,
	 * now only used for informational purposes.
	 */
	bzero(chip_id, sizeof(chip_id));
	bus_space_read_multi_1(iot, ioh, ID, chip_id, sizeof(IDSTRING) - 1);
	AIC_START(("AIC ID: %s ", chip_id));
	AIC_START(("chip revision %d\n",
	    (int)bus_space_read_1(iot, ioh, REV)));

	return (1);
}

/*
 * Attach the AIC6360, fill out some high and low level data structures
 */
void
aicattach(struct aic_softc *sc)
{
	struct scsibus_attach_args saa;
	AIC_TRACE(("aicattach  "));
	sc->sc_state = AIC_INIT;

	sc->sc_initiator = 7;
	sc->sc_freq = 20;	/* XXXX assume 20 MHz. */

	/*
	 * These are the bounds of the sync period, based on the frequency of
	 * the chip's clock input and the size and offset of the sync period
	 * register.
	 *
	 * For a 20MHz clock, this gives us 25, or 100nS, or 10MB/s, as a
	 * maximum transfer rate, and 112.5, or 450nS, or 2.22MB/s, as a
	 * minimum transfer rate.
	 */
	sc->sc_minsync = (2 * 250) / sc->sc_freq;
	sc->sc_maxsync = (9 * 250) / sc->sc_freq;

	aic_init(sc);	/* init chip and driver */

	saa.saa_adapter_softc = sc;
	saa.saa_adapter_target = sc->sc_initiator;
	saa.saa_adapter = &aic_switch;
	saa.saa_luns = saa.saa_adapter_buswidth = 8;
	saa.saa_openings = 2;
	saa.saa_pool = &sc->sc_iopool;
	saa.saa_quirks = saa.saa_flags = 0;
	saa.saa_wwpn = saa.saa_wwnn = 0;

	config_found(&sc->sc_dev, &saa, scsiprint);
}

int
aic_detach(struct device *self, int flags)
{
	struct aic_softc *sc = (struct aic_softc *) self;
	int rv = 0;

	rv = config_detach_children(&sc->sc_dev, flags);

	return (rv);
}

/* Initialize AIC6360 chip itself
 * The following conditions should hold:
 * aicprobe should have succeeded, i.e. the ioh handle in aic_softc must
 * be valid.
 */
void
aic_reset(struct aic_softc *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;

	/*
	 * Doc. recommends to clear these two registers before operations
	 * commence
	 */
	bus_space_write_1(iot, ioh, SCSITEST, 0);
	bus_space_write_1(iot, ioh, TEST, 0);

	/* Reset SCSI-FIFO and abort any transfers */
	bus_space_write_1(iot, ioh, SXFRCTL0, CHEN | CLRCH | CLRSTCNT);

	/* Reset DMA-FIFO */
	bus_space_write_1(iot, ioh, DMACNTRL0, RSTFIFO);
	bus_space_write_1(iot, ioh, DMACNTRL1, 0);

	/* Disable all selection features */
	bus_space_write_1(iot, ioh, SCSISEQ, 0);
	bus_space_write_1(iot, ioh, SXFRCTL1, 0);

	/* Disable some interrupts */
	bus_space_write_1(iot, ioh, SIMODE0, 0x00);
	/* Clear a slew of interrupts */
	bus_space_write_1(iot, ioh, CLRSINT0, 0x7f);

	/* Disable some more interrupts */
	bus_space_write_1(iot, ioh, SIMODE1, 0x00);
	/* Clear another slew of interrupts */
	bus_space_write_1(iot, ioh, CLRSINT1, 0xef);

	/* Disable synchronous transfers */
	bus_space_write_1(iot, ioh, SCSIRATE, 0);

	/* Haven't seen ant errors (yet) */
	bus_space_write_1(iot, ioh, CLRSERR, 0x07);

	/* Set our SCSI-ID */
	bus_space_write_1(iot, ioh, SCSIID, sc->sc_initiator << OID_S);
	bus_space_write_1(iot, ioh, BRSTCNTRL, EISA_BRST_TIM);
}

/* Pull the SCSI RST line for 500 us */
void
aic_scsi_reset(struct aic_softc *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;

	bus_space_write_1(iot, ioh, SCSISEQ, SCSIRSTO);
	delay(500);
	bus_space_write_1(iot, ioh, SCSISEQ, 0);
	delay(50);
}

/*
 * Initialize aic SCSI driver.
 */
void
aic_init(struct aic_softc *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	struct aic_acb *acb;
	int r;

	aic_reset(sc);
	aic_scsi_reset(sc);
	aic_reset(sc);

	if (sc->sc_state == AIC_INIT) {
		/* First time through; initialize. */
		TAILQ_INIT(&sc->ready_list);
		TAILQ_INIT(&sc->nexus_list);
		TAILQ_INIT(&sc->free_list);
		mtx_init(&sc->sc_acb_mtx, IPL_BIO);
		scsi_iopool_init(&sc->sc_iopool, sc, aic_acb_alloc,
		    aic_acb_free);
		sc->sc_nexus = NULL;
		acb = sc->sc_acb;
		bzero(acb, sizeof(sc->sc_acb));
		for (r = 0; r < sizeof(sc->sc_acb) / sizeof(*acb); r++) {
			TAILQ_INSERT_TAIL(&sc->free_list, acb, chain);
			acb++;
		}
		bzero(&sc->sc_tinfo, sizeof(sc->sc_tinfo));
	} else {
		/* Cancel any active commands. */
		sc->sc_state = AIC_CLEANING;
		if ((acb = sc->sc_nexus) != NULL) {
			acb->xs->error = XS_DRIVER_STUFFUP;
			timeout_del(&acb->xs->stimeout);
			aic_done(sc, acb);
		}
		while ((acb = TAILQ_FIRST(&sc->nexus_list)) != NULL) {
			acb->xs->error = XS_DRIVER_STUFFUP;
			timeout_del(&acb->xs->stimeout);
			aic_done(sc, acb);
		}
	}

	sc->sc_prevphase = PH_INVALID;
	for (r = 0; r < 8; r++) {
		struct aic_tinfo *ti = &sc->sc_tinfo[r];

		ti->flags = 0;
#if AIC_USE_SYNCHRONOUS
		ti->flags |= DO_SYNC;
		ti->period = sc->sc_minsync;
		ti->offset = AIC_SYNC_REQ_ACK_OFS;
#else
		ti->period = ti->offset = 0;
#endif
#if AIC_USE_WIDE
		ti->flags |= DO_WIDE;
		ti->width = AIC_MAX_WIDTH;
#else
		ti->width = 0;
#endif
	}

	sc->sc_state = AIC_IDLE;
	bus_space_write_1(iot, ioh, DMACNTRL0, INTEN);
}

void
aic_acb_free(void *xsc, void *xacb)
{
	struct aic_softc *sc = xsc;
	struct aic_acb *acb = xacb;

	mtx_enter(&sc->sc_acb_mtx);
	acb->flags = 0;
	TAILQ_INSERT_HEAD(&sc->free_list, acb, chain);
	mtx_leave(&sc->sc_acb_mtx);
}

void *
aic_acb_alloc(void *xsc)
{
	struct aic_softc *sc = xsc;
	struct aic_acb *acb;

	mtx_enter(&sc->sc_acb_mtx);
	acb = TAILQ_FIRST(&sc->free_list);
	if (acb) {
		TAILQ_REMOVE(&sc->free_list, acb, chain);
		acb->flags |= ACB_ALLOC;
	}
	mtx_leave(&sc->sc_acb_mtx);

	return acb;
}

/*
 * DRIVER FUNCTIONS CALLABLE FROM HIGHER LEVEL DRIVERS
 */

/*
 * Expected sequence:
 * 1) Command inserted into ready list
 * 2) Command selected for execution
 * 3) Command won arbitration and has selected target device
 * 4) Send message out (identify message, eventually also sync.negotiations)
 * 5) Send command
 * 5a) Receive disconnect message, disconnect.
 * 5b) Reselected by target
 * 5c) Receive identify message from target.
 * 6) Send or receive data
 * 7) Receive status
 * 8) Receive message (command complete etc.)
 * 9) If status == SCSI_CHECK construct a synthetic request sense SCSI cmd.
 *    Repeat 2-8 (no disconnects please...)
 */

/*
 * Start a SCSI-command
 * This function is called by the higher level SCSI-driver to queue/run
 * SCSI-commands.
 */
void
aic_scsi_cmd(struct scsi_xfer *xs)
{
	struct scsi_link *sc_link = xs->sc_link;
	struct aic_softc *sc = sc_link->bus->sb_adapter_softc;
	struct aic_acb *acb;
	int s, flags;

	AIC_TRACE(("aic_scsi_cmd  "));
	AIC_CMDS(("[0x%x, %d]->%d ", (int)xs->cmd.opcode, xs->cmdlen,
	    sc_link->target));

	flags = xs->flags;
	acb = xs->io;

	/* Initialize acb */
	acb->xs = xs;
	acb->timeout = xs->timeout;
	timeout_set(&xs->stimeout, aic_timeout, acb);

	if (xs->flags & SCSI_RESET) {
		acb->flags |= ACB_RESET;
		acb->scsi_cmd_length = 0;
		acb->data_length = 0;
	} else {
		bcopy(&xs->cmd, &acb->scsi_cmd, xs->cmdlen);
		acb->scsi_cmd_length = xs->cmdlen;
		acb->data_addr = xs->data;
		acb->data_length = xs->datalen;
	}
	acb->target_stat = 0;

	s = splbio();

	TAILQ_INSERT_TAIL(&sc->ready_list, acb, chain);
	if (sc->sc_state == AIC_IDLE)
		aic_sched(sc);

	splx(s);

	if ((flags & SCSI_POLL) == 0)
		return;

	/* Not allowed to use interrupts, use polling instead */
	if (aic_poll(sc, xs, acb->timeout)) {
		aic_timeout(acb);
		if (aic_poll(sc, xs, acb->timeout))
			aic_timeout(acb);
	}
}

/*
 * Used when interrupt driven I/O isn't allowed, e.g. during boot.
 */
int
aic_poll(struct aic_softc *sc, struct scsi_xfer *xs, int count)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	int s;

	AIC_TRACE(("aic_poll  "));
	while (count) {
		/*
		 * If we had interrupts enabled, would we
		 * have got an interrupt?
		 */
		if ((bus_space_read_1(iot, ioh, DMASTAT) & INTSTAT) != 0) {
			s = splbio();
			aicintr(sc);
			splx(s);
		}
		if ((xs->flags & ITSDONE) != 0)
			return 0;
		delay(1000);
		count--;
	}
	return 1;
}

/*
 * LOW LEVEL SCSI UTILITIES
 */

integrate void
aic_sched_msgout(struct aic_softc *sc, u_char m)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;

	if (sc->sc_msgpriq == 0)
		bus_space_write_1(iot, ioh, SCSISIG, sc->sc_phase | ATNO);
	sc->sc_msgpriq |= m;
}

/*
 * Set synchronous transfer offset and period.
 */
integrate void
aic_setsync(struct aic_softc *sc, struct aic_tinfo *ti)
{
#if AIC_USE_SYNCHRONOUS
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;

	if (ti->offset != 0)
		bus_space_write_1(iot, ioh, SCSIRATE,
		    ((ti->period * sc->sc_freq) / 250 - 2) << 4 | ti->offset);
	else
		bus_space_write_1(iot, ioh, SCSIRATE, 0);
#endif
}

/*
 * Start a selection.  This is used by aic_sched() to select an idle target,
 * and by aic_done() to immediately reselect a target to get sense information.
 */
void
aic_select(struct aic_softc *sc, struct aic_acb *acb)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	struct scsi_link *sc_link = acb->xs->sc_link;
	int target = sc_link->target;
	struct aic_tinfo *ti = &sc->sc_tinfo[target];

	bus_space_write_1(iot, ioh, SCSIID,
	    sc->sc_initiator << OID_S | target);
	aic_setsync(sc, ti);
	bus_space_write_1(iot, ioh, SXFRCTL1, STIMO_256ms | ENSTIMER);

	/* Always enable reselections. */
	bus_space_write_1(iot, ioh, SIMODE0, ENSELDI | ENSELDO);
	bus_space_write_1(iot, ioh, SIMODE1, ENSCSIRST | ENSELTIMO);
	bus_space_write_1(iot, ioh, SCSISEQ, ENRESELI | ENSELO | ENAUTOATNO);

	sc->sc_state = AIC_SELECTING;
}

int
aic_reselect(struct aic_softc *sc, int message)
{
	u_char selid, target, lun;
	struct aic_acb *acb;
	struct scsi_link *sc_link;
	struct aic_tinfo *ti;

	/*
	 * The SCSI chip made a snapshot of the data bus while the reselection
	 * was being negotiated.  This enables us to determine which target did
	 * the reselect.
	 */
	selid = sc->sc_selid & ~(1 << sc->sc_initiator);
	if (selid & (selid - 1)) {
		printf("%s: reselect with invalid selid %02x; ",
		    sc->sc_dev.dv_xname, selid);
		printf("sending DEVICE RESET\n");
		AIC_BREAK();
		goto reset;
	}

	/* Search wait queue for disconnected cmd
	 * The list should be short, so I haven't bothered with
	 * any more sophisticated structures than a simple
	 * singly linked list.
	 */
	target = ffs(selid) - 1;
	lun = message & 0x07;
	TAILQ_FOREACH(acb, &sc->nexus_list, chain) {
		sc_link = acb->xs->sc_link;
		if (sc_link->target == target && sc_link->lun == lun)
			break;
	}
	if (acb == NULL) {
		printf("%s: reselect from target %d lun %d with no nexus; ",
		    sc->sc_dev.dv_xname, target, lun);
		printf("sending ABORT\n");
		AIC_BREAK();
		goto abort;
	}

	/* Make this nexus active again. */
	TAILQ_REMOVE(&sc->nexus_list, acb, chain);
	sc->sc_state = AIC_CONNECTED;
	sc->sc_nexus = acb;
	ti = &sc->sc_tinfo[target];
	ti->lubusy |= (1 << lun);
	aic_setsync(sc, ti);

	if (acb->flags & ACB_RESET)
		aic_sched_msgout(sc, SEND_DEV_RESET);
	else if (acb->flags & ACB_ABORT)
		aic_sched_msgout(sc, SEND_ABORT);

	/* Do an implicit RESTORE POINTERS. */
	sc->sc_dp = acb->data_addr;
	sc->sc_dleft = acb->data_length;
	sc->sc_cp = (u_char *)&acb->scsi_cmd;
	sc->sc_cleft = acb->scsi_cmd_length;

	return (0);

reset:
	aic_sched_msgout(sc, SEND_DEV_RESET);
	return (1);

abort:
	aic_sched_msgout(sc, SEND_ABORT);
	return (1);
}

/*
 * Schedule a SCSI operation.  This has now been pulled out of the interrupt
 * handler so that we may call it from aic_scsi_cmd and aic_done.  This may
 * save us an unnecessary interrupt just to get things going.  Should only be
 * called when state == AIC_IDLE and at bio pl.
 */
void
aic_sched(struct aic_softc *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	struct aic_acb *acb;
	struct scsi_link *sc_link;
	struct aic_tinfo *ti;

	/*
	 * Find first acb in ready queue that is for a target/lunit pair that
	 * is not busy.
	 */
	bus_space_write_1(iot, ioh, CLRSINT1,
	    CLRSELTIMO | CLRBUSFREE | CLRSCSIPERR);
	TAILQ_FOREACH(acb, &sc->ready_list, chain) {
		sc_link = acb->xs->sc_link;
		ti = &sc->sc_tinfo[sc_link->target];
		if ((ti->lubusy & (1 << sc_link->lun)) == 0) {
			AIC_MISC(("selecting %d:%d  ",
			    sc_link->target, sc_link->lun));
			TAILQ_REMOVE(&sc->ready_list, acb, chain);
			sc->sc_nexus = acb;
			aic_select(sc, acb);
			return;
		} else
			AIC_MISC(("%d:%d busy\n",
			    sc_link->target, sc_link->lun));
	}
	AIC_MISC(("idle  "));
	/* Nothing to start; just enable reselections and wait. */
	bus_space_write_1(iot, ioh, SIMODE0, ENSELDI);
	bus_space_write_1(iot, ioh, SIMODE1, ENSCSIRST);
	bus_space_write_1(iot, ioh, SCSISEQ, ENRESELI);
}

void
aic_sense(struct aic_softc *sc, struct aic_acb *acb)
{
	struct scsi_xfer *xs = acb->xs;
	struct scsi_link *sc_link = xs->sc_link;
	struct aic_tinfo *ti = &sc->sc_tinfo[sc_link->target];
	struct scsi_sense *ss = (void *)&acb->scsi_cmd;

	AIC_MISC(("requesting sense  "));
	/* Next, setup a request sense command block */
	bzero(ss, sizeof(*ss));
	ss->opcode = REQUEST_SENSE;
	ss->byte2 = sc_link->lun << 5;
	ss->length = sizeof(struct scsi_sense_data);
	acb->scsi_cmd_length = sizeof(*ss);
	acb->data_addr = (char *)&xs->sense;
	acb->data_length = sizeof(struct scsi_sense_data);
	acb->flags |= ACB_SENSE;
	ti->senses++;
	if (acb->flags & ACB_NEXUS)
		ti->lubusy &= ~(1 << sc_link->lun);
	if (acb == sc->sc_nexus) {
		aic_select(sc, acb);
	} else {
		aic_dequeue(sc, acb);
		TAILQ_INSERT_HEAD(&sc->ready_list, acb, chain);
		if (sc->sc_state == AIC_IDLE)
			aic_sched(sc);
	}
}

/*
 * POST PROCESSING OF SCSI_CMD (usually current)
 */
void
aic_done(struct aic_softc *sc, struct aic_acb *acb)
{
	struct scsi_xfer *xs = acb->xs;
	struct scsi_link *sc_link = xs->sc_link;
	struct aic_tinfo *ti = &sc->sc_tinfo[sc_link->target];

	AIC_TRACE(("aic_done  "));

	/*
	 * Now, if we've come here with no error code, i.e. we've kept the
	 * initial XS_NOERROR, and the status code signals that we should
	 * check sense, we'll need to set up a request sense cmd block and
	 * push the command back into the ready queue *before* any other
	 * commands for this target/lunit, else we lose the sense info.
	 * We don't support chk sense conditions for the request sense cmd.
	 */
	if (xs->error == XS_NOERROR) {
		if (acb->flags & ACB_ABORT) {
			xs->error = XS_DRIVER_STUFFUP;
		} else if (acb->flags & ACB_SENSE) {
			xs->error = XS_SENSE;
		} else if (acb->target_stat == SCSI_CHECK) {
			/* First, save the return values */
			xs->resid = acb->data_length;
			xs->status = acb->target_stat;
			aic_sense(sc, acb);
			return;
		} else {
			xs->resid = acb->data_length;
		}
	}

#ifdef AIC_DEBUG
	if ((aic_debug & AIC_SHOWMISC) != 0) {
		if (xs->resid != 0)
			printf("resid=%lu ", (u_long)xs->resid);
		if (xs->error == XS_SENSE)
			printf("sense=0x%02x\n", xs->sense.error_code);
		else
			printf("error=%d\n", xs->error);
	}
#endif

	/*
	 * Remove the ACB from whatever queue it happens to be on.
	 */
	if (acb->flags & ACB_NEXUS)
		ti->lubusy &= ~(1 << sc_link->lun);
	if (acb == sc->sc_nexus) {
		sc->sc_nexus = NULL;
		sc->sc_state = AIC_IDLE;
		aic_sched(sc);
	} else
		aic_dequeue(sc, acb);

	ti->cmds++;
	scsi_done(xs);
}

void
aic_dequeue(struct aic_softc *sc, struct aic_acb *acb)
{

	if (acb->flags & ACB_NEXUS) {
		TAILQ_REMOVE(&sc->nexus_list, acb, chain);
	} else {
		TAILQ_REMOVE(&sc->ready_list, acb, chain);
	}
}

/*
 * INTERRUPT/PROTOCOL ENGINE
 */

/*
 * Precondition:
 * The SCSI bus is already in the MSGI phase and there is a message byte
 * on the bus, along with an asserted REQ signal.
 */
void
aic_msgin(struct aic_softc *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	u_char sstat1;
	int n;

	AIC_TRACE(("aic_msgin  "));

	if (sc->sc_prevphase == PH_MSGIN) {
		/* This is a continuation of the previous message. */
		n = sc->sc_imp - sc->sc_imess;
		goto nextbyte;
	}

	/* This is a new MESSAGE IN phase.  Clean up our state. */
	sc->sc_flags &= ~AIC_DROP_MSGIN;

nextmsg:
	n = 0;
	sc->sc_imp = &sc->sc_imess[n];

nextbyte:
	/*
	 * Read a whole message, but don't ack the last byte.  If we reject the
	 * message, we have to assert ATN during the message transfer phase
	 * itself.
	 */
	for (;;) {
		for (;;) {
			sstat1 = bus_space_read_1(iot, ioh, SSTAT1);
			if ((sstat1 & (REQINIT | PHASECHG | BUSFREE)) != 0)
				break;
			/* Wait for REQINIT.  XXX Need timeout. */
		}
		if ((sstat1 & (PHASECHG | BUSFREE)) != 0) {
			/*
			 * Target left MESSAGE IN, probably because it
			 * a) noticed our ATN signal, or
			 * b) ran out of messages.
			 */
			goto out;
		}

		/* If parity error, just dump everything on the floor. */
		if ((sstat1 & SCSIPERR) != 0) {
			sc->sc_flags |= AIC_DROP_MSGIN;
			aic_sched_msgout(sc, SEND_PARITY_ERROR);
		}

		/* Gather incoming message bytes if needed. */
		if ((sc->sc_flags & AIC_DROP_MSGIN) == 0) {
			if (n >= AIC_MAX_MSG_LEN) {
				(void) bus_space_read_1(iot, ioh, SCSIDAT);
				sc->sc_flags |= AIC_DROP_MSGIN;
				aic_sched_msgout(sc, SEND_REJECT);
			} else {
				*sc->sc_imp++ = bus_space_read_1(iot, ioh,
				    SCSIDAT);
				n++;
				/*
				 * This testing is suboptimal, but most
				 * messages will be of the one byte variety, so
				 * it should not affect performance
				 * significantly.
				 */
				if (n == 1 && IS1BYTEMSG(sc->sc_imess[0]))
					break;
				if (n == 2 && IS2BYTEMSG(sc->sc_imess[0]))
					break;
				if (n >= 3 && ISEXTMSG(sc->sc_imess[0]) &&
				    n == sc->sc_imess[1] + 2)
					break;
			}
		} else
			(void) bus_space_read_1(iot, ioh, SCSIDAT);

		/*
		 * If we reach this spot we're either:
		 * a) in the middle of a multi-byte message, or
		 * b) dropping bytes.
		 */
		bus_space_write_1(iot, ioh, SXFRCTL0, CHEN | SPIOEN);
		/* Ack the last byte read. */
		(void) bus_space_read_1(iot, ioh, SCSIDAT);
		bus_space_write_1(iot, ioh, SXFRCTL0, CHEN);
		while ((bus_space_read_1(iot, ioh, SCSISIG) & ACKI) != 0)
			;
	}

	AIC_MISC(("n=%d imess=0x%02x  ", n, sc->sc_imess[0]));

	/* We now have a complete message.  Parse it. */
	switch (sc->sc_state) {
		struct aic_acb *acb;
		struct scsi_link *sc_link;
		struct aic_tinfo *ti;

	case AIC_CONNECTED:
		AIC_ASSERT(sc->sc_nexus != NULL);
		acb = sc->sc_nexus;
		ti = &sc->sc_tinfo[acb->xs->sc_link->target];

		switch (sc->sc_imess[0]) {
		case MSG_CMDCOMPLETE:
			if ((long)sc->sc_dleft < 0) {
				sc_link = acb->xs->sc_link;
				printf("%s: %lu extra bytes from %d:%d\n",
				    sc->sc_dev.dv_xname, (u_long)-sc->sc_dleft,
				    sc_link->target, sc_link->lun);
				acb->data_length = 0;
			}
			acb->xs->resid = acb->data_length = sc->sc_dleft;
			sc->sc_state = AIC_CMDCOMPLETE;
			break;

		case MSG_PARITY_ERROR:
			/* Resend the last message. */
			aic_sched_msgout(sc, sc->sc_lastmsg);
			break;

		case MSG_MESSAGE_REJECT:
			AIC_MISC(("message rejected %02x  ", sc->sc_lastmsg));
			switch (sc->sc_lastmsg) {
#if AIC_USE_SYNCHRONOUS + AIC_USE_WIDE
			case SEND_IDENTIFY:
				ti->flags &= ~(DO_SYNC | DO_WIDE);
				ti->period = ti->offset = 0;
				aic_setsync(sc, ti);
				ti->width = 0;
				break;
#endif
#if AIC_USE_SYNCHRONOUS
			case SEND_SDTR:
				ti->flags &= ~DO_SYNC;
				ti->period = ti->offset = 0;
				aic_setsync(sc, ti);
				break;
#endif
#if AIC_USE_WIDE
			case SEND_WDTR:
				ti->flags &= ~DO_WIDE;
				ti->width = 0;
				break;
#endif
			case SEND_INIT_DET_ERR:
				aic_sched_msgout(sc, SEND_ABORT);
				break;
			}
			break;

		case MSG_NOOP:
			break;

		case MSG_DISCONNECT:
			ti->dconns++;
			sc->sc_state = AIC_DISCONNECT;
			break;

		case MSG_SAVEDATAPOINTER:
			acb->data_addr = sc->sc_dp;
			acb->data_length = sc->sc_dleft;
			break;

		case MSG_RESTOREPOINTERS:
			sc->sc_dp = acb->data_addr;
			sc->sc_dleft = acb->data_length;
			sc->sc_cp = (u_char *)&acb->scsi_cmd;
			sc->sc_cleft = acb->scsi_cmd_length;
			break;

		case MSG_EXTENDED:
			switch (sc->sc_imess[2]) {
#if AIC_USE_SYNCHRONOUS
			case MSG_EXT_SDTR:
				if (sc->sc_imess[1] != 3)
					goto reject;
				ti->period = sc->sc_imess[3];
				ti->offset = sc->sc_imess[4];
				ti->flags &= ~DO_SYNC;
				if (ti->offset == 0) {
				} else if (ti->period < sc->sc_minsync ||
				    ti->period > sc->sc_maxsync ||
				    ti->offset > 8) {
					ti->period = ti->offset = 0;
					aic_sched_msgout(sc, SEND_SDTR);
				} else {
					sc_print_addr(acb->xs->sc_link);
					printf("sync, offset %d, ",
					    ti->offset);
					printf("period %dnsec\n",
					    ti->period * 4);
				}
				aic_setsync(sc, ti);
				break;
#endif

#if AIC_USE_WIDE
			case MSG_EXT_WDTR:
				if (sc->sc_imess[1] != 2)
					goto reject;
				ti->width = sc->sc_imess[3];
				ti->flags &= ~DO_WIDE;
				if (ti->width == 0) {
				} else if (ti->width > AIC_MAX_WIDTH) {
					ti->width = 0;
					aic_sched_msgout(sc, SEND_WDTR);
				} else {
					sc_print_addr(acb->xs->sc_link);
					printf("wide, width %d\n",
					    1 << (3 + ti->width));
				}
				break;
#endif

			default:
				printf("%s: unrecognized MESSAGE EXTENDED; ",
				    sc->sc_dev.dv_xname);
				printf("sending REJECT\n");
				AIC_BREAK();
				goto reject;
			}
			break;

		default:
			printf("%s: unrecognized MESSAGE; sending REJECT\n",
			    sc->sc_dev.dv_xname);
			AIC_BREAK();
		reject:
			aic_sched_msgout(sc, SEND_REJECT);
			break;
		}
		break;

	case AIC_RESELECTED:
		if (!MSG_ISIDENTIFY(sc->sc_imess[0])) {
			printf("%s: reselect without IDENTIFY; ",
			    sc->sc_dev.dv_xname);
			printf("sending DEVICE RESET\n");
			AIC_BREAK();
			goto reset;
		}

		(void) aic_reselect(sc, sc->sc_imess[0]);
		break;

	default:
		printf("%s: unexpected MESSAGE IN; sending DEVICE RESET\n",
		    sc->sc_dev.dv_xname);
		AIC_BREAK();
	reset:
		aic_sched_msgout(sc, SEND_DEV_RESET);
		break;

#ifdef notdef
	abort:
		aic_sched_msgout(sc, SEND_ABORT);
		break;
#endif
	}

	bus_space_write_1(iot, ioh, SXFRCTL0, CHEN | SPIOEN);
	/* Ack the last message byte. */
	(void) bus_space_read_1(iot, ioh, SCSIDAT);
	bus_space_write_1(iot, ioh, SXFRCTL0, CHEN);
	while ((bus_space_read_1(iot, ioh, SCSISIG) & ACKI) != 0)
		;

	/* Go get the next message, if any. */
	goto nextmsg;

out:
	AIC_MISC(("n=%d imess=0x%02x  ", n, sc->sc_imess[0]));
}

/*
 * Send the highest priority, scheduled message.
 */
void
aic_msgout(struct aic_softc *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
#if AIC_USE_SYNCHRONOUS
	struct aic_tinfo *ti;
#endif
	u_char sstat1;
	int n;

	AIC_TRACE(("aic_msgout  "));

	/* Reset the FIFO. */
	bus_space_write_1(iot, ioh, DMACNTRL0, RSTFIFO);
	/* Enable REQ/ACK protocol. */
	bus_space_write_1(iot, ioh, SXFRCTL0, CHEN | SPIOEN);

	if (sc->sc_prevphase == PH_MSGOUT) {
		if (sc->sc_omp == sc->sc_omess) {
			/*
			 * This is a retransmission.
			 *
			 * We get here if the target stayed in MESSAGE OUT
			 * phase.  Section 5.1.9.2 of the SCSI 2 spec indicates
			 * that all of the previously transmitted messages must
			 * be sent again, in the same order.  Therefore, we
			 * requeue all the previously transmitted messages, and
			 * start again from the top.  Our simple priority
			 * scheme keeps the messages in the right order.
			 */
			AIC_MISC(("retransmitting  "));
			sc->sc_msgpriq |= sc->sc_msgoutq;
			/*
			 * Set ATN.  If we're just sending a trivial 1-byte
			 * message, we'll clear ATN later on anyway.
			 */
			bus_space_write_1(iot, ioh, SCSISIG, PH_MSGOUT | ATNO);
		} else {
			/* This is a continuation of the previous message. */
			n = sc->sc_omp - sc->sc_omess;
			goto nextbyte;
		}
	}

	/* No messages transmitted so far. */
	sc->sc_msgoutq = 0;
	sc->sc_lastmsg = 0;

nextmsg:
	/* Pick up highest priority message. */
	sc->sc_currmsg = sc->sc_msgpriq & -sc->sc_msgpriq;
	sc->sc_msgpriq &= ~sc->sc_currmsg;
	sc->sc_msgoutq |= sc->sc_currmsg;

	/* Build the outgoing message data. */
	switch (sc->sc_currmsg) {
	case SEND_IDENTIFY:
		AIC_ASSERT(sc->sc_nexus != NULL);
		sc->sc_omess[0] =
		    MSG_IDENTIFY(sc->sc_nexus->xs->sc_link->lun, 1);
		n = 1;
		break;

#if AIC_USE_SYNCHRONOUS
	case SEND_SDTR:
		AIC_ASSERT(sc->sc_nexus != NULL);
		ti = &sc->sc_tinfo[sc->sc_nexus->xs->sc_link->target];
		sc->sc_omess[4] = MSG_EXTENDED;
		sc->sc_omess[3] = 3;
		sc->sc_omess[2] = MSG_EXT_SDTR;
		sc->sc_omess[1] = ti->period >> 2;
		sc->sc_omess[0] = ti->offset;
		n = 5;
		break;
#endif

#if AIC_USE_WIDE
	case SEND_WDTR:
		AIC_ASSERT(sc->sc_nexus != NULL);
		ti = &sc->sc_tinfo[sc->sc_nexus->xs->sc_link->target];
		sc->sc_omess[3] = MSG_EXTENDED;
		sc->sc_omess[2] = 2;
		sc->sc_omess[1] = MSG_EXT_WDTR;
		sc->sc_omess[0] = ti->width;
		n = 4;
		break;
#endif

	case SEND_DEV_RESET:
		sc->sc_flags |= AIC_ABORTING;
		sc->sc_omess[0] = MSG_BUS_DEV_RESET;
		n = 1;
		break;

	case SEND_REJECT:
		sc->sc_omess[0] = MSG_MESSAGE_REJECT;
		n = 1;
		break;

	case SEND_PARITY_ERROR:
		sc->sc_omess[0] = MSG_PARITY_ERROR;
		n = 1;
		break;

	case SEND_INIT_DET_ERR:
		sc->sc_omess[0] = MSG_INITIATOR_DET_ERR;
		n = 1;
		break;

	case SEND_ABORT:
		sc->sc_flags |= AIC_ABORTING;
		sc->sc_omess[0] = MSG_ABORT;
		n = 1;
		break;

	default:
		printf("%s: unexpected MESSAGE OUT; sending NOOP\n",
		    sc->sc_dev.dv_xname);
		AIC_BREAK();
		sc->sc_omess[0] = MSG_NOOP;
		n = 1;
		break;
	}
	sc->sc_omp = &sc->sc_omess[n];

nextbyte:
	/* Send message bytes. */
	for (;;) {
		for (;;) {
			sstat1 = bus_space_read_1(iot, ioh, SSTAT1);
			if ((sstat1 & (REQINIT | PHASECHG | BUSFREE)) != 0)
				break;
			/* Wait for REQINIT.  XXX Need timeout. */
		}
		if ((sstat1 & (PHASECHG | BUSFREE)) != 0) {
			/*
			 * Target left MESSAGE OUT, possibly to reject
			 * our message.
			 *
			 * If this is the last message being sent, then we
			 * deassert ATN, since either the target is going to
			 * ignore this message, or it's going to ask for a
			 * retransmission via MESSAGE PARITY ERROR (in which
			 * case we reassert ATN anyway).
			 */
			if (sc->sc_msgpriq == 0)
				bus_space_write_1(iot, ioh, CLRSINT1, CLRATNO);
			goto out;
		}

		/* Clear ATN before last byte if this is the last message. */
		if (n == 1 && sc->sc_msgpriq == 0)
			bus_space_write_1(iot, ioh, CLRSINT1, CLRATNO);
		/* Send message byte. */
		bus_space_write_1(iot, ioh, SCSIDAT, *--sc->sc_omp);
		--n;
		/* Keep track of the last message we've sent any bytes of. */
		sc->sc_lastmsg = sc->sc_currmsg;
		/* Wait for ACK to be negated.  XXX Need timeout. */
		while ((bus_space_read_1(iot, ioh, SCSISIG) & ACKI) != 0)
			;

		if (n == 0)
			break;
	}

	/* We get here only if the entire message has been transmitted. */
	if (sc->sc_msgpriq != 0) {
		/* There are more outgoing messages. */
		goto nextmsg;
	}

	/*
	 * The last message has been transmitted.  We need to remember the last
	 * message transmitted (in case the target switches to MESSAGE IN phase
	 * and sends a MESSAGE REJECT), and the list of messages transmitted
	 * this time around (in case the target stays in MESSAGE OUT phase to
	 * request a retransmit).
	 */

out:
	/* Disable REQ/ACK protocol. */
	bus_space_write_1(iot, ioh, SXFRCTL0, CHEN);
}

/* aic_dataout_pio: perform a data transfer using the FIFO datapath in the aic6360
 * Precondition: The SCSI bus should be in the DOUT phase, with REQ asserted
 * and ACK deasserted (i.e. waiting for a data byte).
 * This new revision has been optimized (I tried) to make the common case fast,
 * and the rarer cases (as a result) somewhat more complex.
 */
int
aic_dataout_pio(struct aic_softc *sc, u_char *p, int n)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	u_char dmastat = 0;
	int out = 0;
#define DOUTAMOUNT 128		/* Full FIFO */

	AIC_MISC(("%02x%02x  ", bus_space_read_1(iot, ioh, FIFOSTAT),
	    bus_space_read_1(iot, ioh, SSTAT2)));

	/* Clear host FIFO and counter. */
	bus_space_write_1(iot, ioh, DMACNTRL0, RSTFIFO | WRITE);
	/* Enable FIFOs. */
	bus_space_write_1(iot, ioh, DMACNTRL0, ENDMA | DWORDPIO | WRITE);
	bus_space_write_1(iot, ioh, SXFRCTL0, SCSIEN | DMAEN | CHEN);

	/* Turn off ENREQINIT for now. */
	bus_space_write_1(iot, ioh, SIMODE1,
	    ENSCSIRST | ENSCSIPERR | ENBUSFREE | ENPHASECHG);

	/* I have tried to make the main loop as tight as possible.  This
	 * means that some of the code following the loop is a bit more
	 * complex than otherwise.
	 */
	while (n > 0) {
		for (;;) {
			dmastat = bus_space_read_1(iot, ioh, DMASTAT);
			if ((dmastat & (DFIFOEMP | INTSTAT)) != 0)
				break;
		}

		if ((dmastat & INTSTAT) != 0)
			goto phasechange;

		if (n >= DOUTAMOUNT) {
			n -= DOUTAMOUNT;
			out += DOUTAMOUNT;

#if AIC_USE_DWORDS
			bus_space_write_multi_4(iot, ioh, DMADATALONG,
			    (u_int32_t *)p, DOUTAMOUNT >> 2);
#else
			bus_space_write_multi_2(iot, ioh, DMADATA,
			    (u_int16_t *)p, DOUTAMOUNT >> 1);
#endif

			p += DOUTAMOUNT;
		} else {
			int xfer;

			xfer = n;
			AIC_MISC(("%d> ", xfer));

			n -= xfer;
			out += xfer;

#if AIC_USE_DWORDS
			if (xfer >= 12) {
				bus_space_write_multi_4(iot, ioh, DMADATALONG,
				    (u_int32_t *)p, xfer >> 2);
				p += xfer & ~3;
				xfer &= 3;
			}
#else
			if (xfer >= 8) {
				bus_space_write_multi_2(iot, ioh,  DMADATA,
				    (u_int16_t *)p, xfer >> 1);
				p += xfer & ~1;
				xfer &= 1;
			}
#endif

			if (xfer > 0) {
				bus_space_write_1(iot, ioh, DMACNTRL0,
				    ENDMA | B8MODE | WRITE);
				bus_space_write_multi_1(iot, ioh,  DMADATA, p,
				    xfer);
				p += xfer;
				bus_space_write_1(iot, ioh, DMACNTRL0,
				    ENDMA | DWORDPIO | WRITE);
			}
		}
	}

	if (out == 0) {
		bus_space_write_1(iot, ioh, SXFRCTL1, BITBUCKET);
		for (;;) {
			if ((bus_space_read_1(iot, ioh, DMASTAT) & INTSTAT) !=
			    0)
				break;
		}
		bus_space_write_1(iot, ioh, SXFRCTL1, 0);
		AIC_MISC(("extra data  "));
	} else {
		/* See the bytes off chip */
		for (;;) {
			dmastat = bus_space_read_1(iot, ioh, DMASTAT);
			if ((dmastat & INTSTAT) != 0)
				goto phasechange;
			if ((dmastat & DFIFOEMP) != 0 &&
			    (bus_space_read_1(iot, ioh, SSTAT2) & SEMPTY) != 0)
				break;
		}
	}

phasechange:
	if ((dmastat & INTSTAT) != 0) {
		/* Some sort of phase change. */
		int amount;

		/* Stop transfers, do some accounting */
		amount = bus_space_read_1(iot, ioh, FIFOSTAT) +
		    (bus_space_read_1(iot, ioh, SSTAT2) & 15);
		if (amount > 0) {
			out -= amount;
			bus_space_write_1(iot, ioh, DMACNTRL0,
			    RSTFIFO | WRITE);
			bus_space_write_1(iot, ioh, SXFRCTL0, CHEN | CLRCH);
			AIC_MISC(("+%d ", amount));
		}
	}

	/* Turn on ENREQINIT again. */
	bus_space_write_1(iot, ioh, SIMODE1,
	    ENSCSIRST | ENSCSIPERR | ENBUSFREE | ENREQINIT | ENPHASECHG);

	/* Stop the FIFO data path. */
	bus_space_write_1(iot, ioh, SXFRCTL0, CHEN);
	bus_space_write_1(iot, ioh, DMACNTRL0, 0);

	return out;
}

/* aic_datain_pio: perform data transfers using the FIFO datapath in the aic6360
 * Precondition: The SCSI bus should be in the DIN phase, with REQ asserted
 * and ACK deasserted (i.e. at least one byte is ready).
 * For now, uses a pretty dumb algorithm, hangs around until all data has been
 * transferred.  This, is OK for fast targets, but not so smart for slow
 * targets which don't disconnect or for huge transfers.
 */
int
aic_datain_pio(struct aic_softc *sc, u_char *p, int n)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	u_char dmastat;
	int in = 0;
#define DINAMOUNT 128		/* Full FIFO */

	AIC_MISC(("%02x%02x  ", bus_space_read_1(iot, ioh, FIFOSTAT),
	    bus_space_read_1(iot, ioh, SSTAT2)));

	/* Clear host FIFO and counter. */
	bus_space_write_1(iot, ioh, DMACNTRL0, RSTFIFO);
	/* Enable FIFOs. */
	bus_space_write_1(iot, ioh, DMACNTRL0, ENDMA | DWORDPIO);
	bus_space_write_1(iot, ioh, SXFRCTL0, SCSIEN | DMAEN | CHEN);

	/* Turn off ENREQINIT for now. */
	bus_space_write_1(iot, ioh, SIMODE1,
	    ENSCSIRST | ENSCSIPERR | ENBUSFREE | ENPHASECHG);

	/* We leave this loop if one or more of the following is true:
	 * a) phase != PH_DATAIN && FIFOs are empty
	 * b) SCSIRSTI is set (a reset has occurred) or busfree is detected.
	 */
	while (n > 0) {
		/* Wait for fifo half full or phase mismatch */
		for (;;) {
			dmastat = bus_space_read_1(iot, ioh, DMASTAT);
			if ((dmastat & (DFIFOFULL | INTSTAT)) != 0)
				break;
		}

		if ((dmastat & DFIFOFULL) != 0) {
			n -= DINAMOUNT;
			in += DINAMOUNT;

#if AIC_USE_DWORDS
			bus_space_read_multi_4(iot, ioh, DMADATALONG,
			    (u_int32_t *)p, DINAMOUNT >> 2);
#else
			bus_space_read_multi_2(iot, ioh, DMADATA,
			    (u_int16_t *)p, DINAMOUNT >> 1);
#endif

			p += DINAMOUNT;
		} else {
			int xfer;

			xfer = min(bus_space_read_1(iot, ioh, FIFOSTAT), n);
			AIC_MISC((">%d ", xfer));

			n -= xfer;
			in += xfer;

#if AIC_USE_DWORDS
			if (xfer >= 12) {
				bus_space_read_multi_4(iot, ioh, DMADATALONG,
				    (u_int32_t *)p, xfer >> 2);
				p += xfer & ~3;
				xfer &= 3;
			}
#else
			if (xfer >= 8) {
				bus_space_read_multi_2(iot, ioh, DMADATA,
				    (u_int16_t *)p, xfer >> 1);
				p += xfer & ~1;
				xfer &= 1;
			}
#endif

			if (xfer > 0) {
				bus_space_write_1(iot, ioh, DMACNTRL0,
				    ENDMA | B8MODE);
				bus_space_read_multi_1(iot, ioh, DMADATA, p,
				    xfer);
				p += xfer;
				bus_space_write_1(iot, ioh, DMACNTRL0,
				    ENDMA | DWORDPIO);
			}
		}

		if ((dmastat & INTSTAT) != 0)
			goto phasechange;
	}

	/* Some SCSI-devices are rude enough to transfer more data than what
	 * was requested, e.g. 2048 bytes from a CD-ROM instead of the
	 * requested 512.  Test for progress, i.e. real transfers.  If no real
	 * transfers have been performed (n is probably already zero) and the
	 * FIFO is not empty, waste some bytes....
	 */
	if (in == 0) {
		bus_space_write_1(iot, ioh, SXFRCTL1, BITBUCKET);
		for (;;) {
			if ((bus_space_read_1(iot, ioh, DMASTAT) & INTSTAT) !=
			    0)
				break;
		}
		bus_space_write_1(iot, ioh, SXFRCTL1, 0);
		AIC_MISC(("extra data  "));
	}

phasechange:
	/* Turn on ENREQINIT again. */
	bus_space_write_1(iot, ioh, SIMODE1,
	    ENSCSIRST | ENSCSIPERR | ENBUSFREE | ENREQINIT | ENPHASECHG);

	/* Stop the FIFO data path. */
	bus_space_write_1(iot, ioh, SXFRCTL0, CHEN);
	bus_space_write_1(iot, ioh, DMACNTRL0, 0);

	return in;
}

/*
 * This is the workhorse routine of the driver.
 * Deficiencies (for now):
 * 1) always uses programmed I/O
 */
int
aicintr(void *arg)
{
	struct aic_softc *sc = arg;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	u_char sstat0, sstat1;
	struct aic_acb *acb;
	struct scsi_link *sc_link;
	struct aic_tinfo *ti;
	int n;

	/*
	 * Clear INTEN.  We enable it again before returning.  This makes the
	 * interrupt essentially level-triggered.
	 */
	bus_space_write_1(iot, ioh, DMACNTRL0, 0);

	AIC_TRACE(("aicintr  "));

loop:
	/*
	 * First check for abnormal conditions, such as reset.
	 */
	sstat1 = bus_space_read_1(iot, ioh, SSTAT1);
	AIC_MISC(("sstat1:0x%02x ", sstat1));

	if ((sstat1 & SCSIRSTI) != 0) {
		printf("%s: SCSI bus reset\n", sc->sc_dev.dv_xname);
		goto reset;
	}

	/*
	 * Check for less serious errors.
	 */
	if ((sstat1 & SCSIPERR) != 0) {
		printf("%s: SCSI bus parity error\n", sc->sc_dev.dv_xname);
		bus_space_write_1(iot, ioh, CLRSINT1, CLRSCSIPERR);
		if (sc->sc_prevphase == PH_MSGIN) {
			sc->sc_flags |= AIC_DROP_MSGIN;
			aic_sched_msgout(sc, SEND_PARITY_ERROR);
		} else
			aic_sched_msgout(sc, SEND_INIT_DET_ERR);
	}

	/*
	 * If we're not already busy doing something test for the following
	 * conditions:
	 * 1) We have been reselected by something
	 * 2) We have selected something successfully
	 * 3) Our selection process has timed out
	 * 4) This is really a bus free interrupt just to get a new command
	 *    going?
	 * 5) Spurious interrupt?
	 */
	switch (sc->sc_state) {
	case AIC_IDLE:
	case AIC_SELECTING:
		sstat0 = bus_space_read_1(iot, ioh, SSTAT0);
		AIC_MISC(("sstat0:0x%02x ", sstat0));

		if ((sstat0 & TARGET) != 0) {
			/*
			 * We don't currently support target mode.
			 */
			printf("%s: target mode selected; going to BUS FREE\n",
			    sc->sc_dev.dv_xname);
			bus_space_write_1(iot, ioh, SCSISIG, 0);

			goto sched;
		} else if ((sstat0 & SELDI) != 0) {
			AIC_MISC(("reselected  "));

			/*
			 * If we're trying to select a target ourselves,
			 * push our command back into the ready list.
			 */
			if (sc->sc_state == AIC_SELECTING) {
				AIC_MISC(("backoff selector  "));
				AIC_ASSERT(sc->sc_nexus != NULL);
				acb = sc->sc_nexus;
				sc->sc_nexus = NULL;
				TAILQ_INSERT_HEAD(&sc->ready_list, acb, chain);
			}

			/* Save reselection ID. */
			sc->sc_selid = bus_space_read_1(iot, ioh, SELID);

			sc->sc_state = AIC_RESELECTED;
		} else if ((sstat0 & SELDO) != 0) {
			AIC_MISC(("selected  "));

			/* We have selected a target. Things to do:
			 * a) Determine what message(s) to send.
			 * b) Verify that we're still selecting the target.
			 * c) Mark device as busy.
			 */
			if (sc->sc_state != AIC_SELECTING) {
				printf("%s: selection out while idle; ",
				    sc->sc_dev.dv_xname);
				printf("resetting\n");
				AIC_BREAK();
				goto reset;
			}
			AIC_ASSERT(sc->sc_nexus != NULL);
			acb = sc->sc_nexus;
			sc_link = acb->xs->sc_link;
			ti = &sc->sc_tinfo[sc_link->target];

			sc->sc_msgpriq = SEND_IDENTIFY;
			if (acb->flags & ACB_RESET)
				sc->sc_msgpriq |= SEND_DEV_RESET;
			else if (acb->flags & ACB_ABORT)
				sc->sc_msgpriq |= SEND_ABORT;
			else {
#if AIC_USE_SYNCHRONOUS
				if ((ti->flags & DO_SYNC) != 0)
					sc->sc_msgpriq |= SEND_SDTR;
#endif
#if AIC_USE_WIDE
				if ((ti->flags & DO_WIDE) != 0)
					sc->sc_msgpriq |= SEND_WDTR;
#endif
			}

			acb->flags |= ACB_NEXUS;
			ti->lubusy |= (1 << sc_link->lun);

			/* Do an implicit RESTORE POINTERS. */
			sc->sc_dp = acb->data_addr;
			sc->sc_dleft = acb->data_length;
			sc->sc_cp = (u_char *)&acb->scsi_cmd;
			sc->sc_cleft = acb->scsi_cmd_length;

			/* On our first connection, schedule a timeout. */
			if ((acb->xs->flags & SCSI_POLL) == 0)
				timeout_add_msec(&acb->xs->stimeout, acb->timeout);

			sc->sc_state = AIC_CONNECTED;
		} else if ((sstat1 & SELTO) != 0) {
			AIC_MISC(("selection timeout  "));

			if (sc->sc_state != AIC_SELECTING) {
				printf("%s: selection timeout while idle; ",
				    sc->sc_dev.dv_xname);
				printf("resetting\n");
				AIC_BREAK();
				goto reset;
			}
			AIC_ASSERT(sc->sc_nexus != NULL);
			acb = sc->sc_nexus;

			bus_space_write_1(iot, ioh, SXFRCTL1, 0);
			bus_space_write_1(iot, ioh, SCSISEQ, ENRESELI);
			bus_space_write_1(iot, ioh, CLRSINT1, CLRSELTIMO);
			delay(250);

			acb->xs->error = XS_SELTIMEOUT;
			goto finish;
		} else {
			if (sc->sc_state != AIC_IDLE) {
				printf("%s: BUS FREE while not idle; ",
				    sc->sc_dev.dv_xname);
				printf("state=%d\n", sc->sc_state);
				AIC_BREAK();
				goto out;
			}

			goto sched;
		}

		/*
		 * Turn off selection stuff, and prepare to catch bus free
		 * interrupts, parity errors, and phase changes.
		 */
		bus_space_write_1(iot, ioh, SXFRCTL0, CHEN | CLRSTCNT | CLRCH);
		bus_space_write_1(iot, ioh, SXFRCTL1, 0);
		bus_space_write_1(iot, ioh, SCSISEQ, ENAUTOATNP);
		bus_space_write_1(iot, ioh, CLRSINT0, CLRSELDI | CLRSELDO);
		bus_space_write_1(iot, ioh, CLRSINT1,
		    CLRBUSFREE | CLRPHASECHG);
		bus_space_write_1(iot, ioh, SIMODE0, 0);
		bus_space_write_1(iot, ioh, SIMODE1,
		    ENSCSIRST | ENSCSIPERR | ENBUSFREE | ENREQINIT |
		    ENPHASECHG);

		sc->sc_flags = 0;
		sc->sc_prevphase = PH_INVALID;
		goto dophase;
	}

	if ((sstat1 & BUSFREE) != 0) {
		/* We've gone to BUS FREE phase. */
		bus_space_write_1(iot, ioh, CLRSINT1,
		    CLRBUSFREE | CLRPHASECHG);

		switch (sc->sc_state) {
		case AIC_RESELECTED:
			goto sched;

		case AIC_CONNECTED:
			AIC_ASSERT(sc->sc_nexus != NULL);
			acb = sc->sc_nexus;

#if AIC_USE_SYNCHRONOUS + AIC_USE_WIDE
			if (sc->sc_prevphase == PH_MSGOUT) {
				/*
				 * If the target went to BUS FREE phase during
				 * or immediately after sending a SDTR or WDTR
				 * message, disable negotiation.
				 */
				sc_link = acb->xs->sc_link;
				ti = &sc->sc_tinfo[sc_link->target];
				switch (sc->sc_lastmsg) {
#if AIC_USE_SYNCHRONOUS
				case SEND_SDTR:
					ti->flags &= ~DO_SYNC;
					ti->period = ti->offset = 0;
					break;
#endif
#if AIC_USE_WIDE
				case SEND_WDTR:
					ti->flags &= ~DO_WIDE;
					ti->width = 0;
					break;
#endif
				}
			}
#endif

			if ((sc->sc_flags & AIC_ABORTING) == 0) {
				/*
				 * Section 5.1.1 of the SCSI 2 spec suggests
				 * issuing a REQUEST SENSE following an
				 * unexpected disconnect.  Some devices go into
				 * a contingent allegiance condition when
				 * disconnecting, and this is necessary to
				 * clean up their state.
				 */
				printf("%s: unexpected disconnect; ",
				    sc->sc_dev.dv_xname);
				printf("sending REQUEST SENSE\n");
				AIC_BREAK();
				aic_sense(sc, acb);
				goto out;
			}

			acb->xs->error = XS_DRIVER_STUFFUP;
			goto finish;

		case AIC_DISCONNECT:
			AIC_ASSERT(sc->sc_nexus != NULL);
			acb = sc->sc_nexus;
#if 1 /* XXXX */
			acb->data_addr = sc->sc_dp;
			acb->data_length = sc->sc_dleft;
#endif
			TAILQ_INSERT_HEAD(&sc->nexus_list, acb, chain);
			sc->sc_nexus = NULL;
			goto sched;

		case AIC_CMDCOMPLETE:
			AIC_ASSERT(sc->sc_nexus != NULL);
			acb = sc->sc_nexus;
			goto finish;
		}
	}

	bus_space_write_1(iot, ioh, CLRSINT1, CLRPHASECHG);

dophase:
	if ((sstat1 & REQINIT) == 0) {
		/* Wait for REQINIT. */
		goto out;
	}

	sc->sc_phase = bus_space_read_1(iot, ioh, SCSISIG) & PH_MASK;
	bus_space_write_1(iot, ioh, SCSISIG, sc->sc_phase);

	switch (sc->sc_phase) {
	case PH_MSGOUT:
		if (sc->sc_state != AIC_CONNECTED &&
		    sc->sc_state != AIC_RESELECTED)
			break;
		aic_msgout(sc);
		sc->sc_prevphase = PH_MSGOUT;
		goto loop;

	case PH_MSGIN:
		if (sc->sc_state != AIC_CONNECTED &&
		    sc->sc_state != AIC_RESELECTED)
			break;
		aic_msgin(sc);
		sc->sc_prevphase = PH_MSGIN;
		goto loop;

	case PH_CMD:
		if (sc->sc_state != AIC_CONNECTED)
			break;
#ifdef AIC_DEBUG
		if ((aic_debug & AIC_SHOWMISC) != 0) {
			AIC_ASSERT(sc->sc_nexus != NULL);
			acb = sc->sc_nexus;
			printf("cmd=0x%02x+%d ",
			    acb->scsi_cmd.opcode, acb->scsi_cmd_length-1);
		}
#endif
		n = aic_dataout_pio(sc, sc->sc_cp, sc->sc_cleft);
		sc->sc_cp += n;
		sc->sc_cleft -= n;
		sc->sc_prevphase = PH_CMD;
		goto loop;

	case PH_DATAOUT:
		if (sc->sc_state != AIC_CONNECTED)
			break;
		AIC_MISC(("dataout dleft=%lu ", (u_long)sc->sc_dleft));
		n = aic_dataout_pio(sc, sc->sc_dp, sc->sc_dleft);
		sc->sc_dp += n;
		sc->sc_dleft -= n;
		sc->sc_prevphase = PH_DATAOUT;
		goto loop;

	case PH_DATAIN:
		if (sc->sc_state != AIC_CONNECTED)
			break;
		AIC_MISC(("datain %lu ", (u_long)sc->sc_dleft));
		n = aic_datain_pio(sc, sc->sc_dp, sc->sc_dleft);
		sc->sc_dp += n;
		sc->sc_dleft -= n;
		sc->sc_prevphase = PH_DATAIN;
		goto loop;

	case PH_STAT:
		if (sc->sc_state != AIC_CONNECTED)
			break;
		AIC_ASSERT(sc->sc_nexus != NULL);
		acb = sc->sc_nexus;
		bus_space_write_1(iot, ioh, SXFRCTL0, CHEN | SPIOEN);
		acb->target_stat = bus_space_read_1(iot, ioh, SCSIDAT);
		bus_space_write_1(iot, ioh, SXFRCTL0, CHEN);
		AIC_MISC(("target_stat=0x%02x ", acb->target_stat));
		sc->sc_prevphase = PH_STAT;
		goto loop;
	}

	printf("%s: unexpected bus phase; resetting\n", sc->sc_dev.dv_xname);
	AIC_BREAK();
reset:
	aic_init(sc);
	return 1;

finish:
	timeout_del(&acb->xs->stimeout);
	aic_done(sc, acb);
	goto out;

sched:
	sc->sc_state = AIC_IDLE;
	aic_sched(sc);
	goto out;

out:
	bus_space_write_1(iot, ioh, DMACNTRL0, INTEN);
	return 1;
}

void
aic_abort(struct aic_softc *sc, struct aic_acb *acb)
{

	/* 2 secs for the abort */
	acb->timeout = AIC_ABORT_TIMEOUT;
	acb->flags |= ACB_ABORT;

	if (acb == sc->sc_nexus) {
		/*
		 * If we're still selecting, the message will be scheduled
		 * after selection is complete.
		 */
		if (sc->sc_state == AIC_CONNECTED)
			aic_sched_msgout(sc, SEND_ABORT);
	} else {
		aic_dequeue(sc, acb);
		TAILQ_INSERT_HEAD(&sc->ready_list, acb, chain);
		if (sc->sc_state == AIC_IDLE)
			aic_sched(sc);
	}
}

void
aic_timeout(void *arg)
{
	struct aic_acb *acb = arg;
	struct scsi_xfer *xs = acb->xs;
	struct scsi_link *sc_link = xs->sc_link;
	struct aic_softc *sc = sc_link->bus->sb_adapter_softc;
	int s;

	sc_print_addr(sc_link);
	printf("timed out");

	s = splbio();

	if (acb->flags & ACB_ABORT) {
		/* abort timed out */
		printf(" AGAIN\n");
		/* XXX Must reset! */
	} else {
		/* abort the operation that has timed out */
		printf("\n");
		acb->xs->error = XS_TIMEOUT;
		aic_abort(sc, acb);
	}

	splx(s);
}

#ifdef AIC_DEBUG
/*
 * The following functions are mostly used for debugging purposes, either
 * directly called from the driver or from the kernel debugger.
 */

void
aic_show_scsi_cmd(struct aic_acb *acb)
{
	u_char  *b = (u_char *)&acb->scsi_cmd;
	struct scsi_link *sc_link = acb->xs->sc_link;
	int i;

	sc_print_addr(sc_link);
	if ((acb->xs->flags & SCSI_RESET) == 0) {
		for (i = 0; i < acb->scsi_cmd_length; i++) {
			if (i)
				printf(",");
			printf("%x", b[i]);
		}
		printf("\n");
	} else
		printf("RESET\n");
}

void
aic_print_acb(struct aic_acb *acb)
{

	printf("acb@%p xs=%p flags=%x", acb, acb->xs, acb->flags);
	printf(" dp=%p dleft=%d target_stat=%x\n",
	       acb->data_addr, acb->data_length, acb->target_stat);
	aic_show_scsi_cmd(acb);
}

void
aic_print_active_acb(void)
{
	struct aic_acb *acb;
	struct aic_softc *sc = aic_cd.cd_devs[0];

	printf("ready list:\n");
	TAILQ_FOREACH(acb, &sc->ready_list, chain)
		aic_print_acb(acb);
	printf("nexus:\n");
	if (sc->sc_nexus != NULL)
		aic_print_acb(sc->sc_nexus);
	printf("nexus list:\n");
	TAILQ_FOREACH(acb, &sc->nexus_list, chain)
		aic_print_acb(acb);
}

void
aic_dump6360(struct aic_softc *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;

	printf("aic6360: SCSISEQ=%x SXFRCTL0=%x SXFRCTL1=%x SCSISIG=%x\n",
	    bus_space_read_1(iot, ioh, SCSISEQ),
	    bus_space_read_1(iot, ioh, SXFRCTL0),
	    bus_space_read_1(iot, ioh, SXFRCTL1),
	    bus_space_read_1(iot, ioh, SCSISIG));
	printf("         SSTAT0=%x SSTAT1=%x SSTAT2=%x SSTAT3=%x SSTAT4=%x\n",
	    bus_space_read_1(iot, ioh, SSTAT0),
	    bus_space_read_1(iot, ioh, SSTAT1),
	    bus_space_read_1(iot, ioh, SSTAT2),
	    bus_space_read_1(iot, ioh, SSTAT3),
	    bus_space_read_1(iot, ioh, SSTAT4));
	printf("         SIMODE0=%x SIMODE1=%x ",
	    bus_space_read_1(iot, ioh, SIMODE0),
	    bus_space_read_1(iot, ioh, SIMODE1));
	printf("DMACNTRL0=%x DMACNTRL1=%x DMASTAT=%x\n",
	    bus_space_read_1(iot, ioh, DMACNTRL0),
	    bus_space_read_1(iot, ioh, DMACNTRL1),
	    bus_space_read_1(iot, ioh, DMASTAT));
	printf("         FIFOSTAT=%d SCSIBUS=0x%x\n",
	    bus_space_read_1(iot, ioh, FIFOSTAT),
	    bus_space_read_1(iot, ioh, SCSIBUS));
}

void
aic_dump_driver(struct aic_softc *sc)
{
	struct aic_tinfo *ti;
	int i;

	printf("nexus=%p prevphase=%x\n", sc->sc_nexus, sc->sc_prevphase);
	printf("state=%x msgin=%x ", sc->sc_state, sc->sc_imess[0]);
	printf("msgpriq=%x msgoutq=%x lastmsg=%x currmsg=%x\n", sc->sc_msgpriq,
	    sc->sc_msgoutq, sc->sc_lastmsg, sc->sc_currmsg);
	for (i = 0; i < 7; i++) {
		ti = &sc->sc_tinfo[i];
		printf("tinfo%d: %d cmds %d disconnects %d timeouts",
		    i, ti->cmds, ti->dconns, ti->touts);
		printf(" %d senses flags=%x\n", ti->senses, ti->flags);
	}
}
#endif
