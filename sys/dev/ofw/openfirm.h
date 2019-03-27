/*	$NetBSD: openfirm.h,v 1.1 1998/05/15 10:16:00 tsubai Exp $	*/

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
/*
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
 *
 * $FreeBSD$
 */

#ifndef _DEV_OPENFIRM_H_
#define _DEV_OPENFIRM_H_

#include <sys/types.h>
#include <machine/_bus.h>

/*
 * Prototypes for Open Firmware Interface Routines
 */

typedef uint32_t	ihandle_t;
typedef uint32_t	phandle_t;
typedef uint32_t	pcell_t;

#ifdef _KERNEL
#include <sys/malloc.h>

#include <machine/ofw_machdep.h>

MALLOC_DECLARE(M_OFWPROP);

/*
 * Open Firmware interface initialization.  OF_install installs the named
 * interface as the Open Firmware access mechanism, OF_init initializes it.
 */

boolean_t	OF_install(char *name, int prio);
int		OF_init(void *cookie);

/*
 * Known Open Firmware interface names
 */

#define	OFW_STD_DIRECT	"ofw_std"	/* Standard OF interface */
#define	OFW_STD_REAL	"ofw_real"	/* Real-mode OF interface */
#define	OFW_STD_32BIT	"ofw_32bit"	/* 32-bit OF interface */
#define	OFW_FDT		"ofw_fdt"	/* Flattened Device Tree */

/* Generic functions */
int		OF_test(const char *name);
void		OF_printf(const char *fmt, ...);

/* Device tree functions */
phandle_t	OF_peer(phandle_t node);
phandle_t	OF_child(phandle_t node);
phandle_t	OF_parent(phandle_t node);
ssize_t		OF_getproplen(phandle_t node, const char *propname);
ssize_t		OF_getprop(phandle_t node, const char *propname, void *buf,
		    size_t len);
ssize_t		OF_getencprop(phandle_t node, const char *prop, pcell_t *buf,
		    size_t len); /* Same as getprop, but maintains endianness */
int		OF_hasprop(phandle_t node, const char *propname);
ssize_t		OF_searchprop(phandle_t node, const char *propname, void *buf,
		    size_t len);
ssize_t		OF_searchencprop(phandle_t node, const char *propname,
		    pcell_t *buf, size_t len);
ssize_t		OF_getprop_alloc(phandle_t node, const char *propname,
		    void **buf);
ssize_t		OF_getprop_alloc_multi(phandle_t node, const char *propname,
		    int elsz, void **buf);
ssize_t		OF_getencprop_alloc(phandle_t node, const char *propname,
		    void **buf);
ssize_t		OF_getencprop_alloc_multi(phandle_t node, const char *propname,
		    int elsz, void **buf);
void		OF_prop_free(void *buf);
int		OF_nextprop(phandle_t node, const char *propname, char *buf,
		    size_t len);
int		OF_setprop(phandle_t node, const char *name, const void *buf,
		    size_t len);
ssize_t		OF_canon(const char *path, char *buf, size_t len);
phandle_t	OF_finddevice(const char *path);
ssize_t		OF_package_to_path(phandle_t node, char *buf, size_t len);

/*
 * Some OF implementations (IBM, FDT) have a concept of effective phandles
 * used for device-tree cross-references. Given one of these, returns the
 * real phandle. If one can't be found (or running on OF implementations
 * without this property), returns its input.
 */
phandle_t	OF_node_from_xref(phandle_t xref);
phandle_t	OF_xref_from_node(phandle_t node);

/*
 * When properties contain references to other nodes using xref handles it is
 * often necessary to use interfaces provided by the driver for the referenced
 * instance.  These routines allow a driver that provides such an interface to
 * register its association with an xref handle, and for other drivers to obtain
 * the device_t associated with an xref handle.
 */
device_t	OF_device_from_xref(phandle_t xref);
phandle_t	OF_xref_from_device(device_t dev);
int		OF_device_register_xref(phandle_t xref, device_t dev);

/* Device I/O functions */
ihandle_t	OF_open(const char *path);
void		OF_close(ihandle_t instance);
ssize_t		OF_read(ihandle_t instance, void *buf, size_t len);
ssize_t		OF_write(ihandle_t instance, const void *buf, size_t len);
int		OF_seek(ihandle_t instance, uint64_t where);

phandle_t	OF_instance_to_package(ihandle_t instance);
ssize_t		OF_instance_to_path(ihandle_t instance, char *buf, size_t len);
int		OF_call_method(const char *method, ihandle_t instance,
		    int nargs, int nreturns, ...);

/* Memory functions */
void		*OF_claim(void *virtrequest, size_t size, u_int align);
void		OF_release(void *virt, size_t size);

/* Control transfer functions */
void		OF_enter(void);
void		OF_exit(void) __attribute__((noreturn));

/* User interface functions */
int		OF_interpret(const char *cmd, int nreturns, ...);

/*
 * Decode the Nth register property of the given device node and create a bus
 * space tag and handle for accessing it.  This is for use in setting up things
 * like early console output before newbus is available.  The implementation is
 * machine-dependent, and sparc uses a different function signature as well.
 */
#ifndef __sparc64__
int		OF_decode_addr(phandle_t dev, int regno, bus_space_tag_t *ptag,
		    bus_space_handle_t *phandle, bus_size_t *sz);
#endif

#endif /* _KERNEL */
#endif /* _DEV_OPENFIRM_H_ */
