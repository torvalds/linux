/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 - 2003 by Thomas Moestl <tmm@FreeBSD.org>.
 * Copyright (c) 2005 Marius Strobl <marius@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_platform.h"
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/errno.h>
#include <sys/libkern.h>

#include <machine/resource.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/openfirm.h>

#include "ofw_bus_if.h"

#define	OFW_COMPAT_LEN	255
#define	OFW_STATUS_LEN	16

int
ofw_bus_gen_setup_devinfo(struct ofw_bus_devinfo *obd, phandle_t node)
{

	if (obd == NULL)
		return (ENOMEM);
	/* The 'name' property is considered mandatory. */
	if ((OF_getprop_alloc(node, "name", (void **)&obd->obd_name)) == -1)
		return (EINVAL);
	OF_getprop_alloc(node, "compatible", (void **)&obd->obd_compat);
	OF_getprop_alloc(node, "device_type", (void **)&obd->obd_type);
	OF_getprop_alloc(node, "model", (void **)&obd->obd_model);
	OF_getprop_alloc(node, "status", (void **)&obd->obd_status);
	obd->obd_node = node;
	return (0);
}

void
ofw_bus_gen_destroy_devinfo(struct ofw_bus_devinfo *obd)
{

	if (obd == NULL)
		return;
	if (obd->obd_compat != NULL)
		free(obd->obd_compat, M_OFWPROP);
	if (obd->obd_model != NULL)
		free(obd->obd_model, M_OFWPROP);
	if (obd->obd_name != NULL)
		free(obd->obd_name, M_OFWPROP);
	if (obd->obd_type != NULL)
		free(obd->obd_type, M_OFWPROP);
	if (obd->obd_status != NULL)
		free(obd->obd_status, M_OFWPROP);
}

int
ofw_bus_gen_child_pnpinfo_str(device_t cbdev, device_t child, char *buf,
    size_t buflen)
{

	*buf = '\0';
	if (!ofw_bus_status_okay(child))
		return (0);

	if (ofw_bus_get_name(child) != NULL) {
		strlcat(buf, "name=", buflen);
		strlcat(buf, ofw_bus_get_name(child), buflen);
	}

	if (ofw_bus_get_compat(child) != NULL) {
		strlcat(buf, " compat=", buflen);
		strlcat(buf, ofw_bus_get_compat(child), buflen);
	}

	return (0);
};

const char *
ofw_bus_gen_get_compat(device_t bus, device_t dev)
{
	const struct ofw_bus_devinfo *obd;

	obd = OFW_BUS_GET_DEVINFO(bus, dev);
	if (obd == NULL)
		return (NULL);
	return (obd->obd_compat);
}

const char *
ofw_bus_gen_get_model(device_t bus, device_t dev)
{
	const struct ofw_bus_devinfo *obd;

	obd = OFW_BUS_GET_DEVINFO(bus, dev);
	if (obd == NULL)
		return (NULL);
	return (obd->obd_model);
}

const char *
ofw_bus_gen_get_name(device_t bus, device_t dev)
{
	const struct ofw_bus_devinfo *obd;

	obd = OFW_BUS_GET_DEVINFO(bus, dev);
	if (obd == NULL)
		return (NULL);
	return (obd->obd_name);
}

phandle_t
ofw_bus_gen_get_node(device_t bus, device_t dev)
{
	const struct ofw_bus_devinfo *obd;

	obd = OFW_BUS_GET_DEVINFO(bus, dev);
	if (obd == NULL)
		return (0);
	return (obd->obd_node);
}

const char *
ofw_bus_gen_get_type(device_t bus, device_t dev)
{
	const struct ofw_bus_devinfo *obd;

	obd = OFW_BUS_GET_DEVINFO(bus, dev);
	if (obd == NULL)
		return (NULL);
	return (obd->obd_type);
}

const char *
ofw_bus_get_status(device_t dev)
{
	const struct ofw_bus_devinfo *obd;

	obd = OFW_BUS_GET_DEVINFO(device_get_parent(dev), dev);
	if (obd == NULL)
		return (NULL);

	return (obd->obd_status);
}

