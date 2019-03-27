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

#include <stdlib.h>
#include <string.h>		/* strlen/memcpy */
#include <termios.h>

#include "layer.h"
#include "mbuf.h"
#include "log.h"
#include "defs.h"
#include "timer.h"
#include "fsm.h"
#include "auth.h"
#include "pap.h"
#include "lqr.h"
#include "hdlc.h"
#include "lcp.h"
#include "proto.h"
#include "async.h"
#include "throughput.h"
#include "ccp.h"
#include "link.h"
#include "descriptor.h"
#include "physical.h"
#include "iplist.h"
#include "slcompress.h"
#include "ncpaddr.h"
#include "ipcp.h"
#include "filter.h"
#include "mp.h"
#ifndef NORADIUS
#include "radius.h"
#endif
#include "ipv6cp.h"
#include "ncp.h"
#include "bundle.h"
#include "chat.h"
#include "chap.h"
#include "cbcp.h"
#include "datalink.h"

static const char * const papcodes[] = {
  "???", "REQUEST", "SUCCESS", "FAILURE"
};
#define MAXPAPCODE (sizeof papcodes / sizeof papcodes[0] - 1)

static void
pap_Req(struct authinfo *authp)
{
  struct bundle *bundle = authp->physical->dl->bundle;
  struct fsmheader lh;
  struct mbuf *bp;
  u_char *cp;
  int namelen, keylen, plen;

  namelen = strlen(bundle->cfg.auth.name);
  keylen = strlen(bundle->cfg.auth.key);
  plen = namelen + keylen + 2;
  log_Printf(LogDEBUG, "pap_Req: namelen = %d, keylen = %d\n", namelen, keylen);
  log_Printf(LogPHASE, "Pap Output: %s ********\n", bundle->cfg.auth.name);
  if (*bundle->cfg.auth.name == '\0')
    log_Printf(LogWARN, "Sending empty PAP authname!\n");
  lh.code = PAP_REQUEST;
  lh.id = authp->id;
  lh.length = htons(plen + sizeof(struct fsmheader));
  bp = m_get(plen + sizeof(struct fsmheader), MB_PAPOUT);
  memcpy(MBUF_CTOP(bp), &lh, sizeof(struct fsmheader));
  cp = MBUF_CTOP(bp) + sizeof(struct fsmheader);
  *cp++ = namelen;
  memcpy(cp, bundle->cfg.auth.name, namelen);
  cp += namelen;
  *cp++ = keylen;
  memcpy(cp, bundle->cfg.auth.key, keylen);
  link_PushPacket(&authp->physical->link, bp, bundle,
                  LINK_QUEUES(&authp->physical->link) - 1, PROTO_PAP);
}

static void
SendPapCode(struct authinfo *authp, int code, const char *message)
{
  struct fsmheader lh;
  struct mbuf *bp;
  u_char *cp;
  int plen, mlen;

  lh.code = code;
  lh.id = authp->id;
  mlen = strlen(message);
  plen = mlen + 1;
  lh.length = htons(plen + sizeof(struct fsmheader));
  bp = m_get(plen + sizeof(struct fsmheader), MB_PAPOUT);
  memcpy(MBUF_CTOP(bp), &lh, sizeof(struct fsmheader));
  cp = MBUF_CTOP(bp) + sizeof(struct fsmheader);
  /*
   * If our message is longer than 255 bytes, truncate the length to
   * 255 and send the entire message anyway.  Maybe the other end will
   * display it... (see pap_Input() !)
   */
  *cp++ = mlen > 255 ? 255 : mlen;
  memcpy(cp, message, mlen);
  log_Printf(LogPHASE, "Pap Output: %s\n", papcodes[code]);

  link_PushPacket(&authp->physical->link, bp, authp->physical->dl->bundle,
                  LINK_QUEUES(&authp->physical->link) - 1, PROTO_PAP);
}

static void
pap_Success(struct authinfo *authp)
{
  struct bundle *bundle = authp->physical->dl->bundle;

  datalink_GotAuthname(authp->physical->dl, authp->in.name);
#ifndef NORADIUS
  if (*bundle->radius.cfg.file && bundle->radius.repstr)
    SendPapCode(authp, PAP_ACK, bundle->radius.repstr);
  else
#endif
    SendPapCode(authp, PAP_ACK, "Greetings!!");
  authp->physical->link.lcp.auth_ineed = 0;
  if (Enabled(bundle, OPT_UTMP))
    physical_Login(authp->physical, authp->in.name);

  if (authp->physical->link.lcp.auth_iwait == 0)
    /*
     * Either I didn't need to authenticate, or I've already been
     * told that I got the answer right.
     */
    datalink_AuthOk(authp->physical->dl);
}

