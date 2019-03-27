/*	$NetBSD: Locore.c,v 1.7 2000/08/20 07:04:59 tsubai Exp $	*/

/*-
 * SPDX-License-Identifier:BSD-4-Clause AND BSD-2-Clause-FreeBSD
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
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/systm.h>

#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/pmap.h>

#include <machine/bus.h>
#include <machine/md_var.h>
#include <machine/ofw_machdep.h>
#include <machine/stdarg.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofwvar.h>
#include "ofw_if.h"

static int ofw_real_init(ofw_t, void *openfirm);
static int ofw_real_test(ofw_t, const char *name);
static phandle_t ofw_real_peer(ofw_t, phandle_t node);
static phandle_t ofw_real_child(ofw_t, phandle_t node);
static phandle_t ofw_real_parent(ofw_t, phandle_t node);
static phandle_t ofw_real_instance_to_package(ofw_t, ihandle_t instance);
static ssize_t ofw_real_getproplen(ofw_t, phandle_t package, 
    const char *propname);
static ssize_t ofw_real_getprop(ofw_t, phandle_t package, const char *propname, 
    void *buf, size_t buflen);
static int ofw_real_nextprop(ofw_t, phandle_t package, const char *previous, 
    char *buf, size_t);
static int ofw_real_setprop(ofw_t, phandle_t package, const char *propname,
    const void *buf, size_t len);
static ssize_t ofw_real_canon(ofw_t, const char *device, char *buf, size_t len);
static phandle_t ofw_real_finddevice(ofw_t, const char *device);
static ssize_t ofw_real_instance_to_path(ofw_t, ihandle_t instance, char *buf, 
    size_t len);
static ssize_t ofw_real_package_to_path(ofw_t, phandle_t package, char *buf, 
    size_t len);
static int ofw_real_call_method(ofw_t, ihandle_t instance, const char *method, 
    int nargs, int nreturns, cell_t *args_and_returns);
static int ofw_real_interpret(ofw_t ofw, const char *cmd, int nreturns,
    cell_t *returns);
static ihandle_t ofw_real_open(ofw_t, const char *device);
static void ofw_real_close(ofw_t, ihandle_t instance);
static ssize_t ofw_real_read(ofw_t, ihandle_t instance, void *addr, size_t len);
static ssize_t ofw_real_write(ofw_t, ihandle_t instance, const void *addr, 
    size_t len);
static int ofw_real_seek(ofw_t, ihandle_t instance, u_int64_t pos);
static caddr_t ofw_real_claim(ofw_t, void *virt, size_t size, u_int align);
static void ofw_real_release(ofw_t, void *virt, size_t size);
static void ofw_real_enter(ofw_t);
static void ofw_real_exit(ofw_t);

static ofw_method_t ofw_real_methods[] = {
	OFWMETHOD(ofw_init,			ofw_real_init),
	OFWMETHOD(ofw_peer,			ofw_real_peer),
	OFWMETHOD(ofw_child,			ofw_real_child),
	OFWMETHOD(ofw_parent,			ofw_real_parent),
	OFWMETHOD(ofw_instance_to_package,	ofw_real_instance_to_package),
	OFWMETHOD(ofw_getproplen,		ofw_real_getproplen),
	OFWMETHOD(ofw_getprop,			ofw_real_getprop),
	OFWMETHOD(ofw_nextprop,			ofw_real_nextprop),
	OFWMETHOD(ofw_setprop,			ofw_real_setprop),
	OFWMETHOD(ofw_canon,			ofw_real_canon),
	OFWMETHOD(ofw_finddevice,		ofw_real_finddevice),
	OFWMETHOD(ofw_instance_to_path,		ofw_real_instance_to_path),
	OFWMETHOD(ofw_package_to_path,		ofw_real_package_to_path),

	OFWMETHOD(ofw_test,			ofw_real_test),
	OFWMETHOD(ofw_call_method,		ofw_real_call_method),
	OFWMETHOD(ofw_interpret,		ofw_real_interpret),
	OFWMETHOD(ofw_open,			ofw_real_open),
	OFWMETHOD(ofw_close,			ofw_real_close),
	OFWMETHOD(ofw_read,			ofw_real_read),
	OFWMETHOD(ofw_write,			ofw_real_write),
	OFWMETHOD(ofw_seek,			ofw_real_seek),
	OFWMETHOD(ofw_claim,			ofw_real_claim),
	OFWMETHOD(ofw_release,			ofw_real_release),
	OFWMETHOD(ofw_enter,			ofw_real_enter),
	OFWMETHOD(ofw_exit,			ofw_real_exit),

	{ 0, 0 }
};

static ofw_def_t ofw_real = {
	OFW_STD_REAL,
	ofw_real_methods,
	0
};
OFW_DEF(ofw_real);

static ofw_def_t ofw_32bit = {
	OFW_STD_32BIT,
	ofw_real_methods,
	0
};
OFW_DEF(ofw_32bit);

static MALLOC_DEFINE(M_OFWREAL, "ofwreal",
    "Open Firmware Real Mode Bounce Page");

static int (*openfirmware)(void *);

static vm_offset_t	of_bounce_phys;
static caddr_t		of_bounce_virt;
static off_t		of_bounce_offset;
static size_t		of_bounce_size;
static struct mtx	of_bounce_mtx;

extern int		ofw_real_mode;

/*
 * After the VM is up, allocate a wired, low memory bounce page.
 */

