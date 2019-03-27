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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>	/* memcpy() on some archs */
#include <termios.h>

#include "layer.h"
#include "defs.h"
#include "command.h"
#include "mbuf.h"
#include "log.h"
#include "timer.h"
#include "fsm.h"
#include "proto.h"
#include "pred.h"
#include "deflate.h"
#include "throughput.h"
#include "iplist.h"
#include "slcompress.h"
#include "lqr.h"
#include "hdlc.h"
#include "lcp.h"
#include "ccp.h"
#include "ncpaddr.h"
#include "ipcp.h"
#include "filter.h"
#include "descriptor.h"
#include "prompt.h"
#include "link.h"
#include "mp.h"
#include "async.h"
#include "physical.h"
#ifndef NORADIUS
#include "radius.h"
#endif
#ifndef NODES
#include "mppe.h"
#endif
#include "ipv6cp.h"
#include "ncp.h"
#include "bundle.h"

static void CcpSendConfigReq(struct fsm *);
static void CcpSentTerminateReq(struct fsm *);
static void CcpSendTerminateAck(struct fsm *, u_char);
static void CcpDecodeConfig(struct fsm *, u_char *, u_char *, int,
                            struct fsm_decode *);
static void CcpLayerStart(struct fsm *);
static void CcpLayerFinish(struct fsm *);
static int CcpLayerUp(struct fsm *);
static void CcpLayerDown(struct fsm *);
static void CcpInitRestartCounter(struct fsm *, int);
static int CcpRecvResetReq(struct fsm *);
static void CcpRecvResetAck(struct fsm *, u_char);

static struct fsm_callbacks ccp_Callbacks = {
  CcpLayerUp,
  CcpLayerDown,
  CcpLayerStart,
  CcpLayerFinish,
  CcpInitRestartCounter,
  CcpSendConfigReq,
  CcpSentTerminateReq,
  CcpSendTerminateAck,
  CcpDecodeConfig,
  CcpRecvResetReq,
  CcpRecvResetAck
};

static const char * const ccp_TimerNames[] =
  {"CCP restart", "CCP openmode", "CCP stopped"};

static const char *
protoname(int proto)
{
  static char const * const cftypes[] = {
    /* Check out the latest ``Compression Control Protocol'' rfc (1962) */
    "OUI",		/* 0: OUI */
    "PRED1",		/* 1: Predictor type 1 */
    "PRED2",		/* 2: Predictor type 2 */
    "PUDDLE",		/* 3: Puddle Jumber */
    NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL,
    "HWPPC",		/* 16: Hewlett-Packard PPC */
    "STAC",		/* 17: Stac Electronics LZS (rfc1974) */
    "MPPE",		/* 18: Microsoft PPC (rfc2118) and */
			/*     Microsoft PPE (draft-ietf-pppext-mppe) */
    "GAND",		/* 19: Gandalf FZA (rfc1993) */
    "V42BIS",		/* 20: ARG->DATA.42bis compression */
    "BSD",		/* 21: BSD LZW Compress */
    NULL,
    "LZS-DCP",		/* 23: LZS-DCP Compression Protocol (rfc1967) */
    "MAGNALINK/DEFLATE",/* 24: Magnalink Variable Resource (rfc1975) */
			/* 24: Deflate (according to pppd-2.3.*) */
    "DCE",		/* 25: Data Circuit-Terminating Equip (rfc1976) */
    "DEFLATE",		/* 26: Deflate (rfc1979) */
  };

  if (proto < 0 || (unsigned)proto > sizeof cftypes / sizeof *cftypes ||
      cftypes[proto] == NULL) {
    if (proto == -1)
      return "none";
    return HexStr(proto, NULL, 0);
  }

  return cftypes[proto];
}

/* We support these algorithms, and Req them in the given order */
static const struct ccp_algorithm * const algorithm[] = {
  &DeflateAlgorithm,
  &Pred1Algorithm,
  &PppdDeflateAlgorithm
#ifndef NODES
  , &MPPEAlgorithm
#endif
};

#define NALGORITHMS (sizeof algorithm/sizeof algorithm[0])

