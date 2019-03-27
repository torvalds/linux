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

#include <sys/param.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <string.h>
#include <termios.h>

#include "layer.h"
#include "ua.h"
#include "mbuf.h"
#include "log.h"
#include "defs.h"
#include "timer.h"
#include "fsm.h"
#include "iplist.h"
#include "lqr.h"
#include "hdlc.h"
#include "throughput.h"
#include "slcompress.h"
#include "ncpaddr.h"
#include "ipcp.h"
#include "filter.h"
#include "descriptor.h"
#include "lcp.h"
#include "ccp.h"
#include "link.h"
#include "mp.h"
#ifndef NORADIUS
#include "radius.h"
#endif
#include "ipv6cp.h"
#include "ncp.h"
#include "bundle.h"
#include "async.h"
#include "physical.h"
#include "proto.h"

static void FsmSendConfigReq(struct fsm *);
static void FsmSendTerminateReq(struct fsm *);
static void FsmInitRestartCounter(struct fsm *, int);

typedef void (recvfn)(struct fsm *, struct fsmheader *, struct mbuf *);
static recvfn FsmRecvConfigReq, FsmRecvConfigAck, FsmRecvConfigNak,
              FsmRecvConfigRej, FsmRecvTermReq, FsmRecvTermAck,
              FsmRecvCodeRej, FsmRecvProtoRej, FsmRecvEchoReq,
              FsmRecvEchoRep, FsmRecvDiscReq, FsmRecvIdent,
              FsmRecvTimeRemain, FsmRecvResetReq, FsmRecvResetAck;

static const struct fsmcodedesc {
  recvfn *recv;
  unsigned check_reqid : 1;
  unsigned inc_reqid : 1;
  const char *name;
} FsmCodes[] = {
  { FsmRecvConfigReq, 0, 0, "ConfigReq"    },
  { FsmRecvConfigAck, 1, 1, "ConfigAck"    },
  { FsmRecvConfigNak, 1, 1, "ConfigNak"    },
  { FsmRecvConfigRej, 1, 1, "ConfigRej"    },
  { FsmRecvTermReq,   0, 0, "TerminateReq" },
  { FsmRecvTermAck,   1, 1, "TerminateAck" },
  { FsmRecvCodeRej,   0, 0, "CodeRej"      },
  { FsmRecvProtoRej,  0, 0, "ProtocolRej"  },
  { FsmRecvEchoReq,   0, 0, "EchoRequest"  },
  { FsmRecvEchoRep,   0, 0, "EchoReply"    },
  { FsmRecvDiscReq,   0, 0, "DiscardReq"   },
  { FsmRecvIdent,     0, 1, "Ident"        },
  { FsmRecvTimeRemain,0, 0, "TimeRemain"   },
  { FsmRecvResetReq,  0, 0, "ResetReq"     },
  { FsmRecvResetAck,  0, 1, "ResetAck"     }
};

static const char *
Code2Nam(u_int code)
{
  if (code == 0 || code > sizeof FsmCodes / sizeof FsmCodes[0])
    return "Unknown";
  return FsmCodes[code-1].name;
}

const char *
State2Nam(u_int state)
{
  static const char * const StateNames[] = {
    "Initial", "Starting", "Closed", "Stopped", "Closing", "Stopping",
    "Req-Sent", "Ack-Rcvd", "Ack-Sent", "Opened",
  };

  if (state >= sizeof StateNames / sizeof StateNames[0])
    return "unknown";
  return StateNames[state];
}

static void
StoppedTimeout(void *v)
{
  struct fsm *fp = (struct fsm *)v;

  log_Printf(fp->LogLevel, "%s: Stopped timer expired\n", fp->link->name);
  if (fp->OpenTimer.state == TIMER_RUNNING) {
    log_Printf(LogWARN, "%s: %s: aborting open delay due to stopped timer\n",
              fp->link->name, fp->name);
    timer_Stop(&fp->OpenTimer);
  }
  if (fp->state == ST_STOPPED)
    fsm2initial(fp);
}

void
fsm_Init(struct fsm *fp, const char *name, u_short proto, int mincode,
         int maxcode, int LogLevel, struct bundle *bundle,
         struct link *l, const struct fsm_parent *parent,
         struct fsm_callbacks *fn, const char * const timer_names[3])
{
  fp->name = name;
  fp->proto = proto;
  fp->min_code = mincode;
  fp->max_code = maxcode;
  fp->state = fp->min_code > CODE_TERMACK ? ST_OPENED : ST_INITIAL;
  fp->reqid = 1;
  fp->restart = 1;
  fp->more.reqs = fp->more.naks = fp->more.rejs = 3;
  memset(&fp->FsmTimer, '\0', sizeof fp->FsmTimer);
  memset(&fp->OpenTimer, '\0', sizeof fp->OpenTimer);
  memset(&fp->StoppedTimer, '\0', sizeof fp->StoppedTimer);
  fp->LogLevel = LogLevel;
  fp->link = l;
  fp->bundle = bundle;
  fp->parent = parent;
  fp->fn = fn;
  fp->FsmTimer.name = timer_names[0];
  fp->OpenTimer.name = timer_names[1];
  fp->StoppedTimer.name = timer_names[2];
}

