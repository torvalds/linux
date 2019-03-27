/*-
 * Copyright (c) 2015 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Semihalf under
 * the sponsorship of the FreeBSD Foundation.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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
#include <sys/bitset.h>
#include <sys/bitstring.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/pciio.h>
#include <sys/pcpu.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/cpuset.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_media.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/mii/miivar.h>

#include "thunder_bgx.h"
#include "thunder_bgx_var.h"

#define	CONN_TYPE_MAXLEN	16
#define	CONN_TYPE_OFFSET	2

#define	BGX_NODE_NAME		"bgx"
#define	BGX_MAXID		9
/* BGX func. 0, i.e.: reg = <0x8000 0 0 0 0>; DEVFN = 0x80 */
#define	BGX_DEVFN_0		0x80

#define	FDT_NAME_MAXLEN		31

int bgx_fdt_init_phy(struct bgx *);

static void
bgx_fdt_get_macaddr(phandle_t phy, uint8_t *hwaddr)
{
	uint8_t addr[ETHER_ADDR_LEN];

	if (OF_getprop(phy, "local-mac-address", addr, ETHER_ADDR_LEN) == -1) {
		/* Missing MAC address should be marked by clearing it */
		memset(hwaddr, 0, ETHER_ADDR_LEN);
	} else
		memcpy(hwaddr, addr, ETHER_ADDR_LEN);
}

static boolean_t
bgx_fdt_phy_mode_match(struct bgx *bgx, char *qlm_mode, ssize_t size)
{
	const char *type;
	ssize_t sz;
	ssize_t offset;

	switch (bgx->qlm_mode) {
	case QLM_MODE_SGMII:
		type = "sgmii";
		sz = sizeof("sgmii");
		offset = size - sz;
		break;
	case QLM_MODE_XAUI_1X4:
		type = "xaui";
		sz = sizeof("xaui");
		offset = size - sz;
		if (offset < 0)
			return (FALSE);
		if (strncmp(&qlm_mode[offset], type, sz) == 0)
			return (TRUE);
		type = "dxaui";
		sz = sizeof("dxaui");
		offset = size - sz;
		break;
	case QLM_MODE_RXAUI_2X2:
		type = "raui";
		sz = sizeof("raui");
		offset = size - sz;
		break;
	case QLM_MODE_XFI_4X1:
		type = "xfi";
		sz = sizeof("xfi");
		offset = size - sz;
		break;
	case QLM_MODE_XLAUI_1X4:
		type = "xlaui";
		sz = sizeof("xlaui");
		offset = size - sz;
		break;
	case QLM_MODE_10G_KR_4X1:
		type = "xfi-10g-kr";
		sz = sizeof("xfi-10g-kr");
		offset = size - sz;
		break;
	case QLM_MODE_40G_KR4_1X4:
		type = "xlaui-40g-kr";
		sz = sizeof("xlaui-40g-kr");
		offset = size - sz;
		break;
	default:
		return (FALSE);
	}

	if (offset < 0)
		return (FALSE);

	if (strncmp(&qlm_mode[offset], type, sz) == 0)
		return (TRUE);

	return (FALSE);
}

static boolean_t
bgx_fdt_phy_name_match(struct bgx *bgx, char *phy_name, ssize_t size)
{
	const char *type;
	ssize_t sz;

	switch (bgx->qlm_mode) {
	case QLM_MODE_SGMII:
		type = "sgmii";
		sz = sizeof("sgmii");
		break;
	case QLM_MODE_XAUI_1X4:
		type = "xaui";
		sz = sizeof("xaui");
		if (sz < size)
			return (FALSE);
		if (strncmp(phy_name, type, sz) == 0)
			return (TRUE);
		type = "dxaui";
		sz = sizeof("dxaui");
		break;
	case QLM_MODE_RXAUI_2X2:
		type = "raui";
		sz = sizeof("raui");
		break;
	case QLM_MODE_XFI_4X1:
		type = "xfi";
		sz = sizeof("xfi");
		break;
	case QLM_MODE_XLAUI_1X4:
		type = "xlaui";
		sz = sizeof("xlaui");
		break;
	case QLM_MODE_10G_KR_4X1:
		type = "xfi-10g-kr";
		sz = sizeof("xfi-10g-kr");
		break;
	case QLM_MODE_40G_KR4_1X4:
		type = "xlaui-40g-kr";
		sz = sizeof("xlaui-40g-kr");
		break;
	default:
		return (FALSE);
	}

	if (sz > size)
		return (FALSE);
	if (strncmp(phy_name, type, sz - 1) == 0 &&
	    (phy_name[sz - 1] == '\0' || phy_name[sz - 1] == '@'))
		return (TRUE);

	return (FALSE);
}