int
ccp_ReportStatus(struct cmdargs const *arg)
{
  struct ccp_opt **o;
  struct link *l;
  struct ccp *ccp;
  int f;

  l = command_ChooseLink(arg);
  ccp = &l->ccp;

  prompt_Printf(arg->prompt, "%s: %s [%s]\n", l->name, ccp->fsm.name,
                State2Nam(ccp->fsm.state));
  if (ccp->fsm.state == ST_OPENED) {
    prompt_Printf(arg->prompt, " My protocol = %s, His protocol = %s\n",
                  protoname(ccp->my_proto), protoname(ccp->his_proto));
    prompt_Printf(arg->prompt, " Output: %ld --> %ld,  Input: %ld --> %ld\n",
                  ccp->uncompout, ccp->compout,
                  ccp->compin, ccp->uncompin);
  }

  if (ccp->in.algorithm != -1)
    prompt_Printf(arg->prompt, "\n Input Options:  %s\n",
                  (*algorithm[ccp->in.algorithm]->Disp)(&ccp->in.opt));

  if (ccp->out.algorithm != -1) {
    o = &ccp->out.opt;
    for (f = 0; f < ccp->out.algorithm; f++)
      if (IsEnabled(ccp->cfg.neg[algorithm[f]->Neg]))
        o = &(*o)->next;
    prompt_Printf(arg->prompt, " Output Options: %s\n",
                  (*algorithm[ccp->out.algorithm]->Disp)(&(*o)->val));
  }

  prompt_Printf(arg->prompt, "\n Defaults: ");
  prompt_Printf(arg->prompt, "FSM retry = %us, max %u Config"
                " REQ%s, %u Term REQ%s\n", ccp->cfg.fsm.timeout,
                ccp->cfg.fsm.maxreq, ccp->cfg.fsm.maxreq == 1 ? "" : "s",
                ccp->cfg.fsm.maxtrm, ccp->cfg.fsm.maxtrm == 1 ? "" : "s");
  prompt_Printf(arg->prompt, "           deflate windows: ");
  prompt_Printf(arg->prompt, "incoming = %d, ", ccp->cfg.deflate.in.winsize);
  prompt_Printf(arg->prompt, "outgoing = %d\n", ccp->cfg.deflate.out.winsize);
#ifndef NODES
  prompt_Printf(arg->prompt, "           MPPE: ");
  if (ccp->cfg.mppe.keybits)
    prompt_Printf(arg->prompt, "%d bits, ", ccp->cfg.mppe.keybits);
  else
    prompt_Printf(arg->prompt, "any bits, ");
  switch (ccp->cfg.mppe.state) {
  case MPPE_STATEFUL:
    prompt_Printf(arg->prompt, "stateful");
    break;
  case MPPE_STATELESS:
    prompt_Printf(arg->prompt, "stateless");
    break;
  case MPPE_ANYSTATE:
    prompt_Printf(arg->prompt, "any state");
    break;
  }
  prompt_Printf(arg->prompt, "%s\n",
                ccp->cfg.mppe.required ? ", required" : "");
#endif

  prompt_Printf(arg->prompt, "\n           DEFLATE:    %s\n",
                command_ShowNegval(ccp->cfg.neg[CCP_NEG_DEFLATE]));
  prompt_Printf(arg->prompt, "           PREDICTOR1: %s\n",
                command_ShowNegval(ccp->cfg.neg[CCP_NEG_PRED1]));
  prompt_Printf(arg->prompt, "           DEFLATE24:  %s\n",
                command_ShowNegval(ccp->cfg.neg[CCP_NEG_DEFLATE24]));
#ifndef NODES
  prompt_Printf(arg->prompt, "           MPPE:       %s\n",
                command_ShowNegval(ccp->cfg.neg[CCP_NEG_MPPE]));
#endif
  return 0;
}

void
ccp_SetupCallbacks(struct ccp *ccp)
{
  ccp->fsm.fn = &ccp_Callbacks;
  ccp->fsm.FsmTimer.name = ccp_TimerNames[0];
  ccp->fsm.OpenTimer.name = ccp_TimerNames[1];
  ccp->fsm.StoppedTimer.name = ccp_TimerNames[2];
}

