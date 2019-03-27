/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2011 Daniel Braniss <danny@cs.huji.ac.il>
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
 */
/*
 | $Id: iscsi.c 752 2009-08-20 11:23:28Z danny $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_iscsi_initiator.h"

#include <sys/param.h>
#include <sys/capsicum.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/conf.h>
#include <sys/bus.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/ctype.h>
#include <sys/errno.h>
#include <sys/sysctl.h>
#include <sys/file.h>
#include <sys/uio.h>
#include <sys/socketvar.h>
#include <sys/socket.h>
#include <sys/protosw.h>
#include <sys/proc.h>
#include <sys/ioccom.h>
#include <sys/queue.h>
#include <sys/kthread.h>
#include <sys/mbuf.h>
#include <sys/syslog.h>
#include <vm/uma.h>
#include <sys/sx.h>

#include <dev/iscsi_initiator/iscsi.h>
#include <dev/iscsi_initiator/iscsivar.h>
static char *iscsi_driver_version = "2.3.1";

static struct isc_softc *isc;

MALLOC_DEFINE(M_ISCSI, "iSCSI", "iSCSI driver");
MALLOC_DEFINE(M_ISCSIBUF, "iSCbuf", "iSCSI buffers");
static MALLOC_DEFINE(M_TMP, "iSCtmp", "iSCSI tmp");

#ifdef ISCSI_INITIATOR_DEBUG
int iscsi_debug = ISCSI_INITIATOR_DEBUG;
SYSCTL_INT(_debug, OID_AUTO, iscsi_initiator, CTLFLAG_RW, &iscsi_debug, 0,
	"iSCSI driver debug flag");

struct mtx iscsi_dbg_mtx;
#endif

static int max_sessions = MAX_SESSIONS;
SYSCTL_INT(_net, OID_AUTO, iscsi_initiator_max_sessions, CTLFLAG_RDTUN,
    &max_sessions, 0, "Max sessions allowed");
static int max_pdus = MAX_PDUS;
SYSCTL_INT(_net, OID_AUTO, iscsi_initiator_max_pdus, CTLFLAG_RDTUN,
    &max_pdus, 0, "Max PDU pool");

static char isid[6+1] = {
     0x80,
     'D',
     'I',
     'B',
     '0',
     '0',
     0
};

static int	i_create_session(struct cdev *dev, int *ndev);

static int	i_ping(struct cdev *dev);
static int	i_send(struct cdev *dev, caddr_t arg, struct thread *td);
static int	i_recv(struct cdev *dev, caddr_t arg, struct thread *td);
static int	i_setsoc(isc_session_t *sp, int fd, struct thread *td);
static int	i_fullfeature(struct cdev *dev, int flag);

static d_open_t iscsi_open;
static d_close_t iscsi_close;
static d_ioctl_t iscsi_ioctl;
#ifdef ISCSI_INITIATOR_DEBUG
static d_read_t iscsi_read;
#endif

static struct cdevsw iscsi_cdevsw = {
     .d_version = D_VERSION,
     .d_open	= iscsi_open,
     .d_close	= iscsi_close,
     .d_ioctl	= iscsi_ioctl,
#ifdef ISCSI_INITIATOR_DEBUG
     .d_read	= iscsi_read,
#endif
     .d_name	= "iSCSI",
};

static int
iscsi_open(struct cdev *dev, int flags, int otype, struct thread *td)
{
     debug_called(8);

     debug(7, "dev=%d", dev2unit(dev));

     if(dev2unit(dev) > max_sessions) {
	  // should not happen
          return ENODEV;
     }
     return 0;
}

static int
iscsi_close(struct cdev *dev, int flag, int otyp, struct thread *td)
{
     isc_session_t	*sp;

     debug_called(8);

     debug(3, "session=%d flag=%x", dev2unit(dev), flag);

     if(dev2unit(dev) == max_sessions) {
	  return 0;
     }
     sp = dev->si_drv2;
     if(sp != NULL) {
	  sdebug(3, "sp->flags=%x", sp->flags );
	  /*
	   | if still in full phase, this probably means
	   | that something went really bad.
	   | it could be a result from 'shutdown', in which case
	   | we will ignore it (so buffers can be flushed).
	   | the problem is that there is no way of differentiating
	   | between a shutdown procedure and 'iscontrol' dying.
	   */
	  if(sp->flags & ISC_FFPHASE)
	       // delay in case this is a shutdown.
	       tsleep(sp, PRIBIO, "isc-cls", 60*hz);
	  ism_stop(sp);
     }
     debug(2, "done");
     return 0;
}

