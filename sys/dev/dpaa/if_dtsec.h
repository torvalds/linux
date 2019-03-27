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
 *
 * $FreeBSD$
 */

#ifndef IF_DTSEC_H_
#define IF_DTSEC_H_

/**
 * @group dTSEC common API.
 * @{
 */
#define DTSEC_MODE_REGULAR		0
#define DTSEC_MODE_INDEPENDENT		1

#define DTSEC_LOCK(sc)			mtx_lock(&(sc)->sc_lock)
#define DTSEC_UNLOCK(sc)		mtx_unlock(&(sc)->sc_lock)
#define DTSEC_LOCK_ASSERT(sc)		mtx_assert(&(sc)->sc_lock, MA_OWNED)
#define DTSEC_MII_LOCK(sc)		mtx_lock(&(sc)->sc_mii_lock)
#define DTSEC_MII_UNLOCK(sc)		mtx_unlock(&(sc)->sc_mii_lock)

enum eth_dev_type {
	ETH_DTSEC = 0x1,
	ETH_10GSEC = 0x2
};

struct dtsec_softc {
	/* XXX MII bus requires that struct ifnet is first!!! */
	struct ifnet			*sc_ifnet;

	device_t			sc_dev;
	struct resource			*sc_mem;
	struct mtx			sc_lock;
	int				sc_mode;

	/* Methods */
	int				(*sc_port_rx_init)
	    (struct dtsec_softc *sc, int unit);
	int				(*sc_port_tx_init)
	    (struct dtsec_softc *sc, int unit);
	void				(*sc_start_locked)
	    (struct dtsec_softc *sc);

	/* dTSEC data */
	enum eth_dev_type		sc_eth_dev_type;
	uint8_t				sc_eth_id; /* Ethernet ID within its frame manager */
	uintptr_t			sc_mac_mem_offset;
	e_EnetMode			sc_mac_enet_mode;
	int				sc_mac_mdio_irq;
	uint8_t				sc_mac_addr[6];
	int				sc_port_rx_hw_id;
	int				sc_port_tx_hw_id;
	uint32_t			sc_port_tx_qman_chan;
	int				sc_phy_addr;
	bool				sc_hidden;
	device_t			sc_mdio;

	/* Params from fman_bus driver */
	vm_offset_t			sc_fm_base;
	t_Handle			sc_fmh;
	t_Handle			sc_muramh;

	t_Handle			sc_mach;
	t_Handle			sc_rxph;
	t_Handle			sc_txph;

	/* MII data */
	struct mii_data			*sc_mii;
	device_t			sc_mii_dev;
	struct mtx			sc_mii_lock;

	struct callout			sc_tick_callout;

	/* RX Pool */
	t_Handle			sc_rx_pool;
	uint8_t				sc_rx_bpid;
	uma_zone_t			sc_rx_zone;
	char				sc_rx_zname[64];

	/* RX Frame Queue */
	t_Handle			sc_rx_fqr;
	uint32_t			sc_rx_fqid;

	/* TX Frame Queue */
	t_Handle			sc_tx_fqr;
	bool				sc_tx_fqr_full;
	t_Handle			sc_tx_conf_fqr;
	uint32_t			sc_tx_conf_fqid;

	/* Frame Info Zone */
	uma_zone_t			sc_fi_zone;
	char				sc_fi_zname[64];
};
/** @} */


/**
 * @group dTSEC FMan PORT API.
 * @{
 */
enum dtsec_fm_port_params {
	FM_PORT_LIODN_BASE	= 0,
	FM_PORT_LIODN_OFFSET 	= 0,
	FM_PORT_MEM_ID		= 0,
	FM_PORT_MEM_ATTR	= MEMORY_ATTR_CACHEABLE,
	FM_PORT_BUFFER_SIZE	= MCLBYTES,
};

e_FmPortType	dtsec_fm_port_rx_type(enum eth_dev_type type);
void		dtsec_fm_port_rx_exception_callback(t_Handle app,
		    e_FmPortExceptions exception);
void		dtsec_fm_port_tx_exception_callback(t_Handle app,
		    e_FmPortExceptions exception);
e_FmPortType	dtsec_fm_port_tx_type(enum eth_dev_type type);
/** @} */


/**
 * @group dTSEC bus interface.
 * @{
 */
int		dtsec_attach(device_t dev);
int		dtsec_detach(device_t dev);
int		dtsec_suspend(device_t dev);
int		dtsec_resume(device_t dev);
int		dtsec_shutdown(device_t dev);
int		dtsec_miibus_readreg(device_t dev, int phy, int reg);
int		dtsec_miibus_writereg(device_t dev, int phy, int reg,
		    int value);
void		dtsec_miibus_statchg(device_t dev);
/** @} */

#endif /* IF_DTSEC_H_ */
