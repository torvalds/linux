/*	$OpenBSD: mb89352.c,v 1.35 2024/11/04 18:27:14 miod Exp $	*/
/*	$NetBSD: mb89352.c,v 1.5 2000/03/23 07:01:31 thorpej Exp $	*/
/*	NecBSD: mb89352.c,v 1.4 1998/03/14 07:31:20 kmatsuda Exp	*/

#ifdef DDB
#define	integrate
#else
#define	integrate	static __inline 
#endif

/*-
 * Copyright (c) 1996,97,98,99 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum, Masaru Oki and Kouichi Matsuda.
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
 * [NetBSD for NEC PC-98 series]
 *  Copyright (c) 1996, 1997, 1998
 *	NetBSD/pc98 porting staff. All rights reserved.
 *  Copyright (c) 1996, 1997, 1998
 *	Kouichi Matsuda. All rights reserved.
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

/* Synchronous data transfers? */
#define SPC_USE_SYNCHRONOUS	0
#define SPC_SYNC_REQ_ACK_OFS 	8

/* Wide data transfers? */
#define	SPC_USE_WIDE		0
#define	SPC_MAX_WIDTH		0

/* Max attempts made to transmit a message */
#define SPC_MSG_MAX_ATTEMPT	3 /* Not used now XXX */

/*
 * Some spin loop parameters (essentially how long to wait some places)
 * The problem(?) is that sometimes we expect either to be able to transmit a
 * byte or to get a new one from the SCSI bus pretty soon.  In order to avoid
 * returning from the interrupt just to get yanked back for the next byte we
 * may spin in the interrupt routine waiting for this byte to come.  How long?
 * This is really (SCSI) device and processor dependent.  Tuneable, I guess.
 */
#define SPC_MSGIN_SPIN	1 	/* Will spinwait upto ?ms for a new msg byte */
#define SPC_MSGOUT_SPIN	1

/*
 * Include debug functions?  At the end of this file there are a bunch of
 * functions that will print out various information regarding queued SCSI
 * commands, driver state and chip contents.  You can call them from the
 * kernel debugger.  If you set SPC_DEBUG to 0 they are not included (the
 * kernel uses less memory) but you lose the debugging facilities.
 */
/* #define SPC_DEBUG */

#define	SPC_ABORT_TIMEOUT	2000	/* time to wait for abort */

/* End of customizable parameters */

/*
 * MB89352 SCSI Protocol Controller (SPC) routines.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/device.h>
#include <sys/buf.h>
#include <sys/proc.h>
#include <sys/queue.h>

#include <machine/intr.h>
#include <machine/bus.h>

#include <scsi/scsi_all.h>
#include <scsi/scsi_message.h>
#include <scsi/scsiconf.h>

#include <luna88k/dev/mb89352reg.h>
#include <luna88k/dev/mb89352var.h>

#ifndef DDB
#define	db_enter() panic("should call debugger here (mb89352.c)")
#endif /* ! DDB */

#ifdef SPC_DEBUG
int spc_debug = 0x00; /* SPC_SHOWSTART|SPC_SHOWMISC|SPC_SHOWTRACE; */
#endif

void	spc_done	(struct spc_softc *, struct spc_acb *);
void	spc_dequeue	(struct spc_softc *, struct spc_acb *);
void	spc_scsi_cmd	(struct scsi_xfer *);
int	spc_poll	(struct spc_softc *, struct scsi_xfer *, int);
integrate void	spc_sched_msgout(struct spc_softc *, u_char);
integrate void	spc_setsync(struct spc_softc *, struct spc_tinfo *);
void	spc_select	(struct spc_softc *, struct spc_acb *);
void	spc_timeout	(void *);
void	spc_scsi_reset	(struct spc_softc *);
void	spc_reset	(struct spc_softc *);
void	spc_acb_free	(void *, void *);
void	*spc_acb_alloc	(void *);
int	spc_reselect	(struct spc_softc *, int);
void	spc_sense	(struct spc_softc *, struct spc_acb *);
void	spc_msgin	(struct spc_softc *);
void	spc_abort	(struct spc_softc *, struct spc_acb *);
void	spc_msgout	(struct spc_softc *);
int	spc_dataout_pio	(struct spc_softc *, u_char *, int);
int	spc_datain_pio	(struct spc_softc *, u_char *, int);
#ifdef SPC_DEBUG
void	spc_print_acb	(struct spc_acb *);
void	spc_dump_driver (struct spc_softc *);
void	spc_dump89352	(struct spc_softc *);
void	spc_show_scsi_cmd(struct spc_acb *);
void	spc_print_active_acb(void);
#endif

extern struct cfdriver spc_cd;

#define breathe() \
do { \
	asm volatile ("or %r0, %r0, %r0"); \
	asm volatile ("or %r0, %r0, %r0"); \
	asm volatile ("or %r0, %r0, %r0"); \
	asm volatile ("or %r0, %r0, %r0"); \
} while (0)

/*
 * INITIALIZATION ROUTINES (probe, attach ++)
 */

void
/* spc_attach(sc) */
spc_attach(struct spc_softc *sc, const struct scsi_adapter *adapter)
{
	struct scsibus_attach_args saa;
	SPC_TRACE(("spc_attach  "));
	sc->sc_state = SPC_INIT;

	sc->sc_freq = 20;	/* XXXX Assume 20 MHz. */

#if SPC_USE_SYNCHRONOUS
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
#endif

	spc_init(sc);	/* Init chip and driver */

	saa.saa_adapter_softc = sc;
	saa.saa_adapter_target = sc->sc_initiator;
	saa.saa_adapter = adapter;
	saa.saa_luns = saa.saa_adapter_buswidth = 8;
	saa.saa_openings = 2;
	saa.saa_pool = &sc->sc_iopool;
	saa.saa_flags = saa.saa_quirks = 0;
	saa.saa_wwpn = saa.saa_wwnn = 0;

	config_found(&sc->sc_dev, &saa, scsiprint);
}

/*
 * Initialize MB89352 chip itself
 * The following conditions should hold:
 * spc_isa_probe should have succeeded, i.e. the iobase address in spc_softc
 * must be valid.
 */
void
spc_reset(struct spc_softc *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;

	SPC_TRACE(("spc_reset  "));
	/*
	 * Disable interrupts then reset the FUJITSU chip.
	 */
	bus_space_write_1(iot, ioh, SCTL, SCTL_DISABLE | SCTL_CTRLRST);
	bus_space_write_1(iot, ioh, SCMD, 0);
	bus_space_write_1(iot, ioh, TMOD, 0);
	bus_space_write_1(iot, ioh, PCTL, 0);
	bus_space_write_1(iot, ioh, TEMP, 0);
	bus_space_write_1(iot, ioh, TCH, 0);
	bus_space_write_1(iot, ioh, TCM, 0);
	bus_space_write_1(iot, ioh, TCL, 0);
	bus_space_write_1(iot, ioh, INTS, 0);
	bus_space_write_1(iot, ioh, SCTL,
	    SCTL_DISABLE | SCTL_ABRT_ENAB | SCTL_PARITY_ENAB | SCTL_RESEL_ENAB);
	bus_space_write_1(iot, ioh, BDID, sc->sc_initiator);
	delay(400);
	bus_space_write_1(iot, ioh, SCTL,
	    bus_space_read_1(iot, ioh, SCTL) & ~SCTL_DISABLE);
}


/*
 * Pull the SCSI RST line for 500us.
 */
