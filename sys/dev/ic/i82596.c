/*	$OpenBSD: i82596.c,v 1.56 2025/04/06 00:37:07 jsg Exp $	*/
/*	$NetBSD: i82586.c,v 1.18 1998/08/15 04:42:42 mycroft Exp $	*/

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Paul Kranenburg and Charles M. Hannum.
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
/*-
 * Copyright (c) 1997 Paul Kranenburg.
 * Copyright (c) 1992, 1993, University of Vermont and State
 *  Agricultural College.
 * Copyright (c) 1992, 1993, Garrett A. Wollman.
 *
 * Portions:
 * Copyright (c) 1994, 1995, Rafal K. Boni
 * Copyright (c) 1990, 1991, William F. Jolitz
 * Copyright (c) 1990, The Regents of the University of California
 *
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of Vermont
 *	and State Agricultural College and Garrett A. Wollman, by William F.
 *	Jolitz, and by the University of California, Berkeley, Lawrence
 *	Berkeley Laboratory, and its contributors.
 * 4. Neither the names of the Universities nor the names of the authors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR AUTHORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * Intel 82586/82596 Ethernet chip
 * Register, bit, and structure definitions.
 *
 * Original StarLAN driver written by Garrett Wollman with reference to the
 * Clarkson Packet Driver code for this chip written by Russ Nelson and others.
 *
 * BPF support code taken from hpdev/if_le.c, supplied with tcpdump.
 *
 * 3C507 support is loosely based on code donated to NetBSD by Rafal Boni.
 *
 * Majorly cleaned up and 3C507 code merged by Charles Hannum.
 *
 * Converted to SUN ie driver by Charles D. Cranor,
 *		October 1994, January 1995.
 * This sun version based on i386 version 1.30.
 */
/*
 * The i82596 is a very painful chip, found in sun3's, sun-4/100's
 * sun-4/200's, and VME based suns.  The byte order is all wrong for a
 * SUN, making life difficult.  Programming this chip is mostly the same,
 * but certain details differ from system to system.  This driver is
 * written so that different "ie" interfaces can be controlled by the same
 * driver.
 */

/*
Mode of operation:

   We run the 82596 in a standard Ethernet mode.  We keep NFRAMES
   received frame descriptors around for the receiver to use, and
   NRXBUF associated receive buffer descriptors, both in a circular
   list.  Whenever a frame is received, we rotate both lists as
   necessary.  (The 596 treats both lists as a simple queue.)  We also
   keep a transmit command around so that packets can be sent off
   quickly.

   We configure the adapter in AL-LOC = 1 mode, which means that the
   Ethernet/802.3 MAC header is placed at the beginning of the receive
   buffer rather than being split off into various fields in the RFD.
   This also means that we must include this header in the transmit
   buffer as well.

   By convention, all transmit commands, and only transmit commands,
   shall have the I (IE_CMD_INTR) bit set in the command.  This way,
   when an interrupt arrives at i82596_intr(), it is immediately possible
   to tell what precisely caused it.  ANY OTHER command-sending
   routines should run at splnet(), and should post an acknowledgement
   to every interrupt they generate.

   To save the expense of shipping a command to 82596 every time we
   want to send a frame, we use a linked list of commands consisting
   of alternate XMIT and NOP commands. The links of these elements
   are manipulated (in i82596_xmit()) such that the NOP command loops back
   to itself whenever the following XMIT command is not yet ready to
   go. Whenever an XMIT is ready, the preceding NOP link is pointed
   at it, while its own link field points to the following NOP command.
   Thus, a single transmit command sets off an interlocked traversal
   of the xmit command chain, with the host processor in control of
   the synchronization.
*/

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <sys/syslog.h>
#include <sys/device.h>

#include <net/if.h>
#include <net/if_media.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <machine/bus.h>

#include <dev/ic/i82596reg.h>
#include <dev/ic/i82596var.h>

static	char *padbuf;

void	i82596_reset(struct ie_softc *, int);
void	i82596_watchdog(struct ifnet *);
int	i82596_init(struct ie_softc *);
int	i82596_ioctl(struct ifnet *, u_long, caddr_t);
void	i82596_start(struct ifnet *);

int	i82596_rint(struct ie_softc *, int);
int	i82596_tint(struct ie_softc *, int);

int   	i82596_mediachange(struct ifnet *);
void  	i82596_mediastatus(struct ifnet *, struct ifmediareq *);

int 	i82596_readframe(struct ie_softc *, int);
int	i82596_get_rbd_list(struct ie_softc *,
					     u_int16_t *, u_int16_t *, int *);
void	i82596_release_rbd_list(struct ie_softc *, u_int16_t, u_int16_t);
int	i82596_drop_frames(struct ie_softc *);
int	i82596_chk_rx_ring(struct ie_softc *);

void	i82596_start_transceiver(struct ie_softc *);
void	i82596_stop(struct ie_softc *);
void 	i82596_xmit(struct ie_softc *);

void 	i82596_setup_bufs(struct ie_softc *);
void	i82596_simple_command(struct ie_softc *, int, int);
int 	ie_cfg_setup(struct ie_softc *, int, int, int);
int	ie_ia_setup(struct ie_softc *, int);
void 	ie_run_tdr(struct ie_softc *, int);
int 	ie_mc_setup(struct ie_softc *, int);
void 	ie_mc_reset(struct ie_softc *);
int	i82596_cmd_wait(struct ie_softc *);

#ifdef I82596_DEBUG
void 	print_rbd(struct ie_softc *, int);
#endif

struct cfdriver ie_cd = {
	NULL, "ie", DV_IFNET
};

/*
 * generic i82596 probe routine
 */
int
i82596_probe(struct ie_softc *sc)
{
	int i;

	sc->scp = sc->sc_msize - IE_SCP_SZ;
	sc->iscp = 0;
	sc->scb = 32;

	(sc->ie_bus_write16)(sc, IE_ISCP_BUSY(sc->iscp), 1);
	(sc->ie_bus_write16)(sc, IE_ISCP_SCB(sc->iscp), sc->scb);
	(sc->ie_bus_write24)(sc, IE_ISCP_BASE(sc->iscp), sc->sc_maddr);
	(sc->ie_bus_write24)(sc, IE_SCP_ISCP(sc->scp), sc->sc_maddr);
	(sc->ie_bus_write16)(sc, IE_SCP_BUS_USE(sc->scp), sc->sysbus);

	(sc->hwreset)(sc, IE_CARD_RESET);

	if ((sc->ie_bus_read16)(sc, IE_ISCP_BUSY(sc->iscp))) {
#ifdef I82596_DEBUG
		printf("%s: ISCP set failed\n", sc->sc_dev.dv_xname);
#endif
		return 0;
	}

	if (sc->port) {
		(sc->ie_bus_write24)(sc, sc->scp, 0);
		(sc->ie_bus_write24)(sc, IE_SCP_TEST(sc->scp), -1);
		(sc->port)(sc, IE_PORT_TEST);
		for (i = 9000; i-- &&
			     (sc->ie_bus_read16)(sc, IE_SCP_TEST(sc->scp));
		     DELAY(100))
			;
	}

	return 1;
}

/*
 * Front-ends call this function to attach to the MI driver.
 *
 * The front-end has responsibility for managing the ICP and ISCP
 * structures. Both of these are opaque to us.  Also, the front-end
 * chooses a location for the SCB which is expected to be addressable
 * (through `sc->scb') as an offset against the shared-memory bus handle.
 *
 * The following MD interface function must be setup by the front-end
 * before calling here:
 *
 *	hwreset			- board dependent reset
 *	hwinit			- board dependent initialization
 *	chan_attn		- channel attention
 *	intrhook		- board dependent interrupt processing
 *	memcopyin		- shared memory copy: board to KVA
 *	memcopyout		- shared memory copy: KVA to board
 *	ie_bus_read16		- read a sixteen-bit i82596 pointer
 *	ie_bus_write16		- write a sixteen-bit i82596 pointer
 *	ie_bus_write24		- write a twenty-four-bit i82596 pointer
 *
 */