static void
NewState(struct fsm *fp, int new)
{
  log_Printf(fp->LogLevel, "%s: State change %s --> %s\n",
             fp->link->name, State2Nam(fp->state), State2Nam(new));
  if (fp->state == ST_STOPPED && fp->StoppedTimer.state == TIMER_RUNNING)
    timer_Stop(&fp->StoppedTimer);
  fp->state = new;
  if ((new >= ST_INITIAL && new <= ST_STOPPED) || (new == ST_OPENED)) {
    timer_Stop(&fp->FsmTimer);
    if (new == ST_STOPPED && fp->StoppedTimer.load) {
      timer_Stop(&fp->StoppedTimer);
      fp->StoppedTimer.func = StoppedTimeout;
      fp->StoppedTimer.arg = (void *) fp;
      timer_Start(&fp->StoppedTimer);
    }
  }
}

void
fsm_Output(struct fsm *fp, u_int code, u_int id, u_char *ptr, unsigned count,
           int mtype)
{
  int plen;
  struct fsmheader lh;
  struct mbuf *bp;

  if (log_IsKept(fp->LogLevel)) {
    log_Printf(fp->LogLevel, "%s: Send%s(%d) state = %s\n",
              fp->link->name, Code2Nam(code), id, State2Nam(fp->state));
    switch (code) {
      case CODE_CONFIGREQ:
      case CODE_CONFIGACK:
      case CODE_CONFIGREJ:
      case CODE_CONFIGNAK:
        (*fp->fn->DecodeConfig)(fp, ptr, ptr + count, MODE_NOP, NULL);
        if (count < sizeof(struct fsm_opt_hdr))
          log_Printf(fp->LogLevel, "  [EMPTY]\n");
        break;
    }
  }

  plen = sizeof(struct fsmheader) + count;
  lh.code = code;
  lh.id = id;
  lh.length = htons(plen);
  bp = m_get(plen, mtype);
  memcpy(MBUF_CTOP(bp), &lh, sizeof(struct fsmheader));
  if (count)
    memcpy(MBUF_CTOP(bp) + sizeof(struct fsmheader), ptr, count);
  log_DumpBp(LogDEBUG, "fsm_Output", bp);
  link_PushPacket(fp->link, bp, fp->bundle, LINK_QUEUES(fp->link) - 1,
                  fp->proto);

  if (code == CODE_CONFIGREJ)
    lcp_SendIdentification(&fp->link->lcp);
}

static void
FsmOpenNow(void *v)
{
  struct fsm *fp = (struct fsm *)v;

  timer_Stop(&fp->OpenTimer);
  if (fp->state <= ST_STOPPED) {
    if (fp->state != ST_STARTING) {
      /*
       * In practice, we're only here in ST_STOPPED (when delaying the
       * first config request) or ST_CLOSED (when openmode == 0).
       *
       * The ST_STOPPED bit is breaking the RFC already :-(
       *
       * According to the RFC (1661) state transition table, a TLS isn't
       * required for an Open event when state == Closed, but the RFC
       * must be wrong as TLS hasn't yet been called (since the last TLF)
       * ie, Initial gets an `Up' event, Closing gets a RTA etc.
       */
      (*fp->fn->LayerStart)(fp);
      (*fp->parent->LayerStart)(fp->parent->object, fp);
    }
    FsmInitRestartCounter(fp, FSM_REQ_TIMER);
    FsmSendConfigReq(fp);
    NewState(fp, ST_REQSENT);
  }
}

void
fsm_Open(struct fsm *fp)
{
  switch (fp->state) {
  case ST_INITIAL:
    NewState(fp, ST_STARTING);
    (*fp->fn->LayerStart)(fp);
    (*fp->parent->LayerStart)(fp->parent->object, fp);
    break;
  case ST_CLOSED:
    if (fp->open_mode == OPEN_PASSIVE) {
      NewState(fp, ST_STOPPED);		/* XXX: This is a hack ! */
    } else if (fp->open_mode > 0) {
      if (fp->open_mode > 1)
        log_Printf(LogPHASE, "%s: Entering STOPPED state for %d seconds\n",
                  fp->link->name, fp->open_mode);
      NewState(fp, ST_STOPPED);		/* XXX: This is a not-so-bad hack ! */
      timer_Stop(&fp->OpenTimer);
      fp->OpenTimer.load = fp->open_mode * SECTICKS;
      fp->OpenTimer.func = FsmOpenNow;
      fp->OpenTimer.arg = (void *)fp;
      timer_Start(&fp->OpenTimer);
    } else
      FsmOpenNow(fp);
    break;
  case ST_STOPPED:		/* XXX: restart option */
  case ST_REQSENT:
  case ST_ACKRCVD:
  case ST_ACKSENT:
  case ST_OPENED:		/* XXX: restart option */
    break;
  case ST_CLOSING:		/* XXX: restart option */
  case ST_STOPPING:		/* XXX: restart option */
    NewState(fp, ST_STOPPING);
    break;
  }
}

