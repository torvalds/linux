/*	$NetBSD: Locore.c,v 1.7 2000/08/20 07:04:59 tsubai Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (C) 1995, 1996 Wolfgang Solfrank.
 * Copyright (C) 1995, 1996 TooLs GmbH.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*-
 * Copyright (C) 2000 Benno Rice.
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
 * THIS SOFTWARE IS PROVIDED BY Benno Rice ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_platform.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/systm.h>
#include <sys/endian.h>

#include <machine/stdarg.h>

#include <dev/ofw/ofwvar.h>
#include <dev/ofw/openfirm.h>

#include "ofw_if.h"

static void OF_putchar(int c, void *arg);

MALLOC_DEFINE(M_OFWPROP, "openfirm", "Open Firmware properties");

static ihandle_t stdout;

static ofw_def_t	*ofw_def_impl = NULL;
static ofw_t		ofw_obj;
static struct ofw_kobj	ofw_kernel_obj;
static struct kobj_ops	ofw_kernel_kops;

struct xrefinfo {
	phandle_t	xref;
	phandle_t 	node;
	device_t  	dev;
	SLIST_ENTRY(xrefinfo) next_entry;
};

static SLIST_HEAD(, xrefinfo) xreflist = SLIST_HEAD_INITIALIZER(xreflist);
static struct mtx xreflist_lock;
static boolean_t xref_init_done;

#define	FIND_BY_XREF	0
#define	FIND_BY_NODE	1
#define	FIND_BY_DEV	2

/*
 * xref-phandle-device lookup helper routines.
 *
 * As soon as we are able to use malloc(), walk the node tree and build a list
 * of info that cross-references node handles, xref handles, and device_t
 * instances.  This list exists primarily to allow association of a device_t
 * with an xref handle, but it is also used to speed up translation between xref
 * and node handles.  Before malloc() is available we have to recursively search
 * the node tree each time we want to translate between a node and xref handle.
 * Afterwards we can do the translations by searching this much shorter list.
 */
static void
xrefinfo_create(phandle_t node)
{
	struct xrefinfo * xi;
	phandle_t child, xref;

	/*
	 * Recursively descend from parent, looking for nodes with a property
	 * named either "phandle", "ibm,phandle", or "linux,phandle".  For each
	 * such node found create an entry in the xreflist.
	 */
	for (child = OF_child(node); child != 0; child = OF_peer(child)) {
		xrefinfo_create(child);
		if (OF_getencprop(child, "phandle", &xref, sizeof(xref)) ==
		    -1 && OF_getencprop(child, "ibm,phandle", &xref,
		    sizeof(xref)) == -1 && OF_getencprop(child,
		    "linux,phandle", &xref, sizeof(xref)) == -1)
			continue;
		xi = malloc(sizeof(*xi), M_OFWPROP, M_WAITOK | M_ZERO);
		xi->node = child;
		xi->xref = xref;
		SLIST_INSERT_HEAD(&xreflist, xi, next_entry);
	}
}

static void
xrefinfo_init(void *unsed)
{

	/*
	 * There is no locking during this init because it runs much earlier
	 * than any of the clients/consumers of the xref list data, but we do
	 * initialize the mutex that will be used for access later.
	 */
	mtx_init(&xreflist_lock, "OF xreflist lock", NULL, MTX_DEF);
	xrefinfo_create(OF_peer(0));
	xref_init_done = true;
}
SYSINIT(xrefinfo, SI_SUB_KMEM, SI_ORDER_ANY, xrefinfo_init, NULL);

static struct xrefinfo *
xrefinfo_find(uintptr_t key, int find_by)
{
	struct xrefinfo *rv, *xi;

	rv = NULL;
	mtx_lock(&xreflist_lock);
	SLIST_FOREACH(xi, &xreflist, next_entry) {
		if ((find_by == FIND_BY_XREF && (phandle_t)key == xi->xref) ||
		    (find_by == FIND_BY_NODE && (phandle_t)key == xi->node) ||
		    (find_by == FIND_BY_DEV && key == (uintptr_t)xi->dev)) {
			rv = xi;
			break;
		}
	}
	mtx_unlock(&xreflist_lock);
	return (rv);
}

