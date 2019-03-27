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
 | $Id: isc_cam.c 998 2009-12-20 10:32:45Z danny $
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_iscsi_initiator.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/callout.h>
#if __FreeBSD_version >= 700000
#include <sys/lock.h>
#include <sys/mutex.h>
#endif
#include <sys/conf.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/uio.h>
#include <sys/sysctl.h>
#include <sys/sx.h>
#include <vm/uma.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt_sim.h>
#include <cam/cam_periph.h>

#include <dev/iscsi_initiator/iscsi.h>
#include <dev/iscsi_initiator/iscsivar.h>

static void
_inq(struct cam_sim *sim, union ccb *ccb)
{
     struct ccb_pathinq *cpi = &ccb->cpi;
     isc_session_t *sp = cam_sim_softc(sim);

     debug_called(8);
     debug(3, "sid=%d target=%d lun=%jx", sp->sid, ccb->ccb_h.target_id, (uintmax_t)ccb->ccb_h.target_lun);

     cpi->version_num = 1; /* XXX??? */
     cpi->hba_inquiry = PI_SDTR_ABLE | PI_TAG_ABLE | PI_WIDE_32;
     cpi->target_sprt = 0;
     cpi->hba_misc = 0;
     cpi->hba_eng_cnt = 0;
     cpi->max_target = 0; //ISCSI_MAX_TARGETS - 1;
     cpi->initiator_id = ISCSI_MAX_TARGETS;
     cpi->max_lun = sp->opt.maxluns - 1;
     cpi->bus_id = cam_sim_bus(sim);
     cpi->base_transfer_speed = 3300; // 40000; // XXX:
     strlcpy(cpi->sim_vid, "FreeBSD", SIM_IDLEN);
     strlcpy(cpi->hba_vid, "iSCSI", HBA_IDLEN);
     strlcpy(cpi->dev_name, cam_sim_name(sim), DEV_IDLEN);
     cpi->unit_number = cam_sim_unit(sim);
     cpi->ccb_h.status = CAM_REQ_CMP;
#if defined(KNOB_VALID_ADDRESS)
     cpi->transport = XPORT_ISCSI;
     cpi->transport_version = 0;
#endif
}

static __inline int
_scsi_encap(struct cam_sim *sim, union ccb *ccb)
{
     int		ret;

#if __FreeBSD_version < 700000
     ret = scsi_encap(sim, ccb);
#else
     isc_session_t	*sp = cam_sim_softc(sim);

     mtx_unlock(&sp->cam_mtx);
     ret = scsi_encap(sim, ccb);
     mtx_lock(&sp->cam_mtx);
#endif
     return ret;
}

void
ic_lost_target(isc_session_t *sp, int target)
{
     debug_called(8);
     sdebug(2, "lost target=%d", target);

     if(sp->cam_path != NULL) {
	  mtx_lock(&sp->cam_mtx);
	  xpt_async(AC_LOST_DEVICE, sp->cam_path, NULL);
	  xpt_free_path(sp->cam_path);
	  mtx_unlock(&sp->cam_mtx);
	  sp->cam_path = 0; // XXX
     }
}

static void
scan_callback(struct cam_periph *periph, union ccb *ccb)
{
     isc_session_t *sp = (isc_session_t *)ccb->ccb_h.spriv_ptr0;

     debug_called(8);

     xpt_free_ccb(ccb);

     if(sp->flags & ISC_SCANWAIT) {
	  sp->flags &= ~ISC_SCANWAIT;
	  wakeup(sp);
     }
}

static int
ic_scan(isc_session_t *sp)
{
     union ccb	*ccb;

     debug_called(8);
     sdebug(2, "scanning sid=%d", sp->sid);

     sp->flags &= ~ISC_CAMDEVS;
     sp->flags |= ISC_SCANWAIT;

     ccb = xpt_alloc_ccb();
     ccb->ccb_h.path		= sp->cam_path;
     ccb->ccb_h.cbfcnp		= scan_callback;
     ccb->ccb_h.spriv_ptr0	= sp;

     xpt_rescan(ccb);

     while(sp->flags & ISC_SCANWAIT)
	  tsleep(sp, PRIBIO, "ffp", 5*hz); // the timeout time should
					    // be configurable
     sdebug(2, "# of luns=%d", sp->target_nluns);

     if(sp->target_nluns > 0) {
	  sp->flags |= ISC_CAMDEVS;
	  return 0;
     }

     return ENODEV;
}

