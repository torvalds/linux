/*-
 * Copyright (c) 2009-2012,2016-2017 Microsoft Corp.
 * Copyright (c) 2010-2012 Citrix Inc.
 * Copyright (c) 2012 NetApp Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 * Network Virtualization Service.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet6.h"
#include "opt_inet.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/taskqueue.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/tcp_lro.h>

#include <dev/hyperv/include/hyperv.h>
#include <dev/hyperv/include/hyperv_busdma.h>
#include <dev/hyperv/include/vmbus.h>
#include <dev/hyperv/include/vmbus_xact.h>

#include <dev/hyperv/netvsc/ndis.h>
#include <dev/hyperv/netvsc/if_hnreg.h>
#include <dev/hyperv/netvsc/if_hnvar.h>
#include <dev/hyperv/netvsc/hn_nvs.h>

static int			hn_nvs_conn_chim(struct hn_softc *);
static int			hn_nvs_conn_rxbuf(struct hn_softc *);
static void			hn_nvs_disconn_chim(struct hn_softc *);
static void			hn_nvs_disconn_rxbuf(struct hn_softc *);
static int			hn_nvs_conf_ndis(struct hn_softc *, int);
static int			hn_nvs_init_ndis(struct hn_softc *);
static int			hn_nvs_doinit(struct hn_softc *, uint32_t);
static int			hn_nvs_init(struct hn_softc *);
static const void		*hn_nvs_xact_execute(struct hn_softc *,
				    struct vmbus_xact *, void *, int,
				    size_t *, uint32_t);
static void			hn_nvs_sent_none(struct hn_nvs_sendctx *,
				    struct hn_softc *, struct vmbus_channel *,
				    const void *, int);

struct hn_nvs_sendctx		hn_nvs_sendctx_none =
    HN_NVS_SENDCTX_INITIALIZER(hn_nvs_sent_none, NULL);

static const uint32_t		hn_nvs_version[] = {
	HN_NVS_VERSION_5,
	HN_NVS_VERSION_4,
	HN_NVS_VERSION_2,
	HN_NVS_VERSION_1
};

static const void *
hn_nvs_xact_execute(struct hn_softc *sc, struct vmbus_xact *xact,
    void *req, int reqlen, size_t *resplen0, uint32_t type)
{
	struct hn_nvs_sendctx sndc;
	size_t resplen, min_resplen = *resplen0;
	const struct hn_nvs_hdr *hdr;
	int error;

	KASSERT(min_resplen >= sizeof(*hdr),
	    ("invalid minimum response len %zu", min_resplen));

	/*
	 * Execute the xact setup by the caller.
	 */
	hn_nvs_sendctx_init(&sndc, hn_nvs_sent_xact, xact);

	vmbus_xact_activate(xact);
	error = hn_nvs_send(sc->hn_prichan, VMBUS_CHANPKT_FLAG_RC,
	    req, reqlen, &sndc);
	if (error) {
		vmbus_xact_deactivate(xact);
		return (NULL);
	}
	hdr = vmbus_chan_xact_wait(sc->hn_prichan, xact, &resplen,
	    HN_CAN_SLEEP(sc));

	/*
	 * Check this NVS response message.
	 */
	if (resplen < min_resplen) {
		if_printf(sc->hn_ifp, "invalid NVS resp len %zu\n", resplen);
		return (NULL);
	}
	if (hdr->nvs_type != type) {
		if_printf(sc->hn_ifp, "unexpected NVS resp 0x%08x, "
		    "expect 0x%08x\n", hdr->nvs_type, type);
		return (NULL);
	}
	/* All pass! */
	*resplen0 = resplen;
	return (hdr);
}

static __inline int
hn_nvs_req_send(struct hn_softc *sc, void *req, int reqlen)
{

	return (hn_nvs_send(sc->hn_prichan, VMBUS_CHANPKT_FLAG_NONE,
	    req, reqlen, &hn_nvs_sendctx_none));
}