void
ccp_Init(struct ccp *ccp, struct bundle *bundle, struct link *l,
         const struct fsm_parent *parent)
{
  /* Initialise ourselves */

  fsm_Init(&ccp->fsm, "CCP", PROTO_CCP, 1, CCP_MAXCODE, LogCCP,
           bundle, l, parent, &ccp_Callbacks, ccp_TimerNames);

  ccp->cfg.deflate.in.winsize = 0;
  ccp->cfg.deflate.out.winsize = 15;
  ccp->cfg.fsm.timeout = DEF_FSMRETRY;
  ccp->cfg.fsm.maxreq = DEF_FSMTRIES;
  ccp->cfg.fsm.maxtrm = DEF_FSMTRIES;
  ccp->cfg.neg[CCP_NEG_DEFLATE] = NEG_ENABLED|NEG_ACCEPTED;
  ccp->cfg.neg[CCP_NEG_PRED1] = NEG_ENABLED|NEG_ACCEPTED;
  ccp->cfg.neg[CCP_NEG_DEFLATE24] = 0;
#ifndef NODES
  ccp->cfg.mppe.keybits = 0;
  ccp->cfg.mppe.state = MPPE_ANYSTATE;
  ccp->cfg.mppe.required = 0;
  ccp->cfg.neg[CCP_NEG_MPPE] = NEG_ENABLED|NEG_ACCEPTED;
#endif

  ccp_Setup(ccp);
}

void
ccp_Setup(struct ccp *ccp)
{
  /* Set ourselves up for a startup */
  ccp->fsm.open_mode = 0;
  ccp->his_proto = ccp->my_proto = -1;
  ccp->reset_sent = ccp->last_reset = -1;
  ccp->in.algorithm = ccp->out.algorithm = -1;
  ccp->in.state = ccp->out.state = NULL;
  ccp->in.opt.hdr.id = -1;
  ccp->out.opt = NULL;
  ccp->his_reject = ccp->my_reject = 0;
  ccp->uncompout = ccp->compout = 0;
  ccp->uncompin = ccp->compin = 0;
}

/*
 * Is ccp *REQUIRED* ?
 * We ask each of the configured ccp protocols if they're required and
 * return TRUE if they are.
 *
 * It's not possible for the peer to reject a required ccp protocol
 * without our state machine bringing the supporting lcp layer down.
 *
 * If ccp is required but not open, the NCP layer should not push
 * any data into the link.
 */
int
ccp_Required(struct ccp *ccp)
{
  unsigned f;

  for (f = 0; f < NALGORITHMS; f++)
    if (IsEnabled(ccp->cfg.neg[algorithm[f]->Neg]) &&
        (*algorithm[f]->Required)(&ccp->fsm))
      return 1;

  return 0;
}

/*
 * Report whether it's possible to increase a packet's size after
 * compression (and by how much).
 */
int
ccp_MTUOverhead(struct ccp *ccp)
{
  if (ccp->fsm.state == ST_OPENED && ccp->out.algorithm >= 0)
    return algorithm[ccp->out.algorithm]->o.MTUOverhead;

  return 0;
}

static void
CcpInitRestartCounter(struct fsm *fp, int what)
{
  /* Set fsm timer load */
  struct ccp *ccp = fsm2ccp(fp);

  fp->FsmTimer.load = ccp->cfg.fsm.timeout * SECTICKS;
  switch (what) {
    case FSM_REQ_TIMER:
      fp->restart = ccp->cfg.fsm.maxreq;
      break;
    case FSM_TRM_TIMER:
      fp->restart = ccp->cfg.fsm.maxtrm;
      break;
    default:
      fp->restart = 1;
      break;
  }
}