static phandle_t
bgx_fdt_traverse_nodes(uint8_t unit, phandle_t start, char *name,
    size_t len)
{
	phandle_t node, ret;
	uint32_t *reg;
	size_t buf_size;
	ssize_t proplen;
	char *node_name;
	int err;

	/*
	 * Traverse all subordinate nodes of 'start' to find BGX instance.
	 * This supports both old (by name) and new (by reg) methods.
	 */
	buf_size = sizeof(*node_name) * FDT_NAME_MAXLEN;
	if (len > buf_size) {
		/*
		 * This is an erroneous situation since the string
		 * to compare cannot be longer than FDT_NAME_MAXLEN.
		 */
		return (0);
	}

	node_name = malloc(buf_size, M_BGX, M_WAITOK);
	for (node = OF_child(start); node != 0; node = OF_peer(node)) {
		/* Clean-up the buffer */
		memset(node_name, 0, buf_size);
		/* Recurse to children */
		if (OF_child(node) != 0) {
			ret = bgx_fdt_traverse_nodes(unit, node, name, len);
			if (ret != 0) {
				free(node_name, M_BGX);
				return (ret);
			}
		}
		/*
		 * Old way - by name
		 */
		proplen = OF_getproplen(node, "name");
		if ((proplen <= 0) || (proplen < len))
			continue;

		err = OF_getprop(node, "name", node_name, proplen);
		if (err <= 0)
			continue;

		if (strncmp(node_name, name, len) == 0) {
			free(node_name, M_BGX);
			return (node);
		}
		/*
		 * New way - by reg
		 */
		/* Check if even BGX */
		if (strncmp(node_name,
		    BGX_NODE_NAME, sizeof(BGX_NODE_NAME) - 1) != 0)
			continue;
		/* Get reg */
		err = OF_getencprop_alloc_multi(node, "reg", sizeof(*reg),
		    (void **)&reg);
		if (err == -1) {
			free(reg, M_OFWPROP);
			continue;
		}

		/* Match BGX device function */
		if ((BGX_DEVFN_0 + unit) == (reg[0] >> 8)) {
			free(reg, M_OFWPROP);
			free(node_name, M_BGX);
			return (node);
		}
		free(reg, M_OFWPROP);
	}
	free(node_name, M_BGX);

	return (0);
}

/*
 * Similar functionality to pci_find_pcie_root_port()
 * but this one works for ThunderX.
 */
static device_t
bgx_find_root_pcib(device_t dev)
{
	devclass_t pci_class;
	device_t pcib, bus;

	pci_class = devclass_find("pci");
	KASSERT(device_get_devclass(device_get_parent(dev)) == pci_class,
	    ("%s: non-pci device %s", __func__, device_get_nameunit(dev)));

	/* Walk the bridge hierarchy until we find a non-PCI device */
	for (;;) {
		bus = device_get_parent(dev);
		KASSERT(bus != NULL, ("%s: null parent of %s", __func__,
		    device_get_nameunit(dev)));

		if (device_get_devclass(bus) != pci_class)
			return (NULL);

		pcib = device_get_parent(bus);
		KASSERT(pcib != NULL, ("%s: null bridge of %s", __func__,
		    device_get_nameunit(bus)));

		/*
		 * If the parent of this PCIB is not PCI
		 * then we found our root PCIB.
		 */
		if (device_get_devclass(device_get_parent(pcib)) != pci_class)
			return (pcib);

		dev = pcib;
	}
}

