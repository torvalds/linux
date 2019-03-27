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

#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include <net/if_tun.h>		/* For TUNS* ioctls */
#include <net/route.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <sys/un.h>

#include <errno.h>
#include <fcntl.h>
#ifdef __OpenBSD__
#include <util.h>
#else
#include <libutil.h>
#endif
#include <paths.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "layer.h"
#include "defs.h"
#include "command.h"
#include "mbuf.h"
#include "log.h"
#include "id.h"
#include "timer.h"
#include "fsm.h"
#include "iplist.h"
#include "lqr.h"
#include "hdlc.h"
#include "throughput.h"
#include "slcompress.h"
#include "ncpaddr.h"
#include "ip.h"
#include "ipcp.h"
#include "filter.h"
#include "descriptor.h"
#include "route.h"
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
#include "auth.h"
#include "proto.h"
#include "chap.h"
#include "tun.h"
#include "prompt.h"
#include "chat.h"
#include "cbcp.h"
#include "datalink.h"
#include "iface.h"
#include "server.h"
#include "probe.h"
#ifndef NODES
#include "mppe.h"
#endif

#define SCATTER_SEGMENTS 7  /* version, datalink, name, physical,
                               throughput, throughput, device       */

#define SEND_MAXFD 3        /* Max file descriptors passed through
                               the local domain socket              */

static int bundle_RemainingIdleTime(struct bundle *);

static const char * const PhaseNames[] = {
  "Dead", "Establish", "Authenticate", "Network", "Terminate"
};

const char *
bundle_PhaseName(struct bundle *bundle)
{
  return bundle->phase <= PHASE_TERMINATE ?
    PhaseNames[bundle->phase] : "unknown";
}

void
bundle_NewPhase(struct bundle *bundle, u_int new)
{
  if (new == bundle->phase)
    return;

  if (new <= PHASE_TERMINATE)
    log_Printf(LogPHASE, "bundle: %s\n", PhaseNames[new]);

  switch (new) {
  case PHASE_DEAD:
    bundle->phase = new;
#ifndef NODES
    MPPE_MasterKeyValid = 0;
#endif
    log_DisplayPrompts();
    break;

  case PHASE_ESTABLISH:
    bundle->phase = new;
    break;

  case PHASE_AUTHENTICATE:
    bundle->phase = new;
    log_DisplayPrompts();
    break;

  case PHASE_NETWORK:
    if (ncp_fsmStart(&bundle->ncp, bundle)) {
      bundle->phase = new;
      log_DisplayPrompts();
    } else {
      log_Printf(LogPHASE, "bundle: All NCPs are disabled\n");
      bundle_Close(bundle, NULL, CLOSE_STAYDOWN);
    }
    break;

  case PHASE_TERMINATE:
    bundle->phase = new;
    mp_Down(&bundle->ncp.mp);
    log_DisplayPrompts();
    break;
  }
}

static void
bundle_LayerStart(void *v __unused, struct fsm *fp __unused)
{
  /* The given FSM is about to start up ! */
}


void
bundle_Notify(struct bundle *bundle, char c)
{
  if (bundle->notify.fd != -1) {
    int ret;

    ret = write(bundle->notify.fd, &c, 1);
    if (c != EX_REDIAL && c != EX_RECONNECT) {
      if (ret == 1)
        log_Printf(LogCHAT, "Parent notified of %s\n",
                   c == EX_NORMAL ? "success" : "failure");
      else
        log_Printf(LogERROR, "Failed to notify parent of success\n");
      close(bundle->notify.fd);
      bundle->notify.fd = -1;
    } else if (ret == 1)
      log_Printf(LogCHAT, "Parent notified of %s\n", ex_desc(c));
    else
      log_Printf(LogERROR, "Failed to notify parent of %s\n", ex_desc(c));
  }
}

static void
bundle_ClearQueues(void *v)
{
  struct bundle *bundle = (struct bundle *)v;
  struct datalink *dl;

  log_Printf(LogPHASE, "Clearing choked output queue\n");
  timer_Stop(&bundle->choked.timer);

  /*
   * Emergency time:
   *
   * We've had a full queue for PACKET_DEL_SECS seconds without being
   * able to get rid of any of the packets.  We've probably given up
   * on the redials at this point, and the queued data has almost
   * definitely been timed out by the layer above.  As this is preventing
   * us from reading the TUN_NAME device (we don't want to buffer stuff
   * indefinitely), we may as well nuke this data and start with a clean
   * slate !
   *
   * Unfortunately, this has the side effect of shafting any compression
   * dictionaries in use (causing the relevant RESET_REQ/RESET_ACK).
   */

  ncp_DeleteQueues(&bundle->ncp);
  for (dl = bundle->links; dl; dl = dl->next)
    physical_DeleteQueue(dl->physical);
}

static void
bundle_LinkAdded(struct bundle *bundle, struct datalink *dl)
{
  bundle->phys_type.all |= dl->physical->type;
  if (dl->state == DATALINK_OPEN)
    bundle->phys_type.open |= dl->physical->type;

#ifndef NORADIUS
  if ((bundle->phys_type.open & (PHYS_DEDICATED|PHYS_DDIAL))
      != bundle->phys_type.open && bundle->session.timer.state == TIMER_STOPPED)
    if (bundle->radius.sessiontime)
      bundle_StartSessionTimer(bundle, 0);
#endif

  if ((bundle->phys_type.open & (PHYS_DEDICATED|PHYS_DDIAL))
      != bundle->phys_type.open && bundle->idle.timer.state == TIMER_STOPPED)
    /* We may need to start our idle timer */
    bundle_StartIdleTimer(bundle, 0);
}

void
bundle_LinksRemoved(struct bundle *bundle)
{
  struct datalink *dl;

  bundle->phys_type.all = bundle->phys_type.open = 0;
  for (dl = bundle->links; dl; dl = dl->next)
    bundle_LinkAdded(bundle, dl);

  bundle_CalculateBandwidth(bundle);
  mp_CheckAutoloadTimer(&bundle->ncp.mp);

  if ((bundle->phys_type.open & (PHYS_DEDICATED|PHYS_DDIAL))
      == bundle->phys_type.open) {
#ifndef NORADIUS
    if (bundle->radius.sessiontime)
      bundle_StopSessionTimer(bundle);
#endif
    bundle_StopIdleTimer(bundle);
   }
}

static void
bundle_LayerUp(void *v, struct fsm *fp)
{
  /*
   * The given fsm is now up
   * If it's an LCP, adjust our phys_mode.open value and check the
   * autoload timer.
   * If it's the first NCP, calculate our bandwidth
   * If it's the first NCP, set our ``upat'' time
   * If it's the first NCP, start the idle timer.
   * If it's an NCP, tell our -background parent to go away.
   * If it's the first NCP, start the autoload timer
   */
  struct bundle *bundle = (struct bundle *)v;

  if (fp->proto == PROTO_LCP) {
    struct physical *p = link2physical(fp->link);

    bundle_LinkAdded(bundle, p->dl);
    mp_CheckAutoloadTimer(&bundle->ncp.mp);
  } else if (isncp(fp->proto)) {
    if (ncp_LayersOpen(&fp->bundle->ncp) == 1) {
      bundle_CalculateBandwidth(fp->bundle);
      time(&bundle->upat);
#ifndef NORADIUS
      if (bundle->radius.sessiontime)
        bundle_StartSessionTimer(bundle, 0);
#endif
      bundle_StartIdleTimer(bundle, 0);
      mp_CheckAutoloadTimer(&fp->bundle->ncp.mp);
    }
    bundle_Notify(bundle, EX_NORMAL);
  } else if (fp->proto == PROTO_CCP)
    bundle_CalculateBandwidth(fp->bundle);	/* Against ccp_MTUOverhead */
}