void
fsm_Up(struct fsm *fp)
{
  switch (fp->state) {
  case ST_INITIAL:
    log_Printf(fp->LogLevel, "FSM: Using \"%s\" as a transport\n",
              fp->link->name);
    NewState(fp, ST_CLOSED);
    break;
  case ST_STARTING:
    FsmInitRestartCounter(fp, FSM_REQ_TIMER);
    FsmSendConfigReq(fp);
    NewState(fp, ST_REQSENT);
    break;
  default:
    log_Printf(fp->LogLevel, "%s: Oops, Up at %s\n",
              fp->link->name, State2Nam(fp->state));
    break;
  }
}

void
fsm_Down(struct fsm *fp)
{
  switch (fp->state) {
  case ST_CLOSED:
    NewState(fp, ST_INITIAL);
    break;
  case ST_CLOSING:
    /* This TLF contradicts the RFC (1661), which ``misses it out'' ! */
    (*fp->fn->LayerFinish)(fp);
    NewState(fp, ST_INITIAL);
    (*fp->parent->LayerFinish)(fp->parent->object, fp);
    break;
  case ST_STOPPED:
    NewState(fp, ST_STARTING);
    (*fp->fn->LayerStart)(fp);
    (*fp->parent->LayerStart)(fp->parent->object, fp);
    break;
  case ST_STOPPING:
  case ST_REQSENT:
  case ST_ACKRCVD:
  case ST_ACKSENT:
    NewState(fp, ST_STARTING);
    break;
  case ST_OPENED:
    (*fp->fn->LayerDown)(fp);
    NewState(fp, ST_STARTING);
    (*fp->parent->LayerDown)(fp->parent->object, fp);
    break;
  }
}

void
fsm_Close(struct fsm *fp)
{
  switch (fp->state) {
  case ST_STARTING:
    (*fp->fn->LayerFinish)(fp);
    NewState(fp, ST_INITIAL);
    (*fp->parent->LayerFinish)(fp->parent->object, fp);
    break;
  case ST_STOPPED:
    NewState(fp, ST_CLOSED);
    break;
  case ST_STOPPING:
    NewState(fp, ST_CLOSING);
    break;
  case ST_OPENED:
    (*fp->fn->LayerDown)(fp);
    if (fp->state == ST_OPENED) {
      FsmInitRestartCounter(fp, FSM_TRM_TIMER);
      FsmSendTerminateReq(fp);
      NewState(fp, ST_CLOSING);
      (*fp->parent->LayerDown)(fp->parent->object, fp);
    }
    break;
  case ST_REQSENT:
  case ST_ACKRCVD:
  case ST_ACKSENT:
    FsmInitRestartCounter(fp, FSM_TRM_TIMER);
    FsmSendTerminateReq(fp);
    NewState(fp, ST_CLOSING);
    break;
  }
}

/*
 *	Send functions
 */
static void
FsmSendConfigReq(struct fsm *fp)
{
  if (fp->more.reqs-- > 0 && fp->restart-- > 0) {
    (*fp->fn->SendConfigReq)(fp);
    timer_Start(&fp->FsmTimer);		/* Start restart timer */
  } else {
    if (fp->more.reqs < 0)
      log_Printf(LogPHASE, "%s: Too many %s REQs sent - abandoning "
                 "negotiation\n", fp->link->name, fp->name);
    lcp_SendIdentification(&fp->link->lcp);
    fsm_Close(fp);
  }
}

static void
FsmSendTerminateReq(struct fsm *fp)
{
  fsm_Output(fp, CODE_TERMREQ, fp->reqid, NULL, 0, MB_UNKNOWN);
  (*fp->fn->SentTerminateReq)(fp);
  timer_Start(&fp->FsmTimer);	/* Start restart timer */
  fp->restart--;		/* Decrement restart counter */
}

/*
 *	Timeout actions
 */
static void
FsmTimeout(void *v)
{
  struct fsm *fp = (struct fsm *)v;

  if (fp->restart) {
    switch (fp->state) {
    case ST_CLOSING:
    case ST_STOPPING:
      FsmSendTerminateReq(fp);
      break;
    case ST_REQSENT:
    case ST_ACKSENT:
      FsmSendConfigReq(fp);
      break;
    case ST_ACKRCVD:
      FsmSendConfigReq(fp);
      NewState(fp, ST_REQSENT);
      break;
    }
    timer_Start(&fp->FsmTimer);
  } else {
    switch (fp->state) {
    case ST_CLOSING:
      (*fp->fn->LayerFinish)(fp);
      NewState(fp, ST_CLOSED);
      (*fp->parent->LayerFinish)(fp->parent->object, fp);
      break;
    case ST_STOPPING:
      (*fp->fn->LayerFinish)(fp);
      NewState(fp, ST_STOPPED);
      (*fp->parent->LayerFinish)(fp->parent->object, fp);
      break;
    case ST_REQSENT:		/* XXX: 3p */
    case ST_ACKSENT:
    case ST_ACKRCVD:
      (*fp->fn->LayerFinish)(fp);
      NewState(fp, ST_STOPPED);
      (*fp->parent->LayerFinish)(fp->parent->object, fp);
      break;
    }
  }
}