static struct xrefinfo *
xrefinfo_add(phandle_t node, phandle_t xref, device_t dev)
{
	struct xrefinfo *xi;

	xi = malloc(sizeof(*xi), M_OFWPROP, M_WAITOK);
	xi->node = node;
	xi->xref = xref;
	xi->dev  = dev;
	mtx_lock(&xreflist_lock);
	SLIST_INSERT_HEAD(&xreflist, xi, next_entry);
	mtx_unlock(&xreflist_lock);
	return (xi);
}

/*
 * OFW install routines.  Highest priority wins, equal priority also
 * overrides allowing last-set to win.
 */
SET_DECLARE(ofw_set, ofw_def_t);

boolean_t
OF_install(char *name, int prio)
{
	ofw_def_t *ofwp, **ofwpp;
	static int curr_prio = 0;

	/* Allow OF layer to be uninstalled */
	if (name == NULL) {
		ofw_def_impl = NULL;
		return (FALSE);
	}

	/*
	 * Try and locate the OFW kobj corresponding to the name.
	 */
	SET_FOREACH(ofwpp, ofw_set) {
		ofwp = *ofwpp;

		if (ofwp->name &&
		    !strcmp(ofwp->name, name) &&
		    prio >= curr_prio) {
			curr_prio = prio;
			ofw_def_impl = ofwp;
			return (TRUE);
		}
	}

	return (FALSE);
}

/* Initializer */
int
OF_init(void *cookie)
{
	phandle_t chosen;
	int rv;

	if (ofw_def_impl == NULL)
		return (-1);

	ofw_obj = &ofw_kernel_obj;
	/*
	 * Take care of compiling the selected class, and
	 * then statically initialize the OFW object.
	 */
	kobj_class_compile_static(ofw_def_impl, &ofw_kernel_kops);
	kobj_init_static((kobj_t)ofw_obj, ofw_def_impl);

	rv = OFW_INIT(ofw_obj, cookie);

	if ((chosen = OF_finddevice("/chosen")) != -1)
		if (OF_getencprop(chosen, "stdout", &stdout,
		    sizeof(stdout)) == -1)
			stdout = -1;

	return (rv);
}

static void
OF_putchar(int c, void *arg __unused)
{
	char cbuf;

	if (c == '\n') {
		cbuf = '\r';
		OF_write(stdout, &cbuf, 1);
	}

	cbuf = c;
	OF_write(stdout, &cbuf, 1);
}

void
OF_printf(const char *fmt, ...)
{
	va_list	va;

	va_start(va, fmt);
	(void)kvprintf(fmt, OF_putchar, NULL, 10, va);
	va_end(va);
}

/*
 * Generic functions
 */

/* Test to see if a service exists. */
int
OF_test(const char *name)
{

	if (ofw_def_impl == NULL)
		return (-1);

	return (OFW_TEST(ofw_obj, name));
}

int
OF_interpret(const char *cmd, int nreturns, ...)
{
	va_list ap;
	cell_t slots[16];
	int i = 0;
	int status;

	if (ofw_def_impl == NULL)
		return (-1);

	status = OFW_INTERPRET(ofw_obj, cmd, nreturns, slots);
	if (status == -1)
		return (status);

	va_start(ap, nreturns);
	while (i < nreturns)
		*va_arg(ap, cell_t *) = slots[i++];
	va_end(ap);

	return (status);
}

/*
 * Device tree functions
 */

/* Return the next sibling of this node or 0. */
phandle_t
OF_peer(phandle_t node)
{

	if (ofw_def_impl == NULL)
		return (0);

	return (OFW_PEER(ofw_obj, node));
}

/* Return the first child of this node or 0. */
phandle_t
OF_child(phandle_t node)
{

	if (ofw_def_impl == NULL)
		return (0);

	return (OFW_CHILD(ofw_obj, node));
}

/* Return the parent of this node or 0. */
phandle_t
OF_parent(phandle_t node)
{

	if (ofw_def_impl == NULL)
		return (0);

	return (OFW_PARENT(ofw_obj, node));
}