static int 
hn_nvs_conn_rxbuf(struct hn_softc *sc)
{
	struct vmbus_xact *xact = NULL;
	struct hn_nvs_rxbuf_conn *conn;
	const struct hn_nvs_rxbuf_connresp *resp;
	size_t resp_len;
	uint32_t status;
	int error, rxbuf_size;

	/*
	 * Limit RXBUF size for old NVS.
	 */
	if (sc->hn_nvs_ver <= HN_NVS_VERSION_2)
		rxbuf_size = HN_RXBUF_SIZE_COMPAT;
	else
		rxbuf_size = HN_RXBUF_SIZE;

	/*
	 * Connect the RXBUF GPADL to the primary channel.
	 *
	 * NOTE:
	 * Only primary channel has RXBUF connected to it.  Sub-channels
	 * just share this RXBUF.
	 */
	error = vmbus_chan_gpadl_connect(sc->hn_prichan,
	    sc->hn_rxbuf_dma.hv_paddr, rxbuf_size, &sc->hn_rxbuf_gpadl);
	if (error) {
		if_printf(sc->hn_ifp, "rxbuf gpadl conn failed: %d\n",
		    error);
		goto cleanup;
	}

	/*
	 * Connect RXBUF to NVS.
	 */

	xact = vmbus_xact_get(sc->hn_xact, sizeof(*conn));
	if (xact == NULL) {
		if_printf(sc->hn_ifp, "no xact for nvs rxbuf conn\n");
		error = ENXIO;
		goto cleanup;
	}
	conn = vmbus_xact_req_data(xact);
	conn->nvs_type = HN_NVS_TYPE_RXBUF_CONN;
	conn->nvs_gpadl = sc->hn_rxbuf_gpadl;
	conn->nvs_sig = HN_NVS_RXBUF_SIG;

	resp_len = sizeof(*resp);
	resp = hn_nvs_xact_execute(sc, xact, conn, sizeof(*conn), &resp_len,
	    HN_NVS_TYPE_RXBUF_CONNRESP);
	if (resp == NULL) {
		if_printf(sc->hn_ifp, "exec nvs rxbuf conn failed\n");
		error = EIO;
		goto cleanup;
	}

	status = resp->nvs_status;
	vmbus_xact_put(xact);
	xact = NULL;

	if (status != HN_NVS_STATUS_OK) {
		if_printf(sc->hn_ifp, "nvs rxbuf conn failed: %x\n", status);
		error = EIO;
		goto cleanup;
	}
	sc->hn_flags |= HN_FLAG_RXBUF_CONNECTED;

	return (0);

cleanup:
	if (xact != NULL)
		vmbus_xact_put(xact);
	hn_nvs_disconn_rxbuf(sc);
	return (error);
}

