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
struct bundle;
struct cmdargs;
struct datalink;

#define ENDDISC_NULL	0
#define ENDDISC_LOCAL	1
#define ENDDISC_IP	2
#define ENDDISC_MAC	3
#define ENDDISC_MAGIC	4
#define ENDDISC_PSN	5

#define MP_LINKSENT	0	/* We attached the link to another ppp */
#define MP_UP		1	/* We've started MP */
#define MP_ADDED	2	/* We've added the link to our MP */
#define MP_FAILED	3	/* No go */

#define MPSERVER_CONNECTED	0
#define MPSERVER_LISTENING	1
#define MPSERVER_FAILED		2

struct enddisc {
  u_char class;
  char address[50];
  int len;
};

struct peerid {
  struct enddisc enddisc;	/* Peers endpoint discriminator */
  char authname[AUTHLEN];	/* Peers name (authenticated) */
};

struct mpserver {
  struct fdescriptor desc;
  int fd;			/* listen()ing or connect()ing here */
  struct sockaddr_un socket;	/* On this socket */

  struct {
    struct datalink *dl;	/* Send this datalink */
  } send;			/* (in UpdateSet()) */
};

struct mp {
  struct link link;

  unsigned active : 1;
  unsigned peer_is12bit : 1;	/* 12/24bit seq nos */
  unsigned local_is12bit : 1;
  u_short peer_mrru;
  u_short local_mrru;

  struct peerid peer;		/* Who are we talking to */
  struct mpserver server;	/* Our ``sharing'' socket */

  struct {
    u_int32_t seq;		/* next outgoing seq */
    int link;			/* Next link to send on */
    int af;			/* Next address family to send */
  } out;

  struct {
    u_int32_t min_in;		/* minimum received incoming seq */
    u_int32_t next_in;		/* next incoming seq to process */
  } seq;

  struct {
    u_short mrru;		/* Max Reconstructed Receive Unit */
    unsigned shortseq : 2;	/* I want short Sequence Numbers */
    unsigned negenddisc : 2;	/* I want an endpoint discriminator */
    struct enddisc enddisc;	/* endpoint discriminator */
    struct {
      int min;			/* Lowest percent of bundle->bandwidth */
      int max;			/* Highest percent of bundle->bandwidth out */
      int period;		/* link->throughput sample period */
    } autoload;
  } cfg;

  struct mbuf *inbufs;		/* Received fragments */
  struct fsm_parent fsmp;	/* Our callback functions */
  struct bundle *bundle;	/* Parent */
};

struct mp_link {
  u_int32_t seq;		/* 12 or 24 bit incoming seq */
  unsigned bandwidth;		/* Our link bandwidth (or zero) */
};

struct mp_header {
  unsigned begin : 1;
  unsigned end : 1;
  u_int32_t seq;
};

#define descriptor2mpserver(d) \
  ((d)->type == MPSERVER_DESCRIPTOR ? (struct mpserver *)(d) : NULL)
#define mpserver_IsOpen(s) ((s)->fd != -1)

extern void peerid_Init(struct peerid *);
extern int peerid_Equal(const struct peerid *, const struct peerid *);
extern void mpserver_Init(struct mpserver *);
extern int mpserver_Open(struct mpserver *, struct peerid *);
extern void mpserver_Close(struct mpserver *);
extern void mp_Init(struct mp *, struct bundle *);
extern void mp_linkInit(struct mp_link *);
extern int mp_Up(struct mp *, struct datalink *);
extern void mp_Down(struct mp *);
extern struct mbuf *mp_Input(struct bundle *, struct link *, struct mbuf *);
extern int mp_FillPhysicalQueues(struct bundle *);
extern int mp_SetDatalinkBandwidth(struct cmdargs const *);
extern int mp_ShowStatus(struct cmdargs const *);
extern const char *mp_Enddisc(u_char, const char *, size_t);
extern int mp_SetEnddisc(struct cmdargs const *);
extern void mp_LinkLost(struct mp *, struct datalink *);
extern void mp_RestartAutoloadTimer(struct mp *);
extern void mp_CheckAutoloadTimer(struct mp *);
extern void mp_StopAutoloadTimer(struct mp *);
extern size_t mp_QueueLen(struct mp *);