/* Return the package handle that corresponds to an instance handle. */
phandle_t
OF_instance_to_package(ihandle_t instance)
{

	if (ofw_def_impl == NULL)
		return (-1);

	return (OFW_INSTANCE_TO_PACKAGE(ofw_obj, instance));
}

/* Get the length of a property of a package. */
ssize_t
OF_getproplen(phandle_t package, const char *propname)
{

	if (ofw_def_impl == NULL)
		return (-1);

	return (OFW_GETPROPLEN(ofw_obj, package, propname));
}

/* Check existence of a property of a package. */
int
OF_hasprop(phandle_t package, const char *propname)
{

	return (OF_getproplen(package, propname) >= 0 ? 1 : 0);
}

/* Get the value of a property of a package. */
ssize_t
OF_getprop(phandle_t package, const char *propname, void *buf, size_t buflen)
{

	if (ofw_def_impl == NULL)
		return (-1);

	return (OFW_GETPROP(ofw_obj, package, propname, buf, buflen));
}

ssize_t
OF_getencprop(phandle_t node, const char *propname, pcell_t *buf, size_t len)
{
	ssize_t retval;
	int i;

	KASSERT(len % 4 == 0, ("Need a multiple of 4 bytes"));

	retval = OF_getprop(node, propname, buf, len);
	if (retval <= 0)
		return (retval);

	for (i = 0; i < len/4; i++)
		buf[i] = be32toh(buf[i]);

	return (retval);
}

/*
 * Recursively search the node and its parent for the given property, working
 * downward from the node to the device tree root.  Returns the value of the
 * first match.
 */
ssize_t
OF_searchprop(phandle_t node, const char *propname, void *buf, size_t len)
{
	ssize_t rv;

	for (; node != 0; node = OF_parent(node))
		if ((rv = OF_getprop(node, propname, buf, len)) != -1)
			return (rv);
	return (-1);
}

ssize_t
OF_searchencprop(phandle_t node, const char *propname, pcell_t *buf, size_t len)
{
	ssize_t rv;

	for (; node != 0; node = OF_parent(node))
		if ((rv = OF_getencprop(node, propname, buf, len)) != -1)
			return (rv);
	return (-1);
}

/*
 * Store the value of a property of a package into newly allocated memory
 * (using the M_OFWPROP malloc pool and M_WAITOK).
 */
ssize_t
OF_getprop_alloc(phandle_t package, const char *propname, void **buf)
{
	int len;

	*buf = NULL;
	if ((len = OF_getproplen(package, propname)) == -1)
		return (-1);

	if (len > 0) {
		*buf = malloc(len, M_OFWPROP, M_WAITOK);
		if (OF_getprop(package, propname, *buf, len) == -1) {
			free(*buf, M_OFWPROP);
			*buf = NULL;
			return (-1);
		}
	}
	return (len);
}

/*
 * Store the value of a property of a package into newly allocated memory
 * (using the M_OFWPROP malloc pool and M_WAITOK).  elsz is the size of a
 * single element, the number of elements is return in number.
 */
ssize_t
OF_getprop_alloc_multi(phandle_t package, const char *propname, int elsz, void **buf)
{
	int len;

	*buf = NULL;
	if ((len = OF_getproplen(package, propname)) == -1 ||
	    len % elsz != 0)
		return (-1);

	if (len > 0) {
		*buf = malloc(len, M_OFWPROP, M_WAITOK);
		if (OF_getprop(package, propname, *buf, len) == -1) {
			free(*buf, M_OFWPROP);
			*buf = NULL;
			return (-1);
		}
	}
	return (len / elsz);
}

ssize_t
OF_getencprop_alloc(phandle_t package, const char *name, void **buf)
{
	ssize_t ret;

	ret = OF_getencprop_alloc_multi(package, name, sizeof(pcell_t),
	    buf);
	if (ret < 0)
		return (ret);
	else
		return (ret * sizeof(pcell_t));
}

ssize_t
OF_getencprop_alloc_multi(phandle_t package, const char *name, int elsz,
    void **buf)
{
	ssize_t retval;
	pcell_t *cell;
	int i;

	retval = OF_getprop_alloc_multi(package, name, elsz, buf);
	if (retval == -1)
		return (-1);

	cell = *buf;
	for (i = 0; i < retval * elsz / 4; i++)
		cell[i] = be32toh(cell[i]);

	return (retval);
}

