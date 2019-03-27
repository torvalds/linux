/*	$NetBSD: Locore.c,v 1.7 2000/08/20 07:04:59 tsubai Exp $	*/

/*-
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

#include <machine/stdarg.h>

#include <stand.h>

#include "openfirm.h"

int (*openfirmware)(void *);

phandle_t chosen;
ihandle_t mmu;
ihandle_t memory;
int	  real_mode = 0;

/* Initialiser */

void
OF_init(int (*openfirm)(void *))
{
	phandle_t options;
	char	  mode[sizeof("true")];

	openfirmware = openfirm;

	if ((chosen = OF_finddevice("/chosen")) == -1)
		OF_exit();
	if (OF_getprop(chosen, "memory", &memory, sizeof(memory)) == -1) {
		memory = OF_open("/memory");
		if (memory == -1)
			memory = OF_open("/memory@0");
		if (memory == -1)
			OF_exit();
	}
	if (OF_getprop(chosen, "mmu", &mmu, sizeof(mmu)) == -1)
		OF_exit();

	/*
	 * Check if we run in real mode. If so, we do not need to map
	 * memory later on.
	 */
	options = OF_finddevice("/options");
	if (OF_getprop(options, "real-mode?", mode, sizeof(mode)) > 0 &&
	    strcmp(mode, "true") == 0)
		real_mode = 1;
}

/*
 * Generic functions
 */