static int
iscsi_ioctl(struct cdev *dev, u_long cmd, caddr_t arg, int mode, struct thread *td)
{
     struct isc_softc	*sc;
     isc_session_t	*sp;
     isc_opt_t		*opt;
     int		error;

     debug_called(8);

     error = 0;
     if(dev2unit(dev) == max_sessions) {
	  /*
	   | non Session commands
	   */
	  sc = dev->si_drv1;
	  if(sc == NULL)
	       return ENXIO;

	  switch(cmd) {
	  case ISCSISETSES:
	       error = i_create_session(dev, (int *)arg);
	       if(error == 0)
		    break;

	  default:
	       error = ENXIO;
	  }
	  return error;
     }
     /*
      | session commands
      */
     sp = dev->si_drv2;
     if(sp == NULL)
	  return ENXIO;

     sdebug(6, "dev=%d cmd=%d", dev2unit(dev), (int)(cmd & 0xff));

     switch(cmd) {
     case ISCSISETSOC:
	  error = i_setsoc(sp, *(u_int *)arg, td);
	  break;

     case ISCSISETOPT:
	  opt = (isc_opt_t *)arg;
	  error = i_setopt(sp, opt);
	  break;

     case ISCSISEND:
	  error = i_send(dev, arg, td);
	  break;

     case ISCSIRECV:
	  error = i_recv(dev, arg, td);
	  break;

     case ISCSIPING:
	  error = i_ping(dev);
	  break;

     case ISCSISTART:
	  error = sp->soc == NULL? ENOTCONN: i_fullfeature(dev, 1);
	  if(error == 0) {
	       sp->proc = td->td_proc;
	       SYSCTL_ADD_INT(&sp->clist, SYSCTL_CHILDREN(sp->oid),
			       OID_AUTO, "pid", CTLFLAG_RD,
			       &sp->proc->p_pid, sizeof(pid_t), "control process id");
	  }
	  break;

     case ISCSIRESTART:
	  error = sp->soc == NULL? ENOTCONN: i_fullfeature(dev, 2);
	  break;

     case ISCSISTOP:
	  error = i_fullfeature(dev, 0);
	  break;
	  
     case ISCSISIGNAL: {
	  int sig = *(int *)arg;

	  if(sig < 0 || sig > _SIG_MAXSIG)
	       error = EINVAL;
	  else
		sp->signal = sig;
	  break;
     }

     case ISCSIGETCAM: {
	  iscsi_cam_t *cp = (iscsi_cam_t *)arg;

	  error = ic_getCamVals(sp, cp);
	  break;
     }

     default:
	  error = ENOIOCTL;
     }

     return error;
}