static void
FsmInitRestartCounter(struct fsm *fp, int what)
{
  timer_Stop(&fp->FsmTimer);
  fp->FsmTimer.func = FsmTimeout;
  fp->FsmTimer.arg = (void *)fp;
  (*fp->fn->InitRestartCounter)(fp, what);
}

/*
 * Actions when receive packets
 */
static void
FsmRecvConfigReq(struct fsm *fp, struct fsmheader *lhp, struct mbuf *bp)
/* RCR */
{
  struct fsm_decode dec;
  int plen, flen;
  int ackaction = 0;
  u_char *cp;

  bp = m_pullup(bp);
  plen = m_length(bp);
  flen = ntohs(lhp->length) - sizeof *lhp;
  if (plen < flen) {
    log_Printf(LogWARN, "%s: FsmRecvConfigReq: plen (%d) < flen (%d)\n",
               fp->link->name, plen, flen);
    m_freem(bp);
    return;
  }

  /* Some things must be done before we Decode the packet */
  switch (fp->state) {
  case ST_OPENED:
    (*fp->fn->LayerDown)(fp);
  }

  dec.ackend = dec.ack;
  dec.nakend = dec.nak;
  dec.rejend = dec.rej;
  cp = MBUF_CTOP(bp);
  (*fp->fn->DecodeConfig)(fp, cp, cp + flen, MODE_REQ, &dec);
  if (flen < (int)sizeof(struct fsm_opt_hdr))
    log_Printf(fp->LogLevel, "  [EMPTY]\n");

  if (dec.nakend == dec.nak && dec.rejend == dec.rej)
    ackaction = 1;

  /* Check and process easy case */
  switch (fp->state) {
  case ST_INITIAL:
    if (fp->proto == PROTO_CCP && fp->link->lcp.fsm.state == ST_OPENED) {
      /*
       * ccp_SetOpenMode() leaves us in initial if we're disabling
       * & denying everything.
       */
      bp = m_prepend(bp, lhp, sizeof *lhp, 2);
      bp = proto_Prepend(bp, fp->proto, 0, 0);
      bp = m_pullup(bp);
      lcp_SendProtoRej(&fp->link->lcp, MBUF_CTOP(bp), bp->m_len);
      m_freem(bp);
      return;
    }
    /* Drop through */
  case ST_STARTING:
    log_Printf(fp->LogLevel, "%s: Oops, RCR in %s.\n",
              fp->link->name, State2Nam(fp->state));
    m_freem(bp);
    return;
  case ST_CLOSED:
    (*fp->fn->SendTerminateAck)(fp, lhp->id);
    m_freem(bp);
    return;
  case ST_CLOSING:
    log_Printf(fp->LogLevel, "%s: Error: Got ConfigReq while state = %s\n",
              fp->link->name, State2Nam(fp->state));
  case ST_STOPPING:
    m_freem(bp);
    return;
  case ST_STOPPED:
    FsmInitRestartCounter(fp, FSM_REQ_TIMER);
    /* Drop through */
  case ST_OPENED:
    FsmSendConfigReq(fp);
    break;
  }

  if (dec.rejend != dec.rej)
    fsm_Output(fp, CODE_CONFIGREJ, lhp->id, dec.rej, dec.rejend - dec.rej,
               MB_UNKNOWN);
  if (dec.nakend != dec.nak)
    fsm_Output(fp, CODE_CONFIGNAK, lhp->id, dec.nak, dec.nakend - dec.nak,
               MB_UNKNOWN);
  if (ackaction)
    fsm_Output(fp, CODE_CONFIGACK, lhp->id, dec.ack, dec.ackend - dec.ack,
               MB_UNKNOWN);

  switch (fp->state) {
  case ST_STOPPED:
      /*
       * According to the RFC (1661) state transition table, a TLS isn't
       * required for a RCR when state == ST_STOPPED, but the RFC
       * must be wrong as TLS hasn't yet been called (since the last TLF)
       */
    (*fp->fn->LayerStart)(fp);
    (*fp->parent->LayerStart)(fp->parent->object, fp);
    /* FALLTHROUGH */

  case ST_OPENED:
    if (ackaction)
      NewState(fp, ST_ACKSENT);
    else
      NewState(fp, ST_REQSENT);
    (*fp->parent->LayerDown)(fp->parent->object, fp);
    break;
  case ST_REQSENT:
    if (ackaction)
      NewState(fp, ST_ACKSENT);
    break;
  case ST_ACKRCVD:
    if (ackaction) {
      NewState(fp, ST_OPENED);
      if ((*fp->fn->LayerUp)(fp))
        (*fp->parent->LayerUp)(fp->parent->object, fp);
      else {
        (*fp->fn->LayerDown)(fp);
        FsmInitRestartCounter(fp, FSM_TRM_TIMER);
        FsmSendTerminateReq(fp);
        NewState(fp, ST_CLOSING);
        lcp_SendIdentification(&fp->link->lcp);
      }
    }
    break;
  case ST_ACKSENT:
    if (!ackaction)
      NewState(fp, ST_REQSENT);
    break;
  }
  m_freem(bp);

  if (dec.rejend != dec.rej && --fp->more.rejs <= 0) {
    log_Printf(LogPHASE, "%s: Too many %s REJs sent - abandoning negotiation\n",
               fp->link->name, fp->name);
    lcp_SendIdentification(&fp->link->lcp);
    fsm_Close(fp);
  }

  if (dec.nakend != dec.nak && --fp->more.naks <= 0) {
    log_Printf(LogPHASE, "%s: Too many %s NAKs sent - abandoning negotiation\n",
               fp->link->name, fp->name);
    lcp_SendIdentification(&fp->link->lcp);
    fsm_Close(fp);
  }
}

