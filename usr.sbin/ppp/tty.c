/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Brian Somers <brian@Awfulhak.org>
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
#include <sys/un.h>
#if defined(__OpenBSD__) || defined(__NetBSD__)
#include <sys/ioctl.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <sys/uio.h>
#include <termios.h>
#include <ttyent.h>
#include <unistd.h>
#ifndef NONETGRAPH
#include <netgraph.h>
#include <netgraph/ng_async.h>
#include <netgraph/ng_message.h>
#include <netgraph/ng_ppp.h>
#include <netgraph/ng_tty.h>
#endif

#include "layer.h"
#include "defs.h"
#include "mbuf.h"
#include "log.h"
#include "timer.h"
#include "lqr.h"
#include "hdlc.h"
#include "throughput.h"
#include "fsm.h"
#include "lcp.h"
#include "ccp.h"
#include "link.h"
#include "async.h"
#include "descriptor.h"
#include "physical.h"
#include "mp.h"
#include "chat.h"
#include "auth.h"
#include "chap.h"
#include "cbcp.h"
#include "datalink.h"
#include "main.h"
#include "id.h"
#include "tty.h"

#if defined(__mac68k__) || defined(__macppc__)
#undef	CRTS_IFLOW
#undef	CCTS_OFLOW
#define	CRTS_IFLOW	CDTRCTS
#define	CCTS_OFLOW	CDTRCTS
#endif

#define	Online(dev)	((dev)->mbits & TIOCM_CD)

struct ttydevice {
  struct device dev;		/* What struct physical knows about */
  struct pppTimer Timer;	/* CD checks */
  int mbits;			/* Current DCD status */
  int carrier_seconds;		/* seconds before CD is *required* */
#ifndef NONETGRAPH
  struct {
    unsigned speed;		/* Pre-line-discipline speed */
    int fd;			/* Pre-line-discipline fd */
    int disc;			/* Old line-discipline */
  } real;
  char hook[sizeof NG_ASYNC_HOOK_SYNC]; /* our ng_socket hook */
  int cs;			/* A netgraph control socket (maybe) */
#endif
  struct termios ios;		/* To be able to reset from raw mode */
};

#define device2tty(d) ((d)->type == TTY_DEVICE ? (struct ttydevice *)d : NULL)

unsigned
tty_DeviceSize(void)
{
  return sizeof(struct ttydevice);
}

/*
 * tty_Timeout() watches the DCD signal and mentions it if it's status
 * changes.
 */
static void
tty_Timeout(void *data)
{
  struct physical *p = data;
  struct ttydevice *dev = device2tty(p->handler);
  int ombits, change;

  timer_Stop(&dev->Timer);
  dev->Timer.load = SECTICKS;		/* Once a second please */
  timer_Start(&dev->Timer);
  ombits = dev->mbits;

  if (p->fd >= 0) {
    if (ioctl(p->fd, TIOCMGET, &dev->mbits) < 0) {
      /* we must be a pty ? */
      if (p->cfg.cd.necessity != CD_DEFAULT)
        log_Printf(LogWARN, "%s: Carrier ioctl not supported, "
                   "using ``set cd off''\n", p->link.name);
      timer_Stop(&dev->Timer);
      dev->mbits = TIOCM_CD;
      return;
    }
  } else
    dev->mbits = 0;

  if (ombits == -1) {
    /* First time looking for carrier */
    if (Online(dev))
      log_Printf(LogPHASE, "%s: %s: CD detected\n", p->link.name, p->name.full);
    else if (++dev->carrier_seconds >= dev->dev.cd.delay) {
      if (dev->dev.cd.necessity == CD_REQUIRED)
        log_Printf(LogPHASE, "%s: %s: Required CD not detected\n",
                   p->link.name, p->name.full);
      else {
        log_Printf(LogPHASE, "%s: %s doesn't support CD\n",
                   p->link.name, p->name.full);
        dev->mbits = TIOCM_CD;		/* Dodgy null-modem cable ? */
      }
      timer_Stop(&dev->Timer);
      /* tty_AwaitCarrier() will notice */
    } else {
      /* Keep waiting */
      log_Printf(LogDEBUG, "%s: %s: Still no carrier (%d/%d)\n",
                 p->link.name, p->name.full, dev->carrier_seconds,
                 dev->dev.cd.delay);
      dev->mbits = -1;
    }
  } else {
    change = ombits ^ dev->mbits;
    if (change & TIOCM_CD) {
      if (dev->mbits & TIOCM_CD)
        log_Printf(LogDEBUG, "%s: offline -> online\n", p->link.name);
      else {
        log_Printf(LogDEBUG, "%s: online -> offline\n", p->link.name);
        log_Printf(LogPHASE, "%s: Carrier lost\n", p->link.name);
        datalink_Down(p->dl, CLOSE_NORMAL);
        timer_Stop(&dev->Timer);
      }
    } else
      log_Printf(LogDEBUG, "%s: Still %sline\n", p->link.name,
                 Online(dev) ? "on" : "off");
  }
}