static void
pap_Failure(struct authinfo *authp)
{
  SendPapCode(authp, PAP_NAK, "Login incorrect");
  datalink_AuthNotOk(authp->physical->dl);
}

void
pap_Init(struct authinfo *pap, struct physical *p)
{
  auth_Init(pap, p, pap_Req, pap_Success, pap_Failure);
}

struct mbuf *
pap_Input(struct bundle *bundle, struct link *l, struct mbuf *bp)
{
  struct physical *p = link2physical(l);
  struct authinfo *authp = &p->dl->pap;
  u_char nlen, klen, *key;
  const char *txt;
  int txtlen;

  if (p == NULL) {
    log_Printf(LogERROR, "pap_Input: Not a physical link - dropped\n");
    m_freem(bp);
    return NULL;
  }

  if (bundle_Phase(bundle) != PHASE_NETWORK &&
      bundle_Phase(bundle) != PHASE_AUTHENTICATE) {
    log_Printf(LogPHASE, "Unexpected pap input - dropped !\n");
    m_freem(bp);
    return NULL;
  }

  if ((bp = auth_ReadHeader(authp, bp)) == NULL &&
      ntohs(authp->in.hdr.length) == 0) {
    log_Printf(LogWARN, "Pap Input: Truncated header !\n");
    return NULL;
  }

  if (authp->in.hdr.code == 0 || authp->in.hdr.code > MAXPAPCODE) {
    log_Printf(LogPHASE, "Pap Input: %d: Bad PAP code !\n", authp->in.hdr.code);
    m_freem(bp);
    return NULL;
  }

  if (authp->in.hdr.code != PAP_REQUEST && authp->id != authp->in.hdr.id &&
      Enabled(bundle, OPT_IDCHECK)) {
    /* Wrong conversation dude ! */
    log_Printf(LogPHASE, "Pap Input: %s dropped (got id %d, not %d)\n",
               papcodes[authp->in.hdr.code], authp->in.hdr.id, authp->id);
    m_freem(bp);
    return NULL;
  }
  m_settype(bp, MB_PAPIN);
  authp->id = authp->in.hdr.id;		/* We respond with this id */

  if (bp) {
    bp = mbuf_Read(bp, &nlen, 1);
    if (authp->in.hdr.code == PAP_ACK) {
      /*
       * Don't restrict the length of our acknowledgement freetext to
       * nlen (a one-byte length).  Show the rest of the ack packet
       * instead.  This isn't really part of the protocol.....
       */
      bp = m_pullup(bp);
      txt = MBUF_CTOP(bp);
      txtlen = m_length(bp);
    } else {
      bp = auth_ReadName(authp, bp, nlen);
      txt = authp->in.name;
      txtlen = strlen(authp->in.name);
    }
  } else {
    txt = "";
    txtlen = 0;
  }

  log_Printf(LogPHASE, "Pap Input: %s (%.*s)\n",
             papcodes[authp->in.hdr.code], txtlen, txt);

  switch (authp->in.hdr.code) {
    case PAP_REQUEST:
      if (bp == NULL) {
        log_Printf(LogPHASE, "Pap Input: No key given !\n");
        break;
      }
      bp = mbuf_Read(bp, &klen, 1);
      if (m_length(bp) < klen) {
        log_Printf(LogERROR, "Pap Input: Truncated key !\n");
        break;
      }
      if ((key = malloc(klen+1)) == NULL) {
        log_Printf(LogERROR, "Pap Input: Out of memory !\n");
        break;
      }
      bp = mbuf_Read(bp, key, klen);
      key[klen] = '\0';

#ifndef NORADIUS
      if (*bundle->radius.cfg.file) {
        if (!radius_Authenticate(&bundle->radius, authp, authp->in.name,
                                 key, strlen(key), NULL, 0))
          pap_Failure(authp);
      } else
#endif
      if (auth_Validate(bundle, authp->in.name, key))
        pap_Success(authp);
      else
        pap_Failure(authp);

      free(key);
      break;

    case PAP_ACK:
      auth_StopTimer(authp);
      if (p->link.lcp.auth_iwait == PROTO_PAP) {
        p->link.lcp.auth_iwait = 0;
        if (p->link.lcp.auth_ineed == 0)
          /*
           * We've succeeded in our ``login''
           * If we're not expecting  the peer to authenticate (or he already
           * has), proceed to network phase.
           */
          datalink_AuthOk(p->dl);
      }
      break;

    case PAP_NAK:
      auth_StopTimer(authp);
      datalink_AuthNotOk(p->dl);
      break;
  }

  m_freem(bp);
  return NULL;
}