void
i82596_attach(struct ie_softc *sc, const char *name, u_int8_t *etheraddr,
    uint64_t *media, int nmedia, uint64_t defmedia)
{
	int i;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;

	/* Setup SCP+ISCP */
	(sc->ie_bus_write16)(sc, IE_ISCP_BUSY(sc->iscp), 1);
	(sc->ie_bus_write16)(sc, IE_ISCP_SCB(sc->iscp), sc->scb);
	(sc->ie_bus_write24)(sc, IE_ISCP_BASE(sc->iscp), sc->sc_maddr);
	(sc->ie_bus_write24)(sc, IE_SCP_ISCP(sc->scp), sc->sc_maddr +sc->iscp);
	(sc->ie_bus_write16)(sc, IE_SCP_BUS_USE(sc->scp), sc->sysbus);
	(sc->hwreset)(sc, IE_CARD_RESET);

	/* Setup Iface */
	bcopy(sc->sc_dev.dv_xname, ifp->if_xname, IFNAMSIZ);
	ifp->if_softc = sc;
	ifp->if_start = i82596_start;
	ifp->if_ioctl = i82596_ioctl;
	ifp->if_watchdog = i82596_watchdog;
	ifp->if_flags =
#ifdef I82596_DEBUG
		IFF_DEBUG |
#endif
		IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;

        /* Initialize media goo. */
        ifmedia_init(&sc->sc_media, 0, i82596_mediachange, i82596_mediastatus);
        if (media != NULL) {
                for (i = 0; i < nmedia; i++)
                        ifmedia_add(&sc->sc_media, media[i], 0, NULL);
                ifmedia_set(&sc->sc_media, defmedia);
        } else {
                ifmedia_add(&sc->sc_media, IFM_ETHER|IFM_MANUAL, 0, NULL);
                ifmedia_set(&sc->sc_media, IFM_ETHER|IFM_MANUAL);
        }

	if (padbuf == NULL) {
		padbuf = malloc(ETHER_MIN_LEN - ETHER_CRC_LEN, M_DEVBUF,
		    M_NOWAIT | M_ZERO);
		if (padbuf == NULL) {
			printf("%s: can't allocate pad buffer\n",
			    sc->sc_dev.dv_xname);
			return;
		}
	}

	/* Attach the interface. */
	if_attach(ifp);
	ether_ifattach(ifp);

	printf(" %s v%d.%d, address %s\n", name, sc->sc_vers / 10,
	       sc->sc_vers % 10, ether_sprintf(etheraddr));
}


/*
 * Device timeout/watchdog routine.
 * Entered if the device neglects to generate an interrupt after a
 * transmit has been started on it.
 */
void
i82596_watchdog(struct ifnet *ifp)
{
	struct ie_softc *sc = ifp->if_softc;

	log(LOG_ERR, "%s: device timeout\n", sc->sc_dev.dv_xname);
	++ifp->if_oerrors;

	i82596_reset(sc, 1);
}

int
i82596_cmd_wait(struct ie_softc *sc)
{
	/* spin on i82596 command acknowledge; wait at most 0.9 (!) seconds */
	int i, off;

	for (i = 180000; i--; DELAY(5)) {
		/* Read the command word */
		off = IE_SCB_CMD(sc->scb);
		bus_space_barrier(sc->bt, sc->bh, off, 2,
				  BUS_SPACE_BARRIER_READ);
		if ((sc->ie_bus_read16)(sc, off) == 0) {
#ifdef I82596_DEBUG
			if (sc->sc_debug & IED_CMDS)
				printf("%s: cmd_wait after %d usec\n",
				    sc->sc_dev.dv_xname, (180000 - i) * 5);
#endif
			return (0);
		}
	}

#ifdef I82596_DEBUG
	if (sc->sc_debug & IED_CMDS)
		printf("i82596_cmd_wait: timo(%ssync): scb status: %b\n",
		    sc->async_cmd_inprogress? "a" : "",
		    sc->ie_bus_read16(sc, IE_SCB_STATUS(sc->scb)),
		    IE_STAT_BITS);
#endif
	return (1);	/* Timeout */
}

/*
 * Send a command to the controller and wait for it to either complete
 * or be accepted, depending on the command.  If the command pointer
 * is null, then pretend that the command is not an action command.
 * If the command pointer is not null, and the command is an action
 * command, wait for one of the MASK bits to turn on in the command's
 * status field.
 * If ASYNC is set, we just call the chip's attention and return.
 * We may have to wait for the command's acceptance later though.
 */
int
i82596_start_cmd(struct ie_softc *sc, int cmd, int iecmdbuf, int mask,
    int async)
{
	int i, off;

#ifdef I82596_DEBUG
	if (sc->sc_debug & IED_CMDS)
		printf("start_cmd: %p, %x, %x, %b, %ssync\n",
		       sc, cmd, iecmdbuf, mask, IE_STAT_BITS, async?"a":"");
#endif
	if (sc->async_cmd_inprogress != 0) {
		/*
		 * If previous command was issued asynchronously, wait
		 * for it now.
		 */
		if (i82596_cmd_wait(sc) != 0)
			return (1);
		sc->async_cmd_inprogress = 0;
	}

	off = IE_SCB_CMD(sc->scb);
	(sc->ie_bus_write16)(sc, off, cmd);
	bus_space_barrier(sc->bt, sc->bh, off, 2, BUS_SPACE_BARRIER_WRITE);
	(sc->chan_attn)(sc);

	if (async) {
		sc->async_cmd_inprogress = 1;
		return (0);
	}

	if (IE_ACTION_COMMAND(cmd) && iecmdbuf) {
		int status;
		/*
		 * Now spin-lock waiting for status.  This is not a very nice
		 * thing to do, and can kill performance pretty well...
		 * According to the packet driver, the minimum timeout
		 * should be .369 seconds.
		 */
		for (i = 73800; i--; DELAY(5)) {
			/* Read the command status */
			off = IE_CMD_COMMON_STATUS(iecmdbuf);
			bus_space_barrier(sc->bt, sc->bh, off, 2,
			    BUS_SPACE_BARRIER_READ);
			status = (sc->ie_bus_read16)(sc, off);
			if (status & mask) {
#ifdef I82596_DEBUG
				if (sc->sc_debug & IED_CMDS)
					printf("%s: cmd status %b\n",
					    sc->sc_dev.dv_xname,
					    status, IE_STAT_BITS);
#endif
				return (0);
			}
		}

	} else {
		/*
		 * Otherwise, just wait for the command to be accepted.
		 */
		return (i82596_cmd_wait(sc));
	}

	/* Timeout */
	return (1);
}

/*
 * Transfer accumulated chip error counters to IF.
 */
static __inline void
i82596_count_errors(struct ie_softc *sc)
{
	int scb = sc->scb;

	sc->sc_arpcom.ac_if.if_ierrors +=
	    sc->ie_bus_read16(sc, IE_SCB_ERRCRC(scb)) +
	    sc->ie_bus_read16(sc, IE_SCB_ERRALN(scb)) +
	    sc->ie_bus_read16(sc, IE_SCB_ERRRES(scb)) +
	    sc->ie_bus_read16(sc, IE_SCB_ERROVR(scb));

	/* Clear error counters */
	sc->ie_bus_write16(sc, IE_SCB_ERRCRC(scb), 0);
	sc->ie_bus_write16(sc, IE_SCB_ERRALN(scb), 0);
	sc->ie_bus_write16(sc, IE_SCB_ERRRES(scb), 0);
	sc->ie_bus_write16(sc, IE_SCB_ERROVR(scb), 0);
}

static __inline void
i82596_rx_errors(struct ie_softc *sc, int fn, int status)
{
	log(LOG_ERR, "%s: rx error (frame# %d): %b\n", sc->sc_dev.dv_xname, fn,
	    status, IE_FD_STATUSBITS);
}

/*
 * i82596 interrupt entry point.
 */