static void
tty_StartTimer(struct physical *p)
{
  struct ttydevice *dev = device2tty(p->handler);

  timer_Stop(&dev->Timer);
  dev->Timer.load = SECTICKS;
  dev->Timer.func = tty_Timeout;
  dev->Timer.name = "tty CD";
  dev->Timer.arg = p;
  log_Printf(LogDEBUG, "%s: Using tty_Timeout [%p]\n",
             p->link.name, tty_Timeout);
  timer_Start(&dev->Timer);
}

static int
tty_AwaitCarrier(struct physical *p)
{
  struct ttydevice *dev = device2tty(p->handler);

  if (dev->dev.cd.necessity == CD_NOTREQUIRED || physical_IsSync(p))
    return CARRIER_OK;

  if (dev->mbits == -1) {
    if (dev->Timer.state == TIMER_STOPPED) {
      dev->carrier_seconds = 0;
      tty_StartTimer(p);
    }
    return CARRIER_PENDING;			/* Not yet ! */
  }

  return Online(dev) ? CARRIER_OK : CARRIER_LOST;
}

#ifdef NONETGRAPH
#define tty_SetAsyncParams	NULL
#define tty_Write		NULL
#define tty_Read		NULL
#else

static int
isngtty(struct ttydevice *dev)
{
  return dev->real.fd != -1;
}

static void
tty_SetAsyncParams(struct physical *p, u_int32_t mymap, u_int32_t hismap)
{
  struct ttydevice *dev = device2tty(p->handler);
  char asyncpath[NG_PATHSIZ];
  struct ng_async_cfg cfg;

  if (isngtty(dev)) {
    /* Configure the async converter node */

    snprintf(asyncpath, sizeof asyncpath, ".:%s", dev->hook);
    memset(&cfg, 0, sizeof cfg);
    cfg.enabled = 1;
    cfg.accm = mymap | hismap;
    cfg.amru = MAX_MTU;
    cfg.smru = MAX_MRU;
    log_Printf(LogDEBUG, "Configure async node at %s\n", asyncpath);
    if (NgSendMsg(dev->cs, asyncpath, NGM_ASYNC_COOKIE,
                  NGM_ASYNC_CMD_SET_CONFIG, &cfg, sizeof cfg) < 0)
      log_Printf(LogWARN, "%s: Can't configure async node at %s\n",
                 p->link.name, asyncpath);
  } else
    /* No netgraph node, just config the async layer */
    async_SetLinkParams(&p->async, mymap, hismap);
}