void
spc_scsi_reset(struct spc_softc *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;

	SPC_TRACE(("spc_scsi_reset  "));
	bus_space_write_1(iot, ioh, SCMD, bus_space_read_1(iot, ioh, SCMD) | SCMD_RST);
	delay(500);
	bus_space_write_1(iot, ioh, SCMD, bus_space_read_1(iot, ioh, SCMD) & ~SCMD_RST);
	delay(50);
}

/*
 * Initialize spc SCSI driver.
 */
void
spc_init(struct spc_softc *sc)
{
	struct spc_acb *acb;
	int r;

	SPC_TRACE(("spc_init  "));
	spc_reset(sc);
	spc_scsi_reset(sc);
	spc_reset(sc);

	if (sc->sc_state == SPC_INIT) {
		/* First time through; initialize. */
		TAILQ_INIT(&sc->ready_list);
		TAILQ_INIT(&sc->nexus_list);
		TAILQ_INIT(&sc->free_list);
		mtx_init(&sc->sc_acb_mtx, IPL_BIO);
		scsi_iopool_init(&sc->sc_iopool, sc, spc_acb_alloc, spc_acb_free);
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
		sc->sc_state = SPC_CLEANING;
		if ((acb = sc->sc_nexus) != NULL) {
			acb->xs->error = XS_DRIVER_STUFFUP;
			timeout_del(&acb->xs->stimeout);
			spc_done(sc, acb);
		}
		while ((acb = TAILQ_FIRST(&sc->nexus_list)) != NULL) {
			acb->xs->error = XS_DRIVER_STUFFUP;
			timeout_del(&acb->xs->stimeout);
			spc_done(sc, acb);
		}
	}

	sc->sc_prevphase = PH_INVALID;
	for (r = 0; r < 8; r++) {
		struct spc_tinfo *ti = &sc->sc_tinfo[r];

		ti->flags = 0;
#if SPC_USE_SYNCHRONOUS
		ti->flags |= DO_SYNC;
		ti->period = sc->sc_minsync;
		ti->offset = SPC_SYNC_REQ_ACK_OFS;
#else
		ti->period = ti->offset = 0;
#endif
#if SPC_USE_WIDE
		ti->flags |= DO_WIDE;
		ti->width = SPC_MAX_WIDTH;
#else
		ti->width = 0;
#endif
	}

	sc->sc_state = SPC_IDLE;
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, SCTL,
	    bus_space_read_1(sc->sc_iot, sc->sc_ioh, SCTL) | SCTL_INTR_ENAB);
}

void
spc_acb_free(void *xsc, void *xacb)
{
	struct spc_softc *sc = xsc;
	struct spc_acb *acb = xacb;

	SPC_TRACE(("spc_acb_free  "));

	acb->flags = 0;
	mtx_enter(&sc->sc_acb_mtx);
	TAILQ_INSERT_HEAD(&sc->free_list, acb, chain);
	mtx_leave(&sc->sc_acb_mtx);
}