int
i82596_intr(void *v)
{
	register struct ie_softc *sc = v;
	register u_int status;
	register int off;

	off = IE_SCB_STATUS(sc->scb);
	bus_space_barrier(sc->bt, sc->bh, off, 2, BUS_SPACE_BARRIER_READ);
	status = sc->ie_bus_read16(sc, off) /* & IE_ST_WHENCE */;

	if ((status & IE_ST_WHENCE) == 0) {
		if (sc->intrhook)
			(sc->intrhook)(sc, IE_INTR_EXIT);

		return (0);
	}

loop:
	/* Ack interrupts FIRST in case we receive more during the ISR. */
	i82596_start_cmd(sc, status & IE_ST_WHENCE, 0, 0, 1);

	if (status & (IE_ST_FR | IE_ST_RNR)) {
		if (sc->intrhook)
			(sc->intrhook)(sc, IE_INTR_ENRCV);

		if (i82596_rint(sc, status) != 0)
			goto reset;
	}

	if (status & IE_ST_CX) {
		if (sc->intrhook)
			(sc->intrhook)(sc, IE_INTR_ENSND);

		if (i82596_tint(sc, status) != 0)
			goto reset;
	}

#ifdef I82596_DEBUG
	if ((status & IE_ST_CNA) && (sc->sc_debug & IED_CNA))
		printf("%s: cna; status=%b\n", sc->sc_dev.dv_xname,
			status, IE_ST_BITS);
#endif
	if (sc->intrhook)
		(sc->intrhook)(sc, IE_INTR_LOOP);

	/*
	 * Interrupt ACK was posted asynchronously; wait for
	 * completion here before reading SCB status again.
	 *
	 * If ACK fails, try to reset the chip, in hopes that
	 * it helps.
	 */
	if (i82596_cmd_wait(sc))
		goto reset;

	bus_space_barrier(sc->bt, sc->bh, off, 2, BUS_SPACE_BARRIER_READ);
	status = sc->ie_bus_read16(sc, off);
	if ((status & IE_ST_WHENCE) != 0)
		goto loop;

out:
	if (sc->intrhook)
		(sc->intrhook)(sc, IE_INTR_EXIT);
	return (1);

reset:
	i82596_cmd_wait(sc);
	i82596_reset(sc, 1);
	goto out;
}

/*
 * Process a received-frame interrupt.
 */
int
i82596_rint(struct ie_softc *sc, int scbstatus)
{
	static int timesthru = 1024;
	register int i, status, off;

#ifdef I82596_DEBUG
	if (sc->sc_debug & IED_RINT)
		printf("%s: rint: status %b\n",
			sc->sc_dev.dv_xname, scbstatus, IE_ST_BITS);
#endif

	for (;;) {
		register int drop = 0;

		i = sc->rfhead;
		off = IE_RFRAME_STATUS(sc->rframes, i);
		bus_space_barrier(sc->bt, sc->bh, off, 2,
				  BUS_SPACE_BARRIER_READ);
		status = sc->ie_bus_read16(sc, off);

#ifdef I82596_DEBUG
		if (sc->sc_debug & IED_RINT)
			printf("%s: rint: frame(%d) status %b\n",
				sc->sc_dev.dv_xname, i, status, IE_ST_BITS);
#endif
		if ((status & IE_FD_COMPLETE) == 0) {
			if ((status & IE_FD_OK) != 0) {
				printf("%s: rint: weird: ",
					sc->sc_dev.dv_xname);
				i82596_rx_errors(sc, i, status);
				break;
			}
			if (--timesthru == 0) {
				/* Account the accumulated errors */
				i82596_count_errors(sc);
				timesthru = 1024;
			}
			break;
		} else if ((status & IE_FD_OK) == 0) {
			/*
			 * If the chip is configured to automatically
			 * discard bad frames, the only reason we can
			 * get here is an "out-of-resource" condition.
			 */
			i82596_rx_errors(sc, i, status);
			drop = 1;
		}

#ifdef I82596_DEBUG
		if ((status & IE_FD_BUSY) != 0)
			printf("%s: rint: frame(%d) busy; status=%x\n",
				sc->sc_dev.dv_xname, i, status, IE_ST_BITS);
#endif

		/*
		 * Advance the RFD list, since we're done with
		 * this descriptor.
		 */

		/* Clear frame status */
		sc->ie_bus_write16(sc, off, 0);

		/* Put fence at this frame (the head) */
		off = IE_RFRAME_LAST(sc->rframes, i);
		sc->ie_bus_write16(sc, off, IE_FD_EOL|IE_FD_SUSP);

		/* and clear RBD field */
		off = IE_RFRAME_BUFDESC(sc->rframes, i);
		sc->ie_bus_write16(sc, off, 0xffff);

		/* Remove fence from current tail */
		off = IE_RFRAME_LAST(sc->rframes, sc->rftail);
		sc->ie_bus_write16(sc, off, 0);

		if (++sc->rftail == sc->nframes)
			sc->rftail = 0;
		if (++sc->rfhead == sc->nframes)
			sc->rfhead = 0;

		/* Pull the frame off the board */
		if (drop) {
			i82596_drop_frames(sc);
			if ((status & IE_FD_RNR) != 0)
				sc->rnr_expect = 1;
			sc->sc_arpcom.ac_if.if_ierrors++;
		} else if (i82596_readframe(sc, i) != 0)
			return (1);
	}

	if ((scbstatus & IE_ST_RNR) != 0) {

		/*
		 * Receiver went "Not Ready". We try to figure out
		 * whether this was an expected event based on past
		 * frame status values.
		 */

		if ((scbstatus & IE_RUS_SUSPEND) != 0) {
			/*
			 * We use the "suspend on last frame" flag.
			 * Send a RU RESUME command in response, since
			 * we should have dealt with all completed frames
			 * by now.
			 */
			printf("RINT: SUSPENDED; scbstatus=%b\n",
				scbstatus, IE_ST_BITS);
			if (i82596_start_cmd(sc, IE_RUC_RESUME, 0, 0, 0) == 0)
				return (0);
			printf("%s: RU RESUME command timed out\n",
				sc->sc_dev.dv_xname);
			return (1);	/* Ask for a reset */
		}

		if (sc->rnr_expect != 0) {
			/*
			 * The RNR condition was announced in the previously
			 * completed frame.  Assume the receive ring is Ok,
			 * so restart the receiver without further delay.
			 */
			i82596_start_transceiver(sc);
			sc->rnr_expect = 0;
			return (0);

		} else if ((scbstatus & IE_RUS_NOSPACE) != 0) {
			/*
			 * We saw no previous IF_FD_RNR flag.
			 * We check our ring invariants and, if ok,
			 * just restart the receiver at the current
			 * point in the ring.
			 */
			if (i82596_chk_rx_ring(sc) != 0)
				return (1);

			i82596_start_transceiver(sc);
			sc->sc_arpcom.ac_if.if_ierrors++;
			return (0);
		} else
			printf("%s: receiver not ready; scbstatus=%b\n",
				sc->sc_dev.dv_xname, scbstatus, IE_ST_BITS);

		sc->sc_arpcom.ac_if.if_ierrors++;
		return (1);	/* Ask for a reset */
	}

	return (0);
}

/*
 * Process a command-complete interrupt.  These are only generated by the
 * transmission of frames.  This routine is deceptively simple, since most
 * of the real work is done by i82596_start().
 */
int
i82596_tint(struct ie_softc *sc, int scbstatus)
{
	register struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	register int off, status;

	ifp->if_timer = 0;
	ifq_clr_oactive(&ifp->if_snd);

#ifdef I82596_DEBUG
	if (sc->xmit_busy <= 0) {
		printf("%s: i82596_tint: WEIRD:"
		       "xmit_busy=%d, xctail=%d, xchead=%d\n",
		       sc->sc_dev.dv_xname,
		       sc->xmit_busy, sc->xctail, sc->xchead);
		return (0);
	}
#endif

	off = IE_CMD_XMIT_STATUS(sc->xmit_cmds, sc->xctail);
	status = sc->ie_bus_read16(sc, off);

#ifdef I82596_DEBUG
	if (sc->sc_debug & IED_TINT)
		printf("%s: tint: SCB status %b; xmit status %b\n",
			sc->sc_dev.dv_xname, scbstatus, IE_ST_BITS,
			status, IE_XS_BITS);
#endif

	if ((status & (IE_STAT_COMPL|IE_STAT_BUSY)) == IE_STAT_BUSY) {
		printf("%s: i82596_tint: command still busy;"
		       "status=%b; tail=%d\n", sc->sc_dev.dv_xname,
		       status, IE_XS_BITS, sc->xctail);
		printf("iestatus = %b\n", scbstatus, IE_ST_BITS);
	}

	if (status & IE_STAT_OK) {
		ifp->if_collisions += (status & IE_XS_MAXCOLL);
	} else {
		ifp->if_oerrors++;
		/*
		 * Check SQE and DEFERRED?
		 * What if more than one bit is set?
		 */
#ifdef I82596_DEBUG
		if (status & IE_STAT_ABORT)
			printf("%s: send aborted\n", sc->sc_dev.dv_xname);
		else if (status & IE_XS_NOCARRIER)
			printf("%s: no carrier\n", sc->sc_dev.dv_xname);
		else if (status & IE_XS_LOSTCTS)
			printf("%s: lost CTS\n", sc->sc_dev.dv_xname);
		else if (status & IE_XS_UNDERRUN)
			printf("%s: DMA underrun\n", sc->sc_dev.dv_xname);
		else
#endif /* I82596_DEBUG */
		if (status & IE_XS_EXCMAX) {
#ifdef I82596_DEBUG
			printf("%s: too many collisions\n",
				sc->sc_dev.dv_xname);
#endif /* I82596_DEBUG */
			sc->sc_arpcom.ac_if.if_collisions += 16;
		}
	}

	/*
	 * If multicast addresses were added or deleted while transmitting,
	 * ie_mc_reset() set the want_mcsetup flag indicating that we
	 * should do it.
	 */
	if (sc->want_mcsetup) {
		ie_mc_setup(sc, IE_XBUF_ADDR(sc, sc->xctail));
		sc->want_mcsetup = 0;
	}

	/* Done with the buffer. */
	sc->xmit_busy--;
	sc->xctail = (sc->xctail + 1) % NTXBUF;

	/* Start the next packet, if any, transmitting. */
	if (sc->xmit_busy > 0)
		i82596_xmit(sc);

	i82596_start(ifp);
	return (0);
}