static void ofw_real_bounce_alloc(void *);

SYSINIT(ofw_real_bounce_alloc, SI_SUB_KMEM, SI_ORDER_ANY, 
    ofw_real_bounce_alloc, NULL);

static void
ofw_real_start(void)
{
	mtx_lock(&of_bounce_mtx);
	of_bounce_offset = 0;
}
	
static void
ofw_real_stop(void)
{
	mtx_unlock(&of_bounce_mtx);
}

static void
ofw_real_bounce_alloc(void *junk)
{
	caddr_t temp;

	/*
	 * Check that ofw_real is actually in use before allocating wads 
	 * of memory. Do this by checking if our mutex has been set up.
	 */
	if (!mtx_initialized(&of_bounce_mtx))
		return;

	/*
	 * Allocate a page of contiguous, wired physical memory that can
	 * fit into a 32-bit address space and accessed from real mode.
	 */
	temp = contigmalloc(4 * PAGE_SIZE, M_OFWREAL, 0, 0,
	    ulmin(platform_real_maxaddr(), BUS_SPACE_MAXADDR_32BIT), PAGE_SIZE,
	    4 * PAGE_SIZE);
	if (temp == NULL)
		panic("%s: Not able to allocated contiguous memory\n", __func__);

	mtx_lock(&of_bounce_mtx);

	of_bounce_virt = temp;

	of_bounce_phys = vtophys(of_bounce_virt);
	of_bounce_size = 4 * PAGE_SIZE;

	/*
	 * For virtual-mode OF, direct map this physical address so that
	 * we have a 32-bit virtual address to give OF.
	 */

	if (!ofw_real_mode && (!hw_direct_map || DMAP_BASE_ADDRESS != 0)) 
		pmap_kenter(of_bounce_phys, of_bounce_phys);

	mtx_unlock(&of_bounce_mtx);
}

static cell_t
ofw_real_map(const void *buf, size_t len)
{
	static char emergency_buffer[255];
	cell_t phys;

	mtx_assert(&of_bounce_mtx, MA_OWNED);

	if (of_bounce_virt == NULL) {
		/*
		 * If we haven't set up the MMU, then buf is guaranteed
		 * to be accessible to OF, because the only memory we
		 * can use right now is memory mapped by firmware.
		 */
		if (!pmap_bootstrapped)
			return (cell_t)((uintptr_t)buf & ~DMAP_BASE_ADDRESS);

		/*
		 * XXX: It is possible for us to get called before the VM has
		 * come online, but after the MMU is up. We don't have the
		 * bounce buffer yet, but can no longer presume a 1:1 mapping.
		 * Copy into the emergency buffer, and reset at the end.
		 */
		of_bounce_virt = emergency_buffer;
		of_bounce_phys = (vm_offset_t)of_bounce_virt &
		    ~DMAP_BASE_ADDRESS;
		of_bounce_size = sizeof(emergency_buffer);
	}

	/*
	 * Make sure the bounce page offset satisfies any reasonable
	 * alignment constraint.
	 */
	of_bounce_offset += sizeof(register_t) -
	    (of_bounce_offset % sizeof(register_t));

	if (of_bounce_offset + len > of_bounce_size) {
		panic("Oversize Open Firmware call!");
		return 0;
	}

	if (buf != NULL)
		memcpy(of_bounce_virt + of_bounce_offset, buf, len);
	else
		return (0);

	phys = of_bounce_phys + of_bounce_offset;

	of_bounce_offset += len;

	return (phys);
}