static int
LoadLineDiscipline(struct physical *p)
{
  struct ttydevice *dev = device2tty(p->handler);
  u_char rbuf[sizeof(struct ng_mesg) + sizeof(struct nodeinfo)];
  struct ng_mesg *reply;
  struct nodeinfo *info;
  char ttypath[NG_NODESIZ];
  struct ngm_mkpeer ngm;
  struct ngm_connect ngc;
  int ldisc, cs, ds, hot;
  unsigned speed;

  /*
   * Don't use the netgraph line discipline for now.  Using it works, but
   * carrier cannot be detected via TIOCMGET and the device doesn't become
   * selectable with 0 bytes to read when carrier is lost :(
   */
  return 0;

  reply = (struct ng_mesg *)rbuf;
  info = (struct nodeinfo *)reply->data;

  loadmodules(LOAD_VERBOSLY, "netgraph", "ng_tty", "ng_async", "ng_socket",
              NULL);

  /* Get the speed before loading the line discipline */
  speed = physical_GetSpeed(p);

  if (ioctl(p->fd, TIOCGETD, &dev->real.disc) < 0) {
    log_Printf(LogDEBUG, "%s: Couldn't get tty line discipline\n",
               p->link.name);
    return 0;
  }
  ldisc = NETGRAPHDISC;
  if (ID0ioctl(p->fd, TIOCSETD, &ldisc) < 0) {
    log_Printf(LogDEBUG, "%s: Couldn't set NETGRAPHDISC line discipline\n",
               p->link.name);
    return 0;
  }

  /* Get the name of the tty node */
  if (ioctl(p->fd, NGIOCGINFO, info) < 0) {
    log_Printf(LogWARN, "%s: ioctl(NGIOCGINFO): %s\n", p->link.name,
               strerror(errno));
    ID0ioctl(p->fd, TIOCSETD, &dev->real.disc);
    return 0;
  }
  snprintf(ttypath, sizeof ttypath, "%s:", info->name);

  /* Create a socket node for our endpoint (and to send messages via) */
  if (ID0NgMkSockNode(NULL, &cs, &ds) == -1) {
    log_Printf(LogWARN, "%s: NgMkSockNode: %s\n", p->link.name,
               strerror(errno));
    ID0ioctl(p->fd, TIOCSETD, &dev->real.disc);
    return 0;
  }

  /* Set the ``hot char'' on the TTY node */
  hot = HDLC_SYN;
  log_Printf(LogDEBUG, "%s: Set tty hotchar to 0x%02x\n", p->link.name, hot);
  if (NgSendMsg(cs, ttypath, NGM_TTY_COOKIE,
      NGM_TTY_SET_HOTCHAR, &hot, sizeof hot) < 0) {
    log_Printf(LogWARN, "%s: Can't set hot char\n", p->link.name);
    goto failed;
  }

  /* Attach an async converter node */
  snprintf(ngm.type, sizeof ngm.type, "%s", NG_ASYNC_NODE_TYPE);
  snprintf(ngm.ourhook, sizeof ngm.ourhook, "%s", NG_TTY_HOOK);
  snprintf(ngm.peerhook, sizeof ngm.peerhook, "%s", NG_ASYNC_HOOK_ASYNC);
  log_Printf(LogDEBUG, "%s: Send mkpeer async:%s to %s:%s\n", p->link.name,
             ngm.peerhook, ttypath, ngm.ourhook);
  if (NgSendMsg(cs, ttypath, NGM_GENERIC_COOKIE,
      NGM_MKPEER, &ngm, sizeof ngm) < 0) {
    log_Printf(LogWARN, "%s: Can't create %s node\n", p->link.name,
               NG_ASYNC_NODE_TYPE);
    goto failed;
  }

  /* Connect the async node to our socket */
  snprintf(ngc.path, sizeof ngc.path, "%s%s", ttypath, NG_TTY_HOOK);
  snprintf(ngc.peerhook, sizeof ngc.peerhook, "%s", NG_ASYNC_HOOK_SYNC);
  memcpy(ngc.ourhook, ngc.peerhook, sizeof ngc.ourhook);
  log_Printf(LogDEBUG, "%s: Send connect %s:%s to .:%s\n", p->link.name,
             ngc.path, ngc.peerhook, ngc.ourhook);
  if (NgSendMsg(cs, ".:", NGM_GENERIC_COOKIE, NGM_CONNECT,
      &ngc, sizeof ngc) < 0) {
    log_Printf(LogWARN, "%s: Can't connect .:%s -> %s.%s: %s\n",
               p->link.name, ngc.ourhook, ngc.path, ngc.peerhook,
               strerror(errno));
    goto failed;
  }

  /* Get the async node id */
  if (NgSendMsg(cs, ngc.path, NGM_GENERIC_COOKIE, NGM_NODEINFO, NULL, 0) < 0) {
    log_Printf(LogWARN, "%s: Can't request async node info at %s: %s\n",
               p->link.name, ngc.path, strerror(errno));
    goto failed;
  }
  if (NgRecvMsg(cs, reply, sizeof rbuf, NULL) < 0) {
    log_Printf(LogWARN, "%s: Can't obtain async node info at %s: %s\n",
               p->link.name, ngc.path, strerror(errno));
    goto failed;
  }

  /* All done, set up our device state */
  snprintf(dev->hook, sizeof dev->hook, "%s", ngc.ourhook);
  dev->cs = cs;
  dev->real.fd = p->fd;
  p->fd = ds;
  dev->real.speed = speed;
  physical_SetSync(p);

  tty_SetAsyncParams(p, 0xffffffff, 0xffffffff);
  physical_SetupStack(p, dev->dev.name, PHYSICAL_NOFORCE);
  log_Printf(LogPHASE, "%s: Loaded netgraph tty line discipline\n",
             p->link.name);

  return 1;

failed:
  ID0ioctl(p->fd, TIOCSETD, &dev->real.disc);
  close(ds);
  close(cs);

  return 0;
}