static int
iscsi_read(struct cdev *dev, struct uio *uio, int ioflag)
{
#ifdef  ISCSI_INITIATOR_DEBUG
     struct isc_softc	*sc;
     isc_session_t	*sp;
     pduq_t 		*pq;
     char		buf[1024];

     sc = dev->si_drv1;
     sp = dev->si_drv2;
     if(dev2unit(dev) == max_sessions) {
	  sprintf(buf, "/----- Session ------/\n");
	  uiomove(buf, strlen(buf), uio);
	  int	i = 0;

	  TAILQ_FOREACH(sp, &sc->isc_sess, sp_link) {
	       if(uio->uio_resid == 0)
		    return 0;
	       sprintf(buf, "%03d] '%s' '%s'\n", i++, sp->opt.targetAddress, sp->opt.targetName);
	       uiomove(buf, strlen(buf), uio);
	  }
	  sprintf(buf, "free npdu_alloc=%d, npdu_max=%d\n", sc->npdu_alloc, sc->npdu_max);
	  uiomove(buf, strlen(buf), uio);
     }
     else {
	  int	i = 0;
	  struct socket	*so = sp->soc;
#define pukeit(i, pq) do {\
	       sprintf(buf, "%03d] %06x %02x %06x %06x %jd\n",\
		       i, ntohl(pq->pdu.ipdu.bhs.CmdSN),\
		       pq->pdu.ipdu.bhs.opcode, ntohl(pq->pdu.ipdu.bhs.itt),\
		       ntohl(pq->pdu.ipdu.bhs.ExpStSN),\
		       (intmax_t)pq->ts.sec);\
	       } while(0)

	  sprintf(buf, "%d/%d /---- hld -----/\n", sp->stats.nhld, sp->stats.max_hld);
	  uiomove(buf, strlen(buf), uio);
	  TAILQ_FOREACH(pq, &sp->hld, pq_link) {
	       if(uio->uio_resid == 0)
		    return 0;
	       pukeit(i, pq); i++;
	       uiomove(buf, strlen(buf), uio);
	  }
	  sprintf(buf, "%d/%d /---- rsp -----/\n", sp->stats.nrsp, sp->stats.max_rsp);
	  uiomove(buf, strlen(buf), uio);
	  i = 0;
	  TAILQ_FOREACH(pq, &sp->rsp, pq_link) {
	       if(uio->uio_resid == 0)
		    return 0;
	       pukeit(i, pq); i++;
	       uiomove(buf, strlen(buf), uio);
	  }
	  sprintf(buf, "%d/%d /---- csnd -----/\n", sp->stats.ncsnd, sp->stats.max_csnd);
	  i = 0;
	  uiomove(buf, strlen(buf), uio);
	  TAILQ_FOREACH(pq, &sp->csnd, pq_link) {
	       if(uio->uio_resid == 0)
		    return 0;
	       pukeit(i, pq); i++;
	       uiomove(buf, strlen(buf), uio);
	  }
	  sprintf(buf, "%d/%d /---- wsnd -----/\n", sp->stats.nwsnd, sp->stats.max_wsnd);
	  i = 0;
	  uiomove(buf, strlen(buf), uio);
	  TAILQ_FOREACH(pq, &sp->wsnd, pq_link) {
	       if(uio->uio_resid == 0)
		    return 0;
	       pukeit(i, pq); i++;
	       uiomove(buf, strlen(buf), uio);
	  }
	  sprintf(buf, "%d/%d /---- isnd -----/\n", sp->stats.nisnd, sp->stats.max_isnd);
	  i = 0;
	  uiomove(buf, strlen(buf), uio);
	  TAILQ_FOREACH(pq, &sp->isnd, pq_link) {
	       if(uio->uio_resid == 0)
		    return 0;
	       pukeit(i, pq); i++;
	       uiomove(buf, strlen(buf), uio);
	  }

	  sprintf(buf, "/---- Stats ---/\n");
	  uiomove(buf, strlen(buf), uio);

	  sprintf(buf, "recv=%d sent=%d\n", sp->stats.nrecv, sp->stats.nsent);
	  uiomove(buf, strlen(buf), uio);

	  sprintf(buf, "flags=%x pdus: alloc=%d max=%d\n", 
		  sp->flags, sc->npdu_alloc, sc->npdu_max);
	  uiomove(buf, strlen(buf), uio);

	  sprintf(buf, "cws=%d last cmd=%x exp=%x max=%x stat=%x itt=%x\n",
		  sp->cws, sp->sn.cmd, sp->sn.expCmd, sp->sn.maxCmd, sp->sn.stat, sp->sn.itt);
	  uiomove(buf, strlen(buf), uio);

	  sprintf(buf, "/---- socket -----/\nso_count=%d so_state=%x\n", so->so_count, so->so_state);
	  uiomove(buf, strlen(buf), uio);

     }
