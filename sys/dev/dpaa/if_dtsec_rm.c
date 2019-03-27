/*-
 * Copyright (c) 2012 Semihalf.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/sockio.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_arp.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include "miibus_if.h"

#include <contrib/ncsw/inc/integrations/dpaa_integration_ext.h>
#include <contrib/ncsw/inc/Peripherals/fm_ext.h>
#include <contrib/ncsw/inc/Peripherals/fm_mac_ext.h>
#include <contrib/ncsw/inc/Peripherals/fm_port_ext.h>
#include <contrib/ncsw/inc/xx_ext.h>

#include "fman.h"
#include "bman.h"
#include "qman.h"
#include "if_dtsec.h"
#include "if_dtsec_rm.h"


/**
 * @group dTSEC RM private defines.
 * @{
 */
#define	DTSEC_BPOOLS_USED	(1)
#define	DTSEC_MAX_TX_QUEUE_LEN	256

struct dtsec_rm_frame_info {
	struct mbuf			*fi_mbuf;
	t_DpaaSGTE			fi_sgt[DPAA_NUM_OF_SG_TABLE_ENTRY];
};

enum dtsec_rm_pool_params {
	DTSEC_RM_POOL_RX_LOW_MARK	= 16,
	DTSEC_RM_POOL_RX_HIGH_MARK	= 64,
	DTSEC_RM_POOL_RX_MAX_SIZE	= 256,

	DTSEC_RM_POOL_FI_LOW_MARK	= 16,
	DTSEC_RM_POOL_FI_HIGH_MARK	= 64,
	DTSEC_RM_POOL_FI_MAX_SIZE	= 256,
};

enum dtsec_rm_fqr_params {
	DTSEC_RM_FQR_RX_CHANNEL		= e_QM_FQ_CHANNEL_POOL1,
	DTSEC_RM_FQR_RX_WQ		= 1,
	DTSEC_RM_FQR_TX_CONF_CHANNEL	= e_QM_FQ_CHANNEL_SWPORTAL0,
	DTSEC_RM_FQR_TX_WQ		= 1,
	DTSEC_RM_FQR_TX_CONF_WQ		= 1
};
/** @} */


/**
 * @group dTSEC Frame Info routines.
 * @{
 */
void
dtsec_rm_fi_pool_free(struct dtsec_softc *sc)
{

	if (sc->sc_fi_zone != NULL)
		uma_zdestroy(sc->sc_fi_zone);
}

int
dtsec_rm_fi_pool_init(struct dtsec_softc *sc)
{

	snprintf(sc->sc_fi_zname, sizeof(sc->sc_fi_zname), "%s: Frame Info",
	    device_get_nameunit(sc->sc_dev));

	sc->sc_fi_zone = uma_zcreate(sc->sc_fi_zname,
	    sizeof(struct dtsec_rm_frame_info), NULL, NULL, NULL, NULL,
	    UMA_ALIGN_PTR, 0);
	if (sc->sc_fi_zone == NULL)
		return (EIO);

	return (0);
}

static struct dtsec_rm_frame_info *
dtsec_rm_fi_alloc(struct dtsec_softc *sc)
{
	struct dtsec_rm_frame_info *fi;

	fi = uma_zalloc(sc->sc_fi_zone, M_NOWAIT);

	return (fi);
}

static void
dtsec_rm_fi_free(struct dtsec_softc *sc, struct dtsec_rm_frame_info *fi)
{

	uma_zfree(sc->sc_fi_zone, fi);
}
/** @} */


/**
 * @group dTSEC FMan PORT routines.
 * @{
 */