static void
bundle_LayerDown(void *v, struct fsm *fp)
{
  /*
   * The given FSM has been told to come down.
   * If it's our last NCP, stop the idle timer.
   * If it's our last NCP, clear our ``upat'' value.
   * If it's our last NCP, stop the autoload timer
   * If it's an LCP, adjust our phys_type.open value and any timers.
   * If it's an LCP and we're in multilink mode, adjust our tun
   * If it's the last LCP, down all NCPs
   * speed and make sure our minimum sequence number is adjusted.
   */

  struct bundle *bundle = (struct bundle *)v;

  if (isncp(fp->proto)) {
    if (ncp_LayersOpen(&fp->bundle->ncp) == 0) {
#ifndef NORADIUS
      if (bundle->radius.sessiontime)
        bundle_StopSessionTimer(bundle);
#endif
      bundle_StopIdleTimer(bundle);
      bundle->upat = 0;
      mp_StopAutoloadTimer(&bundle->ncp.mp);
    }
  } else if (fp->proto == PROTO_LCP) {
    struct datalink *dl;
    struct datalink *lost;
    int others_active;

    bundle_LinksRemoved(bundle);  /* adjust timers & phys_type values */

    lost = NULL;
    others_active = 0;
    for (dl = bundle->links; dl; dl = dl->next) {
      if (fp == &dl->physical->link.lcp.fsm)
        lost = dl;
      else if (dl->state != DATALINK_CLOSED && dl->state != DATALINK_HANGUP)
        others_active++;
    }

    if (bundle->ncp.mp.active) {
      bundle_CalculateBandwidth(bundle);

      if (lost)
        mp_LinkLost(&bundle->ncp.mp, lost);
      else
        log_Printf(LogALERT, "Oops, lost an unrecognised datalink (%s) !\n",
                   fp->link->name);
    }

    if (!others_active) {
      /* Down the NCPs.  We don't expect to get fsm_Close()d ourself ! */
      ncp2initial(&bundle->ncp);
      mp_Down(&bundle->ncp.mp);
    }
  }
}

static void
bundle_LayerFinish(void *v, struct fsm *fp)
{
  /* The given fsm is now down (fp cannot be NULL)
   *
   * If it's the last NCP, fsm_Close all LCPs
   * If it's the last NCP, bring any MP layer down
   */

  struct bundle *bundle = (struct bundle *)v;
  struct datalink *dl;

  if (isncp(fp->proto) && !ncp_LayersUnfinished(&bundle->ncp)) {
    if (bundle_Phase(bundle) != PHASE_DEAD)
      bundle_NewPhase(bundle, PHASE_TERMINATE);
    for (dl = bundle->links; dl; dl = dl->next)
      if (dl->state == DATALINK_OPEN)
        datalink_Close(dl, CLOSE_STAYDOWN);
    fsm2initial(fp);
    mp_Down(&bundle->ncp.mp);
  }
}

void
bundle_Close(struct bundle *bundle, const char *name, int how)
{
  /*
   * Please close the given datalink.
   * If name == NULL or name is the last datalink, fsm_Close all NCPs
   * (except our MP)
   * If it isn't the last datalink, just Close that datalink.
   */

  struct datalink *dl, *this_dl;
  int others_active;

  others_active = 0;
  this_dl = NULL;

  for (dl = bundle->links; dl; dl = dl->next) {
    if (name && !strcasecmp(name, dl->name))
      this_dl = dl;
    if (name == NULL || this_dl == dl) {
      switch (how) {
        case CLOSE_LCP:
          datalink_DontHangup(dl);
          break;
        case CLOSE_STAYDOWN:
          datalink_StayDown(dl);
          break;
      }
    } else if (dl->state != DATALINK_CLOSED && dl->state != DATALINK_HANGUP)
      others_active++;
  }

  if (name && this_dl == NULL) {
    log_Printf(LogWARN, "%s: Invalid datalink name\n", name);
    return;
  }

  if (!others_active) {
#ifndef NORADIUS
    if (bundle->radius.sessiontime)
      bundle_StopSessionTimer(bundle);
#endif
    bundle_StopIdleTimer(bundle);
    if (ncp_LayersUnfinished(&bundle->ncp))
      ncp_Close(&bundle->ncp);
    else {
      ncp2initial(&bundle->ncp);
      mp_Down(&bundle->ncp.mp);
      for (dl = bundle->links; dl; dl = dl->next)
        datalink_Close(dl, how);
    }
  } else if (this_dl && this_dl->state != DATALINK_CLOSED &&
             this_dl->state != DATALINK_HANGUP)
    datalink_Close(this_dl, how);
}

void
bundle_Down(struct bundle *bundle, int how)
{
  struct datalink *dl;

  for (dl = bundle->links; dl; dl = dl->next)
    datalink_Down(dl, how);
}

static int
bundle_UpdateSet(struct fdescriptor *d, fd_set *r, fd_set *w, fd_set *e, int *n)
{
  struct bundle *bundle = descriptor2bundle(d);
  struct datalink *dl;
  int result, nlinks;
  u_short ifqueue;
  size_t queued;

  result = 0;

  /* If there are aren't many packets queued, look for some more. */
  for (nlinks = 0, dl = bundle->links; dl; dl = dl->next)
    nlinks++;

  if (nlinks) {
    queued = r ? ncp_FillPhysicalQueues(&bundle->ncp, bundle) :
                 ncp_QueueLen(&bundle->ncp);

    if (r && (bundle->phase == PHASE_NETWORK ||
              bundle->phys_type.all & PHYS_AUTO)) {
      /* enough surplus so that we can tell if we're getting swamped */
      ifqueue = nlinks > bundle->cfg.ifqueue ? nlinks : bundle->cfg.ifqueue;
      if (queued < ifqueue) {
        /* Not enough - select() for more */
        if (bundle->choked.timer.state == TIMER_RUNNING)
          timer_Stop(&bundle->choked.timer);	/* Not needed any more */
        FD_SET(bundle->dev.fd, r);
        if (*n < bundle->dev.fd + 1)
          *n = bundle->dev.fd + 1;
        log_Printf(LogTIMER, "%s: fdset(r) %d\n", TUN_NAME, bundle->dev.fd);
        result++;
      } else if (bundle->choked.timer.state == TIMER_STOPPED) {
        bundle->choked.timer.func = bundle_ClearQueues;
        bundle->choked.timer.name = "output choke";
        bundle->choked.timer.load = bundle->cfg.choked.timeout * SECTICKS;
        bundle->choked.timer.arg = bundle;
        timer_Start(&bundle->choked.timer);
      }
    }
  }

#ifndef NORADIUS
  result += descriptor_UpdateSet(&bundle->radius.desc, r, w, e, n);
#endif

  /* Which links need a select() ? */
  for (dl = bundle->links; dl; dl = dl->next)
    result += descriptor_UpdateSet(&dl->desc, r, w, e, n);

  /*
   * This *MUST* be called after the datalink UpdateSet()s as it
   * might be ``holding'' one of the datalinks (death-row) and
   * wants to be able to de-select() it from the descriptor set.
   */
  result += descriptor_UpdateSet(&bundle->ncp.mp.server.desc, r, w, e, n);

  return result;
}

static int
bundle_IsSet(struct fdescriptor *d, const fd_set *fdset)
{
  struct bundle *bundle = descriptor2bundle(d);
  struct datalink *dl;

  for (dl = bundle->links; dl; dl = dl->next)
    if (descriptor_IsSet(&dl->desc, fdset))
      return 1;

#ifndef NORADIUS
  if (descriptor_IsSet(&bundle->radius.desc, fdset))
    return 1;
#endif

  if (descriptor_IsSet(&bundle->ncp.mp.server.desc, fdset))
    return 1;

  return FD_ISSET(bundle->dev.fd, fdset);
}

