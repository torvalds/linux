/*	$FreeBSD$	*/
/*	$KAME: key_debug.h,v 1.10 2001/08/05 08:37:52 itojun Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
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
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _NETIPSEC_KEY_DEBUG_H_
#define _NETIPSEC_KEY_DEBUG_H_

#ifdef _KERNEL
/* debug flags */
#define KEYDEBUG_STAMP		0x00000001 /* path */
#define KEYDEBUG_DATA		0x00000002 /* data */
#define KEYDEBUG_DUMP		0x00000004 /* dump */

#define KEYDEBUG_KEY		0x00000010 /* key processing */
#define KEYDEBUG_ALG		0x00000020 /* ciph & auth algorithm */
#define KEYDEBUG_IPSEC		0x00000040 /* ipsec processing */

#define KEYDEBUG_KEY_STAMP	(KEYDEBUG_KEY | KEYDEBUG_STAMP)
#define KEYDEBUG_KEY_DATA	(KEYDEBUG_KEY | KEYDEBUG_DATA)
#define KEYDEBUG_KEY_DUMP	(KEYDEBUG_KEY | KEYDEBUG_DUMP)
#define KEYDEBUG_ALG_STAMP	(KEYDEBUG_ALG | KEYDEBUG_STAMP)
#define KEYDEBUG_ALG_DATA	(KEYDEBUG_ALG | KEYDEBUG_DATA)
#define KEYDEBUG_ALG_DUMP	(KEYDEBUG_ALG | KEYDEBUG_DUMP)
#define KEYDEBUG_IPSEC_STAMP	(KEYDEBUG_IPSEC | KEYDEBUG_STAMP)
#define KEYDEBUG_IPSEC_DATA	(KEYDEBUG_IPSEC | KEYDEBUG_DATA)
#define KEYDEBUG_IPSEC_DUMP	(KEYDEBUG_IPSEC | KEYDEBUG_DUMP)

#ifdef IPSEC_DEBUG
#define KEYDBG(lev, arg)	\
    if ((V_key_debug_level & (KEYDEBUG_ ## lev)) == (KEYDEBUG_ ## lev)) { \
	    arg;		\
    }
#else
#define	KEYDBG(lev, arg)
#endif /* !IPSEC_DEBUG */

VNET_DECLARE(uint32_t, key_debug_level);
#define	V_key_debug_level	VNET(key_debug_level)
#endif /*_KERNEL*/

struct sadb_msg;
struct sadb_ext;
extern void kdebug_sadb(struct sadb_msg *);
extern void kdebug_sadb_x_policy(struct sadb_ext *);

#ifdef _KERNEL
struct secpolicy;
struct secpolicyindex;
struct secasindex;
struct secashead;
struct secasvar;
struct secreplay;
struct mbuf;
union sockaddr_union;
const char* kdebug_secpolicy_state(u_int);
const char* kdebug_secpolicy_policy(u_int);
const char* kdebug_secpolicyindex_dir(u_int);
const char* kdebug_ipsecrequest_level(u_int);
const char* kdebug_secasindex_mode(u_int);
const char* kdebug_secasv_state(u_int);
void kdebug_secpolicy(struct secpolicy *);
void kdebug_secpolicyindex(struct secpolicyindex *, const char *);
void kdebug_secasindex(const struct secasindex *, const char *);
void kdebug_secash(struct secashead *, const char *);
void kdebug_secasv(struct secasvar *);
void kdebug_mbufhdr(const struct mbuf *);
void kdebug_mbuf(const struct mbuf *);
char *ipsec_address(const union sockaddr_union *, char *, socklen_t);
char *ipsec_sa2str(struct secasvar *, char *, size_t);
#endif /*_KERNEL*/

struct sockaddr;
extern void kdebug_sockaddr(struct sockaddr *);

extern void ipsec_hexdump(caddr_t, int);
extern void ipsec_bindump(caddr_t, int);

#endif /* _NETIPSEC_KEY_DEBUG_H_ */
