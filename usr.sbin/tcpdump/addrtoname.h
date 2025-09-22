/*	$OpenBSD: addrtoname.h,v 1.12 2018/10/22 16:12:45 kn Exp $	*/

/*
 * Copyright (c) 1990, 1992, 1993, 1994, 1995, 1996, 1997
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @(#) $Id: addrtoname.h,v 1.12 2018/10/22 16:12:45 kn Exp $ (LBL)
 */

#ifndef BYTE_ORDER
#error "No byte order defined"
#endif

#if BYTE_ORDER == BIG_ENDIAN
#define WORDS_BIGENDIAN
#endif /* BYTE_ORDER */

/* Name to address translation routines. */
extern char *linkaddr_string(const u_char *, const int);
extern char *etheraddr_string(const u_char *);
extern char *etherproto_string(u_short);
extern char *tcpport_string(u_short);
extern char *udpport_string(u_short);
extern char *ipproto_string(u_int);
extern char *getname(const u_char *);
extern char *getname6(const u_char *);
extern char *intoa(u_int32_t);

extern void init_addrtoname(u_int32_t, u_int32_t);
extern struct hnamemem *newhnamemem(void);
extern struct h6namemem *newh6namemem(void);

#define ipaddr_string(p) getname((const u_char *)(p))
#define ip6addr_string(p) getname6((const u_char *)(p))
