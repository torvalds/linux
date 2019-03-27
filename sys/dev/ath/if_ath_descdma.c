/*-
 * Copyright (c) 2002-2009 Sam Leffler, Errno Consulting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Driver for the Atheros Wireless LAN controller.
 *
 * This software is derived from work of Atsushi Onoe; his contribution
 * is greatly appreciated.
 */

#include "opt_inet.h"
#include "opt_ath.h"
/*
 * This is needed for register operations which are performed
 * by the driver - eg, calls to ath_hal_gettsf32().
 *
 * It's also required for any AH_DEBUG checks in here, eg the
 * module dependencies.
 */
#include "opt_ah.h"
#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/errno.h>
#include <sys/callout.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kthread.h>
#include <sys/taskqueue.h>
#include <sys/priv.h>
#include <sys/module.h>
#include <sys/ktr.h>
#include <sys/smp.h>	/* for mp_ncpus */

#include <machine/bus.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_llc.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_regdomain.h>
#ifdef IEEE80211_SUPPORT_SUPERG
#include <net80211/ieee80211_superg.h>
#endif
#ifdef IEEE80211_SUPPORT_TDMA
#include <net80211/ieee80211_tdma.h>
#endif

#include <net/bpf.h>

#ifdef INET
#include <netinet/in.h>
#include <netinet/if_ether.h>
#endif

#include <dev/ath/if_athvar.h>
#include <dev/ath/ath_hal/ah_devid.h>		/* XXX for softled */
#include <dev/ath/ath_hal/ah_diagcodes.h>

#include <dev/ath/if_ath_debug.h>
#include <dev/ath/if_ath_misc.h>
#if 0
#include <dev/ath/if_ath_tsf.h>
#include <dev/ath/if_ath_tx.h>
#include <dev/ath/if_ath_sysctl.h>
#include <dev/ath/if_ath_led.h>
#include <dev/ath/if_ath_keycache.h>
#include <dev/ath/if_ath_rx.h>
#include <dev/ath/if_ath_rx_edma.h>
#include <dev/ath/if_ath_tx_edma.h>
#include <dev/ath/if_ath_beacon.h>
#include <dev/ath/if_ath_btcoex.h>
#include <dev/ath/if_ath_spectral.h>
#include <dev/ath/if_ath_lna_div.h>
#include <dev/ath/if_athdfs.h>
#endif
#include <dev/ath/if_ath_descdma.h>

MALLOC_DECLARE(M_ATHDEV);

/*
 * This is the descriptor setup / busdma memory intialisation and
 * teardown routines.
 */

static void
ath_load_cb(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{
	bus_addr_t *paddr = (bus_addr_t*) arg;
	KASSERT(error == 0, ("error %u on bus_dma callback", error));
	*paddr = segs->ds_addr;
}

/*
 * Allocate the descriptors and appropriate DMA tag/setup.
 *
 * For some situations (eg EDMA TX completion), there isn't a requirement
 * for the ath_buf entries to be allocated.
 */
int
ath_descdma_alloc_desc(struct ath_softc *sc,
	struct ath_descdma *dd, ath_bufhead *head,
	const char *name, int ds_size, int ndesc)
{
#define	DS2PHYS(_dd, _ds) \
	((_dd)->dd_desc_paddr + ((caddr_t)(_ds) - (caddr_t)(_dd)->dd_desc))
#define	ATH_DESC_4KB_BOUND_CHECK(_daddr, _len) \
	((((u_int32_t)(_daddr) & 0xFFF) > (0x1000 - (_len))) ? 1 : 0)
	int error;

	dd->dd_descsize = ds_size;

	DPRINTF(sc, ATH_DEBUG_RESET,
	    "%s: %s DMA: %u desc, %d bytes per descriptor\n",
	    __func__, name, ndesc, dd->dd_descsize);

	dd->dd_name = name;
	dd->dd_desc_len = dd->dd_descsize * ndesc;

	/*
	 * Merlin work-around:
	 * Descriptors that cross the 4KB boundary can't be used.
	 * Assume one skipped descriptor per 4KB page.
	 */
	if (! ath_hal_split4ktrans(sc->sc_ah)) {
		int numpages = dd->dd_desc_len / 4096;
		dd->dd_desc_len += ds_size * numpages;
	}

	/*
	 * Setup DMA descriptor area.
	 *
	 * BUS_DMA_ALLOCNOW is not used; we never use bounce
	 * buffers for the descriptors themselves.
	 */
	error = bus_dma_tag_create(bus_get_dma_tag(sc->sc_dev),	/* parent */
		       PAGE_SIZE, 0,		/* alignment, bounds */
		       BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
		       BUS_SPACE_MAXADDR,	/* highaddr */
		       NULL, NULL,		/* filter, filterarg */
		       dd->dd_desc_len,		/* maxsize */
		       1,			/* nsegments */
		       dd->dd_desc_len,		/* maxsegsize */
		       0,			/* flags */
		       NULL,			/* lockfunc */
		       NULL,			/* lockarg */
		       &dd->dd_dmat);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "cannot allocate %s DMA tag\n", dd->dd_name);
		return error;
	}

	/* allocate descriptors */
	error = bus_dmamem_alloc(dd->dd_dmat, (void**) &dd->dd_desc,
				 BUS_DMA_NOWAIT | BUS_DMA_COHERENT,
				 &dd->dd_dmamap);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "unable to alloc memory for %u %s descriptors, error %u\n",
		    ndesc, dd->dd_name, error);
		goto fail1;
	}

	error = bus_dmamap_load(dd->dd_dmat, dd->dd_dmamap,
				dd->dd_desc, dd->dd_desc_len,
				ath_load_cb, &dd->dd_desc_paddr,
				BUS_DMA_NOWAIT);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "unable to map %s descriptors, error %u\n",
		    dd->dd_name, error);
		goto fail2;
	}

	DPRINTF(sc, ATH_DEBUG_RESET, "%s: %s DMA map: %p (%lu) -> %p (%lu)\n",
	    __func__, dd->dd_name, (uint8_t *) dd->dd_desc,
	    (u_long) dd->dd_desc_len, (caddr_t) dd->dd_desc_paddr,
	    /*XXX*/ (u_long) dd->dd_desc_len);

	return (0);

