/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2010 Daniel Braniss <danny@cs.huji.ac.il>
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
 | $Id: isc_soc.c 998 2009-12-20 10:32:45Z danny $
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_iscsi_initiator.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/conf.h>
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
#include <sys/syslog.h>
#include <sys/mbuf.h>
#include <sys/user.h>
#include <vm/uma.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>

#include <dev/iscsi_initiator/iscsi.h>
#include <dev/iscsi_initiator/iscsivar.h>

#ifndef NO_USE_MBUF
#define USE_MBUF
#endif

#ifdef USE_MBUF
static int ou_refcnt = 0;
/*
 | function for freeing external storage for mbuf
 */
static void
ext_free(struct mbuf *m)
{
     pduq_t *pq = m->m_ext.ext_arg1;

     if(pq->buf != NULL) {
	  debug(3, "ou_refcnt=%d a=%p b=%p",
	       ou_refcnt, m->m_ext.ext_buf, pq->buf);
	  free(pq->buf, M_ISCSIBUF);
	  pq->buf = NULL;
     }
}

int
isc_sendPDU(isc_session_t *sp, pduq_t *pq)
{
     struct mbuf *mh, **mp;
     pdu_t	*pp = &pq->pdu;
     int	len, error;

     debug_called(8);
     /* 
      | mbuf for the iSCSI header
      */
     MGETHDR(mh, M_WAITOK, MT_DATA);
     mh->m_pkthdr.rcvif = NULL;
     mh->m_next = NULL;
     mh->m_len = sizeof(union ipdu_u);

     if(ISOK2DIG(sp->hdrDigest, pp)) {
	  pp->hdr_dig = sp->hdrDigest(&pp->ipdu, sizeof(union ipdu_u), 0);
	  mh->m_len += sizeof(pp->hdr_dig);
	  if(pp->ahs_len) {
	       debug(2, "ahs_len=%d", pp->ahs_len);
	       pp->hdr_dig = sp->hdrDigest(&pp->ahs_addr, pp->ahs_len, pp->hdr_dig);
	  }
	  debug(3, "pp->hdr_dig=%04x", htonl(pp->hdr_dig));
     }
     if(pp->ahs_len) {
          /* 
	   | Add any AHS to the iSCSI hdr mbuf
	   */
	  if((mh->m_len + pp->ahs_len) < MHLEN) {
	       M_ALIGN(mh, mh->m_len + pp->ahs_len);
	       bcopy(&pp->ipdu, mh->m_data, mh->m_len);
	       bcopy(pp->ahs_addr, mh->m_data + mh->m_len, pp->ahs_len);
	       mh->m_len += pp->ahs_len;
	  }
	  else
	       panic("len AHS=%d too big, not impleneted yet", pp->ahs_len);
     }
     else {
	  M_ALIGN(mh, mh->m_len);
	  bcopy(&pp->ipdu, mh->m_data, mh->m_len);
     }
     mh->m_pkthdr.len = mh->m_len;
     mp = &mh->m_next;
     if(pp->ds_len && pq->pdu.ds_addr) {
          struct mbuf *md;
          int	off = 0;

          len = pp->ds_len;
          while(len > 0) {
	       int l;

	       MGET(md, M_WAITOK, MT_DATA);
	       md->m_ext.ext_cnt = &ou_refcnt;
	       l = min(MCLBYTES, len);
	       debug(4, "setting ext_free(arg=%p len/l=%d/%d)", pq->buf, len, l);
	       m_extadd(md, pp->ds_addr + off, l, ext_free, pq, NULL, 0,
		    EXT_EXTREF);
	       md->m_len = l;
	       md->m_next = NULL;
	       mh->m_pkthdr.len += l;
	       *mp = md;
	       mp = &md->m_next;
	       len -= l;
	       off += l;
          }
	  if(((pp->ds_len & 03) != 0) || ISOK2DIG(sp->dataDigest, pp)) {
	       MGET(md, M_WAITOK, MT_DATA);
	       if(pp->ds_len & 03)
		    len = 4 - (pp->ds_len & 03);
	       else
		    len = 0;
	       md->m_len = len;
	       if(ISOK2DIG(sp->dataDigest, pp))
		    md->m_len += sizeof(pp->ds_dig);
	       M_ALIGN(md, md->m_len);
	       if(ISOK2DIG(sp->dataDigest, pp)) {
		    pp->ds_dig = sp->dataDigest(pp->ds_addr, pp->ds_len, 0);
		    if(len) {
			 bzero(md->m_data, len); // RFC says SHOULD be 0
			 pp->ds_dig = sp->dataDigest(md->m_data, len, pp->ds_dig);
		    }
		    bcopy(&pp->ds_dig, md->m_data+len, sizeof(pp->ds_dig));
	       }
	       md->m_next = NULL;
	       mh->m_pkthdr.len += md->m_len;
	       *mp = md;
	  }
     }
     if((error = sosend(sp->soc, NULL, NULL, mh, 0, 0, sp->td)) != 0) {
	  sdebug(2, "error=%d", error);
	  return error;
     }
     sp->stats.nsent++;
     getbintime(&sp->stats.t_sent);
     return 0;
}
#else /* NO_USE_MBUF */
int
isc_sendPDU(isc_session_t *sp, pduq_t *pq)
{
     struct uio *uio = &pq->uio;
     struct iovec *iv;
     pdu_t	*pp = &pq->pdu;
     int	len, error;

     debug_called(8);

     bzero(uio, sizeof(struct uio));
     uio->uio_rw = UIO_WRITE;
     uio->uio_segflg = UIO_SYSSPACE;
     uio->uio_td = sp->td;
     uio->uio_iov = iv = pq->iov;

     iv->iov_base = &pp->ipdu;
     iv->iov_len = sizeof(union ipdu_u);
     uio->uio_resid = iv->iov_len;
     iv++;
     if(ISOK2DIG(sp->hdrDigest, pp))
	  pq->pdu.hdr_dig = sp->hdrDigest(&pp->ipdu, sizeof(union ipdu_u), 0);
     if(pp->ahs_len) {
	  iv->iov_base = pp->ahs_addr;
	  iv->iov_len = pp->ahs_len;
	  uio->uio_resid += iv->iov_len;
	  iv++;
	  if(ISOK2DIG(sp->hdrDigest, pp))
	       pp->hdr_dig = sp->hdrDigest(&pp->ahs_addr, pp->ahs_len, pp->hdr_dig);
     }
     if(ISOK2DIG(sp->hdrDigest, pp)) {
	  debug(3, "hdr_dig=%04x", htonl(pp->hdr_dig));
	  iv->iov_base = &pp->hdr_dig;
	  iv->iov_len = sizeof(int);
	  uio->uio_resid += iv->iov_len ;
	  iv++;
     }
     if(pq->pdu.ds_addr &&  pp->ds_len) {
	  iv->iov_base = pp->ds_addr;
	  iv->iov_len = pp->ds_len;
	  while(iv->iov_len & 03) // the specs say it must be int aligned
	       iv->iov_len++;
	  uio->uio_resid += iv->iov_len ;
	  iv++;
	  if(ISOK2DIG(sp->dataDigest, pp)) {
	       pp->ds_dig = sp->dataDigest(pp->ds, pp->ds_len, 0);
	       iv->iov_base = &pp->ds_dig;
	       iv->iov_len = sizeof(pp->ds_dig);
	       uio->uio_resid += iv->iov_len ;
	       iv++;
	  }
     }
     uio->uio_iovcnt = iv - pq->iov;
     sdebug(4, "pq->len=%d uio->uio_resid=%d  uio->uio_iovcnt=%d", pq->len,
	    uio->uio_resid,
	    uio->uio_iovcnt);

     sdebug(4, "opcode=%x iovcnt=%d uio_resid=%d itt=%x",
	    pp->ipdu.bhs.opcode, uio->uio_iovcnt, uio->uio_resid,
	    ntohl(pp->ipdu.bhs.itt));
     sdebug(5, "sp=%p sp->soc=%p uio=%p sp->td=%p",
	    sp, sp->soc, uio, sp->td);
     do {
	  len = uio->uio_resid;
	  error = sosend(sp->soc, NULL, uio, 0, 0, 0, sp->td);
	  if(uio->uio_resid == 0 || error || len == uio->uio_resid) {
	       if(uio->uio_resid) {
		    sdebug(2, "uio->uio_resid=%d uio->uio_iovcnt=%d error=%d len=%d",
			   uio->uio_resid, uio->uio_iovcnt, error, len);
		    if(error == 0)
			 error = EAGAIN; // 35
	       }
	       break;
	  }
	  /*
	   | XXX: untested code
	   */
	  sdebug(1, "uio->uio_resid=%d uio->uio_iovcnt=%d",
		 uio->uio_resid, uio->uio_iovcnt);
	  iv = uio->uio_iov;
	  len -= uio->uio_resid;
	  while(uio->uio_iovcnt > 0) {
	       if(iv->iov_len > len) {
		    caddr_t bp = (caddr_t)iv->iov_base;

		    iv->iov_len -= len;
		    iv->iov_base = (void *)&bp[len];
		    break;
	       }
	       len -= iv->iov_len;
	       uio->uio_iovcnt--;
	       uio->uio_iov++;
	       iv++;
	  }
     } while(uio->uio_resid);

     if(error == 0) {
	  sp->stats.nsent++;
	  getbintime(&sp->stats.t_sent);
     }

     return error;
}
#endif /* USE_MBUF */

