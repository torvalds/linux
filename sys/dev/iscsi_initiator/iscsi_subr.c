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
 | $Id: iscsi_subr.c 743 2009-08-08 10:54:53Z danny $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_iscsi_initiator.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/callout.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/kthread.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/uio.h>
#include <sys/sysctl.h>
#include <sys/sx.h>
#include <vm/uma.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt_sim.h>
#include <cam/cam_periph.h>
#include <cam/scsi/scsi_message.h>
#include <sys/eventhandler.h>

#include <dev/iscsi_initiator/iscsi.h>
#include <dev/iscsi_initiator/iscsivar.h>

/*
 | Interface to the SCSI layer
 */
void
iscsi_r2t(isc_session_t *sp, pduq_t *opq, pduq_t *pq)
{
     union ccb 		*ccb = opq->ccb;
     struct ccb_scsiio	*csio = &ccb->csio;
     pdu_t		*opp = &opq->pdu;
     bhs_t		*bhp = &opp->ipdu.bhs;
     r2t_t		*r2t = &pq->pdu.ipdu.r2t;
     pduq_t	*wpq;
     int	error;

     debug_called(8);
     sdebug(4, "itt=%x r2tSN=%d bo=%x ddtl=%x W=%d", ntohl(r2t->itt),
	   ntohl(r2t->r2tSN), ntohl(r2t->bo), ntohl(r2t->ddtl), opp->ipdu.scsi_req.W);

     switch(bhp->opcode) {
     case ISCSI_SCSI_CMD:
	  if(opp->ipdu.scsi_req.W) {
	       data_out_t	*cmd;
	       u_int		ddtl = ntohl(r2t->ddtl);
	       u_int		edtl = ntohl(opp->ipdu.scsi_req.edtlen);
	       u_int		bleft, bs, dsn, bo;
	       caddr_t		bp = csio->data_ptr;

	       bo = ntohl(r2t->bo);
	       bp += MIN(bo, edtl - ddtl);
	       bleft = ddtl;

	       if(sp->opt.maxXmitDataSegmentLength > 0) // danny's RFC
		    bs = MIN(sp->opt.maxXmitDataSegmentLength, ddtl);
	       else
		    bs = ddtl;
	       dsn = 0;
	       sdebug(4, "edtl=%x ddtl=%x bo=%x dsn=%x bs=%x maxX=%x",
		      edtl, ddtl, bo, dsn, bs, sp->opt.maxXmitDataSegmentLength);
	       while(bleft > 0) {
		    wpq = pdu_alloc(sp->isc, M_NOWAIT); // testing ...
		    if(wpq == NULL) {
			 sdebug(3, "itt=%x r2tSN=%d bo=%x ddtl=%x W=%d", ntohl(r2t->itt),
				ntohl(r2t->r2tSN), ntohl(r2t->bo), ntohl(r2t->ddtl), opp->ipdu.scsi_req.W);
			 sdebug(1, "npdu_max=%d npdu_alloc=%d", sp->isc->npdu_max, sp->isc->npdu_alloc);

			 while((wpq = pdu_alloc(sp->isc, M_NOWAIT)) == NULL) {
			      sdebug(2, "waiting...");
#if __FreeBSD_version >= 700000
			      pause("isc_r2t", 5*hz);
#else
			      tsleep(sp->isc, 0, "isc_r2t", 5*hz);
#endif
			 }
		    }
		    cmd = &wpq->pdu.ipdu.data_out;
		    cmd->opcode = ISCSI_WRITE_DATA;
		    cmd->lun[0]	= r2t->lun[0];
		    cmd->lun[1]	= r2t->lun[1];
		    cmd->ttt	= r2t->ttt;
		    cmd->itt	= r2t->itt;

		    cmd->dsn	= htonl(dsn);
		    cmd->bo	= htonl(bo);

		    cmd->F 	= (bs < bleft)? 0: 1; // is this the last one?
		    bs = MIN(bs, bleft);
		    
		    wpq->pdu.ds_len	= bs;
		    wpq->pdu.ds_addr	= bp;
		    
		    error = isc_qout(sp, wpq);
		    sdebug(6, "bs=%x bo=%x bp=%p dsn=%x error=%d", bs, bo, bp, dsn, error);
		    if(error)
			 break;
		    bo += bs;
		    bp += bs;
		    bleft -= bs;
		    dsn++;
	       }
	  }
	  break;

     default:
	  // XXX: should not happen ...
	  xdebug("huh? opcode=0x%x", bhp->opcode);
     }
}

