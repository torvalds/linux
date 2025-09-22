/*	$OpenBSD: twevar.h,v 1.14 2020/07/22 13:16:04 krw Exp $	*/

/*
 * Copyright (c) 2000 Michael Shalayeff
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

struct twe_softc;

struct twe_ccb {
	struct twe_softc	*ccb_sc;
	struct twe_cmd		*ccb_cmd;
	struct scsi_xfer	*ccb_xs;
	paddr_t			ccb_cmdpa;
	TAILQ_ENTRY(twe_ccb)	ccb_link;
	enum {
		TWE_CCB_FREE, TWE_CCB_READY, TWE_CCB_QUEUED, TWE_CCB_PREQUEUED,
		TWE_CCB_DONE
	} ccb_state;
	int			ccb_length;
	void			*ccb_data;
	void			*ccb_realdata;
	bus_dmamap_t		ccb_dmamap;
	bus_dma_segment_t	ccb_2bseg[TWE_MAXOFFSETS];
	int			ccb_2nseg;
};

typedef TAILQ_HEAD(twe_queue_head, twe_ccb)	twe_queue_head;

struct twe_softc {
	struct device	sc_dev;
	void		*sc_ih;
	struct proc	*sc_thread;
	int		sc_thread_on;

	bus_space_tag_t	iot;
	bus_space_handle_t ioh;
	bus_dma_tag_t	dmat;

	void *sc_cmds;
	bus_dmamap_t	sc_cmdmap;
	bus_dma_segment_t sc_cmdseg[1];
	struct twe_ccb	sc_ccbs[TWE_MAXCMDS];
	twe_queue_head	sc_free_ccb;
	twe_queue_head	sc_ccbq;
	twe_queue_head	sc_ccb2q;
	twe_queue_head	sc_done_ccb;
	struct mutex	sc_ccb_mtx;
	struct scsi_iopool sc_iopool;

	struct timeout	sc_enqueue_tmo;

	struct scsi_iohandler sc_aen;

	struct {
		int	hd_present;
		int	hd_devtype;
		int	hd_lock;
		int	hd_size;
	} sc_hdr[TWE_MAX_UNITS];
};

/* XXX These have to become spinlocks in case of SMP */
#define TWE_LOCK(sc) splbio()
#define TWE_UNLOCK(sc, lock) splx(lock)
typedef int twe_lock_t;

int	twe_attach(struct twe_softc *);
int	twe_intr(void *);
