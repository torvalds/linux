/*	$OpenBSD: npppd_pool.h,v 1.4 2012/05/08 13:15:12 yasuoka Exp $ */

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
#ifndef	NPPPD_POOL_H
#define	NPPPD_POOL_H 1

typedef struct _npppd_pool npppd_pool;

#define	ADDRESS_OK		0	/** address is assignable. */
#define	ADDRESS_RESERVED	1	/** address is reserved. */
#define	ADDRESS_BUSY		2	/** address is busy. */
#define	ADDRESS_INVALID		3	/** address is unusable. */
#define	ADDRESS_OUT_OF_POOL	4	/** address is out of pool. */

#ifdef __cplusplus
extern "C" {
#endif


int       npppd_pool_init (npppd_pool *, npppd *, const char *);
int       npppd_pool_start (npppd_pool *);
int       npppd_pool_reload (npppd_pool *);
void      npppd_pool_uninit (npppd_pool *);

int       npppd_pool_get_assignability (npppd_pool *, uint32_t, uint32_t, struct sockaddr_npppd **);
uint32_t  npppd_pool_get_dynamic (npppd_pool *, npppd_ppp *);
int       npppd_pool_assign_ip (npppd_pool *, npppd_ppp *);
void      npppd_pool_release_ip (npppd_pool *, npppd_ppp *);

#ifdef __cplusplus
}
#endif
#endif