static void
UnloadLineDiscipline(struct physical *p)
{
  struct ttydevice *dev = device2tty(p->handler);

  if (isngtty(dev)) {
    if (!physical_SetSpeed(p, dev->real.speed))
      log_Printf(LogWARN, "Couldn't reset tty speed to %d\n", dev->real.speed);
    dev->real.speed = 0;
    close(p->fd);
    p->fd = dev->real.fd;
    dev->real.fd = -1;
    close(dev->cs);
    dev->cs = -1;
    *dev->hook = '\0';
    if (ID0ioctl(p->fd, TIOCSETD, &dev->real.disc) == 0) {
      physical_SetupStack(p, dev->dev.name, PHYSICAL_NOFORCE);
      log_Printf(LogPHASE, "%s: Unloaded netgraph tty line discipline\n",
                 p->link.name);
    } else
      log_Printf(LogWARN, "%s: Failed to unload netgraph tty line discipline\n",
                 p->link.name);
  }
}

static ssize_t
tty_Write(struct physical *p, const void *v, size_t n)
{
  struct ttydevice *dev = device2tty(p->handler);

  if (isngtty(dev))
    return NgSendData(p->fd, dev->hook, v, n) == -1 ? -1 : (ssize_t)n;
  else
    return write(p->fd, v, n);
}

static ssize_t
tty_Read(struct physical *p, void *v, size_t n)
{
  struct ttydevice *dev = device2tty(p->handler);
  char hook[sizeof NG_ASYNC_HOOK_SYNC];

  if (isngtty(dev))
    return NgRecvData(p->fd, v, n, hook);
  else
    return read(p->fd, v, n);
}

#endif /* NETGRAPH */

static int
tty_Raw(struct physical *p)
{
  struct ttydevice *dev = device2tty(p->handler);
  struct termios ios;
  int oldflag;

  log_Printf(LogDEBUG, "%s: Entering tty_Raw\n", p->link.name);

  if (p->type != PHYS_DIRECT && p->fd >= 0 && !Online(dev))
    log_Printf(LogDEBUG, "%s: Raw: descriptor = %d, mbits = %x\n",
              p->link.name, p->fd, dev->mbits);

  if (!physical_IsSync(p)) {
#ifndef NONETGRAPH
    if (!LoadLineDiscipline(p))
#endif
    {
      tcgetattr(p->fd, &ios);
      cfmakeraw(&ios);
      if (p->cfg.rts_cts)
        ios.c_cflag |= CLOCAL | CCTS_OFLOW | CRTS_IFLOW;
      else
        ios.c_cflag |= CLOCAL;

      if (p->type != PHYS_DEDICATED)
        ios.c_cflag |= HUPCL;

      if (tcsetattr(p->fd, TCSANOW, &ios) == -1)
        log_Printf(LogWARN, "%s: tcsetattr: Failed configuring device\n",
                   p->link.name);
    }
  }

  oldflag = fcntl(p->fd, F_GETFL, 0);
  if (oldflag < 0)
    return 0;
  fcntl(p->fd, F_SETFL, oldflag | O_NONBLOCK);

  return 1;
}

static void
tty_Offline(struct physical *p)
{
  struct ttydevice *dev = device2tty(p->handler);

  if (p->fd >= 0) {
    timer_Stop(&dev->Timer);
    dev->mbits &= ~TIOCM_DTR;	/* XXX: Hmm, what's this supposed to do ? */
    if (Online(dev)) {
      struct termios tio;

      tcgetattr(p->fd, &tio);
      if (cfsetspeed(&tio, B0) == -1 || tcsetattr(p->fd, TCSANOW, &tio) == -1)
        log_Printf(LogWARN, "%s: Unable to set physical to speed 0\n",
                   p->link.name);
    }
  }
}