static __inline phandle_t
bgx_fdt_find_node(struct bgx *bgx)
{
	device_t root_pcib;
	phandle_t node;
	char *bgx_sel;
	size_t len;

	KASSERT(bgx->bgx_id <= BGX_MAXID,
	    ("Invalid BGX ID: %d, max: %d", bgx->bgx_id, BGX_MAXID));

	len = sizeof(BGX_NODE_NAME) + 1; /* <bgx_name>+<digit>+<\0> */
	/* Allocate memory for BGX node name + "/" character */
	bgx_sel = malloc(sizeof(*bgx_sel) * (len + 1), M_BGX,
	    M_ZERO | M_WAITOK);

	/* Prepare node's name */
	snprintf(bgx_sel, len + 1, "/"BGX_NODE_NAME"%d", bgx->bgx_id);
	/* First try the root node */
	node =  OF_finddevice(bgx_sel);
	if (node != -1) {
		/* Found relevant node */
		goto out;
	}
	/*
	 * Clean-up and try to find BGX in DT
	 * starting from the parent PCI bridge node.
	 */
	memset(bgx_sel, 0, sizeof(*bgx_sel) * (len + 1));
	snprintf(bgx_sel, len, BGX_NODE_NAME"%d", bgx->bgx_id);

	/* Find PCI bridge that we are connected to */

	root_pcib = bgx_find_root_pcib(bgx->dev);
	if (root_pcib == NULL) {
		device_printf(bgx->dev, "Unable to find BGX root bridge\n");
		node = 0;
		goto out;
	}

	node = ofw_bus_get_node(root_pcib);
	if ((int)node <= 0) {
		device_printf(bgx->dev, "No parent FDT node for BGX\n");
		goto out;
	}

	node = bgx_fdt_traverse_nodes(bgx->bgx_id, node, bgx_sel, len);
out:
	free(bgx_sel, M_BGX);
	return (node);
}

int
bgx_fdt_init_phy(struct bgx *bgx)
{
	char *node_name;
	phandle_t node, child;
	phandle_t phy, mdio;
	ssize_t len;
	uint8_t lmac;
	char qlm_mode[CONN_TYPE_MAXLEN];

	node = bgx_fdt_find_node(bgx);
	if (node == 0) {
		device_printf(bgx->dev,
		    "Could not find bgx%d node in FDT\n", bgx->bgx_id);
		return (ENXIO);
	}

	lmac = 0;
	for (child = OF_child(node); child > 0; child = OF_peer(child)) {
		len = OF_getprop(child, "qlm-mode", qlm_mode, sizeof(qlm_mode));
		if (len > 0) {
			if (!bgx_fdt_phy_mode_match(bgx, qlm_mode, len)) {
				/*
				 * Connection type not match with BGX mode.
				 */
				continue;
			}
		} else {
			len = OF_getprop_alloc(child, "name",
			    (void **)&node_name);
			if (len <= 0) {
				continue;
			}

			if (!bgx_fdt_phy_name_match(bgx, node_name, len)) {
				free(node_name, M_OFWPROP);
				continue;
			}
			free(node_name, M_OFWPROP);
		}

		/* Acquire PHY address */
		if (OF_getencprop(child, "reg", &bgx->lmac[lmac].phyaddr,
		    sizeof(bgx->lmac[lmac].phyaddr)) <= 0) {
			if (bootverbose) {
				device_printf(bgx->dev,
				    "Could not retrieve PHY address\n");
			}
			bgx->lmac[lmac].phyaddr = MII_PHY_ANY;
		}

		if (OF_getencprop(child, "phy-handle", &phy,
		    sizeof(phy)) <= 0) {
			if (bootverbose) {
				device_printf(bgx->dev,
				    "No phy-handle in PHY node. Skipping...\n");
			}
			continue;
		}
		phy = OF_instance_to_package(phy);
		/*
		 * Get PHY interface (MDIO bus) device.
		 * Driver must be already attached.
		 */
		mdio = OF_parent(phy);
		bgx->lmac[lmac].phy_if_dev =
		    OF_device_from_xref(OF_xref_from_node(mdio));
		if (bgx->lmac[lmac].phy_if_dev == NULL) {
			if (bootverbose) {
				device_printf(bgx->dev,
				    "Could not find interface to PHY\n");
			}
			continue;
		}

		/* Get mac address from FDT */
		bgx_fdt_get_macaddr(child, bgx->lmac[lmac].mac);

		bgx->lmac[lmac].lmacid = lmac;
		lmac++;
		if (lmac == MAX_LMAC_PER_BGX)
			break;
	}
	if (lmac == 0) {
		device_printf(bgx->dev, "Could not find matching PHY\n");
		return (ENXIO);
	}

	return (0);
}