static void
CcpSendConfigReq(struct fsm *fp)
{
  /* Send config REQ please */
  struct ccp *ccp = fsm2ccp(fp);
  struct ccp_opt **o;
  u_char *cp, buff[100];
  unsigned f;
  int alloc;

  cp = buff;
  o = &ccp->out.opt;
  alloc = ccp->his_reject == 0 && ccp->out.opt == NULL;
  ccp->my_proto = -1;
  ccp->out.algorithm = -1;
  for (f = 0; f < NALGORITHMS; f++)
    if (IsEnabled(ccp->cfg.neg[algorithm[f]->Neg]) &&
        !REJECTED(ccp, algorithm[f]->id) &&
        (*algorithm[f]->Usable)(fp)) {

      if (!alloc)
        for (o = &ccp->out.opt; *o != NULL; o = &(*o)->next)
          if ((*o)->val.hdr.id == algorithm[f]->id && (*o)->algorithm == (int)f)
            break;

      if (alloc || *o == NULL) {
        if ((*o = (struct ccp_opt *)malloc(sizeof(struct ccp_opt))) == NULL) {
	  log_Printf(LogERROR, "%s: Not enough memory for CCP REQ !\n",
		     fp->link->name);
	  break;
	}
        (*o)->val.hdr.id = algorithm[f]->id;
        (*o)->val.hdr.len = 2;
        (*o)->next = NULL;
        (*o)->algorithm = f;
        (*algorithm[f]->o.OptInit)(fp->bundle, &(*o)->val, &ccp->cfg);
      }

      if (cp + (*o)->val.hdr.len > buff + sizeof buff) {
        log_Printf(LogERROR, "%s: CCP REQ buffer overrun !\n", fp->link->name);
        break;
      }
      memcpy(cp, &(*o)->val, (*o)->val.hdr.len);
      cp += (*o)->val.hdr.len;

      ccp->my_proto = (*o)->val.hdr.id;
      ccp->out.algorithm = f;

      if (alloc)
        o = &(*o)->next;
    }

  fsm_Output(fp, CODE_CONFIGREQ, fp->reqid, buff, cp - buff, MB_CCPOUT);
}

void
ccp_SendResetReq(struct fsm *fp)
{
  /* We can't read our input - ask peer to reset */
  struct ccp *ccp = fsm2ccp(fp);

  ccp->reset_sent = fp->reqid;
  ccp->last_reset = -1;
  fsm_Output(fp, CODE_RESETREQ, fp->reqid, NULL, 0, MB_CCPOUT);
}

static void
CcpSentTerminateReq(struct fsm *fp __unused)
{
  /* Term REQ just sent by FSM */
}

static void
CcpSendTerminateAck(struct fsm *fp, u_char id)
{
  /* Send Term ACK please */
  fsm_Output(fp, CODE_TERMACK, id, NULL, 0, MB_CCPOUT);
}

static int
CcpRecvResetReq(struct fsm *fp)
{
  /* Got a reset REQ, reset outgoing dictionary */
  struct ccp *ccp = fsm2ccp(fp);
  if (ccp->out.state == NULL)
    return 1;
  return (*algorithm[ccp->out.algorithm]->o.Reset)(ccp->out.state);
}

static void
CcpLayerStart(struct fsm *fp)
{
  /* We're about to start up ! */
  struct ccp *ccp = fsm2ccp(fp);

  log_Printf(LogCCP, "%s: LayerStart.\n", fp->link->name);
  fp->more.reqs = fp->more.naks = fp->more.rejs = ccp->cfg.fsm.maxreq * 3;
}

static void
CcpLayerDown(struct fsm *fp)
{
  /* About to come down */
  struct ccp *ccp = fsm2ccp(fp);
  struct ccp_opt *next;

  log_Printf(LogCCP, "%s: LayerDown.\n", fp->link->name);
  if (ccp->in.state != NULL) {
    (*algorithm[ccp->in.algorithm]->i.Term)(ccp->in.state);
    ccp->in.state = NULL;
    ccp->in.algorithm = -1;
  }
  if (ccp->out.state != NULL) {
    (*algorithm[ccp->out.algorithm]->o.Term)(ccp->out.state);
    ccp->out.state = NULL;
    ccp->out.algorithm = -1;
  }
  ccp->his_reject = ccp->my_reject = 0;

  while (ccp->out.opt) {
    next = ccp->out.opt->next;
    free(ccp->out.opt);
    ccp->out.opt = next;
  }
  ccp_Setup(ccp);
}