/*
 | wait till a PDU header is received
 | from the socket.
 */
/*
   The format of the BHS is:

   Byte/     0       |       1       |       2       |       3       |
      /              |               |               |               |
     |0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|
     +---------------+---------------+---------------+---------------+
    0|.|I| Opcode    |F|  Opcode-specific fields                     |
     +---------------+---------------+---------------+---------------+
    4|TotalAHSLength | DataSegmentLength                             |
     +---------------+---------------+---------------+---------------+
    8| LUN or Opcode-specific fields                                 |
     +                                                               +
   12|                                                               |
     +---------------+---------------+---------------+---------------+
   16| Initiator Task Tag                                            |
     +---------------+---------------+---------------+---------------+
   20/ Opcode-specific fields                                        /
    +/                                                               /
     +---------------+---------------+---------------+---------------+
   48
 */
static __inline int
so_getbhs(isc_session_t *sp)
{
     bhs_t *bhs		= &sp->bhs;
     struct uio		*uio = &sp->uio;
     struct iovec	*iov = &sp->iov;
     int		error, flags;

     debug_called(8);

     iov->iov_base	= bhs;
     iov->iov_len	= sizeof(bhs_t);

     uio->uio_iov	= iov;
     uio->uio_iovcnt	= 1;
     uio->uio_rw	= UIO_READ;
     uio->uio_segflg	= UIO_SYSSPACE;
     uio->uio_td	= curthread; // why ...
     uio->uio_resid	= sizeof(bhs_t);

     flags = MSG_WAITALL;
     error = soreceive(sp->soc, NULL, uio, 0, 0, &flags);

     if(error)
	  debug(2, 
#if __FreeBSD_version > 800000
		"error=%d so_error=%d uio->uio_resid=%zd iov.iov_len=%zd",
#else
		"error=%d so_error=%d uio->uio_resid=%d iov.iov_len=%zd",
#endif
		error,
		sp->soc->so_error, uio->uio_resid, iov->iov_len);
     if(!error && (uio->uio_resid > 0)) {
	  error = EPIPE; // was EAGAIN
	  debug(2,
#if __FreeBSD_version > 800000
		"error=%d so_error=%d uio->uio_resid=%zd iov.iov_len=%zd so_state=%x",
#else
		"error=%d so_error=%d uio->uio_resid=%d iov.iov_len=%zd so_state=%x",
#endif
		error,
		sp->soc->so_error, uio->uio_resid, iov->iov_len, sp->soc->so_state);
     }
     return error;
}