static void
ofw_real_unmap(cell_t physaddr, void *buf, size_t len)
{
	mtx_assert(&of_bounce_mtx, MA_OWNED);

	if (of_bounce_virt == NULL)
		return;

	if (physaddr == 0)
		return;

	memcpy(buf,of_bounce_virt + (physaddr - of_bounce_phys),len);
}

/* Initialiser */

static int
ofw_real_init(ofw_t ofw, void *openfirm)
{
	openfirmware = (int (*)(void *))openfirm;

	mtx_init(&of_bounce_mtx, "OF Bounce Page", NULL, MTX_DEF);
	of_bounce_virt = NULL;
	return (0);
}

/*
 * Generic functions
 */

/* Test to see if a service exists. */
static int
ofw_real_test(ofw_t ofw, const char *name)
{
	vm_offset_t argsptr;
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t service;
		cell_t missing;
	} args;

	args.name = (cell_t)(uintptr_t)"test";
	args.nargs = 1;
	args.nreturns = 1;

	ofw_real_start();

	args.service = ofw_real_map(name, strlen(name) + 1);
	argsptr = ofw_real_map(&args, sizeof(args));
	if (args.service == 0 || openfirmware((void *)argsptr) == -1) {
		ofw_real_stop();
		return (-1);
	}
	ofw_real_unmap(argsptr, &args, sizeof(args));
	ofw_real_stop();
	return (args.missing);
}

/*
 * Device tree functions
 */

/* Return the next sibling of this node or 0. */
static phandle_t
ofw_real_peer(ofw_t ofw, phandle_t node)
{
	vm_offset_t argsptr;
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t node;
		cell_t next;
	} args;

	args.name = (cell_t)(uintptr_t)"peer";
	args.nargs = 1;
	args.nreturns = 1;

	args.node = node;
	ofw_real_start();
	argsptr = ofw_real_map(&args, sizeof(args));
	if (openfirmware((void *)argsptr) == -1) {
		ofw_real_stop();
		return (0);
	}
	ofw_real_unmap(argsptr, &args, sizeof(args));
	ofw_real_stop();
	return (args.next);
}

/* Return the first child of this node or 0. */
static phandle_t
ofw_real_child(ofw_t ofw, phandle_t node)
{
	vm_offset_t argsptr;
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t node;
		cell_t child;
	} args;

	args.name = (cell_t)(uintptr_t)"child";
	args.nargs = 1;
	args.nreturns = 1;

	args.node = node;
	ofw_real_start();
	argsptr = ofw_real_map(&args, sizeof(args));
	if (openfirmware((void *)argsptr) == -1) {
		ofw_real_stop();
		return (0);
	}
	ofw_real_unmap(argsptr, &args, sizeof(args));
	ofw_real_stop();
	return (args.child);
}

/* Return the parent of this node or 0. */
static phandle_t
ofw_real_parent(ofw_t ofw, phandle_t node)
{
	vm_offset_t argsptr;
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t node;
		cell_t parent;
	} args;

	args.name = (cell_t)(uintptr_t)"parent";
	args.nargs = 1;
	args.nreturns = 1;

	args.node = node;
	ofw_real_start();
	argsptr = ofw_real_map(&args, sizeof(args));
	if (openfirmware((void *)argsptr) == -1) {
		ofw_real_stop();
		return (0);
	}
	ofw_real_unmap(argsptr, &args, sizeof(args));
	ofw_real_stop();
	return (args.parent);
}

