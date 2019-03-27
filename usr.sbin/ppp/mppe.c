/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Semen Ustimenko <semenu@FreeBSD.org>
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

#include <sys/socket.h>
#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/un.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <openssl/rc4.h>

#include "defs.h"
#include "mbuf.h"
#include "log.h"
#include "timer.h"
#include "fsm.h"
#include "lqr.h"
#include "hdlc.h"
#include "lcp.h"
#include "ccp.h"
#include "throughput.h"
#include "layer.h"
#include "link.h"
#include "chap_ms.h"
#include "proto.h"
#include "mppe.h"
#include "ua.h"
#include "descriptor.h"
#ifndef NORADIUS
#include "radius.h"
#endif
#include "ncpaddr.h"
#include "iplist.h"
#include "slcompress.h"
#include "ipcp.h"
#include "ipv6cp.h"
#include "filter.h"
#include "mp.h"
#include "ncp.h"
#include "bundle.h"

/*
 * Documentation:
 *
 * draft-ietf-pppext-mppe-04.txt
 * draft-ietf-pppext-mppe-keys-02.txt
 */

#define	MPPE_OPT_STATELESS	0x1000000
#define	MPPE_OPT_COMPRESSED	0x01
#define	MPPE_OPT_40BIT		0x20
#define	MPPE_OPT_56BIT		0x80
#define	MPPE_OPT_128BIT		0x40
#define	MPPE_OPT_BITMASK	0xe0
#define	MPPE_OPT_MASK		(MPPE_OPT_STATELESS | MPPE_OPT_BITMASK)

#define	MPPE_FLUSHED			0x8000
#define	MPPE_ENCRYPTED			0x1000
#define	MPPE_HEADER_BITMASK		0xf000
#define	MPPE_HEADER_FLAG		0x00ff
#define	MPPE_HEADER_FLAGMASK		0x00ff
#define	MPPE_HEADER_FLAGSHIFT		8
#define	MPPE_HEADER_STATEFUL_KEYCHANGES	16

struct mppe_state {
  unsigned	stateless : 1;
  unsigned	flushnext : 1;
  unsigned	flushrequired : 1;
  int		cohnum;
  unsigned	keylen;			/* 8 or 16 bytes */
  int 		keybits;		/* 40, 56 or 128 bits */
  char		sesskey[MPPE_KEY_LEN];
  char		mastkey[MPPE_KEY_LEN];
  RC4_KEY	rc4key;
};

int MPPE_MasterKeyValid = 0;
int MPPE_IsServer = 0;
char MPPE_MasterKey[MPPE_KEY_LEN];

/*
 * The peer has missed a packet.  Mark the next output frame to be FLUSHED
 */
static int
MPPEResetOutput(void *v)
{
  struct mppe_state *mop = (struct mppe_state *)v;

  if (mop->stateless)
    log_Printf(LogCCP, "MPPE: Unexpected output channel reset\n");
  else {
    log_Printf(LogCCP, "MPPE: Output channel reset\n");
    mop->flushnext = 1;
  }

  return 0;		/* Ask FSM not to ACK */
}

static void
MPPEReduceSessionKey(struct mppe_state *mp)
{
  switch(mp->keybits) {
  case 40:
    mp->sesskey[2] = 0x9e;
    mp->sesskey[1] = 0x26;
  case 56:
    mp->sesskey[0] = 0xd1;
  case 128:
    break;
  }
}

static void
MPPEKeyChange(struct mppe_state *mp)
{
  char InterimKey[MPPE_KEY_LEN];
  RC4_KEY RC4Key;

  GetNewKeyFromSHA(mp->mastkey, mp->sesskey, mp->keylen, InterimKey);
  RC4_set_key(&RC4Key, mp->keylen, InterimKey);
  RC4(&RC4Key, mp->keylen, InterimKey, mp->sesskey);

  MPPEReduceSessionKey(mp);
}