static int
getSenseData(u_int status, union ccb *ccb, pduq_t *pq)
{
     pdu_t		*pp = &pq->pdu;
     struct		ccb_scsiio *scsi = (struct ccb_scsiio *)ccb;
     struct		scsi_sense_data *sense = &scsi->sense_data;
     struct mbuf	*m = pq->mp;
     scsi_rsp_t		*cmd = &pp->ipdu.scsi_rsp;
     caddr_t		bp;
     int		sense_len, mustfree = 0;
     int                error_code, sense_key, asc, ascq;

     bp = mtod(pq->mp, caddr_t);
     if((sense_len = scsi_2btoul(bp)) == 0)
	  return 0;
     debug(4, "sense_len=%d", sense_len);
     /*
      | according to the specs, the sense data cannot
      | be larger than 252 ...
      */
     if(sense_len > m->m_len) {
	  bp = malloc(sense_len, M_ISCSI, M_WAITOK);
	  debug(3, "calling i_mbufcopy(len=%d)", sense_len);
	  i_mbufcopy(pq->mp, bp, sense_len);
	  mustfree++;
     }
     scsi->scsi_status = status;

     bcopy(bp+2, sense, min(sense_len, scsi->sense_len));
     scsi->sense_resid = 0;
     if(cmd->flag & (BIT(1)|BIT(2)))
	  scsi->sense_resid = ntohl(pp->ipdu.scsi_rsp.rcnt);
     scsi_extract_sense_len(sense, scsi->sense_len - scsi->sense_resid,
       &error_code, &sense_key, &asc, &ascq, /*show_errors*/ 1);

     debug(3, "sense_len=%d rcnt=%d sense_resid=%d dsl=%d error_code=%x flags=%x",
	   sense_len,
	   ntohl(pp->ipdu.scsi_rsp.rcnt), scsi->sense_resid,
	   pp->ds_len, error_code, sense_key);

     if(mustfree)
	  free(bp, M_ISCSI);

     return 1;
}

/*
 | Some information is from SAM draft.
 */
static void
_scsi_done(isc_session_t *sp, u_int response, u_int status, union ccb *ccb, pduq_t *pq)
{
     struct ccb_hdr	*ccb_h = &ccb->ccb_h;

     debug_called(8);

     if(status || response) {
	  sdebug(3, "response=%x status=%x ccb=%p pq=%p", response, status, ccb, pq);
	  if(pq != NULL)
	       sdebug(3, "mp=%p buf=%p len=%d", pq->mp, pq->buf, pq->len);
     }
     ccb_h->status = 0;
     switch(response) {
     case 0: // Command Completed at Target
	  switch(status) {
	  case 0:	// Good, all is ok
	       ccb_h->status = CAM_REQ_CMP;
	       break;
	       
	  case 0x02: 	// Check Condition
	       if((pq != NULL) && (pq->mp != NULL) && getSenseData(status, ccb, pq))
		    ccb_h->status |= CAM_AUTOSNS_VALID;

	  case 0x14:	// Intermediate-Condition Met
	  case 0x10:	// Intermediate
	  case 0x04:	// Condition Met
	       ccb_h->status |= CAM_SCSI_STATUS_ERROR;
	       break;

	  case 0x08:
	       ccb_h->status = CAM_BUSY;
	       break;

	  case 0x18: // Reservation Conflict
	  case 0x28: // Task Set Full
	       ccb_h->status = CAM_REQUEUE_REQ;
	       break;
	  default:
	       //case 0x22: // Command Terminated
	       //case 0x30: // ACA Active
	       //case 0x40: // Task Aborted
	       ccb_h->status = CAM_REQ_CMP_ERR; //CAM_REQ_ABORTED;
	  }
	  break;

     default:
	  if((response >= 0x80) && (response <= 0xFF)) {
	       // Vendor specific ...
	  }
     case 1: // target failure
	  ccb_h->status = CAM_REQ_CMP_ERR; //CAM_REQ_ABORTED;
	  break;
     }
     sdebug(5, "ccb_h->status=%x", ccb_h->status);

     XPT_DONE(sp, ccb);
}