void *
spc_acb_alloc(void *xsc)
{
	struct spc_softc *sc = xsc;
	struct spc_acb *acb;

	SPC_TRACE(("spc_acb_alloc  "));

	mtx_enter(&sc->sc_acb_mtx);
	acb = TAILQ_FIRST(&sc->free_list);
	if (acb)
		TAILQ_REMOVE(&sc->free_list, acb, chain);
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
spc_scsi_cmd(struct scsi_xfer *xs)
{
	struct scsi_link *sc_link = xs->sc_link;
	struct spc_softc *sc = sc_link->bus->sb_adapter_softc;
	struct spc_acb *acb;
	int s, flags;

	SPC_TRACE(("spc_scsi_cmd  "));
	SPC_CMDS(("[0x%x, %d]->%d ", (int)xs->cmd.opcode, xs->cmdlen,
	    sc_link->target));

	flags = xs->flags;
	acb = xs->io;

	/* Initialize acb */
	acb->xs = xs;
	timeout_set(&xs->stimeout, spc_timeout, acb);

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
	/*
	 * Start scheduling unless a queue process is in progress.
	 */
	if (sc->sc_state == SPC_IDLE)
		spc_sched(sc);
	/*
	 * After successful sending, check if we should return just now.
	 */

	splx(s);

	if ((flags & SCSI_POLL) == 0)
		return;

	/* Not allowed to use interrupts, use polling instead */
	s = splbio();
	if (spc_poll(sc, xs, xs->timeout)) {
		spc_timeout(acb);
		if (spc_poll(sc, xs, xs->timeout))
			spc_timeout(acb);
	}
	splx(s);
}

/*
 * Used when interrupt driven I/O isn't allowed, e.g. during boot.
 */
int
spc_poll(struct spc_softc *sc, struct scsi_xfer *xs, int count)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;

	SPC_TRACE(("spc_poll  "));
	while (count) {
		/*
		 * If we had interrupts enabled, would we
		 * have got an interrupt?
		 */
		if (bus_space_read_1(iot, ioh, INTS) != 0)
			spc_intr(sc);
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
spc_sched_msgout(struct spc_softc *sc, u_char m)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;

	SPC_TRACE(("spc_sched_msgout  "));
	if (sc->sc_msgpriq == 0)
		bus_space_write_1(iot, ioh, SCMD, SCMD_SET_ATN);
	sc->sc_msgpriq |= m;
}

/*
 * Set synchronous transfer offset and period.
 */
integrate void
spc_setsync(struct spc_softc *sc, struct spc_tinfo *ti)
{
#if SPC_USE_SYNCHRONOUS
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;

	SPC_TRACE(("spc_setsync  "));
	if (ti->offset != 0)
		bus_space_write_1(iot, ioh, TMOD,
		    ((ti->period * sc->sc_freq) / 250 - 2) << 4 | ti->offset);
	else
		bus_space_write_1(iot, ioh, TMOD, 0);
#endif
}

/*
 * Start a selection.  This is used by spc_sched() to select an idle target,
 * and by spc_done() to immediately reselect a target to get sense information.
 */
void
spc_select(struct spc_softc *sc, struct spc_acb *acb)
{
	struct scsi_link *sc_link = acb->xs->sc_link;
	int target = sc_link->target;
	struct spc_tinfo *ti = &sc->sc_tinfo[target];
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;

	SPC_TRACE(("spc_select  "));
	spc_setsync(sc, ti);

#if 0
	bus_space_write_1(iot, ioh, SCMD, SCMD_SET_ATN);
#endif

	bus_space_write_1(iot, ioh, PCTL, 0);
	bus_space_write_1(iot, ioh, TEMP,
	    (1 << sc->sc_initiator) | (1 << target));
	/*
	 * Setup BSY timeout (selection timeout).
	 * 250ms according to the SCSI specification.
	 * T = (X * 256 + 15) * Tclf * 2  (Tclf = 200ns on x68k)
	 * To setup 256ms timeout,
	 * 128000ns/200ns = X * 256 + 15
	 * 640 - 15 = X * 256
	 * X = 625 / 256
	 * X = 2 + 113 / 256
	 *  ==> tch = 2, tcm = 113 (correct?)
	 */
	/* Time to the information transfer phase start. */
	/* XXX These values should be calculated from sc_freq */
	bus_space_write_1(iot, ioh, TCH, 2);
	bus_space_write_1(iot, ioh, TCM, 113);
	bus_space_write_1(iot, ioh, TCL, 3);
	bus_space_write_1(iot, ioh, SCMD, SCMD_SELECT);

	sc->sc_state = SPC_SELECTING;
}

int
spc_reselect(struct spc_softc *sc, int message)
{
	u_char selid, target, lun;
	struct spc_acb *acb;
	struct scsi_link *sc_link;
	struct spc_tinfo *ti;

	SPC_TRACE(("spc_reselect  "));
	/*
	 * The SCSI chip made a snapshot of the data bus while the reselection
	 * was being negotiated.  This enables us to determine which target did
	 * the reselect.
	 */
	selid = sc->sc_selid & ~(1 << sc->sc_initiator);
	if (selid & (selid - 1)) {
		printf("%s: reselect with invalid selid %02x; "
		    "sending DEVICE RESET\n", sc->sc_dev.dv_xname, selid);
		SPC_BREAK();
		goto reset;
	}

	/*
	 * Search wait queue for disconnected cmd
	 * The list should be short, so I haven't bothered with
	 * any more sophisticated structures than a simple
	 * singly linked list.
	 */
	target = ffs(selid) - 1;
	lun = message & 0x07;
	TAILQ_FOREACH(acb, &sc->nexus_list, chain) {
		sc_link = acb->xs->sc_link;
		if (sc_link->target == target &&
		    sc_link->lun == lun)
			break;
	}
	if (acb == NULL) {
		printf("%s: reselect from target %d lun %d with no nexus; "
		    "sending ABORT\n", sc->sc_dev.dv_xname, target, lun);
		SPC_BREAK();
		goto abort;
	}

	/* Make this nexus active again. */
	TAILQ_REMOVE(&sc->nexus_list, acb, chain);
	sc->sc_state = SPC_CONNECTED;
	sc->sc_nexus = acb;
	ti = &sc->sc_tinfo[target];
	ti->lubusy |= (1 << lun);
	spc_setsync(sc, ti);

	if (acb->flags & ACB_RESET)
		spc_sched_msgout(sc, SEND_DEV_RESET);
	else if (acb->flags & ACB_ABORT)
		spc_sched_msgout(sc, SEND_ABORT);

	/* Do an implicit RESTORE POINTERS. */
	sc->sc_dp = acb->data_addr;
	sc->sc_dleft = acb->data_length;
	sc->sc_cp = (u_char *)&acb->scsi_cmd;
	sc->sc_cleft = acb->scsi_cmd_length;

	return (0);

reset:
	spc_sched_msgout(sc, SEND_DEV_RESET);
	return (1);

abort:
	spc_sched_msgout(sc, SEND_ABORT);
	return (1);
}

/*
 * Schedule a SCSI operation.  This has now been pulled out of the interrupt
 * handler so that we may call it from spc_scsi_cmd and spc_done.  This may
 * save us an unnecessary interrupt just to get things going.  Should only be
 * called when state == SPC_IDLE and at bio pl.
 */
void
spc_sched(struct spc_softc *sc)
{
	struct spc_acb *acb;
	struct scsi_link *sc_link;
	struct spc_tinfo *ti;

	/* missing the hw, just return and wait for our hw */
	if (sc->sc_flags & SPC_INACTIVE)
		return;
	SPC_TRACE(("spc_sched  "));
	/*
	 * Find first acb in ready queue that is for a target/lunit pair that
	 * is not busy.
	 */
	TAILQ_FOREACH(acb, &sc->ready_list, chain) {
		sc_link = acb->xs->sc_link;
		ti = &sc->sc_tinfo[sc_link->target];
		if ((ti->lubusy & (1 << sc_link->lun)) == 0) {
			SPC_MISC(("selecting %d:%d  ",
			    sc_link->target, sc_link->lun));
			TAILQ_REMOVE(&sc->ready_list, acb, chain);
			sc->sc_nexus = acb;
			spc_select(sc, acb);
			return;
		} else
			SPC_MISC(("%d:%d busy\n",
			    sc_link->target, sc_link->lun));
	}
	SPC_MISC(("idle  "));
	/* Nothing to start; just enable reselections and wait. */
}

void
spc_sense(struct spc_softc *sc, struct spc_acb *acb)
{
	struct scsi_xfer *xs = acb->xs;
	struct scsi_link *sc_link = xs->sc_link;
	struct spc_tinfo *ti = &sc->sc_tinfo[sc_link->target];
	struct scsi_sense *ss = (void *)&acb->scsi_cmd;

	SPC_MISC(("requesting sense  "));
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
		spc_select(sc, acb);
	} else {
		spc_dequeue(sc, acb);
		TAILQ_INSERT_HEAD(&sc->ready_list, acb, chain);
		if (sc->sc_state == SPC_IDLE)
			spc_sched(sc);
	}
}

/*
 * POST PROCESSING OF SCSI_CMD (usually current)
 */
void
spc_done(struct spc_softc *sc, struct spc_acb *acb)
{
	struct scsi_xfer *xs = acb->xs;
	struct scsi_link *sc_link = xs->sc_link;
	struct spc_tinfo *ti = &sc->sc_tinfo[sc_link->target];

	SPC_TRACE(("spc_done  "));

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
		} else {
			switch (acb->target_stat) {
			case SCSI_CHECK:
				/* First, save the return values */
				xs->resid = acb->data_length;
				xs->status = acb->target_stat;
				spc_sense(sc, acb);
				return;
			case SCSI_BUSY:
				xs->error = XS_BUSY;
				break;
			case SCSI_OK:
				xs->resid = acb->data_length;
				break;
			default:
				xs->error = XS_DRIVER_STUFFUP;
#ifdef SPC_DEBUG
				printf("%s: spc_done: bad stat 0x%x\n",
				    sc->sc_dev.dv_xname, acb->target_stat);
#endif
				break;
			}
		}
	}

#ifdef SPC_DEBUG
	if ((spc_debug & SPC_SHOWMISC) != 0) {
		if (xs->resid != 0)
			printf("resid=%d ", xs->resid);
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
		sc->sc_state = SPC_IDLE;
		spc_sched(sc);
	} else
		spc_dequeue(sc, acb);

	ti->cmds++;
	scsi_done(xs);
}