/*
 * Get a range of receive buffer descriptors that represent one packet.
 */
int
i82596_get_rbd_list(struct ie_softc *sc, u_int16_t *start, u_int16_t *end,
    int *pktlen)
{
	int	off, rbbase = sc->rbds;
	int	rbindex, count = 0;
	int	plen = 0;
	int	rbdstatus;

	*start = rbindex = sc->rbhead;

	do {
		off = IE_RBD_STATUS(rbbase, rbindex);
		bus_space_barrier(sc->bt, sc->bh, off, 2,
				  BUS_SPACE_BARRIER_READ);
		rbdstatus = sc->ie_bus_read16(sc, off);
		if ((rbdstatus & IE_RBD_USED) == 0) {
			/*
			 * This means we are somehow out of sync.  So, we
			 * reset the adapter.
			 */
#ifdef I82596_DEBUG
			print_rbd(sc, rbindex);
#endif
			log(LOG_ERR,
			    "%s: receive descriptors out of sync at %d\n",
			    sc->sc_dev.dv_xname, rbindex);
			return (0);
		}
		plen += (rbdstatus & IE_RBD_CNTMASK);

		if (++rbindex == sc->nrxbuf)
			rbindex = 0;

		++count;
	} while ((rbdstatus & IE_RBD_LAST) == 0);
	*end = rbindex;
	*pktlen = plen;
	return (count);
}


/*
 * Release a range of receive buffer descriptors after we've copied the packet.
 */
void
i82596_release_rbd_list(struct ie_softc *sc, u_int16_t start, u_int16_t end)
{
	register int	off, rbbase = sc->rbds;
	register int	rbindex = start;

	do {
		/* Clear buffer status */
		off = IE_RBD_STATUS(rbbase, rbindex);
		sc->ie_bus_write16(sc, off, 0);
		if (++rbindex == sc->nrxbuf)
			rbindex = 0;
	} while (rbindex != end);

	/* Mark EOL at new tail */
	rbindex = ((rbindex == 0) ? sc->nrxbuf : rbindex) - 1;
	off = IE_RBD_BUFLEN(rbbase, rbindex);
	sc->ie_bus_write16(sc, off, IE_RBUF_SIZE|IE_RBD_EOL);

	/* Remove EOL from current tail */
	off = IE_RBD_BUFLEN(rbbase, sc->rbtail);
	sc->ie_bus_write16(sc, off, IE_RBUF_SIZE);

	/* New head & tail pointer */
/* hmm, why have both? head is always (tail + 1) % NRXBUF */
	sc->rbhead = end;
	sc->rbtail = rbindex;
}

/*
 * Drop the packet at the head of the RX buffer ring.
 * Called if the frame descriptor reports an error on this packet.
 * Returns 1 if the buffer descriptor ring appears to be corrupt;
 * and 0 otherwise.
 */
int
i82596_drop_frames(struct ie_softc *sc)
{
	u_int16_t bstart, bend;
	int pktlen;

	if (!i82596_get_rbd_list(sc, &bstart, &bend, &pktlen))
		return (1);
	i82596_release_rbd_list(sc, bstart, bend);
	return (0);
}

/*
 * Check the RX frame & buffer descriptor lists for our invariants,
 * i.e.: EOL bit set iff. it is pointed at by the r*tail pointer.
 *
 * Called when the receive unit has stopped unexpectedly.
 * Returns 1 if an inconsistency is detected; 0 otherwise.
 *
 * The Receive Unit is expected to be NOT RUNNING.
 */
int
i82596_chk_rx_ring(struct ie_softc *sc)
{
	int n, off, val;

	for (n = 0; n < sc->nrxbuf; n++) {
		off = IE_RBD_BUFLEN(sc->rbds, n);
		val = sc->ie_bus_read16(sc, off);
		if ((n == sc->rbtail) ^ ((val & IE_RBD_EOL) != 0)) {
			/* `rbtail' and EOL flag out of sync */
			log(LOG_ERR,
			    "%s: rx buffer descriptors out of sync at %d\n",
			    sc->sc_dev.dv_xname, n);
			return (1);
		}

		/* Take the opportunity to clear the status fields here ? */
	}

	for (n = 0; n < sc->nframes; n++) {
		off = IE_RFRAME_LAST(sc->rframes, n);
		val = sc->ie_bus_read16(sc, off);
		if ((n == sc->rftail) ^ ((val & (IE_FD_EOL|IE_FD_SUSP)) != 0)) {
			/* `rftail' and EOL flag out of sync */
			log(LOG_ERR,
			    "%s: rx frame list out of sync at %d\n",
			    sc->sc_dev.dv_xname, n);
			return (1);
		}
	}

	return (0);
}

/*
 * Read data off the interface, and turn it into an mbuf chain.
 *
 * This code is DRAMATICALLY different from the previous version; this
 * version tries to allocate the entire mbuf chain up front, given the
 * length of the data available.  This enables us to allocate mbuf
 * clusters in many situations where before we would have had a long
 * chain of partially-full mbufs.  This should help to speed up the
 * operation considerably.  (Provided that it works, of course.)
 */
static __inline__ struct mbuf *
i82596_get(struct ie_softc *sc, int head, int totlen)
{
	struct mbuf *m, *m0, *newm;
	int off, len, resid;
	int thisrboff, thismboff;
	struct ether_header eh;

	/*
	 * Snarf the Ethernet header.
	 */
	(sc->memcopyin)(sc, &eh, IE_RBUF_ADDR(sc, head),
	    sizeof(struct ether_header));

	resid = totlen;

	MGETHDR(m0, M_DONTWAIT, MT_DATA);
	if (m0 == NULL)
		return (0);
	m0->m_pkthdr.len = totlen;
	len = MHLEN;
	m = m0;

	/*
	 * This loop goes through and allocates mbufs for all the data we will
	 * be copying in.  It does not actually do the copying yet.
	 */
	while (totlen > 0) {
		if (totlen >= MINCLSIZE) {
			MCLGET(m, M_DONTWAIT);
			if ((m->m_flags & M_EXT) == 0)
				goto bad;
			len = MCLBYTES;
		}

		if (m == m0) {
			caddr_t newdata = (caddr_t)
			    ALIGN(m->m_data + sizeof(struct ether_header)) -
			    sizeof(struct ether_header);
			len -= newdata - m->m_data;
			m->m_data = newdata;
		}

		m->m_len = len = min(totlen, len);

		totlen -= len;
		if (totlen > 0) {
			MGET(newm, M_DONTWAIT, MT_DATA);
			if (newm == NULL)
				goto bad;
			len = MLEN;
			m = m->m_next = newm;
		}
	}

	m = m0;
	thismboff = 0;

	/*
	 * Copy the Ethernet header into the mbuf chain.
	 */
	bcopy(&eh, mtod(m, caddr_t), sizeof(struct ether_header));
	thismboff = sizeof(struct ether_header);
	thisrboff = sizeof(struct ether_header);
	resid -= sizeof(struct ether_header);

	/*
	 * Now we take the mbuf chain (hopefully only one mbuf most of the
	 * time) and stuff the data into it.  There are no possible failures
	 * at or after this point.
	 */
	while (resid > 0) {
		int thisrblen = IE_RBUF_SIZE - thisrboff,
		    thismblen = m->m_len - thismboff;
		len = min(thisrblen, thismblen);

		off = IE_RBUF_ADDR(sc,head) + thisrboff;
		(sc->memcopyin)(sc, mtod(m, caddr_t) + thismboff, off, len);
		resid -= len;

		if (len == thismblen) {
			m = m->m_next;
			thismboff = 0;
		} else
			thismboff += len;

		if (len == thisrblen) {
			if (++head == sc->nrxbuf)
				head = 0;
			thisrboff = 0;
		} else
			thisrboff += len;
	}

	/*
	 * Unless something changed strangely while we were doing the copy,
	 * we have now copied everything in from the shared memory.
	 * This means that we are done.
	 */
	return (m0);

bad:
	m_freem(m0);
	return (NULL);
}