/* Return the package handle that corresponds to an instance handle. */
static phandle_t
ofw_real_instance_to_package(ofw_t ofw, ihandle_t instance)
{
	vm_offset_t argsptr;
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t instance;
		cell_t package;
	} args;

	args.name = (cell_t)(uintptr_t)"instance-to-package";
	args.nargs = 1;
	args.nreturns = 1;

	args.instance = instance;
	ofw_real_start();
	argsptr = ofw_real_map(&args, sizeof(args));
	if (openfirmware((void *)argsptr) == -1) {
		ofw_real_stop();
		return (-1);
	}
	ofw_real_unmap(argsptr, &args, sizeof(args));
	ofw_real_stop();
	return (args.package);
}

/* Get the length of a property of a package. */
static ssize_t
ofw_real_getproplen(ofw_t ofw, phandle_t package, const char *propname)
{
	vm_offset_t argsptr;
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t package;
		cell_t propname;
		int32_t proplen;
	} args;

	args.name = (cell_t)(uintptr_t)"getproplen";
	args.nargs = 2;
	args.nreturns = 1;

	ofw_real_start();

	args.package = package;
	args.propname = ofw_real_map(propname, strlen(propname) + 1);
	argsptr = ofw_real_map(&args, sizeof(args));
	if (args.propname == 0 || openfirmware((void *)argsptr) == -1) {
		ofw_real_stop();
		return (-1);
	}
	ofw_real_unmap(argsptr, &args, sizeof(args));
	ofw_real_stop();
	return (args.proplen);
}

/* Get the value of a property of a package. */
static ssize_t
ofw_real_getprop(ofw_t ofw, phandle_t package, const char *propname, void *buf, 
    size_t buflen)
{
	vm_offset_t argsptr;
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t package;
		cell_t propname;
		cell_t buf;
		cell_t buflen;
		int32_t size;
	} args;

	args.name = (cell_t)(uintptr_t)"getprop";
	args.nargs = 4;
	args.nreturns = 1;

	ofw_real_start();

	args.package = package;
	args.propname = ofw_real_map(propname, strlen(propname) + 1);
	args.buf = ofw_real_map(buf, buflen);
	args.buflen = buflen;
	argsptr = ofw_real_map(&args, sizeof(args));
	if (args.propname == 0 || args.buf == 0 ||
	    openfirmware((void *)argsptr) == -1) {
		ofw_real_stop();
		return (-1);
	}
	ofw_real_unmap(argsptr, &args, sizeof(args));
	ofw_real_unmap(args.buf, buf, buflen);

	ofw_real_stop();
	return (args.size);
}

/* Get the next property of a package. */
static int
ofw_real_nextprop(ofw_t ofw, phandle_t package, const char *previous, 
    char *buf, size_t size)
{
	vm_offset_t argsptr;
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t package;
		cell_t previous;
		cell_t buf;
		cell_t flag;
	} args;

	args.name = (cell_t)(uintptr_t)"nextprop";
	args.nargs = 3;
	args.nreturns = 1;

	ofw_real_start();

	args.package = package;
	args.previous = ofw_real_map(previous, (previous != NULL) ? (strlen(previous) + 1) : 0);
	args.buf = ofw_real_map(buf, size);
	argsptr = ofw_real_map(&args, sizeof(args));
	if (args.buf == 0 || openfirmware((void *)argsptr) == -1) {
		ofw_real_stop();
		return (-1);
	}
	ofw_real_unmap(argsptr, &args, sizeof(args));
	ofw_real_unmap(args.buf, buf, size);

	ofw_real_stop();
	return (args.flag);
}

/* Set the value of a property of a package. */
/* XXX Has a bug on FirePower */
static int
ofw_real_setprop(ofw_t ofw, phandle_t package, const char *propname,
    const void *buf, size_t len)
{
	vm_offset_t argsptr;
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t package;
		cell_t propname;
		cell_t buf;
		cell_t len;
		cell_t size;
	} args;

	args.name = (cell_t)(uintptr_t)"setprop";
	args.nargs = 4;
	args.nreturns = 1;

	ofw_real_start();

	args.package = package;
	args.propname = ofw_real_map(propname, strlen(propname) + 1);
	args.buf = ofw_real_map(buf, len);
	args.len = len;
	argsptr = ofw_real_map(&args, sizeof(args));
	if (args.propname == 0 || args.buf == 0 ||
	    openfirmware((void *)argsptr) == -1) {
		ofw_real_stop();
		return (-1);
	}
	ofw_real_unmap(argsptr, &args, sizeof(args));
	ofw_real_stop();
	return (args.size);
}

