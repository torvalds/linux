/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003-2012 Broadcom Corporation
 * All Rights Reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY BROADCOM ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL BROADCOM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef __XLPGE_H__
#define __XLPGE_H__

#define	NLM_XLPGE_TXQ_SIZE	1024
#define	MAC_CRC_LEN		4

enum xlpge_link_state {
	NLM_LINK_DOWN,
	NLM_LINK_UP
};

enum xlpge_floctrl_status {
	NLM_FLOWCTRL_DISABLED,
	NLM_FLOWCTRL_ENABLED
};

struct nlm_xlp_portdata {
	struct ifnet *xlpge_if;
	struct nlm_xlpge_softc *xlpge_sc;
};

struct nlm_xlpnae_softc {
	device_t	xlpnae_dev;
	int		node;		/* XLP Node id */
	uint64_t	base;		/* NAE IO base */
	uint64_t	poe_base;	/* POE IO base */
	uint64_t	poedv_base;	/* POE distribution vec IO base */

	int		freq;		/* frequency of nae block */
	int		flow_crc_poly;	/* Flow CRC16 polynomial */
	int		total_free_desc; /* total for node */
	int		max_ports;
	int		total_num_ports;
	int		per_port_num_flows;

	u_int		nucores;
	u_int		nblocks;
	u_int		num_complex;
	u_int		ncontexts;

	/*  Ingress side parameters */
	u_int		num_desc;	/* no of descriptors in each packet */
	u_int		parser_threshold;/* threshold of entries above which */
					/* the parser sequencer is scheduled */
	/* NetIOR configs */
	u_int		cmplx_type[8];		/* XXXJC: redundant? */
	struct nae_port_config *portcfg;
	u_int		blockmask;
	u_int		portmask[XLP_NAE_NBLOCKS];
	u_int		ilmask;
	u_int		xauimask;
	u_int		sgmiimask;
	u_int		hw_parser_en;
	u_int		prepad_en;
	u_int		prepad_size;
	u_int		driver_mode;
	u_int		ieee_1588_en;
};

struct nlm_xlpge_softc {
	struct ifnet	*xlpge_if;	/* should be first member */
					/* see - mii.c:miibus_attach() */
	device_t	xlpge_dev;
	device_t	mii_bus;
	struct nlm_xlpnae_softc *network_sc;
	uint64_t	base_addr;	/* NAE IO base */
	int		node;		/* node id (quickread) */
	int		block;		/* network block id (quickread) */
	int		port;		/* port id - among the 18 in XLP */
	int		type;		/* port type - see xlp_gmac_port_types */
	int		valid;		/* boolean: valid port or not */
	struct mii_data	xlpge_mii;
	int		nfree_desc;	/* No of free descriptors sent to port */
	int		phy_addr;	/* PHY id for the interface */

	int		speed;		/* Port speed */
	int		duplexity;	/* Port duplexity */
	int		link;		/* Port link status */
	int		 flowctrl;	/* Port flow control setting */

	unsigned char	dev_addr[ETHER_ADDR_LEN];
	struct mtx	sc_lock;
	int		if_flags;
	struct nae_port_config *portcfg;
	struct callout  xlpge_callout;
	int		mdio_bus;
	int		txq;
	int		rxfreeq;
	int		hw_parser_en;
	int		prepad_en;
	int		prepad_size;
};

#define	XLP_NTXFRAGS		16
#define	NULL_VFBID		127

struct xlpge_tx_desc {
        uint64_t        frag[XLP_NTXFRAGS];
};

#define	XLPGE_LOCK_INIT(_sc, _name)	\
	mtx_init(&(_sc)->sc_lock, _name, MTX_NETWORK_LOCK, MTX_DEF)
#define	XLPGE_LOCK_DESTROY(_sc)	mtx_destroy(&(_sc)->sc_lock)
#define	XLPGE_LOCK(_sc)		mtx_lock(&(_sc)->sc_lock)
#define	XLPGE_UNLOCK(_sc)	mtx_unlock(&(_sc)->sc_lock)
#define	XLPGE_LOCK_ASSERT(_sc)	mtx_assert(&(_sc)->sc_lock, MA_OWNED)

#endif
