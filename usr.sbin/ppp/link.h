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
 *
 */


#define PHYSICAL_LINK	1
#define LOGICAL_LINK	2

#define NPROTOSTAT 13

struct bundle;
struct prompt;
struct cmdargs;

struct link {
  int type;                               /* _LINK type */
  const char *name;                       /* Points to datalink::name */
  int len;                                /* full size of parent struct */
  struct {
    unsigned gather : 1;                  /* Gather statistics ourself ? */
    struct pppThroughput total;           /* Link throughput statistics */
    struct pppThroughput *parent;         /* MP link throughput statistics */
  } stats;
  struct mqueue Queue[2];                 /* Our output queue of mbufs */

  u_long proto_in[NPROTOSTAT];            /* outgoing protocol stats */
  u_long proto_out[NPROTOSTAT];           /* incoming protocol stats */

  struct lcp lcp;                         /* Our line control FSM */
  struct ccp ccp;                         /* Our compression FSM */

  struct layer const *layer[LAYER_MAX];   /* i/o layers */
  int nlayers;
};

#define LINK_QUEUES(link) (sizeof (link)->Queue / sizeof (link)->Queue[0])
#define LINK_HIGHQ(link) ((link)->Queue + LINK_QUEUES(link) - 1)

extern void link_SequenceQueue(struct link *);
extern void link_DeleteQueue(struct link *);
extern size_t link_QueueLen(struct link *);
extern size_t link_QueueBytes(struct link *);
extern void link_PendingLowPriorityData(struct link *, size_t *, size_t *);
extern struct mbuf *link_Dequeue(struct link *);

extern void link_PushPacket(struct link *, struct mbuf *, struct bundle *,
                            int, u_short);
extern void link_PullPacket(struct link *, char *, size_t, struct bundle *);
extern int link_Stack(struct link *, struct layer *);
extern void link_EmptyStack(struct link *);

#define PROTO_IN  1                       /* third arg to link_ProtocolRecord */
#define PROTO_OUT 2
extern void link_ProtocolRecord(struct link *, u_short, int);
extern void link_ReportProtocolStatus(struct link *, struct prompt *);
extern int link_ShowLayers(struct cmdargs const *);
