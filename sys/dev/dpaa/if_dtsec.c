/*-
 * Copyright (c) 2011-2012 Semihalf.
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

#include <machine/bus.h>
#include <machine/resource.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_arp.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/openfirm.h>

#include "miibus_if.h"

#include <contrib/ncsw/inc/integrations/dpaa_integration_ext.h>
#include <contrib/ncsw/inc/Peripherals/fm_mac_ext.h>
#include <contrib/ncsw/inc/Peripherals/fm_port_ext.h>
#include <contrib/ncsw/inc/xx_ext.h>

#include "fman.h"
#include "if_dtsec.h"
#include "if_dtsec_im.h"
#include "if_dtsec_rm.h"

#define	DTSEC_MIN_FRAME_SIZE	64
#define	DTSEC_MAX_FRAME_SIZE	9600

#define	DTSEC_REG_MAXFRM	0x110

/**
 * @group dTSEC private defines.
 * @{
 */
/**
 * dTSEC FMan MAC exceptions info struct.
 */
struct dtsec_fm_mac_ex_str {
	const int num;
	const char *str;
};
/** @} */


/**
 * @group FMan MAC routines.
 * @{
 */
#define	DTSEC_MAC_EXCEPTIONS_END	(-1)

/**
 * FMan MAC exceptions.
 */
static const struct dtsec_fm_mac_ex_str dtsec_fm_mac_exceptions[] = {
	{ e_FM_MAC_EX_10G_MDIO_SCAN_EVENTMDIO, "MDIO scan event" },
	{ e_FM_MAC_EX_10G_MDIO_CMD_CMPL, "MDIO command completion" },
	{ e_FM_MAC_EX_10G_REM_FAULT, "Remote fault" },
	{ e_FM_MAC_EX_10G_LOC_FAULT, "Local fault" },
	{ e_FM_MAC_EX_10G_1TX_ECC_ER, "Transmit frame ECC error" },
	{ e_FM_MAC_EX_10G_TX_FIFO_UNFL, "Transmit FIFO underflow" },
	{ e_FM_MAC_EX_10G_TX_FIFO_OVFL, "Receive FIFO overflow" },
	{ e_FM_MAC_EX_10G_TX_ER, "Transmit frame error" },
	{ e_FM_MAC_EX_10G_RX_FIFO_OVFL, "Receive FIFO overflow" },
	{ e_FM_MAC_EX_10G_RX_ECC_ER, "Receive frame ECC error" },
	{ e_FM_MAC_EX_10G_RX_JAB_FRM, "Receive jabber frame" },
	{ e_FM_MAC_EX_10G_RX_OVRSZ_FRM, "Receive oversized frame" },
	{ e_FM_MAC_EX_10G_RX_RUNT_FRM, "Receive runt frame" },
	{ e_FM_MAC_EX_10G_RX_FRAG_FRM, "Receive fragment frame" },
	{ e_FM_MAC_EX_10G_RX_LEN_ER, "Receive payload length error" },
	{ e_FM_MAC_EX_10G_RX_CRC_ER, "Receive CRC error" },
	{ e_FM_MAC_EX_10G_RX_ALIGN_ER, "Receive alignment error" },
	{ e_FM_MAC_EX_1G_BAB_RX, "Babbling receive error" },
	{ e_FM_MAC_EX_1G_RX_CTL, "Receive control (pause frame) interrupt" },
	{ e_FM_MAC_EX_1G_GRATEFUL_TX_STP_COMPLET, "Graceful transmit stop "
	    "complete" },
	{ e_FM_MAC_EX_1G_BAB_TX, "Babbling transmit error" },
	{ e_FM_MAC_EX_1G_TX_CTL, "Transmit control (pause frame) interrupt" },
	{ e_FM_MAC_EX_1G_TX_ERR, "Transmit error" },
	{ e_FM_MAC_EX_1G_LATE_COL, "Late collision" },
	{ e_FM_MAC_EX_1G_COL_RET_LMT, "Collision retry limit" },
	{ e_FM_MAC_EX_1G_TX_FIFO_UNDRN, "Transmit FIFO underrun" },
	{ e_FM_MAC_EX_1G_MAG_PCKT, "Magic Packet detected when dTSEC is in "
	    "Magic Packet detection mode" },
	{ e_FM_MAC_EX_1G_MII_MNG_RD_COMPLET, "MII management read completion" },
	{ e_FM_MAC_EX_1G_MII_MNG_WR_COMPLET, "MII management write completion" },
	{ e_FM_MAC_EX_1G_GRATEFUL_RX_STP_COMPLET, "Graceful receive stop "
	    "complete" },
	{ e_FM_MAC_EX_1G_TX_DATA_ERR, "Internal data error on transmit" },
	{ e_FM_MAC_EX_1G_RX_DATA_ERR, "Internal data error on receive" },
	{ e_FM_MAC_EX_1G_1588_TS_RX_ERR, "Time-Stamp Receive Error" },
	{ e_FM_MAC_EX_1G_RX_MIB_CNT_OVFL, "MIB counter overflow" },
	{ DTSEC_MAC_EXCEPTIONS_END, "" }
};