/*
 * Read frame NUM from unit UNIT (pre-cached as IE).
 *
 * This routine reads the RFD at NUM, and copies in the buffers from the list
 * of RBD, then rotates the RBD list so that the receiver doesn't start
 * complaining.  Trailers are DROPPED---there's no point in wasting time
 * on confusing code to deal with them.  Hopefully, this machine will
 * never ARP for trailers anyway.
 */
int
i82596_readframe(struct ie_softc *sc, int num)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct mbuf *m;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	u_int16_t bstart, bend;
	int pktlen;

	if (i82596_get_rbd_list(sc, &bstart, &bend, &pktlen) == 0) {
		ifp->if_ierrors++;
		return (1);
	}

	m = i82596_get(sc, bstart, pktlen);
	i82596_release_rbd_list(sc, bstart, bend);

	if (m == NULL) {
		sc->sc_arpcom.ac_if.if_ierrors++;
		return (0);
	}

#ifdef I82596_DEBUG
	if (sc->sc_debug & IED_READFRAME) {
		struct ether_header *eh = mtod(m, struct ether_header *);

		printf("%s: frame from ether %s type 0x%x len %d\n",
		    sc->sc_dev.dv_xname, ether_sprintf(eh->ether_shost),
		    (u_int)eh->ether_type, pktlen);
	}
#endif

	ml_enqueue(&ml, m);
	if_input(ifp, &ml);
	return (0);
}


/*
 * Setup all necessary artifacts for an XMIT command, and then pass the XMIT
 * command to the chip to be executed.
 */
void
i82596_xmit(struct ie_softc *sc)
{
	int off, cur, prev;

	cur = sc->xctail;

#ifdef I82596_DEBUG
	if (sc->sc_debug & IED_XMIT)
		printf("%s: xmit buffer %d\n", sc->sc_dev.dv_xname, cur);
#endif

	/*
	 * Setup the transmit command.
	 */
	sc->ie_bus_write16(sc, IE_CMD_XMIT_DESC(sc->xmit_cmds, cur),
			       IE_XBD_ADDR(sc->xbds, cur));

	sc->ie_bus_write16(sc, IE_CMD_XMIT_STATUS(sc->xmit_cmds, cur), 0);

	if (sc->do_xmitnopchain) {
		/*
		 * Gate this XMIT command to the following NOP
		 */
		sc->ie_bus_write16(sc, IE_CMD_XMIT_LINK(sc->xmit_cmds, cur),
				       IE_CMD_NOP_ADDR(sc->nop_cmds, cur));
		sc->ie_bus_write16(sc, IE_CMD_XMIT_CMD(sc->xmit_cmds, cur),
				       IE_CMD_XMIT | IE_CMD_INTR);

		/*
		 * Loopback at following NOP
		 */
		sc->ie_bus_write16(sc, IE_CMD_NOP_STATUS(sc->nop_cmds, cur), 0);
		sc->ie_bus_write16(sc, IE_CMD_NOP_LINK(sc->nop_cmds, cur),
				       IE_CMD_NOP_ADDR(sc->nop_cmds, cur));

		/*
		 * Gate preceding NOP to this XMIT command
		 */
		prev = (cur + NTXBUF - 1) % NTXBUF;
		sc->ie_bus_write16(sc, IE_CMD_NOP_STATUS(sc->nop_cmds, prev), 0);
		sc->ie_bus_write16(sc, IE_CMD_NOP_LINK(sc->nop_cmds, prev),
				       IE_CMD_XMIT_ADDR(sc->xmit_cmds, cur));

		off = IE_SCB_STATUS(sc->scb);
		bus_space_barrier(sc->bt, sc->bh, off, 2,
				  BUS_SPACE_BARRIER_READ);
		if ((sc->ie_bus_read16(sc, off) & IE_CUS_ACTIVE) == 0) {
			printf("i82596_xmit: CU not active\n");
			i82596_start_transceiver(sc);
		}
	} else {
		sc->ie_bus_write16(sc, IE_CMD_XMIT_LINK(sc->xmit_cmds,cur),
				       0xffff);

		sc->ie_bus_write16(sc, IE_CMD_XMIT_CMD(sc->xmit_cmds, cur),
				       IE_CMD_XMIT | IE_CMD_INTR | IE_CMD_LAST);

		off = IE_SCB_CMDLST(sc->scb);
		sc->ie_bus_write16(sc, off, IE_CMD_XMIT_ADDR(sc->xmit_cmds, cur));
		bus_space_barrier(sc->bt, sc->bh, off, 2,
				  BUS_SPACE_BARRIER_WRITE);

		if (i82596_start_cmd(sc, IE_CUC_START, 0, 0, 1)) {
#ifdef I82596_DEBUG
			if (sc->sc_debug & IED_XMIT)
				printf("%s: i82596_xmit: "
				    "start xmit command timed out\n",
				       sc->sc_dev.dv_xname);
#endif
		}
	}

	sc->sc_arpcom.ac_if.if_timer = 5;
}


/*
 * Start transmission on an interface.
 */
void
i82596_start(struct ifnet *ifp)
{
	struct ie_softc *sc = ifp->if_softc;
	struct mbuf *m0, *m;
	int	buffer, head, xbase;
	u_short	len;

#ifdef I82596_DEBUG
	if (sc->sc_debug & IED_ENQ)
		printf("i82596_start(%p)\n", ifp);
#endif

	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		return;

	for (;;) {
		if (sc->xmit_busy == NTXBUF) {
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		m0 = ifq_dequeue(&ifp->if_snd);
		if (m0 == NULL)
			break;

		/* We need to use m->m_pkthdr.len, so require the header */
		if ((m0->m_flags & M_PKTHDR) == 0)
			panic("i82596_start: no header mbuf");

#if NBPFILTER > 0
		/* Tap off here if there is a BPF listener. */
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, m0, BPF_DIRECTION_OUT);
#endif

		if (m0->m_pkthdr.len > IE_TBUF_SIZE)
			printf("%s: tbuf overflow\n", sc->sc_dev.dv_xname);

		head = sc->xchead;
		sc->xchead = (head + 1) % NTXBUF;
		buffer = IE_XBUF_ADDR(sc, head);

#ifdef I82596_DEBUG
		if (sc->sc_debug & IED_ENQ)
			printf("%s: fill buffer %d offset %x",
			    sc->sc_dev.dv_xname, head, buffer);
#endif

		for (m = m0; m != 0; m = m->m_next) {
#ifdef I82596_DEBUG
			if (sc->sc_debug & IED_ENQ) {
				u_int8_t *e, *p = mtod(m, u_int8_t *);
				static int i;
				if (m == m0)
					i = 0;
				for (e = p + m->m_len; p < e; i++, p += 2) {
					if (!(i % 8))
						printf("\n%s:",
						    sc->sc_dev.dv_xname);
					printf(" %02x%02x", p[0], p[1]);
				}
			}
#endif
			(sc->memcopyout)(sc, mtod(m,caddr_t), buffer, m->m_len);
			buffer += m->m_len;
		}

		len = m0->m_pkthdr.len;
		if (len < ETHER_MIN_LEN - ETHER_CRC_LEN) {
			(sc->memcopyout)(sc, padbuf, buffer,
			    ETHER_MIN_LEN - ETHER_CRC_LEN - len);
			buffer += ETHER_MIN_LEN - ETHER_CRC_LEN - len;
			len = ETHER_MIN_LEN - ETHER_CRC_LEN;
		}

#ifdef I82596_DEBUG
		if (sc->sc_debug & IED_ENQ)
			printf("\n");
#endif

		m_freem(m0);

		/*
		 * Setup the transmit buffer descriptor here, while we
		 * know the packet's length.
		 */
		xbase = sc->xbds;
		sc->ie_bus_write16(sc, IE_XBD_FLAGS(xbase, head),
				       len | IE_TBD_EOL);
		sc->ie_bus_write16(sc, IE_XBD_NEXT(xbase, head), 0xffff);
		sc->ie_bus_write24(sc, IE_XBD_BUF(xbase, head),
				       sc->sc_maddr + IE_XBUF_ADDR(sc, head));

		/* Start the first packet transmitting. */
		if (sc->xmit_busy++ == 0)
			i82596_xmit(sc);
	}
}