int
ofw_bus_status_okay(device_t dev)
{
	const char *status;

	status = ofw_bus_get_status(dev);
	if (status == NULL || strcmp(status, "okay") == 0 ||
	    strcmp(status, "ok") == 0)
		return (1);
	
	return (0);
}

int
ofw_bus_node_status_okay(phandle_t node)
{
	char status[OFW_STATUS_LEN];
	int len;

	len = OF_getproplen(node, "status");
	if (len <= 0)
		return (1);

	OF_getprop(node, "status", status, OFW_STATUS_LEN);
	if ((len == 5 && (bcmp(status, "okay", len) == 0)) ||
	    (len == 3 && (bcmp(status, "ok", len))))
		return (1);

	return (0);
}

static int
ofw_bus_node_is_compatible_int(const char *compat, int len,
    const char *onecompat)
{
	int onelen, l, ret;

	onelen = strlen(onecompat);

	ret = 0;
	while (len > 0) {
		if (strlen(compat) == onelen &&
		    strncasecmp(compat, onecompat, onelen) == 0) {
			/* Found it. */
			ret = 1;
			break;
		}

		/* Slide to the next sub-string. */
		l = strlen(compat) + 1;
		compat += l;
		len -= l;
	}

	return (ret);
}

int
ofw_bus_node_is_compatible(phandle_t node, const char *compatstr)
{
	char compat[OFW_COMPAT_LEN];
	int len, rv;

	if ((len = OF_getproplen(node, "compatible")) <= 0)
		return (0);

	bzero(compat, OFW_COMPAT_LEN);

	if (OF_getprop(node, "compatible", compat, OFW_COMPAT_LEN) < 0)
		return (0);

	rv = ofw_bus_node_is_compatible_int(compat, len, compatstr);

	return (rv);
}

int
ofw_bus_is_compatible(device_t dev, const char *onecompat)
{
	phandle_t node;
	const char *compat;
	int len;

	if ((compat = ofw_bus_get_compat(dev)) == NULL)
		return (0);

	if ((node = ofw_bus_get_node(dev)) == -1)
		return (0);

	/* Get total 'compatible' prop len */
	if ((len = OF_getproplen(node, "compatible")) <= 0)
		return (0);

	return (ofw_bus_node_is_compatible_int(compat, len, onecompat));
}

int
ofw_bus_is_compatible_strict(device_t dev, const char *compatible)
{
	const char *compat;
	size_t len;

	if ((compat = ofw_bus_get_compat(dev)) == NULL)
		return (0);

	len = strlen(compatible);
	if (strlen(compat) == len &&
	    strncasecmp(compat, compatible, len) == 0)
		return (1);

	return (0);
}

const struct ofw_compat_data *
ofw_bus_search_compatible(device_t dev, const struct ofw_compat_data *compat)
{

	if (compat == NULL)
		return NULL;

	for (; compat->ocd_str != NULL; ++compat) {
		if (ofw_bus_is_compatible(dev, compat->ocd_str))
			break;
	}

	return (compat);
}

int
ofw_bus_has_prop(device_t dev, const char *propname)
{
	phandle_t node;

	if ((node = ofw_bus_get_node(dev)) == -1)
		return (0);

	return (OF_hasprop(node, propname));
}

void
ofw_bus_setup_iinfo(phandle_t node, struct ofw_bus_iinfo *ii, int intrsz)
{
	pcell_t addrc;
	int msksz;

	if (OF_getencprop(node, "#address-cells", &addrc, sizeof(addrc)) == -1)
		addrc = 2;
	ii->opi_addrc = addrc * sizeof(pcell_t);

	ii->opi_imapsz = OF_getencprop_alloc(node, "interrupt-map",
	    (void **)&ii->opi_imap);
	if (ii->opi_imapsz > 0) {
		msksz = OF_getencprop_alloc(node, "interrupt-map-mask",
		    (void **)&ii->opi_imapmsk);
		/*
		 * Failure to get the mask is ignored; a full mask is used
		 * then.  We barf on bad mask sizes, however.
		 */
		if (msksz != -1 && msksz != ii->opi_addrc + intrsz)
			panic("ofw_bus_setup_iinfo: bad interrupt-map-mask "
			    "property!");
	}
}

