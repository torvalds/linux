/*	$NetBSD: netconfig.h,v 1.6 2008/04/28 20:22:54 martin Exp $	*/
/*	$FreeBSD$ */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2004 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Frank van der Linden.
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

#ifndef _NETCONFIG_H_
#define _NETCONFIG_H_

#include <sys/cdefs.h>

#define NETCONFIG	"/etc/netconfig"
#define NETPATH		"NETPATH"

struct netconfig {
	char *nc_netid;			/* Network ID */
	unsigned long nc_semantics;	/* Semantics (see below) */
	unsigned long nc_flag;		/* Flags (see below) */
	char *nc_protofmly;		/* Protocol family */
	char *nc_proto;			/* Protocol name */
	char *nc_device;		/* Network device pathname */
	unsigned long nc_nlookups;	/* Number of directory lookup libs */
	char **nc_lookups;		/* Names of the libraries */
	unsigned long nc_unused[9];	/* reserved */
};

typedef struct {
	struct netconfig **nc_head;
	struct netconfig **nc_curr;
} NCONF_HANDLE;

/*
 * nc_semantics values
 */
#define NC_TPI_CLTS	1	/* Connectionless transport */
#define NC_TPI_COTS	2	/* Connection oriented transport */
#define NC_TPI_COTS_ORD	3	/* Connection oriented, ordered transport */
#define NC_TPI_RAW	4	/* Raw connection */

/*
 * nc_flag values
 */
#define NC_NOFLAG	0x00
#define NC_VISIBLE	0x01
#define NC_BROADCAST	0x02

/*
 * nc_protofmly values
 */
#define NC_NOPROTOFMLY	"-"
#define NC_LOOPBACK	"loopback"
#define NC_INET		"inet"
#define NC_INET6	"inet6"
#define NC_IMPLINK	"implink"
#define NC_PUP		"pup"
#define NC_CHAOS	"chaos"
#define NC_NS		"ns"
#define NC_NBS		"nbs"
#define NC_ECMA		"ecma"
#define NC_DATAKIT	"datakit"
#define NC_CCITT	"ccitt"
#define NC_SNA		"sna"
#define NC_DECNET	"decnet"
#define NC_DLI		"dli"
#define NC_LAT		"lat"
#define NC_HYLINK	"hylink"
#define NC_APPLETALK	"appletalk"
#define NC_NIT		"nit"
#define NC_IEEE802	"ieee802"
#define NC_OSI		"osi"
#define NC_X25		"x25"
#define NC_OSINET	"osinet"
#define NC_GOSIP	"gosip"

/*
 * nc_proto values
 */
#define NC_NOPROTO	"-"
#define NC_TCP		"tcp"
#define NC_UDP		"udp"
#define NC_ICMP		"icmp"

__BEGIN_DECLS
void *setnetconfig(void);
struct netconfig *getnetconfig(void *);
struct netconfig *getnetconfigent(const char *);
void freenetconfigent(struct netconfig *);
int endnetconfig(void *);

void *setnetpath(void);
struct netconfig *getnetpath(void *);
int endnetpath(void *);

void nc_perror(const char *);
char *nc_sperror(void);
__END_DECLS

#endif /* _NETCONFIG_H_ */
