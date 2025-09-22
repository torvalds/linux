/*	$OpenBSD: npppd_defs.h,v 1.8 2012/09/18 13:14:08 yasuoka Exp $ */

/*-
 * Copyright (c) 2009 Internet Initiative Japan Inc.
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#ifndef NPPPD_DEFS_H
#define NPPPD_DEFS_H 1

#define NPPPD_MAX_POOLED_ADDRS		8192
#define NPPPD_USER_HASH_SIZ		1777
#define	NPPPD_GENERIC_NAME_LEN		32
#ifndef	LOG_NPPPD
#define	LOG_NPPPD			LOG_LOCAL1
#endif

#ifndef NPPPD_MAX_SERVERS
/** numbers of RADIUS servers */
#define NPPPD_MAX_SERVERS			8
#endif

#ifndef	NPPPD_TIMER_TICK_IVAL
#define	NPPPD_TIMER_TICK_IVAL 			4
#endif

/** interval time in seconds of authentication realm finalizer */
#define NPPPD_AUTH_REALM_FINALIZER_INTERVAL		300

#ifndef	NPPPD_MAX_IPCP_CONFIG
/** numbers of IPCP configurations */
#define	NPPPD_MAX_IPCP_CONFIG			1
#endif

#ifndef	NPPPD_MAX_IFACE
/** numbers of PPP concentration interfaces */
#define	NPPPD_MAX_IFACE				1
#endif

#ifndef	NPPPD_MAX_POOL
/** numbers of pool */
#define	NPPPD_MAX_POOL				1
#endif

#ifndef	NPPPD_MAX_PPTP
/** numbers of local authentication realms */
#define	NPPPD_MAX_PPTP				2
#endif

#ifndef	NPPPD_DEFAULT_AUTH_LOCAL_RELOADABLE
#define	NPPPD_DEFAULT_AUTH_LOCAL_RELOADABLE	0
#endif

/** default value of maximum PPP sessions per user */
#define	NPPPD_DEFAULT_USER_MAX_PPP	3

#ifndef	NPPPD_DEFAULT_MAX_PPP
/** default value of maximum PPP sessions in system */
#define	NPPPD_DEFAULT_MAX_PPP		8192
#endif

#ifndef	LOOPBACK_IFNAME
#define	LOOPBACK_IFNAME			"lo0"
#endif

#ifndef	countof
#define	countof(x)	(sizeof(x) / sizeof((x)[0]))
#endif

#endif