int
ofw_bus_lookup_imap(phandle_t node, struct ofw_bus_iinfo *ii, void *reg,
    int regsz, void *pintr, int pintrsz, void *mintr, int mintrsz,
    phandle_t *iparent)
{
	uint8_t maskbuf[regsz + pintrsz];
	int rv;

	if (ii->opi_imapsz <= 0)
		return (0);
	KASSERT(regsz >= ii->opi_addrc,
	    ("ofw_bus_lookup_imap: register size too small: %d < %d",
		regsz, ii->opi_addrc));
	if (node != -1) {
		rv = OF_getencprop(node, "reg", reg, regsz);
		if (rv < regsz)
			panic("ofw_bus_lookup_imap: cannot get reg property");
	}
	return (ofw_bus_search_intrmap(pintr, pintrsz, reg, ii->opi_addrc,
	    ii->opi_imap, ii->opi_imapsz, ii->opi_imapmsk, maskbuf, mintr,
	    mintrsz, iparent));
}

/*
 * Map an interrupt using the firmware reg, interrupt-map and
 * interrupt-map-mask properties.
 * The interrupt property to be mapped must be of size intrsz, and pointed to
 * by intr.  The regs property of the node for which the mapping is done must
 * be passed as regs. This property is an array of register specifications;
 * the size of the address part of such a specification must be passed as
 * physsz.  Only the first element of the property is used.
 * imap and imapsz hold the interrupt mask and it's size.
 * imapmsk is a pointer to the interrupt-map-mask property, which must have
 * a size of physsz + intrsz; it may be NULL, in which case a full mask is
 * assumed.
 * maskbuf must point to a buffer of length physsz + intrsz.
 * The interrupt is returned in result, which must point to a buffer of length
 * rintrsz (which gives the expected size of the mapped interrupt).
 * Returns number of cells in the interrupt if a mapping was found, 0 otherwise.
 */
int
ofw_bus_search_intrmap(void *intr, int intrsz, void *regs, int physsz,
    void *imap, int imapsz, void *imapmsk, void *maskbuf, void *result,
    int rintrsz, phandle_t *iparent)
{
	phandle_t parent;
	uint8_t *ref = maskbuf;
	uint8_t *uiintr = intr;
	uint8_t *uiregs = regs;
	uint8_t *uiimapmsk = imapmsk;
	uint8_t *mptr;
	pcell_t paddrsz;
	pcell_t pintrsz;
	int i, tsz;

	if (imapmsk != NULL) {
		for (i = 0; i < physsz; i++)
			ref[i] = uiregs[i] & uiimapmsk[i];
		for (i = 0; i < intrsz; i++)
			ref[physsz + i] = uiintr[i] & uiimapmsk[physsz + i];
	} else {
		bcopy(regs, ref, physsz);
		bcopy(intr, ref + physsz, intrsz);
	}

	mptr = imap;
	i = imapsz;
	paddrsz = 0;
	while (i > 0) {
		bcopy(mptr + physsz + intrsz, &parent, sizeof(parent));
#ifndef OFW_IMAP_NO_IPARENT_ADDR_CELLS
		/*
		 * Find if we need to read the parent address data.
		 * CHRP-derived OF bindings, including ePAPR-compliant FDTs,
		 * use this as an optional part of the specifier.
		 */
		if (OF_getencprop(OF_node_from_xref(parent),
		    "#address-cells", &paddrsz, sizeof(paddrsz)) == -1)
			paddrsz = 0;	/* default */
		paddrsz *= sizeof(pcell_t);
#endif

		if (OF_searchencprop(OF_node_from_xref(parent),
		    "#interrupt-cells", &pintrsz, sizeof(pintrsz)) == -1)
			pintrsz = 1;	/* default */
		pintrsz *= sizeof(pcell_t);

		/* Compute the map stride size. */
		tsz = physsz + intrsz + sizeof(phandle_t) + paddrsz + pintrsz;
		KASSERT(i >= tsz, ("ofw_bus_search_intrmap: truncated map"));

		if (bcmp(ref, mptr, physsz + intrsz) == 0) {
			bcopy(mptr + physsz + intrsz + sizeof(parent) + paddrsz,
			    result, MIN(rintrsz, pintrsz));

			if (iparent != NULL)
				*iparent = parent;
			return (pintrsz/sizeof(pcell_t));
		}
		mptr += tsz;
		i -= tsz;
	}
	return (0);
}