void
spc_dequeue(struct spc_softc *sc, struct spc_acb *acb)
{

	SPC_TRACE(("spc_dequeue  "));
	if (acb->flags & ACB_NEXUS)
		TAILQ_REMOVE(&sc->nexus_list, acb, chain);
	else
		TAILQ_REMOVE(&sc->ready_list, acb, chain);
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
spc_msgin(struct spc_softc *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	int n;

	SPC_TRACE(("spc_msgin  "));

	if (sc->sc_prevphase == PH_MSGIN) {
		/* This is a continuation of the previous message. */
		n = sc->sc_imp - sc->sc_imess;
		goto nextbyte;
	}

	/* This is a new MESSAGE IN phase.  Clean up our state. */
	sc->sc_flags &= ~SPC_DROP_MSGIN;

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
		u_int8_t intstat;
#if 0
		for (;;) {
			if ((bus_space_read_1(iot, ioh, PSNS) & PSNS_REQ) != 0)
				break;
			/* Wait for REQINIT.  XXX Need timeout. */
		}
#endif
		if (bus_space_read_1(iot, ioh, INTS) != 0) {
			/*
			 * Target left MESSAGE IN, probably because it
			 * a) noticed our ATN signal, or
			 * b) ran out of messages.
			 */
			goto out;
		}

		/* If parity error, just dump everything on the floor. */
		if ((bus_space_read_1(iot, ioh, SERR) &
		    (SERR_SCSI_PAR|SERR_SPC_PAR)) != 0) {
			sc->sc_flags |= SPC_DROP_MSGIN;
			spc_sched_msgout(sc, SEND_PARITY_ERROR);
		}

		/* send TRANSFER command. */
		bus_space_write_1(iot, ioh, TCH, 0);
		bus_space_write_1(iot, ioh, TCM, 0);
		bus_space_write_1(iot, ioh, TCL, 1);
		bus_space_write_1(iot, ioh, PCTL,
				  sc->sc_phase | PCTL_BFINT_ENAB);
#ifdef x68k
		bus_space_write_1(iot, ioh, SCMD, SCMD_XFR); /* | SCMD_PROG_XFR */
#else
		bus_space_write_1(iot, ioh, SCMD, SCMD_XFR | SCMD_PROG_XFR);	/* XXX */
#endif
		intstat = 0;
		for (;;) {
			/*if ((bus_space_read_1(iot, ioh, SSTS) & SSTS_BUSY) != 0
				&& (bus_space_read_1(iot, ioh, SSTS) & SSTS_DREG_EMPTY) != 0)*/
			if ((bus_space_read_1(iot, ioh, SSTS) & SSTS_DREG_EMPTY) == 0)
				break;
			/*
			 * We have to read INTS before checking SSTS to avoid
			 * race between SSTS_DREG_EMPTY and INTS_CMD_DONE.
			 */
			if (intstat != 0)
				goto out;
			intstat = bus_space_read_1(iot, ioh, INTS);
		}

		/* Gather incoming message bytes if needed. */
		if ((sc->sc_flags & SPC_DROP_MSGIN) == 0) {
			if (n >= SPC_MAX_MSG_LEN) {
				(void) bus_space_read_1(iot, ioh, DREG);
				sc->sc_flags |= SPC_DROP_MSGIN;
				spc_sched_msgout(sc, SEND_REJECT);
			} else {
				*sc->sc_imp++ = bus_space_read_1(iot, ioh, DREG);
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
			(void) bus_space_read_1(iot, ioh, DREG);

		/*
		 * If we reach this spot we're either:
		 * a) in the middle of a multi-byte message, or
		 * b) dropping bytes.
		 */
#if 0
		/* Ack the last byte read. */
		/*(void) bus_space_read_1(iot, ioh, DREG);*/
		while ((bus_space_read_1(iot, ioh, PSNS) & ACKI) != 0)
			;
#endif
	}

	SPC_MISC(("n=%d imess=0x%02x  ", n, sc->sc_imess[0]));

	/* We now have a complete message.  Parse it. */
	switch (sc->sc_state) {
		struct spc_acb *acb;
		struct scsi_link *sc_link;
		struct spc_tinfo *ti;

	case SPC_CONNECTED:
		SPC_ASSERT(sc->sc_nexus != NULL);
		acb = sc->sc_nexus;
		ti = &sc->sc_tinfo[acb->xs->sc_link->target];

		switch (sc->sc_imess[0]) {
		case MSG_CMDCOMPLETE:
			if (sc->sc_dleft < 0) {
				sc_link = acb->xs->sc_link;
				printf("%s: %ld extra bytes from %d:%d\n",
				    sc->sc_dev.dv_xname, -sc->sc_dleft,
				    sc_link->target, sc_link->lun);
				acb->data_length = 0;
			}
			acb->xs->resid = acb->data_length = sc->sc_dleft;
			sc->sc_state = SPC_CMDCOMPLETE;
			break;

		case MSG_PARITY_ERROR:
			/* Resend the last message. */
			spc_sched_msgout(sc, sc->sc_lastmsg);
			break;

		case MSG_MESSAGE_REJECT:
			SPC_MISC(("message rejected %02x  ", sc->sc_lastmsg));
			switch (sc->sc_lastmsg) {
#if SPC_USE_SYNCHRONOUS + SPC_USE_WIDE
			case SEND_IDENTIFY:
				ti->flags &= ~(DO_SYNC | DO_WIDE);
				ti->period = ti->offset = 0;
				spc_setsync(sc, ti);
				ti->width = 0;
				break;
#endif
#if SPC_USE_SYNCHRONOUS
			case SEND_SDTR:
				ti->flags &= ~DO_SYNC;
				ti->period = ti->offset = 0;
				spc_setsync(sc, ti);
				break;
#endif
#if SPC_USE_WIDE
			case SEND_WDTR:
				ti->flags &= ~DO_WIDE;
				ti->width = 0;
				break;
#endif
			case SEND_INIT_DET_ERR:
				spc_sched_msgout(sc, SEND_ABORT);
				break;
			}
			break;

		case MSG_NOOP:
			break;

		case MSG_DISCONNECT:
			ti->dconns++;
			sc->sc_state = SPC_DISCONNECT;
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
#if SPC_USE_SYNCHRONOUS
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
					spc_sched_msgout(sc, SEND_SDTR);
				} else {
					sc_print_addr(acb->xs->sc_link);
					printf("sync, offset %d, "
					    "period %dnsec\n",
					    ti->offset, ti->period * 4);
				}
				spc_setsync(sc, ti);
				break;
#endif

#if SPC_USE_WIDE
			case MSG_EXT_WDTR:
				if (sc->sc_imess[1] != 2)
					goto reject;
				ti->width = sc->sc_imess[3];
				ti->flags &= ~DO_WIDE;
				if (ti->width == 0) {
				} else if (ti->width > SPC_MAX_WIDTH) {
					ti->width = 0;
					spc_sched_msgout(sc, SEND_WDTR);
				} else {
					sc_print_addr(acb->xs->sc_link);
					printf("wide, width %d\n",
					    1 << (3 + ti->width));
				}
				break;
#endif

			default:
				printf("%s: unrecognized MESSAGE EXTENDED; "
				    "sending REJECT\n", sc->sc_dev.dv_xname);
				SPC_BREAK();
				goto reject;
			}
			break;

		default:
			printf("%s: unrecognized MESSAGE; sending REJECT\n",
			    sc->sc_dev.dv_xname);
			SPC_BREAK();
		reject:
			spc_sched_msgout(sc, SEND_REJECT);
			break;
		}
		break;

	case SPC_RESELECTED:
		if (!MSG_ISIDENTIFY(sc->sc_imess[0])) {
			printf("%s: reselect without IDENTIFY; "
			    "sending DEVICE RESET\n", sc->sc_dev.dv_xname);
			SPC_BREAK();
			goto reset;
		}

		(void) spc_reselect(sc, sc->sc_imess[0]);
		break;

	default:
		printf("%s: unexpected MESSAGE IN; sending DEVICE RESET\n",
		    sc->sc_dev.dv_xname);
		SPC_BREAK();
	reset:
		spc_sched_msgout(sc, SEND_DEV_RESET);
		break;

#ifdef notdef
	abort:
		spc_sched_msgout(sc, SEND_ABORT);
		break;
#endif
	}

	/* Ack the last message byte. */
#if 0 /* XXX? */
	(void) bus_space_read_1(iot, ioh, DREG);
	while ((bus_space_read_1(iot, ioh, PSNS) & ACKI) != 0)
		;
#endif

	/* Go get the next message, if any. */
	goto nextmsg;

out:
	bus_space_write_1(iot, ioh, SCMD, SCMD_RST_ACK);
	SPC_MISC(("n=%d imess=0x%02x  ", n, sc->sc_imess[0]));
}

