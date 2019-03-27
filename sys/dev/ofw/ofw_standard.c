/*	$NetBSD: Locore.c,v 1.7 2000/08/20 07:04:59 tsubai Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-4-Clause AND BSD-2-Clause-FreeBSD
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

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#include <machine/stdarg.h>

#include <dev/ofw/ofwvar.h>
#include <dev/ofw/openfirm.h>

#include "ofw_if.h"

static int ofw_std_init(ofw_t ofw, void *openfirm);
static int ofw_std_test(ofw_t ofw, const char *name);
static int ofw_std_interpret(ofw_t ofw, const char *cmd, int nreturns,
    cell_t *returns);
static phandle_t ofw_std_peer(ofw_t ofw, phandle_t node);
static phandle_t ofw_std_child(ofw_t ofw, phandle_t node);
static phandle_t ofw_std_parent(ofw_t ofw, phandle_t node);
static phandle_t ofw_std_instance_to_package(ofw_t ofw, ihandle_t instance);
static ssize_t ofw_std_getproplen(ofw_t ofw, phandle_t package,
    const char *propname);
static ssize_t ofw_std_getprop(ofw_t ofw, phandle_t package,
    const char *propname, void *buf, size_t buflen);
static int ofw_std_nextprop(ofw_t ofw, phandle_t package, const char *previous,
    char *buf, size_t);
static int ofw_std_setprop(ofw_t ofw, phandle_t package, const char *propname,
    const void *buf, size_t len);
static ssize_t ofw_std_canon(ofw_t ofw, const char *device, char *buf,
    size_t len);
static phandle_t ofw_std_finddevice(ofw_t ofw, const char *device);
static ssize_t ofw_std_instance_to_path(ofw_t ofw, ihandle_t instance,
    char *buf, size_t len);
static ssize_t ofw_std_package_to_path(ofw_t ofw, phandle_t package, char *buf,
    size_t len);
static int ofw_std_call_method(ofw_t ofw, ihandle_t instance,
    const char *method, int nargs, int nreturns, cell_t *args_and_returns);
static ihandle_t ofw_std_open(ofw_t ofw, const char *device);
static void ofw_std_close(ofw_t ofw, ihandle_t instance);
static ssize_t ofw_std_read(ofw_t ofw, ihandle_t instance, void *addr,
    size_t len);
static ssize_t ofw_std_write(ofw_t ofw, ihandle_t instance, const void *addr,
    size_t len);
static int ofw_std_seek(ofw_t ofw, ihandle_t instance, uint64_t pos);
static caddr_t ofw_std_claim(ofw_t ofw, void *virt, size_t size, u_int align);
static void ofw_std_release(ofw_t ofw, void *virt, size_t size);
static void ofw_std_enter(ofw_t ofw);
static void ofw_std_exit(ofw_t ofw);

static ofw_method_t ofw_std_methods[] = {
	OFWMETHOD(ofw_init,			ofw_std_init),
	OFWMETHOD(ofw_peer,			ofw_std_peer),
	OFWMETHOD(ofw_child,			ofw_std_child),
	OFWMETHOD(ofw_parent,			ofw_std_parent),
	OFWMETHOD(ofw_instance_to_package,	ofw_std_instance_to_package),
	OFWMETHOD(ofw_getproplen,		ofw_std_getproplen),
	OFWMETHOD(ofw_getprop,			ofw_std_getprop),
	OFWMETHOD(ofw_nextprop,			ofw_std_nextprop),
	OFWMETHOD(ofw_setprop,			ofw_std_setprop),
	OFWMETHOD(ofw_canon,			ofw_std_canon),
	OFWMETHOD(ofw_finddevice,		ofw_std_finddevice),
	OFWMETHOD(ofw_instance_to_path,		ofw_std_instance_to_path),
	OFWMETHOD(ofw_package_to_path,		ofw_std_package_to_path),

	OFWMETHOD(ofw_test,			ofw_std_test),
	OFWMETHOD(ofw_call_method,		ofw_std_call_method),
	OFWMETHOD(ofw_interpret,		ofw_std_interpret),
	OFWMETHOD(ofw_open,			ofw_std_open),
	OFWMETHOD(ofw_close,			ofw_std_close),
	OFWMETHOD(ofw_read,			ofw_std_read),
	OFWMETHOD(ofw_write,			ofw_std_write),
	OFWMETHOD(ofw_seek,			ofw_std_seek),
	OFWMETHOD(ofw_claim,			ofw_std_claim),
	OFWMETHOD(ofw_release,			ofw_std_release),
	OFWMETHOD(ofw_enter,			ofw_std_enter),
	OFWMETHOD(ofw_exit,			ofw_std_exit),

	{ 0, 0 }
};

static ofw_def_t ofw_std = {
	OFW_STD_DIRECT,
	ofw_std_methods,
	0
};
OFW_DEF(ofw_std);

static int (*openfirmware)(void *);

/* Initializer */