int
ofw_bus_msimap(phandle_t node, uint16_t pci_rid, phandle_t *msi_parent,
    uint32_t *msi_rid)
{
	pcell_t *map, mask, msi_base, rid_base, rid_length;
	ssize_t len;
	uint32_t masked_rid;
	int err, i;

	/* TODO: This should be OF_searchprop_alloc if we had it */
	len = OF_getencprop_alloc_multi(node, "msi-map", sizeof(*map),
	    (void **)&map);
	if (len < 0) {
		if (msi_parent != NULL) {
			*msi_parent = 0;
			OF_getencprop(node, "msi-parent", msi_parent,
			    sizeof(*msi_parent));
		}
		if (msi_rid != NULL)
			*msi_rid = pci_rid;
		return (0);
	}

	err = ENOENT;
	mask = 0xffffffff;
	OF_getencprop(node, "msi-map-mask", &mask, sizeof(mask));

	masked_rid = pci_rid & mask;
	for (i = 0; i < len; i += 4) {
		rid_base = map[i + 0];
		rid_length = map[i + 3];

		if (masked_rid < rid_base ||
		    masked_rid >= (rid_base + rid_length))
			continue;

		msi_base = map[i + 2];

		if (msi_parent != NULL)
			*msi_parent = map[i + 1];
		if (msi_rid != NULL)
			*msi_rid = masked_rid - rid_base + msi_base;
		err = 0;
		break;
	}

	free(map, M_OFWPROP);

	return (err);
}

static int
ofw_bus_reg_to_rl_helper(device_t dev, phandle_t node, pcell_t acells, pcell_t scells,
    struct resource_list *rl, const char *reg_source)
{
	uint64_t phys, size;
	ssize_t i, j, rid, nreg, ret;
	uint32_t *reg;
	char *name;

	/*
	 * This may be just redundant when having ofw_bus_devinfo
	 * but makes this routine independent of it.
	 */
	ret = OF_getprop_alloc(node, "name", (void **)&name);
	if (ret == -1)
		name = NULL;

	ret = OF_getencprop_alloc_multi(node, reg_source, sizeof(*reg),
	    (void **)&reg);
	nreg = (ret == -1) ? 0 : ret;

	if (nreg % (acells + scells) != 0) {
		if (bootverbose)
			device_printf(dev, "Malformed reg property on <%s>\n",
			    (name == NULL) ? "unknown" : name);
		nreg = 0;
	}

	for (i = 0, rid = 0; i < nreg; i += acells + scells, rid++) {
		phys = size = 0;
		for (j = 0; j < acells; j++) {
			phys <<= 32;
			phys |= reg[i + j];
		}
		for (j = 0; j < scells; j++) {
			size <<= 32;
			size |= reg[i + acells + j];
		}
		/* Skip the dummy reg property of glue devices like ssm(4). */
		if (size != 0)
			resource_list_add(rl, SYS_RES_MEMORY, rid,
			    phys, phys + size - 1, size);
	}
	free(name, M_OFWPROP);
	free(reg, M_OFWPROP);

	return (0);
}

int
ofw_bus_reg_to_rl(device_t dev, phandle_t node, pcell_t acells, pcell_t scells,
    struct resource_list *rl)
{

	return (ofw_bus_reg_to_rl_helper(dev, node, acells, scells, rl, "reg"));
}

int
ofw_bus_assigned_addresses_to_rl(device_t dev, phandle_t node, pcell_t acells,
    pcell_t scells, struct resource_list *rl)
{

	return (ofw_bus_reg_to_rl_helper(dev, node, acells, scells,
	    rl, "assigned-addresses"));
}

/*
 * Get interrupt parent for given node.
 * Returns 0 if interrupt parent doesn't exist.
 */
phandle_t
ofw_bus_find_iparent(phandle_t node)
{
	phandle_t iparent;

	if (OF_searchencprop(node, "interrupt-parent", &iparent,
		    sizeof(iparent)) == -1) {
		for (iparent = node; iparent != 0;
		    iparent = OF_parent(iparent)) {
			if (OF_hasprop(iparent, "interrupt-controller"))
				break;
		}
		iparent = OF_xref_from_node(iparent);
	}
	return (iparent);
}