static struct mbuf *
MPPEOutput(void *v, struct ccp *ccp, struct link *l __unused, int pri __unused,
	   u_short *proto, struct mbuf *mp)
{
  struct mppe_state *mop = (struct mppe_state *)v;
  struct mbuf *mo;
  u_short nproto, prefix;
  int dictinit, ilen, len;
  char *rp;

  ilen = m_length(mp);
  dictinit = 0;

  log_Printf(LogDEBUG, "MPPE: Output: Proto %02x (%d bytes)\n", *proto, ilen);
  if (*proto < 0x21 || *proto > 0xFA) {
    log_Printf(LogDEBUG, "MPPE: Output: Not encrypting\n");
    ccp->compout += ilen;
    ccp->uncompout += ilen;
    return mp;
  }

  log_DumpBp(LogDEBUG, "MPPE: Output: Encrypt packet:", mp);

  /* Get mbuf for prefixes */
  mo = m_get(4, MB_CCPOUT);
  mo->m_next = mp;

  rp = MBUF_CTOP(mo);
  prefix = MPPE_ENCRYPTED | mop->cohnum;

  if (mop->stateless ||
      (mop->cohnum & MPPE_HEADER_FLAGMASK) == MPPE_HEADER_FLAG) {
    /* Change our key */
    log_Printf(LogDEBUG, "MPPEOutput: Key changed [%d]\n", mop->cohnum);
    MPPEKeyChange(mop);
    dictinit = 1;
  }

  if (mop->stateless || mop->flushnext) {
    prefix |= MPPE_FLUSHED;
    dictinit = 1;
    mop->flushnext = 0;
  }

  if (dictinit) {
    /* Initialise our dictionary */
    log_Printf(LogDEBUG, "MPPEOutput: Dictionary initialised [%d]\n",
               mop->cohnum);
    RC4_set_key(&mop->rc4key, mop->keylen, mop->sesskey);
  }

  /* Set MPPE packet prefix */
  ua_htons(&prefix, rp);

  /* Save encrypted protocol number */
  nproto = htons(*proto);
  RC4(&mop->rc4key, 2, (char *)&nproto, rp + 2);

  /* Encrypt main packet */
  rp = MBUF_CTOP(mp);
  RC4(&mop->rc4key, ilen, rp, rp);

  mop->cohnum++;
  mop->cohnum &= ~MPPE_HEADER_BITMASK;

  /* Set the protocol number */
  *proto = ccp_Proto(ccp);
  len = m_length(mo);
  ccp->uncompout += ilen;
  ccp->compout += len;

  log_Printf(LogDEBUG, "MPPE: Output: Encrypted: Proto %02x (%d bytes)\n",
             *proto, len);

  return mo;
}

static void
MPPEResetInput(void *v __unused)
{
  log_Printf(LogCCP, "MPPE: Unexpected input channel ack\n");
}

