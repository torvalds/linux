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

#define	PHASE_DEAD		0	/* Link is dead */
#define	PHASE_ESTABLISH		1	/* Establishing link */
#define	PHASE_AUTHENTICATE	2	/* Being authenticated */
#define	PHASE_NETWORK		3	/* We're alive ! */
#define	PHASE_TERMINATE		4	/* Terminating link */

/* cfg.opt bit settings */
#define OPT_FILTERDECAP		1
#define OPT_FORCE_SCRIPTS	2 /* force chat scripts */
#define OPT_IDCHECK		3
#define OPT_IFACEALIAS		4
#ifndef NOINET6
#define OPT_IPCP		5
#define OPT_IPV6CP		6
#endif
#define OPT_KEEPSESSION		7
#define OPT_LOOPBACK		8
#define OPT_NAS_IP_ADDRESS	9
#define OPT_NAS_IDENTIFIER	10
#define OPT_PASSWDAUTH		11
#define OPT_PROXY		12
#define OPT_PROXYALL		13
#define OPT_SROUTES		14
#define OPT_TCPMSSFIXUP		15
#define OPT_THROUGHPUT		16
#define OPT_UTMP		17
#define OPT_MAX			17

#define MAX_ENDDISC_CLASS 5

#define Enabled(b, o)		((b)->cfg.optmask & (1ull << (o)))
#define opt_enable(b, o)	((b)->cfg.optmask |= (1ull << (o)))
#define opt_disable(b, o)	((b)->cfg.optmask &= ~(1ull << (o)))

/* AutoAdjust() values */
#define AUTO_UP		1
#define AUTO_DOWN	2

struct sockaddr_un;
struct datalink;
struct physical;
struct link;
struct server;
struct prompt;
struct iface;

struct bundle {
  struct fdescriptor desc;    /* really all our datalinks */
  int unit;                   /* The device/interface unit number */

  struct {
    char Name[20];            /* The /dev/XXXX name */
    int fd;                   /* The /dev/XXXX descriptor */
    unsigned header : 1;      /* Family header sent & received ? */
  } dev;

  u_long bandwidth;           /* struct tuninfo speed */
  struct iface *iface;        /* Interface information */

  int routing_seq;            /* The current routing sequence number */
  u_int phase;                /* Curent phase */

  struct {
    int all;                  /* Union of all physical::type's */
    int open;                 /* Union of all open physical::type's */
  } phys_type;

  unsigned CleaningUp : 1;    /* Going to exit.... */
  unsigned NatEnabled : 1;    /* Are we using libalias ? */

  struct fsm_parent fsm;      /* Our callback functions */
  struct datalink *links;     /* Our data links */

  time_t upat;                /* When the link came up */

  struct {
    struct {
      unsigned timeout;          /* NCP Idle timeout value */
      unsigned min_timeout;      /* Don't idle out before this */
    } idle;
    struct {
      char name[AUTHLEN];        /* PAP/CHAP system name */
      char key[AUTHLEN];         /* PAP/CHAP key */
    } auth;
    unsigned long long optmask;  /* Uses OPT_ bits from above */
    char label[50];              /* last thing `load'ed */
    u_short ifqueue;             /* Interface queue size */

    struct {
      unsigned timeout;          /* How long to leave the output queue choked */
    } choked;
  } cfg;

  struct ncp ncp;

  struct {
    struct filter in;         /* incoming packet filter */
    struct filter out;        /* outgoing packet filter */
    struct filter dial;       /* dial-out packet filter */
    struct filter alive;      /* keep-alive packet filter */
  } filter;

  struct {
    struct pppTimer timer;    /* timeout after cfg.idle_timeout */
    time_t done;
  } idle;

#ifndef NORADIUS
  struct {
    struct pppTimer timer;
    time_t done;
  } session;
#endif

  struct {
    int fd;                   /* write status here */
  } notify;

  struct {
    struct pppTimer timer;    /* choked output queue timer */
  } choked;

#ifndef NORADIUS
  struct radius radius;       /* Info retrieved from radius server */
  struct radacct radacct;
#ifndef NOINET6
  struct radacct radacct6;
#endif
#endif
};

#define descriptor2bundle(d) \
  ((d)->type == BUNDLE_DESCRIPTOR ? (struct bundle *)(d) : NULL)

extern struct bundle *bundle_Create(const char *, int, int);
extern void bundle_Destroy(struct bundle *);
extern const char *bundle_PhaseName(struct bundle *);
#define bundle_Phase(b) ((b)->phase)
extern void bundle_NewPhase(struct bundle *, u_int);
extern void bundle_LinksRemoved(struct bundle *);
extern void bundle_Close(struct bundle *, const char *, int);
extern void bundle_Down(struct bundle *, int);
extern void bundle_Open(struct bundle *, const char *, int, int);
extern void bundle_LinkClosed(struct bundle *, struct datalink *);

extern int bundle_ShowLinks(struct cmdargs const *);
extern int bundle_ShowStatus(struct cmdargs const *);
extern void bundle_StartIdleTimer(struct bundle *, unsigned secs);
extern void bundle_SetIdleTimer(struct bundle *, unsigned, unsigned);
extern void bundle_StopIdleTimer(struct bundle *);
extern int bundle_IsDead(struct bundle *);
extern struct datalink *bundle2datalink(struct bundle *, const char *);

#ifndef NORADIUS
extern void bundle_StartSessionTimer(struct bundle *, unsigned secs);
extern void bundle_StopSessionTimer(struct bundle *);
#endif

extern void bundle_RegisterDescriptor(struct bundle *, struct fdescriptor *);
extern void bundle_UnRegisterDescriptor(struct bundle *, struct fdescriptor *);

extern void bundle_SetTtyCommandMode(struct bundle *, struct datalink *);

extern int bundle_DatalinkClone(struct bundle *, struct datalink *,
                                const char *);
extern void bundle_DatalinkRemove(struct bundle *, struct datalink *);
extern void bundle_CleanDatalinks(struct bundle *);
extern void bundle_SetLabel(struct bundle *, const char *);
extern const char *bundle_GetLabel(struct bundle *);
extern void bundle_SendDatalink(struct datalink *, int, struct sockaddr_un *);
extern int bundle_LinkSize(void);
extern void bundle_ReceiveDatalink(struct bundle *, int);
extern int bundle_SetMode(struct bundle *, struct datalink *, int);
extern int bundle_RenameDatalink(struct bundle *, struct datalink *,
                                 const char *);
extern void bundle_setsid(struct bundle *, int);
extern void bundle_LockTun(struct bundle *);
extern unsigned bundle_HighestState(struct bundle *);
extern int bundle_Exception(struct bundle *, int);
extern void bundle_AdjustFilters(struct bundle *, struct ncpaddr *,
                                 struct ncpaddr *);
extern void bundle_AdjustDNS(struct bundle *);
extern void bundle_CalculateBandwidth(struct bundle *);
extern void bundle_AutoAdjust(struct bundle *, int, int);
extern int bundle_WantAutoloadTimer(struct bundle *);
extern void bundle_ChangedPID(struct bundle *);
extern void bundle_Notify(struct bundle *, char);
extern int bundle_Uptime(struct bundle *);