int
ofw_bus_intr_to_rl(device_t dev, phandle_t node,
    struct resource_list *rl, int *rlen)
{
	phandle_t iparent;
	uint32_t icells, *intr;
	int err, i, irqnum, nintr, rid;
	boolean_t extended;

	nintr = OF_getencprop_alloc_multi(node, "interrupts",  sizeof(*intr),
	    (void **)&intr);
	if (nintr > 0) {
		iparent = ofw_bus_find_iparent(node);
		if (iparent == 0) {
			device_printf(dev, "No interrupt-parent found, "
			    "assuming direct parent\n");
			iparent = OF_parent(node);
			iparent = OF_xref_from_node(iparent);
		}
		if (OF_searchencprop(OF_node_from_xref(iparent), 
		    "#interrupt-cells", &icells, sizeof(icells)) == -1) {
			device_printf(dev, "Missing #interrupt-cells "
			    "property, assuming <1>\n");
			icells = 1;
		}
		if (icells < 1 || icells > nintr) {
			device_printf(dev, "Invalid #interrupt-cells property "
			    "value <%d>, assuming <1>\n", icells);
			icells = 1;
		}
		extended = false;
	} else {
		nintr = OF_getencprop_alloc_multi(node, "interrupts-extended",
		    sizeof(*intr), (void **)&intr);
		if (nintr <= 0)
			return (0);
		extended = true;
	}
	err = 0;
	rid = 0;
	for (i = 0; i < nintr; i += icells) {
		if (extended) {
			iparent = intr[i++];
			if (OF_searchencprop(OF_node_from_xref(iparent), 
			    "#interrupt-cells", &icells, sizeof(icells)) == -1) {
				device_printf(dev, "Missing #interrupt-cells "
				    "property\n");
				err = ENOENT;
				break;
			}
			if (icells < 1 || (i + icells) > nintr) {
				device_printf(dev, "Invalid #interrupt-cells "
				    "property value <%d>\n", icells);
				err = ERANGE;
				break;
			}
		}
		irqnum = ofw_bus_map_intr(dev, iparent, icells, &intr[i]);
		resource_list_add(rl, SYS_RES_IRQ, rid++, irqnum, irqnum, 1);
	}
	if (rlen != NULL)
		*rlen = rid;
	free(intr, M_OFWPROP);
	return (err);
}

int
ofw_bus_intr_by_rid(device_t dev, phandle_t node, int wanted_rid,
    phandle_t *producer, int *ncells, pcell_t **cells)
{
	phandle_t iparent;
	uint32_t icells, *intr;
	int err, i, nintr, rid;
	boolean_t extended;

	nintr = OF_getencprop_alloc_multi(node, "interrupts",  sizeof(*intr),
	    (void **)&intr);
	if (nintr > 0) {
		iparent = ofw_bus_find_iparent(node);
		if (iparent == 0) {
			device_printf(dev, "No interrupt-parent found, "
			    "assuming direct parent\n");
			iparent = OF_parent(node);
			iparent = OF_xref_from_node(iparent);
		}
		if (OF_searchencprop(OF_node_from_xref(iparent),
		    "#interrupt-cells", &icells, sizeof(icells)) == -1) {
			device_printf(dev, "Missing #interrupt-cells "
			    "property, assuming <1>\n");
			icells = 1;
		}
		if (icells < 1 || icells > nintr) {
			device_printf(dev, "Invalid #interrupt-cells property "
			    "value <%d>, assuming <1>\n", icells);
			icells = 1;
		}
		extended = false;
	} else {
		nintr = OF_getencprop_alloc_multi(node, "interrupts-extended",
		    sizeof(*intr), (void **)&intr);
		if (nintr <= 0)
			return (ESRCH);
		extended = true;
	}
	err = ESRCH;
	rid = 0;
	for (i = 0; i < nintr; i += icells, rid++) {
		if (extended) {
			iparent = intr[i++];
			if (OF_searchencprop(OF_node_from_xref(iparent),
			    "#interrupt-cells", &icells, sizeof(icells)) == -1) {
				device_printf(dev, "Missing #interrupt-cells "
				    "property\n");
				err = ENOENT;
				break;
			}
			if (icells < 1 || (i + icells) > nintr) {
				device_printf(dev, "Invalid #interrupt-cells "
				    "property value <%d>\n", icells);
				err = ERANGE;
				break;
			}
		}
		if (rid == wanted_rid) {
			*cells = malloc(icells * sizeof(**cells), M_OFWPROP,
			    M_WAITOK);
			*producer = iparent;
			*ncells= icells;
			memcpy(*cells, intr + i, icells * sizeof(**cells));
			err = 0;
			break;
		}
	}
	free(intr, M_OFWPROP);
	return (err);
}