static struct mbuf *
MPPEInput(void *v, struct ccp *ccp, u_short *proto, struct mbuf *mp)
{
  struct mppe_state *mip = (struct mppe_state *)v;
  u_short prefix;
  char *rp;
  int dictinit, flushed, ilen, len, n;

  ilen = m_length(mp);
  dictinit = 0;
  ccp->compin += ilen;

  log_Printf(LogDEBUG, "MPPE: Input: Proto %02x (%d bytes)\n", *proto, ilen);
  log_DumpBp(LogDEBUG, "MPPE: Input: Packet:", mp);

  mp = mbuf_Read(mp, &prefix, 2);
  prefix = ntohs(prefix);
  flushed = prefix & MPPE_FLUSHED;
  prefix &= ~flushed;
  if ((prefix & MPPE_HEADER_BITMASK) != MPPE_ENCRYPTED) {
    log_Printf(LogERROR, "MPPE: Input: Invalid packet (flags = 0x%x)\n",
               (prefix & MPPE_HEADER_BITMASK) | flushed);
    m_freem(mp);
    return NULL;
  }

  prefix &= ~MPPE_HEADER_BITMASK;

  if (!flushed && mip->stateless) {
    log_Printf(LogCCP, "MPPEInput: Packet without MPPE_FLUSHED set"
               " in stateless mode\n");
    flushed = MPPE_FLUSHED;
    /* Should we really continue ? */
  }

  if (mip->stateless) {
    /* Change our key for each missed packet in stateless mode */
    while (prefix != mip->cohnum) {
      log_Printf(LogDEBUG, "MPPEInput: Key changed [%u]\n", prefix);
      MPPEKeyChange(mip);
      /*
       * mip->cohnum contains what we received last time in stateless
       * mode.
       */
      mip->cohnum++;
      mip->cohnum &= ~MPPE_HEADER_BITMASK;
    }
    dictinit = 1;
  } else {
    if (flushed) {
      /*
       * We can always process a flushed packet.
       * Catch up on any outstanding key changes.
       */
      n = (prefix >> MPPE_HEADER_FLAGSHIFT) -
          (mip->cohnum >> MPPE_HEADER_FLAGSHIFT);
      if (n < 0)
        n += MPPE_HEADER_STATEFUL_KEYCHANGES;
      while (n--) {
        log_Printf(LogDEBUG, "MPPEInput: Key changed during catchup [%u]\n",
                   prefix);
        MPPEKeyChange(mip);
      }
      mip->flushrequired = 0;
      mip->cohnum = prefix;
      dictinit = 1;
    }

    if (mip->flushrequired) {
      /*
       * Perhaps we should be lenient if
       * (prefix & MPPE_HEADER_FLAGMASK) == MPPE_HEADER_FLAG
       * The spec says that we shouldn't be though....
       */
      log_Printf(LogDEBUG, "MPPE: Not flushed - discarded\n");
      fsm_Output(&ccp->fsm, CODE_RESETREQ, ccp->fsm.reqid++, NULL, 0,
                 MB_CCPOUT);
      m_freem(mp);
      return NULL;
    }

    if (prefix != mip->cohnum) {
      /*
       * We're in stateful mode and didn't receive the expected
       * packet.  Send a reset request, but don't tell the CCP layer
       * about it as we don't expect to receive a Reset ACK !
       * Guess what... M$ invented this !
       */
      log_Printf(LogCCP, "MPPE: Input: Got seq %u, not %u\n",
                 prefix, mip->cohnum);
      fsm_Output(&ccp->fsm, CODE_RESETREQ, ccp->fsm.reqid++, NULL, 0,
                 MB_CCPOUT);
      mip->flushrequired = 1;
      m_freem(mp);
      return NULL;
    }

    if ((prefix & MPPE_HEADER_FLAGMASK) == MPPE_HEADER_FLAG) {
      log_Printf(LogDEBUG, "MPPEInput: Key changed [%u]\n", prefix);
      MPPEKeyChange(mip);
      dictinit = 1;
    } else if (flushed)
      dictinit = 1;

    /*
     * mip->cohnum contains what we expect to receive next time in stateful
     * mode.
     */
    mip->cohnum++;
    mip->cohnum &= ~MPPE_HEADER_BITMASK;
  }

  if (dictinit) {
    log_Printf(LogDEBUG, "MPPEInput: Dictionary initialised [%u]\n", prefix);
    RC4_set_key(&mip->rc4key, mip->keylen, mip->sesskey);
  }

  mp = mbuf_Read(mp, proto, 2);
  RC4(&mip->rc4key, 2, (char *)proto, (char *)proto);
  *proto = ntohs(*proto);

  rp = MBUF_CTOP(mp);
  len = m_length(mp);
  RC4(&mip->rc4key, len, rp, rp);

  log_Printf(LogDEBUG, "MPPEInput: Decrypted: Proto %02x (%d bytes)\n",
             *proto, len);
  log_DumpBp(LogDEBUG, "MPPEInput: Decrypted: Packet:", mp);

  ccp->uncompin += len;

  return mp;
}

static void
MPPEDictSetup(void *v __unused, struct ccp *ccp __unused,
	      u_short proto __unused, struct mbuf *mp __unused)
{
  /* Nothing to see here */
}

static const char *
MPPEDispOpts(struct fsm_opt *o)
{
  static char buf[70];
  u_int32_t val;
  char ch;
  int len, n;

  ua_ntohl(o->data, &val);
  len = 0;
  if ((n = snprintf(buf, sizeof buf, "value 0x%08x ", (unsigned)val)) > 0)
    len += n;
  if (!(val & MPPE_OPT_BITMASK)) {
    if ((n = snprintf(buf + len, sizeof buf - len, "(0")) > 0)
      len += n;
  } else {
    ch = '(';
    if (val & MPPE_OPT_128BIT) {
      if ((n = snprintf(buf + len, sizeof buf - len, "%c128", ch)) > 0)
        len += n;
      ch = '/';
    }
    if (val & MPPE_OPT_56BIT) {
      if ((n = snprintf(buf + len, sizeof buf - len, "%c56", ch)) > 0)
        len += n;
      ch = '/';
    }
    if (val & MPPE_OPT_40BIT) {
      if ((n = snprintf(buf + len, sizeof buf - len, "%c40", ch)) > 0)
        len += n;
      ch = '/';
    }
  }

  if ((n = snprintf(buf + len, sizeof buf - len, " bits, state%s",
                    (val & MPPE_OPT_STATELESS) ? "less" : "ful")) > 0)
    len += n;

  if (val & MPPE_OPT_COMPRESSED) {
    if ((n = snprintf(buf + len, sizeof buf - len, ", compressed")) > 0)
      len += n;
  }

  snprintf(buf + len, sizeof buf - len, ")");

  return buf;
}

