/*	$FreeBSD$	*/
/*	$KAME: libpfkey.h,v 1.6 2001/03/05 18:22:17 thorpej Exp $	*/

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

struct sadb_msg;
extern void pfkey_sadump(struct sadb_msg *);
extern void pfkey_spdump(struct sadb_msg *);

struct sockaddr;
struct sadb_alg;
int ipsec_check_keylen(u_int, u_int, u_int);
int ipsec_check_keylen2(u_int, u_int, u_int);
int ipsec_get_keylen(u_int, u_int, struct sadb_alg *);
u_int pfkey_set_softrate(u_int, u_int);
u_int pfkey_get_softrate(u_int);
int pfkey_send_getspi(int, u_int, u_int, struct sockaddr *, struct sockaddr *,
	u_int32_t, u_int32_t, u_int32_t, u_int32_t);
int pfkey_send_update(int, u_int, u_int, struct sockaddr *, struct sockaddr *,
	u_int32_t, u_int32_t, u_int, caddr_t, u_int, u_int, u_int, u_int,
	u_int, u_int32_t, u_int64_t, u_int64_t, u_int64_t, u_int32_t);
int pfkey_send_add(int, u_int, u_int, struct sockaddr *, struct sockaddr *,
	u_int32_t, u_int32_t, u_int, caddr_t, u_int, u_int, u_int, u_int,
	u_int, u_int32_t, u_int64_t, u_int64_t, u_int64_t, u_int32_t);
int pfkey_send_delete(int, u_int, u_int, struct sockaddr *, struct sockaddr *,
	u_int32_t);
int pfkey_send_delete_all(int, u_int, u_int, struct sockaddr *,
	struct sockaddr *);
int pfkey_send_get(int, u_int, u_int, struct sockaddr *, struct sockaddr *,
	u_int32_t);
int pfkey_send_register(int, u_int);
int pfkey_recv_register(int);
int pfkey_set_supported(struct sadb_msg *, int);
int pfkey_send_flush(int, u_int);
int pfkey_send_dump(int, u_int);
int pfkey_send_promisc_toggle(int, int);
int pfkey_send_spdadd(int, struct sockaddr *, u_int, struct sockaddr *, u_int,
	u_int, caddr_t, int, u_int32_t);
int pfkey_send_spdadd2(int, struct sockaddr *, u_int, struct sockaddr *, u_int,
	u_int, u_int64_t, u_int64_t, caddr_t, int, u_int32_t);
int pfkey_send_spdupdate(int, struct sockaddr *, u_int, struct sockaddr *,
	u_int, u_int, caddr_t, int, u_int32_t);
int pfkey_send_spdupdate2(int, struct sockaddr *, u_int, struct sockaddr *,
	u_int, u_int, u_int64_t, u_int64_t, caddr_t, int, u_int32_t);
int pfkey_send_spddelete(int, struct sockaddr *, u_int, struct sockaddr *,
	u_int, u_int, caddr_t, int, u_int32_t);
int pfkey_send_spddelete2(int, u_int32_t);
int pfkey_send_spdget(int, u_int32_t);
int pfkey_send_spdsetidx(int, struct sockaddr *, u_int, struct sockaddr *,
	u_int, u_int, caddr_t, int, u_int32_t);
int pfkey_send_spdflush(int);
int pfkey_send_spddump(int);

int pfkey_open(void);
void pfkey_close(int);
struct sadb_msg *pfkey_recv(int);
int pfkey_send(int, struct sadb_msg *, int);
int pfkey_align(struct sadb_msg *, caddr_t *);
int pfkey_check(caddr_t *);
