/*	$OpenBSD: if_urtwn.c,v 1.16 2011/02/10 17:26:40 jakemsr Exp $	*/

/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2014 Kevin Lo <kevlo@FreeBSD.org>
 * Copyright (c) 2015-2016 Andriy Voskoboinyk <avos@FreeBSD.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/linker.h>
#include <sys/kdb.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/ethernet.h>
#include <net/if_media.h>

#include <net80211/ieee80211_var.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include "usbdevs.h"

#include <dev/rtwn/if_rtwnvar.h>
#include <dev/rtwn/if_rtwn_nop.h>

#include <dev/rtwn/usb/rtwn_usb_var.h>

#include <dev/rtwn/usb/rtwn_usb_attach.h>
#include <dev/rtwn/usb/rtwn_usb_ep.h>
#include <dev/rtwn/usb/rtwn_usb_reg.h>
#include <dev/rtwn/usb/rtwn_usb_tx.h>

#include <dev/rtwn/rtl8192c/r92c_reg.h>

static device_probe_t	rtwn_usb_match;
static device_attach_t	rtwn_usb_attach;
static device_detach_t	rtwn_usb_detach;
static device_suspend_t	rtwn_usb_suspend;
static device_resume_t	rtwn_usb_resume;

static int	rtwn_usb_alloc_list(struct rtwn_softc *,
		    struct rtwn_data[], int, int);
static int	rtwn_usb_alloc_rx_list(struct rtwn_softc *);
static int	rtwn_usb_alloc_tx_list(struct rtwn_softc *);
static void	rtwn_usb_free_list(struct rtwn_softc *,
		    struct rtwn_data data[], int);
static void	rtwn_usb_free_rx_list(struct rtwn_softc *);
static void	rtwn_usb_free_tx_list(struct rtwn_softc *);
static void	rtwn_usb_reset_lists(struct rtwn_softc *,
		    struct ieee80211vap *);
static void	rtwn_usb_reset_tx_list(struct rtwn_usb_softc *,
		    rtwn_datahead *, struct ieee80211vap *);
static void	rtwn_usb_reset_rx_list(struct rtwn_usb_softc *);
static void	rtwn_usb_start_xfers(struct rtwn_softc *);
static void	rtwn_usb_abort_xfers(struct rtwn_softc *);
static int	rtwn_usb_fw_write_block(struct rtwn_softc *,
		    const uint8_t *, uint16_t, int);
static void	rtwn_usb_drop_incorrect_tx(struct rtwn_softc *);
static void	rtwn_usb_attach_methods(struct rtwn_softc *);
static void	rtwn_usb_sysctlattach(struct rtwn_softc *);

#define RTWN_CONFIG_INDEX	0


static int
rtwn_usb_match(device_t self)
{
	struct usb_attach_arg *uaa = device_get_ivars(self);

	if (uaa->usb_mode != USB_MODE_HOST)
		return (ENXIO);
	if (uaa->info.bConfigIndex != RTWN_CONFIG_INDEX)
		return (ENXIO);
	if (uaa->info.bIfaceIndex != RTWN_IFACE_INDEX)
		return (ENXIO);

	return (usbd_lookup_id_by_uaa(rtwn_devs, sizeof(rtwn_devs), uaa));
}

static int
rtwn_usb_alloc_list(struct rtwn_softc *sc, struct rtwn_data data[],
    int ndata, int maxsz)
{
	int i, error;

	for (i = 0; i < ndata; i++) {
		struct rtwn_data *dp = &data[i];
		dp->m = NULL;
		dp->buf = malloc(maxsz, M_USBDEV, M_NOWAIT);
		if (dp->buf == NULL) {
			device_printf(sc->sc_dev,
			    "could not allocate buffer\n");
			error = ENOMEM;
			goto fail;
		}
		dp->ni = NULL;
	}

	return (0);
fail:
	rtwn_usb_free_list(sc, data, ndata);
	return (error);
}