static void
bundle_DescriptorRead(struct fdescriptor *d __unused, struct bundle *bundle,
                      const fd_set *fdset)
{
  struct datalink *dl;
  unsigned secs;
  u_int32_t af;

  if (descriptor_IsSet(&bundle->ncp.mp.server.desc, fdset))
    descriptor_Read(&bundle->ncp.mp.server.desc, bundle, fdset);

  for (dl = bundle->links; dl; dl = dl->next)
    if (descriptor_IsSet(&dl->desc, fdset))
      descriptor_Read(&dl->desc, bundle, fdset);

#ifndef NORADIUS
  if (descriptor_IsSet(&bundle->radius.desc, fdset))
    descriptor_Read(&bundle->radius.desc, bundle, fdset);
#endif

  if (FD_ISSET(bundle->dev.fd, fdset)) {
    struct tun_data tun;
    int n, pri;
    u_char *data;
    size_t sz;

    if (bundle->dev.header) {
      data = (u_char *)&tun;
      sz = sizeof tun;
    } else {
      data = tun.data;
      sz = sizeof tun.data;
    }

    /* something to read from tun */

    n = read(bundle->dev.fd, data, sz);
    if (n < 0) {
      log_Printf(LogWARN, "%s: read: %s\n", bundle->dev.Name, strerror(errno));
      return;
    }

    if (bundle->dev.header) {
      n -= sz - sizeof tun.data;
      if (n <= 0) {
        log_Printf(LogERROR, "%s: read: Got only %d bytes of data !\n",
                   bundle->dev.Name, n);
        return;
      }
      af = ntohl(tun.header.family);
#ifndef NOINET6
      if (af != AF_INET && af != AF_INET6)
#else
      if (af != AF_INET)
#endif
        /* XXX: Should be maintaining drop/family counts ! */
        return;
    } else
      af = AF_INET;

    if (af == AF_INET && ((struct ip *)tun.data)->ip_dst.s_addr ==
        bundle->ncp.ipcp.my_ip.s_addr) {
      /* we've been asked to send something addressed *to* us :( */
      if (Enabled(bundle, OPT_LOOPBACK)) {
        pri = PacketCheck(bundle, af, tun.data, n, &bundle->filter.in,
                          NULL, NULL);
        if (pri >= 0) {
          n += sz - sizeof tun.data;
          write(bundle->dev.fd, data, n);
          log_Printf(LogDEBUG, "Looped back packet addressed to myself\n");
        }
        return;
      } else
        log_Printf(LogDEBUG, "Oops - forwarding packet addressed to myself\n");
    }

    /*
     * Process on-demand dialup. Output packets are queued within the tunnel
     * device until the appropriate NCP is opened.
     */

    if (bundle_Phase(bundle) == PHASE_DEAD) {
      /*
       * Note, we must be in AUTO mode :-/ otherwise our interface should
       * *not* be UP and we can't receive data
       */
      pri = PacketCheck(bundle, af, tun.data, n, &bundle->filter.dial,
                        NULL, NULL);
      if (pri >= 0)
        bundle_Open(bundle, NULL, PHYS_AUTO, 0);
      else
        /*
         * Drop the packet.  If we were to queue it, we'd just end up with
         * a pile of timed-out data in our output queue by the time we get
         * around to actually dialing.  We'd also prematurely reach the
         * threshold at which we stop select()ing to read() the tun
         * device - breaking auto-dial.
         */
        return;
    }

    secs = 0;
    pri = PacketCheck(bundle, af, tun.data, n, &bundle->filter.out,
                      NULL, &secs);
    if (pri >= 0) {
      /* Prepend the number of seconds timeout given in the filter */
      tun.header.timeout = secs;
      ncp_Enqueue(&bundle->ncp, af, pri, (char *)&tun, n + sizeof tun.header);
    }
  }
}

static int
bundle_DescriptorWrite(struct fdescriptor *d __unused, struct bundle *bundle,
                       const fd_set *fdset)
{
  struct datalink *dl;
  int result = 0;

  /* This is not actually necessary as struct mpserver doesn't Write() */
  if (descriptor_IsSet(&bundle->ncp.mp.server.desc, fdset))
    if (descriptor_Write(&bundle->ncp.mp.server.desc, bundle, fdset) == 1)
      result++;

  for (dl = bundle->links; dl; dl = dl->next)
    if (descriptor_IsSet(&dl->desc, fdset))
      switch (descriptor_Write(&dl->desc, bundle, fdset)) {
      case -1:
        datalink_ComeDown(dl, CLOSE_NORMAL);
        break;
      case 1:
        result++;
      }

  return result;
}

void
bundle_LockTun(struct bundle *bundle)
{
  FILE *lockfile;
  char pidfile[PATH_MAX];

  snprintf(pidfile, sizeof pidfile, "%stun%d.pid", _PATH_VARRUN, bundle->unit);
  lockfile = ID0fopen(pidfile, "w");
  if (lockfile != NULL) {
    fprintf(lockfile, "%d\n", (int)getpid());
    fclose(lockfile);
  }
#ifndef RELEASE_CRUNCH
  else
    log_Printf(LogERROR, "Warning: Can't create %s: %s\n",
               pidfile, strerror(errno));
#endif
}

static void
bundle_UnlockTun(struct bundle *bundle)
{
  char pidfile[PATH_MAX];

  snprintf(pidfile, sizeof pidfile, "%stun%d.pid", _PATH_VARRUN, bundle->unit);
  ID0unlink(pidfile);
}

struct bundle *
bundle_Create(const char *prefix, int type, int unit)
{
  static struct bundle bundle;		/* there can be only one */
  int enoentcount, err, minunit, maxunit;
  const char *ifname;
#if defined(__FreeBSD__) && !defined(NOKLDLOAD)
  int kldtried;
#endif
#if defined(TUNSIFMODE) || defined(TUNSLMODE) || defined(TUNSIFHEAD)
  int iff;
#endif

  if (bundle.iface != NULL) {	/* Already allocated ! */
    log_Printf(LogALERT, "bundle_Create:  There's only one BUNDLE !\n");
    return NULL;
  }

  if (unit == -1) {
    minunit = 0;
    maxunit = -1;
  } else {
    minunit = unit;
    maxunit = unit + 1;
  }
  err = ENOENT;
  enoentcount = 0;
#if defined(__FreeBSD__) && !defined(NOKLDLOAD)
  kldtried = 0;
#endif
  for (bundle.unit = minunit; bundle.unit != maxunit; bundle.unit++) {
    snprintf(bundle.dev.Name, sizeof bundle.dev.Name, "%s%d",
             prefix, bundle.unit);
    bundle.dev.fd = ID0open(bundle.dev.Name, O_RDWR);
    if (bundle.dev.fd >= 0)
      break;
    else if (errno == ENXIO || errno == ENOENT) {
#if defined(__FreeBSD__) && !defined(NOKLDLOAD)
      if (bundle.unit == minunit && !kldtried++) {
        /*
         * Attempt to load the tunnel interface KLD if it isn't loaded
         * already.
         */
        if (loadmodules(LOAD_VERBOSLY, "if_tun", NULL))
          bundle.unit--;
        continue;
      }
#endif
      if (errno != ENOENT || ++enoentcount > 2) {
        err = errno;
	break;
      }
    } else
      err = errno;
  }

  if (bundle.dev.fd < 0) {
    if (unit == -1)
      log_Printf(LogWARN, "No available tunnel devices found (%s)\n",
                strerror(err));
    else
      log_Printf(LogWARN, "%s%d: %s\n", prefix, unit, strerror(err));
    return NULL;
  }

  log_SetTun(bundle.unit, NULL);

  ifname = strrchr(bundle.dev.Name, '/');
  if (ifname == NULL)
    ifname = bundle.dev.Name;
  else
    ifname++;

  bundle.iface = iface_Create(ifname);
  if (bundle.iface == NULL) {
    close(bundle.dev.fd);
    return NULL;
  }

#ifdef TUNSIFMODE
  /* Make sure we're POINTOPOINT & IFF_MULTICAST */
  iff = IFF_POINTOPOINT | IFF_MULTICAST;
  if (ID0ioctl(bundle.dev.fd, TUNSIFMODE, &iff) < 0)
    log_Printf(LogERROR, "bundle_Create: ioctl(TUNSIFMODE): %s\n",
	       strerror(errno));
#endif

#ifdef TUNSLMODE
  /* Make sure we're not prepending sockaddrs */
  iff = 0;
  if (ID0ioctl(bundle.dev.fd, TUNSLMODE, &iff) < 0)
    log_Printf(LogERROR, "bundle_Create: ioctl(TUNSLMODE): %s\n",
	       strerror(errno));
#endif

#ifdef TUNSIFHEAD
  /* We want the address family please ! */
  iff = 1;
  if (ID0ioctl(bundle.dev.fd, TUNSIFHEAD, &iff) < 0) {
    log_Printf(LogERROR, "bundle_Create: ioctl(TUNSIFHEAD): %s\n",
	       strerror(errno));
    bundle.dev.header = 0;
  } else
    bundle.dev.header = 1;
#else
#ifdef __OpenBSD__
  /* Always present for OpenBSD */
  bundle.dev.header = 1;
#else
  /*
   * If TUNSIFHEAD isn't available and we're not OpenBSD, assume
   * everything's AF_INET (hopefully the tun device won't pass us
   * anything else !).
   */
  bundle.dev.header = 0;
#endif
#endif

  log_Printf(LogPHASE, "Using interface: %s\n", ifname);

  bundle.bandwidth = 0;
  bundle.routing_seq = 0;
  bundle.phase = PHASE_DEAD;
  bundle.CleaningUp = 0;
  bundle.NatEnabled = 0;

  bundle.fsm.LayerStart = bundle_LayerStart;
  bundle.fsm.LayerUp = bundle_LayerUp;
  bundle.fsm.LayerDown = bundle_LayerDown;
  bundle.fsm.LayerFinish = bundle_LayerFinish;
  bundle.fsm.object = &bundle;

  bundle.cfg.idle.timeout = NCP_IDLE_TIMEOUT;
  bundle.cfg.idle.min_timeout = 0;
  *bundle.cfg.auth.name = '\0';
  *bundle.cfg.auth.key = '\0';
  bundle.cfg.optmask = (1ull << OPT_IDCHECK) | (1ull << OPT_LOOPBACK) |
                       (1ull << OPT_SROUTES) | (1ull << OPT_TCPMSSFIXUP) |
                       (1ull << OPT_THROUGHPUT) | (1ull << OPT_UTMP) |
                       (1ull << OPT_NAS_IP_ADDRESS) |
                       (1ull << OPT_NAS_IDENTIFIER);
#ifndef NOINET6
  opt_enable(&bundle, OPT_IPCP);
  if (probe.ipv6_available)
    opt_enable(&bundle, OPT_IPV6CP);
#endif
  *bundle.cfg.label = '\0';
  bundle.cfg.ifqueue = DEF_IFQUEUE;
  bundle.cfg.choked.timeout = CHOKED_TIMEOUT;
  bundle.phys_type.all = type;
  bundle.phys_type.open = 0;
  bundle.upat = 0;

  bundle.links = datalink_Create("deflink", &bundle, type);
  if (bundle.links == NULL) {
    log_Printf(LogALERT, "Cannot create data link: %s\n", strerror(errno));
    iface_Free(bundle.iface);
    bundle.iface = NULL;
    close(bundle.dev.fd);
    return NULL;
  }

  bundle.desc.type = BUNDLE_DESCRIPTOR;
  bundle.desc.UpdateSet = bundle_UpdateSet;
  bundle.desc.IsSet = bundle_IsSet;
  bundle.desc.Read = bundle_DescriptorRead;
  bundle.desc.Write = bundle_DescriptorWrite;

  ncp_Init(&bundle.ncp, &bundle);

  memset(&bundle.filter, '\0', sizeof bundle.filter);
  bundle.filter.in.fragok = bundle.filter.in.logok = 1;
  bundle.filter.in.name = "IN";
  bundle.filter.out.fragok = bundle.filter.out.logok = 1;
  bundle.filter.out.name = "OUT";
  bundle.filter.dial.name = "DIAL";
  bundle.filter.dial.logok = 1;
  bundle.filter.alive.name = "ALIVE";
  bundle.filter.alive.logok = 1;
  {
    int i;
    for (i = 0; i < MAXFILTERS; i++) {
        bundle.filter.in.rule[i].f_action = A_NONE;
        bundle.filter.out.rule[i].f_action = A_NONE;
        bundle.filter.dial.rule[i].f_action = A_NONE;
        bundle.filter.alive.rule[i].f_action = A_NONE;
    }
  }
  memset(&bundle.idle.timer, '\0', sizeof bundle.idle.timer);
  bundle.idle.done = 0;
  bundle.notify.fd = -1;
  memset(&bundle.choked.timer, '\0', sizeof bundle.choked.timer);
#ifndef NORADIUS
  radius_Init(&bundle.radius);
#endif

  /* Clean out any leftover crud */
  iface_Clear(bundle.iface, &bundle.ncp, 0, IFACE_CLEAR_ALL);

  bundle_LockTun(&bundle);

  return &bundle;
}