static int
ofw_std_init(ofw_t ofw, void *openfirm)
{

	openfirmware = (int (*)(void *))openfirm;
	return (0);
}

/*
 * Generic functions
 */

/* Test to see if a service exists. */
static int
ofw_std_test(ofw_t ofw, const char *name)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t service;
		cell_t missing;
	} args = {
		(cell_t)"test",
		1,
		1,
	};

	args.service = (cell_t)name;
	if (openfirmware(&args) == -1)
		return (-1);
	return (args.missing);
}

static int
ofw_std_interpret(ofw_t ofw, const char *cmd, int nreturns, cell_t *returns)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t slot[16];
	} args = {
		(cell_t)"interpret",
		1,
	};
	cell_t status;
	int i = 0, j = 0;

	args.nreturns = ++nreturns;
	args.slot[i++] = (cell_t)cmd;
	if (openfirmware(&args) == -1)
		return (-1);
	status = args.slot[i++];
	while (i < 1 + nreturns)
		returns[j++] = args.slot[i++];
	return (status);
}

/*
 * Device tree functions
 */

/* Return the next sibling of this node or 0. */
static phandle_t
ofw_std_peer(ofw_t ofw, phandle_t node)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t node;
		cell_t next;
	} args = {
		(cell_t)"peer",
		1,
		1,
	};

	args.node = node;
	if (openfirmware(&args) == -1)
		return (0);
	return (args.next);
}

/* Return the first child of this node or 0. */
static phandle_t
ofw_std_child(ofw_t ofw, phandle_t node)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t node;
		cell_t child;
	} args = {
		(cell_t)"child",
		1,
		1,
	};

	args.node = node;
	if (openfirmware(&args) == -1)
		return (0);
	return (args.child);
}

/* Return the parent of this node or 0. */
static phandle_t
ofw_std_parent(ofw_t ofw, phandle_t node)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t node;
		cell_t parent;
	} args = {
		(cell_t)"parent",
		1,
		1,
	};

	args.node = node;
	if (openfirmware(&args) == -1)
		return (0);
	return (args.parent);
}

/* Return the package handle that corresponds to an instance handle. */
static phandle_t
ofw_std_instance_to_package(ofw_t ofw, ihandle_t instance)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t instance;
		cell_t package;
	} args = {
		(cell_t)"instance-to-package",
		1,
		1,
	};

	args.instance = instance;
	if (openfirmware(&args) == -1)
		return (-1);
	return (args.package);
}

/* Get the length of a property of a package. */
static ssize_t
ofw_std_getproplen(ofw_t ofw, phandle_t package, const char *propname)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t package;
		cell_t propname;
		cell_t proplen;
	} args = {
		(cell_t)"getproplen",
		2,
		1,
	};

	args.package = package;
	args.propname = (cell_t)propname;
	if (openfirmware(&args) == -1)
		return (-1);
	return (args.proplen);
}