static void
tty_Cooked(struct physical *p)
{
  struct ttydevice *dev = device2tty(p->handler);
  int oldflag;

  tty_Offline(p);	/* In case of emergency close()s */

  tcflush(p->fd, TCIOFLUSH);

  if (!physical_IsSync(p) && tcsetattr(p->fd, TCSAFLUSH, &dev->ios) == -1)
    log_Printf(LogWARN, "%s: tcsetattr: Unable to restore device settings\n",
               p->link.name);

#ifndef NONETGRAPH
  UnloadLineDiscipline(p);
#endif

  if ((oldflag = fcntl(p->fd, F_GETFL, 0)) != -1)
    fcntl(p->fd, F_SETFL, oldflag & ~O_NONBLOCK);
}

static void
tty_StopTimer(struct physical *p)
{
  struct ttydevice *dev = device2tty(p->handler);

  timer_Stop(&dev->Timer);
}

static void
tty_Free(struct physical *p)
{
  struct ttydevice *dev = device2tty(p->handler);

  tty_Offline(p);	/* In case of emergency close()s */
  free(dev);
}

static unsigned
tty_Speed(struct physical *p)
{
  struct termios ios;

  if (tcgetattr(p->fd, &ios) == -1)
    return 0;

  return SpeedToUnsigned(cfgetispeed(&ios));
}

static const char *
tty_OpenInfo(struct physical *p)
{
  struct ttydevice *dev = device2tty(p->handler);
  static char buf[13];

  if (Online(dev))
    strcpy(buf, "with");
  else
    strcpy(buf, "no");
  strcat(buf, " carrier");

  return buf;
}

static int
tty_Slot(struct physical *p)
{
  struct ttyent *ttyp;
  int slot;

  setttyent();
  for (slot = 1; (ttyp = getttyent()); ++slot)
    if (!strcmp(ttyp->ty_name, p->name.base)) {
      endttyent();
      return slot;
    }

  endttyent();
  return -1;
}

static void
tty_device2iov(struct device *d, struct iovec *iov, int *niov,
               int maxiov __unused,
#ifndef NONETGRAPH
               int *auxfd, int *nauxfd
#else
               int *auxfd __unused, int *nauxfd __unused
#endif
               )
{
  struct ttydevice *dev;
  int sz = physical_MaxDeviceSize();

  iov[*niov].iov_base = d = realloc(d, sz);
  if (d == NULL) {
    log_Printf(LogALERT, "Failed to allocate memory: %d\n", sz);
    AbortProgram(EX_OSERR);
  }
  iov[*niov].iov_len = sz;
  (*niov)++;

  dev = device2tty(d);

#ifndef NONETGRAPH
  if (dev->cs >= 0) {
    *auxfd = dev->cs;
    (*nauxfd)++;
  }
#endif

  if (dev->Timer.state != TIMER_STOPPED) {
    timer_Stop(&dev->Timer);
    dev->Timer.state = TIMER_RUNNING;
  }
}

static struct device basettydevice = {
  TTY_DEVICE,
  "tty",
  0,
  { CD_VARIABLE, DEF_TTYCDDELAY },
  tty_AwaitCarrier,
  NULL,
  tty_Raw,
  tty_Offline,
  tty_Cooked,
  tty_SetAsyncParams,
  tty_StopTimer,
  tty_Free,
  tty_Read,
  tty_Write,
  tty_device2iov,
  tty_Speed,
  tty_OpenInfo,
  tty_Slot
};

struct device *
tty_iov2device(int type, struct physical *p, struct iovec *iov, int *niov,
               int maxiov __unused,
#ifndef NONETGRAPH
               int *auxfd, int *nauxfd
#else
               int *auxfd __unused, int *nauxfd __unused
#endif
               )
{
  if (type == TTY_DEVICE) {
    struct ttydevice *dev = (struct ttydevice *)iov[(*niov)++].iov_base;

    dev = realloc(dev, sizeof *dev);	/* Reduce to the correct size */
    if (dev == NULL) {
      log_Printf(LogALERT, "Failed to allocate memory: %d\n",
                 (int)(sizeof *dev));
      AbortProgram(EX_OSERR);
    }

#ifndef NONETGRAPH
    if (*nauxfd) {
      dev->cs = *auxfd;
      (*nauxfd)--;
    } else
      dev->cs = -1;
#endif

    /* Refresh function pointers etc */
    memcpy(&dev->dev, &basettydevice, sizeof dev->dev);

    physical_SetupStack(p, dev->dev.name, PHYSICAL_NOFORCE);
    if (dev->Timer.state != TIMER_STOPPED) {
      dev->Timer.state = TIMER_STOPPED;
      p->handler = &dev->dev;		/* For the benefit of StartTimer */
      tty_StartTimer(p);
    }
    return &dev->dev;
  }

  return NULL;
}