static int
MPPEUsable(struct fsm *fp)
{
  int ok;
#ifndef NORADIUS
  struct radius *r = &fp->bundle->radius;

  /*
   * If the radius server gave us RAD_MICROSOFT_MS_MPPE_ENCRYPTION_TYPES,
   * use that instead of our configuration value.
   */
  if (*r->cfg.file) {
    ok = r->mppe.sendkeylen && r->mppe.recvkeylen;
    if (!ok)
      log_Printf(LogCCP, "MPPE: Not permitted by RADIUS server\n");
  } else
#endif
  {
    struct lcp *lcp = &fp->link->lcp;
    ok = (lcp->want_auth == PROTO_CHAP && lcp->want_authtype == 0x81) ||
         (lcp->his_auth == PROTO_CHAP && lcp->his_authtype == 0x81);
    if (!ok)
      log_Printf(LogCCP, "MPPE: Not usable without CHAP81\n");
  }

  return ok;
}

static int
MPPERequired(struct fsm *fp)
{
#ifndef NORADIUS
  /*
   * If the radius server gave us RAD_MICROSOFT_MS_MPPE_ENCRYPTION_POLICY,
   * use that instead of our configuration value.
   */
  if (*fp->bundle->radius.cfg.file && fp->bundle->radius.mppe.policy)
    return fp->bundle->radius.mppe.policy == MPPE_POLICY_REQUIRED ? 1 : 0;
#endif

  return fp->link->ccp.cfg.mppe.required;
}

static u_int32_t
MPPE_ConfigVal(struct bundle *bundle __unused, const struct ccp_config *cfg)
{
  u_int32_t val;

  val = cfg->mppe.state == MPPE_STATELESS ? MPPE_OPT_STATELESS : 0;
#ifndef NORADIUS
  /*
   * If the radius server gave us RAD_MICROSOFT_MS_MPPE_ENCRYPTION_TYPES,
   * use that instead of our configuration value.
   */
  if (*bundle->radius.cfg.file && bundle->radius.mppe.types) {
    if (bundle->radius.mppe.types & MPPE_TYPE_40BIT)
      val |= MPPE_OPT_40BIT;
    if (bundle->radius.mppe.types & MPPE_TYPE_128BIT)
      val |= MPPE_OPT_128BIT;
  } else
#endif
    switch(cfg->mppe.keybits) {
    case 128:
      val |= MPPE_OPT_128BIT;
      break;
    case 56:
      val |= MPPE_OPT_56BIT;
      break;
    case 40:
      val |= MPPE_OPT_40BIT;
      break;
    case 0:
      val |= MPPE_OPT_128BIT | MPPE_OPT_56BIT | MPPE_OPT_40BIT;
      break;
    }

  return val;
}

/*
 * What options should we use for our first configure request
 */
static void
MPPEInitOptsOutput(struct bundle *bundle, struct fsm_opt *o,
                   const struct ccp_config *cfg)
{
  u_int32_t mval;

  o->hdr.len = 6;

  if (!MPPE_MasterKeyValid) {
    log_Printf(LogCCP, "MPPE: MasterKey is invalid,"
               " MPPE is available only with CHAP81 authentication\n");
    mval = 0;
    ua_htonl(&mval, o->data);
    return;
  }


  mval = MPPE_ConfigVal(bundle, cfg);
  ua_htonl(&mval, o->data);
}

/*
 * Our CCP request was NAK'd with the given options
 */
