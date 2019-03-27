/*-
 * Copyright (c) 2017 Ian Lepore <ian@freebsd.org>
 * All rights reserved.
 *
 * Development sponsored by Microsemi, Inc.
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

/*
 * Utility functions for PHY drivers on systems configured using FDT data.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/socket.h>
#include <sys/bus.h>
#include <sys/malloc.h>

#include <net/if.h>
#include <net/if_media.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include <dev/mii/mii_fdt.h>

/*
 * Table to translate MII_CONTYPE_xxxx constants to/from devicetree strings.
 * We explicitly associate the enum values with the strings in a table to avoid
 * relying on this list being sorted in the same order as the enum in miivar.h,
 * and to avoid problems if the enum gains new types that aren't in the FDT
 * data.  However, the "unknown" entry must be first because it is referenced
 * using subscript 0 in mii_fdt_contype_to_name().
 */
static struct contype_names {
	mii_contype_t type;
	const char   *name;
} fdt_contype_names[] = {
	{MII_CONTYPE_UNKNOWN,		"unknown"},
	{MII_CONTYPE_MII,		"mii"},
	{MII_CONTYPE_GMII,		"gmii"},
	{MII_CONTYPE_SGMII,		"sgmii"},
	{MII_CONTYPE_QSGMII,		"qsgmii"},
	{MII_CONTYPE_TBI,		"tbi"},
	{MII_CONTYPE_REVMII,		"rev-mii"},
	{MII_CONTYPE_RMII,		"rmii"},
	{MII_CONTYPE_RGMII,		"rgmii"},
	{MII_CONTYPE_RGMII_ID,		"rgmii-id"},
	{MII_CONTYPE_RGMII_RXID,	"rgmii-rxid"},
	{MII_CONTYPE_RGMII_TXID,	"rgmii-txid"},
	{MII_CONTYPE_RTBI,		"rtbi"},
	{MII_CONTYPE_SMII,		"smii"},
	{MII_CONTYPE_XGMII,		"xgmii"},
	{MII_CONTYPE_TRGMII,		"trgmii"},
	{MII_CONTYPE_2000BX,		"2000base-x"},
	{MII_CONTYPE_2500BX,		"2500base-x"},
	{MII_CONTYPE_RXAUI,		"rxaui"},
};                                                           

static phandle_t
mii_fdt_get_phynode(phandle_t macnode)
{
	static const char *props[] = {
	    "phy-handle", "phy", "phy-device"
	};
	pcell_t xref;
	u_int i;

	for (i = 0; i < nitems(props); ++i) {
		if (OF_getencprop(macnode, props[i], &xref, sizeof(xref)) > 0)
			return (OF_node_from_xref(xref));
	}
	return (-1);
}

mii_contype_t
mii_fdt_contype_from_name(const char *name)
{
	u_int i;

	for (i = 0; i < nitems(fdt_contype_names); ++i) {
		if (strcmp(name, fdt_contype_names[i].name) == 0)
			return (fdt_contype_names[i].type);
	}
	return (MII_CONTYPE_UNKNOWN);
}

const char *
mii_fdt_contype_to_name(mii_contype_t contype)
{
	u_int i;

	for (i = 0; i < nitems(fdt_contype_names); ++i) {
		if (contype == fdt_contype_names[i].type)
			return (fdt_contype_names[i].name);
	}
	return (fdt_contype_names[0].name);
}

mii_contype_t
mii_fdt_get_contype(phandle_t macnode)
{
	char val[32];

	if (OF_getprop(macnode, "phy-mode", val, sizeof(val)) <= 0 &&
	    OF_getprop(macnode, "phy-connection-type", val, sizeof(val)) <= 0) {
                return (MII_CONTYPE_UNKNOWN);
	}
	return (mii_fdt_contype_from_name(val));
}

void
mii_fdt_free_config(struct mii_fdt_phy_config *cfg)
{

	free(cfg, M_OFWPROP);
}

mii_fdt_phy_config_t *
mii_fdt_get_config(device_t phydev)
{
	mii_fdt_phy_config_t *cfg;
	device_t miibus, macdev;
	pcell_t val;

	miibus = device_get_parent(phydev);
	macdev = device_get_parent(miibus);

	cfg = malloc(sizeof(*cfg), M_OFWPROP, M_ZERO | M_WAITOK);

	/*
	 * If we can't find our parent MAC's node, there's nothing more we can
	 * fill in; cfg is already full of zero/default values, return it.
	 */
	if ((cfg->macnode = ofw_bus_get_node(macdev)) == -1)
		return (cfg);

	cfg->con_type = mii_fdt_get_contype(cfg->macnode);

	/*
	 * If we can't find our own PHY node, there's nothing more we can fill
	 * in, just return what we've got.
	 */
	if ((cfg->phynode = mii_fdt_get_phynode(cfg->macnode)) == -1)
		return (cfg);

	if (OF_getencprop(cfg->phynode, "max-speed", &val, sizeof(val)) > 0)
		cfg->max_speed = val;

	if (ofw_bus_node_is_compatible(cfg->phynode,
	    "ethernet-phy-ieee802.3-c45"))
		cfg->flags |= MIIF_FDT_COMPAT_CLAUSE45;

	if (OF_hasprop(cfg->phynode, "broken-turn-around"))
		cfg->flags |= MIIF_FDT_BROKEN_TURNAROUND;
	if (OF_hasprop(cfg->phynode, "enet-phy-lane-swap"))
		cfg->flags |= MIIF_FDT_LANE_SWAP;
	if (OF_hasprop(cfg->phynode, "enet-phy-lane-no-swap"))
		cfg->flags |= MIIF_FDT_NO_LANE_SWAP;
	if (OF_hasprop(cfg->phynode, "eee-broken-100tx"))
		cfg->flags |= MIIF_FDT_EEE_BROKEN_100TX;
	if (OF_hasprop(cfg->phynode, "eee-broken-1000t"))
		cfg->flags |= MIIF_FDT_EEE_BROKEN_1000T;
	if (OF_hasprop(cfg->phynode, "eee-broken-10gt"))
		cfg->flags |= MIIF_FDT_EEE_BROKEN_10GT;
	if (OF_hasprop(cfg->phynode, "eee-broken-1000kx"))
		cfg->flags |= MIIF_FDT_EEE_BROKEN_1000KX;
	if (OF_hasprop(cfg->phynode, "eee-broken-10gkx4"))
		cfg->flags |= MIIF_FDT_EEE_BROKEN_10GKX4;
	if (OF_hasprop(cfg->phynode, "eee-broken-10gkr"))
		cfg->flags |= MIIF_FDT_EEE_BROKEN_10GKR;

	return (cfg);
}
