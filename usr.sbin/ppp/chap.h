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

struct mbuf;
struct physical;

#define	CHAP_CHALLENGE	1
#define	CHAP_RESPONSE	2
#define	CHAP_SUCCESS	3
#define	CHAP_FAILURE	4

struct chap {
  struct fdescriptor desc;
  struct {
    pid_t pid;
    int fd;
    struct {
      char ptr[AUTHLEN * 2 + 3];	/* Allow for \r\n at the end (- NUL) */
      int len;
    } buf;
  } child;
  struct authinfo auth;
  struct {
    u_char local[CHAPCHALLENGELEN + AUTHLEN];	/* I invented this one */
    u_char peer[CHAPCHALLENGELEN + AUTHLEN];	/* Peer gave us this one */
  } challenge;
#ifndef NODES
  unsigned NTRespSent : 1;		/* Our last response */
  int peertries;
  u_char authresponse[CHAPAUTHRESPONSELEN];	/* CHAP 81 response */
#endif
};

#define descriptor2chap(d) \
  ((d)->type == CHAP_DESCRIPTOR ? (struct chap *)(d) : NULL)
#define auth2chap(a) \
  ((struct chap *)((char *)a - (uintptr_t)&((struct chap *)0)->auth))

struct MSCHAPv2_resp {		/* rfc2759 */
  char PeerChallenge[16];
  char Reserved[8];
  char NTResponse[24];
  char Flags;
};

extern void chap_Init(struct chap *, struct physical *);
extern void chap_ReInit(struct chap *);
extern struct mbuf *chap_Input(struct bundle *, struct link *, struct mbuf *);