/*
 * Probe IE's ram setup   [ Move all this into MD front-end!? ]
 * Use only if SCP and ISCP represent offsets into shared ram space.
 */
int
i82596_proberam(struct ie_softc *sc)
{
	int result, off;

	/* Put in 16-bit mode */
	off = IE_SCP_BUS_USE(sc->scp);
	(sc->ie_bus_write16)(sc, off, 0);
	bus_space_barrier(sc->bt, sc->bh, off, 2, BUS_SPACE_BARRIER_WRITE);

	/* Set the ISCP `busy' bit */
	off = IE_ISCP_BUSY(sc->iscp);
	(sc->ie_bus_write16)(sc, off, 1);
	bus_space_barrier(sc->bt, sc->bh, off, 2, BUS_SPACE_BARRIER_WRITE);

	if (sc->hwreset)
		(sc->hwreset)(sc, IE_CHIP_PROBE);

	(sc->chan_attn) (sc);

	DELAY(100);		/* wait a while... */

	/* Read back the ISCP `busy' bit; it should be clear by now */
	off = IE_ISCP_BUSY(sc->iscp);
	bus_space_barrier(sc->bt, sc->bh, off, 1, BUS_SPACE_BARRIER_READ);
	result = (sc->ie_bus_read16)(sc, off) == 0;

	/* Acknowledge any interrupts we may have caused. */
	ie_ack(sc, IE_ST_WHENCE);

	return (result);
}

void
i82596_reset(struct ie_softc *sc, int hard)
{
	int s = splnet();

#ifdef I82596_DEBUG
	if (hard)
		printf("%s: reset\n", sc->sc_dev.dv_xname);
#endif

	/* Clear OACTIVE in case we're called from watchdog (frozen xmit). */
	sc->sc_arpcom.ac_if.if_timer = 0;
	ifq_clr_oactive(&sc->sc_arpcom.ac_if.if_snd);

	/*
	 * Stop i82596 dead in its tracks.
	 */
	if (i82596_start_cmd(sc, IE_RUC_ABORT | IE_CUC_ABORT, 0, 0, 0)) {
#ifdef I82596_DEBUG
		printf("%s: abort commands timed out\n", sc->sc_dev.dv_xname);
#endif
	}

	/*
	 * This can really slow down the i82596_reset() on some cards, but it's
	 * necessary to unwedge other ones (eg, the Sun VME ones) from certain
	 * lockups.
	 */
	if (hard && sc->hwreset)
		(sc->hwreset)(sc, IE_CARD_RESET);

	DELAY(100);
	ie_ack(sc, IE_ST_WHENCE);

	if ((sc->sc_arpcom.ac_if.if_flags & IFF_UP) != 0) {
		int retries=0;	/* XXX - find out why init sometimes fails */
		while (retries++ < 2)
			if (i82596_init(sc) == 1)
				break;
	}

	splx(s);
}

void
i82596_simple_command(struct ie_softc *sc, int cmd, int cmdbuf)
{
	/* Setup a simple command */
	sc->ie_bus_write16(sc, IE_CMD_COMMON_STATUS(cmdbuf), 0);
	sc->ie_bus_write16(sc, IE_CMD_COMMON_CMD(cmdbuf), cmd | IE_CMD_LAST);
	sc->ie_bus_write16(sc, IE_CMD_COMMON_LINK(cmdbuf), 0xffff);

	/* Assign the command buffer to the SCB command list */
	sc->ie_bus_write16(sc, IE_SCB_CMDLST(sc->scb), cmdbuf);
}

/*
 * Run the time-domain reflectometer.
 */
void
ie_run_tdr(struct ie_softc *sc, int cmd)
{
	int result, clocks;

	i82596_simple_command(sc, IE_CMD_TDR, cmd);
	(sc->ie_bus_write16)(sc, IE_CMD_TDR_TIME(cmd), 0);

	if (i82596_start_cmd(sc, IE_CUC_START, cmd, IE_STAT_COMPL, 0) ||
	    !(sc->ie_bus_read16(sc, IE_CMD_COMMON_STATUS(cmd)) & IE_STAT_OK))
		result = 0x10000; /* XXX */
	else
		result = sc->ie_bus_read16(sc, IE_CMD_TDR_TIME(cmd));

	/* Squash any pending interrupts */
	ie_ack(sc, IE_ST_WHENCE);

	if (result & IE_TDR_SUCCESS)
		return;

	clocks = result & IE_TDR_TIME;
	if (result & 0x10000)
		printf("%s: TDR command failed\n", sc->sc_dev.dv_xname);
	else if (result & IE_TDR_XCVR) {
#ifdef I82596_DEBUG
		printf("%s: transceiver problem\n", sc->sc_dev.dv_xname);
#endif
	} else if (result & IE_TDR_OPEN)
		printf("%s: TDR detected an open %d clock%s away\n",
		    sc->sc_dev.dv_xname, clocks, clocks == 1? "":"s");
	else if (result & IE_TDR_SHORT)
		printf("%s: TDR detected a short %d clock%s away\n",
		    sc->sc_dev.dv_xname, clocks, clocks == 1? "":"s");
	else
		printf("%s: TDR returned unknown status 0x%x\n",
		    sc->sc_dev.dv_xname, result);
}

/*
 * i82596_setup_bufs: set up the buffers
 *
 * We have a block of KVA at sc->buf_area which is of size sc->buf_area_sz.
 * this is to be used for the buffers.  The chip indexes its control data
 * structures with 16 bit offsets, and it indexes actual buffers with
 * 24 bit addresses.   So we should allocate control buffers first so that
 * we don't overflow the 16 bit offset field.   The number of transmit
 * buffers is fixed at compile time.
 *
 */