static int
MPPESetOptsOutput(struct bundle *bundle, struct fsm_opt *o,
                  const struct ccp_config *cfg)
{
  u_int32_t mval, peer;

  ua_ntohl(o->data, &peer);

  if (!MPPE_MasterKeyValid)
    /* Treat their NAK as a REJ */
    return MODE_NAK;

  mval = MPPE_ConfigVal(bundle, cfg);

  /*
   * If we haven't been configured with a specific number of keybits, allow
   * whatever the peer asks for.
   */
  if (!cfg->mppe.keybits) {
    mval &= ~MPPE_OPT_BITMASK;
    mval |= (peer & MPPE_OPT_BITMASK);
    if (!(mval & MPPE_OPT_BITMASK))
      mval |= MPPE_OPT_128BIT;
  }

  /* Adjust our statelessness */
  if (cfg->mppe.state == MPPE_ANYSTATE) {
    mval &= ~MPPE_OPT_STATELESS;
    mval |= (peer & MPPE_OPT_STATELESS);
  }

  ua_htonl(&mval, o->data);

  return MODE_ACK;
}

/*
 * The peer has requested the given options
 */
static int
MPPESetOptsInput(struct bundle *bundle, struct fsm_opt *o,
                 const struct ccp_config *cfg)
{
  u_int32_t mval, peer;
  int res = MODE_ACK;

  ua_ntohl(o->data, &peer);
  if (!MPPE_MasterKeyValid) {
    if (peer != 0) {
      peer = 0;
      ua_htonl(&peer, o->data);
      return MODE_NAK;
    } else
      return MODE_ACK;
  }

  mval = MPPE_ConfigVal(bundle, cfg);

  if (peer & ~MPPE_OPT_MASK)
    /* He's asking for bits we don't know about */
    res = MODE_NAK;

  if (peer & MPPE_OPT_STATELESS) {
    if (cfg->mppe.state == MPPE_STATEFUL)
      /* Peer can't have stateless */
      res = MODE_NAK;
    else
      /* Peer wants stateless, that's ok */
      mval |= MPPE_OPT_STATELESS;
  } else {
    if (cfg->mppe.state == MPPE_STATELESS)
      /* Peer must have stateless */
      res = MODE_NAK;
    else
      /* Peer doesn't want stateless, that's ok */
      mval &= ~MPPE_OPT_STATELESS;
  }

  /* If we've got a configured number of keybits - the peer must use that */
  if (cfg->mppe.keybits) {
    ua_htonl(&mval, o->data);
    return peer == mval ? res : MODE_NAK;
  }

  /* If a specific number of bits hasn't been requested, we'll need to NAK */
  switch (peer & MPPE_OPT_BITMASK) {
  case MPPE_OPT_128BIT:
  case MPPE_OPT_56BIT:
  case MPPE_OPT_40BIT:
    break;
  default:
    res = MODE_NAK;
  }

  /* Suggest the best number of bits */
  mval &= ~MPPE_OPT_BITMASK;
  if (peer & MPPE_OPT_128BIT)
    mval |= MPPE_OPT_128BIT;
  else if (peer & MPPE_OPT_56BIT)
    mval |= MPPE_OPT_56BIT;
  else if (peer & MPPE_OPT_40BIT)
    mval |= MPPE_OPT_40BIT;
  else
    mval |= MPPE_OPT_128BIT;
  ua_htonl(&mval, o->data);

  return res;
}

static struct mppe_state *
MPPE_InitState(struct fsm_opt *o)
{
  struct mppe_state *mp;
  u_int32_t val;

  if ((mp = calloc(1, sizeof *mp)) != NULL) {
    ua_ntohl(o->data, &val);

    switch (val & MPPE_OPT_BITMASK) {
    case MPPE_OPT_128BIT:
      mp->keylen = 16;
      mp->keybits = 128;
      break;
    case MPPE_OPT_56BIT:
      mp->keylen = 8;
      mp->keybits = 56;
      break;
    case MPPE_OPT_40BIT:
      mp->keylen = 8;
      mp->keybits = 40;
      break;
    default:
      log_Printf(LogWARN, "Unexpected MPPE options 0x%08x\n", val);
      free(mp);
      return NULL;
    }

    mp->stateless = !!(val & MPPE_OPT_STATELESS);
  }

  return mp;
}

