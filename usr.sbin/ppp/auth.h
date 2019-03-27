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

struct physical;
struct bundle;
struct authinfo;
typedef void (*auth_func)(struct authinfo *);

struct authinfo {
  struct {
    auth_func req;
    auth_func success;
    auth_func failure;
  } fn;
  struct {
    struct fsmheader hdr;
    char name[AUTHLEN];
  } in;
  struct pppTimer authtimer;
  int retry;
  int id;
  struct physical *physical;
  struct {
    struct fsm_retry fsm;	/* How often/frequently to resend requests */
  } cfg;
};

#define auth_Failure(a) (*(a)->fn.failure)(a)
#define auth_Success(a) (*(a)->fn.success)(a)

extern const char *Auth2Nam(u_short, u_char);
extern void auth_Init(struct authinfo *, struct physical *,
                      auth_func, auth_func, auth_func);
extern void auth_StopTimer(struct authinfo *);
extern void auth_StartReq(struct authinfo *);
extern int auth_Validate(struct bundle *, const char *, const char *);
extern char *auth_GetSecret(const char *, size_t);
extern int auth_SetPhoneList(const char *, char *, int);
extern int auth_Select(struct bundle *, const char *);
extern struct mbuf *auth_ReadHeader(struct authinfo *, struct mbuf *);
extern struct mbuf *auth_ReadName(struct authinfo *, struct mbuf *, size_t);