void
i82596_setup_bufs(struct ie_softc *sc)
{
	int n, r, ptr = sc->buf_area;	/* memory pool */
	int cl = 32;

	/*
	 * step 0: zero memory and figure out how many recv buffers and
	 * frames we can have.
	 */
	ptr = (ptr + cl - 1) & ~(cl - 1); /* set alignment and stick with it */

	/*
	 *  step 1: lay out data structures in the shared-memory area
	 */

	/* The no-op commands; used if "nop-chaining" is in effect */
	ptr += cl;
	sc->nop_cmds = ptr - 2;
	ptr += NTXBUF * 32;

	/* The transmit commands */
	ptr += cl;
	sc->xmit_cmds = ptr - 2;
	ptr += NTXBUF * 32;

	/* The transmit buffers descriptors */
	ptr += cl;
	sc->xbds = ptr - 2;
	ptr += NTXBUF * 32;

	/* The transmit buffers */
	sc->xbufs = ptr;
	ptr += NTXBUF * IE_TBUF_SIZE;

	ptr = (ptr + cl - 1) & ~(cl - 1);	/* re-align.. just in case */

	/* Compute free space for RECV stuff */
	n = sc->buf_area_sz - (ptr - sc->buf_area);

	/* Compute size of one RECV frame */
	r = 64 + ((32 + IE_RBUF_SIZE) * B_PER_F);

	sc->nframes = n / r;

	if (sc->nframes <= 8)
		panic("ie: bogus buffer calc");

	sc->nrxbuf = sc->nframes * B_PER_F;

	/* The receive frame descriptors */
	ptr += cl;
	sc->rframes = ptr - 2;
	ptr += sc->nframes * 64;

	/* The receive buffer descriptors */
	ptr += cl;
	sc->rbds = ptr - 2;
	ptr += sc->nrxbuf * 32;

	/* The receive buffers */
	sc->rbufs = ptr;
	ptr += sc->nrxbuf * IE_RBUF_SIZE;

#ifdef I82596_DEBUG
	printf("%s: %d frames %d bufs\n", sc->sc_dev.dv_xname, sc->nframes,
		sc->nrxbuf);
#endif

	/*
	 * step 2: link together the recv frames and set EOL on last one
	 */
	for (n = 0; n < sc->nframes; n++) {
		int m = (n == sc->nframes - 1) ? 0 : n + 1;

		/* Clear status */
		sc->ie_bus_write16(sc, IE_RFRAME_STATUS(sc->rframes,n), 0);

		/* RBD link = NULL */
		sc->ie_bus_write16(sc, IE_RFRAME_BUFDESC(sc->rframes,n),
				       0xffff);

		/* Make a circular list */
		sc->ie_bus_write16(sc, IE_RFRAME_NEXT(sc->rframes,n),
				       IE_RFRAME_ADDR(sc->rframes,m));

		/* Mark last as EOL */
		sc->ie_bus_write16(sc, IE_RFRAME_LAST(sc->rframes,n),
				       ((m==0)? (IE_FD_EOL|IE_FD_SUSP) : 0));
	}

	/*
	 * step 3: link the RBDs and set EOL on last one
	 */
	for (n = 0; n < sc->nrxbuf; n++) {
		int m = (n == sc->nrxbuf - 1) ? 0 : n + 1;

		/* Clear status */
		sc->ie_bus_write16(sc, IE_RBD_STATUS(sc->rbds,n), 0);

		/* Make a circular list */
		sc->ie_bus_write16(sc, IE_RBD_NEXT(sc->rbds,n),
				       IE_RBD_ADDR(sc->rbds,m));

		/* Link to data buffers */
		sc->ie_bus_write24(sc, IE_RBD_BUFADDR(sc->rbds, n),
				       sc->sc_maddr + IE_RBUF_ADDR(sc, n));
		sc->ie_bus_write16(sc, IE_RBD_BUFLEN(sc->rbds,n),
				       IE_RBUF_SIZE | ((m==0)?IE_RBD_EOL:0));
	}

	/*
	 * step 4: all xmit no-op commands loopback onto themselves
	 */
	for (n = 0; n < NTXBUF; n++) {
		(sc->ie_bus_write16)(sc, IE_CMD_NOP_STATUS(sc->nop_cmds, n), 0);

		(sc->ie_bus_write16)(sc, IE_CMD_NOP_CMD(sc->nop_cmds, n),
					 IE_CMD_NOP);

		(sc->ie_bus_write16)(sc, IE_CMD_NOP_LINK(sc->nop_cmds, n),
					 IE_CMD_NOP_ADDR(sc->nop_cmds, n));
	}


	/*
	 * step 6: set the head and tail pointers on receive to keep track of
	 * the order in which RFDs and RBDs are used.
	 */

	/* Pointers to last packet sent and next available transmit buffer. */
	sc->xchead = sc->xctail = 0;

	/* Clear transmit-busy flag and set number of free transmit buffers. */
	sc->xmit_busy = 0;

	/*
	 * Pointers to first and last receive frame.
	 * The RFD pointed to by rftail is the only one that has EOL set.
	 */
	sc->rfhead = 0;
	sc->rftail = sc->nframes - 1;

	/*
	 * Pointers to first and last receive descriptor buffer.
	 * The RBD pointed to by rbtail is the only one that has EOL set.
	 */
	sc->rbhead = 0;
	sc->rbtail = sc->nrxbuf - 1;

/* link in recv frames * and buffer into the scb. */
#ifdef I82596_DEBUG
	printf("%s: reserved %d bytes\n",
		sc->sc_dev.dv_xname, ptr - sc->buf_area);
#endif
}

int
ie_cfg_setup(struct ie_softc *sc, int cmd, int promiscuous, int manchester)
{
	int cmdresult, status;

	i82596_simple_command(sc, IE_CMD_CONFIG, cmd);
	bus_space_write_1(sc->bt, sc->bh, IE_CMD_CFG_CNT(cmd), 0x0c);
	bus_space_write_1(sc->bt, sc->bh, IE_CMD_CFG_FIFO(cmd), 8);
	bus_space_write_1(sc->bt, sc->bh, IE_CMD_CFG_SAVEBAD(cmd), 0x40);
	bus_space_write_1(sc->bt, sc->bh, IE_CMD_CFG_ADDRLEN(cmd), 0x2e);
	bus_space_write_1(sc->bt, sc->bh, IE_CMD_CFG_PRIORITY(cmd), 0);
	bus_space_write_1(sc->bt, sc->bh, IE_CMD_CFG_IFS(cmd), 0x60);
	bus_space_write_1(sc->bt, sc->bh, IE_CMD_CFG_SLOT_LOW(cmd), 0);
	bus_space_write_1(sc->bt, sc->bh, IE_CMD_CFG_SLOT_HIGH(cmd), 0xf2);
	bus_space_write_1(sc->bt, sc->bh, IE_CMD_CFG_PROMISC(cmd),
					  !!promiscuous | manchester << 2);
	bus_space_write_1(sc->bt, sc->bh, IE_CMD_CFG_CRSCDT(cmd), 0);
	bus_space_write_1(sc->bt, sc->bh, IE_CMD_CFG_MINLEN(cmd), 64);
	bus_space_write_1(sc->bt, sc->bh, IE_CMD_CFG_JUNK(cmd), 0xff);
	bus_space_barrier(sc->bt, sc->bh, cmd, IE_CMD_CFG_SZ,
			  BUS_SPACE_BARRIER_WRITE);

	cmdresult = i82596_start_cmd(sc, IE_CUC_START, cmd, IE_STAT_COMPL, 0);
	status = sc->ie_bus_read16(sc, IE_CMD_COMMON_STATUS(cmd));
	if (cmdresult != 0) {
		printf("%s: configure command timed out; status %b\n",
			sc->sc_dev.dv_xname, status, IE_STAT_BITS);
		return (0);
	}
	if ((status & IE_STAT_OK) == 0) {
		printf("%s: configure command failed; status %b\n",
			sc->sc_dev.dv_xname, status, IE_STAT_BITS);
		return (0);
	}

	/* Squash any pending interrupts */
	ie_ack(sc, IE_ST_WHENCE);
	return (1);
}

int
ie_ia_setup(struct ie_softc *sc, int cmdbuf)
{
	int cmdresult, status;

	i82596_simple_command(sc, IE_CMD_IASETUP, cmdbuf);

	(sc->memcopyout)(sc, sc->sc_arpcom.ac_enaddr,
			 IE_CMD_IAS_EADDR(cmdbuf), ETHER_ADDR_LEN);

	cmdresult = i82596_start_cmd(sc, IE_CUC_START, cmdbuf, IE_STAT_COMPL, 0);
	status = sc->ie_bus_read16(sc, IE_CMD_COMMON_STATUS(cmdbuf));
	if (cmdresult != 0) {
		printf("%s: individual address command timed out; status %b\n",
			sc->sc_dev.dv_xname, status, IE_STAT_BITS);
		return (0);
	}
	if ((status & IE_STAT_OK) == 0) {
		printf("%s: individual address command failed; status %b\n",
			sc->sc_dev.dv_xname, status, IE_STAT_BITS);
		return (0);
	}

	/* Squash any pending interrupts */
	ie_ack(sc, IE_ST_WHENCE);
	return (1);
}

/*
 * Run the multicast setup command.
 * Called at splnet().
 */
int
ie_mc_setup(struct ie_softc *sc, int cmdbuf)
{
	int cmdresult, status;

	if (sc->mcast_count == 0)
		return (1);

	i82596_simple_command(sc, IE_CMD_MCAST, cmdbuf);

	(sc->memcopyout)(sc, (caddr_t)sc->mcast_addrs,
			 IE_CMD_MCAST_MADDR(cmdbuf),
			 sc->mcast_count * ETHER_ADDR_LEN);

	sc->ie_bus_write16(sc, IE_CMD_MCAST_BYTES(cmdbuf),
			       sc->mcast_count * ETHER_ADDR_LEN);

	/* Start the command */
	cmdresult = i82596_start_cmd(sc, IE_CUC_START, cmdbuf, IE_STAT_COMPL, 0);
	status = sc->ie_bus_read16(sc, IE_CMD_COMMON_STATUS(cmdbuf));
	if (cmdresult != 0) {
		printf("%s: multicast setup command timed out; status %b\n",
			sc->sc_dev.dv_xname, status, IE_STAT_BITS);
		return (0);
	}
	if ((status & IE_STAT_OK) == 0) {
		printf("%s: multicast setup command failed; status %b\n",
			sc->sc_dev.dv_xname, status, IE_STAT_BITS);
		return (0);
	}

	/* Squash any pending interrupts */
	ie_ack(sc, IE_ST_WHENCE);
	return (1);
}

