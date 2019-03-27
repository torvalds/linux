/*	$NetBSD: openfirmio.h,v 1.4 2002/09/06 13:23:19 gehenna Exp $ */

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)openpromio.h	8.1 (Berkeley) 6/11/93
 *
 * $FreeBSD$
 */

#ifndef _DEV_OFW_OPENFIRMIO_H_
#define _DEV_OFW_OPENFIRMIO_H_

#include <dev/ofw/openfirm.h>

struct ofiocdesc {
	phandle_t	of_nodeid;	/* passed or returned node id */
	int		of_namelen;	/* length of of_name */
	const char	*of_name;	/* pointer to field name */
	int		of_buflen;	/* length of of_buf (value-result) */
	char		*of_buf;	/* pointer to field value */
};

#define	OFIOC_BASE	'O'

/* Get openprom field. */
#define	OFIOCGET	_IOWR(OFIOC_BASE, 1, struct ofiocdesc)
/* Set openprom field. */
#define	OFIOCSET	_IOWR(OFIOC_BASE, 2, struct ofiocdesc)
/* Get next property. */
#define	OFIOCNEXTPROP	_IOWR(OFIOC_BASE, 3, struct ofiocdesc)
/* Get options node. */
#define	OFIOCGETOPTNODE	_IOR(OFIOC_BASE, 4, phandle_t)
/* Get next node of node. */
#define	OFIOCGETNEXT	_IOWR(OFIOC_BASE, 5, phandle_t)
/* Get first child of node. */
#define	OFIOCGETCHILD	_IOWR(OFIOC_BASE, 6, phandle_t)
/* Find a specific device. */
#define	OFIOCFINDDEVICE	_IOWR(OFIOC_BASE, 7, struct ofiocdesc)
/* Retrieve the size of a property. */
#define	OFIOCGETPROPLEN	_IOWR(OFIOC_BASE, 8, struct ofiocdesc)

/* Maximum accepted name length. */
#define	OFIOCMAXNAME	8191
/* Maximum accepted value length (maximum of nvramrc property). */
#define	OFIOCMAXVALUE	8192

#endif /* _DEV_OFW_OPENFIRMIO_H_ */