static void
FsmRecvConfigAck(struct fsm *fp, struct fsmheader *lhp, struct mbuf *bp)
/* RCA */
{
  struct fsm_decode dec;
  int plen, flen;
  u_char *cp;

  plen = m_length(bp);
  flen = ntohs(lhp->length) - sizeof *lhp;
  if (plen < flen) {
    m_freem(bp);
    return;
  }

  bp = m_pullup(bp);
  dec.ackend = dec.ack;
  dec.nakend = dec.nak;
  dec.rejend = dec.rej;
  cp = MBUF_CTOP(bp);
  (*fp->fn->DecodeConfig)(fp, cp, cp + flen, MODE_ACK, &dec);
  if (flen < (int)sizeof(struct fsm_opt_hdr))
    log_Printf(fp->LogLevel, "  [EMPTY]\n");

  switch (fp->state) {
    case ST_CLOSED:
    case ST_STOPPED:
    (*fp->fn->SendTerminateAck)(fp, lhp->id);
    break;
  case ST_CLOSING:
  case ST_STOPPING:
    break;
  case ST_REQSENT:
    FsmInitRestartCounter(fp, FSM_REQ_TIMER);
    NewState(fp, ST_ACKRCVD);
    break;
  case ST_ACKRCVD:
    FsmSendConfigReq(fp);
    NewState(fp, ST_REQSENT);
    break;
  case ST_ACKSENT:
    FsmInitRestartCounter(fp, FSM_REQ_TIMER);
    NewState(fp, ST_OPENED);
    if ((*fp->fn->LayerUp)(fp))
      (*fp->parent->LayerUp)(fp->parent->object, fp);
    else {
      (*fp->fn->LayerDown)(fp);
      FsmInitRestartCounter(fp, FSM_TRM_TIMER);
      FsmSendTerminateReq(fp);
      NewState(fp, ST_CLOSING);
      lcp_SendIdentification(&fp->link->lcp);
    }
    break;
  case ST_OPENED:
    (*fp->fn->LayerDown)(fp);
    FsmSendConfigReq(fp);
    NewState(fp, ST_REQSENT);
    (*fp->parent->LayerDown)(fp->parent->object, fp);
    break;
  }
  m_freem(bp);
}

static void
FsmRecvConfigNak(struct fsm *fp, struct fsmheader *lhp, struct mbuf *bp)
/* RCN */
{
  struct fsm_decode dec;
  int plen, flen;
  u_char *cp;

  plen = m_length(bp);
  flen = ntohs(lhp->length) - sizeof *lhp;
  if (plen < flen) {
    m_freem(bp);
    return;
  }

  /*
   * Check and process easy case
   */
  switch (fp->state) {
  case ST_INITIAL:
  case ST_STARTING:
    log_Printf(fp->LogLevel, "%s: Oops, RCN in %s.\n",
              fp->link->name, State2Nam(fp->state));
    m_freem(bp);
    return;
  case ST_CLOSED:
  case ST_STOPPED:
    (*fp->fn->SendTerminateAck)(fp, lhp->id);
    m_freem(bp);
    return;
  case ST_CLOSING:
  case ST_STOPPING:
    m_freem(bp);
    return;
  }

  bp = m_pullup(bp);
  dec.ackend = dec.ack;
  dec.nakend = dec.nak;
  dec.rejend = dec.rej;
  cp = MBUF_CTOP(bp);
  (*fp->fn->DecodeConfig)(fp, cp, cp + flen, MODE_NAK, &dec);
  if (flen < (int)sizeof(struct fsm_opt_hdr))
    log_Printf(fp->LogLevel, "  [EMPTY]\n");

  switch (fp->state) {
  case ST_REQSENT:
  case ST_ACKSENT:
    FsmInitRestartCounter(fp, FSM_REQ_TIMER);
    FsmSendConfigReq(fp);
    break;
  case ST_OPENED:
    (*fp->fn->LayerDown)(fp);
    FsmSendConfigReq(fp);
    NewState(fp, ST_REQSENT);
    (*fp->parent->LayerDown)(fp->parent->object, fp);
    break;
  case ST_ACKRCVD:
    FsmSendConfigReq(fp);
    NewState(fp, ST_REQSENT);
    break;
  }

  m_freem(bp);
}

