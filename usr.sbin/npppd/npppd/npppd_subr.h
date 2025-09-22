/*	$OpenBSD: npppd_subr.h,v 1.4 2013/03/14 10:21:07 mpi Exp $ */

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
#ifndef	NPPPD_SUBR_H
#define	NPPPD_SUBR_H 1

#ifdef __cplusplus
extern "C" {
#endif

int   load_resolv_conf (struct in_addr *, struct in_addr *);
int   in_host_route_add (struct in_addr *, struct in_addr *, const char *, int);
int   in_host_route_delete (struct in_addr *, struct in_addr *);
int   in_route_add (struct in_addr *, struct in_addr *, struct in_addr *, const char *, uint32_t, int);
int   in_route_delete (struct in_addr *, struct in_addr *, struct in_addr *, uint32_t);
int   ip_is_idle_packet (const struct ip *, int);
void  in_addr_range_add_route (struct in_addr_range *);
void  in_addr_range_delete_route (struct in_addr_range *);
int   adjust_tcp_mss(u_char *, int, int);

#ifdef __cplusplus
}
#endif

#endif