/* Convert a device specifier to a fully qualified pathname. */
static ssize_t
ofw_real_canon(ofw_t ofw, const char *device, char *buf, size_t len)
{
	vm_offset_t argsptr;
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t device;
		cell_t buf;
		cell_t len;
		int32_t size;
	} args;

	args.name = (cell_t)(uintptr_t)"canon";
	args.nargs = 3;
	args.nreturns = 1;

	ofw_real_start();

	args.device = ofw_real_map(device, strlen(device) + 1);
	args.buf = ofw_real_map(buf, len);
	args.len = len;
	argsptr = ofw_real_map(&args, sizeof(args));
	if (args.device == 0 || args.buf == 0 ||
	    openfirmware((void *)argsptr) == -1) {
		ofw_real_stop();
		return (-1);
	}
	ofw_real_unmap(argsptr, &args, sizeof(args));
	ofw_real_unmap(args.buf, buf, len);

	ofw_real_stop();
	return (args.size);
}

/* Return a package handle for the specified device. */
static phandle_t
ofw_real_finddevice(ofw_t ofw, const char *device)
{
	vm_offset_t argsptr;
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t device;
		cell_t package;
	} args;

	args.name = (cell_t)(uintptr_t)"finddevice";
	args.nargs = 1;
	args.nreturns = 1;

	ofw_real_start();

	args.device = ofw_real_map(device, strlen(device) + 1);
	argsptr = ofw_real_map(&args, sizeof(args));
	if (args.device == 0 ||
	    openfirmware((void *)argsptr) == -1) {
		ofw_real_stop();
		return (-1);
	}
	ofw_real_unmap(argsptr, &args, sizeof(args));
	ofw_real_stop();
	return (args.package);
}

/* Return the fully qualified pathname corresponding to an instance. */
static ssize_t
ofw_real_instance_to_path(ofw_t ofw, ihandle_t instance, char *buf, size_t len)
{
	vm_offset_t argsptr;
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t instance;
		cell_t buf;
		cell_t len;
		int32_t size;
	} args;

	args.name = (cell_t)(uintptr_t)"instance-to-path";
	args.nargs = 3;
	args.nreturns = 1;

	ofw_real_start();

	args.instance = instance;
	args.buf = ofw_real_map(buf, len);
	args.len = len;
	argsptr = ofw_real_map(&args, sizeof(args));
	if (args.buf == 0 ||
	    openfirmware((void *)argsptr) == -1) {
		ofw_real_stop();
		return (-1);
	}
	ofw_real_unmap(argsptr, &args, sizeof(args));
	ofw_real_unmap(args.buf, buf, len);

	ofw_real_stop();
	return (args.size);
}

/* Return the fully qualified pathname corresponding to a package. */
static ssize_t
ofw_real_package_to_path(ofw_t ofw, phandle_t package, char *buf, size_t len)
{
	vm_offset_t argsptr;
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t package;
		cell_t buf;
		cell_t len;
		int32_t size;
	} args;

	args.name = (cell_t)(uintptr_t)"package-to-path";
	args.nargs = 3;
	args.nreturns = 1;

	ofw_real_start();

	args.package = package;
	args.buf = ofw_real_map(buf, len);
	args.len = len;
	argsptr = ofw_real_map(&args, sizeof(args));
	if (args.buf == 0 ||
	    openfirmware((void *)argsptr) == -1) {
		ofw_real_stop();
		return (-1);
	}
	ofw_real_unmap(argsptr, &args, sizeof(args));
	ofw_real_unmap(args.buf, buf, len);

	ofw_real_stop();
	return (args.size);
}

