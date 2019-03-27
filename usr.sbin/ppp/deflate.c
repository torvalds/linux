/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997 Brian Somers <brian@Awfulhak.org>
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

#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>

#include "mbuf.h"
#include "log.h"
#include "timer.h"
#include "fsm.h"
#include "ccp.h"
#include "deflate.h"

/* Our state */
struct deflate_state {
    u_short seqno;
    int uncomp_rec;
    int winsize;
    z_stream cx;
};

static char garbage[10];
static u_char EMPTY_BLOCK[4] = { 0x00, 0x00, 0xff, 0xff };

#define DEFLATE_CHUNK_LEN (1536 - sizeof(struct mbuf))

static int
DeflateResetOutput(void *v)
{
  struct deflate_state *state = (struct deflate_state *)v;

  state->seqno = 0;
  state->uncomp_rec = 0;
  deflateReset(&state->cx);
  log_Printf(LogCCP, "Deflate: Output channel reset\n");

  return 1;		/* Ask FSM to ACK */
}

static struct mbuf *
DeflateOutput(void *v, struct ccp *ccp, struct link *l __unused,
	      int pri __unused, u_short *proto, struct mbuf *mp)
{
  struct deflate_state *state = (struct deflate_state *)v;
  u_char *wp, *rp;
  int olen, ilen, len, res, flush;
  struct mbuf *mo_head, *mo, *mi_head, *mi;

  ilen = m_length(mp);
  log_Printf(LogDEBUG, "DeflateOutput: Proto %02x (%d bytes)\n", *proto, ilen);
  log_DumpBp(LogDEBUG, "DeflateOutput: Compress packet:", mp);

  /* Stuff the protocol in front of the input */
  mi_head = mi = m_get(2, MB_CCPOUT);
  mi->m_next = mp;
  rp = MBUF_CTOP(mi);
  if (*proto < 0x100) {			/* Compress the protocol */
    rp[0] = *proto & 0377;
    mi->m_len = 1;
  } else {				/* Don't compress the protocol */
    rp[0] = *proto >> 8;
    rp[1] = *proto & 0377;
    mi->m_len = 2;
  }

  /* Allocate the initial output mbuf */
  mo_head = mo = m_get(DEFLATE_CHUNK_LEN, MB_CCPOUT);
  mo->m_len = 2;
  wp = MBUF_CTOP(mo);
  *wp++ = state->seqno >> 8;
  *wp++ = state->seqno & 0377;
  log_Printf(LogDEBUG, "DeflateOutput: Seq %d\n", state->seqno);
  state->seqno++;

  /* Set up the deflation context */
  state->cx.next_out = wp;
  state->cx.avail_out = DEFLATE_CHUNK_LEN - 2;
  state->cx.next_in = MBUF_CTOP(mi);
  state->cx.avail_in = mi->m_len;
  flush = Z_NO_FLUSH;

  olen = 0;
  while (1) {
    if ((res = deflate(&state->cx, flush)) != Z_OK) {
      if (res == Z_STREAM_END)
        break;			/* Done */
      log_Printf(LogWARN, "DeflateOutput: deflate returned %d (%s)\n",
                res, state->cx.msg ? state->cx.msg : "");
      m_freem(mo_head);
      m_free(mi_head);
      state->seqno--;
      return mp;		/* Our dictionary's probably dead now :-( */
    }

    if (flush == Z_SYNC_FLUSH && state->cx.avail_out != 0)
      break;

    if (state->cx.avail_in == 0 && mi->m_next != NULL) {
      mi = mi->m_next;
      state->cx.next_in = MBUF_CTOP(mi);
      state->cx.avail_in = mi->m_len;
      if (mi->m_next == NULL)
        flush = Z_SYNC_FLUSH;
    }

    if (state->cx.avail_out == 0) {
      mo->m_next = m_get(DEFLATE_CHUNK_LEN, MB_CCPOUT);
      olen += (mo->m_len = DEFLATE_CHUNK_LEN);
      mo = mo->m_next;
      mo->m_len = 0;
      state->cx.next_out = MBUF_CTOP(mo);
      state->cx.avail_out = DEFLATE_CHUNK_LEN;
    }
  }

  olen += (mo->m_len = DEFLATE_CHUNK_LEN - state->cx.avail_out);
  olen -= 4;		/* exclude the trailing EMPTY_BLOCK */

  /*
   * If the output packet (including seqno and excluding the EMPTY_BLOCK)
   * got bigger, send the original.
   */
  if (olen >= ilen) {
    m_freem(mo_head);
    m_free(mi_head);
    log_Printf(LogDEBUG, "DeflateOutput: %d => %d: Uncompressible (0x%04x)\n",
              ilen, olen, *proto);
    ccp->uncompout += ilen;
    ccp->compout += ilen;	/* We measure this stuff too */
    return mp;
  }

  m_freem(mi_head);

  /*
   * Lose the last four bytes of our output.
   * XXX: We should probably assert that these are the same as the
   *      contents of EMPTY_BLOCK.
   */
  mo = mo_head;
  for (len = mo->m_len; len < olen; mo = mo->m_next, len += mo->m_len)
    ;
  mo->m_len -= len - olen;
  if (mo->m_next != NULL) {
    m_freem(mo->m_next);
    mo->m_next = NULL;
  }

  ccp->uncompout += ilen;
  ccp->compout += olen;

  log_Printf(LogDEBUG, "DeflateOutput: %d => %d bytes, proto 0x%04x\n",
            ilen, olen, *proto);

  *proto = ccp_Proto(ccp);
  return mo_head;
}

