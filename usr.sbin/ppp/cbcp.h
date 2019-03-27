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

struct mbuf;
struct physical;
struct datalink;

/* fsm states */
#define CBCP_CLOSED	(0)	/* Not in use */
#define CBCP_STOPPED	(1)	/* Waiting for a REQ */
#define CBCP_REQSENT	(2)	/* Waiting for a RESP */
#define CBCP_RESPSENT	(3)	/* Waiting for an ACK */
#define CBCP_ACKSENT	(4)	/* Waiting for an LCP Term REQ */

struct cbcpcfg {
  u_char delay;
  char phone[SCRIPT_LEN];
  long fsmretry;
};

struct cbcp {
  unsigned required : 1;	/* Are we gonna call back ? */
  struct physical *p;		/* On this physical link */
  struct {
    u_char type;		/* cbcp_data::type (none/me/him/list) */
    u_char delay;		/* How long to delay */
    char phone[SCRIPT_LEN];	/* What to dial */

    int state;			/* Our FSM state */
    u_char id;			/* Our FSM ID */
    u_char restart;		/* FSM Send again ? */
    struct pppTimer timer;	/* Resend last option */
  } fsm;
};

extern void cbcp_Init(struct cbcp *, struct physical *);
extern void cbcp_Up(struct cbcp *);
extern struct mbuf *cbcp_Input(struct bundle *, struct link *, struct mbuf *);
extern void cbcp_Down(struct cbcp *);
extern void cbcp_ReceiveTerminateReq(struct physical *);
