/*	$FreeBSD$	*/
/*	$KAME: config.h,v 1.8 2003/06/17 08:26:22 itojun Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 1995, 1996, 1997, 1998, and 1999 WIDE Project.
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

extern struct ifinfo *getconfig(struct ifinfo *);
extern int rm_ifinfo(struct ifinfo *);
extern int rm_ifinfo_index(int);
extern int rm_rainfo(struct rainfo *);
extern int loadconfig_ifname(char *);
extern int loadconfig_index(int);
extern void delete_prefix(struct prefix *);
extern void invalidate_prefix(struct prefix *);
extern void update_prefix(struct prefix *);
extern void make_prefix(struct rainfo *, int, struct in6_addr *, int);
extern void make_packet(struct rainfo *);
extern void get_prefix(struct rainfo *);

/*
 * it is highly unlikely to have 100 prefix information options,
 * so it should be okay to limit it
 */
#define MAXPREFIX	100
#define MAXROUTE	100
#define MAXRDNSSENT	100
#define MAXDNSSLENT	100