static int 
hn_nvs_conn_chim(struct hn_softc *sc)
{
	struct vmbus_xact *xact = NULL;
	struct hn_nvs_chim_conn *chim;
	const struct hn_nvs_chim_connresp *resp;
	size_t resp_len;
	uint32_t status, sectsz;
	int error;

	/*
	 * Connect chimney sending buffer GPADL to the primary channel.
	 *
	 * NOTE:
	 * Only primary channel has chimney sending buffer connected to it.
	 * Sub-channels just share this chimney sending buffer.
	 */
	error = vmbus_chan_gpadl_connect(sc->hn_prichan,
  	    sc->hn_chim_dma.hv_paddr, HN_CHIM_SIZE, &sc->hn_chim_gpadl);
	if (error) {
		if_printf(sc->hn_ifp, "chim gpadl conn failed: %d\n", error);
		goto cleanup;
	}

	/*
	 * Connect chimney sending buffer to NVS
	 */

	xact = vmbus_xact_get(sc->hn_xact, sizeof(*chim));
	if (xact == NULL) {
		if_printf(sc->hn_ifp, "no xact for nvs chim conn\n");
		error = ENXIO;
		goto cleanup;
	}
	chim = vmbus_xact_req_data(xact);
	chim->nvs_type = HN_NVS_TYPE_CHIM_CONN;
	chim->nvs_gpadl = sc->hn_chim_gpadl;
	chim->nvs_sig = HN_NVS_CHIM_SIG;

	resp_len = sizeof(*resp);
	resp = hn_nvs_xact_execute(sc, xact, chim, sizeof(*chim), &resp_len,
	    HN_NVS_TYPE_CHIM_CONNRESP);
	if (resp == NULL) {
		if_printf(sc->hn_ifp, "exec nvs chim conn failed\n");
		error = EIO;
		goto cleanup;
	}

	status = resp->nvs_status;
	sectsz = resp->nvs_sectsz;
	vmbus_xact_put(xact);
	xact = NULL;

	if (status != HN_NVS_STATUS_OK) {
		if_printf(sc->hn_ifp, "nvs chim conn failed: %x\n", status);
		error = EIO;
		goto cleanup;
	}
	if (sectsz == 0 || sectsz % sizeof(uint32_t) != 0) {
		/*
		 * Can't use chimney sending buffer; done!
		 */
		if (sectsz == 0) {
			if_printf(sc->hn_ifp, "zero chimney sending buffer "
			    "section size\n");
		} else {
			if_printf(sc->hn_ifp, "misaligned chimney sending "
			    "buffers, section size: %u\n", sectsz);
		}
		sc->hn_chim_szmax = 0;
		sc->hn_chim_cnt = 0;
		sc->hn_flags |= HN_FLAG_CHIM_CONNECTED;
		return (0);
	}

	sc->hn_chim_szmax = sectsz;
	sc->hn_chim_cnt = HN_CHIM_SIZE / sc->hn_chim_szmax;
	if (HN_CHIM_SIZE % sc->hn_chim_szmax != 0) {
		if_printf(sc->hn_ifp, "chimney sending sections are "
		    "not properly aligned\n");
	}
	if (sc->hn_chim_cnt % LONG_BIT != 0) {
		if_printf(sc->hn_ifp, "discard %d chimney sending sections\n",
		    sc->hn_chim_cnt % LONG_BIT);
	}

	sc->hn_chim_bmap_cnt = sc->hn_chim_cnt / LONG_BIT;
	sc->hn_chim_bmap = malloc(sc->hn_chim_bmap_cnt * sizeof(u_long),
	    M_DEVBUF, M_WAITOK | M_ZERO);

	/* Done! */
	sc->hn_flags |= HN_FLAG_CHIM_CONNECTED;
	if (bootverbose) {
		if_printf(sc->hn_ifp, "chimney sending buffer %d/%d\n",
		    sc->hn_chim_szmax, sc->hn_chim_cnt);
	}
	return (0);

cleanup:
	if (xact != NULL)
		vmbus_xact_put(xact);
	hn_nvs_disconn_chim(sc);
	return (error);
}

static void
hn_nvs_disconn_rxbuf(struct hn_softc *sc)
{
	int error;

	if (sc->hn_flags & HN_FLAG_RXBUF_CONNECTED) {
		struct hn_nvs_rxbuf_disconn disconn;

		/*
		 * Disconnect RXBUF from NVS.
		 */
		memset(&disconn, 0, sizeof(disconn));
		disconn.nvs_type = HN_NVS_TYPE_RXBUF_DISCONN;
		disconn.nvs_sig = HN_NVS_RXBUF_SIG;

		/* NOTE: No response. */
		error = hn_nvs_req_send(sc, &disconn, sizeof(disconn));
		if (error) {
			if_printf(sc->hn_ifp,
			    "send nvs rxbuf disconn failed: %d\n", error);
			/*
			 * Fine for a revoked channel, since the hypervisor
			 * does not drain TX bufring for a revoked channel.
			 */
			if (!vmbus_chan_is_revoked(sc->hn_prichan))
				sc->hn_flags |= HN_FLAG_RXBUF_REF;
		}
		sc->hn_flags &= ~HN_FLAG_RXBUF_CONNECTED;

		/*
		 * Wait for the hypervisor to receive this NVS request.
		 *
		 * NOTE:
		 * The TX bufring will not be drained by the hypervisor,
		 * if the primary channel is revoked.
		 */
		while (!vmbus_chan_tx_empty(sc->hn_prichan) &&
		    !vmbus_chan_is_revoked(sc->hn_prichan))
			pause("waittx", 1);
		/*
		 * Linger long enough for NVS to disconnect RXBUF.
		 */
		pause("lingtx", (200 * hz) / 1000);
	}

	if (sc->hn_rxbuf_gpadl != 0) {
		/*
		 * Disconnect RXBUF from primary channel.
		 */
		error = vmbus_chan_gpadl_disconnect(sc->hn_prichan,
		    sc->hn_rxbuf_gpadl);
		if (error) {
			if_printf(sc->hn_ifp,
			    "rxbuf gpadl disconn failed: %d\n", error);
			sc->hn_flags |= HN_FLAG_RXBUF_REF;
		}
		sc->hn_rxbuf_gpadl = 0;
	}
}