/*
 * Send the highest priority, scheduled message.
 */
void
spc_msgout(struct spc_softc *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
#if SPC_USE_SYNCHRONOUS
	struct spc_tinfo *ti;
#endif
	int n;

	SPC_TRACE(("spc_msgout  "));

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
			SPC_MISC(("retransmitting  "));
			sc->sc_msgpriq |= sc->sc_msgoutq;
			/*
			 * Set ATN.  If we're just sending a trivial 1-byte
			 * message, we'll clear ATN later on anyway.
			 */
			bus_space_write_1(iot, ioh, SCMD,
			    SCMD_SET_ATN); /* XXX? */
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
		SPC_ASSERT(sc->sc_nexus != NULL);
		sc->sc_omess[0] =
		    MSG_IDENTIFY(sc->sc_nexus->xs->sc_link->lun, 1);
		n = 1;
		break;

#if SPC_USE_SYNCHRONOUS
	case SEND_SDTR:
		SPC_ASSERT(sc->sc_nexus != NULL);
		ti = &sc->sc_tinfo[sc->sc_nexus->xs->sc_link->target];
		sc->sc_omess[4] = MSG_EXTENDED;
		sc->sc_omess[3] = MSG_EXT_SDTR_LEN;
		sc->sc_omess[2] = MSG_EXT_SDTR;
		sc->sc_omess[1] = ti->period >> 2;
		sc->sc_omess[0] = ti->offset;
		n = 5;
		break;
#endif

#if SPC_USE_WIDE
	case SEND_WDTR:
		SPC_ASSERT(sc->sc_nexus != NULL);
		ti = &sc->sc_tinfo[sc->sc_nexus->xs->sc_link->target];
		sc->sc_omess[3] = MSG_EXTENDED;
		sc->sc_omess[2] = MSG_EXT_WDTR_LEN;
		sc->sc_omess[1] = MSG_EXT_WDTR;
		sc->sc_omess[0] = ti->width;
		n = 4;
		break;
#endif

	case SEND_DEV_RESET:
		sc->sc_flags |= SPC_ABORTING;
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
		sc->sc_flags |= SPC_ABORTING;
		sc->sc_omess[0] = MSG_ABORT;
		n = 1;
		break;

	default:
		printf("%s: unexpected MESSAGE OUT; sending NOOP\n",
		    sc->sc_dev.dv_xname);
		SPC_BREAK();
		sc->sc_omess[0] = MSG_NOOP;
		n = 1;
		break;
	}
	sc->sc_omp = &sc->sc_omess[n];

nextbyte:
	/* Send message bytes. */
	/* send TRANSFER command. */
	bus_space_write_1(iot, ioh, TCH, n >> 16);
	bus_space_write_1(iot, ioh, TCM, n >> 8);
	bus_space_write_1(iot, ioh, TCL, n);
	bus_space_write_1(iot, ioh, PCTL, sc->sc_phase | PCTL_BFINT_ENAB);
#ifdef x68k
	bus_space_write_1(iot, ioh, SCMD, SCMD_XFR);	/* XXX */
#else
	bus_space_write_1(iot, ioh, SCMD,
	    SCMD_XFR | SCMD_PROG_XFR | SCMD_ICPT_XFR);
#endif
	for (;;) {
		if ((bus_space_read_1(iot, ioh, SSTS) & SSTS_BUSY) != 0)
			break;
		if (bus_space_read_1(iot, ioh, INTS) != 0)
			goto out;
	}
	for (;;) {
#if 0
		for (;;) {
			if ((bus_space_read_1(iot, ioh, PSNS) & PSNS_REQ) != 0)
				break;
			/* Wait for REQINIT.  XXX Need timeout. */
		}
#endif
		if (bus_space_read_1(iot, ioh, INTS) != 0) {
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
#if 0
			if (sc->sc_msgpriq == 0)
				bus_space_write_1(iot, ioh, SCMD, SCMD_RST_ATN);
#endif
			goto out;
		}

#if 0
		/* Clear ATN before last byte if this is the last message. */
		if (n == 1 && sc->sc_msgpriq == 0)
			bus_space_write_1(iot, ioh, SCMD, SCMD_RST_ATN);
#endif

		while ((bus_space_read_1(iot, ioh, SSTS) & SSTS_DREG_FULL) != 0)
			;
		/* Send message byte. */
		bus_space_write_1(iot, ioh, DREG, *--sc->sc_omp);
		--n;
		/* Keep track of the last message we've sent any bytes of. */
		sc->sc_lastmsg = sc->sc_currmsg;
#if 0
		/* Wait for ACK to be negated.  XXX Need timeout. */
		while ((bus_space_read_1(iot, ioh, PSNS) & ACKI) != 0)
			;
#endif

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
	return;
}

/*
 * spc_dataout_pio: perform a data transfer using the FIFO datapath in the spc
 * Precondition: The SCSI bus should be in the DOUT phase, with REQ asserted
 * and ACK deasserted (i.e. waiting for a data byte).
 *
 * This new revision has been optimized (I tried) to make the common case fast,
 * and the rarer cases (as a result) somewhat more complex.
 */
int
spc_dataout_pio(struct spc_softc *sc, u_char *p, int n)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	u_char intstat = 0;
	int out = 0;
#define DOUTAMOUNT 8		/* Full FIFO */

	SPC_TRACE(("spc_dataout_pio  "));
	/* send TRANSFER command. */
	bus_space_write_1(iot, ioh, TCH, n >> 16);
	bus_space_write_1(iot, ioh, TCM, n >> 8);
	bus_space_write_1(iot, ioh, TCL, n);
	bus_space_write_1(iot, ioh, PCTL, sc->sc_phase | PCTL_BFINT_ENAB);
#ifdef x68k
	bus_space_write_1(iot, ioh, SCMD, SCMD_XFR);	/* XXX */
#else
	bus_space_write_1(iot, ioh, SCMD,
	    SCMD_XFR | SCMD_PROG_XFR | SCMD_ICPT_XFR);	/* XXX */
#endif
	for (;;) {
		if ((bus_space_read_1(iot, ioh, SSTS) & SSTS_BUSY) != 0)
			break;
		if (bus_space_read_1(iot, ioh, INTS) != 0)
			break;
	}

	/*
	 * I have tried to make the main loop as tight as possible.  This
	 * means that some of the code following the loop is a bit more
	 * complex than otherwise.
	 */
	while (n > 0) {
		int xfer;

		for (;;) {
			intstat = bus_space_read_1(iot, ioh, INTS);
			/* Wait till buffer is empty. */
			if ((bus_space_read_1(iot, ioh, SSTS) &
			    SSTS_DREG_EMPTY) != 0)
				break;
			/* Break on interrupt. */
			if (intstat != 0)
				goto phasechange;
		}

		xfer = min(DOUTAMOUNT, n);

		SPC_MISC(("%d> ", xfer));

		n -= xfer;
		out += xfer;
#if 0
		bus_space_write_multi_1(iot, ioh, DREG, p, xfer);
		p += xfer;
#else
		switch (xfer) {
		case 8:
			bus_space_write_1(iot, ioh, DREG, *p++);
		case 7:
			bus_space_write_1(iot, ioh, DREG, *p++);
		case 6:
			bus_space_write_1(iot, ioh, DREG, *p++);
		case 5:
			bus_space_write_1(iot, ioh, DREG, *p++);
		case 4:
			bus_space_write_1(iot, ioh, DREG, *p++);
		case 3:
			bus_space_write_1(iot, ioh, DREG, *p++);
		case 2:
			bus_space_write_1(iot, ioh, DREG, *p++);
		case 1:
			bus_space_write_1(iot, ioh, DREG, *p++);
		}
#endif
	}

	if (out == 0) {
		for (;;) {
			if (bus_space_read_1(iot, ioh, INTS) != 0)
				break;
		}
		SPC_MISC(("extra data  "));
	} else {
		/* See the bytes off chip */
		for (;;) {
			/* Wait till buffer is empty. */
			if ((bus_space_read_1(iot, ioh, SSTS) &
			    SSTS_DREG_EMPTY) != 0)
				break;
			intstat = bus_space_read_1(iot, ioh, INTS);
			/* Break on interrupt. */
			if (intstat != 0)
				goto phasechange;
		}
	}

