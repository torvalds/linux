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
#include <contrib/ncsw/inc/Peripherals/fm_mac_ext.h>
#include <contrib/ncsw/inc/Peripherals/fm_port_ext.h>
#include <contrib/ncsw/inc/xx_ext.h>

#include "fman.h"
#include "if_dtsec.h"
#include "if_dtsec_im.h"


/**
 * @group dTSEC FMan PORT routines.
 * @{
 */
static e_RxStoreResponse
dtsec_im_fm_port_rx_callback(t_Handle app, uint8_t *data, uint16_t length,
    uint16_t status, uint8_t position, t_Handle buf_context)
{
	struct dtsec_softc *sc;
	struct mbuf *m;

	/* TODO STATUS / Position checking */
	sc = app;

	m = m_devget(data, length, 0, sc->sc_ifnet, NULL);
	if (m)
		(*sc->sc_ifnet->if_input)(sc->sc_ifnet, m);

	XX_FreeSmart(data);

	return (e_RX_STORE_RESPONSE_CONTINUE);
}

static void
dtsec_im_fm_port_tx_conf_callback(t_Handle app, uint8_t *data, uint16_t status,
    t_Handle buf_context)
{

	/* TODO: Check status */
	XX_FreeSmart(data);
}

static uint8_t *
dtsec_im_fm_port_rx_get_buf(t_Handle buffer_pool, t_Handle *buf_context_handle)
{
	struct dtsec_softc *sc;
	uint8_t *buffer;

	sc = buffer_pool;

	buffer = XX_MallocSmart(FM_PORT_BUFFER_SIZE, 0, sizeof(void *));
	if (!buffer)
		device_printf(sc->sc_dev, "couldn't allocate RX buffer.\n");

	return (buffer);
}

static t_Error
dtsec_im_fm_port_rx_put_buf(t_Handle buffer_pool, uint8_t *buffer,
    t_Handle buf_context)
{

	XX_FreeSmart(buffer);
	return (E_OK);
}

int
dtsec_im_fm_port_rx_init(struct dtsec_softc *sc, int unit)
{
	t_FmPortParams params;
	t_BufferPoolInfo *pool_params;
	t_FmPortImRxTxParams *im_params;
	t_Error error;

	memset(&params, 0, sizeof(params));

	params.baseAddr = sc->sc_fm_base + sc->sc_port_rx_hw_id;
	params.h_Fm = sc->sc_fmh;
	params.portType = dtsec_fm_port_rx_type(sc->sc_eth_dev_type);
	params.portId = sc->sc_eth_id;
	params.independentModeEnable = TRUE;
	params.liodnBase = FM_PORT_LIODN_BASE;
	params.f_Exception = dtsec_fm_port_rx_exception_callback;
	params.h_App = sc;

	im_params = &params.specificParams.imRxTxParams;
	im_params->h_FmMuram = sc->sc_muramh;
	im_params->liodnOffset = FM_PORT_LIODN_OFFSET;
	im_params->dataMemId = FM_PORT_MEM_ID;
	im_params->dataMemAttributes = FM_PORT_MEM_ATTR;
	im_params->f_RxStore = dtsec_im_fm_port_rx_callback;

	pool_params = &params.specificParams.imRxTxParams.rxPoolParams;
	pool_params->h_BufferPool = sc;
	pool_params->f_GetBuf = dtsec_im_fm_port_rx_get_buf;
	pool_params->f_PutBuf = dtsec_im_fm_port_rx_put_buf;
	pool_params->bufferSize = FM_PORT_BUFFER_SIZE;

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
dtsec_im_fm_port_tx_init(struct dtsec_softc *sc, int unit)
{
	t_FmPortParams params;
	t_FmPortImRxTxParams *im_params;
	t_Error error;

	memset(&params, 0, sizeof(params));

	params.baseAddr = sc->sc_fm_base + sc->sc_port_tx_hw_id;
	params.h_Fm = sc->sc_fmh;
	params.portType = dtsec_fm_port_tx_type(sc->sc_eth_dev_type);
	params.portId = unit;
	params.independentModeEnable = TRUE;
	params.liodnBase = FM_PORT_LIODN_BASE;
	params.f_Exception = dtsec_fm_port_tx_exception_callback;
	params.h_App = sc;

	im_params = &params.specificParams.imRxTxParams;
	im_params->h_FmMuram = sc->sc_muramh;
	im_params->liodnOffset = FM_PORT_LIODN_OFFSET;
	im_params->dataMemId = FM_PORT_MEM_ID;
	im_params->dataMemAttributes = FM_PORT_MEM_ATTR;
	im_params->f_TxConf = dtsec_im_fm_port_tx_conf_callback;

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
 * @group dTSEC IFnet routines.
 * @{
 */
void
dtsec_im_if_start_locked(struct dtsec_softc *sc)
{
	uint8_t *buffer;
	uint16_t length;
	struct mbuf *m;
	int error;

	DTSEC_LOCK_ASSERT(sc);
	/* TODO: IFF_DRV_OACTIVE */

	if ((sc->sc_mii->mii_media_status & IFM_ACTIVE) == 0)
		return;

	if ((sc->sc_ifnet->if_drv_flags & IFF_DRV_RUNNING) != IFF_DRV_RUNNING)
		return;

	while (!IFQ_DRV_IS_EMPTY(&sc->sc_ifnet->if_snd)) {
		IFQ_DRV_DEQUEUE(&sc->sc_ifnet->if_snd, m);
		if (m == NULL)
			break;

		length = m_length(m, NULL);
		buffer = XX_MallocSmart(length, 0, sizeof(void *));
		if (!buffer) {
			m_freem(m);
			break;
		}

		m_copydata(m, 0, length, buffer);
		m_freem(m);

		error = FM_PORT_ImTx(sc->sc_txph, buffer, length, TRUE, buffer);
		if (error != E_OK) {
			/* TODO: Ring full */
			XX_FreeSmart(buffer);
			break;
		}
	}
}
/** @} */