static void
bundle_DownInterface(struct bundle *bundle)
{
  route_IfDelete(bundle, 1);
  iface_ClearFlags(bundle->iface->name, IFF_UP);
}

void
bundle_Destroy(struct bundle *bundle)
{
  struct datalink *dl;

  /*
   * Clean up the interface.  We don't really need to do the timer_Stop()s,
   * mp_Down(), iface_Clear() and bundle_DownInterface() unless we're getting
   * out under exceptional conditions such as a descriptor exception.
   */
  timer_Stop(&bundle->idle.timer);
  timer_Stop(&bundle->choked.timer);
  mp_Down(&bundle->ncp.mp);
  iface_Clear(bundle->iface, &bundle->ncp, 0, IFACE_CLEAR_ALL);
  bundle_DownInterface(bundle);

#ifndef NORADIUS
  /* Tell the radius server the bad news */
  radius_Destroy(&bundle->radius);
#endif

  /* Again, these are all DATALINK_CLOSED unless we're abending */
  dl = bundle->links;
  while (dl)
    dl = datalink_Destroy(dl);

  ncp_Destroy(&bundle->ncp);

  close(bundle->dev.fd);
  bundle_UnlockTun(bundle);

  /* In case we never made PHASE_NETWORK */
  bundle_Notify(bundle, EX_ERRDEAD);

  iface_Destroy(bundle->iface);
  bundle->iface = NULL;
}

void
bundle_LinkClosed(struct bundle *bundle, struct datalink *dl)
{
  /*
   * Our datalink has closed.
   * CleanDatalinks() (called from DoLoop()) will remove closed
   * BACKGROUND, FOREGROUND and DIRECT links.
   * If it's the last data link, enter phase DEAD.
   *
   * NOTE: dl may not be in our list (bundle_SendDatalink()) !
   */

  struct datalink *odl;
  int other_links;

  log_SetTtyCommandMode(dl);

  other_links = 0;
  for (odl = bundle->links; odl; odl = odl->next)
    if (odl != dl && odl->state != DATALINK_CLOSED)
      other_links++;

  if (!other_links) {
    if (dl->physical->type != PHYS_AUTO)	/* Not in -auto mode */
      bundle_DownInterface(bundle);
    ncp2initial(&bundle->ncp);
    mp_Down(&bundle->ncp.mp);
    bundle_NewPhase(bundle, PHASE_DEAD);
#ifndef NORADIUS
    if (bundle->radius.sessiontime)
      bundle_StopSessionTimer(bundle);
#endif
    bundle_StopIdleTimer(bundle);
  }
}

void
bundle_Open(struct bundle *bundle, const char *name, int mask, int force)
{
  /*
   * Please open the given datalink, or all if name == NULL
   */
  struct datalink *dl;

  for (dl = bundle->links; dl; dl = dl->next)
    if (name == NULL || !strcasecmp(dl->name, name)) {
      if ((mask & dl->physical->type) &&
          (dl->state == DATALINK_CLOSED ||
           (force && dl->state == DATALINK_OPENING &&
            dl->dial.timer.state == TIMER_RUNNING) ||
           dl->state == DATALINK_READY)) {
        timer_Stop(&dl->dial.timer);	/* We're finished with this */
        datalink_Up(dl, 1, 1);
        if (mask & PHYS_AUTO)
          break;			/* Only one AUTO link at a time */
      }
      if (name != NULL)
        break;
    }
}

struct datalink *
bundle2datalink(struct bundle *bundle, const char *name)
{
  struct datalink *dl;

  if (name != NULL) {
    for (dl = bundle->links; dl; dl = dl->next)
      if (!strcasecmp(dl->name, name))
        return dl;
  } else if (bundle->links && !bundle->links->next)
    return bundle->links;

  return NULL;
}

int
bundle_ShowLinks(struct cmdargs const *arg)
{
  struct datalink *dl;
  struct pppThroughput *t;
  unsigned long long octets;
  int secs;

  for (dl = arg->bundle->links; dl; dl = dl->next) {
    octets = MAX(dl->physical->link.stats.total.in.OctetsPerSecond,
                 dl->physical->link.stats.total.out.OctetsPerSecond);

    prompt_Printf(arg->prompt, "Name: %s [%s, %s]",
                  dl->name, mode2Nam(dl->physical->type), datalink_State(dl));
    if (dl->physical->link.stats.total.rolling && dl->state == DATALINK_OPEN)
      prompt_Printf(arg->prompt, " bandwidth %d, %llu bps (%llu bytes/sec)",
                    dl->mp.bandwidth ? dl->mp.bandwidth :
                                       physical_GetSpeed(dl->physical),
                    octets * 8, octets);
    prompt_Printf(arg->prompt, "\n");
  }

  t = &arg->bundle->ncp.mp.link.stats.total;
  octets = MAX(t->in.OctetsPerSecond, t->out.OctetsPerSecond);
  secs = t->downtime ? 0 : throughput_uptime(t);
  if (secs > t->SamplePeriod)
    secs = t->SamplePeriod;
  if (secs)
    prompt_Printf(arg->prompt, "Currently averaging %llu bps (%llu bytes/sec)"
                  " over the last %d secs\n", octets * 8, octets, secs);

  return 0;
}