phasechange:
	/* Stop the FIFO data path. */

	if (intstat != 0) {
		/* Some sort of phase change. */
		int amount;

		amount = ((bus_space_read_1(iot, ioh, TCH) << 16) |
		    (bus_space_read_1(iot, ioh, TCM) << 8) |
		    bus_space_read_1(iot, ioh, TCL));
		if (amount > 0) {
			out -= amount;
			SPC_MISC(("+%d ", amount));
		}
	}

	return out;
}

/*
 * spc_datain_pio: perform data transfers using the FIFO datapath in the spc
 * Precondition: The SCSI bus should be in the DIN phase, with REQ asserted
 * and ACK deasserted (i.e. at least one byte is ready).
 *
 * For now, uses a pretty dumb algorithm, hangs around until all data has been
 * transferred.  This, is OK for fast targets, but not so smart for slow
 * targets which don't disconnect or for huge transfers.
 */
int
spc_datain_pio(struct spc_softc *sc, u_char *p, int n)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	u_int8_t intstat, sstat;
	int in = 0;
#define DINAMOUNT 8		/* Full FIFO */

	SPC_TRACE(("spc_datain_pio  "));
	/* send TRANSFER command. */
	bus_space_write_1(iot, ioh, TCH, n >> 16);
	bus_space_write_1(iot, ioh, TCM, n >> 8);
	bus_space_write_1(iot, ioh, TCL, n);
	bus_space_write_1(iot, ioh, PCTL, sc->sc_phase | PCTL_BFINT_ENAB);
#ifdef x68k
	bus_space_write_1(iot, ioh, SCMD, SCMD_XFR);	/* XXX */
#else
	bus_space_write_1(iot, ioh, SCMD,
	    SCMD_XFR | SCMD_PROG_XFR);	/* XXX */
#endif
	for (;;) {
		if ((bus_space_read_1(iot, ioh, SSTS) & SSTS_BUSY) != 0)
			break;
		if (bus_space_read_1(iot, ioh, INTS) != 0)
			goto phasechange;
	}

	/*
	 * We leave this loop if one or more of the following is true:
	 * a) phase != PH_DATAIN && FIFOs are empty
	 * b) reset has occurred or busfree is detected.
	 */
	while (n > 0) {
		int xfer;

		/* Wait for fifo half full or phase mismatch */
		for (;;) {
			intstat = bus_space_read_1(iot, ioh, INTS);
			sstat = bus_space_read_1(iot, ioh, SSTS);
			if (intstat != 0 ||
			    (sstat & SSTS_DREG_FULL) != 0 ||
			    (sstat & SSTS_DREG_EMPTY) == 0)
				break;
		}

#if 1
		if (intstat != 0)
			goto phasechange;
#else
		if (intstat != 0 &&
		    (sstat & SSTS_DREG_EMPTY) != 0)
			goto phasechange;
#endif
		if ((sstat & SSTS_DREG_FULL) != 0)
			xfer = min(DINAMOUNT, n);
		else
			xfer = 1;

		SPC_MISC((">%d ", xfer));

		n -= xfer;
		in += xfer;
#if 0
		bus_space_read_multi_1(iot, ioh, DREG, p, xfer);
		p += xfer;
#else
		switch (xfer) {
		case 8:
			*p++ = bus_space_read_1(iot, ioh, DREG);
		case 7:
			*p++ = bus_space_read_1(iot, ioh, DREG);
		case 6:
			*p++ = bus_space_read_1(iot, ioh, DREG);
		case 5:
			*p++ = bus_space_read_1(iot, ioh, DREG);
		case 4:
			*p++ = bus_space_read_1(iot, ioh, DREG);
		case 3:
			*p++ = bus_space_read_1(iot, ioh, DREG);
		case 2:
			*p++ = bus_space_read_1(iot, ioh, DREG);
		case 1:
			*p++ = bus_space_read_1(iot, ioh, DREG);
		}
#endif

		if (intstat != 0)
			goto phasechange;
	}

	/*
	 * Some SCSI-devices are rude enough to transfer more data than what
	 * was requested, e.g. 2048 bytes from a CD-ROM instead of the
	 * requested 512.  Test for progress, i.e. real transfers.  If no real
	 * transfers have been performed (n is probably already zero) and the
	 * FIFO is not empty, waste some bytes....
	 */
	if (in == 0) {
		for (;;) {
			/* XXX needs timeout */
			if (bus_space_read_1(iot, ioh, INTS) != 0)
				break;
		}
		SPC_MISC(("extra data  "));
	}

phasechange:
	/* Stop the FIFO data path. */

	return in;
}

/*
 * Catch an interrupt from the adaptor
 */
/*
 * This is the workhorse routine of the driver.
 * Deficiencies (for now):
 * 1) always uses programmed I/O
 */