int
dtsec_rm_fm_port_rx_init(struct dtsec_softc *sc, int unit)
{
	t_FmPortParams params;
	t_FmPortRxParams *rx_params;
	t_FmExtPools *pool_params;
	t_Error error;

	memset(&params, 0, sizeof(params));

	params.baseAddr = sc->sc_fm_base + sc->sc_port_rx_hw_id;
	params.h_Fm = sc->sc_fmh;
	params.portType = dtsec_fm_port_rx_type(sc->sc_eth_dev_type);
	params.portId = sc->sc_eth_id;
	params.independentModeEnable = false;
	params.liodnBase = FM_PORT_LIODN_BASE;
	params.f_Exception = dtsec_fm_port_rx_exception_callback;
	params.h_App = sc;

	rx_params = &params.specificParams.rxParams;
	rx_params->errFqid = sc->sc_rx_fqid;
	rx_params->dfltFqid = sc->sc_rx_fqid;
	rx_params->liodnOffset = 0;

	pool_params = &rx_params->extBufPools;
	pool_params->numOfPoolsUsed = DTSEC_BPOOLS_USED;
	pool_params->extBufPool->id = sc->sc_rx_bpid;
	pool_params->extBufPool->size = FM_PORT_BUFFER_SIZE;

	sc->sc_rxph = FM_PORT_Config(&params);
	if (sc->sc_rxph == NULL) {
		device_printf(sc->sc_dev, "couldn't configure FM Port RX.\n");
		return (ENXIO);
	}

	error = FM_PORT_Init(sc->sc_rxph);
	if (error != E_OK) {
		device_printf(sc->sc_dev, "couldn't initialize FM Port RX.\n");
		FM_PORT_Free(sc->sc_rxph);
		return (ENXIO);
	}

	if (bootverbose)
		device_printf(sc->sc_dev, "RX hw port 0x%02x initialized.\n",
		    sc->sc_port_rx_hw_id);

	return (0);
}

int
dtsec_rm_fm_port_tx_init(struct dtsec_softc *sc, int unit)
{
	t_FmPortParams params;
	t_FmPortNonRxParams *tx_params;
	t_Error error;

	memset(&params, 0, sizeof(params));

	params.baseAddr = sc->sc_fm_base + sc->sc_port_tx_hw_id;
	params.h_Fm = sc->sc_fmh;
	params.portType = dtsec_fm_port_tx_type(sc->sc_eth_dev_type);
	params.portId = sc->sc_eth_id;
	params.independentModeEnable = false;
	params.liodnBase = FM_PORT_LIODN_BASE;
	params.f_Exception = dtsec_fm_port_tx_exception_callback;
	params.h_App = sc;

	tx_params = &params.specificParams.nonRxParams;
	tx_params->errFqid = sc->sc_tx_conf_fqid;
	tx_params->dfltFqid = sc->sc_tx_conf_fqid;
	tx_params->qmChannel = sc->sc_port_tx_qman_chan;
#ifdef FM_OP_PARTITION_ERRATA_FMANx8
	tx_params->opLiodnOffset = 0;
#endif

	sc->sc_txph = FM_PORT_Config(&params);
	if (sc->sc_txph == NULL) {
		device_printf(sc->sc_dev, "couldn't configure FM Port TX.\n");
		return (ENXIO);
	}

	error = FM_PORT_Init(sc->sc_txph);
	if (error != E_OK) {
		device_printf(sc->sc_dev, "couldn't initialize FM Port TX.\n");
		FM_PORT_Free(sc->sc_txph);
		return (ENXIO);
	}

	if (bootverbose)
		device_printf(sc->sc_dev, "TX hw port 0x%02x initialized.\n",
		    sc->sc_port_tx_hw_id);

	return (0);
}
/** @} */


/**
 * @group dTSEC buffer pools routines.
 * @{
 */
static t_Error
dtsec_rm_pool_rx_put_buffer(t_Handle h_BufferPool, uint8_t *buffer,
    t_Handle context)
{
	struct dtsec_softc *sc;

	sc = h_BufferPool;
	uma_zfree(sc->sc_rx_zone, buffer);

	return (E_OK);
}

static uint8_t *
dtsec_rm_pool_rx_get_buffer(t_Handle h_BufferPool, t_Handle *context)
{
	struct dtsec_softc *sc;
	uint8_t *buffer;

	sc = h_BufferPool;
	buffer = uma_zalloc(sc->sc_rx_zone, M_NOWAIT);

	return (buffer);
}

static void
dtsec_rm_pool_rx_depleted(t_Handle h_App, bool in)
{
	struct dtsec_softc *sc;
	unsigned int count;

	sc = h_App;

	if (!in)
		return;

	while (1) {
		count = bman_count(sc->sc_rx_pool);
		if (count > DTSEC_RM_POOL_RX_HIGH_MARK)
			return;

		bman_pool_fill(sc->sc_rx_pool, DTSEC_RM_POOL_RX_HIGH_MARK);
	}
}

void
dtsec_rm_pool_rx_free(struct dtsec_softc *sc)
{

	if (sc->sc_rx_pool != NULL)
		bman_pool_destroy(sc->sc_rx_pool);

	if (sc->sc_rx_zone != NULL)
		uma_zdestroy(sc->sc_rx_zone);
}