#endif
     return 0;
}

static int
i_ping(struct cdev *dev)
{
     return 0;
}
/*
 | low level I/O
 */
static int
i_setsoc(isc_session_t *sp, int fd, struct thread *td)
{
     cap_rights_t rights;
     int error = 0;

     if(sp->soc != NULL)
	  isc_stop_receiver(sp);

     error = getsock_cap(td, fd, cap_rights_init(&rights, CAP_SOCK_CLIENT),
	     &sp->fp, NULL, NULL);
     if(error)
	  return error;

     sp->soc = sp->fp->f_data;
     sp->td = td;
     isc_start_receiver(sp);

     return error;
}

static int
i_send(struct cdev *dev, caddr_t arg, struct thread *td)
{
     isc_session_t	*sp = dev->si_drv2;
     caddr_t		bp;
     pduq_t		*pq;
     pdu_t		*pp;
     int		n, error;

     debug_called(8);

     if(sp->soc == NULL)
	  return ENOTCONN;

     if((pq = pdu_alloc(sp->isc, M_NOWAIT)) == NULL)
	  return EAGAIN;
     pp = &pq->pdu;
     pq->pdu = *(pdu_t *)arg;
     if((error = i_prepPDU(sp, pq)) != 0)
	  goto out;

     bp = NULL;
     if((pq->len - sizeof(union ipdu_u)) > 0) {
	  pq->buf = bp = malloc(pq->len - sizeof(union ipdu_u), M_ISCSIBUF, M_NOWAIT);
	  if(pq->buf == NULL) {
	       error = EAGAIN;
	       goto out;
	  }
     }
     else
	  pq->buf = NULL; // just in case?

     sdebug(2, "len=%d ahs_len=%d ds_len=%d buf=%zu@%p",
	    pq->len, pp->ahs_len, pp->ds_len, pq->len - sizeof(union ipdu_u), bp);

     if(pp->ahs_len) {
	  // XXX: never tested, looks suspicious
	  n = pp->ahs_len;
	  error = copyin(pp->ahs_addr, bp, n);
	  if(error != 0) {
	       sdebug(3, "copyin ahs: error=%d", error);
	       goto out;
	  }
	  pp->ahs_addr = (ahs_t *)bp;
	  bp += n;
     }
     if(pp->ds_len) {
	  n = pp->ds_len;
	  error = copyin(pp->ds_addr, bp, n);
	  if(error != 0) {
	       sdebug(3, "copyin ds: error=%d", error);
	       goto out;
	  }
	  pp->ds_addr = bp;
	  bp += n;
	  while(n & 03) {
	       n++;
	       *bp++ = 0;
	  }
     }

     error = isc_qout(sp, pq);
     if(error == 0)
	  wakeup(&sp->flags); // XXX: to 'push' proc_out ...
out:
     if(error)
	  pdu_free(sp->isc, pq);

     return error;
}

