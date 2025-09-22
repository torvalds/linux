/* $OpenBSD: mcbusvar.h,v 1.1 2007/03/16 21:22:27 robert Exp $ */
/* $NetBSD: mcbusvar.h,v 1.6 2005/12/11 12:16:17 christos Exp $ */

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
 * Soft definitions for the MCBUS main system
 * bus found on AlphaServer 4100 systems.
 */

/*
 * The structure used to attach devices to the MCbus.
 */
struct mcbus_dev_attach_args {
	char *		ma_name;	/* so things aren't confused */
	u_int8_t	ma_gid;		/* GID of MCBUS (MCBUS #) */
	u_int8_t	ma_mid;		/* Module ID on MCBUS */
	u_int8_t	ma_type;	/* Module "type" */
	u_int8_t	ma_configured;	/* nonzero if configured */
};
#define	MCBUS_GID_FROM_INSTANCE(unit)	(7 - unit)

/*
 * Bus-dependent structure for CPUs. This is dynamically allocated
 * for each CPU on the MCbus, and glued into the cpu_softc as sc_busdep,
 * if there is such a beast available. Otherwise, a single global version
 * is used so that the MCPCIA configuration code can determine toads
 * like module id and bcache size of the master CPU.
 */
struct mcbus_cpu_busdep {
	u_int8_t	mcbus_cpu_mid;	/* MCbus Module ID */
	u_int8_t	mcbus_bcache;	/* BCache on this CPU */
	u_int8_t	mcbus_valid;
};

#define	MCBUS_CPU_BCACHE_0MB	0
#define	MCBUS_CPU_BCACHE_1MB	1
#define	MCBUS_CPU_BCACHE_4MB	2

/*
 * "types"
 */
#define	MCBUS_TYPE_RES	0
#define	MCBUS_TYPE_UNK	1
#define	MCBUS_TYPE_MEM	2
#define	MCBUS_TYPE_CPU	3
#define	MCBUS_TYPE_PCI	4

#ifdef _KERNEL
extern struct mcbus_cpu_busdep mcbus_primary;
extern const int mcbus_mcpcia_probe_order[];
#endif