static void
FsmRecvTermReq(struct fsm *fp, struct fsmheader *lhp, struct mbuf *bp)
/* RTR */
{
  switch (fp->state) {
  case ST_INITIAL:
  case ST_STARTING:
    log_Printf(fp->LogLevel, "%s: Oops, RTR in %s\n",
              fp->link->name, State2Nam(fp->state));
    break;
  case ST_CLOSED:
  case ST_STOPPED:
  case ST_CLOSING:
  case ST_STOPPING:
  case ST_REQSENT:
    (*fp->fn->SendTerminateAck)(fp, lhp->id);
    break;
  case ST_ACKRCVD:
  case ST_ACKSENT:
    (*fp->fn->SendTerminateAck)(fp, lhp->id);
    NewState(fp, ST_REQSENT);
    break;
  case ST_OPENED:
    (*fp->fn->LayerDown)(fp);
    (*fp->fn->SendTerminateAck)(fp, lhp->id);
    FsmInitRestartCounter(fp, FSM_TRM_TIMER);
    timer_Start(&fp->FsmTimer);			/* Start restart timer */
    fp->restart = 0;
    NewState(fp, ST_STOPPING);
    (*fp->parent->LayerDown)(fp->parent->object, fp);
    /* A delayed ST_STOPPED is now scheduled */
    break;
  }
  m_freem(bp);
}

static void
FsmRecvTermAck(struct fsm *fp, struct fsmheader *lhp __unused, struct mbuf *bp)
/* RTA */
{
  switch (fp->state) {
  case ST_CLOSING:
    (*fp->fn->LayerFinish)(fp);
    NewState(fp, ST_CLOSED);
    (*fp->parent->LayerFinish)(fp->parent->object, fp);
    break;
  case ST_STOPPING:
    (*fp->fn->LayerFinish)(fp);
    NewState(fp, ST_STOPPED);
    (*fp->parent->LayerFinish)(fp->parent->object, fp);
    break;
  case ST_ACKRCVD:
    NewState(fp, ST_REQSENT);
    break;
  case ST_OPENED:
    (*fp->fn->LayerDown)(fp);
    FsmSendConfigReq(fp);
    NewState(fp, ST_REQSENT);
    (*fp->parent->LayerDown)(fp->parent->object, fp);
    break;
  }
  m_freem(bp);
}

static void
FsmRecvConfigRej(struct fsm *fp, struct fsmheader *lhp, struct mbuf *bp)
/* RCJ */
{
  struct fsm_decode dec;
  size_t plen;
  int flen;
  u_char *cp;

  plen = m_length(bp);
  flen = ntohs(lhp->length) - sizeof *lhp;
  if ((int)plen < flen) {
    m_freem(bp);
    return;
  }

  lcp_SendIdentification(&fp->link->lcp);

  /*
   * Check and process easy case
   */
  switch (fp->state) {
  case ST_INITIAL:
  case ST_STARTING:
    log_Printf(fp->LogLevel, "%s: Oops, RCJ in %s.\n",
              fp->link->name, State2Nam(fp->state));
    m_freem(bp);
    return;
  case ST_CLOSED:
  case ST_STOPPED:
    (*fp->fn->SendTerminateAck)(fp, lhp->id);
    m_freem(bp);
    return;
  case ST_CLOSING:
  case ST_STOPPING:
    m_freem(bp);
    return;
  }

  bp = m_pullup(bp);
  dec.ackend = dec.ack;
  dec.nakend = dec.nak;
  dec.rejend = dec.rej;
  cp = MBUF_CTOP(bp);
  (*fp->fn->DecodeConfig)(fp, cp, cp + flen, MODE_REJ, &dec);
  if (flen < (int)sizeof(struct fsm_opt_hdr))
    log_Printf(fp->LogLevel, "  [EMPTY]\n");

  switch (fp->state) {
  case ST_REQSENT:
  case ST_ACKSENT:
    FsmInitRestartCounter(fp, FSM_REQ_TIMER);
    FsmSendConfigReq(fp);
    break;
  case ST_OPENED:
    (*fp->fn->LayerDown)(fp);
    FsmSendConfigReq(fp);
    NewState(fp, ST_REQSENT);
    (*fp->parent->LayerDown)(fp->parent->object, fp);
    break;
  case ST_ACKRCVD:
    FsmSendConfigReq(fp);
    NewState(fp, ST_REQSENT);
    break;
  }
  m_freem(bp);
}

static void
FsmRecvCodeRej(struct fsm *fp __unused, struct fsmheader *lhp __unused,
	       struct mbuf *bp)
{
  m_freem(bp);
}