int
dtsec_rm_pool_rx_init(struct dtsec_softc *sc)
{

	/* FM_PORT_BUFFER_SIZE must be less than PAGE_SIZE */
	CTASSERT(FM_PORT_BUFFER_SIZE < PAGE_SIZE);

	snprintf(sc->sc_rx_zname, sizeof(sc->sc_rx_zname), "%s: RX Buffers",
	    device_get_nameunit(sc->sc_dev));

	sc->sc_rx_zone = uma_zcreate(sc->sc_rx_zname, FM_PORT_BUFFER_SIZE, NULL,
	    NULL, NULL, NULL, FM_PORT_BUFFER_SIZE - 1, 0);
	if (sc->sc_rx_zone == NULL)
		return (EIO);

	sc->sc_rx_pool = bman_pool_create(&sc->sc_rx_bpid, FM_PORT_BUFFER_SIZE,
	    0, 0, DTSEC_RM_POOL_RX_MAX_SIZE, dtsec_rm_pool_rx_get_buffer,
	    dtsec_rm_pool_rx_put_buffer, DTSEC_RM_POOL_RX_LOW_MARK,
	    DTSEC_RM_POOL_RX_HIGH_MARK, 0, 0, dtsec_rm_pool_rx_depleted, sc, NULL,
	    NULL);
	if (sc->sc_rx_pool == NULL) {
		device_printf(sc->sc_dev, "NULL rx pool  somehow\n");
		dtsec_rm_pool_rx_free(sc);
		return (EIO);
	}

	return (0);
}
/** @} */


/**
 * @group dTSEC Frame Queue Range routines.
 * @{
 */
static void
dtsec_rm_fqr_mext_free(struct mbuf *m)
{
	struct dtsec_softc *sc;
	void *buffer;

	buffer = m->m_ext.ext_arg1;
	sc = m->m_ext.ext_arg2;
	if (bman_count(sc->sc_rx_pool) <= DTSEC_RM_POOL_RX_MAX_SIZE)
		bman_put_buffer(sc->sc_rx_pool, buffer);
	else
		dtsec_rm_pool_rx_put_buffer(sc, buffer, NULL);
}

static e_RxStoreResponse
dtsec_rm_fqr_rx_callback(t_Handle app, t_Handle fqr, t_Handle portal,
    uint32_t fqid_off, t_DpaaFD *frame)
{
	struct dtsec_softc *sc;
	struct mbuf *m;
	void *frame_va;

	m = NULL;
	sc = app;

	frame_va = DPAA_FD_GET_ADDR(frame);
	KASSERT(DPAA_FD_GET_FORMAT(frame) == e_DPAA_FD_FORMAT_TYPE_SHORT_SBSF,
	    ("%s(): Got unsupported frame format 0x%02X!", __func__,
	    DPAA_FD_GET_FORMAT(frame)));

	KASSERT(DPAA_FD_GET_OFFSET(frame) == 0,
	    ("%s(): Only offset 0 is supported!", __func__));

	if (DPAA_FD_GET_STATUS(frame) != 0) {
		device_printf(sc->sc_dev, "RX error: 0x%08X\n",
		    DPAA_FD_GET_STATUS(frame));
		goto err;
	}

	m = m_gethdr(M_NOWAIT, MT_HEADER);
	if (m == NULL)
		goto err;

	m_extadd(m, frame_va, FM_PORT_BUFFER_SIZE,
	    dtsec_rm_fqr_mext_free, frame_va, sc, 0,
	    EXT_NET_DRV);

	m->m_pkthdr.rcvif = sc->sc_ifnet;
	m->m_len = DPAA_FD_GET_LENGTH(frame);
	m_fixhdr(m);

	(*sc->sc_ifnet->if_input)(sc->sc_ifnet, m);

	return (e_RX_STORE_RESPONSE_CONTINUE);

err:
	bman_put_buffer(sc->sc_rx_pool, frame_va);
	if (m != NULL)
		m_freem(m);

	return (e_RX_STORE_RESPONSE_CONTINUE);
}