static const char *
optval(struct bundle *bundle, int opt)
{
  return Enabled(bundle, opt) ? "enabled" : "disabled";
}

int
bundle_ShowStatus(struct cmdargs const *arg)
{
  int remaining;

  prompt_Printf(arg->prompt, "Phase %s\n", bundle_PhaseName(arg->bundle));
  prompt_Printf(arg->prompt, " Device:        %s\n", arg->bundle->dev.Name);
  prompt_Printf(arg->prompt, " Interface:     %s @ %lubps",
                arg->bundle->iface->name, arg->bundle->bandwidth);

  if (arg->bundle->upat) {
    int secs = bundle_Uptime(arg->bundle);

    prompt_Printf(arg->prompt, ", up time %d:%02d:%02d", secs / 3600,
                  (secs / 60) % 60, secs % 60);
  }
  prompt_Printf(arg->prompt, "\n Queued:        %lu of %u\n",
                (unsigned long)ncp_QueueLen(&arg->bundle->ncp),
                arg->bundle->cfg.ifqueue);

  prompt_Printf(arg->prompt, "\nDefaults:\n");
  prompt_Printf(arg->prompt, " Label:             %s\n",
                arg->bundle->cfg.label);
  prompt_Printf(arg->prompt, " Auth name:         %s\n",
                arg->bundle->cfg.auth.name);
  prompt_Printf(arg->prompt, " Diagnostic socket: ");
  if (*server.cfg.sockname != '\0') {
    prompt_Printf(arg->prompt, "%s", server.cfg.sockname);
    if (server.cfg.mask != (mode_t)-1)
      prompt_Printf(arg->prompt, ", mask 0%03o", (int)server.cfg.mask);
    prompt_Printf(arg->prompt, "%s\n", server.fd == -1 ? " (not open)" : "");
  } else if (server.cfg.port != 0)
    prompt_Printf(arg->prompt, "TCP port %d%s\n", server.cfg.port,
                  server.fd == -1 ? " (not open)" : "");
  else
    prompt_Printf(arg->prompt, "none\n");

  prompt_Printf(arg->prompt, " Choked Timer:      %us\n",
                arg->bundle->cfg.choked.timeout);

#ifndef NORADIUS
  radius_Show(&arg->bundle->radius, arg->prompt);
#endif

  prompt_Printf(arg->prompt, " Idle Timer:        ");
  if (arg->bundle->cfg.idle.timeout) {
    prompt_Printf(arg->prompt, "%us", arg->bundle->cfg.idle.timeout);
    if (arg->bundle->cfg.idle.min_timeout)
      prompt_Printf(arg->prompt, ", min %us",
                    arg->bundle->cfg.idle.min_timeout);
    remaining = bundle_RemainingIdleTime(arg->bundle);
    if (remaining != -1)
      prompt_Printf(arg->prompt, " (%ds remaining)", remaining);
    prompt_Printf(arg->prompt, "\n");
  } else
    prompt_Printf(arg->prompt, "disabled\n");

  prompt_Printf(arg->prompt, " Filter Decap:      %-20.20s",
                optval(arg->bundle, OPT_FILTERDECAP));
  prompt_Printf(arg->prompt, " ID check:          %s\n",
                optval(arg->bundle, OPT_IDCHECK));
  prompt_Printf(arg->prompt, " Iface-Alias:       %-20.20s",
                optval(arg->bundle, OPT_IFACEALIAS));
#ifndef NOINET6
  prompt_Printf(arg->prompt, " IPCP:              %s\n",
                optval(arg->bundle, OPT_IPCP));
  prompt_Printf(arg->prompt, " IPV6CP:            %-20.20s",
                optval(arg->bundle, OPT_IPV6CP));
#endif
  prompt_Printf(arg->prompt, " Keep-Session:      %s\n",
                optval(arg->bundle, OPT_KEEPSESSION));
  prompt_Printf(arg->prompt, " Loopback:          %-20.20s",
                optval(arg->bundle, OPT_LOOPBACK));
  prompt_Printf(arg->prompt, " PasswdAuth:        %s\n",
                optval(arg->bundle, OPT_PASSWDAUTH));
  prompt_Printf(arg->prompt, " Proxy:             %-20.20s",
                optval(arg->bundle, OPT_PROXY));
  prompt_Printf(arg->prompt, " Proxyall:          %s\n",
                optval(arg->bundle, OPT_PROXYALL));
  prompt_Printf(arg->prompt, " Sticky Routes:     %-20.20s",
                optval(arg->bundle, OPT_SROUTES));
  prompt_Printf(arg->prompt, " TCPMSS Fixup:      %s\n",
                optval(arg->bundle, OPT_TCPMSSFIXUP));
  prompt_Printf(arg->prompt, " Throughput:        %-20.20s",
                optval(arg->bundle, OPT_THROUGHPUT));
  prompt_Printf(arg->prompt, " Utmp Logging:      %s\n",
                optval(arg->bundle, OPT_UTMP));
  prompt_Printf(arg->prompt, " NAS-IP-Address:    %-20.20s",
                optval(arg->bundle, OPT_NAS_IP_ADDRESS));
  prompt_Printf(arg->prompt, " NAS-Identifier:    %s\n",
                optval(arg->bundle, OPT_NAS_IDENTIFIER));

  return 0;
}

static void
bundle_IdleTimeout(void *v)
{
  struct bundle *bundle = (struct bundle *)v;

  log_Printf(LogPHASE, "Idle timer expired\n");
  bundle_StopIdleTimer(bundle);
  bundle_Close(bundle, NULL, CLOSE_STAYDOWN);
}

/*
 *  Start Idle timer. If timeout is reached, we call bundle_Close() to
 *  close LCP and link.
 */
void
bundle_StartIdleTimer(struct bundle *bundle, unsigned secs)
{
  timer_Stop(&bundle->idle.timer);
  if ((bundle->phys_type.open & (PHYS_DEDICATED|PHYS_DDIAL)) !=
      bundle->phys_type.open && bundle->cfg.idle.timeout) {
    time_t now = time(NULL);

    if (secs == 0)
      secs = bundle->cfg.idle.timeout;

    /* We want at least `secs' */
    if (bundle->cfg.idle.min_timeout > secs && bundle->upat) {
      unsigned up = now - bundle->upat;

      if (bundle->cfg.idle.min_timeout > up &&
          bundle->cfg.idle.min_timeout - up > (long long)secs)
        /* Only increase from the current `remaining' value */
        secs = bundle->cfg.idle.min_timeout - up;
    }
    bundle->idle.timer.func = bundle_IdleTimeout;
    bundle->idle.timer.name = "idle";
    bundle->idle.timer.load = secs * SECTICKS;
    bundle->idle.timer.arg = bundle;
    timer_Start(&bundle->idle.timer);
    bundle->idle.done = now + secs;
  }
}

void
bundle_SetIdleTimer(struct bundle *bundle, unsigned timeout,
		    unsigned min_timeout)
{
  bundle->cfg.idle.timeout = timeout;
  bundle->cfg.idle.min_timeout = min_timeout;
  if (ncp_LayersOpen(&bundle->ncp))
    bundle_StartIdleTimer(bundle, 0);
}

void
bundle_StopIdleTimer(struct bundle *bundle)
{
  timer_Stop(&bundle->idle.timer);
  bundle->idle.done = 0;
}

static int
bundle_RemainingIdleTime(struct bundle *bundle)
{
  if (bundle->idle.done)
    return bundle->idle.done - time(NULL);
  return -1;
}

#ifndef NORADIUS

static void
bundle_SessionTimeout(void *v)
{
  struct bundle *bundle = (struct bundle *)v;

  log_Printf(LogPHASE, "Session-Timeout timer expired\n");
  bundle_StopSessionTimer(bundle);
  bundle_Close(bundle, NULL, CLOSE_STAYDOWN);
}

void
bundle_StartSessionTimer(struct bundle *bundle, unsigned secs)
{
  timer_Stop(&bundle->session.timer);
  if ((bundle->phys_type.open & (PHYS_DEDICATED|PHYS_DDIAL)) !=
      bundle->phys_type.open && bundle->radius.sessiontime) {
    time_t now = time(NULL);

    if (secs == 0)
      secs = bundle->radius.sessiontime;

    bundle->session.timer.func = bundle_SessionTimeout;
    bundle->session.timer.name = "session";
    bundle->session.timer.load = secs * SECTICKS;
    bundle->session.timer.arg = bundle;
    timer_Start(&bundle->session.timer);
    bundle->session.done = now + secs;
  }
}