static void
CcpLayerFinish(struct fsm *fp)
{
  /* We're now down */
  struct ccp *ccp = fsm2ccp(fp);
  struct ccp_opt *next;

  log_Printf(LogCCP, "%s: LayerFinish.\n", fp->link->name);

  /*
   * Nuke options that may be left over from sending a REQ but never
   * coming up.
   */
  while (ccp->out.opt) {
    next = ccp->out.opt->next;
    free(ccp->out.opt);
    ccp->out.opt = next;
  }

  if (ccp_Required(ccp)) {
    if (fp->link->lcp.fsm.state == ST_OPENED)
      log_Printf(LogLCP, "%s: Closing due to CCP completion\n", fp->link->name);
    fsm_Close(&fp->link->lcp.fsm);
  }
}

/*  Called when CCP has reached the OPEN state */
static int
CcpLayerUp(struct fsm *fp)
{
  /* We're now up */
  struct ccp *ccp = fsm2ccp(fp);
  struct ccp_opt **o;
  unsigned f, fail;

  for (f = fail = 0; f < NALGORITHMS; f++)
    if (IsEnabled(ccp->cfg.neg[algorithm[f]->Neg]) &&
        (*algorithm[f]->Required)(&ccp->fsm) &&
        (ccp->in.algorithm != (int)f || ccp->out.algorithm != (int)f)) {
      /* Blow it all away - we haven't negotiated a required algorithm */
      log_Printf(LogWARN, "%s: Failed to negotiate (required) %s\n",
                 fp->link->name, protoname(algorithm[f]->id));
      fail = 1;
    }

  if (fail) {
    ccp->his_proto = ccp->my_proto = -1;
    fsm_Close(fp);
    fsm_Close(&fp->link->lcp.fsm);
    return 0;
  }

  log_Printf(LogCCP, "%s: LayerUp.\n", fp->link->name);

  if (ccp->in.state == NULL && ccp->in.algorithm >= 0 &&
      ccp->in.algorithm < (int)NALGORITHMS) {
    ccp->in.state = (*algorithm[ccp->in.algorithm]->i.Init)
      (fp->bundle, &ccp->in.opt);
    if (ccp->in.state == NULL) {
      log_Printf(LogERROR, "%s: %s (in) initialisation failure\n",
                fp->link->name, protoname(ccp->his_proto));
      ccp->his_proto = ccp->my_proto = -1;
      fsm_Close(fp);
      return 0;
    }
  }

  o = &ccp->out.opt;
  if (ccp->out.algorithm > 0)
    for (f = 0; f < (unsigned)ccp->out.algorithm; f++)
      if (IsEnabled(ccp->cfg.neg[algorithm[f]->Neg]))
	o = &(*o)->next;

  if (ccp->out.state == NULL && ccp->out.algorithm >= 0 &&
      ccp->out.algorithm < (int)NALGORITHMS) {
    ccp->out.state = (*algorithm[ccp->out.algorithm]->o.Init)
      (fp->bundle, &(*o)->val);
    if (ccp->out.state == NULL) {
      log_Printf(LogERROR, "%s: %s (out) initialisation failure\n",
                fp->link->name, protoname(ccp->my_proto));
      ccp->his_proto = ccp->my_proto = -1;
      fsm_Close(fp);
      return 0;
    }
  }

  fp->more.reqs = fp->more.naks = fp->more.rejs = ccp->cfg.fsm.maxreq * 3;

  log_Printf(LogCCP, "%s: Out = %s[%d], In = %s[%d]\n",
            fp->link->name, protoname(ccp->my_proto), ccp->my_proto,
            protoname(ccp->his_proto), ccp->his_proto);

  return 1;
}

static void
CcpDecodeConfig(struct fsm *fp, u_char *cp, u_char *end, int mode_type,
                struct fsm_decode *dec)
{
  /* Deal with incoming data */
  struct ccp *ccp = fsm2ccp(fp);
  int f;
  const char *disp;
  struct fsm_opt *opt;

  if (mode_type == MODE_REQ)
    ccp->in.algorithm = -1;	/* In case we've received two REQs in a row */