/*
 | returns the lowest cmdseq that was not acked
 */
int
iscsi_requeue(isc_session_t *sp)
{
     pduq_t	*pq;
     u_int	i, n, last;

     debug_called(8);
     i = last = 0;
     sp->flags |= ISC_HOLD;
     while((pq = i_dqueue_hld(sp)) != NULL) {
	  i++;
	  if(pq->ccb != NULL) {
	       _scsi_done(sp, 0, 0x28, pq->ccb, NULL);
	       n = ntohl(pq->pdu.ipdu.bhs.CmdSN);
	       if(last==0 || (last > n))
		    last = n;
	       sdebug(2, "last=%x n=%x", last, n);
	  }
	  pdu_free(sp->isc, pq);
     }
     sp->flags &= ~ISC_HOLD;
     return i? last: sp->sn.cmd;
}

int
i_pdu_flush(isc_session_t *sp)
{
     int	n = 0;
     pduq_t	*pq;

     debug_called(8);
     while((pq = i_dqueue_rsp(sp)) != NULL) {
	  pdu_free(sp->isc, pq);
	  n++;
     }
     while((pq = i_dqueue_rsv(sp)) != NULL) {
	  pdu_free(sp->isc, pq);
	  n++;
     }
     while((pq = i_dqueue_snd(sp, -1)) != NULL) {
	  pdu_free(sp->isc, pq);
	  n++;
     }
     while((pq = i_dqueue_hld(sp)) != NULL) {
	  pdu_free(sp->isc, pq);
	  n++;
     }
     while((pq = i_dqueue_wsnd(sp)) != NULL) {
	  pdu_free(sp->isc, pq);
	  n++;
     }
     if(n != 0)
	  xdebug("%d pdus recovered, should have been ZERO!", n);
     return n;
}
/*
 | called from ism_destroy.
 */
void
iscsi_cleanup(isc_session_t *sp)
{
     pduq_t *pq, *pqtmp;

     debug_called(8);

     TAILQ_FOREACH_SAFE(pq, &sp->hld, pq_link, pqtmp) {
	  sdebug(3, "hld pq=%p", pq);
	  if(pq->ccb)
	       _scsi_done(sp, 1, 0x40, pq->ccb, NULL);
	  TAILQ_REMOVE(&sp->hld, pq, pq_link);
	  if(pq->buf) {
	       free(pq->buf, M_ISCSIBUF);
	       pq->buf = NULL;
	  }
	  pdu_free(sp->isc, pq);
     }
     while((pq = i_dqueue_snd(sp, BIT(0)|BIT(1)|BIT(2))) != NULL) {
	  sdebug(3, "pq=%p", pq);
	  if(pq->ccb)
	       _scsi_done(sp, 1, 0x40, pq->ccb, NULL);
	  if(pq->buf) {
	       free(pq->buf, M_ISCSIBUF);
	       pq->buf = NULL;
	  }
	  pdu_free(sp->isc, pq);
     }

     wakeup(&sp->rsp);
}

void
iscsi_done(isc_session_t *sp, pduq_t *opq, pduq_t *pq)
{
     pdu_t		*pp = &pq->pdu;
     scsi_rsp_t		*cmd = &pp->ipdu.scsi_rsp;

     debug_called(8);

     _scsi_done(sp, cmd->response, cmd->status, opq->ccb, pq);

     pdu_free(sp->isc, opq);
}

// see RFC 3720, 10.9.1 page 146
/*
 | NOTE:
 | the call to isc_stop_receiver is a kludge,
 | instead, it should be handled by the userland controller,
 | but that means that there should be a better way, other than
 | sending a signal. Somehow, this packet should be supplied to
 | the userland via read.
 */