void
bundle_StopSessionTimer(struct bundle *bundle)
{
  timer_Stop(&bundle->session.timer);
  bundle->session.done = 0;
}

#endif

int
bundle_IsDead(struct bundle *bundle)
{
  return !bundle->links || (bundle->phase == PHASE_DEAD && bundle->CleaningUp);
}

static struct datalink *
bundle_DatalinkLinkout(struct bundle *bundle, struct datalink *dl)
{
  struct datalink **dlp;

  for (dlp = &bundle->links; *dlp; dlp = &(*dlp)->next)
    if (*dlp == dl) {
      *dlp = dl->next;
      dl->next = NULL;
      bundle_LinksRemoved(bundle);
      return dl;
    }

  return NULL;
}

static void
bundle_DatalinkLinkin(struct bundle *bundle, struct datalink *dl)
{
  struct datalink **dlp = &bundle->links;

  while (*dlp)
    dlp = &(*dlp)->next;

  *dlp = dl;
  dl->next = NULL;

  bundle_LinkAdded(bundle, dl);
  mp_CheckAutoloadTimer(&bundle->ncp.mp);
}

void
bundle_CleanDatalinks(struct bundle *bundle)
{
  struct datalink **dlp = &bundle->links;
  int found = 0;

  while (*dlp)
    if ((*dlp)->state == DATALINK_CLOSED &&
        (*dlp)->physical->type &
        (PHYS_DIRECT|PHYS_BACKGROUND|PHYS_FOREGROUND)) {
      *dlp = datalink_Destroy(*dlp);
      found++;
    } else
      dlp = &(*dlp)->next;

  if (found)
    bundle_LinksRemoved(bundle);
}

int
bundle_DatalinkClone(struct bundle *bundle, struct datalink *dl,
                     const char *name)
{
  if (bundle2datalink(bundle, name)) {
    log_Printf(LogWARN, "Clone: %s: name already exists\n", name);
    return 0;
  }

  bundle_DatalinkLinkin(bundle, datalink_Clone(dl, name));
  return 1;
}

void
bundle_DatalinkRemove(struct bundle *bundle, struct datalink *dl)
{
  dl = bundle_DatalinkLinkout(bundle, dl);
  if (dl)
    datalink_Destroy(dl);
}

void
bundle_SetLabel(struct bundle *bundle, const char *label)
{
  if (label)
    strncpy(bundle->cfg.label, label, sizeof bundle->cfg.label - 1);
  else
    *bundle->cfg.label = '\0';
}

const char *
bundle_GetLabel(struct bundle *bundle)
{
  return *bundle->cfg.label ? bundle->cfg.label : NULL;
}

int
bundle_LinkSize()
{
  struct iovec iov[SCATTER_SEGMENTS];
  int niov, expect, f;

  iov[0].iov_len = strlen(Version) + 1;
  iov[0].iov_base = NULL;
  niov = 1;
  if (datalink2iov(NULL, iov, &niov, SCATTER_SEGMENTS, NULL, NULL) == -1) {
    log_Printf(LogERROR, "Cannot determine space required for link\n");
    return 0;
  }

  for (f = expect = 0; f < niov; f++)
    expect += iov[f].iov_len;

  return expect;
}

void
bundle_ReceiveDatalink(struct bundle *bundle, int s)
{
  char cmsgbuf[sizeof(struct cmsghdr) + sizeof(int) * SEND_MAXFD];
  int niov, expect, f, *fd, nfd, onfd;
  ssize_t got;
  struct iovec iov[SCATTER_SEGMENTS];
  struct cmsghdr *cmsg;
  struct msghdr msg;
  struct datalink *dl;
  pid_t pid;

  log_Printf(LogPHASE, "Receiving datalink\n");

  /*
   * Create our scatter/gather array - passing NULL gets the space
   * allocation requirement rather than actually flattening the
   * structures.
   */
  iov[0].iov_len = strlen(Version) + 1;
  iov[0].iov_base = NULL;
  niov = 1;
  if (datalink2iov(NULL, iov, &niov, SCATTER_SEGMENTS, NULL, NULL) == -1) {
    log_Printf(LogERROR, "Cannot determine space required for link\n");
    return;
  }

  /* Allocate the scatter/gather array for recvmsg() */
  for (f = expect = 0; f < niov; f++) {
    if ((iov[f].iov_base = malloc(iov[f].iov_len)) == NULL) {
      log_Printf(LogERROR, "Cannot allocate space to receive link\n");
      return;
    }
    if (f)
      expect += iov[f].iov_len;
  }

  /* Set up our message */
  cmsg = (struct cmsghdr *)cmsgbuf;
  cmsg->cmsg_len = sizeof cmsgbuf;
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = 0;

  memset(&msg, '\0', sizeof msg);
  msg.msg_name = NULL;
  msg.msg_namelen = 0;
  msg.msg_iov = iov;
  msg.msg_iovlen = 1;		/* Only send the version at the first pass */
  msg.msg_control = cmsgbuf;
  msg.msg_controllen = sizeof cmsgbuf;

  log_Printf(LogDEBUG, "Expecting %u scatter/gather bytes\n",
             (unsigned)iov[0].iov_len);

  if ((got = recvmsg(s, &msg, MSG_WAITALL)) != (ssize_t)iov[0].iov_len) {
    if (got == -1)
      log_Printf(LogERROR, "Failed recvmsg: %s\n", strerror(errno));
    else
      log_Printf(LogERROR, "Failed recvmsg: Got %zd, not %u\n",
                 got, (unsigned)iov[0].iov_len);
    while (niov--)
      free(iov[niov].iov_base);
    return;
  }

  if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) {
    log_Printf(LogERROR, "Recvmsg: no descriptors received !\n");
    while (niov--)
      free(iov[niov].iov_base);
    return;
  }

  fd = (int *)CMSG_DATA(cmsg);
  nfd = ((caddr_t)cmsg + cmsg->cmsg_len - (caddr_t)fd) / sizeof(int);

  if (nfd < 2) {
    log_Printf(LogERROR, "Recvmsg: %d descriptor%s received (too few) !\n",
               nfd, nfd == 1 ? "" : "s");
    while (nfd--)
      close(fd[nfd]);
    while (niov--)
      free(iov[niov].iov_base);
    return;
  }

  /*
   * We've successfully received two or more open file descriptors
   * through our socket, plus a version string.  Make sure it's the
   * correct version, and drop the connection if it's not.
   */
  if (strncmp(Version, iov[0].iov_base, iov[0].iov_len)) {
    log_Printf(LogWARN, "Cannot receive datalink, incorrect version"
               " (\"%.*s\", not \"%s\")\n", (int)iov[0].iov_len,
               (char *)iov[0].iov_base, Version);
    while (nfd--)
      close(fd[nfd]);
    while (niov--)
      free(iov[niov].iov_base);
    return;
  }

  /*
   * Everything looks good.  Send the other side our process id so that
   * they can transfer lock ownership, and wait for them to send the
   * actual link data.
   */
  pid = getpid();
  if ((got = write(fd[1], &pid, sizeof pid)) != sizeof pid) {
    if (got == -1)
      log_Printf(LogERROR, "Failed write: %s\n", strerror(errno));
    else
      log_Printf(LogERROR, "Failed write: Got %zd, not %d\n", got,
                 (int)(sizeof pid));
    while (nfd--)
      close(fd[nfd]);
    while (niov--)
      free(iov[niov].iov_base);
    return;
  }

  if ((got = readv(fd[1], iov + 1, niov - 1)) != expect) {
    if (got == -1)
      log_Printf(LogERROR, "Failed write: %s\n", strerror(errno));
    else
      log_Printf(LogERROR, "Failed write: Got %zd, not %d\n", got, expect);
    while (nfd--)
      close(fd[nfd]);
    while (niov--)
      free(iov[niov].iov_base);
    return;
  }
  close(fd[1]);

  onfd = nfd;	/* We've got this many in our array */
  nfd -= 2;	/* Don't include p->fd and our reply descriptor */
  niov = 1;	/* Skip the version id */
  dl = iov2datalink(bundle, iov, &niov, sizeof iov / sizeof *iov, fd[0],
                    fd + 2, &nfd);
  if (dl) {

    if (nfd) {
      log_Printf(LogERROR, "bundle_ReceiveDatalink: Failed to handle %d "
                 "auxiliary file descriptors (%d remain)\n", onfd, nfd);
      datalink_Destroy(dl);
      while (nfd--)
        close(fd[onfd--]);
      close(fd[0]);
    } else {
      bundle_DatalinkLinkin(bundle, dl);
      datalink_AuthOk(dl);
      bundle_CalculateBandwidth(dl->bundle);
    }
  } else {
    while (nfd--)
      close(fd[onfd--]);
    close(fd[0]);
    close(fd[1]);
  }

  free(iov[0].iov_base);
}

