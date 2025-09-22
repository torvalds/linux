/*	$OpenBSD: uha.c,v 1.42 2022/04/16 19:19:59 naddy Exp $	*/
/*	$NetBSD: uha.c,v 1.3 1996/10/13 01:37:29 christos Exp $	*/
/*
 * Copyright (c) 1994, 1996 Charles M. Hannum.  All rights reserved.
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

/*
 * Ported for use with the UltraStor 14f by Gary Close (gclose@wvnvms.wvnet.edu)
 * Slight fixes to timeouts to run with the 34F
 * Thanks to Julian Elischer for advice and help with this port.
 *
 * Originally written by Julian Elischer (julian@tfs.com)
 * for TRW Financial Systems for use under the MACH(2.5) operating system.
 *
 * TRW Financial Systems, in accordance with their agreement with Carnegie
 * Mellon University, makes this software available to CMU to distribute
 * or use in any manner that they see fit as long as this message is kept with
 * the software. For this reason TFS also grants any other persons or
 * organisations permission to use or modify this software.
 *
 * TFS supplies this software to be publicly redistributed
 * on the understanding that TFS is not responsible for the correct
 * functioning of this software in any circumstances.
 *
 * commenced: Sun Sep 27 18:14:01 PDT 1992
 * slight mod to make work with 34F as well: Wed Jun  2 18:05:48 WST 1993
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/buf.h>
#include <uvm/uvm_extern.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#include <dev/ic/uhareg.h>
#include <dev/ic/uhavar.h>

#define KVTOPHYS(x)	vtophys((vaddr_t)x)

void uha_reset_mscp(struct uha_softc *, struct uha_mscp *);
void uha_mscp_free(void *, void *);
void *uha_mscp_alloc(void *);
void uha_scsi_cmd(struct scsi_xfer *);
int uhaprint(void *, const char *);

const struct scsi_adapter uha_switch = {
	uha_scsi_cmd, NULL, NULL, NULL, NULL
};

struct cfdriver uha_cd = {
	NULL, "uha", DV_DULL
};

#define	UHA_ABORT_TIMEOUT	2000	/* time to wait for abort (mSec) */

int
uhaprint(void *aux, const char *name)
{
	if (name != NULL)
		printf("%s: scsibus ", name);
	return UNCONF;
}

/*
 * Attach all the sub-devices we can find
 */
void
uha_attach(struct uha_softc *sc)
{
	struct scsibus_attach_args saa;

	(sc->init)(sc);
	SLIST_INIT(&sc->sc_free_mscp);

	mtx_init(&sc->sc_mscp_mtx, IPL_BIO);
	scsi_iopool_init(&sc->sc_iopool, sc, uha_mscp_alloc, uha_mscp_free);

	saa.saa_adapter_softc = sc;
	saa.saa_adapter_target = sc->sc_scsi_dev;
	saa.saa_adapter = &uha_switch;
	saa.saa_luns = saa.saa_adapter_buswidth = 8;
	saa.saa_openings = 2;
	saa.saa_pool = &sc->sc_iopool;
	saa.saa_quirks = saa.saa_flags = 0;
	saa.saa_wwpn = saa.saa_wwnn = 0;

	config_found(&sc->sc_dev, &saa, uhaprint);
}

void
uha_reset_mscp(struct uha_softc *sc, struct uha_mscp *mscp)
{

	mscp->flags = 0;
}

/*
 * A mscp (and hence a mbx-out) is put onto the free list.
 */
void
uha_mscp_free(void *xsc, void *xmscp)
{
	struct uha_softc *sc = xsc;
	struct uha_mscp *mscp = xmscp;

	uha_reset_mscp(sc, mscp);

	mtx_enter(&sc->sc_mscp_mtx);
	SLIST_INSERT_HEAD(&sc->sc_free_mscp, mscp, chain);
	mtx_leave(&sc->sc_mscp_mtx);
}

/*
 * Get a free mscp
 */
void *
uha_mscp_alloc(void *xsc)
{
	struct uha_softc *sc = xsc;
	struct uha_mscp *mscp;

	mtx_enter(&sc->sc_mscp_mtx);
	mscp = SLIST_FIRST(&sc->sc_free_mscp);
	if (mscp) {
		SLIST_REMOVE_HEAD(&sc->sc_free_mscp, chain);
		mscp->flags |= MSCP_ALLOC;
	}
	mtx_leave(&sc->sc_mscp_mtx);

	return (mscp);
}

