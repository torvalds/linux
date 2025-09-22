/*	$OpenBSD: conf.c,v 1.8 2015/01/16 06:40:19 deraadt Exp $	*/
/*	$NetBSD: conf.c,v 1.5 1995/10/06 05:12:13 thorpej Exp $	*/

/*
 * Copyright (c) 1988, 1992 The University of Utah and the Center
 *	for Software Science (CSS).
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Center for Software Science of the University of Utah Computer
 * Science Department.  CSS requests users of this software to return
 * to css-dist@cs.utah.edu any improvements that they make and grant
 * CSS redistribution rights.
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
 *	from: @(#)conf.c	8.1 (Berkeley) 6/4/93
 *
 * From: Utah Hdr: conf.c 3.1 92/07/06
 * Author: Jeff Forys, University of Utah CSS
 */

#include <sys/time.h>

#include <stdio.h>
#include <limits.h>
#include "defs.h"
#include "pathnames.h"

/*
**  Define (and possibly initialize) global variables here.
**
**  Caveat:
**	The maximum number of bootable files (`char *BootFiles[]') is
**	limited to C_MAXFILE (i.e. the maximum number of files that
**	can be spec'd in the configuration file).  This was done to
**	simplify the boot file search code.
*/

char	MyHost[HOST_NAME_MAX+1];			/* host name */
int	DebugFlg = 0;				/* set true if debugging */
int	BootAny = 0;				/* set true if we boot anyone */

char	*ConfigFile = NULL;			/* configuration file */
char	*DfltConfig = _PATH_RBOOTDCONF;		/* default configuration file */
char	*BootDir = _PATH_RBOOTDDIR;		/* directory w/boot files */
char	*DbgFile = _PATH_RBOOTDDBG;		/* debug output file */

FILE	*DbgFp = NULL;				/* debug file pointer */
char	*IntfName = NULL;			/* intf we are attached to */

u_int16_t SessionID = 0;			/* generated session ID */

char	*BootFiles[C_MAXFILE];			/* list of boot files */

CLIENT	*Clients = NULL;			/* list of addrs we'll accept */
RMPCONN	*RmpConns = NULL;			/* list of active connections */

u_int8_t RmpMcastAddr[RMP_ADDRLEN] = RMP_ADDR;	/* RMP multicast address */