/*
 * This routine takes the environment generated by check_ie_present() and adds
 * to it all the other structures we need to operate the adapter.  This
 * includes executing the CONFIGURE, IA-SETUP, and MC-SETUP commands, starting
 * the receiver unit, and clearing interrupts.
 *
 * THIS ROUTINE MUST BE CALLED AT splnet() OR HIGHER.
 */
int
i82596_init(struct ie_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	int cmd;

	sc->async_cmd_inprogress = 0;

	cmd = sc->buf_area;

	/*
	 * Send the configure command first.
	 */
	if (ie_cfg_setup(sc, cmd, sc->promisc, 0) == 0)
		return (0);

	/*
	 * Send the Individual Address Setup command.
	 */
	if (ie_ia_setup(sc, cmd) == 0)
		return (0);

	/*
	 * Run the time-domain reflectometer.
	 */
	ie_run_tdr(sc, cmd);

	/*
	 * Set the multi-cast filter, if any
	 */
	if (ie_mc_setup(sc, cmd) == 0)
		return (0);

	/*
	 * Acknowledge any interrupts we have generated thus far.
	 */
	ie_ack(sc, IE_ST_WHENCE);

	/*
	 * Set up the transmit and recv buffers.
	 */
	i82596_setup_bufs(sc);

	if (sc->hwinit)
		(sc->hwinit)(sc);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	if (NTXBUF < 2)
		sc->do_xmitnopchain = 0;

	i82596_start_transceiver(sc);
	return (1);
}

/*
 * Start the RU and possibly the CU unit
 */
void
i82596_start_transceiver(struct ie_softc *sc)
{

	/*
	 * Start RU at current position in frame & RBD lists.
	 */
	sc->ie_bus_write16(sc, IE_RFRAME_BUFDESC(sc->rframes,sc->rfhead),
			       IE_RBD_ADDR(sc->rbds, sc->rbhead));

	sc->ie_bus_write16(sc, IE_SCB_RCVLST(sc->scb),
			       IE_RFRAME_ADDR(sc->rframes,sc->rfhead));

	if (sc->do_xmitnopchain) {
		/* Stop transmit command chain */
		if (i82596_start_cmd(sc, IE_CUC_SUSPEND|IE_RUC_SUSPEND, 0, 0, 0))
			printf("%s: CU/RU stop command timed out\n",
				sc->sc_dev.dv_xname);

		/* Start the receiver & transmitter chain */
		/* sc->scb->ie_command_list =
			IEADDR(sc->nop_cmds[(sc->xctail+NTXBUF-1) % NTXBUF]);*/
		sc->ie_bus_write16(sc, IE_SCB_CMDLST(sc->scb),
				   IE_CMD_NOP_ADDR(
					sc->nop_cmds,
					(sc->xctail + NTXBUF - 1) % NTXBUF));

		if (i82596_start_cmd(sc, IE_CUC_START|IE_RUC_START, 0, 0, 0))
			printf("%s: CU/RU command timed out\n",
				sc->sc_dev.dv_xname);
	} else {
		if (i82596_start_cmd(sc, IE_RUC_START, 0, 0, 0))
			printf("%s: RU command timed out\n",
				sc->sc_dev.dv_xname);
	}
}

void
i82596_stop(struct ie_softc *sc)
{

	if (i82596_start_cmd(sc, IE_RUC_SUSPEND | IE_CUC_SUSPEND, 0, 0, 0))
		printf("%s: i82596_stop: disable commands timed out\n",
			sc->sc_dev.dv_xname);
}

int
i82596_ioctl(register struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct ie_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int s, error = 0;

	s = splnet();

	switch(cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		i82596_init(sc);
		break;

	case SIOCSIFFLAGS:
		sc->promisc = ifp->if_flags & (IFF_PROMISC | IFF_ALLMULTI);
		if ((ifp->if_flags & IFF_UP) == 0 &&
		    (ifp->if_flags & IFF_RUNNING) != 0) {
			/*
			 * If interface is marked down and it is running, then
			 * stop it.
			 */
			i82596_stop(sc);
			ifp->if_flags &= ~IFF_RUNNING;
		} else if ((ifp->if_flags & IFF_UP) != 0 &&
			   (ifp->if_flags & IFF_RUNNING) == 0) {
			/*
			 * If interface is marked up and it is stopped, then
			 * start it.
			 */
			i82596_init(sc);
		} else {
			/*
			 * Reset the interface to pick up changes in any other
			 * flags that affect hardware registers.
			 */
			i82596_stop(sc);
			i82596_init(sc);
		}
#ifdef I82596_DEBUG
		if (ifp->if_flags & IFF_DEBUG)
			sc->sc_debug = IED_ALL;
		else
			sc->sc_debug = 0;
#endif
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_media, cmd);
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_arpcom, cmd, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			ie_mc_reset(sc);
		error = 0;
	}

	splx(s);
	return (error);
}

void
ie_mc_reset(struct ie_softc *sc)
{
	struct arpcom *ac = &sc->sc_arpcom;
	struct ether_multi *enm;
	struct ether_multistep step;
	int size;

	if (ac->ac_multicnt >= IE_MAXMCAST || ac->ac_multirangecnt > 0) {
		ac->ac_if.if_flags |= IFF_ALLMULTI;
		i82596_ioctl(&ac->ac_if, SIOCSIFFLAGS, NULL);
		return;
	}

	/*
	 * Step through the list of addresses.
	 */
	size = 0;
	sc->mcast_count = 0;
	ETHER_FIRST_MULTI(step, &sc->sc_arpcom, enm);
	while (enm) {
		size += ETHER_ADDR_LEN;
		sc->mcast_count++;
		ETHER_NEXT_MULTI(step, enm);
	}

	if (size > sc->mcast_addrs_size) {
		/* Need to allocate more space */
		if (sc->mcast_addrs_size)
			free(sc->mcast_addrs, M_IFMADDR, 0);
		sc->mcast_addrs = (char *)
			malloc(size, M_IFMADDR, M_WAITOK);
		sc->mcast_addrs_size = size;
	}

	/*
	 * We've got the space; now copy the addresses
	 */
	sc->mcast_count = 0;
	ETHER_FIRST_MULTI(step, &sc->sc_arpcom, enm);
	while (enm) {
		bcopy(enm->enm_addrlo,
		   &sc->mcast_addrs[sc->mcast_count * ETHER_ADDR_LEN],
		   ETHER_ADDR_LEN);
		sc->mcast_count++;
		ETHER_NEXT_MULTI(step, enm);
	}
	sc->want_mcsetup = 1;
}

/*
 * Media change callback.
 */
int
i82596_mediachange(struct ifnet *ifp)
{
        struct ie_softc *sc = ifp->if_softc;

        if (sc->sc_mediachange)
                return ((*sc->sc_mediachange)(sc));
        return (EINVAL);
}

/*
 * Media status callback.
 */
void
i82596_mediastatus(struct ifnet *ifp, struct ifmediareq *ifmr)
{
        struct ie_softc *sc = ifp->if_softc;

        if (sc->sc_mediastatus)
                (*sc->sc_mediastatus)(sc, ifmr);
}

#ifdef I82596_DEBUG
void
print_rbd(struct ie_softc *sc, int n)
{

	printf("RBD at %08x:\n  status %b, next %04x, buffer %lx\n"
		"length/EOL %04x\n", IE_RBD_ADDR(sc->rbds,n),
		sc->ie_bus_read16(sc, IE_RBD_STATUS(sc->rbds,n)), IE_STAT_BITS,
		sc->ie_bus_read16(sc, IE_RBD_NEXT(sc->rbds,n)),
		(u_long)0,/*bus_space_read_4(sc->bt, sc->bh, IE_RBD_BUFADDR(sc->rbds,n)),-* XXX */
		sc->ie_bus_read16(sc, IE_RBD_BUFLEN(sc->rbds,n)));
}
#endif