/*
 * given a physical address, find the mscp that it corresponds to.
 */
struct uha_mscp *
uha_mscp_phys_kv(struct uha_softc *sc, u_long mscp_phys)
{
	int hashnum = MSCP_HASH(mscp_phys);
	struct uha_mscp *mscp = sc->sc_mscphash[hashnum];

	while (mscp) {
		if (mscp->hashkey == mscp_phys)
			break;
		mscp = mscp->nexthash;
	}
	return (mscp);
}

/*
 * We have a mscp which has been processed by the adaptor, now we look to see
 * how the operation went.
 */
void
uha_done(struct uha_softc *sc, struct uha_mscp *mscp)
{
	struct scsi_sense_data *s1, *s2;
	struct scsi_xfer *xs = mscp->xs;

#ifdef UHADEBUG
	printf("%s: uha_done\n", sc->sc_dev.dv_xname);
#endif

	/*
	 * Otherwise, put the results of the operation
	 * into the xfer and call whoever started it
	 */
	if ((mscp->flags & MSCP_ALLOC) == 0) {
		panic("%s: exiting ccb not allocated!", sc->sc_dev.dv_xname);
		return;
	}
	if (xs->error == XS_NOERROR) {
		if (mscp->host_stat != UHA_NO_ERR) {
			switch (mscp->host_stat) {
			case UHA_SBUS_TIMEOUT:		/* No response */
				xs->error = XS_SELTIMEOUT;
				break;
			default:	/* Other scsi protocol messes */
				printf("%s: host_stat %x\n",
				    sc->sc_dev.dv_xname, mscp->host_stat);
				xs->error = XS_DRIVER_STUFFUP;
			}
		} else if (mscp->target_stat != SCSI_OK) {
			switch (mscp->target_stat) {
			case SCSI_CHECK:
				s1 = &mscp->mscp_sense;
				s2 = &xs->sense;
				*s2 = *s1;
				xs->error = XS_SENSE;
				break;
			case SCSI_BUSY:
				xs->error = XS_BUSY;
				break;
			default:
				printf("%s: target_stat %x\n",
				    sc->sc_dev.dv_xname, mscp->target_stat);
				xs->error = XS_DRIVER_STUFFUP;
			}
		} else
			xs->resid = 0;
	}

	scsi_done(xs);
}

/*
 * start a scsi operation given the command and the data address.  Also
 * needs the unit, target and lu.
 */