static void
FsmRecvProtoRej(struct fsm *fp, struct fsmheader *lhp __unused, struct mbuf *bp)
{
  struct physical *p = link2physical(fp->link);
  u_short proto;

  if (m_length(bp) < 2) {
    m_freem(bp);
    return;
  }
  bp = mbuf_Read(bp, &proto, 2);
  proto = ntohs(proto);
  log_Printf(fp->LogLevel, "%s: -- Protocol 0x%04x (%s) was rejected!\n",
            fp->link->name, proto, hdlc_Protocol2Nam(proto));

  switch (proto) {
  case PROTO_LQR:
    if (p)
      lqr_Stop(p, LQM_LQR);
    else
      log_Printf(LogERROR, "%s: FsmRecvProtoRej: Not a physical link !\n",
                fp->link->name);
    break;
  case PROTO_CCP:
    if (fp->proto == PROTO_LCP) {
      fp = &fp->link->ccp.fsm;
      /* Despite the RFC (1661), don't do an out-of-place TLF */
      /* (*fp->fn->LayerFinish)(fp); */
      switch (fp->state) {
      case ST_CLOSED:
      case ST_CLOSING:
        NewState(fp, ST_CLOSED);
        break;
      default:
        NewState(fp, ST_STOPPED);
        break;
      }
      /* See above */
      /* (*fp->parent->LayerFinish)(fp->parent->object, fp); */
    }
    break;
  case PROTO_IPCP:
    if (fp->proto == PROTO_LCP) {
      log_Printf(LogPHASE, "%s: IPCP protocol reject closes IPCP !\n",
                fp->link->name);
      fsm_Close(&fp->bundle->ncp.ipcp.fsm);
    }
    break;
#ifndef NOINET6
  case PROTO_IPV6CP:
    if (fp->proto == PROTO_LCP) {
      log_Printf(LogPHASE, "%s: IPV6CP protocol reject closes IPV6CP !\n",
                fp->link->name);
      fsm_Close(&fp->bundle->ncp.ipv6cp.fsm);
    }
    break;
#endif
  case PROTO_MP:
    if (fp->proto == PROTO_LCP) {
      struct lcp *lcp = fsm2lcp(fp);

      if (lcp->want_mrru && lcp->his_mrru) {
        log_Printf(LogPHASE, "%s: MP protocol reject is fatal !\n",
                  fp->link->name);
        fsm_Close(fp);
      }
    }
    break;
  }
  m_freem(bp);
}

static void
FsmRecvEchoReq(struct fsm *fp, struct fsmheader *lhp, struct mbuf *bp)
{
  struct lcp *lcp = fsm2lcp(fp);
  u_char *cp;
  u_int32_t magic;

  bp = m_pullup(bp);
  m_settype(bp, MB_ECHOIN);

  if (lcp && ntohs(lhp->length) - sizeof *lhp >= 4) {
    cp = MBUF_CTOP(bp);
    ua_ntohl(cp, &magic);
    if (magic != lcp->his_magic) {
      log_Printf(fp->LogLevel, "%s: RecvEchoReq: magic 0x%08lx is wrong,"
                 " expecting 0x%08lx\n", fp->link->name, (u_long)magic,
                 (u_long)lcp->his_magic);
      /* XXX: We should send terminate request */
    }
    if (fp->state == ST_OPENED) {
      ua_htonl(&lcp->want_magic, cp);		/* local magic */
      fsm_Output(fp, CODE_ECHOREP, lhp->id, cp,
                 ntohs(lhp->length) - sizeof *lhp, MB_ECHOOUT);
    }
  }
  m_freem(bp);
}

static void
FsmRecvEchoRep(struct fsm *fp, struct fsmheader *lhp __unused, struct mbuf *bp)
{
  if (fsm2lcp(fp))
    bp = lqr_RecvEcho(fp, bp);

  m_freem(bp);
}

static void
FsmRecvDiscReq(struct fsm *fp __unused, struct fsmheader *lhp __unused,
	       struct mbuf *bp)
{
  m_freem(bp);
}

static void
FsmRecvIdent(struct fsm *fp, struct fsmheader *lhp, struct mbuf *bp)
{
  u_int32_t magic;
  u_short len;
  u_char *cp;

  len = ntohs(lhp->length) - sizeof *lhp;
  if (len >= 4) {
    bp = m_pullup(m_append(bp, "", 1));
    cp = MBUF_CTOP(bp);
    ua_ntohl(cp, &magic);
    if (magic != fp->link->lcp.his_magic)
      log_Printf(fp->LogLevel, "%s: RecvIdent: magic 0x%08lx is wrong,"
                 " expecting 0x%08lx\n", fp->link->name, (u_long)magic,
                 (u_long)fp->link->lcp.his_magic);
    cp[len] = '\0';
    lcp_RecvIdentification(&fp->link->lcp, cp + 4);
  }
  m_freem(bp);
}

static void
FsmRecvTimeRemain(struct fsm *fp __unused, struct fsmheader *lhp __unused,
		  struct mbuf *bp)
{
  m_freem(bp);
}

static void
FsmRecvResetReq(struct fsm *fp, struct fsmheader *lhp, struct mbuf *bp)
{
  if ((*fp->fn->RecvResetReq)(fp)) {
    /*
     * All sendable compressed packets are queued in the first (lowest
     * priority) modem output queue.... dump 'em to the priority queue
     * so that they arrive at the peer before our ResetAck.
     */
    link_SequenceQueue(fp->link);
    fsm_Output(fp, CODE_RESETACK, lhp->id, NULL, 0, MB_CCPOUT);
  }
  m_freem(bp);
}

static void
FsmRecvResetAck(struct fsm *fp, struct fsmheader *lhp, struct mbuf *bp)
{
  (*fp->fn->RecvResetAck)(fp, lhp->id);
  m_freem(bp);
}