int
spc_intr(void *arg)
{
	struct spc_softc *sc = arg;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	u_char ints;
	struct spc_acb *acb;
	struct scsi_link *sc_link;
	struct spc_tinfo *ti;
	int n;

	/*
	 * On LUNA-88K2, 2 spc(4)'s share the level 3 interrupt.
	 * So, first, check if this device needs to process this interrupt.
	 */
	ints = bus_space_read_1(iot, ioh, INTS);
	if (ints == 0)		/* No interrupt event on this device */
		return 0;

	/*
	 * Disable interrupt.
	 */
	bus_space_write_1(iot, ioh, SCTL,
	    bus_space_read_1(iot, ioh, SCTL) & ~SCTL_INTR_ENAB);

	SPC_TRACE(("spc_intr  "));

loop:
	/*
	 * Loop until transfer completion.
	 */
	/*
	 * First check for abnormal conditions, such as reset.
	 */
#ifdef x68k			/* XXX? */
	while ((ints = bus_space_read_1(iot, ioh, INTS)) == 0)
		delay(1);
	SPC_MISC(("ints = 0x%x  ", ints));
#else
	ints = bus_space_read_1(iot, ioh, INTS);
	SPC_MISC(("ints = 0x%x  ", ints));
#endif

	if ((ints & INTS_RST) != 0) {
		printf("%s: SCSI bus reset\n", sc->sc_dev.dv_xname);
		goto reset;
	}

	/*
	 * Check for less serious errors.
	 */
	if ((bus_space_read_1(iot, ioh, SERR) & (SERR_SCSI_PAR|SERR_SPC_PAR))
	    != 0) {
		printf("%s: SCSI bus parity error\n", sc->sc_dev.dv_xname);
		if (sc->sc_prevphase == PH_MSGIN) {
			sc->sc_flags |= SPC_DROP_MSGIN;
			spc_sched_msgout(sc, SEND_PARITY_ERROR);
		} else
			spc_sched_msgout(sc, SEND_INIT_DET_ERR);
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
	case SPC_IDLE:
	case SPC_SELECTING:
		SPC_MISC(("ints:0x%02x ", ints));

		if ((ints & INTS_SEL) != 0) {
			/*
			 * We don't currently support target mode.
			 */
			printf("%s: target mode selected; going to BUS FREE\n",
			    sc->sc_dev.dv_xname);

			goto sched;
		} else if ((ints & INTS_RESEL) != 0) {
			SPC_MISC(("reselected  "));

			/*
			 * If we're trying to select a target ourselves,
			 * push our command back into the ready list.
			 */
			if (sc->sc_state == SPC_SELECTING) {
				SPC_MISC(("backoff selector  "));
				SPC_ASSERT(sc->sc_nexus != NULL);
				acb = sc->sc_nexus;
				sc->sc_nexus = NULL;
				TAILQ_INSERT_HEAD(&sc->ready_list, acb, chain);
			}

			/* Save reselection ID. */
			sc->sc_selid = bus_space_read_1(iot, ioh, TEMP);

			sc->sc_state = SPC_RESELECTED;
		} else if ((ints & INTS_CMD_DONE) != 0) {
			SPC_MISC(("selected  "));

			/*
			 * We have selected a target. Things to do:
			 * a) Determine what message(s) to send.
			 * b) Verify that we're still selecting the target.
			 * c) Mark device as busy.
			 */
			if (sc->sc_state != SPC_SELECTING) {
				printf("%s: selection out while idle; "
				    "resetting\n", sc->sc_dev.dv_xname);
				SPC_BREAK();
				goto reset;
			}
			SPC_ASSERT(sc->sc_nexus != NULL);
			acb = sc->sc_nexus;
			sc_link = acb->xs->sc_link;
			ti = &sc->sc_tinfo[sc_link->target];

			sc->sc_msgpriq = SEND_IDENTIFY;
			if (acb->flags & ACB_RESET)
				sc->sc_msgpriq |= SEND_DEV_RESET;
			else if (acb->flags & ACB_ABORT)
				sc->sc_msgpriq |= SEND_ABORT;
			else {
#if SPC_USE_SYNCHRONOUS
				if ((ti->flags & DO_SYNC) != 0)
					sc->sc_msgpriq |= SEND_SDTR;
#endif
#if SPC_USE_WIDE
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
				timeout_add_msec(&acb->xs->stimeout,
				    acb->xs->timeout);
			sc->sc_state = SPC_CONNECTED;
		} else if ((ints & INTS_TIMEOUT) != 0) {
			SPC_MISC(("selection timeout  "));

			if (sc->sc_state != SPC_SELECTING) {
				printf("%s: selection timeout while idle; "
				    "resetting\n", sc->sc_dev.dv_xname);
				SPC_BREAK();
				goto reset;
			}
			SPC_ASSERT(sc->sc_nexus != NULL);
			acb = sc->sc_nexus;

			delay(250);

			acb->xs->error = XS_SELTIMEOUT;
			goto finish;
		} else {
			if (sc->sc_state != SPC_IDLE) {
				printf("%s: BUS FREE while not idle; "
				    "state=%d\n",
				    sc->sc_dev.dv_xname, sc->sc_state);
				SPC_BREAK();
				goto out;
			}

			goto sched;
		}

		/*
		 * Turn off selection stuff, and prepare to catch bus free
		 * interrupts, parity errors, and phase changes.
		 */

		sc->sc_flags = 0;
		sc->sc_prevphase = PH_INVALID;
		goto dophase;
	}

	if ((ints & INTS_DISCON) != 0) {
		/* disable disconnect interrupt */
		bus_space_write_1(iot, ioh, PCTL,
		    bus_space_read_1(iot, ioh, PCTL) & ~PCTL_BFINT_ENAB);
		/* XXX reset interrupt */
		bus_space_write_1(iot, ioh, INTS, ints);

		switch (sc->sc_state) {
		case SPC_RESELECTED:
			goto sched;

		case SPC_CONNECTED:
			SPC_ASSERT(sc->sc_nexus != NULL);
			acb = sc->sc_nexus;

#if SPC_USE_SYNCHRONOUS + SPC_USE_WIDE
			if (sc->sc_prevphase == PH_MSGOUT) {
				/*
				 * If the target went to BUS FREE phase during
				 * or immediately after sending a SDTR or WDTR
				 * message, disable negotiation.
				 */
				sc_link = acb->xs->sc_link;
				ti = &sc->sc_tinfo[sc_link->target];
				switch (sc->sc_lastmsg) {
#if SPC_USE_SYNCHRONOUS
				case SEND_SDTR:
					ti->flags &= ~DO_SYNC;
					ti->period = ti->offset = 0;
					break;
#endif
#if SPC_USE_WIDE
				case SEND_WDTR:
					ti->flags &= ~DO_WIDE;
					ti->width = 0;
					break;
#endif
				}
			}
#endif

			if ((sc->sc_flags & SPC_ABORTING) == 0) {
				/*
				 * Section 5.1.1 of the SCSI 2 spec suggests
				 * issuing a REQUEST SENSE following an
				 * unexpected disconnect.  Some devices go into
				 * a contingent allegiance condition when
				 * disconnecting, and this is necessary to
				 * clean up their state.
				 */
				printf("%s: unexpected disconnect; "
				    "sending REQUEST SENSE\n",
				    sc->sc_dev.dv_xname);
				SPC_BREAK();
				spc_sense(sc, acb);
				goto out;
			}

			acb->xs->error = XS_DRIVER_STUFFUP;
			goto finish;

		case SPC_DISCONNECT:
			SPC_ASSERT(sc->sc_nexus != NULL);
			acb = sc->sc_nexus;
			TAILQ_INSERT_HEAD(&sc->nexus_list, acb, chain);
			sc->sc_nexus = NULL;
			goto sched;

		case SPC_CMDCOMPLETE:
			SPC_ASSERT(sc->sc_nexus != NULL);
			acb = sc->sc_nexus;
			goto finish;
		}
	}
	else if ((ints & INTS_CMD_DONE) != 0 &&
	    sc->sc_prevphase == PH_MSGIN && sc->sc_state != SPC_CONNECTED)
		goto out;

dophase:
#if 0
	if ((bus_space_read_1(iot, ioh, PSNS) & PSNS_REQ) == 0) {
		/* Wait for REQINIT. */
		goto out;
	}
#else
	bus_space_write_1(iot, ioh, INTS, ints);
	ints = 0;
	while ((bus_space_read_1(iot, ioh, PSNS) & PSNS_REQ) == 0)
		breathe();	/* need timeout XXX */
#endif

	/*
	 * State transition.
	 */
	sc->sc_phase = bus_space_read_1(iot, ioh, PSNS) & PH_MASK;
#if 0
	bus_space_write_1(iot, ioh, PCTL, sc->sc_phase);
#endif

	SPC_MISC(("phase=%d\n", sc->sc_phase));
	switch (sc->sc_phase) {
	case PH_MSGOUT:
		if (sc->sc_state != SPC_CONNECTED &&
		    sc->sc_state != SPC_RESELECTED)
			break;
		spc_msgout(sc);
		sc->sc_prevphase = PH_MSGOUT;
		goto loop;

	case PH_MSGIN:
		if (sc->sc_state != SPC_CONNECTED &&
		    sc->sc_state != SPC_RESELECTED)
			break;
		spc_msgin(sc);
		sc->sc_prevphase = PH_MSGIN;
		goto loop;

	case PH_CMD:
		if (sc->sc_state != SPC_CONNECTED)
			break;
#ifdef SPC_DEBUG
		if ((spc_debug & SPC_SHOWMISC) != 0) {
			SPC_ASSERT(sc->sc_nexus != NULL);
			acb = sc->sc_nexus;
			printf("cmd=0x%02x+%d  ",
			    acb->scsi_cmd.opcode, acb->scsi_cmd_length - 1);
		}
#endif
		n = spc_dataout_pio(sc, sc->sc_cp, sc->sc_cleft);
		sc->sc_cp += n;
		sc->sc_cleft -= n;
		sc->sc_prevphase = PH_CMD;
		goto loop;

	case PH_DATAOUT:
		if (sc->sc_state != SPC_CONNECTED)
			break;
		SPC_MISC(("dataout dleft=%d  ", sc->sc_dleft));
		n = spc_dataout_pio(sc, sc->sc_dp, sc->sc_dleft);
		sc->sc_dp += n;
		sc->sc_dleft -= n;
		sc->sc_prevphase = PH_DATAOUT;
		goto loop;

	case PH_DATAIN:
		if (sc->sc_state != SPC_CONNECTED)
			break;
		SPC_MISC(("datain  "));
		n = spc_datain_pio(sc, sc->sc_dp, sc->sc_dleft);
		sc->sc_dp += n;
		sc->sc_dleft -= n;
		sc->sc_prevphase = PH_DATAIN;
		goto loop;

	case PH_STAT:
		if (sc->sc_state != SPC_CONNECTED)
			break;
		SPC_ASSERT(sc->sc_nexus != NULL);
		acb = sc->sc_nexus;
		/*acb->target_stat = bus_space_read_1(iot, ioh, DREG);*/
		spc_datain_pio(sc, &acb->target_stat, 1);
		SPC_MISC(("target_stat=0x%02x  ", acb->target_stat));
		sc->sc_prevphase = PH_STAT;
		goto loop;
	}

	printf("%s: unexpected bus phase; resetting\n", sc->sc_dev.dv_xname);
	SPC_BREAK();
reset:
	spc_init(sc);
	return 1;

finish:
	timeout_del(&acb->xs->stimeout);
	bus_space_write_1(iot, ioh, INTS, ints);
	ints = 0;
	spc_done(sc, acb);
	goto out;

sched:
	sc->sc_state = SPC_IDLE;
	spc_sched(sc);
	goto out;

out:
	if (ints)
		bus_space_write_1(iot, ioh, INTS, ints);
	bus_space_write_1(iot, ioh, SCTL,
	    bus_space_read_1(iot, ioh, SCTL) | SCTL_INTR_ENAB);
	return 1;
}

void
spc_abort(struct spc_softc *sc, struct spc_acb *acb)
{

	/* 2 secs for the abort */
	acb->xs->timeout = SPC_ABORT_TIMEOUT;
	acb->flags |= ACB_ABORT;

	if (acb == sc->sc_nexus) {
		/*
		 * If we're still selecting, the message will be scheduled
		 * after selection is complete.
		 */
		if (sc->sc_state == SPC_CONNECTED)
			spc_sched_msgout(sc, SEND_ABORT);
	} else {
		spc_dequeue(sc, acb);
		TAILQ_INSERT_HEAD(&sc->ready_list, acb, chain);
		if (sc->sc_state == SPC_IDLE)
			spc_sched(sc);
	}
}

void
spc_timeout(void *arg)
{
	struct spc_acb *acb = arg;
	struct scsi_xfer *xs = acb->xs;
	struct scsi_link *sc_link = xs->sc_link;
	struct spc_softc *sc = sc_link->bus->sb_adapter_softc;
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
		spc_abort(sc, acb);
	}

	splx(s);
}

