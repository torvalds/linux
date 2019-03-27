/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998 Brian Somers <brian@Awfulhak.org>
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

struct ifa_msghdr;

struct iface_addr {
  unsigned system : 1;		/* System alias ? */
  struct ncprange ifa;		/* local address/mask */
  struct ncpaddr peer;		/* peer address */
};

struct iface {
  char *name;			/* Interface name (malloc'd) */
  char *descr;			/* Interface description (malloc'd) */
  int index;			/* Interface index */
  int flags;			/* Interface flags (IFF_*) */
  unsigned long mtu;		/* struct tuninfo MTU */

  unsigned addrs;		/* How many in_addr's */
  struct iface_addr *addr;	/* Array of addresses (malloc'd) */
};

#define IFACE_CLEAR_ALL		0	/* Nuke 'em all */
#define IFACE_CLEAR_ALIASES	1	/* Leave the NCP address */

#define IFACE_ADD_LAST		0	/* Just another alias */
#define IFACE_ADD_FIRST		1	/* The IPCP address */
#define IFACE_FORCE_ADD		2	/* OR'd with IFACE_ADD_{FIRST,LAST} */

#define IFACE_SYSTEM		4	/* Set/clear SYSTEM entries */

extern struct iface *iface_Create(const char *name);
extern void iface_Clear(struct iface *, struct ncp *, int, int);
extern int iface_Name(struct iface *, const char *);
extern int iface_Descr(struct cmdargs const *);
extern int iface_Add(struct iface *, struct ncp *, const struct ncprange *,
                     const struct ncpaddr *, int);
extern int iface_Delete(struct iface *, struct ncp *, const struct ncpaddr *);
extern int iface_Show(struct cmdargs const *);
extern int iface_SetFlags(const char *, int);
extern int iface_ClearFlags(const char *, int);
extern void iface_Free(struct iface *);
extern void iface_Destroy(struct iface *);
extern void iface_ParseHdr(struct ifa_msghdr *, struct sockaddr *[RTAX_MAX]);