static int
i_recv(struct cdev *dev, caddr_t arg, struct thread *td)
{
     isc_session_t	*sp = dev->si_drv2;
     pduq_t		*pq;
     pdu_t		*pp, *up;
     caddr_t		bp;
     int		error, mustfree, cnt;
     size_t		need, have, n;

     debug_called(8);

     if(sp == NULL)
	  return EIO;

     if(sp->soc == NULL)
	  return ENOTCONN;
     cnt = 6;     // XXX: maybe the user can request a time out?
     mtx_lock(&sp->rsp_mtx);
     while((pq = TAILQ_FIRST(&sp->rsp)) == NULL) {
	  msleep(&sp->rsp, &sp->rsp_mtx, PRIBIO, "isc_rsp", hz*10);
	  if(cnt-- == 0) break; // XXX: for now, needs work
     }
     if(pq != NULL) {
	  sp->stats.nrsp--;
	  TAILQ_REMOVE(&sp->rsp, pq, pq_link);
     }
     mtx_unlock(&sp->rsp_mtx);

     sdebug(6, "cnt=%d", cnt);

     if(pq == NULL) {
	  error = ENOTCONN;
	  sdebug(3, "error=%d sp->flags=%x ", error, sp->flags);
	  return error;
     }
     up = (pdu_t *)arg;
     pp = &pq->pdu;
     up->ipdu = pp->ipdu;
     n = 0;
     up->ds_len = 0;
     up->ahs_len = 0;
     error = 0;

     if(pq->mp) {
	  u_int	len;

	  // Grr...
	  len = 0;
	  if(pp->ahs_len) {
	       len += pp->ahs_len;
	  }
	  if(pp->ds_len) {
	       len += pp->ds_len;
	  }

	  mustfree = 0;
	  if(len > pq->mp->m_len) {
	       mustfree++;
	       bp = malloc(len, M_TMP, M_WAITOK);
	       sdebug(4, "need mbufcopy: %d", len);
	       i_mbufcopy(pq->mp, bp, len);
	  } 
	  else
	       bp = mtod(pq->mp, caddr_t);

	  if(pp->ahs_len) {
	       need = pp->ahs_len;
	       n = MIN(up->ahs_size, need);
	       error = copyout(bp, (caddr_t)up->ahs_addr, n);
	       up->ahs_len = n;
	       bp += need;
	  }
	  if(!error && pp->ds_len) {
	       need = pp->ds_len;
	       if((have = up->ds_size) == 0) {
		    have = up->ahs_size - n;
		    up->ds_addr = (caddr_t)up->ahs_addr + n;
	       }
	       n = MIN(have, need);
	       error = copyout(bp, (caddr_t)up->ds_addr, n);
	       up->ds_len = n;
	  }

	  if(mustfree)
	       free(bp, M_TMP);
     }

     sdebug(6, "len=%d ahs_len=%d ds_len=%d", pq->len, pp->ahs_len, pp->ds_len);

     pdu_free(sp->isc, pq);

     return error;
}

static int
i_fullfeature(struct cdev *dev, int flag)
{
     isc_session_t	*sp = dev->si_drv2;
     int		error;

     sdebug(2, "flag=%d", flag);

     error = 0;
     switch(flag) {
     case 0: // stop
         sp->flags &= ~ISC_FFPHASE;
         break;
     case 1: // start
         sp->flags |= ISC_FFPHASE;
         error = ic_init(sp);
         break;
     case 2: // restart
         sp->flags |= ISC_FFPHASE;
         ism_restart(sp);
         break;
     }
     return error;
}

static int
i_create_session(struct cdev *dev, int *ndev)
{ 
     struct isc_softc	*sc = dev->si_drv1;
     isc_session_t	*sp;
     int		error, n;

     debug_called(8);

     sp = malloc(sizeof(isc_session_t), M_ISCSI, M_WAITOK | M_ZERO);
     if(sp == NULL)
	  return ENOMEM;

     sx_xlock(&sc->unit_sx);
     if((n = alloc_unr(sc->unit)) < 0) {
	  sx_unlock(&sc->unit_sx);
	  free(sp, M_ISCSI);
	  xdebug("too many sessions!");
	  return EPERM;
     }
     sx_unlock(&sc->unit_sx);

     mtx_lock(&sc->isc_mtx);
     TAILQ_INSERT_TAIL(&sc->isc_sess, sp, sp_link);
     isc->nsess++;
     mtx_unlock(&sc->isc_mtx);

     sp->dev = make_dev(&iscsi_cdevsw, n, UID_ROOT, GID_WHEEL, 0600, "iscsi%d", n);
     *ndev = sp->sid = n;
     sp->isc = sc;
     sp->dev->si_drv1 = sc;
     sp->dev->si_drv2 = sp;

     sp->opt.maxRecvDataSegmentLength = 8192;
     sp->opt.maxXmitDataSegmentLength = 8192;
     sp->opt.maxBurstLength = 65536;	// 64k
     sp->opt.maxluns = ISCSI_MAX_LUNS;

     error = ism_start(sp);

     return error;
}