static int
rtwn_usb_alloc_rx_list(struct rtwn_softc *sc)
{
	struct rtwn_usb_softc *uc = RTWN_USB_SOFTC(sc);
	int error, i;

	error = rtwn_usb_alloc_list(sc, uc->uc_rx, RTWN_USB_RX_LIST_COUNT,
	    uc->uc_rx_buf_size * RTWN_USB_RXBUFSZ_UNIT);
	if (error != 0)
		return (error);

	STAILQ_INIT(&uc->uc_rx_active);
	STAILQ_INIT(&uc->uc_rx_inactive);

	for (i = 0; i < RTWN_USB_RX_LIST_COUNT; i++)
		STAILQ_INSERT_HEAD(&uc->uc_rx_inactive, &uc->uc_rx[i], next);

	return (0);
}

static int
rtwn_usb_alloc_tx_list(struct rtwn_softc *sc)
{
	struct rtwn_usb_softc *uc = RTWN_USB_SOFTC(sc);
	int error, i;

	error = rtwn_usb_alloc_list(sc, uc->uc_tx, RTWN_USB_TX_LIST_COUNT,
	    RTWN_USB_TXBUFSZ);
	if (error != 0)
		return (error);

	STAILQ_INIT(&uc->uc_tx_active);
	STAILQ_INIT(&uc->uc_tx_inactive);
	STAILQ_INIT(&uc->uc_tx_pending);

	for (i = 0; i < RTWN_USB_TX_LIST_COUNT; i++)
		STAILQ_INSERT_HEAD(&uc->uc_tx_inactive, &uc->uc_tx[i], next);

	return (0);
}

static void
rtwn_usb_free_list(struct rtwn_softc *sc, struct rtwn_data data[], int ndata)
{
	int i;

	for (i = 0; i < ndata; i++) {
		struct rtwn_data *dp = &data[i];

		if (dp->buf != NULL) {
			free(dp->buf, M_USBDEV);
			dp->buf = NULL;
		}
		if (dp->ni != NULL) {
			ieee80211_free_node(dp->ni);
			dp->ni = NULL;
		}
		if (dp->m != NULL) {
			m_freem(dp->m);
			dp->m = NULL;
		}
	}
}

static void
rtwn_usb_free_rx_list(struct rtwn_softc *sc)
{
	struct rtwn_usb_softc *uc = RTWN_USB_SOFTC(sc);

	rtwn_usb_free_list(sc, uc->uc_rx, RTWN_USB_RX_LIST_COUNT);

	uc->uc_rx_stat_len = 0;
	uc->uc_rx_off = 0;

	STAILQ_INIT(&uc->uc_rx_active);
	STAILQ_INIT(&uc->uc_rx_inactive);
}

static void
rtwn_usb_free_tx_list(struct rtwn_softc *sc)
{
	struct rtwn_usb_softc *uc = RTWN_USB_SOFTC(sc);

	rtwn_usb_free_list(sc, uc->uc_tx, RTWN_USB_TX_LIST_COUNT);

	STAILQ_INIT(&uc->uc_tx_active);
	STAILQ_INIT(&uc->uc_tx_inactive);
	STAILQ_INIT(&uc->uc_tx_pending);
}

static void
rtwn_usb_reset_lists(struct rtwn_softc *sc, struct ieee80211vap *vap)
{
	struct rtwn_usb_softc *uc = RTWN_USB_SOFTC(sc);

	RTWN_ASSERT_LOCKED(sc);

	rtwn_usb_reset_tx_list(uc, &uc->uc_tx_active, vap);
	rtwn_usb_reset_tx_list(uc, &uc->uc_tx_pending, vap);
	if (vap == NULL) {
		rtwn_usb_reset_rx_list(uc);
		sc->qfullmsk = 0;
	}
}

static void
rtwn_usb_reset_tx_list(struct rtwn_usb_softc *uc,
    rtwn_datahead *head, struct ieee80211vap *vap)
{
	struct rtwn_vap *uvp = RTWN_VAP(vap);
	struct rtwn_data *dp, *tmp;
	int id;

	id = (uvp != NULL ? uvp->id : RTWN_VAP_ID_INVALID);

	STAILQ_FOREACH_SAFE(dp, head, next, tmp) {
		if (vap == NULL || (dp->ni == NULL &&
		    (dp->id == id || id == RTWN_VAP_ID_INVALID)) ||
		    (dp->ni != NULL && dp->ni->ni_vap == vap)) {
			if (dp->ni != NULL) {
				ieee80211_free_node(dp->ni);
				dp->ni = NULL;
			}

			if (dp->m != NULL) {
				m_freem(dp->m);
				dp->m = NULL;
			}

			STAILQ_REMOVE(head, dp, rtwn_data, next);
			STAILQ_INSERT_TAIL(&uc->uc_tx_inactive, dp, next);
		}
	}
}

