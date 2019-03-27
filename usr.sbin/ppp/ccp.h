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

#define	CCP_MAXCODE	CODE_RESETACK

#define	TY_OUI		0	/* OUI */
#define	TY_PRED1	1	/* Predictor type 1 */
#define	TY_PRED2	2	/* Predictor type 2 */
#define	TY_PUDDLE	3	/* Puddle Jumper */
#define	TY_HWPPC	16	/* Hewlett-Packard PPC */
#define	TY_STAC		17	/* Stac Electronics LZS */
#define	TY_MSPPC	18	/* Microsoft PPC */
#define	TY_MPPE		18	/* Microsoft PPE */
#define	TY_GAND		19	/* Gandalf FZA */
#define	TY_V42BIS	20	/* V.42bis compression */
#define	TY_BSD		21	/* BSD LZW Compress */
#define	TY_PPPD_DEFLATE	24	/* Deflate (gzip) - (mis) numbered by pppd */
#define	TY_DEFLATE	26	/* Deflate (gzip) - rfc 1979 */

#define CCP_NEG_DEFLATE		0
#define CCP_NEG_PRED1		1
#define CCP_NEG_DEFLATE24	2
#ifndef NODES
#define CCP_NEG_MPPE		3
#define CCP_NEG_TOTAL		4
#else
#define CCP_NEG_TOTAL		3
#endif

#ifndef NODES
enum mppe_negstate {
  MPPE_ANYSTATE,
  MPPE_STATELESS,
  MPPE_STATEFUL
};
#endif

struct mbuf;
struct link;

struct ccp_config {
  struct {
    struct {
      int winsize;
    } in, out;
  } deflate;
#ifndef NODES
  struct {
    int keybits;
    enum mppe_negstate state;
    unsigned required : 1;
  } mppe;
#endif
  struct fsm_retry fsm;	/* How often/frequently to resend requests */
  unsigned neg[CCP_NEG_TOTAL];
};

struct ccp_opt {
  struct ccp_opt *next;
  int algorithm;
  struct fsm_opt val;
};

struct ccp {
  struct fsm fsm;		/* The finite state machine */

  int his_proto;		/* peer's compression protocol */
  int my_proto;			/* our compression protocol */

  int reset_sent;		/* If != -1, ignore compressed 'till ack */
  int last_reset;		/* We can receive more (dups) w/ this id */

  struct {
    int algorithm;		/* Algorithm in use */
    void *state;		/* Returned by implementations Init() */
    struct fsm_opt opt;		/* Set by implementation's OptInit() */
  } in;

  struct {
    int algorithm;		/* Algorithm in use */
    void *state;		/* Returned by implementations Init() */
    struct ccp_opt *opt;	/* Set by implementation's OptInit() */
  } out;

  u_int32_t his_reject;		/* Request codes rejected by peer */
  u_int32_t my_reject;		/* Request codes I have rejected */

  u_long uncompout, compout;	/* Outgoing bytes before/after compression */
  u_long uncompin, compin;	/* Incoming bytes after/before decompression */

  struct ccp_config cfg;
};

#define fsm2ccp(fp) (fp->proto == PROTO_CCP ? (struct ccp *)fp : NULL)

struct ccp_algorithm {
  int id;
  int Neg;					/* ccp_config neg array item */
  const char *(*Disp)(struct fsm_opt *);	/* Use result immediately !  */
  int (*Usable)(struct fsm *);			/* Ok to negotiate ? */
  int (*Required)(struct fsm *);		/* Must negotiate ? */
  struct {
    int (*Set)(struct bundle *, struct fsm_opt *, const struct ccp_config *);
    void *(*Init)(struct bundle *, struct fsm_opt *);
    void (*Term)(void *);
    void (*Reset)(void *);
    struct mbuf *(*Read)(void *, struct ccp *, u_short *, struct mbuf *);
    void (*DictSetup)(void *, struct ccp *, u_short, struct mbuf *);
  } i;
  struct {
    int MTUOverhead;
    void (*OptInit)(struct bundle *, struct fsm_opt *,
                    const struct ccp_config *);
    int (*Set)(struct bundle *, struct fsm_opt *, const struct ccp_config *);
    void *(*Init)(struct bundle *, struct fsm_opt *);
    void (*Term)(void *);
    int (*Reset)(void *);
    struct mbuf *(*Write)(void *, struct ccp *, struct link *, int, u_short *,
                          struct mbuf *);
  } o;
};

extern void ccp_Init(struct ccp *, struct bundle *, struct link *,
                     const struct fsm_parent *);
extern void ccp_Setup(struct ccp *);
extern int ccp_Required(struct ccp *);
extern int ccp_MTUOverhead(struct ccp *);

extern void ccp_SendResetReq(struct fsm *);
extern struct mbuf *ccp_Input(struct bundle *, struct link *, struct mbuf *);
extern int ccp_ReportStatus(struct cmdargs const *);
extern u_short ccp_Proto(struct ccp *);
extern void ccp_SetupCallbacks(struct ccp *);
extern int ccp_SetOpenMode(struct ccp *);
extern int ccp_DefaultUsable(struct fsm *);
extern int ccp_DefaultRequired(struct fsm *);

extern struct layer ccplayer;