static e_RxStoreResponse
dtsec_rm_fqr_tx_confirm_callback(t_Handle app, t_Handle fqr, t_Handle portal,
    uint32_t fqid_off, t_DpaaFD *frame)
{
	struct dtsec_rm_frame_info *fi;
	struct dtsec_softc *sc;
	unsigned int qlen;
	t_DpaaSGTE *sgt0;

	sc = app;

	if (DPAA_FD_GET_STATUS(frame) != 0)
		device_printf(sc->sc_dev, "TX error: 0x%08X\n",
		    DPAA_FD_GET_STATUS(frame));

	/*
	 * We are storing struct dtsec_rm_frame_info in first entry
	 * of scatter-gather table.
	 */
	sgt0 = DPAA_FD_GET_ADDR(frame);
	fi = DPAA_SGTE_GET_ADDR(sgt0);

	/* Free transmitted frame */
	m_freem(fi->fi_mbuf);
	dtsec_rm_fi_free(sc, fi);

	qlen = qman_fqr_get_counter(sc->sc_tx_conf_fqr, 0,
	    e_QM_FQR_COUNTERS_FRAME);

	if (qlen == 0) {
		DTSEC_LOCK(sc);

		if (sc->sc_tx_fqr_full) {
			sc->sc_tx_fqr_full = 0;
			dtsec_rm_if_start_locked(sc);
		}

		DTSEC_UNLOCK(sc);
	}

	return (e_RX_STORE_RESPONSE_CONTINUE);
}

void
dtsec_rm_fqr_rx_free(struct dtsec_softc *sc)
{

	if (sc->sc_rx_fqr)
		qman_fqr_free(sc->sc_rx_fqr);
}

int
dtsec_rm_fqr_rx_init(struct dtsec_softc *sc)
{
	t_Error error;
	t_Handle fqr;

	/* Default Frame Queue */
	fqr = qman_fqr_create(1, DTSEC_RM_FQR_RX_CHANNEL, DTSEC_RM_FQR_RX_WQ,
	    false, 0, false, false, true, false, 0, 0, 0);
	if (fqr == NULL) {
		device_printf(sc->sc_dev, "could not create default RX queue"
		    "\n");
		return (EIO);
	}

	sc->sc_rx_fqr = fqr;
	sc->sc_rx_fqid = qman_fqr_get_base_fqid(fqr);

	error = qman_fqr_register_cb(fqr, dtsec_rm_fqr_rx_callback, sc);
	if (error != E_OK) {
		device_printf(sc->sc_dev, "could not register RX callback\n");
		dtsec_rm_fqr_rx_free(sc);
		return (EIO);
	}

	return (0);
}

void
dtsec_rm_fqr_tx_free(struct dtsec_softc *sc)
{

	if (sc->sc_tx_fqr)
		qman_fqr_free(sc->sc_tx_fqr);

	if (sc->sc_tx_conf_fqr)
		qman_fqr_free(sc->sc_tx_conf_fqr);
}

int
dtsec_rm_fqr_tx_init(struct dtsec_softc *sc)
{
	t_Error error;
	t_Handle fqr;

	/* TX Frame Queue */
	fqr = qman_fqr_create(1, sc->sc_port_tx_qman_chan,
	    DTSEC_RM_FQR_TX_WQ, false, 0, false, false, true, false, 0, 0, 0);
	if (fqr == NULL) {
		device_printf(sc->sc_dev, "could not create default TX queue"
		    "\n");
		return (EIO);
	}

	sc->sc_tx_fqr = fqr;

	/* TX Confirmation Frame Queue */
	fqr = qman_fqr_create(1, DTSEC_RM_FQR_TX_CONF_CHANNEL,
	    DTSEC_RM_FQR_TX_CONF_WQ, false, 0, false, false, true, false, 0, 0,
	    0);
	if (fqr == NULL) {
		device_printf(sc->sc_dev, "could not create TX confirmation "
		    "queue\n");
		dtsec_rm_fqr_tx_free(sc);
		return (EIO);
	}

	sc->sc_tx_conf_fqr = fqr;
	sc->sc_tx_conf_fqid = qman_fqr_get_base_fqid(fqr);

	error = qman_fqr_register_cb(fqr, dtsec_rm_fqr_tx_confirm_callback, sc);
	if (error != E_OK) {
		device_printf(sc->sc_dev, "could not register TX confirmation "
		    "callback\n");
		dtsec_rm_fqr_tx_free(sc);
		return (EIO);
	}

	return (0);
}
/** @} */


/**
 * @group dTSEC IFnet routines.
 * @{
 */
