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

#define DATALINK_CLOSED  (0)
#define DATALINK_OPENING (1)
#define DATALINK_HANGUP  (2)
#define DATALINK_DIAL    (3)
#define DATALINK_CARRIER (4)
#define DATALINK_LOGOUT  (5)
#define DATALINK_LOGIN   (6)
#define DATALINK_READY   (7)
#define DATALINK_LCP     (8)
#define DATALINK_AUTH    (9)
#define DATALINK_CBCP    (10)
#define DATALINK_OPEN    (11)

#define DATALINK_MAXNAME (20)   /* Maximum datalink::name length */

/* How to close the link */
#define CLOSE_NORMAL		0
#define CLOSE_STAYDOWN		1
#define CLOSE_LCP		2

struct iovec;
struct prompt;
struct physical;
struct bundle;

struct datalink {
  struct fdescriptor desc;	/* We play either a physical or a chat */
  unsigned state;		/* Our DATALINK_* state */
  struct physical *physical;	/* Our link */

  struct chat chat;		/* For bringing the link up & down */

  unsigned stayonline : 1;	/* stay online when LCP is closed ? */
  struct {
    unsigned run : 1;		/* run scripts ? */
    unsigned packetmode : 1;	/* Go into packet mode after login ? */
  } script;

  struct {
    struct {
      char dial[SCRIPT_LEN];
      char login[SCRIPT_LEN];
      char logout[SCRIPT_LEN];
      char hangup[SCRIPT_LEN];
    } script;			/* various chat scripts */
    struct {
      char list[SCRIPT_LEN];	/* Telephone Numbers */
    } phone;
    struct {
      int max;			/* initially try again this number of times */
      int next_timeout;		/* Redial next timeout value */
      int inc;			/* Increment timeout by `inc' each time read */
      int maxinc;		/* Maximum number of increments */
      int timeout;		/* Redial timeout value (end of phone list) */
    } dial;
    struct {
      int max;			/* initially try again this number of times */
      int timeout;		/* Timeout before reconnect on carrier loss */
    } reconnect;
    struct callback callback;	/* Direction depends on physical type */
    struct cbcpcfg cbcp;	/* Direction depends on phys type & callback */
  } cfg;			/* All our config data is in here */

  struct {
    char list[SCRIPT_LEN];	/* copy of cfg.list for strsep() */
    char *next;			/* Next phone from the list */
    char *alt;			/* Alternate (after fail) phone from the list */
    const char *chosen;		/* Chosen phone number after DIAL */
  } phone;

  struct cbcp cbcp;

  struct {
    struct pppTimer timer;	/* For timing between close & open */
    int tries;			/* currently try again this number of times */
    int incs;			/* # times our timeout has been incremented */
  } dial;

  unsigned reconnect_tries;	/* currently try again this number of times */

  char *name;			/* Our name */

  struct peerid peer;		/* Peer identification */

  struct fsm_parent fsmp;	   /* Our callback functions */
  const struct fsm_parent *parent; /* Our parent */

  struct authinfo pap;             /* Authentication using pap */
  struct chap chap;                /* Authentication using chap */

  struct mp_link mp;               /* multilink data */

  struct bundle *bundle;	   /* for the moment */
  struct datalink *next;	   /* Next in the list */
};

#define descriptor2datalink(d) \
  ((d)->type == DATALINK_DESCRIPTOR ? (struct datalink *)(d) : NULL)

extern struct datalink *datalink_Create(const char *name, struct bundle *, int);
extern struct datalink *datalink_Clone(struct datalink *, const char *);
extern struct datalink *iov2datalink(struct bundle *, struct iovec *, int *,
                                     int, int, int *, int *);
extern int datalink2iov(struct datalink *, struct iovec *, int *, int, int *,
                        int *);
extern struct datalink *datalink_Destroy(struct datalink *);
extern void datalink_GotAuthname(struct datalink *, const char *);
extern void datalink_Up(struct datalink *, int, int);
extern void datalink_Close(struct datalink *, int);
extern void datalink_Down(struct datalink *, int);
extern void datalink_StayDown(struct datalink *);
extern void datalink_DontHangup(struct datalink *);
extern void datalink_AuthOk(struct datalink *);
extern void datalink_AuthNotOk(struct datalink *);
extern void datalink_NCPUp(struct datalink *);
extern void datalink_CBCPComplete(struct datalink *);
extern void datalink_CBCPFailed(struct datalink *);
extern int datalink_Show(struct cmdargs const *);
extern int datalink_SetRedial(struct cmdargs const *);
extern int datalink_SetReconnect(struct cmdargs const *);
extern const char *datalink_State(struct datalink *);
extern void datalink_Rename(struct datalink *, const char *);
extern int datalink_RemoveFromSet(struct datalink *, fd_set *, fd_set *,
                                  fd_set *);
extern int datalink_SetMode(struct datalink *, int);
extern int datalink_GetDialTimeout(struct datalink *);
extern const char *datalink_ChoosePhoneNumber(struct datalink *);
extern void datalink_ComeDown(struct datalink *, int);