#ifdef SPC_DEBUG
/*
 * The following functions are mostly used for debugging purposes, either
 * directly called from the driver or from the kernel debugger.
 */

void
spc_show_scsi_cmd(spc_acb *acb)
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
spc_print_acb(spc_acb *acb)
{

	printf("acb@%p xs=%p flags=%x", acb, acb->xs, acb->flags);
	printf(" dp=%p dleft=%d target_stat=%x\n",
	    acb->data_addr, acb->data_length, acb->target_stat);
	spc_show_scsi_cmd(acb);
}

void
spc_print_active_acb()
{
	struct spc_acb *acb;
	struct spc_softc *sc = spc_cd.cd_devs[0]; /* XXX */

	printf("ready list:\n");
	TAILQ_FOREACH(acb, &sc->ready_list, chain)
		spc_print_acb(acb);
	printf("nexus:\n");
	if (sc->sc_nexus != NULL)
		spc_print_acb(sc->sc_nexus);
	printf("nexus list:\n");
	TAILQ_FOREACH(acb, &sc->nexus_list, chain)
		spc_print_acb(acb);
}

void
spc_dump89352(struct spc_softc *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;

	printf("mb89352: BDID=%x SCTL=%x SCMD=%x TMOD=%x\n",
	    bus_space_read_1(iot, ioh, BDID),
	    bus_space_read_1(iot, ioh, SCTL),
	    bus_space_read_1(iot, ioh, SCMD),
	    bus_space_read_1(iot, ioh, TMOD));
	printf("         INTS=%x PSNS=%x SSTS=%x SERR=%x PCTL=%x\n",
	    bus_space_read_1(iot, ioh, INTS),
	    bus_space_read_1(iot, ioh, PSNS),
	    bus_space_read_1(iot, ioh, SSTS),
	    bus_space_read_1(iot, ioh, SERR),
	    bus_space_read_1(iot, ioh, PCTL));
	printf("         MBC=%x DREG=%x TEMP=%x TCH=%x TCM=%x\n",
	    bus_space_read_1(iot, ioh, MBC),
#if 0
	    bus_space_read_1(iot, ioh, DREG),
#else
	    0,
#endif
	    bus_space_read_1(iot, ioh, TEMP),
	    bus_space_read_1(iot, ioh, TCH),
	    bus_space_read_1(iot, ioh, TCM));
	printf("         TCL=%x EXBF=%x\n",
	    bus_space_read_1(iot, ioh, TCL),
	    bus_space_read_1(iot, ioh, EXBF));
}

void
spc_dump_driver(struct spc_softc *sc)
{
	struct spc_tinfo *ti;
	int i;

	printf("nexus=%p prevphase=%x\n", sc->sc_nexus, sc->sc_prevphase);
	printf("state=%x msgin=%x msgpriq=%x msgoutq=%x lastmsg=%x "
	    "currmsg=%x\n", sc->sc_state, sc->sc_imess[0],
	    sc->sc_msgpriq, sc->sc_msgoutq, sc->sc_lastmsg, sc->sc_currmsg);
	for (i = 0; i < 7; i++) {
		ti = &sc->sc_tinfo[i];
		printf("tinfo%d: %d cmds %d disconnects %d timeouts",
		    i, ti->cmds, ti->dconns, ti->touts);
		printf(" %d senses flags=%x\n", ti->senses, ti->flags);
	}
}
#endif