#ifdef notused
static void
iscsi_counters(isc_session_t *sp)
{
     int	h, r, s;
     pduq_t	*pq;

#define _puke(i, pq) do {\
	       debug(2, "%03d] %06x %02x %x %ld %jd %x\n",\
		       i, ntohl( pq->pdu.ipdu.bhs.CmdSN), \
		       pq->pdu.ipdu.bhs.opcode, ntohl(pq->pdu.ipdu.bhs.itt),\
		       (long)pq->ts.sec, pq->ts.frac, pq->flags);\
	       } while(0)

     h = r = s = 0; 
     TAILQ_FOREACH(pq, &sp->hld, pq_link) {
	  _puke(h, pq);
	  h++;
     }
     TAILQ_FOREACH(pq, &sp->rsp, pq_link) r++;
     TAILQ_FOREACH(pq, &sp->csnd, pq_link) s++;
     TAILQ_FOREACH(pq, &sp->wsnd, pq_link) s++;
     TAILQ_FOREACH(pq, &sp->isnd, pq_link) s++;
     debug(2, "hld=%d rsp=%d snd=%d", h, r, s);
}
#endif

static void
iscsi_shutdown(void *v)
{
     struct isc_softc	*sc = v;
     isc_session_t	*sp;
     int	n;

     debug_called(8);
     if(sc == NULL) {
	  xdebug("sc is NULL!");
	  return;
     }
#ifdef DO_EVENTHANDLER
     if(sc->eh == NULL)
	  debug(2, "sc->eh is NULL");
     else {
	  EVENTHANDLER_DEREGISTER(shutdown_pre_sync, sc->eh);
	  debug(2, "done n=%d", sc->nsess);
     }
#endif
     n = 0;
     TAILQ_FOREACH(sp, &sc->isc_sess, sp_link) {
	  debug(2, "%2d] sp->flags=0x%08x", n, sp->flags);
	  n++;
     }
     debug(2, "done");
}

static void
free_pdus(struct isc_softc *sc)
{
     debug_called(8);

     if(sc->pdu_zone != NULL) {
	  uma_zdestroy(sc->pdu_zone);
	  sc->pdu_zone = NULL;
     }
}