void
uha_scsi_cmd(struct scsi_xfer *xs)
{
	struct scsi_link *sc_link = xs->sc_link;
	struct uha_softc *sc = sc_link->bus->sb_adapter_softc;
	struct uha_mscp *mscp;
	struct uha_dma_seg *sg;
	int seg;		/* scatter gather seg being worked on */
	u_long thiskv, thisphys, nextphys;
	int bytes_this_seg, bytes_this_page, datalen, flags;
	int s;

#ifdef UHADEBUG
	printf("%s: uha_scsi_cmd\n", sc->sc_dev.dv_xname);
#endif
	/*
	 * get a mscp (mbox-out) to use. If the transfer
	 * is from a buf (possibly from interrupt time)
	 * then we can't allow it to sleep
	 */
	flags = xs->flags;
	mscp = xs->io;

	mscp->xs = xs;
	mscp->timeout = xs->timeout;
	timeout_set(&xs->stimeout, uha_timeout, xs);

	/*
	 * Put all the arguments for the xfer in the mscp
	 */
	if (flags & SCSI_RESET) {
		mscp->opcode = UHA_SDR;
		mscp->ca = 0x01;
	} else {
		mscp->opcode = UHA_TSP;
		/* XXX Not for tapes. */
		mscp->ca = 0x01;
		bcopy(&xs->cmd, &mscp->scsi_cmd, mscp->scsi_cmd_length);
	}
	mscp->xdir = UHA_SDET;
	mscp->dcn = 0x00;
	mscp->chan = 0x00;
	mscp->target = sc_link->target;
	mscp->lun = sc_link->lun;
	mscp->scsi_cmd_length = xs->cmdlen;
	mscp->sense_ptr = KVTOPHYS(&mscp->mscp_sense);
	mscp->req_sense_length = sizeof(mscp->mscp_sense);
	mscp->host_stat = 0x00;
	mscp->target_stat = 0x00;

	if (xs->datalen) {
		sg = mscp->uha_dma;
		seg = 0;

		/*
		 * Set up the scatter gather block
		 */
#ifdef UHADEBUG
		printf("%s: %d @%p- ", sc->sc_dev.dv_xname, xs->datalen, xs->data);
#endif
		datalen = xs->datalen;
		thiskv = (int) xs->data;
		thisphys = KVTOPHYS(thiskv);

		while (datalen && seg < UHA_NSEG) {
			bytes_this_seg = 0;

			/* put in the base address */
			sg->seg_addr = thisphys;

#ifdef UHADEBUG
			printf("0x%lx", thisphys);
#endif

			/* do it at least once */
			nextphys = thisphys;
			while (datalen && thisphys == nextphys) {
				/*
				 * This page is contiguous (physically)
				 * with the last, just extend the
				 * length
				 */
				/* how far to the end of the page */
				nextphys = (thisphys & ~PGOFSET) + NBPG;
				bytes_this_page = nextphys - thisphys;
				/**** or the data ****/
				bytes_this_page = min(bytes_this_page,
						      datalen);
				bytes_this_seg += bytes_this_page;
				datalen -= bytes_this_page;

				/* get more ready for the next page */
				thiskv = (thiskv & ~PGOFSET) + NBPG;
				if (datalen)
					thisphys = KVTOPHYS(thiskv);
			}
			/*
			 * next page isn't contiguous, finish the seg
			 */
#ifdef UHADEBUG
			printf("(0x%x)", bytes_this_seg);
#endif
			sg->seg_len = bytes_this_seg;
			sg++;
			seg++;
		}

#ifdef UHADEBUG
		printf("\n");
#endif
		if (datalen) {
			/*
			 * there's still data, must have run out of segs!
			 */
			printf("%s: uha_scsi_cmd, more than %d dma segs\n",
			    sc->sc_dev.dv_xname, UHA_NSEG);
			goto bad;
		}
		mscp->data_addr = KVTOPHYS(mscp->uha_dma);
		mscp->data_length = xs->datalen;
		mscp->sgth = 0x01;
		mscp->sg_num = seg;
	} else {		/* No data xfer, use non S/G values */
		mscp->data_addr = (physaddr)0;
		mscp->data_length = 0;
		mscp->sgth = 0x00;
		mscp->sg_num = 0;
	}
	mscp->link_id = 0;
	mscp->link_addr = (physaddr)0;

	s = splbio();
	(sc->start_mbox)(sc, mscp);
	splx(s);

	/*
	 * Usually return SUCCESSFULLY QUEUED
	 */
	if ((flags & SCSI_POLL) == 0)
		return;

	/*
	 * If we can't use interrupts, poll on completion
	 */
	if ((sc->poll)(sc, xs, mscp->timeout)) {
		uha_timeout(mscp);
		if ((sc->poll)(sc, xs, mscp->timeout))
			uha_timeout(mscp);
	}
	return;

bad:
	xs->error = XS_DRIVER_STUFFUP;
	scsi_done(xs);
	return;
}

void
uha_timeout(void *arg)
{
	struct uha_mscp *mscp = arg;
	struct scsi_xfer *xs = mscp->xs;
	struct scsi_link *sc_link = xs->sc_link;
	struct uha_softc *sc = sc_link->bus->sb_adapter_softc;
	int s;

	sc_print_addr(sc_link);
	printf("timed out");

	s = splbio();

	if (mscp->flags & MSCP_ABORT) {
		/* abort timed out */
		printf(" AGAIN\n");
		/* XXX Must reset! */
	} else {
		/* abort the operation that has timed out */
		printf("\n");
		mscp->xs->error = XS_TIMEOUT;
		mscp->timeout = UHA_ABORT_TIMEOUT;
		mscp->flags |= MSCP_ABORT;
		(sc->start_mbox)(sc, mscp);
	}

	splx(s);
}