/*  Call the method in the scope of a given instance. */
static int
ofw_real_call_method(ofw_t ofw, ihandle_t instance, const char *method, 
    int nargs, int nreturns, cell_t *args_and_returns)
{
	vm_offset_t argsptr;
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t method;
		cell_t instance;
		cell_t args_n_results[12];
	} args;
	cell_t *ap, *cp;
	int n;

	args.name = (cell_t)(uintptr_t)"call-method";
	args.nargs = 2;
	args.nreturns = 1;

	if (nargs > 6)
		return (-1);

	ofw_real_start();
	args.nargs = nargs + 2;
	args.nreturns = nreturns + 1;
	args.method = ofw_real_map(method, strlen(method) + 1);
	args.instance = instance;

	ap = args_and_returns;
	for (cp = args.args_n_results + (n = nargs); --n >= 0;)
		*--cp = *(ap++);
	argsptr = ofw_real_map(&args, sizeof(args));
	if (args.method == 0 ||
	    openfirmware((void *)argsptr) == -1) {
		ofw_real_stop();
		return (-1);
	}
	ofw_real_unmap(argsptr, &args, sizeof(args));
	ofw_real_stop();
	if (args.args_n_results[nargs])
		return (args.args_n_results[nargs]);
	for (cp = args.args_n_results + nargs + (n = args.nreturns); --n > 0;)
		*(ap++) = *--cp;
	return (0);
}

static int
ofw_real_interpret(ofw_t ofw, const char *cmd, int nreturns, cell_t *returns)
{
	vm_offset_t argsptr;
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t slot[16];
	} args;
	cell_t status;
	int i = 0, j = 0;

	args.name = (cell_t)(uintptr_t)"interpret";
	args.nargs = 1;

	ofw_real_start();
	args.nreturns = ++nreturns;
	args.slot[i++] = ofw_real_map(cmd, strlen(cmd) + 1);
	argsptr = ofw_real_map(&args, sizeof(args));
	if (openfirmware((void *)argsptr) == -1) {
		ofw_real_stop();
		return (-1);
	}
	ofw_real_unmap(argsptr, &args, sizeof(args));
	ofw_real_stop();
	status = args.slot[i++];
	while (i < 1 + nreturns)
		returns[j++] = args.slot[i++];
	return (status);
}

/*
 * Device I/O functions
 */

/* Open an instance for a device. */
static ihandle_t
ofw_real_open(ofw_t ofw, const char *device)
{
	vm_offset_t argsptr;
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t device;
		cell_t instance;
	} args;

	args.name = (cell_t)(uintptr_t)"open";
	args.nargs = 1;
	args.nreturns = 1;

	ofw_real_start();

	args.device = ofw_real_map(device, strlen(device) + 1);
	argsptr = ofw_real_map(&args, sizeof(args));
	if (args.device == 0 || openfirmware((void *)argsptr) == -1 
	    || args.instance == 0) {
		ofw_real_stop();
		return (-1);
	}
	ofw_real_unmap(argsptr, &args, sizeof(args));
	ofw_real_stop();
	return (args.instance);
}

/* Close an instance. */
static void
ofw_real_close(ofw_t ofw, ihandle_t instance)
{
	vm_offset_t argsptr;
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t instance;
	} args;

	args.name = (cell_t)(uintptr_t)"close";
	args.nargs = 1;
	args.nreturns = 0;
	args.instance = instance;
	ofw_real_start();
	argsptr = ofw_real_map(&args, sizeof(args));
	openfirmware((void *)argsptr);
	ofw_real_stop();
}

/* Read from an instance. */
static ssize_t
ofw_real_read(ofw_t ofw, ihandle_t instance, void *addr, size_t len)
{
	vm_offset_t argsptr;
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t instance;
		cell_t addr;
		cell_t len;
		int32_t actual;
	} args;

	args.name = (cell_t)(uintptr_t)"read";
	args.nargs = 3;
	args.nreturns = 1;

	ofw_real_start();

	args.instance = instance;
	args.addr = ofw_real_map(addr, len);
	args.len = len;
	argsptr = ofw_real_map(&args, sizeof(args));
	if (args.addr == 0 || openfirmware((void *)argsptr) == -1) {
		ofw_real_stop();
		return (-1);
	}
	ofw_real_unmap(argsptr, &args, sizeof(args));
	ofw_real_unmap(args.addr, addr, len);

	ofw_real_stop();
	return (args.actual);
}