void
bundle_SendDatalink(struct datalink *dl, int s, struct sockaddr_un *sun)
{
  char cmsgbuf[CMSG_SPACE(sizeof(int) * SEND_MAXFD)];
  const char *constlock;
  char *lock;
  struct cmsghdr *cmsg;
  struct msghdr msg;
  struct iovec iov[SCATTER_SEGMENTS];
  int niov, f, expect, newsid, fd[SEND_MAXFD], nfd, reply[2];
  ssize_t got;
  pid_t newpid;

  log_Printf(LogPHASE, "Transmitting datalink %s\n", dl->name);

  /* Record the base device name for a lock transfer later */
  constlock = physical_LockedDevice(dl->physical);
  if (constlock) {
    lock = alloca(strlen(constlock) + 1);
    strcpy(lock, constlock);
  } else
    lock = NULL;

  bundle_LinkClosed(dl->bundle, dl);
  bundle_DatalinkLinkout(dl->bundle, dl);

  /* Build our scatter/gather array */
  iov[0].iov_len = strlen(Version) + 1;
  iov[0].iov_base = strdup(Version);
  niov = 1;
  nfd = 0;

  fd[0] = datalink2iov(dl, iov, &niov, SCATTER_SEGMENTS, fd + 2, &nfd);

  if (fd[0] != -1 && socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, reply) != -1) {
    /*
     * fd[1] is used to get the peer process id back, then to confirm that
     * we've transferred any device locks to that process id.
     */
    fd[1] = reply[1];

    nfd += 2;			/* Include fd[0] and fd[1] */
    memset(&msg, '\0', sizeof msg);

    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    /*
     * Only send the version to start...  We used to send the whole lot, but
     * this caused problems with our RECVBUF size as a single link is about
     * 22k !  This way, we should bump into no limits.
     */
    msg.msg_iovlen = 1;
    msg.msg_iov = iov;
    msg.msg_control = cmsgbuf;
    msg.msg_controllen = CMSG_SPACE(sizeof(int) * nfd);
    msg.msg_flags = 0;

    cmsg = (struct cmsghdr *)cmsgbuf;
    cmsg->cmsg_len = msg.msg_controllen;
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;

    for (f = 0; f < nfd; f++)
      *((int *)CMSG_DATA(cmsg) + f) = fd[f];

    for (f = 1, expect = 0; f < niov; f++)
      expect += iov[f].iov_len;

    if (setsockopt(reply[0], SOL_SOCKET, SO_SNDBUF, &expect, sizeof(int)) == -1)
      log_Printf(LogERROR, "setsockopt(SO_RCVBUF, %d): %s\n", expect,
                 strerror(errno));
    if (setsockopt(reply[1], SOL_SOCKET, SO_RCVBUF, &expect, sizeof(int)) == -1)
      log_Printf(LogERROR, "setsockopt(SO_RCVBUF, %d): %s\n", expect,
                 strerror(errno));

    log_Printf(LogDEBUG, "Sending %d descriptor%s and %u bytes in scatter"
               "/gather array\n", nfd, nfd == 1 ? "" : "s",
               (unsigned)iov[0].iov_len);

    if ((got = sendmsg(s, &msg, 0)) == -1)
      log_Printf(LogERROR, "Failed sendmsg: %s: %s\n",
                 sun->sun_path, strerror(errno));
    else if (got != (ssize_t)iov[0].iov_len)
      log_Printf(LogERROR, "%s: Failed initial sendmsg: Only sent %zd of %u\n",
                 sun->sun_path, got, (unsigned)iov[0].iov_len);
    else {
      /* We must get the ACK before closing the descriptor ! */
      int res;

      if ((got = read(reply[0], &newpid, sizeof newpid)) == sizeof newpid) {
        log_Printf(LogDEBUG, "Received confirmation from pid %ld\n",
                   (long)newpid);
        if (lock && (res = ID0uu_lock_txfr(lock, newpid)) != UU_LOCK_OK)
            log_Printf(LogERROR, "uu_lock_txfr: %s\n", uu_lockerr(res));

        log_Printf(LogDEBUG, "Transmitting link (%d bytes)\n", expect);
        if ((got = writev(reply[0], iov + 1, niov - 1)) != expect) {
          if (got == -1)
            log_Printf(LogERROR, "%s: Failed writev: %s\n",
                       sun->sun_path, strerror(errno));
          else
            log_Printf(LogERROR, "%s: Failed writev: Wrote %zd of %d\n",
                       sun->sun_path, got, expect);
        }
      } else if (got == -1)
        log_Printf(LogERROR, "%s: Failed socketpair read: %s\n",
                   sun->sun_path, strerror(errno));
      else
        log_Printf(LogERROR, "%s: Failed socketpair read: Got %zd of %d\n",
                   sun->sun_path, got, (int)(sizeof newpid));
    }

    close(reply[0]);
    close(reply[1]);

    newsid = Enabled(dl->bundle, OPT_KEEPSESSION) ||
             tcgetpgrp(fd[0]) == getpgrp();
    while (nfd)
      close(fd[--nfd]);
    if (newsid)
      bundle_setsid(dl->bundle, got != -1);
  }
  close(s);

  while (niov--)
    free(iov[niov].iov_base);
}

int
bundle_RenameDatalink(struct bundle *bundle, struct datalink *ndl,
                      const char *name)
{
  struct datalink *dl;

  if (!strcasecmp(ndl->name, name))
    return 1;

  for (dl = bundle->links; dl; dl = dl->next)
    if (!strcasecmp(dl->name, name))
      return 0;

  datalink_Rename(ndl, name);
  return 1;
}

int
bundle_SetMode(struct bundle *bundle, struct datalink *dl, int mode)
{
  int omode;

  omode = dl->physical->type;
  if (omode == mode)
    return 1;

  if (mode == PHYS_AUTO && !(bundle->phys_type.all & PHYS_AUTO))
    /* First auto link */
    if (bundle->ncp.ipcp.peer_ip.s_addr == INADDR_ANY) {
      log_Printf(LogWARN, "You must `set ifaddr' or `open' before"
                 " changing mode to %s\n", mode2Nam(mode));
      return 0;
    }

  if (!datalink_SetMode(dl, mode))
    return 0;

  if (mode == PHYS_AUTO && !(bundle->phys_type.all & PHYS_AUTO) &&
      bundle->phase != PHASE_NETWORK)
    /* First auto link, we need an interface */
    ipcp_InterfaceUp(&bundle->ncp.ipcp);

  /* Regenerate phys_type and adjust idle timer */
  bundle_LinksRemoved(bundle);

  return 1;
}