fail2:
	bus_dmamem_free(dd->dd_dmat, dd->dd_desc, dd->dd_dmamap);
fail1:
	bus_dma_tag_destroy(dd->dd_dmat);
	memset(dd, 0, sizeof(*dd));
	return error;
#undef DS2PHYS
#undef ATH_DESC_4KB_BOUND_CHECK
}

int
ath_descdma_setup(struct ath_softc *sc,
	struct ath_descdma *dd, ath_bufhead *head,
	const char *name, int ds_size, int nbuf, int ndesc)
{
#define	DS2PHYS(_dd, _ds) \
	((_dd)->dd_desc_paddr + ((caddr_t)(_ds) - (caddr_t)(_dd)->dd_desc))
#define	ATH_DESC_4KB_BOUND_CHECK(_daddr, _len) \
	((((u_int32_t)(_daddr) & 0xFFF) > (0x1000 - (_len))) ? 1 : 0)
	uint8_t *ds;
	struct ath_buf *bf;
	int i, bsize, error;

	/* Allocate descriptors */
	error = ath_descdma_alloc_desc(sc, dd, head, name, ds_size,
	    nbuf * ndesc);

	/* Assume any errors during allocation were dealt with */
	if (error != 0) {
		return (error);
	}

	ds = (uint8_t *) dd->dd_desc;

	/* allocate rx buffers */
	bsize = sizeof(struct ath_buf) * nbuf;
	bf = malloc(bsize, M_ATHDEV, M_NOWAIT | M_ZERO);
	if (bf == NULL) {
		device_printf(sc->sc_dev,
		    "malloc of %s buffers failed, size %u\n",
		    dd->dd_name, bsize);
		goto fail3;
	}
	dd->dd_bufptr = bf;

	TAILQ_INIT(head);
	for (i = 0; i < nbuf; i++, bf++, ds += (ndesc * dd->dd_descsize)) {
		bf->bf_desc = (struct ath_desc *) ds;
		bf->bf_daddr = DS2PHYS(dd, ds);
		if (! ath_hal_split4ktrans(sc->sc_ah)) {
			/*
			 * Merlin WAR: Skip descriptor addresses which
			 * cause 4KB boundary crossing along any point
			 * in the descriptor.
			 */
			 if (ATH_DESC_4KB_BOUND_CHECK(bf->bf_daddr,
			     dd->dd_descsize)) {
				/* Start at the next page */
				ds += 0x1000 - (bf->bf_daddr & 0xFFF);
				bf->bf_desc = (struct ath_desc *) ds;
				bf->bf_daddr = DS2PHYS(dd, ds);
			}
		}
		error = bus_dmamap_create(sc->sc_dmat, BUS_DMA_NOWAIT,
				&bf->bf_dmamap);
		if (error != 0) {
			device_printf(sc->sc_dev, "unable to create dmamap "
			    "for %s buffer %u, error %u\n",
			    dd->dd_name, i, error);
			ath_descdma_cleanup(sc, dd, head);
			return error;
		}
		bf->bf_lastds = bf->bf_desc;	/* Just an initial value */
		TAILQ_INSERT_TAIL(head, bf, bf_list);
	}

	/*
	 * XXX TODO: ensure that ds doesn't overflow the descriptor
	 * allocation otherwise weird stuff will occur and crash your
	 * machine.
	 */
	return 0;
	/* XXX this should likely just call ath_descdma_cleanup() */
fail3:
	bus_dmamap_unload(dd->dd_dmat, dd->dd_dmamap);
	bus_dmamem_free(dd->dd_dmat, dd->dd_desc, dd->dd_dmamap);
	bus_dma_tag_destroy(dd->dd_dmat);
	memset(dd, 0, sizeof(*dd));
	return error;
#undef DS2PHYS
#undef ATH_DESC_4KB_BOUND_CHECK
}