/*
 | so_recv gets called when 
 | an iSCSI header has been received.
 | Note: the designers had no intentions 
 |       in making programmer's life easy.
 */
static int
so_recv(isc_session_t *sp, pduq_t *pq)
{
     sn_t		*sn = &sp->sn;
     struct uio		*uio = &pq->uio;
     pdu_t		*pp = &pq->pdu;
     bhs_t		*bhs = &pp->ipdu.bhs;
     struct iovec	*iov = pq->iov;
     int		error;
     u_int		len;
     u_int		max, exp;
     int		flags = MSG_WAITALL;

     debug_called(8);
     /*
      | now calculate how much data should be in the buffer
      */
     uio->uio_iov	= iov;
     uio->uio_iovcnt	= 0;
     len = 0;
     if(bhs->AHSLength) {
	  debug(2, "bhs->AHSLength=%d", bhs->AHSLength);
	  pp->ahs_len = bhs->AHSLength * 4;
	  len += pp->ahs_len;
	  pp->ahs_addr = malloc(pp->ahs_len, M_TEMP, M_WAITOK); // XXX: could get stuck here
	  iov->iov_base = pp->ahs_addr;
	  iov->iov_len = pp->ahs_len;
	  uio->uio_iovcnt++;
	  iov++;
     }
     if(ISOK2DIG(sp->hdrDigest, pp)) {
	  len += sizeof(pp->hdr_dig);
	  iov->iov_base = &pp->hdr_dig;
	  iov->iov_len = sizeof(pp->hdr_dig);
	  uio->uio_iovcnt++;
     }
     if(len) {
	  uio->uio_rw		= UIO_READ;
	  uio->uio_segflg	= UIO_SYSSPACE;
	  uio->uio_resid	= len;
	  uio->uio_td		= sp->td; // why ...
	  error = soreceive(sp->soc, NULL, uio, NULL, NULL, &flags);
	  //if(error == EAGAIN)
	  // XXX: this needs work! it hangs iscontrol
	  if(error || uio->uio_resid) {
	       debug(2, 
#if __FreeBSD_version > 800000
		     "len=%d error=%d uio->uio_resid=%zd",
#else
		     "len=%d error=%d uio->uio_resid=%d",
#endif
		     len, error, uio->uio_resid);
	       goto out;
	  }
	  if(ISOK2DIG(sp->hdrDigest, pp)) {
	       bhs_t	*bhs;
	       u_int	digest;
	       
	       bhs = (bhs_t *)&pp->ipdu;
	       digest = sp->hdrDigest(bhs, sizeof(bhs_t), 0);
	       if(pp->ahs_len)
		    digest = sp->hdrDigest(pp->ahs_addr, pp->ahs_len, digest);
	       if(pp->hdr_dig != digest) {
		    debug(2, "bad header digest: received=%x calculated=%x", pp->hdr_dig, digest);
		    // XXX: now what?
		    error = EIO;
		    goto out;
	       }
	  }
	  if(pp->ahs_len) {
	       debug(2, "ahs len=%x type=%x spec=%x",
		     pp->ahs_addr->len, pp->ahs_addr->type, pp->ahs_addr->spec);
	       // XXX: till I figure out what to do with this
	       free(pp->ahs_addr, M_TEMP);
	  }
	  pq->len += len; // XXX: who needs this?
	  bzero(uio, sizeof(struct uio));
	  len = 0;
     }

     if(bhs->DSLength) {
	  len = bhs->DSLength;
#if BYTE_ORDER == LITTLE_ENDIAN
	  len = ((len & 0x00ff0000) >> 16)
	       | (len & 0x0000ff00)
	       | ((len & 0x000000ff) << 16);
#endif
	  pp->ds_len = len;
	  if((sp->opt.maxRecvDataSegmentLength > 0) && (len > sp->opt.maxRecvDataSegmentLength)) {
	       xdebug("impossible PDU length(%d) opt.maxRecvDataSegmentLength=%d",
		      len, sp->opt.maxRecvDataSegmentLength);
	       log(LOG_ERR,
		   "so_recv: impossible PDU length(%d) from iSCSI %s/%s\n",
		   len, sp->opt.targetAddress, sp->opt.targetName);
	       /*
		| XXX: this will really screwup the stream.
		| should clear up the buffer till a valid header
		| is found, or just close connection ...
		| should read the RFC.
	        */
	       error = E2BIG;
	       goto out;
	  }
	  while(len & 03)
	       len++;
	  if(ISOK2DIG(sp->dataDigest, pp))
	       len += 4;
	  uio->uio_resid = len;
	  uio->uio_td = sp->td; // why ...
	  pq->len += len; // XXX: do we need this?
	  error = soreceive(sp->soc, NULL, uio, &pq->mp, NULL, &flags);
	  //if(error == EAGAIN)
	  // XXX: this needs work! it hangs iscontrol
	  if(error || uio->uio_resid)
	       goto out;
          if(ISOK2DIG(sp->dataDigest, pp)) {
	       struct mbuf *m;
	       u_int    digest, ds_len, cnt;

	       // get the received digest
	       m_copydata(pq->mp,
			  len - sizeof(pp->ds_dig),
			  sizeof(pp->ds_dig),
			  (caddr_t)&pp->ds_dig);
	       // calculate all mbufs 
	       digest = 0;
	       ds_len = len - sizeof(pp->ds_dig);
	       for(m = pq->mp; m != NULL; m = m->m_next) {
		    cnt = MIN(ds_len, m->m_len);
		    digest = sp->dataDigest(mtod(m, char *), cnt, digest);
		    ds_len -= cnt;
		    if(ds_len == 0)
			 break;
	       }
	       if(digest != pp->ds_dig) {
		    sdebug(1, "bad data digest: received=%x calculated=%x", pp->ds_dig, digest);
		    error = EIO; // XXX: find a better error
		    goto out;
	       }
	       KASSERT(ds_len == 0, ("ds_len not zero"));
	  }
     }
     sdebug(6, "len=%d] opcode=0x%x ahs_len=0x%x ds_len=0x%x",
	    pq->len, bhs->opcode, pp->ahs_len, pp->ds_len);

     max = ntohl(bhs->MaxCmdSN);
     exp = ntohl(bhs->ExpStSN);
     if(max < exp - 1 &&
	max > exp - _MAXINCR) {
	  sdebug(2,  "bad cmd window size");
	  error = EIO; // XXX: for now;
	  goto out; // error
     }
     if(SNA_GT(max, sn->maxCmd))
	  sn->maxCmd = max;
     if(SNA_GT(exp, sn->expCmd))
	  sn->expCmd = exp;
     /*
      | remove from the holding queue packets
      | that have been acked and don't need
      | further processing.
      */
     i_acked_hld(sp, NULL);

     sp->cws = sn->maxCmd - sn->expCmd + 1;

     return 0;

 out:
     // XXX: need some work here
     if(pp->ahs_len) {
	  // XXX: till I figure out what to do with this
	  free(pp->ahs_addr, M_TEMP);
     }
     xdebug("have a problem, error=%d", error);
     pdu_free(sp->isc, pq);
     if(!error && uio->uio_resid > 0)
	  error = EPIPE;
     return error;
}

