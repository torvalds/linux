/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009-2010 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Semihalf under sponsorship from
 * the FreeBSD Foundation.
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
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#include <contrib/libfdt/libfdt.h>

#include <machine/stdarg.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofwvar.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus_subr.h>

#include "ofw_if.h"

#ifdef DEBUG
#define debugf(fmt, args...) do { printf("%s(): ", __func__);	\
    printf(fmt,##args); } while (0)
#else
#define debugf(fmt, args...)
#endif

#if defined(__arm__)
#if defined(SOC_MV_ARMADAXP) || defined(SOC_MV_ARMADA38X) || \
    defined(SOC_MV_DISCOVERY) || defined(SOC_MV_DOVE) || \
    defined(SOC_MV_FREY) || defined(SOC_MV_KIRKWOOD) || \
    defined(SOC_MV_LOKIPLUS) || defined(SOC_MV_ORION)
#define FDT_MARVELL
#endif
#endif

static int ofw_fdt_init(ofw_t, void *);
static phandle_t ofw_fdt_peer(ofw_t, phandle_t);
static phandle_t ofw_fdt_child(ofw_t, phandle_t);
static phandle_t ofw_fdt_parent(ofw_t, phandle_t);
static phandle_t ofw_fdt_instance_to_package(ofw_t, ihandle_t);
static ssize_t ofw_fdt_getproplen(ofw_t, phandle_t, const char *);
static ssize_t ofw_fdt_getprop(ofw_t, phandle_t, const char *, void *, size_t);
static int ofw_fdt_nextprop(ofw_t, phandle_t, const char *, char *, size_t);
static int ofw_fdt_setprop(ofw_t, phandle_t, const char *, const void *,
    size_t);
static ssize_t ofw_fdt_canon(ofw_t, const char *, char *, size_t);
static phandle_t ofw_fdt_finddevice(ofw_t, const char *);
static ssize_t ofw_fdt_instance_to_path(ofw_t, ihandle_t, char *, size_t);
static ssize_t ofw_fdt_package_to_path(ofw_t, phandle_t, char *, size_t);
static int ofw_fdt_interpret(ofw_t, const char *, int, cell_t *);

static ofw_method_t ofw_fdt_methods[] = {
	OFWMETHOD(ofw_init,			ofw_fdt_init),
	OFWMETHOD(ofw_peer,			ofw_fdt_peer),
	OFWMETHOD(ofw_child,			ofw_fdt_child),
	OFWMETHOD(ofw_parent,			ofw_fdt_parent),
	OFWMETHOD(ofw_instance_to_package,	ofw_fdt_instance_to_package),
	OFWMETHOD(ofw_getproplen,		ofw_fdt_getproplen),
	OFWMETHOD(ofw_getprop,			ofw_fdt_getprop),
	OFWMETHOD(ofw_nextprop,			ofw_fdt_nextprop),
	OFWMETHOD(ofw_setprop,			ofw_fdt_setprop),
	OFWMETHOD(ofw_canon,			ofw_fdt_canon),
	OFWMETHOD(ofw_finddevice,		ofw_fdt_finddevice),
	OFWMETHOD(ofw_instance_to_path,		ofw_fdt_instance_to_path),
	OFWMETHOD(ofw_package_to_path,		ofw_fdt_package_to_path),
	OFWMETHOD(ofw_interpret,		ofw_fdt_interpret),
	{ 0, 0 }
};

static ofw_def_t ofw_fdt = {
	OFW_FDT,
	ofw_fdt_methods,
	0
};
OFW_DEF(ofw_fdt);

static void *fdtp = NULL;

static int
sysctl_handle_dtb(SYSCTL_HANDLER_ARGS)
{

        return (sysctl_handle_opaque(oidp, fdtp, fdt_totalsize(fdtp), req));
}

static void
sysctl_register_fdt_oid(void *arg)
{

	/* If there is no FDT registered, skip adding the sysctl */
	if (fdtp == NULL)
		return;

	SYSCTL_ADD_PROC(NULL, SYSCTL_STATIC_CHILDREN(_hw_fdt), OID_AUTO, "dtb",
	    CTLTYPE_OPAQUE | CTLFLAG_RD, NULL, 0, sysctl_handle_dtb, "",
	    "Device Tree Blob");
}
SYSINIT(dtb_oid, SI_SUB_KMEM, SI_ORDER_ANY, sysctl_register_fdt_oid, NULL);

static int
ofw_fdt_init(ofw_t ofw, void *data)
{
	int err;

	/* Check FDT blob integrity */
	if ((err = fdt_check_header(data)) != 0)
		return (err);

	fdtp = data;
	return (0);
}

/*
 * Device tree functions.
 *
 * We use the offset from fdtp to the node as the 'phandle' in OF interface.
 *
 * phandle is a u32 value, therefore we cannot use the pointer to node as
 * phandle in 64 bit. We also do not use the usual fdt offset as phandle,
 * as it can be 0, and the OF interface has special meaning for phandle 0.
 */

static phandle_t
fdt_offset_phandle(int offset)
{
	if (offset < 0)
		return (0);
	return ((phandle_t)offset + fdt_off_dt_struct(fdtp));
}

static int
fdt_phandle_offset(phandle_t p)
{
	int pint = (int)p;
	int dtoff = fdt_off_dt_struct(fdtp);

	if (pint < dtoff)
		return (-1);
	return (pint - dtoff);
}

/* Return the next sibling of this node or 0. */
static phandle_t
ofw_fdt_peer(ofw_t ofw, phandle_t node)
{
	int offset;

	if (node == 0) {
		/* Find root node */
		offset = fdt_path_offset(fdtp, "/");

		return (fdt_offset_phandle(offset));
	}

	offset = fdt_phandle_offset(node);
	if (offset < 0)
		return (0);
	offset = fdt_next_subnode(fdtp, offset);
	return (fdt_offset_phandle(offset));
}

/* Return the first child of this node or 0. */
static phandle_t
ofw_fdt_child(ofw_t ofw, phandle_t node)
{
	int offset;

	offset = fdt_phandle_offset(node);
	if (offset < 0)
		return (0);
	offset = fdt_first_subnode(fdtp, offset);
	return (fdt_offset_phandle(offset));
}

/* Return the parent of this node or 0. */
static phandle_t
ofw_fdt_parent(ofw_t ofw, phandle_t node)
{
	int offset, paroffset;

	offset = fdt_phandle_offset(node);
	if (offset < 0)
		return (0);

	paroffset = fdt_parent_offset(fdtp, offset);
	return (fdt_offset_phandle(paroffset));
}

/* Return the package handle that corresponds to an instance handle. */
static phandle_t
ofw_fdt_instance_to_package(ofw_t ofw, ihandle_t instance)
{

	/* Where real OF uses ihandles in the tree, FDT uses xref phandles */
	return (OF_node_from_xref(instance));
}

/* Get the length of a property of a package. */
static ssize_t
ofw_fdt_getproplen(ofw_t ofw, phandle_t package, const char *propname)
{
	const void *prop;
	int offset, len;

	offset = fdt_phandle_offset(package);
	if (offset < 0)
		return (-1);

	len = -1;
	prop = fdt_getprop(fdtp, offset, propname, &len);

	if (prop == NULL && strcmp(propname, "name") == 0) {
		/* Emulate the 'name' property */
		fdt_get_name(fdtp, offset, &len);
		return (len + 1);
	}

	if (prop == NULL && offset == fdt_path_offset(fdtp, "/chosen")) {
		if (strcmp(propname, "fdtbootcpu") == 0)
			return (sizeof(cell_t));
		if (strcmp(propname, "fdtmemreserv") == 0)
			return (sizeof(uint64_t)*2*fdt_num_mem_rsv(fdtp));
	}

	if (prop == NULL)
		return (-1);

	return (len);
}

/* Get the value of a property of a package. */
static ssize_t
ofw_fdt_getprop(ofw_t ofw, phandle_t package, const char *propname, void *buf,
    size_t buflen)
{
	const void *prop;
	const char *name;
	int len, offset;
	uint32_t cpuid;

	offset = fdt_phandle_offset(package);
	if (offset < 0)
		return (-1);

	prop = fdt_getprop(fdtp, offset, propname, &len);

	if (prop == NULL && strcmp(propname, "name") == 0) {
		/* Emulate the 'name' property */
		name = fdt_get_name(fdtp, offset, &len);
		strncpy(buf, name, buflen);
		return (len + 1);
	}

	if (prop == NULL && offset == fdt_path_offset(fdtp, "/chosen")) {
		if (strcmp(propname, "fdtbootcpu") == 0) {
			cpuid = cpu_to_fdt32(fdt_boot_cpuid_phys(fdtp));
			len = sizeof(cpuid);
			prop = &cpuid;
		}
		if (strcmp(propname, "fdtmemreserv") == 0) {
			prop = (char *)fdtp + fdt_off_mem_rsvmap(fdtp);
			len = sizeof(uint64_t)*2*fdt_num_mem_rsv(fdtp);
		}
	}

	if (prop == NULL)
		return (-1);

	bcopy(prop, buf, min(len, buflen));

	return (len);
}

/*
 * Get the next property of a package. Return values:
 *  -1: package or previous property does not exist
 *   0: no more properties
 *   1: success
 */
static int
ofw_fdt_nextprop(ofw_t ofw, phandle_t package, const char *previous, char *buf,
    size_t size)
{
	const void *prop;
	const char *name;
	int offset;

	offset = fdt_phandle_offset(package);
	if (offset < 0)
		return (-1);

	if (previous == NULL)
		/* Find the first prop in the node */
		offset = fdt_first_property_offset(fdtp, offset);
	else {
		fdt_for_each_property_offset(offset, fdtp, offset) {
			prop = fdt_getprop_by_offset(fdtp, offset, &name, NULL);
			if (prop == NULL)
				return (-1); /* Internal error */
			/* Skip until we find 'previous', then bail out */
			if (strcmp(name, previous) != 0)
				continue;
			offset = fdt_next_property_offset(fdtp, offset);
			break;
		}
	}

	if (offset < 0)
		return (0); /* No properties */

	prop = fdt_getprop_by_offset(fdtp, offset, &name, &offset);
	if (prop == NULL)
		return (-1); /* Internal error */

	strncpy(buf, name, size);

	return (1);
}

/* Set the value of a property of a package. */
static int
ofw_fdt_setprop(ofw_t ofw, phandle_t package, const char *propname,
    const void *buf, size_t len)
{
	int offset;

	offset = fdt_phandle_offset(package);
	if (offset < 0)
		return (-1);

	if (fdt_setprop_inplace(fdtp, offset, propname, buf, len) != 0)
		/* Try to add property, when setting value inplace failed */
		return (fdt_setprop(fdtp, offset, propname, buf, len));

	return (0);
}

/* Convert a device specifier to a fully qualified pathname. */
static ssize_t
ofw_fdt_canon(ofw_t ofw, const char *device, char *buf, size_t len)
{

	return (-1);
}

/* Return a package handle for the specified device. */
static phandle_t
ofw_fdt_finddevice(ofw_t ofw, const char *device)
{
	int offset;

	offset = fdt_path_offset(fdtp, device);
	if (offset < 0)
		return (-1);
	return (fdt_offset_phandle(offset));
}

/* Return the fully qualified pathname corresponding to an instance. */
static ssize_t
ofw_fdt_instance_to_path(ofw_t ofw, ihandle_t instance, char *buf, size_t len)
{
	phandle_t phandle;

	phandle = OF_instance_to_package(instance);
	if (phandle == -1)
		return (-1);

	return (OF_package_to_path(phandle, buf, len));
}

/* Return the fully qualified pathname corresponding to a package. */
static ssize_t
ofw_fdt_package_to_path(ofw_t ofw, phandle_t package, char *buf, size_t len)
{

	return (-1);
}

#if defined(FDT_MARVELL)
static int
ofw_fdt_fixup(ofw_t ofw)
{
#define FDT_MODEL_LEN	80
	char model[FDT_MODEL_LEN];
	phandle_t root;
	ssize_t len;
	int i;

	if ((root = ofw_fdt_finddevice(ofw, "/")) == -1)
		return (ENODEV);

	if ((len = ofw_fdt_getproplen(ofw, root, "model")) <= 0)
		return (0);

	bzero(model, FDT_MODEL_LEN);
	if (ofw_fdt_getprop(ofw, root, "model", model, FDT_MODEL_LEN) <= 0)
		return (0);

	/*
	 * Search fixup table and call handler if appropriate.
	 */
	for (i = 0; fdt_fixup_table[i].model != NULL; i++) {
		if (strncmp(model, fdt_fixup_table[i].model,
		    FDT_MODEL_LEN) != 0)
			/*
			 * Sometimes it's convenient to provide one
			 * fixup entry that refers to many boards.
			 * To handle this case, simply check if model
			 * is compatible parameter
			 */
			if(!ofw_bus_node_is_compatible(root,
			    fdt_fixup_table[i].model))
				continue;

		if (fdt_fixup_table[i].handler != NULL)
			(*fdt_fixup_table[i].handler)(root);
	}

	return (0);
}
#endif

static int
ofw_fdt_interpret(ofw_t ofw, const char *cmd, int nret, cell_t *retvals)
{
#if defined(FDT_MARVELL)
	int rv;

	/*
	 * Note: FDT does not have the possibility to 'interpret' commands,
	 * but we abuse the interface a bit to use it for doing non-standard
	 * operations on the device tree blob.
	 *
	 * Currently the only supported 'command' is to trigger performing
	 * fixups.
	 */
	if (strncmp("perform-fixup", cmd, 13) != 0)
		return (0);

	rv = ofw_fdt_fixup(ofw);
	if (nret > 0)
		retvals[0] = rv;

	return (rv);
#else
	return (0);
#endif
}