static void
hn_nvs_disconn_chim(struct hn_softc *sc)
{
	int error;

	if (sc->hn_flags & HN_FLAG_CHIM_CONNECTED) {
		struct hn_nvs_chim_disconn disconn;

		/*
		 * Disconnect chimney sending buffer from NVS.
		 */
		memset(&disconn, 0, sizeof(disconn));
		disconn.nvs_type = HN_NVS_TYPE_CHIM_DISCONN;
		disconn.nvs_sig = HN_NVS_CHIM_SIG;

		/* NOTE: No response. */
		error = hn_nvs_req_send(sc, &disconn, sizeof(disconn));
		if (error) {
			if_printf(sc->hn_ifp,
			    "send nvs chim disconn failed: %d\n", error);
			/*
			 * Fine for a revoked channel, since the hypervisor
			 * does not drain TX bufring for a revoked channel.
			 */
			if (!vmbus_chan_is_revoked(sc->hn_prichan))
				sc->hn_flags |= HN_FLAG_CHIM_REF;
		}
		sc->hn_flags &= ~HN_FLAG_CHIM_CONNECTED;

		/*
		 * Wait for the hypervisor to receive this NVS request.
		 *
		 * NOTE:
		 * The TX bufring will not be drained by the hypervisor,
		 * if the primary channel is revoked.
		 */
		while (!vmbus_chan_tx_empty(sc->hn_prichan) &&
		    !vmbus_chan_is_revoked(sc->hn_prichan))
			pause("waittx", 1);
		/*
		 * Linger long enough for NVS to disconnect chimney
		 * sending buffer.
		 */
		pause("lingtx", (200 * hz) / 1000);
	}

	if (sc->hn_chim_gpadl != 0) {
		/*
		 * Disconnect chimney sending buffer from primary channel.
		 */
		error = vmbus_chan_gpadl_disconnect(sc->hn_prichan,
		    sc->hn_chim_gpadl);
		if (error) {
			if_printf(sc->hn_ifp,
			    "chim gpadl disconn failed: %d\n", error);
			sc->hn_flags |= HN_FLAG_CHIM_REF;
		}
		sc->hn_chim_gpadl = 0;
	}

	if (sc->hn_chim_bmap != NULL) {
		free(sc->hn_chim_bmap, M_DEVBUF);
		sc->hn_chim_bmap = NULL;
		sc->hn_chim_bmap_cnt = 0;
	}
}

static int
hn_nvs_doinit(struct hn_softc *sc, uint32_t nvs_ver)
{
	struct vmbus_xact *xact;
	struct hn_nvs_init *init;
	const struct hn_nvs_init_resp *resp;
	size_t resp_len;
	uint32_t status;

	xact = vmbus_xact_get(sc->hn_xact, sizeof(*init));
	if (xact == NULL) {
		if_printf(sc->hn_ifp, "no xact for nvs init\n");
		return (ENXIO);
	}
	init = vmbus_xact_req_data(xact);
	init->nvs_type = HN_NVS_TYPE_INIT;
	init->nvs_ver_min = nvs_ver;
	init->nvs_ver_max = nvs_ver;

	resp_len = sizeof(*resp);
	resp = hn_nvs_xact_execute(sc, xact, init, sizeof(*init), &resp_len,
	    HN_NVS_TYPE_INIT_RESP);
	if (resp == NULL) {
		if_printf(sc->hn_ifp, "exec init failed\n");
		vmbus_xact_put(xact);
		return (EIO);
	}

	status = resp->nvs_status;
	vmbus_xact_put(xact);

	if (status != HN_NVS_STATUS_OK) {
		if (bootverbose) {
			/*
			 * Caller may try another NVS version, and will log
			 * error if there are no more NVS versions to try,
			 * so don't bark out loud here.
			 */
			if_printf(sc->hn_ifp, "nvs init failed for ver 0x%x\n",
			    nvs_ver);
		}
		return (EINVAL);
	}
	return (0);
}

/*
 * Configure MTU and enable VLAN.
 */