struct device *
tty_Create(struct physical *p)
{
  struct ttydevice *dev;
  struct termios ios;
  int oldflag;

  if (p->fd < 0 || !isatty(p->fd))
    /* Don't want this */
    return NULL;

  if (*p->name.full == '\0') {
    physical_SetDevice(p, ttyname(p->fd));
    log_Printf(LogDEBUG, "%s: Input is a tty (%s)\n",
               p->link.name, p->name.full);
  } else
    log_Printf(LogDEBUG, "%s: Opened %s\n", p->link.name, p->name.full);

  /* We're gonna return a ttydevice (unless something goes horribly wrong) */

  if ((dev = malloc(sizeof *dev)) == NULL) {
    /* Complete failure - parent doesn't continue trying to ``create'' */
    close(p->fd);
    p->fd = -1;
    return NULL;
  }

  memcpy(&dev->dev, &basettydevice, sizeof dev->dev);
  memset(&dev->Timer, '\0', sizeof dev->Timer);
  dev->mbits = -1;
#ifndef NONETGRAPH
  dev->real.speed = 0;
  dev->real.fd = -1;
  dev->real.disc = -1;
  *dev->hook = '\0';
#endif
  tcgetattr(p->fd, &ios);
  dev->ios = ios;

  if (p->cfg.cd.necessity != CD_DEFAULT)
    /* Any override is ok for the tty device */
    dev->dev.cd = p->cfg.cd;

  log_Printf(LogDEBUG, "%s: tty_Create: physical (get): fd = %d,"
             " iflag = %lx, oflag = %lx, cflag = %lx\n", p->link.name, p->fd,
             (u_long)ios.c_iflag, (u_long)ios.c_oflag, (u_long)ios.c_cflag);

  cfmakeraw(&ios);
  if (p->cfg.rts_cts)
    ios.c_cflag |= CLOCAL | CCTS_OFLOW | CRTS_IFLOW;
  else {
    ios.c_cflag |= CLOCAL;
    ios.c_iflag |= IXOFF;
  }
  ios.c_iflag |= IXON;
  if (p->type != PHYS_DEDICATED)
    ios.c_cflag |= HUPCL;

  if (p->type != PHYS_DIRECT) {
      /* Change tty speed when we're not in -direct mode */
      ios.c_cflag &= ~(CSIZE | PARODD | PARENB);
      ios.c_cflag |= p->cfg.parity;
      if (cfsetspeed(&ios, UnsignedToSpeed(p->cfg.speed)) == -1)
	log_Printf(LogWARN, "%s: %s: Unable to set speed to %d\n",
		  p->link.name, p->name.full, p->cfg.speed);
  }

  if (tcsetattr(p->fd, TCSADRAIN, &ios) == -1) {
    log_Printf(LogWARN, "%s: tcsetattr: Failed configuring device\n",
               p->link.name);
    if (p->type != PHYS_DIRECT && p->cfg.speed > 115200)
      log_Printf(LogWARN, "%.*s             Perhaps the speed is unsupported\n",
                 (int)strlen(p->link.name), "");
  }

  log_Printf(LogDEBUG, "%s: physical (put): iflag = %lx, oflag = %lx, "
            "cflag = %lx\n", p->link.name, (u_long)ios.c_iflag,
            (u_long)ios.c_oflag, (u_long)ios.c_cflag);

  oldflag = fcntl(p->fd, F_GETFL, 0);
  if (oldflag < 0) {
    /* Complete failure - parent doesn't continue trying to ``create'' */

    log_Printf(LogWARN, "%s: Open: Cannot get physical flags: %s\n",
               p->link.name, strerror(errno));
    tty_Cooked(p);
    close(p->fd);
    p->fd = -1;
    free(dev);
    return NULL;
  } else
    fcntl(p->fd, F_SETFL, oldflag & ~O_NONBLOCK);

  physical_SetupStack(p, dev->dev.name, PHYSICAL_NOFORCE);

  return &dev->dev;
}