static void
rtwn_usb_reset_rx_list(struct rtwn_usb_softc *uc)
{
	int i;

	for (i = 0; i < RTWN_USB_RX_LIST_COUNT; i++) {
		struct rtwn_data *dp = &uc->uc_rx[i];

		if (dp->m != NULL) {
			m_freem(dp->m);
			dp->m = NULL;
		}
	}
	uc->uc_rx_stat_len = 0;
	uc->uc_rx_off = 0;
}

static void
rtwn_usb_start_xfers(struct rtwn_softc *sc)
{
	struct rtwn_usb_softc *uc = RTWN_USB_SOFTC(sc);

	usbd_transfer_start(uc->uc_xfer[RTWN_BULK_RX]);
}

static void
rtwn_usb_abort_xfers(struct rtwn_softc *sc)
{
	struct rtwn_usb_softc *uc = RTWN_USB_SOFTC(sc);
	int i;

	RTWN_ASSERT_LOCKED(sc);

	/* abort any pending transfers */
	RTWN_UNLOCK(sc);
	for (i = 0; i < RTWN_N_TRANSFER; i++)
		usbd_transfer_drain(uc->uc_xfer[i]);
	RTWN_LOCK(sc);
}

static int
rtwn_usb_fw_write_block(struct rtwn_softc *sc, const uint8_t *buf,
    uint16_t reg, int mlen)
{
	int error;

	/* XXX fix this deconst */
	error = rtwn_usb_write_region_1(sc, reg, __DECONST(uint8_t *, buf),
	    mlen);

	return (error);
}

static void
rtwn_usb_drop_incorrect_tx(struct rtwn_softc *sc)
{

	rtwn_setbits_1_shift(sc, R92C_TXDMA_OFFSET_CHK, 0,
	    R92C_TXDMA_OFFSET_DROP_DATA_EN, 1);
}

static void
rtwn_usb_attach_methods(struct rtwn_softc *sc)
{
	sc->sc_write_1		= rtwn_usb_write_1;
	sc->sc_write_2		= rtwn_usb_write_2;
	sc->sc_write_4		= rtwn_usb_write_4;
	sc->sc_read_1		= rtwn_usb_read_1;
	sc->sc_read_2		= rtwn_usb_read_2;
	sc->sc_read_4		= rtwn_usb_read_4;
	sc->sc_delay		= rtwn_usb_delay;
	sc->sc_tx_start		= rtwn_usb_tx_start;
	sc->sc_start_xfers	= rtwn_usb_start_xfers;
	sc->sc_reset_lists	= rtwn_usb_reset_lists;
	sc->sc_abort_xfers	= rtwn_usb_abort_xfers;
	sc->sc_fw_write_block	= rtwn_usb_fw_write_block;
	sc->sc_get_qmap		= rtwn_usb_get_qmap;
	sc->sc_set_desc_addr	= rtwn_nop_softc;
	sc->sc_drop_incorrect_tx = rtwn_usb_drop_incorrect_tx;
	sc->sc_beacon_update_begin = rtwn_nop_softc_vap;
	sc->sc_beacon_update_end = rtwn_nop_softc_vap;
	sc->sc_beacon_unload	= rtwn_nop_softc_int;

	sc->bcn_check_interval	= 100;
}