static int
hn_nvs_conf_ndis(struct hn_softc *sc, int mtu)
{
	struct hn_nvs_ndis_conf conf;
	int error;

	memset(&conf, 0, sizeof(conf));
	conf.nvs_type = HN_NVS_TYPE_NDIS_CONF;
	conf.nvs_mtu = mtu + ETHER_HDR_LEN;
	conf.nvs_caps = HN_NVS_NDIS_CONF_VLAN;
	if (sc->hn_nvs_ver >= HN_NVS_VERSION_5)
		conf.nvs_caps |= HN_NVS_NDIS_CONF_SRIOV;

	/* NOTE: No response. */
	error = hn_nvs_req_send(sc, &conf, sizeof(conf));
	if (error) {
		if_printf(sc->hn_ifp, "send nvs ndis conf failed: %d\n", error);
		return (error);
	}

	if (bootverbose)
		if_printf(sc->hn_ifp, "nvs ndis conf done\n");
	sc->hn_caps |= HN_CAP_MTU | HN_CAP_VLAN;
	return (0);
}

static int
hn_nvs_init_ndis(struct hn_softc *sc)
{
	struct hn_nvs_ndis_init ndis;
	int error;

	memset(&ndis, 0, sizeof(ndis));
	ndis.nvs_type = HN_NVS_TYPE_NDIS_INIT;
	ndis.nvs_ndis_major = HN_NDIS_VERSION_MAJOR(sc->hn_ndis_ver);
	ndis.nvs_ndis_minor = HN_NDIS_VERSION_MINOR(sc->hn_ndis_ver);

	/* NOTE: No response. */
	error = hn_nvs_req_send(sc, &ndis, sizeof(ndis));
	if (error)
		if_printf(sc->hn_ifp, "send nvs ndis init failed: %d\n", error);
	return (error);
}

static int
hn_nvs_init(struct hn_softc *sc)
{
	int i, error;

	if (device_is_attached(sc->hn_dev)) {
		/*
		 * NVS version and NDIS version MUST NOT be changed.
		 */
		if (bootverbose) {
			if_printf(sc->hn_ifp, "reinit NVS version 0x%x, "
			    "NDIS version %u.%u\n", sc->hn_nvs_ver,
			    HN_NDIS_VERSION_MAJOR(sc->hn_ndis_ver),
			    HN_NDIS_VERSION_MINOR(sc->hn_ndis_ver));
		}

		error = hn_nvs_doinit(sc, sc->hn_nvs_ver);
		if (error) {
			if_printf(sc->hn_ifp, "reinit NVS version 0x%x "
			    "failed: %d\n", sc->hn_nvs_ver, error);
			return (error);
		}
		goto done;
	}

	/*
	 * Find the supported NVS version and set NDIS version accordingly.
	 */
	for (i = 0; i < nitems(hn_nvs_version); ++i) {
		error = hn_nvs_doinit(sc, hn_nvs_version[i]);
		if (!error) {
			sc->hn_nvs_ver = hn_nvs_version[i];

			/* Set NDIS version according to NVS version. */
			sc->hn_ndis_ver = HN_NDIS_VERSION_6_30;
			if (sc->hn_nvs_ver <= HN_NVS_VERSION_4)
				sc->hn_ndis_ver = HN_NDIS_VERSION_6_1;

			if (bootverbose) {
				if_printf(sc->hn_ifp, "NVS version 0x%x, "
				    "NDIS version %u.%u\n", sc->hn_nvs_ver,
				    HN_NDIS_VERSION_MAJOR(sc->hn_ndis_ver),
				    HN_NDIS_VERSION_MINOR(sc->hn_ndis_ver));
			}
			goto done;
		}
	}
	if_printf(sc->hn_ifp, "no NVS available\n");
	return (ENXIO);

done:
	if (sc->hn_nvs_ver >= HN_NVS_VERSION_5)
		sc->hn_caps |= HN_CAP_HASHVAL;
	return (0);
}