/*
 | wait for something to arrive.
 | and if the pdu is without errors, process it.
 */
static int
so_input(isc_session_t *sp)
{
     pduq_t		*pq;
     int		error;

     debug_called(8);
     /*
      | first read in the iSCSI header
      */
     error = so_getbhs(sp);
     if(error == 0) {
	  /*
	   | now read the rest.
	   */
	  pq = pdu_alloc(sp->isc, M_NOWAIT); 
	  if(pq == NULL) { // XXX: might cause a deadlock ...
	       debug(2, "out of pdus, wait");
	       pq = pdu_alloc(sp->isc, M_WAITOK);  // OK to WAIT
	  }
	  pq->pdu.ipdu.bhs = sp->bhs;
	  pq->len = sizeof(bhs_t);	// so far only the header was read
	  error = so_recv(sp, pq);
	  if(error != 0) {
	       error += 0x800; // XXX: just to see the error.
	       // terminal error
	       // XXX: close connection and exit
	  }
	  else {
	       sp->stats.nrecv++;
	       getbintime(&sp->stats.t_recv);
	       ism_recv(sp, pq);
	  }
     }
     return error;
}

/*
 | one per active (connected) session.
 | this thread is responsible for reading
 | in packets from the target.
 */
static void
isc_in(void *vp)
{
     isc_session_t	*sp = (isc_session_t *)vp;
     struct socket	*so = sp->soc;
     int		error;

     debug_called(8);

     sp->flags |= ISC_CON_RUNNING;
     error = 0;
     while((sp->flags & (ISC_CON_RUN | ISC_LINK_UP)) == (ISC_CON_RUN | ISC_LINK_UP)) {
	  // XXX: hunting ...
	  if(sp->soc == NULL || !(so->so_state & SS_ISCONNECTED)) {
	       debug(2, "sp->soc=%p", sp->soc);
	       break;
	  }
	  error = so_input(sp);
	  if(error == 0) {
	       mtx_lock(&sp->io_mtx);
	       if(sp->flags & ISC_OWAITING) {
		    wakeup(&sp->flags);
	       }
	       mtx_unlock(&sp->io_mtx);
	  } else if(error == EPIPE) {
	       break;
	  }
	  else if(error == EAGAIN) {
	       if(so->so_state & SS_ISCONNECTED) 
		    // there seems to be a problem in 6.0 ...
		    tsleep(sp, PRIBIO, "isc_soc", 2*hz);
	  }
     }
     sdebug(2, "terminated, flags=%x so_count=%d so_state=%x error=%d proc=%p",
	    sp->flags, so->so_count, so->so_state, error, sp->proc);
     if((sp->proc != NULL) && sp->signal) {
	  PROC_LOCK(sp->proc);
	  kern_psignal(sp->proc, sp->signal);
	  PROC_UNLOCK(sp->proc);
	  sp->flags |= ISC_SIGNALED;
	  sdebug(2, "pid=%d signaled(%d)", sp->proc->p_pid, sp->signal);
     }
     else {
	  // we have to do something ourselves
	  // like closing this session ...
     }
     /*
      | we've been terminated
      */
     // do we need this mutex ...?
     mtx_lock(&sp->io_mtx);
     sp->flags &= ~(ISC_CON_RUNNING | ISC_LINK_UP);
     wakeup(&sp->soc);
     mtx_unlock(&sp->io_mtx);

     sdebug(2, "dropped ISC_CON_RUNNING");
#if __FreeBSD_version >= 800000
     kproc_exit(0);
#else
     kthread_exit(0);
#endif
}

