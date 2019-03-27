/*
 * ng_eiface.h
 */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999-2001, Vitaly V Belekhov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _NETGRAPH_NG_EIFACE_H_
#define _NETGRAPH_NG_EIFACE_H_

/* Node type name and magic cookie */
#define NG_EIFACE_NODE_TYPE		"eiface"
#define NGM_EIFACE_COOKIE		948105892

/* Interface base name */
#define NG_EIFACE_EIFACE_NAME		"ngeth"

/* My hook names */
#define NG_EIFACE_HOOK_ETHER		"ether"

/* MTU bounds */
#define NG_EIFACE_MTU_MIN		72
#define NG_EIFACE_MTU_MAX		ETHER_MAX_LEN_JUMBO
#define NG_EIFACE_MTU_DEFAULT		1500

/* Netgraph commands */
enum {
	NGM_EIFACE_GET_IFNAME = 1,	/* get the interface name */
	NGM_EIFACE_GET_IFADDRS,		/* returns list of addresses */
	NGM_EIFACE_SET,			/* set ethernet address */
};

#endif /* _NETGRAPH_NG_EIFACE_H_ */
