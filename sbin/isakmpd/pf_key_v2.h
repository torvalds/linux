/* $OpenBSD: pf_key_v2.h,v 1.13 2006/05/29 10:17:59 markus Exp $	 */
/* $EOM: pf_key_v2.h,v 1.4 2000/12/04 04:46:35 angelos Exp $	 */

/*
 * Copyright (c) 1999 Niklas Hallqvist.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This code was written under funding by Ericsson Radio Systems.
 */

#ifndef _PF_KEY_V2_H_
#define _PF_KEY_V2_H_

#include <sys/types.h>
#include <sys/queue.h>

struct proto;
struct sa;
struct sockaddr;
struct kernel_sa;

extern int	pf_key_v2_socket;

extern void     pf_key_v2_connection_check(char *);
extern int      pf_key_v2_delete_spi(struct sa *, struct proto *, int);
extern int      pf_key_v2_enable_sa(struct sa *, struct sa *);
extern int	pf_key_v2_disable_sa(struct sa *, int);
extern int	pf_key_v2_enable_spi(in_addr_t, in_addr_t, in_addr_t,
    in_addr_t, u_int8_t *, u_int8_t, in_addr_t);
extern struct sa_kinfo *pf_key_v2_get_kernel_sa(u_int8_t *, size_t, u_int8_t,
    struct sockaddr *);
extern u_int8_t *pf_key_v2_get_spi(size_t *, u_int8_t, struct sockaddr *,
    struct sockaddr *, u_int32_t);
extern int	pf_key_v2_group_spis(struct sa *, struct proto *,
    struct proto *, int);
extern void     pf_key_v2_handler(int);
extern int      pf_key_v2_open(void);
extern int      pf_key_v2_set_spi(struct sa *, struct proto *, int,
    struct sa *);

#endif				/* _PF_KEY_V2_H_ */