void
iscsi_async(isc_session_t *sp, pduq_t *pq)
{
     pdu_t		*pp = &pq->pdu;
     async_t		*cmd = &pp->ipdu.async;

     debug_called(8);

     sdebug(3, "asyncevent=0x%x asyncVCode=0x%0x", cmd->asyncEvent, cmd->asyncVCode);
     switch(cmd->asyncEvent) {
     case 0: // check status ...
	  break;

     case 1: // target request logout
	  isc_stop_receiver(sp);	// XXX: temporary solution
	  break;

     case 2: // target indicates it wants to drop connection
	  isc_stop_receiver(sp);	// XXX: temporary solution
	  break;

     case 3: // target indicates it will drop all connections.
	  isc_stop_receiver(sp);	// XXX: temporary solution
	  break;

     case 4: // target request parameter negotiation
	  break;

     default:
	  break;
     }
}

void
iscsi_reject(isc_session_t *sp, pduq_t *opq, pduq_t *pq)
{
     union ccb 		*ccb = opq->ccb;
     //reject_t		*reject = &pq->pdu.ipdu.reject;

     debug_called(8);
     //XXX: check RFC 10.17.1 (page 176)
     ccb->ccb_h.status = CAM_REQ_ABORTED;
     XPT_DONE(sp, ccb);
 
     pdu_free(sp->isc, opq);
}

/*
 | deal with lun
 */
static int
dwl(isc_session_t *sp, int lun, u_char *lp)
{
     debug_called(8);
     sdebug(4, "lun=%d", lun);
     /*
      | mapping LUN to iSCSI LUN
      | check the SAM-2 specs
      | hint: maxLUNS is a small number, cam's LUN is 32bits
      | iSCSI is 64bits, scsi is ?
      */
     // XXX: check if this will pass the endian test
     if(lun < 256) {
	  lp[0] = 0;
	  lp[1] = lun;
     } else
     if(lun < 16384) {
	  lp[0] = (1 << 5) | ((lun >> 8) & 0x3f);
	  lp[1] = lun & 0xff;
     } 
     else {
	  xdebug("lun %d: is unsupported!", lun);
	  return -1;
     }

     return 0;
}

/*
 | encapsulate the scsi command and 
 */