/*
 * Allocate ath_buf entries but no descriptor contents.
 *
 * This is for RX EDMA where the descriptors are the header part of
 * the RX buffer.
 */
int
ath_descdma_setup_rx_edma(struct ath_softc *sc,
	struct ath_descdma *dd, ath_bufhead *head,
	const char *name, int nbuf, int rx_status_len)
{
	struct ath_buf *bf;
	int i, bsize, error;

	DPRINTF(sc, ATH_DEBUG_RESET, "%s: %s DMA: %u buffers\n",
	    __func__, name, nbuf);

	dd->dd_name = name;
	/*
	 * This is (mostly) purely for show.  We're not allocating any actual
	 * descriptors here as EDMA RX has the descriptor be part
	 * of the RX buffer.
	 *
	 * However, dd_desc_len is used by ath_descdma_free() to determine
	 * whether we have already freed this DMA mapping.
	 */
	dd->dd_desc_len = rx_status_len * nbuf;
	dd->dd_descsize = rx_status_len;

	/* allocate rx buffers */
	bsize = sizeof(struct ath_buf) * nbuf;
	bf = malloc(bsize, M_ATHDEV, M_NOWAIT | M_ZERO);
	if (bf == NULL) {
		device_printf(sc->sc_dev,
		    "malloc of %s buffers failed, size %u\n",
		    dd->dd_name, bsize);
		error = ENOMEM;
		goto fail3;
	}
	dd->dd_bufptr = bf;

	TAILQ_INIT(head);
	for (i = 0; i < nbuf; i++, bf++) {
		bf->bf_desc = NULL;
		bf->bf_daddr = 0;
		bf->bf_lastds = NULL;	/* Just an initial value */

		error = bus_dmamap_create(sc->sc_dmat, BUS_DMA_NOWAIT,
				&bf->bf_dmamap);
		if (error != 0) {
			device_printf(sc->sc_dev, "unable to create dmamap "
			    "for %s buffer %u, error %u\n",
			    dd->dd_name, i, error);
			ath_descdma_cleanup(sc, dd, head);
			return error;
		}
		TAILQ_INSERT_TAIL(head, bf, bf_list);
	}
	return 0;
fail3:
	memset(dd, 0, sizeof(*dd));
	return error;
}

void
ath_descdma_cleanup(struct ath_softc *sc,
	struct ath_descdma *dd, ath_bufhead *head)
{
	struct ath_buf *bf;
	struct ieee80211_node *ni;
	int do_warning = 0;

	if (dd->dd_dmamap != 0) {
		bus_dmamap_unload(dd->dd_dmat, dd->dd_dmamap);
		bus_dmamem_free(dd->dd_dmat, dd->dd_desc, dd->dd_dmamap);
		bus_dma_tag_destroy(dd->dd_dmat);
	}

	if (head != NULL) {
		TAILQ_FOREACH(bf, head, bf_list) {
			if (bf->bf_m) {
				/*
				 * XXX warn if there's buffers here.
				 * XXX it should have been freed by the
				 * owner!
				 */
				
				if (do_warning == 0) {
					do_warning = 1;
					device_printf(sc->sc_dev,
					    "%s: %s: mbuf should've been"
					    " unmapped/freed!\n",
					    __func__,
					    dd->dd_name);
				}
				bus_dmamap_sync(sc->sc_dmat, bf->bf_dmamap,
				    BUS_DMASYNC_POSTREAD);
				bus_dmamap_unload(sc->sc_dmat, bf->bf_dmamap);
				m_freem(bf->bf_m);
				bf->bf_m = NULL;
			}
			if (bf->bf_dmamap != NULL) {
				bus_dmamap_destroy(sc->sc_dmat, bf->bf_dmamap);
				bf->bf_dmamap = NULL;
			}
			ni = bf->bf_node;
			bf->bf_node = NULL;
			if (ni != NULL) {
				/*
				 * Reclaim node reference.
				 */
				ieee80211_free_node(ni);
			}
		}
	}

	if (head != NULL)
		TAILQ_INIT(head);

	if (dd->dd_bufptr != NULL)
		free(dd->dd_bufptr, M_ATHDEV);
	memset(dd, 0, sizeof(*dd));
}