/* Free buffer allocated by OF_getencprop_alloc or OF_getprop_alloc */
void OF_prop_free(void *buf)
{

	free(buf, M_OFWPROP);
}

/* Get the next property of a package. */
int
OF_nextprop(phandle_t package, const char *previous, char *buf, size_t size)
{

	if (ofw_def_impl == NULL)
		return (-1);

	return (OFW_NEXTPROP(ofw_obj, package, previous, buf, size));
}

/* Set the value of a property of a package. */
int
OF_setprop(phandle_t package, const char *propname, const void *buf, size_t len)
{

	if (ofw_def_impl == NULL)
		return (-1);

	return (OFW_SETPROP(ofw_obj, package, propname, buf,len));
}

/* Convert a device specifier to a fully qualified pathname. */
ssize_t
OF_canon(const char *device, char *buf, size_t len)
{

	if (ofw_def_impl == NULL)
		return (-1);

	return (OFW_CANON(ofw_obj, device, buf, len));
}

/* Return a package handle for the specified device. */
phandle_t
OF_finddevice(const char *device)
{

	if (ofw_def_impl == NULL)
		return (-1);

	return (OFW_FINDDEVICE(ofw_obj, device));
}

/* Return the fully qualified pathname corresponding to an instance. */
ssize_t
OF_instance_to_path(ihandle_t instance, char *buf, size_t len)
{

	if (ofw_def_impl == NULL)
		return (-1);

	return (OFW_INSTANCE_TO_PATH(ofw_obj, instance, buf, len));
}

/* Return the fully qualified pathname corresponding to a package. */
ssize_t
OF_package_to_path(phandle_t package, char *buf, size_t len)
{

	if (ofw_def_impl == NULL)
		return (-1);

	return (OFW_PACKAGE_TO_PATH(ofw_obj, package, buf, len));
}

/* Look up effective phandle (see FDT/PAPR spec) */
static phandle_t
OF_child_xref_phandle(phandle_t parent, phandle_t xref)
{
	phandle_t child, rxref;

	/*
	 * Recursively descend from parent, looking for a node with a property
	 * named either "phandle", "ibm,phandle", or "linux,phandle" that
	 * matches the xref we are looking for.
	 */

	for (child = OF_child(parent); child != 0; child = OF_peer(child)) {
		rxref = OF_child_xref_phandle(child, xref);
		if (rxref != -1)
			return (rxref);

		if (OF_getencprop(child, "phandle", &rxref, sizeof(rxref)) ==
		    -1 && OF_getencprop(child, "ibm,phandle", &rxref,
		    sizeof(rxref)) == -1 && OF_getencprop(child,
		    "linux,phandle", &rxref, sizeof(rxref)) == -1)
			continue;

		if (rxref == xref)
			return (child);
	}

	return (-1);
}

phandle_t
OF_node_from_xref(phandle_t xref)
{
	struct xrefinfo *xi;
	phandle_t node;

	if (xref_init_done) {
		if ((xi = xrefinfo_find(xref, FIND_BY_XREF)) == NULL)
			return (xref);
		return (xi->node);
	}

	if ((node = OF_child_xref_phandle(OF_peer(0), xref)) == -1)
		return (xref);
	return (node);
}

phandle_t
OF_xref_from_node(phandle_t node)
{
	struct xrefinfo *xi;
	phandle_t xref;

	if (xref_init_done) {
		if ((xi = xrefinfo_find(node, FIND_BY_NODE)) == NULL)
			return (node);
		return (xi->xref);
	}

	if (OF_getencprop(node, "phandle", &xref, sizeof(xref)) == -1 &&
	    OF_getencprop(node, "ibm,phandle", &xref, sizeof(xref)) == -1 &&
	    OF_getencprop(node, "linux,phandle", &xref, sizeof(xref)) == -1)
		return (node);
	return (xref);
}

