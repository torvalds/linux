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
 *
 *	$KAME: in6_ifattach.h,v 1.14 2001/02/08 12:48:39 jinmei Exp $
 * $FreeBSD$
 */

#ifndef _NETINET6_IN6_IFATTACH_H_
#define _NETINET6_IN6_IFATTACH_H_

#ifdef _KERNEL
void in6_ifattach(struct ifnet *, struct ifnet *);
void in6_ifattach_destroy(void);
void in6_ifdetach(struct ifnet *);
void in6_ifdetach_destroy(struct ifnet *);
int in6_get_tmpifid(struct ifnet *, u_int8_t *, const u_int8_t *, int);
void in6_tmpaddrtimer(void *);
int in6_get_hw_ifid(struct ifnet *, struct in6_addr *);
int in6_nigroup(struct ifnet *, const char *, int, struct in6_addr *);
int in6_nigroup_oldmcprefix(struct ifnet *, const char *, int, struct in6_addr *);
#endif /* _KERNEL */

#endif /* _NETINET6_IN6_IFATTACH_H_ */