static const char *
dtsec_fm_mac_ex_to_str(e_FmMacExceptions exception)
{
	int i;

	for (i = 0; dtsec_fm_mac_exceptions[i].num != exception &&
	    dtsec_fm_mac_exceptions[i].num != DTSEC_MAC_EXCEPTIONS_END; ++i)
		;

	if (dtsec_fm_mac_exceptions[i].num == DTSEC_MAC_EXCEPTIONS_END)
		return ("<Unknown Exception>");

	return (dtsec_fm_mac_exceptions[i].str);
}

static void
dtsec_fm_mac_mdio_event_callback(t_Handle h_App,
    e_FmMacExceptions exception)
{
	struct dtsec_softc *sc;

	sc = h_App;
	device_printf(sc->sc_dev, "MDIO event %i: %s.\n", exception,
	    dtsec_fm_mac_ex_to_str(exception));
}

static void
dtsec_fm_mac_exception_callback(t_Handle app, e_FmMacExceptions exception)
{
	struct dtsec_softc *sc;

	sc = app;
	device_printf(sc->sc_dev, "MAC exception %i: %s.\n", exception,
	    dtsec_fm_mac_ex_to_str(exception));
}

static void
dtsec_fm_mac_free(struct dtsec_softc *sc)
{
	if (sc->sc_mach == NULL)
		return;

	FM_MAC_Disable(sc->sc_mach, e_COMM_MODE_RX_AND_TX);
	FM_MAC_Free(sc->sc_mach);
	sc->sc_mach = NULL;
}

static int
dtsec_fm_mac_init(struct dtsec_softc *sc, uint8_t *mac)
{
	t_FmMacParams params;
	t_Error error;

	memset(&params, 0, sizeof(params));
	memcpy(&params.addr, mac, sizeof(params.addr));

	params.baseAddr = rman_get_bushandle(sc->sc_mem);
	params.enetMode = sc->sc_mac_enet_mode;
	params.macId = sc->sc_eth_id;
	params.mdioIrq = sc->sc_mac_mdio_irq;
	params.f_Event = dtsec_fm_mac_mdio_event_callback;
	params.f_Exception = dtsec_fm_mac_exception_callback;
	params.h_App = sc;
	params.h_Fm = sc->sc_fmh;

	sc->sc_mach = FM_MAC_Config(&params);
	if (sc->sc_mach == NULL) {
		device_printf(sc->sc_dev, "couldn't configure FM_MAC module.\n"
		    );
		return (ENXIO);
	}

	error = FM_MAC_ConfigResetOnInit(sc->sc_mach, TRUE);
	if (error != E_OK) {
		device_printf(sc->sc_dev, "couldn't enable reset on init "
		    "feature.\n");
		dtsec_fm_mac_free(sc);
		return (ENXIO);
	}

	/* Do not inform about pause frames */
	error = FM_MAC_ConfigException(sc->sc_mach, e_FM_MAC_EX_1G_RX_CTL,
	    FALSE);
	if (error != E_OK) {
		device_printf(sc->sc_dev, "couldn't disable pause frames "
			"exception.\n");
		dtsec_fm_mac_free(sc);
		return (ENXIO);
	}

	error = FM_MAC_Init(sc->sc_mach);
	if (error != E_OK) {
		device_printf(sc->sc_dev, "couldn't initialize FM_MAC module."
		    "\n");
		dtsec_fm_mac_free(sc);
		return (ENXIO);
	}

	return (0);
}
/** @} */


