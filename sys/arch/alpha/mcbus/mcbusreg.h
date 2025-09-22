/* $OpenBSD: mcbusreg.h,v 1.2 2025/06/29 15:55:21 miod Exp $ */
/* $NetBSD: mcbusreg.h,v 1.3 1999/11/16 18:36:27 mjacob Exp $ */

/*
 * Copyright (c) 1998 by Matthew Jacob
 * NASA AMES Research Center.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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

/*
 * 'Register' definitions for the MCBUS main
 * system bus found on AlphaServer 4100 systems.
 */

/*
 * Information gathered from:
 *
 * "Rawhide System Programmer's Manual, revision 1.4".
 */

/*
 * There are 7 possible MC bus modules (architecture says 10, but
 * the address map details say otherwise), 1 though 7.
 * Their uses are defined as follows:
 *
 *	MID	Module
 *	----    ------
 *	1	Memory
 *	2	CPU
 *	3	CPU
 *	4	CPU, PCI
 *	5	CPU, PCI
 *	6	CPU, PCI
 *	7	CPU, PCI
 *
 */
#define	MCBUS_MID_MAX		7

/*
 * For this architecture, bit 39 of a 40 bit address controls whether
 * you access I/O or Memory space. Further, there *could* be multiple
 * MC busses (but only one specified for now).
 */

#define	MCBUS_IOSPACE		0x0000008000000000L
#define MCBUS_GID_MASK		0x0000007000000000L
#define	MCBUS_GID_SHIFT		36
#define	MCBUS_MID_MASK		0x0000000E00000000L
#define	MCBUS_MID_SHIFT		33

#define	MAX_MC_BUS		8

/*
 * This is something of a layering violation, but it makes probing cleaner.
 */
#define	MCPCIA_PER_MCBUS	4

/*
 * defaults for locators
 */
#define MCBUSCF_NLOCS 1
#define MCBUSCF_MID 0
#define MCBUSCF_MID_DEFAULT -1

/* the MCPCIA bridge CSR addresses, offset zero, is a good thing to probe for */
#define	MCPCIA_BRIDGE_ADDR(gid, mid)	\
	(MCBUS_IOSPACE | 0x1E0000000LL	|		\
	(((unsigned long) gid) << MCBUS_GID_SHIFT) |	\
	(((unsigned long) mid) << MCBUS_MID_SHIFT))