  while (end >= cp + sizeof(opt->hdr)) {
    if ((opt = fsm_readopt(&cp)) == NULL)
      break;

    for (f = NALGORITHMS-1; f > -1; f--)
      if (algorithm[f]->id == opt->hdr.id)
        break;

    disp = f == -1 ? "" : (*algorithm[f]->Disp)(opt);
    if (disp == NULL)
      disp = "";

    log_Printf(LogCCP, " %s[%d] %s\n", protoname(opt->hdr.id),
               opt->hdr.len, disp);

    if (f == -1) {
      /* Don't understand that :-( */
      if (mode_type == MODE_REQ) {
        ccp->my_reject |= (1 << opt->hdr.id);
        fsm_rej(dec, opt);
      }
    } else {
      struct ccp_opt *o;

      switch (mode_type) {
      case MODE_REQ:
        if (IsAccepted(ccp->cfg.neg[algorithm[f]->Neg]) &&
            (*algorithm[f]->Usable)(fp) &&
            ccp->in.algorithm == -1) {
          memcpy(&ccp->in.opt, opt, opt->hdr.len);
          switch ((*algorithm[f]->i.Set)(fp->bundle, &ccp->in.opt, &ccp->cfg)) {
          case MODE_REJ:
            fsm_rej(dec, &ccp->in.opt);
            break;
          case MODE_NAK:
            fsm_nak(dec, &ccp->in.opt);
            break;
          case MODE_ACK:
            fsm_ack(dec, &ccp->in.opt);
            ccp->his_proto = opt->hdr.id;
            ccp->in.algorithm = (int)f;		/* This one'll do :-) */
            break;
          }
        } else {
          fsm_rej(dec, opt);
        }
        break;
      case MODE_NAK:
        for (o = ccp->out.opt; o != NULL; o = o->next)
          if (o->val.hdr.id == opt->hdr.id)
            break;
        if (o == NULL)
          log_Printf(LogCCP, "%s: Warning: Ignoring peer NAK of unsent"
                     " option\n", fp->link->name);
        else {
          memcpy(&o->val, opt, opt->hdr.len);
          if ((*algorithm[f]->o.Set)(fp->bundle, &o->val, &ccp->cfg) ==
              MODE_ACK)
            ccp->my_proto = algorithm[f]->id;
          else {
            ccp->his_reject |= (1 << opt->hdr.id);
            ccp->my_proto = -1;
            if (algorithm[f]->Required(fp)) {
              log_Printf(LogWARN, "%s: Cannot understand peers (required)"
                         " %s negotiation\n", fp->link->name,
                         protoname(algorithm[f]->id));
              fsm_Close(&fp->link->lcp.fsm);
            }
          }
        }
        break;
      case MODE_REJ:
        ccp->his_reject |= (1 << opt->hdr.id);
        ccp->my_proto = -1;
        if (algorithm[f]->Required(fp)) {
          log_Printf(LogWARN, "%s: Peer rejected (required) %s negotiation\n",
                     fp->link->name, protoname(algorithm[f]->id));
          fsm_Close(&fp->link->lcp.fsm);
        }
        break;
      }
    }
  }

  if (mode_type != MODE_NOP) {
    fsm_opt_normalise(dec);
    if (dec->rejend != dec->rej || dec->nakend != dec->nak) {
      if (ccp->in.state == NULL) {
        ccp->his_proto = -1;
        ccp->in.algorithm = -1;
      }
    }
  }
}

extern struct mbuf *
ccp_Input(struct bundle *bundle, struct link *l, struct mbuf *bp)
{
  /* Got PROTO_CCP from link */
  m_settype(bp, MB_CCPIN);
  if (bundle_Phase(bundle) == PHASE_NETWORK)
    fsm_Input(&l->ccp.fsm, bp);
  else {
    if (bundle_Phase(bundle) < PHASE_NETWORK)
      log_Printf(LogCCP, "%s: Error: Unexpected CCP in phase %s (ignored)\n",
                 l->ccp.fsm.link->name, bundle_PhaseName(bundle));
    m_freem(bp);
  }
  return NULL;
}

static void
CcpRecvResetAck(struct fsm *fp, u_char id)
{
  /* Got a reset ACK, reset incoming dictionary */
  struct ccp *ccp = fsm2ccp(fp);

  if (ccp->reset_sent != -1) {
    if (id != ccp->reset_sent) {
      log_Printf(LogCCP, "%s: Incorrect ResetAck (id %d, not %d)"
                " ignored\n", fp->link->name, id, ccp->reset_sent);
      return;
    }
    /* Whaddaya know - a correct reset ack */
  } else if (id == ccp->last_reset)
    log_Printf(LogCCP, "%s: Duplicate ResetAck (resetting again)\n",
               fp->link->name);
  else {
    log_Printf(LogCCP, "%s: Unexpected ResetAck (id %d) ignored\n",
               fp->link->name, id);
    return;
  }

  ccp->last_reset = ccp->reset_sent;
  ccp->reset_sent = -1;
  if (ccp->in.state != NULL)
    (*algorithm[ccp->in.algorithm]->i.Reset)(ccp->in.state);
}