static void
DeflateResetInput(void *v)
{
  struct deflate_state *state = (struct deflate_state *)v;

  state->seqno = 0;
  state->uncomp_rec = 0;
  inflateReset(&state->cx);
  log_Printf(LogCCP, "Deflate: Input channel reset\n");
}

static struct mbuf *
DeflateInput(void *v, struct ccp *ccp, u_short *proto, struct mbuf *mi)
{
  struct deflate_state *state = (struct deflate_state *)v;
  struct mbuf *mo, *mo_head, *mi_head;
  u_char *wp;
  int ilen, olen;
  int seq, flush, res, first;
  u_char hdr[2];

  log_DumpBp(LogDEBUG, "DeflateInput: Decompress packet:", mi);
  mi_head = mi = mbuf_Read(mi, hdr, 2);
  ilen = 2;

  /* Check the sequence number. */
  seq = (hdr[0] << 8) + hdr[1];
  log_Printf(LogDEBUG, "DeflateInput: Seq %d\n", seq);
  if (seq != state->seqno) {
    if (seq <= state->uncomp_rec)
      /*
       * So the peer's started at zero again - fine !  If we're wrong,
       * inflate() will fail.  This is better than getting into a loop
       * trying to get a ResetReq to a busy sender.
       */
      state->seqno = seq;
    else {
      log_Printf(LogCCP, "DeflateInput: Seq error: Got %d, expected %d\n",
                seq, state->seqno);
      m_freem(mi_head);
      ccp_SendResetReq(&ccp->fsm);
      return NULL;
    }
  }
  state->seqno++;
  state->uncomp_rec = 0;

  /* Allocate an output mbuf */
  mo_head = mo = m_get(DEFLATE_CHUNK_LEN, MB_CCPIN);

  /* Our proto starts with 0 if it's compressed */
  wp = MBUF_CTOP(mo);
  wp[0] = '\0';

  /*
   * We set avail_out to 1 initially so we can look at the first
   * byte of the output and decide whether we have a compressed
   * proto field.
   */
  state->cx.next_in = MBUF_CTOP(mi);
  state->cx.avail_in = mi->m_len;
  state->cx.next_out = wp + 1;
  state->cx.avail_out = 1;
  ilen += mi->m_len;

  flush = mi->m_next ? Z_NO_FLUSH : Z_SYNC_FLUSH;
  first = 1;
  olen = 0;

  while (1) {
    if ((res = inflate(&state->cx, flush)) != Z_OK) {
      if (res == Z_STREAM_END)
        break;			/* Done */
      log_Printf(LogCCP, "DeflateInput: inflate returned %d (%s)\n",
                res, state->cx.msg ? state->cx.msg : "");
      m_freem(mo_head);
      m_freem(mi);
      ccp_SendResetReq(&ccp->fsm);
      return NULL;
    }

    if (flush == Z_SYNC_FLUSH && state->cx.avail_out != 0)
      break;

    if (state->cx.avail_in == 0 && mi && (mi = m_free(mi)) != NULL) {
      /* underflow */
      state->cx.next_in = MBUF_CTOP(mi);
      ilen += (state->cx.avail_in = mi->m_len);
      if (mi->m_next == NULL)
        flush = Z_SYNC_FLUSH;
    }

    if (state->cx.avail_out == 0) {
      /* overflow */
      if (first) {
        if (!(wp[1] & 1)) {
          /* 2 byte proto, shuffle it back in output */
          wp[0] = wp[1];
          state->cx.next_out--;
          state->cx.avail_out = DEFLATE_CHUNK_LEN-1;
        } else
          state->cx.avail_out = DEFLATE_CHUNK_LEN-2;
        first = 0;
      } else {
        olen += (mo->m_len = DEFLATE_CHUNK_LEN);
        mo->m_next = m_get(DEFLATE_CHUNK_LEN, MB_CCPIN);
        mo = mo->m_next;
        state->cx.next_out = MBUF_CTOP(mo);
        state->cx.avail_out = DEFLATE_CHUNK_LEN;
      }
    }
  }

  if (mi != NULL)
    m_freem(mi);

  if (first) {
    log_Printf(LogCCP, "DeflateInput: Length error\n");
    m_freem(mo_head);
    ccp_SendResetReq(&ccp->fsm);
    return NULL;
  }

  olen += (mo->m_len = DEFLATE_CHUNK_LEN - state->cx.avail_out);

  *proto = ((u_short)wp[0] << 8) | wp[1];
  mo_head->m_offset += 2;
  mo_head->m_len -= 2;
  olen -= 2;

  ccp->compin += ilen;
  ccp->uncompin += olen;

  log_Printf(LogDEBUG, "DeflateInput: %d => %d bytes, proto 0x%04x\n",
            ilen, olen, *proto);

  /*
   * Simulate an EMPTY_BLOCK so that our dictionary stays in sync.
   * The peer will have silently removed this!
   */
  state->cx.next_out = garbage;
  state->cx.avail_out = sizeof garbage;
  state->cx.next_in = EMPTY_BLOCK;
  state->cx.avail_in = sizeof EMPTY_BLOCK;
  inflate(&state->cx, Z_SYNC_FLUSH);

  return mo_head;
}