static void
ic_action(struct cam_sim *sim, union ccb *ccb)
{
     isc_session_t	*sp = cam_sim_softc(sim);
     struct ccb_hdr	*ccb_h = &ccb->ccb_h;

     debug_called(8);

     ccb_h->spriv_ptr0 = sp;
     sdebug(4, "func_code=0x%x flags=0x%x status=0x%x target=%d lun=%jx retry_count=%d timeout=%d",
	   ccb_h->func_code, ccb->ccb_h.flags, ccb->ccb_h.status,
	   ccb->ccb_h.target_id, (uintmax_t)ccb->ccb_h.target_lun, 
	   ccb->ccb_h.retry_count, ccb_h->timeout);
     if(sp == NULL) {
	  xdebug("sp == NULL! cannot happen");
	  return;
     }	  
     switch(ccb_h->func_code) {
     case XPT_PATH_INQ:
	  _inq(sim, ccb);
	  break;

     case XPT_RESET_BUS: // (can just be a stub that does nothing and completes)
     {
	  struct ccb_pathinq *cpi = &ccb->cpi;

	  debug(3, "XPT_RESET_BUS");
	  cpi->ccb_h.status = CAM_REQ_CMP;
	  break;
     }

     case XPT_SCSI_IO: 
     {
	  struct ccb_scsiio* csio = &ccb->csio;

	  debug(4, "XPT_SCSI_IO cmd=0x%x", csio->cdb_io.cdb_bytes[0]);
	  if(sp == NULL) {
	       ccb_h->status = CAM_REQ_INVALID; //CAM_NO_NEXUS;
	       debug(4, "xpt_done.status=%d", ccb_h->status);
	       break;
	  }
	  if(ccb_h->target_lun == CAM_LUN_WILDCARD) {
	       debug(3, "target=%d: bad lun (-1)", ccb_h->target_id);
	       ccb_h->status = CAM_LUN_INVALID;
	       break;
	  }
	  if(_scsi_encap(sim, ccb) != 0)
	       return;
	  break;
     }
 
     case XPT_CALC_GEOMETRY:
     {
	  struct	ccb_calc_geometry *ccg;

	  ccg = &ccb->ccg;
	  debug(4, "sid=%d target=%d lun=%jx XPT_CALC_GEOMETRY vsize=%jd bsize=%d",
		sp->sid, ccb->ccb_h.target_id, (uintmax_t)ccb->ccb_h.target_lun,
		ccg->volume_size, ccg->block_size);
	  if(ccg->block_size == 0 ||
	     (ccg->volume_size < ccg->block_size)) {
	       // print error message  ...
	       /* XXX: what error is appropriate? */
	       break;
	  } 
	  else {
	       int	lun, *off, boff;

	       lun = ccb->ccb_h.target_lun;
	       if(lun > ISCSI_MAX_LUNS) {
		    // XXX: 
		    xdebug("lun %d > ISCSI_MAX_LUNS!\n", lun);
		    lun %= ISCSI_MAX_LUNS;
	       }
	       off = &sp->target_lun[lun / (sizeof(int)*8)];
	       boff = BIT(lun % (sizeof(int)*8));
	       debug(4, "sp->target_nluns=%d *off=%x boff=%x",
		     sp->target_nluns, *off, boff);

	       if((*off & boff) == 0) {
		    sp->target_nluns++;
		    *off |= boff;
	       }
	       cam_calc_geometry(ccg, /*extended*/1);
	  }
	  break;
     }

     case XPT_GET_TRAN_SETTINGS:
     default:
	  ccb_h->status = CAM_REQ_INVALID;
	  break;
     }
#if __FreeBSD_version < 700000
     XPT_DONE(sp, ccb);
#else
     xpt_done(ccb);
#endif
     return;
}

static void
ic_poll(struct cam_sim *sim)
{
     debug_called(4);

}

int
ic_getCamVals(isc_session_t *sp, iscsi_cam_t *cp)
{
     debug_called(8);

     if(sp && sp->cam_sim) {
	  cp->path_id = cam_sim_path(sp->cam_sim);
	  cp->target_id = 0;
	  cp->target_nluns = ISCSI_MAX_LUNS; // XXX: -1?
	  return 0;
     }
     return ENXIO;
}

void
ic_destroy(isc_session_t *sp )
{
     debug_called(8);

     if(sp->cam_path != NULL) {
	  sdebug(2, "name=%s unit=%d",
		 cam_sim_name(sp->cam_sim), cam_sim_unit(sp->cam_sim));
	  CAM_LOCK(sp);
#if 0
	  xpt_async(AC_LOST_DEVICE, sp->cam_path, NULL);
#else
	  xpt_async(XPT_RESET_BUS, sp->cam_path, NULL);
#endif
	  xpt_free_path(sp->cam_path);
	  xpt_bus_deregister(cam_sim_path(sp->cam_sim));
	  cam_sim_free(sp->cam_sim, TRUE /*free_devq*/);

	  CAM_UNLOCK(sp);
	  sdebug(2, "done");
     }
}

int
ic_init(isc_session_t *sp)
{
     struct cam_sim	*sim;
     struct cam_devq	*devq;

     debug_called(8);

     if((devq = cam_simq_alloc(256)) == NULL)
	  return ENOMEM;

#if __FreeBSD_version >= 700000
     mtx_init(&sp->cam_mtx, "isc-cam", NULL, MTX_DEF);
#else
     isp->cam_mtx = Giant;
#endif
     sim = cam_sim_alloc(ic_action,
			 ic_poll,
			 "iscsi",
			 sp,
			 sp->sid,	// unit
#if __FreeBSD_version >= 700000
			 &sp->cam_mtx,
#endif
			 1,		// max_dev_transactions
			 0,		// max_tagged_dev_transactions
			 devq);
     if(sim == NULL) {
	  cam_simq_free(devq);
#if __FreeBSD_version >= 700000
	  mtx_destroy(&sp->cam_mtx);
#endif
	  return ENXIO;
     }

     CAM_LOCK(sp);
     if(xpt_bus_register(sim,
#if __FreeBSD_version >= 700000
			 NULL,
#endif
			 0/*bus_number*/) != CAM_SUCCESS) {

	  cam_sim_free(sim, /*free_devq*/TRUE);
	  CAM_UNLOCK(sp);
#if __FreeBSD_version >= 700000
	  mtx_destroy(&sp->cam_mtx);
#endif
	  return ENXIO;
     }
     sp->cam_sim = sim;
     if(xpt_create_path(&sp->cam_path, NULL, cam_sim_path(sp->cam_sim),
	    CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
	  xpt_bus_deregister(cam_sim_path(sp->cam_sim));
	  cam_sim_free(sim, /*free_devq*/TRUE);
	  CAM_UNLOCK(sp);
#if __FreeBSD_version >= 700000
	  mtx_destroy(&sp->cam_mtx);
#endif
	  return ENXIO;
     }
     CAM_UNLOCK(sp);

     sdebug(1, "cam subsystem initialized");

     ic_scan(sp);

     return 0;
}