/* Get the value of a property of a package. */
static ssize_t
ofw_std_getprop(ofw_t ofw, phandle_t package, const char *propname, void *buf,
    size_t buflen)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t package;
		cell_t propname;
		cell_t buf;
		cell_t buflen;
		cell_t size;
	} args = {
		(cell_t)"getprop",
		4,
		1,
	};

	args.package = package;
	args.propname = (cell_t)propname;
	args.buf = (cell_t)buf;
	args.buflen = buflen;
	if (openfirmware(&args) == -1)
		return (-1);
	return (args.size);
}

/* Get the next property of a package. */
static int
ofw_std_nextprop(ofw_t ofw, phandle_t package, const char *previous, char *buf,
    size_t size)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t package;
		cell_t previous;
		cell_t buf;
		cell_t flag;
	} args = {
		(cell_t)"nextprop",
		3,
		1,
	};

	args.package = package;
	args.previous = (cell_t)previous;
	args.buf = (cell_t)buf;
	if (openfirmware(&args) == -1)
		return (-1);
	return (args.flag);
}

/* Set the value of a property of a package. */
/* XXX Has a bug on FirePower */
static int
ofw_std_setprop(ofw_t ofw, phandle_t package, const char *propname,
    const void *buf, size_t len)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t package;
		cell_t propname;
		cell_t buf;
		cell_t len;
		cell_t size;
	} args = {
		(cell_t)"setprop",
		4,
		1,
	};

	args.package = package;
	args.propname = (cell_t)propname;
	args.buf = (cell_t)buf;
	args.len = len;
	if (openfirmware(&args) == -1)
		return (-1);
	return (args.size);
}

/* Convert a device specifier to a fully qualified pathname. */
static ssize_t
ofw_std_canon(ofw_t ofw, const char *device, char *buf, size_t len)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t device;
		cell_t buf;
		cell_t len;
		cell_t size;
	} args = {
		(cell_t)"canon",
		3,
		1,
	};

	args.device = (cell_t)device;
	args.buf = (cell_t)buf;
	args.len = len;
	if (openfirmware(&args) == -1)
		return (-1);
	return (args.size);
}

/* Return a package handle for the specified device. */
static phandle_t
ofw_std_finddevice(ofw_t ofw, const char *device)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t device;
		cell_t package;
	} args = {
		(cell_t)"finddevice",
		1,
		1,
	};

	args.device = (cell_t)device;
	if (openfirmware(&args) == -1)
		return (-1);
	return (args.package);
}

/* Return the fully qualified pathname corresponding to an instance. */
static ssize_t
ofw_std_instance_to_path(ofw_t ofw, ihandle_t instance, char *buf, size_t len)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t instance;
		cell_t buf;
		cell_t len;
		cell_t size;
	} args = {
		(cell_t)"instance-to-path",
		3,
		1,
	};

	args.instance = instance;
	args.buf = (cell_t)buf;
	args.len = len;
	if (openfirmware(&args) == -1)
		return (-1);
	return (args.size);
}

/* Return the fully qualified pathname corresponding to a package. */
static ssize_t
ofw_std_package_to_path(ofw_t ofw, phandle_t package, char *buf, size_t len)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t package;
		cell_t buf;
		cell_t len;
		cell_t size;
	} args = {
		(cell_t)"package-to-path",
		3,
		1,
	};

	args.package = package;
	args.buf = (cell_t)buf;
	args.len = len;
	if (openfirmware(&args) == -1)
		return (-1);
	return (args.size);
}