static struct mbuf *
ccp_LayerPush(struct bundle *b __unused, struct link *l, struct mbuf *bp,
              int pri, u_short *proto)
{
  if (PROTO_COMPRESSIBLE(*proto)) {
    if (l->ccp.fsm.state != ST_OPENED) {
      if (ccp_Required(&l->ccp)) {
        /* The NCP layer shouldn't have let this happen ! */
        log_Printf(LogERROR, "%s: Unexpected attempt to use an unopened and"
                   " required CCP layer\n", l->name);
        m_freem(bp);
        bp = NULL;
      }
    } else if (l->ccp.out.state != NULL) {
      bp = (*algorithm[l->ccp.out.algorithm]->o.Write)
             (l->ccp.out.state, &l->ccp, l, pri, proto, bp);
      switch (*proto) {
        case PROTO_ICOMPD:
          m_settype(bp, MB_ICOMPDOUT);
          break;
        case PROTO_COMPD:
          m_settype(bp, MB_COMPDOUT);
          break;
      }
    }
  }

  return bp;
}

static struct mbuf *
ccp_LayerPull(struct bundle *b __unused, struct link *l, struct mbuf *bp,
	      u_short *proto)
{
  /*
   * If proto isn't PROTO_[I]COMPD, we still want to pass it to the
   * decompression routines so that the dictionary's updated
   */
  if (l->ccp.fsm.state == ST_OPENED) {
    if (*proto == PROTO_COMPD || *proto == PROTO_ICOMPD) {
      /* Decompress incoming data */
      if (l->ccp.reset_sent != -1)
        /* Send another REQ and put the packet in the bit bucket */
        fsm_Output(&l->ccp.fsm, CODE_RESETREQ, l->ccp.reset_sent, NULL, 0,
                   MB_CCPOUT);
      else if (l->ccp.in.state != NULL) {
        bp = (*algorithm[l->ccp.in.algorithm]->i.Read)
               (l->ccp.in.state, &l->ccp, proto, bp);
        switch (*proto) {
          case PROTO_ICOMPD:
            m_settype(bp, MB_ICOMPDIN);
            break;
          case PROTO_COMPD:
            m_settype(bp, MB_COMPDIN);
            break;
        }
        return bp;
      }
      m_freem(bp);
      bp = NULL;
    } else if (PROTO_COMPRESSIBLE(*proto) && l->ccp.in.state != NULL) {
      /* Add incoming Network Layer traffic to our dictionary */
      (*algorithm[l->ccp.in.algorithm]->i.DictSetup)
        (l->ccp.in.state, &l->ccp, *proto, bp);
    }
  }

  return bp;
}

u_short
ccp_Proto(struct ccp *ccp)
{
  return !link2physical(ccp->fsm.link) || !ccp->fsm.bundle->ncp.mp.active ?
         PROTO_COMPD : PROTO_ICOMPD;
}

int
ccp_SetOpenMode(struct ccp *ccp)
{
  int f;

  for (f = 0; f < CCP_NEG_TOTAL; f++)
    if (IsEnabled(ccp->cfg.neg[f])) {
      ccp->fsm.open_mode = 0;
      return 1;
    }

  ccp->fsm.open_mode = OPEN_PASSIVE;	/* Go straight to ST_STOPPED ? */

  for (f = 0; f < CCP_NEG_TOTAL; f++)
    if (IsAccepted(ccp->cfg.neg[f]))
      return 1;

  return 0;				/* No CCP at all */
}

int
ccp_DefaultUsable(struct fsm *fp __unused)
{
  return 1;
}

int
ccp_DefaultRequired(struct fsm *fp __unused)
{
  return 0;
}

struct layer ccplayer = { LAYER_CCP, "ccp", ccp_LayerPush, ccp_LayerPull };
