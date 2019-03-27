/*
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 1999 Internet Business Solutions Ltd., Switzerland
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

#define	MPPE_POLICY_ALLOWED	1
#define	MPPE_POLICY_REQUIRED	2

#define	MPPE_TYPE_40BIT		2
#define	MPPE_TYPE_128BIT	4

#define	RPI_DEFAULT		1
#define	RPI_PID			2
#define	RPI_IFNUM		3
#define	RPI_TUNNUM		4

struct radius {
  struct fdescriptor desc;	/* We're a sort of (selectable) fdescriptor */
  struct {
    int fd;			/* We're selecting on this */
    struct rad_handle *rad;	/* Using this to talk to our lib */
    struct pppTimer timer;	/* for this long */
    struct authinfo *auth;	/* Tell this about success/failure */
  } cx;
  unsigned valid : 1;           /* Is this structure valid ? */
  unsigned vj : 1;              /* FRAMED Compression */
  struct in_addr ip;            /* FRAMED IP */
  struct in_addr mask;          /* FRAMED Netmask */
  unsigned long mtu;            /* FRAMED MTU */
  unsigned long sessiontime;    /* Session-Timeout */
  char *filterid;		/* FRAMED Filter Id */
  struct sticky_route *routes;  /* FRAMED Routes */
  char *msrepstr;		/* MS-CHAP2-Response */
  char *repstr;			/* Reply-Message */
  char *errstr;			/* Error-Message */
#ifndef NOINET6
  uint8_t *ipv6prefix;		/* FRAMED IPv6 Prefix */
  struct sticky_route *ipv6routes;  /* FRAMED IPv6 Routes */
#endif
  struct {
    int policy;			/* MPPE_POLICY_* */
    int types;			/* MPPE_TYPE_*BIT bitmask */
    char *recvkey;
    size_t recvkeylen;
    char *sendkey;
    size_t sendkeylen;
  } mppe;
  struct {
    char file[PATH_MAX];	/* Radius config file */
  } cfg;
  struct {
    struct pppTimer timer;	/* for this long */
    int interval;
  } alive;
  short unsigned int port_id_type;
};

struct radacct {
  struct radius *rad_parent;	/* "Parent" struct radius stored in bundle */
  char user_name[AUTHLEN];	/* Session User-Name */
  char session_id[256];		/* Unique session ID */
  char multi_session_id[51];	/* Unique MP session ID */
  int  authentic;		/* How the session has been authenticated */
  u_short proto;		/* Protocol number */
  union {
    struct {
      struct in_addr addr;
      struct in_addr mask;
    } ip;
#ifndef NOINET6
    struct {
      u_char ifid[8];
    } ipv6;
#endif
  } peer;
};

#define descriptor2radius(d) \
  ((d)->type == RADIUS_DESCRIPTOR ? (struct radius *)(d) : NULL)

struct bundle;

extern void radius_Flush(struct radius *);
extern void radius_Init(struct radius *);
extern void radius_Destroy(struct radius *);

extern void radius_Show(struct radius *, struct prompt *);
extern void radius_StartTimer(struct bundle *);
extern void radius_StopTimer(struct radius *);
extern int radius_Authenticate(struct radius *, struct authinfo *,
                               const char *, const char *, int,
                               const char *, int);
extern void radius_Account_Set_Ip(struct radacct *, struct in_addr *,
				  struct in_addr *);
#ifndef NOINET6
extern void radius_Account_Set_Ipv6(struct radacct *, u_char *);
#endif
extern void radius_Account(struct radius *, struct radacct *,
                           struct datalink *, int, struct pppThroughput *);

/* An (int) parameter to radius_Account, from radlib.h */
#if !defined(RAD_START)
#define RAD_START	1
#define RAD_STOP	2
#endif

#define RAD_ALIVE	3

/* Get address from NAS pool */
#define RADIUS_INADDR_POOL	htonl(0xfffffffe)	/* 255.255.255.254 */
