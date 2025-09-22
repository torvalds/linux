/*	$OpenBSD: autoconf.h,v 1.20 2024/05/17 20:05:08 miod Exp $	*/
/*	$NetBSD: autoconf.h,v 1.10 2001/07/24 19:32:11 eeh Exp $ */

/*-
 * Copyright (c) 1997, 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Paul Kranenburg.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
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
 *	@(#)autoconf.h	8.2 (Berkeley) 9/30/93
 */

/*
 * Autoconfiguration information.
 */

#include <machine/bus.h>
#include <dev/sbus/sbusvar.h>

/* This is used to map device classes to IPLs */
struct intrmap {
	char	*in_class;
	int	in_lev;
};
extern struct intrmap intrmap[];

/* The "mainbus" on ultra desktops is actually the UPA bus.  We need to
 * separate this from peripheral buses like SBus and PCI because each bus may
 * have different ways of encoding properties, such as "reg" and "interrupts".
 */

/* Device register space description */
struct upa_reg {
	int64_t	ur_paddr;
	int64_t	ur_len;
};

/* 
 * Attach arguments presented by mainbus_attach() 
 *
 * Large fields first followed by smaller ones to minimize stack space used.
 */
struct mainbus_attach_args {
	bus_space_tag_t	ma_bustag;	/* parent bus tag */
	bus_dma_tag_t	ma_dmatag;
	char		*ma_name;	/* PROM node name */
	struct upa_reg	*ma_reg;	/* "reg" properties */
	u_int		*ma_address;	/* "address" properties -- 32 bits */
	u_int		*ma_interrupts;	/* "interrupts" properties */
	int		ma_upaid;	/* UPA bus ID */
	int		ma_node;	/* PROM handle */
	int		ma_nreg;	/* Counts for those properties */
	int		ma_naddress;
	int		ma_ninterrupts;
	int		ma_pri;		/* priority (IPL) */
};

/*
 * length; the others convert or make some other guarantee.
 */
long	getproplen(int node, char *name);
int	getprop(int, char *, size_t, int *, void **);
char	*getpropstring(int node, char *name);
int	getpropint(int node, char *name, int deflt);
int	getpropspeed(int node, char *name);

/* Frequently used options node */
extern int optionsnode;

	/* new interfaces: */
char	*getpropstringA(int, char *, char *);

/*
 * `clockfreq' produces a printable representation of a clock frequency
 * (this is just a frill).
 */
char	*clockfreq(long freq);

/* Openprom V2 style boot path */
struct device;
struct bootpath {
	int	node;
	char	name[16];	/* name of this node */
	long	val[3];		/* up to three optional values */
	struct device *dev;	/* device that recognised this component */
};
struct bootpath	*bootpath_store(int, struct bootpath *);

void	bootstrap(int);
int	firstchild(int);
int	nextsibling(int);
void	callrom(void);
struct device *getdevunit(const char *, int);
int	romgetcursoraddr(int **, int **);
int	findroot(void);
int	findnode(int, const char *);
int	checkstatus(int);
int	node_has_property(int, const char *);
void	device_register(struct device *, void *);