/*  Call the method in the scope of a given instance. */
static int
ofw_std_call_method(ofw_t ofw, ihandle_t instance, const char *method,
    int nargs, int nreturns, cell_t *args_and_returns)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t method;
		cell_t instance;
		cell_t args_n_results[12];
	} args = {
		(cell_t)"call-method",
		2,
		1,
	};
	cell_t *ap, *cp;
	int n;

	if (nargs > 6)
		return (-1);
	args.nargs = nargs + 2;
	args.nreturns = nreturns + 1;
	args.method = (cell_t)method;
	args.instance = instance;

	ap = args_and_returns;
	for (cp = args.args_n_results + (n = nargs); --n >= 0;)
		*--cp = *(ap++);
	if (openfirmware(&args) == -1)
		return (-1);
	if (args.args_n_results[nargs])
		return (args.args_n_results[nargs]);
	for (cp = args.args_n_results + nargs + (n = args.nreturns); --n > 0;)
		*(ap++) = *--cp;
	return (0);
}

/*
 * Device I/O functions
 */

/* Open an instance for a device. */
static ihandle_t
ofw_std_open(ofw_t ofw, const char *device)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t device;
		cell_t instance;
	} args = {
		(cell_t)"open",
		1,
		1,
	};

	args.device = (cell_t)device;
	if (openfirmware(&args) == -1 || args.instance == 0)
		return (-1);
	return (args.instance);
}

/* Close an instance. */
static void
ofw_std_close(ofw_t ofw, ihandle_t instance)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t instance;
	} args = {
		(cell_t)"close",
		1,
		0,
	};

	args.instance = instance;
	openfirmware(&args);
}

/* Read from an instance. */
static ssize_t
ofw_std_read(ofw_t ofw, ihandle_t instance, void *addr, size_t len)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t instance;
		cell_t addr;
		cell_t len;
		cell_t actual;
	} args = {
		(cell_t)"read",
		3,
		1,
	};

	args.instance = instance;
	args.addr = (cell_t)addr;
	args.len = len;
	if (openfirmware(&args) == -1)
		return (-1);

	return (args.actual);
}

/* Write to an instance. */
static ssize_t
ofw_std_write(ofw_t ofw, ihandle_t instance, const void *addr, size_t len)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t instance;
		cell_t addr;
		cell_t len;
		cell_t actual;
	} args = {
		(cell_t)"write",
		3,
		1,
	};

	args.instance = instance;
	args.addr = (cell_t)addr;
	args.len = len;
	if (openfirmware(&args) == -1)
		return (-1);
	return (args.actual);
}

/* Seek to a position. */
static int
ofw_std_seek(ofw_t ofw, ihandle_t instance, uint64_t pos)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t instance;
		cell_t poshi;
		cell_t poslo;
		cell_t status;
	} args = {
		(cell_t)"seek",
		3,
		1,
	};

	args.instance = instance;
	args.poshi = pos >> 32;
	args.poslo = pos;
	if (openfirmware(&args) == -1)
		return (-1);
	return (args.status);
}

/*
 * Memory functions
 */

/* Claim an area of memory. */
static caddr_t
ofw_std_claim(ofw_t ofw, void *virt, size_t size, u_int align)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t virt;
		cell_t size;
		cell_t align;
		cell_t baseaddr;
	} args = {
		(cell_t)"claim",
		3,
		1,
	};

	args.virt = (cell_t)virt;
	args.size = size;
	args.align = align;
	if (openfirmware(&args) == -1)
		return ((void *)-1);
	return ((void *)args.baseaddr);
}

/* Release an area of memory. */
static void
ofw_std_release(ofw_t ofw, void *virt, size_t size)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t virt;
		cell_t size;
	} args = {
		(cell_t)"release",
		2,
		0,
	};

	args.virt = (cell_t)virt;
	args.size = size;
	openfirmware(&args);
}

/*
 * Control transfer functions
 */

/* Suspend and drop back to the Open Firmware interface. */
static void
ofw_std_enter(ofw_t ofw)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
	} args = {
		(cell_t)"enter",
		0,
		0,
	};

	openfirmware(&args);
	/* We may come back. */
}

/* Shut down and drop back to the Open Firmware interface. */
static void
ofw_std_exit(ofw_t ofw)
{
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
	} args = {
		(cell_t)"exit",
		0,
		0,
	};

	openfirmware(&args);
	for (;;)			/* just in case */
		;
}