/* Test to see if a service exists. */
int
OF_test(char *name)
{
	static struct {
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

/* Return firmware millisecond count. */
int
OF_milliseconds()
{
	static struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t ms;
	} args = {
		(cell_t)"milliseconds",
		0,
		1,
	};

	openfirmware(&args);
	return (args.ms);
}

/*
 * Device tree functions
 */

/* Return the next sibling of this node or 0. */
phandle_t
OF_peer(phandle_t node)
{
	static struct {
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
		return (-1);
	return (args.next);
}

/* Return the first child of this node or 0. */
phandle_t
OF_child(phandle_t node)
{
	static struct {
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
		return (-1);
	return (args.child);
}

/* Return the parent of this node or 0. */
phandle_t
OF_parent(phandle_t node)
{
	static struct {
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
		return (-1);
	return (args.parent);
}

/* Return the package handle that corresponds to an instance handle. */
phandle_t
OF_instance_to_package(ihandle_t instance)
{
	static struct {
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
int
OF_getproplen(phandle_t package, const char *propname)
{
	static struct {
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
int
OF_getprop(phandle_t package, const char *propname, void *buf, int buflen)
{
	static struct {
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
int
OF_nextprop(phandle_t package, const char *previous, char *buf)
{
	static struct {
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
int
OF_setprop(phandle_t package, const char *propname, void *buf, int len)
{
	static struct {
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
int
OF_canon(const char *device, char *buf, int len)
{
	static struct {
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
phandle_t
OF_finddevice(const char *device)
{
	static struct {
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
int
OF_instance_to_path(ihandle_t instance, char *buf, int len)
{
	static struct {
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
int
OF_package_to_path(phandle_t package, char *buf, int len)
{
	static struct {
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
int
OF_call_method(char *method, ihandle_t instance, int nargs, int nreturns, ...)
{
	va_list ap;
	static struct {
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
	cell_t *cp;
	int n;

	if (nargs > 6)
		return (-1);
	args.nargs = nargs + 2;
	args.nreturns = nreturns + 1;
	args.method = (cell_t)method;
	args.instance = instance;
	va_start(ap, nreturns);
	for (cp = (cell_t *)(args.args_n_results + (n = nargs)); --n >= 0;)
		*--cp = va_arg(ap, cell_t);
	if (openfirmware(&args) == -1)
		return (-1);
	if (args.args_n_results[nargs])
		return (args.args_n_results[nargs]);
	for (cp = (cell_t *)(args.args_n_results + nargs + (n = args.nreturns));
	    --n > 0;)
		*va_arg(ap, cell_t *) = *--cp;
	va_end(ap);
	return (0);
}

/*
 * Device I/O functions
 */

/* Open an instance for a device. */
ihandle_t
OF_open(char *device)
{
	static struct {
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
	if (openfirmware(&args) == -1 || args.instance == 0) {
		return (-1);
	}
	return (args.instance);
}

/* Close an instance. */
void
OF_close(ihandle_t instance)
{
	static struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t instance;
	} args = {
		(cell_t)"close",
		1,
	};

	args.instance = instance;
	openfirmware(&args);
}

/* Read from an instance. */
int
OF_read(ihandle_t instance, void *addr, int len)
{
	static struct {
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

#if defined(OPENFIRM_DEBUG)
	printf("OF_read: called with instance=%08x, addr=%p, len=%d\n",
	    args.instance, args.addr, args.len);
#endif

	if (openfirmware(&args) == -1)
		return (-1);

#if defined(OPENFIRM_DEBUG)
	printf("OF_read: returning instance=%d, addr=%p, len=%d, actual=%d\n",
	    args.instance, args.addr, args.len, args.actual);
#endif

	return (args.actual);
}

/* Write to an instance. */
int
OF_write(ihandle_t instance, void *addr, int len)
{
	static struct {
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
int
OF_seek(ihandle_t instance, uint64_t pos)
{
	static struct {
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

/* Blocks. */
unsigned int
OF_blocks(ihandle_t instance)
{
	static struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t instance;
		cell_t result;
		cell_t blocks;
	} args = {
		(cell_t)"#blocks",
		2,
		1,
	};

	args.instance = instance;
	if (openfirmware(&args) == -1)
		return ((unsigned int)-1);
	return (args.blocks);
}

/* Block size. */
int
OF_block_size(ihandle_t instance)
{
	static struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t instance;
		cell_t result;
		cell_t size;
	} args = {
		(cell_t)"block-size",
		2,
		1,
	};

	args.instance = instance;
	if (openfirmware(&args) == -1)
		return (512);
	return (args.size);
}

/* 
/*
 * Memory functions
 */

/* Claim an area of memory. */
void *
OF_claim(void *virt, u_int size, u_int align)
{
	static struct {
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
void
OF_release(void *virt, u_int size)
{
	static struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t virt;
		cell_t size;
	} args = {
		(cell_t)"release",
		2,
	};

	args.virt = (cell_t)virt;
	args.size = size;
	openfirmware(&args);
}

/*
 * Control transfer functions
 */

/* Reset the system and call "boot <bootspec>". */
void
OF_boot(char *bootspec)
{
	static struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t bootspec;
	} args = {
		(cell_t)"boot",
		1,
	};

	args.bootspec = (cell_t)bootspec;
	openfirmware(&args);
	for (;;)			/* just in case */
		;
}

/* Suspend and drop back to the Open Firmware interface. */
void
OF_enter()
{
	static struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
	} args = {
		(cell_t)"enter",
	};

	openfirmware(&args);
	/* We may come back. */
}

/* Shut down and drop back to the Open Firmware interface. */
void
OF_exit()
{
	static struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
	} args = {
		(cell_t)"exit",
	};

	openfirmware(&args);
	for (;;)			/* just in case */
		;
}

void
OF_quiesce()
{
	static struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
	} args = {
		(cell_t)"quiesce",
	};

	openfirmware(&args);
}

/* Free <size> bytes starting at <virt>, then call <entry> with <arg>. */
#if 0
void
OF_chain(void *virt, u_int size, void (*entry)(), void *arg, u_int len)
{
	static struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
		cell_t virt;
		cell_t size;
		cell_t entry;
		cell_t arg;
		cell_t len;
	} args = {
		(cell_t)"chain",
		5,
	};

	args.virt = (cell_t)virt;
	args.size = size;
	args.entry = (cell_t)entry;
	args.arg = (cell_t)arg;
	args.len = len;
	openfirmware(&args);
}
#else
void
OF_chain(void *virt, u_int size, void (*entry)(), void *arg, u_int len)
{
	/*
	 * This is a REALLY dirty hack till the firmware gets this going
	 */
#if 0
	if (size > 0)
		OF_release(virt, size);
#endif
	entry(0, 0, openfirmware, arg, len);
}
#endif