void
bundle_setsid(struct bundle *bundle, int holdsession)
{
  /*
   * Lose the current session.  This means getting rid of our pid
   * too so that the tty device will really go away, and any getty
   * etc will be allowed to restart.
   */
  pid_t pid, orig;
  int fds[2];
  char done;
  struct datalink *dl;

  if (!holdsession && bundle_IsDead(bundle)) {
    /*
     * No need to lose our session after all... we're going away anyway
     *
     * We should really stop the timer and pause if holdsession is set and
     * the bundle's dead, but that leaves other resources lying about :-(
     */
    return;
  }

  orig = getpid();
  if (pipe(fds) == -1) {
    log_Printf(LogERROR, "pipe: %s\n", strerror(errno));
    return;
  }
  switch ((pid = fork())) {
    case -1:
      log_Printf(LogERROR, "fork: %s\n", strerror(errno));
      close(fds[0]);
      close(fds[1]);
      return;
    case 0:
      close(fds[1]);
      read(fds[0], &done, 1);		/* uu_locks are mine ! */
      close(fds[0]);
      if (pipe(fds) == -1) {
        log_Printf(LogERROR, "pipe(2): %s\n", strerror(errno));
        return;
      }
      switch ((pid = fork())) {
        case -1:
          log_Printf(LogERROR, "fork(2): %s\n", strerror(errno));
          close(fds[0]);
          close(fds[1]);
          return;
        case 0:
          close(fds[1]);
          bundle_LockTun(bundle);	/* update pid */
          read(fds[0], &done, 1);	/* uu_locks are mine ! */
          close(fds[0]);
          setsid();
          bundle_ChangedPID(bundle);
          log_Printf(LogDEBUG, "%ld -> %ld: %s session control\n",
                     (long)orig, (long)getpid(),
                     holdsession ? "Passed" : "Dropped");
          timer_InitService(0);		/* Start the Timer Service */
          break;
        default:
          close(fds[0]);
          /* Give away all our physical locks (to the final process) */
          for (dl = bundle->links; dl; dl = dl->next)
            if (dl->state != DATALINK_CLOSED)
              physical_ChangedPid(dl->physical, pid);
          write(fds[1], "!", 1);	/* done */
          close(fds[1]);
          _exit(0);
          break;
      }
      break;
    default:
      close(fds[0]);
      /* Give away all our physical locks (to the intermediate process) */
      for (dl = bundle->links; dl; dl = dl->next)
        if (dl->state != DATALINK_CLOSED)
          physical_ChangedPid(dl->physical, pid);
      write(fds[1], "!", 1);	/* done */
      close(fds[1]);
      if (holdsession) {
        int fd, status;

        timer_TermService();
        signal(SIGPIPE, SIG_DFL);
        signal(SIGALRM, SIG_DFL);
        signal(SIGHUP, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        for (fd = getdtablesize(); fd >= 0; fd--)
          close(fd);
        /*
         * Reap the intermediate process.  As we're not exiting but the
         * intermediate is, we don't want it to become defunct.
         */
        waitpid(pid, &status, 0);
        /* Tweak our process arguments.... */
        SetTitle("session owner");
#ifndef NOSUID
        setuid(ID0realuid());
#endif
        /*
         * Hang around for a HUP.  This should happen as soon as the
         * ppp that we passed our ctty descriptor to closes it.
         * NOTE: If this process dies, the passed descriptor becomes
         *       invalid and will give a select() error by setting one
         *       of the error fds, aborting the other ppp.  We don't
         *       want that to happen !
         */
        pause();
      }
      _exit(0);
      break;
  }
}

unsigned
bundle_HighestState(struct bundle *bundle)
{
  struct datalink *dl;
  unsigned result = DATALINK_CLOSED;

  for (dl = bundle->links; dl; dl = dl->next)
    if (result < dl->state)
      result = dl->state;

  return result;
}

int
bundle_Exception(struct bundle *bundle, int fd)
{
  struct datalink *dl;

  for (dl = bundle->links; dl; dl = dl->next)
    if (dl->physical->fd == fd) {
      datalink_Down(dl, CLOSE_NORMAL);
      return 1;
    }

  return 0;
}

void
bundle_AdjustFilters(struct bundle *bundle, struct ncpaddr *local,
                     struct ncpaddr *remote)
{
  filter_AdjustAddr(&bundle->filter.in, local, remote, NULL);
  filter_AdjustAddr(&bundle->filter.out, local, remote, NULL);
  filter_AdjustAddr(&bundle->filter.dial, local, remote, NULL);
  filter_AdjustAddr(&bundle->filter.alive, local, remote, NULL);
}

void
bundle_AdjustDNS(struct bundle *bundle)
{
  struct in_addr *dns = bundle->ncp.ipcp.ns.dns;

  filter_AdjustAddr(&bundle->filter.in, NULL, NULL, dns);
  filter_AdjustAddr(&bundle->filter.out, NULL, NULL, dns);
  filter_AdjustAddr(&bundle->filter.dial, NULL, NULL, dns);
  filter_AdjustAddr(&bundle->filter.alive, NULL, NULL, dns);
}

void
bundle_CalculateBandwidth(struct bundle *bundle)
{
  struct datalink *dl;
  int sp, overhead, maxoverhead;

  bundle->bandwidth = 0;
  bundle->iface->mtu = 0;
  maxoverhead = 0;

  for (dl = bundle->links; dl; dl = dl->next) {
    overhead = ccp_MTUOverhead(&dl->physical->link.ccp);
    if (maxoverhead < overhead)
      maxoverhead = overhead;
    if (dl->state == DATALINK_OPEN) {
      if ((sp = dl->mp.bandwidth) == 0 &&
          (sp = physical_GetSpeed(dl->physical)) == 0)
        log_Printf(LogDEBUG, "%s: %s: Cannot determine bandwidth\n",
                   dl->name, dl->physical->name.full);
      else
        bundle->bandwidth += sp;
      if (!bundle->ncp.mp.active) {
        bundle->iface->mtu = dl->physical->link.lcp.his_mru;
        break;
      }
    }
  }

  if (bundle->bandwidth == 0)
    bundle->bandwidth = 115200;		/* Shrug */

  if (bundle->ncp.mp.active) {
    bundle->iface->mtu = bundle->ncp.mp.peer_mrru;
    overhead = ccp_MTUOverhead(&bundle->ncp.mp.link.ccp);
    if (maxoverhead < overhead)
      maxoverhead = overhead;
  } else if (!bundle->iface->mtu)
    bundle->iface->mtu = DEF_MRU;

#ifndef NORADIUS
  if (bundle->radius.valid && bundle->radius.mtu &&
      bundle->radius.mtu < bundle->iface->mtu) {
    log_Printf(LogLCP, "Reducing MTU to radius value %lu\n",
               bundle->radius.mtu);
    bundle->iface->mtu = bundle->radius.mtu;
  }
#endif

  if (maxoverhead) {
    log_Printf(LogLCP, "Reducing MTU from %lu to %lu (CCP requirement)\n",
               bundle->iface->mtu, bundle->iface->mtu - maxoverhead);
    bundle->iface->mtu -= maxoverhead;
  }

  tun_configure(bundle);

  route_UpdateMTU(bundle);
}

void
bundle_AutoAdjust(struct bundle *bundle, int percent, int what)
{
  struct datalink *dl, *choice, *otherlinkup;

  choice = otherlinkup = NULL;
  for (dl = bundle->links; dl; dl = dl->next)
    if (dl->physical->type == PHYS_AUTO) {
      if (dl->state == DATALINK_OPEN) {
        if (what == AUTO_DOWN) {
          if (choice)
            otherlinkup = choice;
          choice = dl;
        }
      } else if (dl->state == DATALINK_CLOSED) {
        if (what == AUTO_UP) {
          choice = dl;
          break;
        }
      } else {
        /* An auto link in an intermediate state - forget it for the moment */
        choice = NULL;
        break;
      }
    } else if (dl->state == DATALINK_OPEN && what == AUTO_DOWN)
      otherlinkup = dl;

  if (choice) {
    if (what == AUTO_UP) {
      log_Printf(LogPHASE, "%d%% saturation -> Opening link ``%s''\n",
                 percent, choice->name);
      datalink_Up(choice, 1, 1);
      mp_CheckAutoloadTimer(&bundle->ncp.mp);
    } else if (otherlinkup) {	/* Only bring the second-last link down */
      log_Printf(LogPHASE, "%d%% saturation -> Closing link ``%s''\n",
                 percent, choice->name);
      datalink_Close(choice, CLOSE_STAYDOWN);
      mp_CheckAutoloadTimer(&bundle->ncp.mp);
    }
  }
}

int
bundle_WantAutoloadTimer(struct bundle *bundle)
{
  struct datalink *dl;
  int autolink, opened;

  if (bundle->phase == PHASE_NETWORK) {
    for (autolink = opened = 0, dl = bundle->links; dl; dl = dl->next)
      if (dl->physical->type == PHYS_AUTO) {
        if (++autolink == 2 || (autolink == 1 && opened))
          /* Two auto links or one auto and one open in NETWORK phase */
          return 1;
      } else if (dl->state == DATALINK_OPEN) {
        opened++;
        if (autolink)
          /* One auto and one open link in NETWORK phase */
          return 1;
      }
  }

  return 0;
}

void
bundle_ChangedPID(struct bundle *bundle)
{
#ifdef TUNSIFPID
  ioctl(bundle->dev.fd, TUNSIFPID, 0);
#endif
}

int
bundle_Uptime(struct bundle *bundle)
{
  if (bundle->upat)
    return time(NULL) - bundle->upat;

  return 0;
}