void
dtsec_rm_if_start_locked(struct dtsec_softc *sc)
{
	vm_size_t dsize, psize, ssize;
	struct dtsec_rm_frame_info *fi;
	unsigned int qlen, i;
	struct mbuf *m0, *m;
	vm_offset_t vaddr;
	t_DpaaFD fd;

	DTSEC_LOCK_ASSERT(sc);
	/* TODO: IFF_DRV_OACTIVE */

	if ((sc->sc_mii->mii_media_status & IFM_ACTIVE) == 0)
		return;

	if ((sc->sc_ifnet->if_drv_flags & IFF_DRV_RUNNING) != IFF_DRV_RUNNING)
		return;

	while (!IFQ_DRV_IS_EMPTY(&sc->sc_ifnet->if_snd)) {
		/* Check length of the TX queue */
		qlen = qman_fqr_get_counter(sc->sc_tx_fqr, 0,
		    e_QM_FQR_COUNTERS_FRAME);

		if (qlen >= DTSEC_MAX_TX_QUEUE_LEN) {
			sc->sc_tx_fqr_full = 1;
			return;
		}

		fi = dtsec_rm_fi_alloc(sc);
		if (fi == NULL)
			return;

		IFQ_DRV_DEQUEUE(&sc->sc_ifnet->if_snd, m0);
		if (m0 == NULL) {
			dtsec_rm_fi_free(sc, fi);
			return;
		}

		i = 0;
		m = m0;
		psize = 0;
		dsize = 0;
		fi->fi_mbuf = m0;
		while (m && i < DPAA_NUM_OF_SG_TABLE_ENTRY) {
			if (m->m_len == 0)
				continue;

			/*
			 * First entry in scatter-gather table is used to keep
			 * pointer to frame info structure.
			 */
			DPAA_SGTE_SET_ADDR(&fi->fi_sgt[i], (void *)fi);
			DPAA_SGTE_SET_LENGTH(&fi->fi_sgt[i], 0);

			DPAA_SGTE_SET_EXTENSION(&fi->fi_sgt[i], 0);
			DPAA_SGTE_SET_FINAL(&fi->fi_sgt[i], 0);
			DPAA_SGTE_SET_BPID(&fi->fi_sgt[i], 0);
			DPAA_SGTE_SET_OFFSET(&fi->fi_sgt[i], 0);
			i++;

			dsize = m->m_len;
			vaddr = (vm_offset_t)m->m_data;
			while (dsize > 0 && i < DPAA_NUM_OF_SG_TABLE_ENTRY) {
				ssize = PAGE_SIZE - (vaddr & PAGE_MASK);
				if (m->m_len < ssize)
					ssize = m->m_len;

				DPAA_SGTE_SET_ADDR(&fi->fi_sgt[i],
				    (void *)vaddr);
				DPAA_SGTE_SET_LENGTH(&fi->fi_sgt[i], ssize);

				DPAA_SGTE_SET_EXTENSION(&fi->fi_sgt[i], 0);
				DPAA_SGTE_SET_FINAL(&fi->fi_sgt[i], 0);
				DPAA_SGTE_SET_BPID(&fi->fi_sgt[i], 0);
				DPAA_SGTE_SET_OFFSET(&fi->fi_sgt[i], 0);

				dsize -= ssize;
				vaddr += ssize;
				psize += ssize;
				i++;
			}

			if (dsize > 0)
				break;

			m = m->m_next;
		}

		/* Check if SG table was constructed properly */
		if (m != NULL || dsize != 0) {
			dtsec_rm_fi_free(sc, fi);
			m_freem(m0);
			continue;
		}

		DPAA_SGTE_SET_FINAL(&fi->fi_sgt[i-1], 1);

		DPAA_FD_SET_ADDR(&fd, fi->fi_sgt);
		DPAA_FD_SET_LENGTH(&fd, psize);
		DPAA_FD_SET_FORMAT(&fd, e_DPAA_FD_FORMAT_TYPE_SHORT_MBSF);

		fd.liodn = 0;
		fd.bpid = 0;
		fd.elion = 0;
		DPAA_FD_SET_OFFSET(&fd, 0);
		DPAA_FD_SET_STATUS(&fd, 0);

		DTSEC_UNLOCK(sc);
		if (qman_fqr_enqueue(sc->sc_tx_fqr, 0, &fd) != E_OK) {
			dtsec_rm_fi_free(sc, fi);
			m_freem(m0);
		}
		DTSEC_LOCK(sc);
	}
}
/** @} */