device_t
OF_device_from_xref(phandle_t xref)
{
	struct xrefinfo *xi;

	if (xref_init_done) {
		if ((xi = xrefinfo_find(xref, FIND_BY_XREF)) == NULL)
			return (NULL);
		return (xi->dev);
	}
	panic("Attempt to find device before xreflist_init");
}

phandle_t
OF_xref_from_device(device_t dev)
{
	struct xrefinfo *xi;

	if (xref_init_done) {
		if ((xi = xrefinfo_find((uintptr_t)dev, FIND_BY_DEV)) == NULL)
			return (0);
		return (xi->xref);
	}
	panic("Attempt to find xref before xreflist_init");
}

int
OF_device_register_xref(phandle_t xref, device_t dev)
{
	struct xrefinfo *xi;

	/*
	 * If the given xref handle doesn't already exist in the list then we
	 * add a list entry.  In theory this can only happen on a system where
	 * nodes don't contain phandle properties and xref and node handles are
	 * synonymous, so the xref handle is added as the node handle as well.
	 */
	if (xref_init_done) {
		if ((xi = xrefinfo_find(xref, FIND_BY_XREF)) == NULL)
			xrefinfo_add(xref, xref, dev);
		else 
			xi->dev = dev;
		return (0);
	}
	panic("Attempt to register device before xreflist_init");
}

/*  Call the method in the scope of a given instance. */
int
OF_call_method(const char *method, ihandle_t instance, int nargs, int nreturns,
    ...)
{
	va_list ap;
	cell_t args_n_results[12];
	int n, status;

	if (nargs > 6 || ofw_def_impl == NULL)
		return (-1);
	va_start(ap, nreturns);
	for (n = 0; n < nargs; n++)
		args_n_results[n] = va_arg(ap, cell_t);

	status = OFW_CALL_METHOD(ofw_obj, instance, method, nargs, nreturns,
	    args_n_results);
	if (status != 0)
		return (status);

	for (; n < nargs + nreturns; n++)
		*va_arg(ap, cell_t *) = args_n_results[n];
	va_end(ap);
	return (0);
}

/*
 * Device I/O functions
 */

/* Open an instance for a device. */
ihandle_t
OF_open(const char *device)
{

	if (ofw_def_impl == NULL)
		return (0);

	return (OFW_OPEN(ofw_obj, device));
}

/* Close an instance. */
void
OF_close(ihandle_t instance)
{

	if (ofw_def_impl == NULL)
		return;

	OFW_CLOSE(ofw_obj, instance);
}

/* Read from an instance. */
ssize_t
OF_read(ihandle_t instance, void *addr, size_t len)
{

	if (ofw_def_impl == NULL)
		return (-1);

	return (OFW_READ(ofw_obj, instance, addr, len));
}

/* Write to an instance. */
ssize_t
OF_write(ihandle_t instance, const void *addr, size_t len)
{

	if (ofw_def_impl == NULL)
		return (-1);

	return (OFW_WRITE(ofw_obj, instance, addr, len));
}

/* Seek to a position. */
int
OF_seek(ihandle_t instance, uint64_t pos)
{

	if (ofw_def_impl == NULL)
		return (-1);

	return (OFW_SEEK(ofw_obj, instance, pos));
}

/*
 * Memory functions
 */

/* Claim an area of memory. */
void *
OF_claim(void *virt, size_t size, u_int align)
{

	if (ofw_def_impl == NULL)
		return ((void *)-1);

	return (OFW_CLAIM(ofw_obj, virt, size, align));
}

/* Release an area of memory. */
void
OF_release(void *virt, size_t size)
{

	if (ofw_def_impl == NULL)
		return;

	OFW_RELEASE(ofw_obj, virt, size);
}

/*
 * Control transfer functions
 */

/* Suspend and drop back to the Open Firmware interface. */
void
OF_enter()
{

	if (ofw_def_impl == NULL)
		return;

	OFW_ENTER(ofw_obj);
}

/* Shut down and drop back to the Open Firmware interface. */
void
OF_exit()
{

	if (ofw_def_impl == NULL)
		panic("OF_exit: Open Firmware not available");

	/* Should not return */
	OFW_EXIT(ofw_obj);

	for (;;)			/* just in case */
		;
}