/**
 * @group FMan PORT routines.
 * @{
 */
static const char *
dtsec_fm_port_ex_to_str(e_FmPortExceptions exception)
{

	switch (exception) {
	case e_FM_PORT_EXCEPTION_IM_BUSY:
		return ("IM: RX busy");
	default:
		return ("<Unknown Exception>");
	}
}

void
dtsec_fm_port_rx_exception_callback(t_Handle app,
    e_FmPortExceptions exception)
{
	struct dtsec_softc *sc;

	sc = app;
	device_printf(sc->sc_dev, "RX exception: %i: %s.\n", exception,
	    dtsec_fm_port_ex_to_str(exception));
}

void
dtsec_fm_port_tx_exception_callback(t_Handle app,
    e_FmPortExceptions exception)
{
	struct dtsec_softc *sc;

	sc = app;
	device_printf(sc->sc_dev, "TX exception: %i: %s.\n", exception,
	    dtsec_fm_port_ex_to_str(exception));
}

e_FmPortType
dtsec_fm_port_rx_type(enum eth_dev_type type)
{
	switch (type) {
	case ETH_DTSEC:
		return (e_FM_PORT_TYPE_RX);
	case ETH_10GSEC:
		return (e_FM_PORT_TYPE_RX_10G);
	default:
		return (e_FM_PORT_TYPE_DUMMY);
	}
}

e_FmPortType
dtsec_fm_port_tx_type(enum eth_dev_type type)
{

	switch (type) {
	case ETH_DTSEC:
		return (e_FM_PORT_TYPE_TX);
	case ETH_10GSEC:
		return (e_FM_PORT_TYPE_TX_10G);
	default:
		return (e_FM_PORT_TYPE_DUMMY);
	}
}

static void
dtsec_fm_port_free_both(struct dtsec_softc *sc)
{
	if (sc->sc_rxph) {
		FM_PORT_Free(sc->sc_rxph);
		sc->sc_rxph = NULL;
	}

	if (sc->sc_txph) {
		FM_PORT_Free(sc->sc_txph);
		sc->sc_txph = NULL;
	}
}
/** @} */


/**
 * @group IFnet routines.
 * @{
 */
static int
dtsec_set_mtu(struct dtsec_softc *sc, unsigned int mtu)
{

	mtu += ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN + ETHER_CRC_LEN;

	DTSEC_LOCK_ASSERT(sc);

	if (mtu >= DTSEC_MIN_FRAME_SIZE && mtu <= DTSEC_MAX_FRAME_SIZE) {
		bus_write_4(sc->sc_mem, DTSEC_REG_MAXFRM, mtu);
		return (mtu);
	}

	return (0);
}

static int
dtsec_if_enable_locked(struct dtsec_softc *sc)
{
	int error;

	DTSEC_LOCK_ASSERT(sc);

	error = FM_MAC_Enable(sc->sc_mach, e_COMM_MODE_RX_AND_TX);
	if (error != E_OK)
		return (EIO);

	error = FM_PORT_Enable(sc->sc_rxph);
	if (error != E_OK)
		return (EIO);

	error = FM_PORT_Enable(sc->sc_txph);
	if (error != E_OK)
		return (EIO);

	sc->sc_ifnet->if_drv_flags |= IFF_DRV_RUNNING;

	/* Refresh link state */
	dtsec_miibus_statchg(sc->sc_dev);

	return (0);
}

static int
dtsec_if_disable_locked(struct dtsec_softc *sc)
{
	int error;

	DTSEC_LOCK_ASSERT(sc);

	error = FM_MAC_Disable(sc->sc_mach, e_COMM_MODE_RX_AND_TX);
	if (error != E_OK)
		return (EIO);

	error = FM_PORT_Disable(sc->sc_rxph);
	if (error != E_OK)
		return (EIO);

	error = FM_PORT_Disable(sc->sc_txph);
	if (error != E_OK)
		return (EIO);

	sc->sc_ifnet->if_drv_flags &= ~IFF_DRV_RUNNING;

	return (0);
}

