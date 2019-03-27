/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Luiz Otavio O Souza.
 * Copyright (c) 2011-2012 Stefan Bethke.
 * Copyright (c) 2012 Adrian Chadd.
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

#ifndef	__IP17X_VAR_H__
#define	__IP17X_VAR_H__

typedef enum {
	IP17X_SWITCH_NONE,
	IP17X_SWITCH_IP175A,
	IP17X_SWITCH_IP175C,
	IP17X_SWITCH_IP175D,
	IP17X_SWITCH_IP178C,
} ip17x_switch_type;

struct ip17x_vlan {
	uint32_t	ports;
	int		vlanid;
};

struct ip17x_softc {
	device_t	sc_dev;
	int		media;		/* cpu port media */
	int		cpuport;	/* which PHY is connected to the CPU */
	int		phymask;	/* PHYs we manage */
	int		phyport[MII_NPHY];
	int		numports;	/* number of ports */
	int		*portphy;
	device_t	**miibus;
	int		miipoll;
	etherswitch_info_t	info;
	ip17x_switch_type	sc_switchtype;
	struct callout	callout_tick;
	struct ifnet	**ifp;
	struct mtx	sc_mtx;		/* serialize access to softc */

	struct ip17x_vlan	vlan[IP17X_MAX_VLANS];
	uint32_t	*pvid;		/* PVID */
	uint32_t	addtag;		/* per port add tag flag */
	uint32_t	striptag;	/* per port strip tag flag */
	uint32_t	vlan_mode;	/* VLAN mode */

	struct {
		int (* ip17x_reset) (struct ip17x_softc *);
		int (* ip17x_hw_setup) (struct ip17x_softc *);
		int (* ip17x_get_vlan_mode) (struct ip17x_softc *);
		int (* ip17x_set_vlan_mode) (struct ip17x_softc *, uint32_t);
	} hal;
};

#define IP17X_IS_SWITCH(_sc, _type)	\
	    (!!((_sc)->sc_switchtype == IP17X_SWITCH_ ## _type))

#define IP17X_LOCK(_sc)			\
	    mtx_lock(&(_sc)->sc_mtx)
#define IP17X_UNLOCK(_sc)		\
	    mtx_unlock(&(_sc)->sc_mtx)
#define IP17X_LOCK_ASSERT(_sc, _what)	\
	    mtx_assert(&(_sc)->sc_mtx, (_what))
#define IP17X_TRYLOCK(_sc)		\
	    mtx_trylock(&(_sc)->sc_mtx)

#if defined(DEBUG)
#define	DPRINTF(dev, args...) device_printf(dev, args)
#else
#define	DPRINTF(dev, args...)
#endif

#endif	/* __IP17X_VAR_H__ */