phandle_t
ofw_bus_find_child(phandle_t start, const char *child_name)
{
	char *name;
	int ret;
	phandle_t child;

	for (child = OF_child(start); child != 0; child = OF_peer(child)) {
		ret = OF_getprop_alloc(child, "name", (void **)&name);
		if (ret == -1)
			continue;
		if (strcmp(name, child_name) == 0) {
			free(name, M_OFWPROP);
			return (child);
		}

		free(name, M_OFWPROP);
	}

	return (0);
}

phandle_t
ofw_bus_find_compatible(phandle_t node, const char *onecompat)
{
	phandle_t child, ret;

	/*
	 * Traverse all children of 'start' node, and find first with
	 * matching 'compatible' property.
	 */
	for (child = OF_child(node); child != 0; child = OF_peer(child)) {
		if (ofw_bus_node_is_compatible(child, onecompat) != 0)
			return (child);

		ret = ofw_bus_find_compatible(child, onecompat);
		if (ret != 0)
			return (ret);
	}
	return (0);
}

/**
 * @brief Return child of bus whose phandle is node
 *
 * A direct child of @p will be returned if it its phandle in the
 * OFW tree is @p node. Otherwise, NULL is returned.
 *
 * @param bus		The bus to examine
 * @param node		The phandle_t to look for.
 */
device_t
ofw_bus_find_child_device_by_phandle(device_t bus, phandle_t node)
{
	device_t *children, retval, child;
	int nkid, i;

	/*
	 * Nothing can match the flag value for no node.
	 */
	if (node == -1)
		return (NULL);

	/*
	 * Search the children for a match. We microoptimize
	 * a bit by not using ofw_bus_get since we already know
	 * the parent. We do not recurse.
	 */
	if (device_get_children(bus, &children, &nkid) != 0)
		return (NULL);
	retval = NULL;
	for (i = 0; i < nkid; i++) {
		child = children[i];
		if (OFW_BUS_GET_NODE(bus, child) == node) {
			retval = child;
			break;
		}
	}
	free(children, M_TEMP);

	return (retval);
}

/*
 * Parse property that contain list of xrefs and values
 * (like standard "clocks" and "resets" properties)
 * Input arguments:
 *  node - consumers device node
 *  list_name  - name of parsed list - "clocks"
 *  cells_name - name of size property - "#clock-cells"
 *  idx - the index of the requested list entry, or, if -1, an indication
 *        to return the number of entries in the parsed list.
 * Output arguments:
 *  producer - handle of producer
 *  ncells   - number of cells in result or the number of items in the list when
 *             idx == -1.
 *  cells    - array of decoded cells
 */
static int
ofw_bus_parse_xref_list_internal(phandle_t node, const char *list_name,
    const char *cells_name, int idx, phandle_t *producer, int *ncells,
    pcell_t **cells)
{
	phandle_t pnode;
	phandle_t *elems;
	uint32_t  pcells;
	int rv, i, j, nelems, cnt;

	elems = NULL;
	nelems = OF_getencprop_alloc_multi(node, list_name,  sizeof(*elems),
	    (void **)&elems);
	if (nelems <= 0)
		return (ENOENT);
	rv = (idx == -1) ? 0 : ENOENT;
	for (i = 0, cnt = 0; i < nelems; i += pcells, cnt++) {
		pnode = elems[i++];
		if (OF_getencprop(OF_node_from_xref(pnode),
		    cells_name, &pcells, sizeof(pcells)) == -1) {
			printf("Missing %s property\n", cells_name);
			rv = ENOENT;
			break;
		}

		if ((i + pcells) > nelems) {
			printf("Invalid %s property value <%d>\n", cells_name,
			    pcells);
			rv = ERANGE;
			break;
		}
		if (cnt == idx) {
			*cells= malloc(pcells * sizeof(**cells), M_OFWPROP,
			    M_WAITOK);
			*producer = pnode;
			*ncells = pcells;
			for (j = 0; j < pcells; j++)
				(*cells)[j] = elems[i + j];
			rv = 0;
			break;
		}
	}
	if (elems != NULL)
		free(elems, M_OFWPROP);
	if (idx == -1 && rv == 0)
		*ncells = cnt;
	return (rv);
}