static int
dtsec_if_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct dtsec_softc *sc;
	struct ifreq *ifr;
	int error;

	sc = ifp->if_softc;
	ifr = (struct ifreq *)data;
	error = 0;

	/* Basic functionality to achieve media status reports */
	switch (command) {
	case SIOCSIFMTU:
		DTSEC_LOCK(sc);
		if (dtsec_set_mtu(sc, ifr->ifr_mtu))
			ifp->if_mtu = ifr->ifr_mtu;
		else
			error = EINVAL;
		DTSEC_UNLOCK(sc);
		break;
	case SIOCSIFFLAGS:
		DTSEC_LOCK(sc);

		if (sc->sc_ifnet->if_flags & IFF_UP)
			error = dtsec_if_enable_locked(sc);
		else
			error = dtsec_if_disable_locked(sc);

		DTSEC_UNLOCK(sc);
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_mii->mii_media,
		    command);
		break;

	default:
		error = ether_ioctl(ifp, command, data);
	}

	return (error);
}

static void
dtsec_if_tick(void *arg)
{
	struct dtsec_softc *sc;

	sc = arg;

	/* TODO */
	DTSEC_LOCK(sc);

	mii_tick(sc->sc_mii);
	callout_reset(&sc->sc_tick_callout, hz, dtsec_if_tick, sc);

	DTSEC_UNLOCK(sc);
}

static void
dtsec_if_deinit_locked(struct dtsec_softc *sc)
{

	DTSEC_LOCK_ASSERT(sc);

	DTSEC_UNLOCK(sc);
	callout_drain(&sc->sc_tick_callout);
	DTSEC_LOCK(sc);
}

static void
dtsec_if_init_locked(struct dtsec_softc *sc)
{
	int error;

	DTSEC_LOCK_ASSERT(sc);

	/* Set MAC address */
	error = FM_MAC_ModifyMacAddr(sc->sc_mach,
	    (t_EnetAddr *)IF_LLADDR(sc->sc_ifnet));
	if (error != E_OK) {
		device_printf(sc->sc_dev, "couldn't set MAC address.\n");
		goto err;
	}

	/* Start MII polling */
	if (sc->sc_mii)
		callout_reset(&sc->sc_tick_callout, hz, dtsec_if_tick, sc);

	if (sc->sc_ifnet->if_flags & IFF_UP) {
		error = dtsec_if_enable_locked(sc);
		if (error != 0)
			goto err;
	} else {
		error = dtsec_if_disable_locked(sc);
		if (error != 0)
			goto err;
	}

	return;

err:
	dtsec_if_deinit_locked(sc);
	device_printf(sc->sc_dev, "initialization error.\n");
	return;
}

static void
dtsec_if_init(void *data)
{
	struct dtsec_softc *sc;

	sc = data;

	DTSEC_LOCK(sc);
	dtsec_if_init_locked(sc);
	DTSEC_UNLOCK(sc);
}

static void
dtsec_if_start(struct ifnet *ifp)
{
	struct dtsec_softc *sc;

	sc = ifp->if_softc;
	DTSEC_LOCK(sc);
	sc->sc_start_locked(sc);
	DTSEC_UNLOCK(sc);
}

static void
dtsec_if_watchdog(struct ifnet *ifp)
{
	/* TODO */
}
/** @} */


/**
 * @group IFmedia routines.
 * @{
 */
static int
dtsec_ifmedia_upd(struct ifnet *ifp)
{
	struct dtsec_softc *sc = ifp->if_softc;

	DTSEC_LOCK(sc);
	mii_mediachg(sc->sc_mii);
	DTSEC_UNLOCK(sc);

	return (0);
}

static void
dtsec_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct dtsec_softc *sc = ifp->if_softc;

	DTSEC_LOCK(sc);

	mii_pollstat(sc->sc_mii);

	ifmr->ifm_active = sc->sc_mii->mii_media_active;
	ifmr->ifm_status = sc->sc_mii->mii_media_status;

	DTSEC_UNLOCK(sc);
}
/** @} */


/**
 * @group dTSEC bus interface.
 * @{
 */