static void
rtwn_usb_sysctlattach(struct rtwn_softc *sc)
{
	struct rtwn_usb_softc *uc = RTWN_USB_SOFTC(sc);
	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(sc->sc_dev);
	struct sysctl_oid *tree = device_get_sysctl_tree(sc->sc_dev);
	char str[64];
	int ret;

	ret = snprintf(str, sizeof(str),
	    "Rx buffer size, 512-byte units [%d...%d]",
	    RTWN_USB_RXBUFSZ_MIN, RTWN_USB_RXBUFSZ_MAX);
	KASSERT(ret > 0, ("ret (%d) <= 0!\n", ret));
	(void) ret;

	uc->uc_rx_buf_size = RTWN_USB_RXBUFSZ_DEF;
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "rx_buf_size", CTLFLAG_RDTUN, &uc->uc_rx_buf_size,
	    uc->uc_rx_buf_size, str);
	if (uc->uc_rx_buf_size < RTWN_USB_RXBUFSZ_MIN)
		uc->uc_rx_buf_size = RTWN_USB_RXBUFSZ_MIN;
	if (uc->uc_rx_buf_size > RTWN_USB_RXBUFSZ_MAX)
		uc->uc_rx_buf_size = RTWN_USB_RXBUFSZ_MAX;
}

static int
rtwn_usb_attach(device_t self)
{
	struct usb_attach_arg *uaa = device_get_ivars(self);
	struct rtwn_usb_softc *uc = device_get_softc(self);
	struct rtwn_softc *sc = &uc->uc_sc;
	struct ieee80211com *ic = &sc->sc_ic;
	int error;

	device_set_usb_desc(self);
	uc->uc_udev = uaa->device;
	sc->sc_dev = self;
	ic->ic_name = device_get_nameunit(self);

	/* Need to be initialized early. */
	rtwn_sysctlattach(sc);
	rtwn_usb_sysctlattach(sc);
	mtx_init(&sc->sc_mtx, ic->ic_name, MTX_NETWORK_LOCK, MTX_DEF);

	rtwn_usb_attach_methods(sc);
	rtwn_usb_attach_private(uc, USB_GET_DRIVER_INFO(uaa));

	error = rtwn_usb_setup_endpoints(uc);
	if (error != 0)
		goto detach;

	/* Allocate Tx/Rx buffers. */
	error = rtwn_usb_alloc_rx_list(sc);
	if (error != 0)
		goto detach;

	error = rtwn_usb_alloc_tx_list(sc);
	if (error != 0)
		goto detach;

	/* Generic attach. */
	error = rtwn_attach(sc);
	if (error != 0)
		goto detach;

	return (0);

detach:
	rtwn_usb_detach(self);		/* failure */
	return (ENXIO);
}

static int
rtwn_usb_detach(device_t self)
{
	struct rtwn_usb_softc *uc = device_get_softc(self);
	struct rtwn_softc *sc = &uc->uc_sc;

	/* Generic detach. */
	rtwn_detach(sc);

	/* Free Tx/Rx buffers. */
	rtwn_usb_free_tx_list(sc);
	rtwn_usb_free_rx_list(sc);

	/* Detach all USB transfers. */
	usbd_transfer_unsetup(uc->uc_xfer, RTWN_N_TRANSFER);

	rtwn_detach_private(sc);
	mtx_destroy(&sc->sc_mtx);

	return (0);
}

static int
rtwn_usb_suspend(device_t self)
{
	struct rtwn_usb_softc *uc = device_get_softc(self);

	rtwn_suspend(&uc->uc_sc);

	return (0);
}

static int
rtwn_usb_resume(device_t self)
{
	struct rtwn_usb_softc *uc = device_get_softc(self);

	rtwn_resume(&uc->uc_sc);

	return (0);
}

static device_method_t rtwn_usb_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		rtwn_usb_match),
	DEVMETHOD(device_attach,	rtwn_usb_attach),
	DEVMETHOD(device_detach,	rtwn_usb_detach),
	DEVMETHOD(device_suspend,	rtwn_usb_suspend),
	DEVMETHOD(device_resume,	rtwn_usb_resume),

	DEVMETHOD_END
};

static driver_t rtwn_usb_driver = {
	"rtwn",
	rtwn_usb_methods,
	sizeof(struct rtwn_usb_softc)
};

static devclass_t rtwn_usb_devclass;

DRIVER_MODULE(rtwn_usb, uhub, rtwn_usb_driver, rtwn_usb_devclass, NULL, NULL);
MODULE_VERSION(rtwn_usb, 1);
MODULE_DEPEND(rtwn_usb, usb, 1, 1, 1);
MODULE_DEPEND(rtwn_usb, wlan, 1, 1, 1);
MODULE_DEPEND(rtwn_usb, rtwn, 2, 2, 2);
USB_PNP_HOST_INFO(rtwn_devs);