/*
 * Parse property that contain list of xrefs and values
 * (like standard "clocks" and "resets" properties)
 * Input arguments:
 *  node - consumers device node
 *  list_name  - name of parsed list - "clocks"
 *  cells_name - name of size property - "#clock-cells"
 *  idx - the index of the requested list entry (>= 0)
 * Output arguments:
 *  producer - handle of producer
 *  ncells   - number of cells in result
 *  cells    - array of decoded cells
 */
int
ofw_bus_parse_xref_list_alloc(phandle_t node, const char *list_name,
    const char *cells_name, int idx, phandle_t *producer, int *ncells,
    pcell_t **cells)
{

	KASSERT(idx >= 0,
	    ("ofw_bus_parse_xref_list_alloc: negative index supplied"));

	return (ofw_bus_parse_xref_list_internal(node, list_name, cells_name,
		    idx, producer, ncells, cells));
}

/*
 * Parse property that contain list of xrefs and values
 * (like standard "clocks" and "resets" properties)
 * and determine the number of items in the list
 * Input arguments:
 *  node - consumers device node
 *  list_name  - name of parsed list - "clocks"
 *  cells_name - name of size property - "#clock-cells"
 * Output arguments:
 *  count - number of items in list
 */
int
ofw_bus_parse_xref_list_get_length(phandle_t node, const char *list_name,
    const char *cells_name, int *count)
{

	return (ofw_bus_parse_xref_list_internal(node, list_name, cells_name,
		    -1, NULL, count, NULL));
}

/*
 * Find index of string in string list property (case sensitive).
 */
int
ofw_bus_find_string_index(phandle_t node, const char *list_name,
    const char *name, int *idx)
{
	char *elems;
	int rv, i, cnt, nelems;

	elems = NULL;
	nelems = OF_getprop_alloc(node, list_name, (void **)&elems);
	if (nelems <= 0)
		return (ENOENT);

	rv = ENOENT;
	for (i = 0, cnt = 0; i < nelems; cnt++) {
		if (strcmp(elems + i, name) == 0) {
			*idx = cnt;
			rv = 0;
			break;
		}
		i += strlen(elems + i) + 1;
	}

	if (elems != NULL)
		free(elems, M_OFWPROP);
	return (rv);
}

/*
 * Create zero terminated array of strings from string list property.
 */
int
ofw_bus_string_list_to_array(phandle_t node, const char *list_name,
   const char ***out_array)
{
	char *elems, *tptr;
	const char **array;
	int i, cnt, nelems, len;

	elems = NULL;
	nelems = OF_getprop_alloc(node, list_name, (void **)&elems);
	if (nelems <= 0)
		return (nelems);

	/* Count number of strings. */
	for (i = 0, cnt = 0; i < nelems; cnt++)
		i += strlen(elems + i) + 1;

	/* Allocate space for arrays and all strings. */
	array = malloc((cnt + 1) * sizeof(char *) + nelems, M_OFWPROP,
	    M_WAITOK);

	/* Get address of first string. */
	tptr = (char *)(array + cnt + 1);

	/* Copy strings. */
	memcpy(tptr, elems, nelems);
	free(elems, M_OFWPROP);

	/* Fill string pointers. */
	for (i = 0, cnt = 0; i < nelems; cnt++) {
		len = strlen(tptr) + 1;
		array[cnt] = tptr;
		i += len;
		tptr += len;
	}
	array[cnt] = NULL;
	*out_array = array;

	return (cnt);
}