static void *
MPPEInitInput(struct bundle *bundle __unused, struct fsm_opt *o)
{
  struct mppe_state *mip;

  if (!MPPE_MasterKeyValid) {
    log_Printf(LogWARN, "MPPE: Cannot initialise without CHAP81\n");
    return NULL;
  }

  if ((mip = MPPE_InitState(o)) == NULL) {
    log_Printf(LogWARN, "MPPEInput: Cannot initialise - unexpected options\n");
    return NULL;
  }

  log_Printf(LogDEBUG, "MPPE: InitInput: %d-bits\n", mip->keybits);

#ifndef NORADIUS
  if (*bundle->radius.cfg.file && bundle->radius.mppe.recvkey) {
    if (mip->keylen > bundle->radius.mppe.recvkeylen)
      mip->keylen = bundle->radius.mppe.recvkeylen;
    if (mip->keylen > sizeof mip->mastkey)
      mip->keylen = sizeof mip->mastkey;
    memcpy(mip->mastkey, bundle->radius.mppe.recvkey, mip->keylen);
  } else
#endif
    GetAsymetricStartKey(MPPE_MasterKey, mip->mastkey, mip->keylen, 0,
                         MPPE_IsServer);

  GetNewKeyFromSHA(mip->mastkey, mip->mastkey, mip->keylen, mip->sesskey);

  MPPEReduceSessionKey(mip);

  log_Printf(LogCCP, "MPPE: Input channel initiated\n");

  if (!mip->stateless) {
    /*
     * We need to initialise our dictionary here as the first packet we
     * receive is unlikely to have the FLUSHED bit set.
     */
    log_Printf(LogDEBUG, "MPPEInitInput: Dictionary initialised [%d]\n",
               mip->cohnum);
    RC4_set_key(&mip->rc4key, mip->keylen, mip->sesskey);
  } else {
    /*
     * We do the first key change here as the first packet is expected
     * to have a sequence number of 0 and we'll therefore not expect
     * to have to change the key at that point.
     */
    log_Printf(LogDEBUG, "MPPEInitInput: Key changed [%d]\n", mip->cohnum);
    MPPEKeyChange(mip);
  }

  return mip;
}

static void *
MPPEInitOutput(struct bundle *bundle __unused, struct fsm_opt *o)
{
  struct mppe_state *mop;

  if (!MPPE_MasterKeyValid) {
    log_Printf(LogWARN, "MPPE: Cannot initialise without CHAP81\n");
    return NULL;
  }

  if ((mop = MPPE_InitState(o)) == NULL) {
    log_Printf(LogWARN, "MPPEOutput: Cannot initialise - unexpected options\n");
    return NULL;
  }

  log_Printf(LogDEBUG, "MPPE: InitOutput: %d-bits\n", mop->keybits);

#ifndef NORADIUS
  if (*bundle->radius.cfg.file && bundle->radius.mppe.sendkey) {
    if (mop->keylen > bundle->radius.mppe.sendkeylen)
      mop->keylen = bundle->radius.mppe.sendkeylen;
    if (mop->keylen > sizeof mop->mastkey)
      mop->keylen = sizeof mop->mastkey;
    memcpy(mop->mastkey, bundle->radius.mppe.sendkey, mop->keylen);
  } else
#endif
    GetAsymetricStartKey(MPPE_MasterKey, mop->mastkey, mop->keylen, 1,
                         MPPE_IsServer);

  GetNewKeyFromSHA(mop->mastkey, mop->mastkey, mop->keylen, mop->sesskey);

  MPPEReduceSessionKey(mop);

  log_Printf(LogCCP, "MPPE: Output channel initiated\n");

  if (!mop->stateless) {
    /*
     * We need to initialise our dictionary now as the first packet we
     * send won't have the FLUSHED bit set.
     */
    log_Printf(LogDEBUG, "MPPEInitOutput: Dictionary initialised [%d]\n",
               mop->cohnum);
    RC4_set_key(&mop->rc4key, mop->keylen, mop->sesskey);
  }

  return mop;
}

static void
MPPETermInput(void *v)
{
  free(v);
}

static void
MPPETermOutput(void *v)
{
  free(v);
}

const struct ccp_algorithm MPPEAlgorithm = {
  TY_MPPE,
  CCP_NEG_MPPE,
  MPPEDispOpts,
  MPPEUsable,
  MPPERequired,
  {
    MPPESetOptsInput,
    MPPEInitInput,
    MPPETermInput,
    MPPEResetInput,
    MPPEInput,
    MPPEDictSetup
  },
  {
    2,
    MPPEInitOptsOutput,
    MPPESetOptsOutput,
    MPPEInitOutput,
    MPPETermOutput,
    MPPEResetOutput,
    MPPEOutput
  },
};
