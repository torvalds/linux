/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Bjoern A. Zeeb <bz@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/socket.h>

#ifdef DDB
#include <ddb/ddb.h>
#endif

#include <net/if.h>
#include <net/if_var.h>

#include <netinet/in.h>
#include <netinet/in_var.h>

#ifdef DDB
static void
in_show_sockaddr_in(struct sockaddr_in *sin)
{

#define	SIN_DB_RPINTF(f, e)	db_printf("\t   %s = " f "\n", #e, sin->e);
	db_printf("\tsockaddr_in = %p\n", sin);
	SIN_DB_RPINTF("%u", sin_len);
	SIN_DB_RPINTF("%u", sin_family);
	SIN_DB_RPINTF("%u", sin_port);
	SIN_DB_RPINTF("0x%08x", sin_addr.s_addr);
	db_printf("\t   %s = %02x%02x%02x%02x%02x%02x%02x%02x\n",
	    "sin_zero[8]",
	    sin->sin_zero[0], sin->sin_zero[1],
	    sin->sin_zero[2], sin->sin_zero[3],
	    sin->sin_zero[4], sin->sin_zero[5],
	    sin->sin_zero[6], sin->sin_zero[7]);
#undef SIN_DB_RPINTF
}

DB_SHOW_COMMAND(sin, db_show_sin)
{
	struct sockaddr_in *sin;

	sin = (struct sockaddr_in *)addr;
	if (sin == NULL) {
		/* usage: No need to confess if you didn't sin. */
		db_printf("usage: show sin <struct sockaddr_in *>\n");
		return;
	}

	in_show_sockaddr_in(sin);
}

static void
in_show_in_ifaddr(struct in_ifaddr *ia)
{

#define	IA_DB_RPINTF(f, e)	db_printf("\t   %s = " f "\n", #e, ia->e);
#define	IA_DB_RPINTF_PTR(f, e)	db_printf("\t   %s = " f "\n", #e, &ia->e);
#define	IA_DB_RPINTF_DPTR(f, e)	db_printf("\t  *%s = " f "\n", #e, *ia->e);
	db_printf("\tin_ifaddr = %p\n", ia);
	IA_DB_RPINTF_PTR("%p", ia_ifa);
	IA_DB_RPINTF("0x%08lx", ia_subnet);
	IA_DB_RPINTF("0x%08lx", ia_subnetmask);
	IA_DB_RPINTF("%p", ia_hash.le_next);
	IA_DB_RPINTF("%p", ia_hash.le_prev);
	IA_DB_RPINTF_DPTR("%p", ia_hash.le_prev);
	IA_DB_RPINTF("%p", ia_link.cstqe_next);
	IA_DB_RPINTF_PTR("%p", ia_addr);
	IA_DB_RPINTF_PTR("%p", ia_dstaddr);
	IA_DB_RPINTF_PTR("%p", ia_sockmask);
#undef IA_DB_RPINTF_DPTR
#undef IA_DB_RPINTF_PTR
#undef IA_DB_RPINTF
}

DB_SHOW_COMMAND(in_ifaddr, db_show_in_ifaddr)
{
	struct in_ifaddr *ia;

	ia = (struct in_ifaddr *)addr;
	if (ia == NULL) {
		db_printf("usage: show in_ifaddr <struct in_ifaddr *>\n");
		return;
	}

	in_show_in_ifaddr(ia);
}
#endif