int
scsi_encap(struct cam_sim *sim, union ccb *ccb)
{
     isc_session_t	*sp = cam_sim_softc(sim);
     struct ccb_scsiio	*csio = &ccb->csio;
     struct ccb_hdr	*ccb_h = &ccb->ccb_h;
     pduq_t		*pq;
     scsi_req_t		*cmd;

     debug_called(8);

     debug(4, "ccb->sp=%p", ccb_h->spriv_ptr0);
     sp = ccb_h->spriv_ptr0;

     if((pq = pdu_alloc(sp->isc, M_NOWAIT)) == NULL) {
	  debug(2, "ccb->sp=%p", ccb_h->spriv_ptr0);
	  sdebug(1, "pdu_alloc failed sc->npdu_max=%d npdu_alloc=%d",
		 sp->isc->npdu_max, sp->isc->npdu_alloc);
	  while((pq = pdu_alloc(sp->isc, M_NOWAIT)) == NULL) {
	       sdebug(2, "waiting...");
#if __FreeBSD_version >= 700000
	       pause("isc_encap", 5*hz);
#else
	       tsleep(sp->isc, 0, "isc_encap", 5*hz);
#endif
	  }
     }
     cmd = &pq->pdu.ipdu.scsi_req;
     cmd->opcode = ISCSI_SCSI_CMD;
     cmd->F = 1;
#if 0
// this breaks at least Isilon's iscsi target.
     /*
      | map tag option, default is UNTAGGED
      */
     switch(csio->tag_action) {
     case MSG_SIMPLE_Q_TAG:	cmd->attr = iSCSI_TASK_SIMPLE;	break;
     case MSG_HEAD_OF_Q_TAG:	cmd->attr = iSCSI_TASK_HOFQ;	break;
     case MSG_ORDERED_Q_TAG:	cmd->attr = iSCSI_TASK_ORDER;	break;
     case MSG_ACA_TASK:		cmd->attr = iSCSI_TASK_ACA;	break;
     }
#else
     cmd->attr = iSCSI_TASK_SIMPLE;
#endif

     dwl(sp, ccb_h->target_lun, (u_char *)&cmd->lun);

     if((ccb_h->flags & CAM_CDB_POINTER) != 0) {
	  if((ccb_h->flags & CAM_CDB_PHYS) == 0) {
	       if(csio->cdb_len > 16) {
		    sdebug(3, "oversize cdb %d > 16", csio->cdb_len);
		    goto invalid;
	       }
	  }
	  else {
	       sdebug(3, "not phys");
	       goto invalid;
	  }
     }

     if(csio->cdb_len > sizeof(cmd->cdb))
	  xdebug("guevalt! %d > %ld", csio->cdb_len, (long)sizeof(cmd->cdb));

     memcpy(cmd->cdb,
	    ccb_h->flags & CAM_CDB_POINTER? csio->cdb_io.cdb_ptr: csio->cdb_io.cdb_bytes,
	    csio->cdb_len);

     cmd->W = (ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_OUT;
     cmd->R = (ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_IN;
     cmd->edtlen = htonl(csio->dxfer_len);

     pq->ccb = ccb;
     /*
      | place it in the out queue
      */
     if(isc_qout(sp, pq) == 0)
	  return 1; 
 invalid:
     ccb->ccb_h.status = CAM_REQ_INVALID;
     pdu_free(sp->isc, pq);

     return 0;
}

int
scsi_decap(isc_session_t *sp, pduq_t *opq, pduq_t *pq)
{
     union ccb 		*ccb = opq->ccb;
     struct ccb_scsiio	*csio = &ccb->csio;
     pdu_t		*opp = &opq->pdu;
     bhs_t		*bhp = &opp->ipdu.bhs;
     
     debug_called(8);
     sdebug(6, "pq=%p opq=%p bhp->opcode=0x%x len=%d",
	    pq, opq, bhp->opcode, pq->pdu.ds_len);
     if(ccb == NULL) {
	  sdebug(1, "itt=0x%x pq=%p opq=%p bhp->opcode=0x%x len=%d",
		 ntohl(pq->pdu.ipdu.bhs.itt),
		 pq, opq, bhp->opcode, pq->pdu.ds_len);
	  xdebug("%d] ccb == NULL!", sp->sid);
	  return 0;
     }
     if(pq->pdu.ds_len != 0) {
	  switch(bhp->opcode) {
	  case ISCSI_SCSI_CMD: {
	       scsi_req_t *cmd = &opp->ipdu.scsi_req;
	       sdebug(5, "itt=0x%x opcode=%x R=%d",
		      ntohl(pq->pdu.ipdu.bhs.itt),
		      pq->pdu.ipdu.bhs.opcode, cmd->R);

	       switch(pq->pdu.ipdu.bhs.opcode) {
	       case ISCSI_READ_DATA: // SCSI Data in
	       {
		    caddr_t	bp = NULL; // = mtod(pq->mp, caddr_t);
		    data_in_t 	*rcmd = &pq->pdu.ipdu.data_in;

		    if(cmd->R) {
			 sdebug(5, "copy to=%p from=%p l1=%d l2=%d mp@%p",
				csio->data_ptr, bp? mtod(pq->mp, caddr_t): 0,
				ntohl(cmd->edtlen), pq->pdu.ds_len, pq->mp);
			 if(ntohl(cmd->edtlen) >= pq->pdu.ds_len) {
			      int	offset, len = pq->pdu.ds_len;

			      if(pq->mp != NULL) {
				   caddr_t		dp;

				   offset = ntohl(rcmd->bo);
				   dp = csio->data_ptr + offset;
				   i_mbufcopy(pq->mp, dp, len);
			      }
			 }
			 else {
			      xdebug("edtlen=%d < ds_len=%d",
				     ntohl(cmd->edtlen), pq->pdu.ds_len);
			 }
		    }
		    if(rcmd->S) {
			 /*
			  | contains also the SCSI Status
			  */
			 _scsi_done(sp, 0, rcmd->status, opq->ccb, NULL);
			 return 0;
		    } else
			 return 1;
	       }
	       break;
	       }
	  }
	  default:
	       sdebug(3, "opcode=%02x", bhp->opcode);
	       break;
	  }
     }
     /*
      | XXX: error ...
      */
     return 1;
}
