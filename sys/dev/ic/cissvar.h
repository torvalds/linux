/*	$OpenBSD: cissvar.h,v 1.16 2020/07/22 13:16:04 krw Exp $	*/

/*
 * Copyright (c) 2005,2006 Michael Shalayeff
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/sensors.h>

struct ciss_ld {
	struct ciss_blink bling;	/* a copy of blink state */
	char	xname[16];		/* copy of the sdN name */
	int	ndrives;
	u_int8_t tgts[1];
};

struct ciss_softc {
	struct device	sc_dev;
	struct timeout	sc_hb;
	void		*sc_ih;
	int		sc_flush;
	struct ksensor	*sensors;
	struct ksensordev sensordev;

	u_int	sc_flags;
#define	CISS_BIO	0x0001
	int ccblen, maxcmd, maxsg, nbus, ndrives, maxunits;
	struct ciss_ccb_list sc_free_ccb;
	struct mutex	sc_free_ccb_mtx;
	struct scsi_iopool sc_iopool;

	bus_space_tag_t	iot;
	bus_space_handle_t ioh, cfg_ioh;
	bus_dma_tag_t	dmat;
	bus_dmamap_t	cmdmap;
	bus_dma_segment_t cmdseg[1];
	void		*ccbs;
	void		*scratch;

	struct ciss_config cfg;
	int cfgoff;
	u_int32_t iem;
	u_int32_t heartbeat;
	int       fibrillation;
	struct ciss_ld **sc_lds;
};

/* XXX These have to become spinlocks in case of fine SMP */
#define	CISS_LOCK(sc) splbio()
#define	CISS_UNLOCK(sc, lock) splx(lock)
#define	CISS_LOCK_SCRATCH(sc) splbio()
#define	CISS_UNLOCK_SCRATCH(sc, lock) splx(lock)
typedef	int ciss_lock_t;

int	ciss_attach(struct ciss_softc *sc);
int	ciss_intr(void *v);
void	ciss_shutdown(void *v);
