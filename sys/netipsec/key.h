/*	$FreeBSD$	*/
/*	$KAME: key.h,v 1.21 2001/07/27 03:51:30 itojun Exp $	*/

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

#ifndef _NETIPSEC_KEY_H_
#define _NETIPSEC_KEY_H_

#ifdef _KERNEL

struct secpolicy;
struct secpolicyindex;
struct secasvar;
struct sockaddr;
struct socket;
struct sadb_msg;
struct sadb_x_policy;
struct secasindex;
union sockaddr_union;
struct xformsw;

struct secpolicy *key_newsp(void);
struct secpolicy *key_allocsp(struct secpolicyindex *, u_int);
struct secpolicy *key_msg2sp(struct sadb_x_policy *, size_t, int *);
int key_sp2msg(struct secpolicy *, void *, size_t *);
void key_addref(struct secpolicy *);
void key_freesp(struct secpolicy **);
int key_spdacquire(struct secpolicy *);
int key_havesp(u_int);
void key_bumpspgen(void);
uint32_t key_getspgen(void);
uint32_t key_newreqid(void);

struct secasvar *key_allocsa(union sockaddr_union *, uint8_t, uint32_t);
struct secasvar *key_allocsa_tunnel(union sockaddr_union *,
    union sockaddr_union *, uint8_t);
struct secasvar *key_allocsa_policy(struct secpolicy *,
    const struct secasindex *, int *);
struct secasvar *key_allocsa_tcpmd5(struct secasindex *);
void key_freesav(struct secasvar **);

int key_sockaddrcmp(const struct sockaddr *, const struct sockaddr *, int);
int key_sockaddrcmp_withmask(const struct sockaddr *, const struct sockaddr *,
    size_t);

int key_register_ifnet(struct secpolicy **, u_int);
void key_unregister_ifnet(struct secpolicy **, u_int);

void key_delete_xform(const struct xformsw *);

extern u_long key_random(void);
extern void key_randomfill(void *, size_t);
extern void key_freereg(struct socket *);
extern int key_parse(struct mbuf *, struct socket *);
extern void key_init(void);
#ifdef VIMAGE
extern void key_destroy(void);
#endif
extern void key_sa_recordxfer(struct secasvar *, struct mbuf *);
uint16_t key_portfromsaddr(struct sockaddr *);
void key_porttosaddr(struct sockaddr *, uint16_t port);

#ifdef MALLOC_DECLARE
MALLOC_DECLARE(M_IPSEC_SA);
MALLOC_DECLARE(M_IPSEC_SAH);
MALLOC_DECLARE(M_IPSEC_SP);
MALLOC_DECLARE(M_IPSEC_SR);
MALLOC_DECLARE(M_IPSEC_MISC);
MALLOC_DECLARE(M_IPSEC_SAQ);
MALLOC_DECLARE(M_IPSEC_SAR);
MALLOC_DECLARE(M_IPSEC_INPCB);
#endif /* MALLOC_DECLARE */

#endif /* defined(_KERNEL) */
#endif /* _NETIPSEC_KEY_H_ */
