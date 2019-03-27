/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1996 - 2001 Brian Somers <brian@Awfulhak.org>
 *          based on work by Toshiharu OHNO <tony-o@iij.ad.jp>
 *                           Internet Initiative Japan, Inc (IIJ)
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
 *
 * $FreeBSD$
 */

struct bundle;
struct cmdargs;
struct rt_msghdr;
struct sockaddr;

#define ROUTE_STATIC		0x0000
#define ROUTE_DSTMYADDR		0x0001
#define ROUTE_DSTMYADDR6	0x0002
#define ROUTE_DSTHISADDR	0x0004
#define ROUTE_DSTHISADDR6	0x0008
#define ROUTE_DSTDNS0		0x0010
#define ROUTE_DSTDNS1		0x0020
#define ROUTE_DSTANY		0x0040
#define ROUTE_GWHISADDR		0x0080	/* May be ORd with DST_* */
#define ROUTE_GWHISADDR6	0x0100	/* May be ORd with DST_* */

struct sticky_route {
  int type;				/* ROUTE_* value (not _STATIC) */
  struct sticky_route *next;		/* next in list */

  struct ncprange dst;
  struct ncpaddr gw;
};

extern int GetIfIndex(char *);
extern int route_Show(struct cmdargs const *);
extern void route_IfDelete(struct bundle *, int);
extern void route_UpdateMTU(struct bundle *);
extern const char *Index2Nam(int);
extern void route_Change(struct bundle *, struct sticky_route *,
                         const struct ncpaddr *, const struct ncpaddr *);
extern void route_Add(struct sticky_route **, int, const struct ncprange *,
                      const struct ncpaddr *);
extern void route_Delete(struct sticky_route **, int, const struct ncprange *);
extern void route_DeleteAll(struct sticky_route **);
extern void route_Clean(struct bundle *, struct sticky_route *);
extern void route_ShowSticky(struct prompt *, struct sticky_route *,
                             const char *, int);
extern void route_ParseHdr(struct rt_msghdr *, struct sockaddr *[RTAX_MAX]);
extern int rt_Set(struct bundle *, int, const struct ncprange *,
                  const struct ncpaddr *, int, int);
extern void rt_Update(struct bundle *, const struct sockaddr *,
                      const struct sockaddr *, const struct sockaddr *,
                      const struct sockaddr *, const struct sockaddr *);