void
isc_stop_receiver(isc_session_t *sp)
{
     int	n;

     debug_called(8);
     sdebug(3, "sp=%p sp->soc=%p", sp, sp? sp->soc: 0);
     mtx_lock(&sp->io_mtx);
     sp->flags &= ~ISC_LINK_UP;
     msleep(&sp->soc, &sp->io_mtx, PRIBIO|PDROP, "isc_stpc", 5*hz);

     soshutdown(sp->soc, SHUT_RD);

     mtx_lock(&sp->io_mtx);
     sdebug(3, "soshutdown");
     sp->flags &= ~ISC_CON_RUN;
     n = 2;
     while(n-- && (sp->flags & ISC_CON_RUNNING)) {
	  sdebug(3, "waiting n=%d... flags=%x", n, sp->flags);
	  msleep(&sp->soc, &sp->io_mtx, PRIBIO, "isc_stpc", 5*hz);
     }
     mtx_unlock(&sp->io_mtx);

     if(sp->fp != NULL)
	  fdrop(sp->fp, sp->td);
     sp->soc = NULL;
     sp->fp = NULL;

     sdebug(3, "done");
}

void
isc_start_receiver(isc_session_t *sp)
{
     debug_called(8);

     sp->flags |= ISC_CON_RUN | ISC_LINK_UP;
#if __FreeBSD_version >= 800000
     kproc_create
#else
     kthread_create
#endif
	  (isc_in, sp, &sp->soc_proc, 0, 0, "isc_in %d", sp->sid);
}