void
fsm_Input(struct fsm *fp, struct mbuf *bp)
{
  size_t len;
  struct fsmheader lh;
  const struct fsmcodedesc *codep;

  len = m_length(bp);
  if (len < sizeof(struct fsmheader)) {
    m_freem(bp);
    return;
  }
  bp = mbuf_Read(bp, &lh, sizeof lh);

  if (ntohs(lh.length) > len) {
    log_Printf(LogWARN, "%s: Oops: Got %zu bytes but %d byte payload "
               "- dropped\n", fp->link->name, len, (int)ntohs(lh.length));
    m_freem(bp);
    return;
  }

  if (lh.code < fp->min_code || lh.code > fp->max_code ||
      lh.code > sizeof FsmCodes / sizeof *FsmCodes) {
    /*
     * Use a private id.  This is really a response-type packet, but we
     * MUST send a unique id for each REQ....
     */
    static u_char id;

    bp = m_prepend(bp, &lh, sizeof lh, 0);
    bp = m_pullup(bp);
    fsm_Output(fp, CODE_CODEREJ, id++, MBUF_CTOP(bp), bp->m_len, MB_UNKNOWN);
    m_freem(bp);
    return;
  }

  codep = FsmCodes + lh.code - 1;
  if (lh.id != fp->reqid && codep->check_reqid &&
      Enabled(fp->bundle, OPT_IDCHECK)) {
    log_Printf(fp->LogLevel, "%s: Recv%s(%d), dropped (expected %d)\n",
               fp->link->name, codep->name, lh.id, fp->reqid);
    return;
  }

  log_Printf(fp->LogLevel, "%s: Recv%s(%d) state = %s\n",
             fp->link->name, codep->name, lh.id, State2Nam(fp->state));

  if (codep->inc_reqid && (lh.id == fp->reqid ||
      (!Enabled(fp->bundle, OPT_IDCHECK) && codep->check_reqid)))
    fp->reqid++;	/* That's the end of that ``exchange''.... */

  (*codep->recv)(fp, &lh, bp);
}

int
fsm_NullRecvResetReq(struct fsm *fp)
{
  log_Printf(fp->LogLevel, "%s: Oops - received unexpected reset req\n",
            fp->link->name);
  return 1;
}

void
fsm_NullRecvResetAck(struct fsm *fp, u_char id __unused)
{
  log_Printf(fp->LogLevel, "%s: Oops - received unexpected reset ack\n",
            fp->link->name);
}

void
fsm_Reopen(struct fsm *fp)
{
  if (fp->state == ST_OPENED) {
    (*fp->fn->LayerDown)(fp);
    FsmInitRestartCounter(fp, FSM_REQ_TIMER);
    FsmSendConfigReq(fp);
    NewState(fp, ST_REQSENT);
    (*fp->parent->LayerDown)(fp->parent->object, fp);
  }
}

void
fsm2initial(struct fsm *fp)
{
  timer_Stop(&fp->FsmTimer);
  timer_Stop(&fp->OpenTimer);
  timer_Stop(&fp->StoppedTimer);
  if (fp->state == ST_STOPPED)
    fsm_Close(fp);
  if (fp->state > ST_INITIAL)
    fsm_Down(fp);
  if (fp->state > ST_INITIAL)
    fsm_Close(fp);
}

struct fsm_opt *
fsm_readopt(u_char **cp)
{
  struct fsm_opt *o = (struct fsm_opt *)*cp;

  if (o->hdr.len < sizeof(struct fsm_opt_hdr)) {
    log_Printf(LogERROR, "Bad option length %d (out of phase?)\n", o->hdr.len);
    return NULL;
  }

  *cp += o->hdr.len;

  if (o->hdr.len > sizeof(struct fsm_opt)) {
    log_Printf(LogERROR, "Warning: Truncating option length from %d to %d\n",
               o->hdr.len, (int)sizeof(struct fsm_opt));
    o->hdr.len = sizeof(struct fsm_opt);
  }

  return o;
}

static int
fsm_opt(u_char *opt, int optlen, const struct fsm_opt *o)
{
  unsigned cplen = o->hdr.len;

  if (optlen < (int)sizeof(struct fsm_opt_hdr))
    optlen = 0;

  if ((int)cplen > optlen) {
    log_Printf(LogERROR, "Can't REJ length %d - trunating to %d\n",
      cplen, optlen);
    cplen = optlen;
  }
  memcpy(opt, o, cplen);
  if (cplen)
    opt[1] = cplen;

  return cplen;
}

void
fsm_rej(struct fsm_decode *dec, const struct fsm_opt *o)
{
  if (!dec)
    return;
  dec->rejend += fsm_opt(dec->rejend, FSM_OPTLEN - (dec->rejend - dec->rej), o);
}

void
fsm_ack(struct fsm_decode *dec, const struct fsm_opt *o)
{
  if (!dec)
    return;
  dec->ackend += fsm_opt(dec->ackend, FSM_OPTLEN - (dec->ackend - dec->ack), o);
}

void
fsm_nak(struct fsm_decode *dec, const struct fsm_opt *o)
{
  if (!dec)
    return;
  dec->nakend += fsm_opt(dec->nakend, FSM_OPTLEN - (dec->nakend - dec->nak), o);
}

void
fsm_opt_normalise(struct fsm_decode *dec)
{
  if (dec->rejend != dec->rej) {
    /* rejects are preferred */
    dec->ackend = dec->ack;
    dec->nakend = dec->nak;
  } else if (dec->nakend != dec->nak)
    /* then NAKs */
    dec->ackend = dec->ack;
}
