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

/*
 *  State of machine
 */
#define	ST_INITIAL	0
#define	ST_STARTING	1
#define	ST_CLOSED	2
#define	ST_STOPPED	3
#define	ST_CLOSING	4
#define	ST_STOPPING	5
#define	ST_REQSENT	6
#define	ST_ACKRCVD	7
#define	ST_ACKSENT	8
#define	ST_OPENED	9

#define	ST_MAX		10
#define	ST_UNDEF	-1

#define	MODE_REQ	0
#define	MODE_NAK	1
#define	MODE_REJ	2
#define	MODE_NOP	3
#define	MODE_ACK	4	/* pseudo mode for ccp negotiations */

#define	OPEN_PASSIVE	-1

#define FSM_REQ_TIMER	1
#define FSM_TRM_TIMER	2

#define FSM_OPTLEN	100

struct fsm;

struct fsm_retry {
  u_int timeout;                             /* FSM retry frequency */
  u_int maxreq;                              /* Max Config REQ retries */
  u_int maxtrm;                              /* Max Term REQ retries */
};

struct fsm_decode {
  u_char ack[FSM_OPTLEN], *ackend;
  u_char nak[FSM_OPTLEN], *nakend;
  u_char rej[FSM_OPTLEN], *rejend;
};

struct fsm_callbacks {
  int (*LayerUp)(struct fsm *);                 /* Layer is now up (tlu) */
  void (*LayerDown)(struct fsm *);              /* About to come down (tld) */
  void (*LayerStart)(struct fsm *);             /* Layer about to start (tls) */
  void (*LayerFinish)(struct fsm *);            /* Layer now down (tlf) */
  void (*InitRestartCounter)(struct fsm *, int);/* Set fsm timer load */
  void (*SendConfigReq)(struct fsm *);          /* Send REQ please */
  void (*SentTerminateReq)(struct fsm *);       /* Term REQ just sent */
  void (*SendTerminateAck)(struct fsm *, u_char); /* Send Term ACK please */
  void (*DecodeConfig)(struct fsm *, u_char *, u_char *, int,
                       struct fsm_decode *);    /* Deal with incoming data */
  int (*RecvResetReq)(struct fsm *fp);          /* Reset output */
  void (*RecvResetAck)(struct fsm *fp, u_char); /* Reset input */
};

struct fsm_parent {
  void (*LayerStart) (void *, struct fsm *);         /* tls */
  void (*LayerUp) (void *, struct fsm *);            /* tlu */
  void (*LayerDown) (void *, struct fsm *);          /* tld */
  void (*LayerFinish) (void *, struct fsm *);        /* tlf */
  void *object;
};

struct link;
struct bundle;

struct fsm {
  const char *name;		/* Name of protocol */
  u_short proto;		/* Protocol number */
  u_short min_code;
  u_short max_code;
  int open_mode;		/* Delay before config REQ (-1 forever) */
  unsigned state;		/* State of the machine */
  u_char reqid;			/* Next request id */
  int restart;			/* Restart counter value */

  struct {
    int reqs;			/* Max config REQs before a close() */
    int naks;			/* Max config NAKs before a close() */
    int rejs;			/* Max config REJs before a close() */
  } more;

  struct pppTimer FsmTimer;	/* Restart Timer */
  struct pppTimer OpenTimer;	/* Delay before opening */

  /*
   * This timer times the ST_STOPPED state out after the given value
   * (specified via "set stopped ...").  Although this isn't specified in the
   * rfc, the rfc *does* say that "the application may use higher level
   * timers to avoid deadlock". The StoppedTimer takes effect when the other
   * side ABENDs rather than going into ST_ACKSENT (and sending the ACK),
   * causing ppp to time out and drop into ST_STOPPED.  At this point,
   * nothing will change this state :-(
   */
  struct pppTimer StoppedTimer;
  int LogLevel;

  /* The link layer active with this FSM (may be our bundle below) */
  struct link *link;

  /* Our high-level link */
  struct bundle *bundle;

  const struct fsm_parent *parent;
  const struct fsm_callbacks *fn;
};

struct fsmheader {
  u_char code;			/* Request code */
  u_char id;			/* Identification */
  u_short length;		/* Length of packet */
};

#define	CODE_CONFIGREQ	1
#define	CODE_CONFIGACK	2
#define	CODE_CONFIGNAK	3
#define	CODE_CONFIGREJ	4
#define	CODE_TERMREQ	5
#define	CODE_TERMACK	6
#define	CODE_CODEREJ	7
#define	CODE_PROTOREJ	8
#define	CODE_ECHOREQ	9	/* Used in LCP */
#define	CODE_ECHOREP	10	/* Used in LCP */
#define	CODE_DISCREQ	11
#define	CODE_IDENT	12	/* Used in LCP Extension */
#define	CODE_TIMEREM	13	/* Used in LCP Extension */
#define	CODE_RESETREQ	14	/* Used in CCP */
#define	CODE_RESETACK	15	/* Used in CCP */

struct fsm_opt_hdr {
  u_char id;
  u_char len;
} __packed;

#define MAX_FSM_OPT_LEN 52
struct fsm_opt {
  struct fsm_opt_hdr hdr;
  u_char data[MAX_FSM_OPT_LEN-2];
};

#define INC_FSM_OPT(ty, length, o)                      \
  do {                                                  \
    (o)->hdr.id = (ty);                                 \
    (o)->hdr.len = (length);                            \
    (o) = (struct fsm_opt *)((u_char *)(o) + (length)); \
  } while (0)


extern void fsm_Init(struct fsm *, const char *, u_short, int, int, int,
                     struct bundle *, struct link *, const  struct fsm_parent *,
                     struct fsm_callbacks *, const char * const [3]);
extern void fsm_Output(struct fsm *, u_int, u_int, u_char *, unsigned, int);
extern void fsm_Open(struct fsm *);
extern void fsm_Up(struct fsm *);
extern void fsm_Down(struct fsm *);
extern void fsm_Input(struct fsm *, struct mbuf *);
extern void fsm_Close(struct fsm *);
extern int fsm_NullRecvResetReq(struct fsm *);
extern void fsm_NullRecvResetAck(struct fsm *, u_char);
extern void fsm_Reopen(struct fsm *);
extern void fsm2initial(struct fsm *);
extern const char *State2Nam(u_int);
extern struct fsm_opt *fsm_readopt(u_char **);
extern void fsm_rej(struct fsm_decode *, const struct fsm_opt *);
extern void fsm_ack(struct fsm_decode *, const struct fsm_opt *);
extern void fsm_nak(struct fsm_decode *, const struct fsm_opt *);
extern void fsm_opt_normalise(struct fsm_decode *);