static void
DeflateDictSetup(void *v, struct ccp *ccp, u_short proto, struct mbuf *mi)
{
  struct deflate_state *state = (struct deflate_state *)v;
  int res, flush, expect_error;
  u_char *rp;
  struct mbuf *mi_head;
  short len;

  log_Printf(LogDEBUG, "DeflateDictSetup: Got seq %d\n", state->seqno);

  /*
   * Stuff an ``uncompressed data'' block header followed by the
   * protocol in front of the input
   */
  mi_head = m_get(7, MB_CCPOUT);
  mi_head->m_next = mi;
  len = m_length(mi);
  mi = mi_head;
  rp = MBUF_CTOP(mi);
  if (proto < 0x100) {			/* Compress the protocol */
    rp[5] = proto & 0377;
    mi->m_len = 6;
    len++;
  } else {				/* Don't compress the protocol */
    rp[5] = proto >> 8;
    rp[6] = proto & 0377;
    mi->m_len = 7;
    len += 2;
  }
  rp[0] = 0x80;				/* BITS: 100xxxxx */
  rp[1] = len & 0377;			/* The length */
  rp[2] = len >> 8;
  rp[3] = (~len) & 0377;		/* One's compliment of the length */
  rp[4] = (~len) >> 8;

  state->cx.next_in = rp;
  state->cx.avail_in = mi->m_len;
  state->cx.next_out = garbage;
  state->cx.avail_out = sizeof garbage;
  flush = Z_NO_FLUSH;
  expect_error = 0;

  while (1) {
    if ((res = inflate(&state->cx, flush)) != Z_OK) {
      if (res == Z_STREAM_END)
        break;			/* Done */
      if (expect_error && res == Z_BUF_ERROR)
        break;
      log_Printf(LogCCP, "DeflateDictSetup: inflate returned %d (%s)\n",
                res, state->cx.msg ? state->cx.msg : "");
      log_Printf(LogCCP, "DeflateDictSetup: avail_in %d, avail_out %d\n",
                state->cx.avail_in, state->cx.avail_out);
      ccp_SendResetReq(&ccp->fsm);
      m_free(mi_head);		/* lose our allocated ``head'' buf */
      return;
    }

    if (flush == Z_SYNC_FLUSH && state->cx.avail_out != 0)
      break;

    if (state->cx.avail_in == 0 && mi && (mi = mi->m_next) != NULL) {
      /* underflow */
      state->cx.next_in = MBUF_CTOP(mi);
      state->cx.avail_in = mi->m_len;
      if (mi->m_next == NULL)
        flush = Z_SYNC_FLUSH;
    }

    if (state->cx.avail_out == 0) {
      if (state->cx.avail_in == 0)
        /*
         * This seems to be a bug in libz !  If inflate() finished
         * with 0 avail_in and 0 avail_out *and* this is the end of
         * our input *and* inflate() *has* actually written all the
         * output it's going to, it *doesn't* return Z_STREAM_END !
         * When we subsequently call it with no more input, it gives
         * us Z_BUF_ERROR :-(  It seems pretty safe to ignore this
         * error (the dictionary seems to stay in sync).  In the worst
         * case, we'll drop the next compressed packet and do a
         * CcpReset() then.
         */
        expect_error = 1;
      /* overflow */
      state->cx.next_out = garbage;
      state->cx.avail_out = sizeof garbage;
    }
  }

  ccp->compin += len;
  ccp->uncompin += len;

  state->seqno++;
  state->uncomp_rec++;
  m_free(mi_head);		/* lose our allocated ``head'' buf */
}