/* Write to an instance. */
static ssize_t
ofw_real_write(ofw_t ofw, ihandle_t instance, const void *addr, size_t len)
{
	vm_offset_t argsptr;
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t instance;
		cell_t addr;
		cell_t len;
		int32_t actual;
	} args;

	args.name = (cell_t)(uintptr_t)"write";
	args.nargs = 3;
	args.nreturns = 1;

	ofw_real_start();

	args.instance = instance;
	args.addr = ofw_real_map(addr, len);
	args.len = len;
	argsptr = ofw_real_map(&args, sizeof(args));
	if (args.addr == 0 || openfirmware((void *)argsptr) == -1) {
		ofw_real_stop();
		return (-1);
	}
	ofw_real_unmap(argsptr, &args, sizeof(args));
	ofw_real_stop();
	return (args.actual);
}

/* Seek to a position. */
static int
ofw_real_seek(ofw_t ofw, ihandle_t instance, u_int64_t pos)
{
	vm_offset_t argsptr;
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t instance;
		cell_t poshi;
		cell_t poslo;
		cell_t status;
	} args;

	args.name = (cell_t)(uintptr_t)"seek";
	args.nargs = 3;
	args.nreturns = 1;

	args.instance = instance;
	args.poshi = pos >> 32;
	args.poslo = pos;
	ofw_real_start();
	argsptr = ofw_real_map(&args, sizeof(args));
	if (openfirmware((void *)argsptr) == -1) {
		ofw_real_stop();
		return (-1);
	}
	ofw_real_unmap(argsptr, &args, sizeof(args));
	ofw_real_stop();
	return (args.status);
}

/*
 * Memory functions
 */

/* Claim an area of memory. */
static caddr_t
ofw_real_claim(ofw_t ofw, void *virt, size_t size, u_int align)
{
	vm_offset_t argsptr;
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t virt;
		cell_t size;
		cell_t align;
		cell_t baseaddr;
	} args;

	args.name = (cell_t)(uintptr_t)"claim";
	args.nargs = 3;
	args.nreturns = 1;

	args.virt = (cell_t)(uintptr_t)virt;
	args.size = size;
	args.align = align;
	ofw_real_start();
	argsptr = ofw_real_map(&args, sizeof(args));
	if (openfirmware((void *)argsptr) == -1) {
		ofw_real_stop();
		return ((void *)-1);
	}
	ofw_real_unmap(argsptr, &args, sizeof(args));
	ofw_real_stop();
	return ((void *)(uintptr_t)args.baseaddr);
}

/* Release an area of memory. */
static void
ofw_real_release(ofw_t ofw, void *virt, size_t size)
{
	vm_offset_t argsptr;
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t virt;
		cell_t size;
	} args;

	args.name = (cell_t)(uintptr_t)"release";
	args.nargs = 2;
	args.nreturns = 0;

	args.virt = (cell_t)(uintptr_t)virt;
	args.size = size;
	ofw_real_start();
	argsptr = ofw_real_map(&args, sizeof(args));
	openfirmware((void *)argsptr);
	ofw_real_stop();
}

/*
 * Control transfer functions
 */

/* Suspend and drop back to the Open Firmware interface. */
static void
ofw_real_enter(ofw_t ofw)
{
	vm_offset_t argsptr;
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
	} args;

	args.name = (cell_t)(uintptr_t)"enter";
	args.nargs = 0;
	args.nreturns = 0;

	ofw_real_start();
	argsptr = ofw_real_map(&args, sizeof(args));
	openfirmware((void *)argsptr);
	/* We may come back. */
	ofw_real_stop();
}

/* Shut down and drop back to the Open Firmware interface. */
static void
ofw_real_exit(ofw_t ofw)
{
	vm_offset_t argsptr;
	struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
	} args;

	args.name = (cell_t)(uintptr_t)"exit";
	args.nargs = 0;
	args.nreturns = 0;

	ofw_real_start();
	argsptr = ofw_real_map(&args, sizeof(args));
	openfirmware((void *)argsptr);
	for (;;)			/* just in case */
		;
	ofw_real_stop();
}