static void
dtsec_configure_mode(struct dtsec_softc *sc)
{
	char tunable[64];

	snprintf(tunable, sizeof(tunable), "%s.independent_mode",
	    device_get_nameunit(sc->sc_dev));

	sc->sc_mode = DTSEC_MODE_REGULAR;
	TUNABLE_INT_FETCH(tunable, &sc->sc_mode);

	if (sc->sc_mode == DTSEC_MODE_REGULAR) {
		sc->sc_port_rx_init = dtsec_rm_fm_port_rx_init;
		sc->sc_port_tx_init = dtsec_rm_fm_port_tx_init;
		sc->sc_start_locked = dtsec_rm_if_start_locked;
	} else {
		sc->sc_port_rx_init = dtsec_im_fm_port_rx_init;
		sc->sc_port_tx_init = dtsec_im_fm_port_tx_init;
		sc->sc_start_locked = dtsec_im_if_start_locked;
	}

	device_printf(sc->sc_dev, "Configured for %s mode.\n",
	    (sc->sc_mode == DTSEC_MODE_REGULAR) ? "regular" : "independent");
}

int
dtsec_attach(device_t dev)
{
	struct dtsec_softc *sc;
	device_t parent;
	int error;
	struct ifnet *ifp;

	sc = device_get_softc(dev);

	parent = device_get_parent(dev);
	sc->sc_dev = dev;
	sc->sc_mac_mdio_irq = NO_IRQ;

	/* Check if MallocSmart allocator is ready */
	if (XX_MallocSmartInit() != E_OK)
		return (ENXIO);

	/* Init locks */
	mtx_init(&sc->sc_lock, device_get_nameunit(dev),
	    "DTSEC Global Lock", MTX_DEF);

	mtx_init(&sc->sc_mii_lock, device_get_nameunit(dev),
	    "DTSEC MII Lock", MTX_DEF);

	/* Init callouts */
	callout_init(&sc->sc_tick_callout, CALLOUT_MPSAFE);

	/* Read configuraton */
	if ((error = fman_get_handle(parent, &sc->sc_fmh)) != 0)
		return (error);

	if ((error = fman_get_muram_handle(parent, &sc->sc_muramh)) != 0)
		return (error);

	if ((error = fman_get_bushandle(parent, &sc->sc_fm_base)) != 0)
		return (error);

	/* Configure working mode */
	dtsec_configure_mode(sc);

	/* If we are working in regular mode configure BMAN and QMAN */
	if (sc->sc_mode == DTSEC_MODE_REGULAR) {
		/* Create RX buffer pool */
		error = dtsec_rm_pool_rx_init(sc);
		if (error != 0)
			return (EIO);

		/* Create RX frame queue range */
		error = dtsec_rm_fqr_rx_init(sc);
		if (error != 0)
			return (EIO);

		/* Create frame info pool */
		error = dtsec_rm_fi_pool_init(sc);
		if (error != 0)
			return (EIO);

		/* Create TX frame queue range */
		error = dtsec_rm_fqr_tx_init(sc);
		if (error != 0)
			return (EIO);
	}

	/* Init FMan MAC module. */
	error = dtsec_fm_mac_init(sc, sc->sc_mac_addr);
	if (error != 0) {
		dtsec_detach(dev);
		return (ENXIO);
	}

	/* Init FMan TX port */
	error = sc->sc_port_tx_init(sc, device_get_unit(sc->sc_dev));
	if (error != 0) {
		dtsec_detach(dev);
		return (ENXIO);
	}

	/* Init FMan RX port */
	error = sc->sc_port_rx_init(sc, device_get_unit(sc->sc_dev));
	if (error != 0) {
		dtsec_detach(dev);
		return (ENXIO);
	}

	/* Create network interface for upper layers */
	ifp = sc->sc_ifnet = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(sc->sc_dev, "if_alloc() failed.\n");
		dtsec_detach(dev);
		return (ENOMEM);
	}

	ifp->if_softc = sc;
	ifp->if_mtu = ETHERMTU;	/* TODO: Configure */
	ifp->if_flags = IFF_SIMPLEX | IFF_BROADCAST;
	ifp->if_init = dtsec_if_init;
	ifp->if_start = dtsec_if_start;
	ifp->if_ioctl = dtsec_if_ioctl;
	ifp->if_snd.ifq_maxlen = IFQ_MAXLEN;

	if (sc->sc_phy_addr >= 0)
		if_initname(ifp, device_get_name(sc->sc_dev),
		    device_get_unit(sc->sc_dev));
	else
		if_initname(ifp, "dtsec_phy", device_get_unit(sc->sc_dev));

	/* TODO */