static int
iscsi_start(void)
{
     debug_called(8);

     isc =  malloc(sizeof(struct isc_softc), M_ISCSI, M_ZERO|M_WAITOK);
     mtx_init(&isc->isc_mtx, "iscsi-isc", NULL, MTX_DEF);

     TAILQ_INIT(&isc->isc_sess);
     /*
      | now init the free pdu list
      */
     isc->pdu_zone = uma_zcreate("pdu", sizeof(pduq_t),
				 NULL, NULL, NULL, NULL,
				 0, 0);
     uma_zone_set_max(isc->pdu_zone, max_pdus);
     isc->unit = new_unrhdr(0, max_sessions-1, NULL);
     sx_init(&isc->unit_sx, "iscsi sx");

#ifdef DO_EVENTHANDLER
     if((isc->eh = EVENTHANDLER_REGISTER(shutdown_pre_sync, iscsi_shutdown,
					sc, SHUTDOWN_PRI_DEFAULT-1)) == NULL)
	  xdebug("shutdown event registration failed\n");
#endif
     /*
      | sysctl stuff
      */
     sysctl_ctx_init(&isc->clist);
     isc->oid = SYSCTL_ADD_NODE(&isc->clist,
			       SYSCTL_STATIC_CHILDREN(_net),
			       OID_AUTO,
			       "iscsi_initiator",
			       CTLFLAG_RD,
			       0,
			       "iSCSI Subsystem");

     SYSCTL_ADD_STRING(&isc->clist,
		       SYSCTL_CHILDREN(isc->oid),
		       OID_AUTO,
		       "driver_version",
		       CTLFLAG_RD,
		       iscsi_driver_version,
		       0,
		       "iscsi driver version");
 
     SYSCTL_ADD_STRING(&isc->clist,
		       SYSCTL_CHILDREN(isc->oid),
		       OID_AUTO,
		       "isid",
		       CTLFLAG_RW,
		       isid,
		       6+1,
		       "initiator part of the Session Identifier");

     SYSCTL_ADD_INT(&isc->clist,
		    SYSCTL_CHILDREN(isc->oid),
		    OID_AUTO,
		    "sessions",
		    CTLFLAG_RD,
		    &isc->nsess,
		    sizeof(isc->nsess),
		    "number of active session");

#ifdef ISCSI_INITIATOR_DEBUG
     mtx_init(&iscsi_dbg_mtx, "iscsi_dbg", NULL, MTX_DEF);
#endif

     isc->dev = make_dev_credf(MAKEDEV_CHECKNAME, &iscsi_cdevsw, max_sessions,
			       NULL, UID_ROOT, GID_WHEEL, 0600, "iscsi");
     if (isc->dev == NULL) {
	  xdebug("iscsi_initiator: make_dev_credf failed");
	  return (EEXIST);
     }
     isc->dev->si_drv1 = isc;

     printf("iscsi: version %s\n", iscsi_driver_version);
     return (0);
}

/*
 | Notes:
 |	unload SHOULD fail if there is activity
 |	activity: there is/are active session/s
 */
static void
iscsi_stop(void)
{
     isc_session_t	*sp, *sp_tmp;

     debug_called(8);

     /*
      | go through all the sessions
      | Note: close should have done this ...
      */
     TAILQ_FOREACH_SAFE(sp, &isc->isc_sess, sp_link, sp_tmp) {
	  //XXX: check for activity ...
	  ism_stop(sp);
     }
     mtx_destroy(&isc->isc_mtx);
     sx_destroy(&isc->unit_sx);

     free_pdus(isc);

     if(isc->dev)
	  destroy_dev(isc->dev);

     if(sysctl_ctx_free(&isc->clist))
	  xdebug("sysctl_ctx_free failed");

     iscsi_shutdown(isc); // XXX: check EVENTHANDLER_ ...

#ifdef ISCSI_INITIATOR_DEBUG
     mtx_destroy(&iscsi_dbg_mtx);
#endif

     free(isc, M_ISCSI);
}

static int
iscsi_modevent(module_t mod, int what, void *arg)
{
     int error = 0;

     debug_called(8);

     switch(what) {
     case MOD_LOAD:
	  error = iscsi_start();
	  break;

     case MOD_QUIESCE:
	  if(isc->nsess) {
	       xdebug("iscsi module busy(nsess=%d), cannot unload", isc->nsess);
	       log(LOG_ERR, "iscsi module busy, cannot unload");
	  }
	  return isc->nsess;

     case MOD_SHUTDOWN:
	  break;

     case MOD_UNLOAD:
	  iscsi_stop();
	  break;

     default:
	  break;
     }
     return (error);
}

moduledata_t iscsi_mod = {
         "iscsi_initiator",
         (modeventhand_t) iscsi_modevent,
         0
};

#ifdef ISCSI_ROOT
static void
iscsi_rootconf(void)
{
#if 0
	nfs_setup_diskless();
	if (nfs_diskless_valid)
		rootdevnames[0] = "nfs:";
#endif
	printf("** iscsi_rootconf **\n");
}

SYSINIT(cpu_rootconf1, SI_SUB_ROOT_CONF, SI_ORDER_FIRST, iscsi_rootconf, NULL)
#endif

DECLARE_MODULE(iscsi_initiator, iscsi_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_DEPEND(iscsi_initiator, cam, 1, 1, 1);