static const char *
DeflateDispOpts(struct fsm_opt *o)
{
  static char disp[7];		/* Must be used immediately */

  sprintf(disp, "win %d", (o->data[0]>>4) + 8);
  return disp;
}

static void
DeflateInitOptsOutput(struct bundle *bundle __unused, struct fsm_opt *o,
                      const struct ccp_config *cfg)
{
  o->hdr.len = 4;
  o->data[0] = ((cfg->deflate.out.winsize - 8) << 4) + 8;
  o->data[1] = '\0';
}

static int
DeflateSetOptsOutput(struct bundle *bundle __unused, struct fsm_opt *o,
                     const struct ccp_config *cfg __unused)
{
  if (o->hdr.len != 4 || (o->data[0] & 15) != 8 || o->data[1] != '\0')
    return MODE_REJ;

  if ((o->data[0] >> 4) + 8 > 15) {
    o->data[0] = ((15 - 8) << 4) + 8;
    return MODE_NAK;
  }

  return MODE_ACK;
}

static int
DeflateSetOptsInput(struct bundle *bundle __unused, struct fsm_opt *o,
                    const struct ccp_config *cfg)
{
  int want;

  if (o->hdr.len != 4 || (o->data[0] & 15) != 8 || o->data[1] != '\0')
    return MODE_REJ;

  want = (o->data[0] >> 4) + 8;
  if (cfg->deflate.in.winsize == 0) {
    if (want < 8 || want > 15) {
      o->data[0] = ((15 - 8) << 4) + 8;
    }
  } else if (want != cfg->deflate.in.winsize) {
    o->data[0] = ((cfg->deflate.in.winsize - 8) << 4) + 8;
    return MODE_NAK;
  }

  return MODE_ACK;
}

static void *
DeflateInitInput(struct bundle *bundle __unused, struct fsm_opt *o)
{
  struct deflate_state *state;

  state = (struct deflate_state *)malloc(sizeof(struct deflate_state));
  if (state != NULL) {
    state->winsize = (o->data[0] >> 4) + 8;
    state->cx.zalloc = NULL;
    state->cx.opaque = NULL;
    state->cx.zfree = NULL;
    state->cx.next_out = NULL;
    if (inflateInit2(&state->cx, -state->winsize) == Z_OK)
      DeflateResetInput(state);
    else {
      free(state);
      state = NULL;
    }
  }

  return state;
}

static void *
DeflateInitOutput(struct bundle *bundle __unused, struct fsm_opt *o)
{
  struct deflate_state *state;

  state = (struct deflate_state *)malloc(sizeof(struct deflate_state));
  if (state != NULL) {
    state->winsize = (o->data[0] >> 4) + 8;
    state->cx.zalloc = NULL;
    state->cx.opaque = NULL;
    state->cx.zfree = NULL;
    state->cx.next_in = NULL;
    if (deflateInit2(&state->cx, Z_DEFAULT_COMPRESSION, 8,
                     -state->winsize, 8, Z_DEFAULT_STRATEGY) == Z_OK)
      DeflateResetOutput(state);
    else {
      free(state);
      state = NULL;
    }
  }

  return state;
}

static void
DeflateTermInput(void *v)
{
  struct deflate_state *state = (struct deflate_state *)v;

  inflateEnd(&state->cx);
  free(state);
}

static void
DeflateTermOutput(void *v)
{
  struct deflate_state *state = (struct deflate_state *)v;

  deflateEnd(&state->cx);
  free(state);
}

const struct ccp_algorithm PppdDeflateAlgorithm = {
  TY_PPPD_DEFLATE,	/* Older versions of pppd expected this ``type'' */
  CCP_NEG_DEFLATE24,
  DeflateDispOpts,
  ccp_DefaultUsable,
  ccp_DefaultRequired,
  {
    DeflateSetOptsInput,
    DeflateInitInput,
    DeflateTermInput,
    DeflateResetInput,
    DeflateInput,
    DeflateDictSetup
  },
  {
    0,
    DeflateInitOptsOutput,
    DeflateSetOptsOutput,
    DeflateInitOutput,
    DeflateTermOutput,
    DeflateResetOutput,
    DeflateOutput
  },
};

const struct ccp_algorithm DeflateAlgorithm = {
  TY_DEFLATE,		/* rfc 1979 */
  CCP_NEG_DEFLATE,
  DeflateDispOpts,
  ccp_DefaultUsable,
  ccp_DefaultRequired,
  {
    DeflateSetOptsInput,
    DeflateInitInput,
    DeflateTermInput,
    DeflateResetInput,
    DeflateInput,
    DeflateDictSetup
  },
  {
    0,
    DeflateInitOptsOutput,
    DeflateSetOptsOutput,
    DeflateInitOutput,
    DeflateTermOutput,
    DeflateResetOutput,
    DeflateOutput
  },
};