#if 0
	IFQ_SET_MAXLEN(&ifp->if_snd, TSEC_TX_NUM_DESC - 1);
	ifp->if_snd.ifq_drv_maxlen = TSEC_TX_NUM_DESC - 1;
	IFQ_SET_READY(&ifp->if_snd);
#endif
	ifp->if_capabilities = IFCAP_JUMBO_MTU; /* TODO: HWCSUM */
	ifp->if_capenable = ifp->if_capabilities;

	/* Attach PHY(s) */
	error = mii_attach(sc->sc_dev, &sc->sc_mii_dev, ifp, dtsec_ifmedia_upd,
	    dtsec_ifmedia_sts, BMSR_DEFCAPMASK, sc->sc_phy_addr,
	    MII_OFFSET_ANY, 0);
	if (error) {
		device_printf(sc->sc_dev, "attaching PHYs failed: %d\n", error);
		dtsec_detach(sc->sc_dev);
		return (error);
	}
	sc->sc_mii = device_get_softc(sc->sc_mii_dev);

	/* Attach to stack */
	ether_ifattach(ifp, sc->sc_mac_addr);

	return (0);
}

int
dtsec_detach(device_t dev)
{
	struct dtsec_softc *sc;
	if_t ifp;

	sc = device_get_softc(dev);
	ifp = sc->sc_ifnet;

	if (device_is_attached(dev)) {
		ether_ifdetach(ifp);
		/* Shutdown interface */
		DTSEC_LOCK(sc);
		dtsec_if_deinit_locked(sc);
		DTSEC_UNLOCK(sc);
	}

	if (sc->sc_ifnet) {
		if_free(sc->sc_ifnet);
		sc->sc_ifnet = NULL;
	}

	if (sc->sc_mode == DTSEC_MODE_REGULAR) {
		/* Free RX/TX FQRs */
		dtsec_rm_fqr_rx_free(sc);
		dtsec_rm_fqr_tx_free(sc);

		/* Free frame info pool */
		dtsec_rm_fi_pool_free(sc);

		/* Free RX buffer pool */
		dtsec_rm_pool_rx_free(sc);
	}

	dtsec_fm_mac_free(sc);
	dtsec_fm_port_free_both(sc);

	/* Destroy lock */
	mtx_destroy(&sc->sc_lock);

	return (0);
}

int
dtsec_suspend(device_t dev)
{

	return (0);
}

int
dtsec_resume(device_t dev)
{

	return (0);
}

int
dtsec_shutdown(device_t dev)
{

	return (0);
}
/** @} */


/**
 * @group MII bus interface.
 * @{
 */
int
dtsec_miibus_readreg(device_t dev, int phy, int reg)
{
	struct dtsec_softc *sc;

	sc = device_get_softc(dev);

	return (MIIBUS_READREG(sc->sc_mdio, phy, reg));
}

int
dtsec_miibus_writereg(device_t dev, int phy, int reg, int value)
{

	struct dtsec_softc *sc;

	sc = device_get_softc(dev);

	return (MIIBUS_WRITEREG(sc->sc_mdio, phy, reg, value));
}

void
dtsec_miibus_statchg(device_t dev)
{
	struct dtsec_softc *sc;
	e_EnetSpeed speed;
	bool duplex;
	int error;

	sc = device_get_softc(dev);

	DTSEC_LOCK_ASSERT(sc);

	duplex = ((sc->sc_mii->mii_media_active & IFM_GMASK) == IFM_FDX);

	switch (IFM_SUBTYPE(sc->sc_mii->mii_media_active)) {
	case IFM_1000_T:
	case IFM_1000_SX:
		speed = e_ENET_SPEED_1000;
		break;

        case IFM_100_TX:
		speed = e_ENET_SPEED_100;
		break;

        case IFM_10_T:
		speed = e_ENET_SPEED_10;
		break;

	default:
		speed = e_ENET_SPEED_10;
	}

	error = FM_MAC_AdjustLink(sc->sc_mach, speed, duplex);
	if (error != E_OK)
		device_printf(sc->sc_dev, "error while adjusting MAC speed.\n");
}
/** @} */