int
hn_nvs_attach(struct hn_softc *sc, int mtu)
{
	int error;

	if (hyperv_ver_major >= 10) {
		/* UDP 4-tuple hash is enforced. */
		sc->hn_caps |= HN_CAP_UDPHASH;
	}

	/*
	 * Initialize NVS.
	 */
	error = hn_nvs_init(sc);
	if (error)
		return (error);

	if (sc->hn_nvs_ver >= HN_NVS_VERSION_2) {
		/*
		 * Configure NDIS before initializing it.
		 */
		error = hn_nvs_conf_ndis(sc, mtu);
		if (error)
			return (error);
	}

	/*
	 * Initialize NDIS.
	 */
	error = hn_nvs_init_ndis(sc);
	if (error)
		return (error);

	/*
	 * Connect RXBUF.
	 */
	error = hn_nvs_conn_rxbuf(sc);
	if (error)
		return (error);

	/*
	 * Connect chimney sending buffer.
	 */
	error = hn_nvs_conn_chim(sc);
	if (error) {
		hn_nvs_disconn_rxbuf(sc);
		return (error);
	}
	return (0);
}

void
hn_nvs_detach(struct hn_softc *sc)
{

	/* NOTE: there are no requests to stop the NVS. */
	hn_nvs_disconn_rxbuf(sc);
	hn_nvs_disconn_chim(sc);
}

void
hn_nvs_sent_xact(struct hn_nvs_sendctx *sndc,
    struct hn_softc *sc __unused, struct vmbus_channel *chan __unused,
    const void *data, int dlen)
{

	vmbus_xact_wakeup(sndc->hn_cbarg, data, dlen);
}

static void
hn_nvs_sent_none(struct hn_nvs_sendctx *sndc __unused,
    struct hn_softc *sc __unused, struct vmbus_channel *chan __unused,
    const void *data __unused, int dlen __unused)
{
	/* EMPTY */
}

int
hn_nvs_alloc_subchans(struct hn_softc *sc, int *nsubch0)
{
	struct vmbus_xact *xact;
	struct hn_nvs_subch_req *req;
	const struct hn_nvs_subch_resp *resp;
	int error, nsubch_req;
	uint32_t nsubch;
	size_t resp_len;

	nsubch_req = *nsubch0;
	KASSERT(nsubch_req > 0, ("invalid # of sub-channels %d", nsubch_req));

	xact = vmbus_xact_get(sc->hn_xact, sizeof(*req));
	if (xact == NULL) {
		if_printf(sc->hn_ifp, "no xact for nvs subch alloc\n");
		return (ENXIO);
	}
	req = vmbus_xact_req_data(xact);
	req->nvs_type = HN_NVS_TYPE_SUBCH_REQ;
	req->nvs_op = HN_NVS_SUBCH_OP_ALLOC;
	req->nvs_nsubch = nsubch_req;

	resp_len = sizeof(*resp);
	resp = hn_nvs_xact_execute(sc, xact, req, sizeof(*req), &resp_len,
	    HN_NVS_TYPE_SUBCH_RESP);
	if (resp == NULL) {
		if_printf(sc->hn_ifp, "exec nvs subch alloc failed\n");
		error = EIO;
		goto done;
	}
	if (resp->nvs_status != HN_NVS_STATUS_OK) {
		if_printf(sc->hn_ifp, "nvs subch alloc failed: %x\n",
		    resp->nvs_status);
		error = EIO;
		goto done;
	}

	nsubch = resp->nvs_nsubch;
	if (nsubch > nsubch_req) {
		if_printf(sc->hn_ifp, "%u subchans are allocated, "
		    "requested %d\n", nsubch, nsubch_req);
		nsubch = nsubch_req;
	}
	*nsubch0 = nsubch;
	error = 0;
done:
	vmbus_xact_put(xact);
	return (error);
}

int
hn_nvs_send_rndis_ctrl(struct vmbus_channel *chan,
    struct hn_nvs_sendctx *sndc, struct vmbus_gpa *gpa, int gpa_cnt)
{

	return hn_nvs_send_rndis_sglist(chan, HN_NVS_RNDIS_MTYPE_CTRL,
	    sndc, gpa, gpa_cnt);
}

void
hn_nvs_set_datapath(struct hn_softc *sc, uint32_t path)
{
	struct hn_nvs_datapath dp;

	memset(&dp, 0, sizeof(dp));
	dp.nvs_type = HN_NVS_TYPE_SET_DATAPATH;
	dp.nvs_active_path = path;

	hn_nvs_req_send(sc, &dp, sizeof(dp));
}
