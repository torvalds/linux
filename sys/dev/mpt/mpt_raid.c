/*-
 * Routines for handling the integrated RAID features LSI MPT Fusion adapters.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2005, WHEEL Sp. z o.o.
 * Copyright (c) 2005 Justin T. Gibbs.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon including
 *    a substantially similar Disclaimer requirement for further binary
 *    redistribution.
 * 3. Neither the names of the above listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF THE COPYRIGHT
 * OWNER OR CONTRIBUTOR IS ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*-
 * Some Breakage and Bug Fixing added later.
 * Copyright (c) 2006, by Matthew Jacob
 * All Rights Reserved
 *
 * Support from LSI-Logic has also gone a great deal toward making this a
 * workable subsystem and is gratefully acknowledged.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <dev/mpt/mpt.h>
#include <dev/mpt/mpt_raid.h>

#include "dev/mpt/mpilib/mpi_ioc.h" /* XXX Fix Event Handling!!! */
#include "dev/mpt/mpilib/mpi_raid.h"

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_periph.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt_sim.h>

#include <sys/callout.h>
#include <sys/kthread.h>
#include <sys/sysctl.h>

#include <machine/stdarg.h>

struct mpt_raid_action_result
{
	union {
		MPI_RAID_VOL_INDICATOR	indicator_struct;
		uint32_t		new_settings;
		uint8_t			phys_disk_num;
	} action_data;
	uint16_t			action_status;
};

#define REQ_TO_RAID_ACTION_RESULT(req) ((struct mpt_raid_action_result *) \
	(((MSG_RAID_ACTION_REQUEST *)(req->req_vbuf)) + 1))

#define REQ_IOCSTATUS(req) ((req)->IOCStatus & MPI_IOCSTATUS_MASK)

static mpt_probe_handler_t	mpt_raid_probe;
static mpt_attach_handler_t	mpt_raid_attach;
static mpt_enable_handler_t	mpt_raid_enable;
static mpt_event_handler_t	mpt_raid_event;
static mpt_shutdown_handler_t	mpt_raid_shutdown;
static mpt_reset_handler_t	mpt_raid_ioc_reset;
static mpt_detach_handler_t	mpt_raid_detach;

static struct mpt_personality mpt_raid_personality =
{
	.name		= "mpt_raid",
	.probe		= mpt_raid_probe,
	.attach		= mpt_raid_attach,
	.enable		= mpt_raid_enable,
	.event		= mpt_raid_event,
	.reset		= mpt_raid_ioc_reset,
	.shutdown	= mpt_raid_shutdown,
	.detach		= mpt_raid_detach,
};

DECLARE_MPT_PERSONALITY(mpt_raid, SI_ORDER_THIRD);
MPT_PERSONALITY_DEPEND(mpt_raid, mpt_cam, 1, 1, 1);

static mpt_reply_handler_t mpt_raid_reply_handler;
static int mpt_raid_reply_frame_handler(struct mpt_softc *mpt, request_t *req,
					MSG_DEFAULT_REPLY *reply_frame);
static int mpt_spawn_raid_thread(struct mpt_softc *mpt);
static void mpt_terminate_raid_thread(struct mpt_softc *mpt);
static void mpt_raid_thread(void *arg);
static timeout_t mpt_raid_timer;
#if 0
static void mpt_enable_vol(struct mpt_softc *mpt,
			   struct mpt_raid_volume *mpt_vol, int enable);
#endif
static void mpt_verify_mwce(struct mpt_softc *, struct mpt_raid_volume *);
static void mpt_adjust_queue_depth(struct mpt_softc *, struct mpt_raid_volume *,
    struct cam_path *);
static void mpt_raid_sysctl_attach(struct mpt_softc *);

static const char *mpt_vol_type(struct mpt_raid_volume *vol);
static const char *mpt_vol_state(struct mpt_raid_volume *vol);
static const char *mpt_disk_state(struct mpt_raid_disk *disk);
static void mpt_vol_prt(struct mpt_softc *mpt, struct mpt_raid_volume *vol,
    const char *fmt, ...);
static void mpt_disk_prt(struct mpt_softc *mpt, struct mpt_raid_disk *disk,
    const char *fmt, ...);

static int mpt_issue_raid_req(struct mpt_softc *mpt,
    struct mpt_raid_volume *vol, struct mpt_raid_disk *disk, request_t *req,
    u_int Action, uint32_t ActionDataWord, bus_addr_t addr, bus_size_t len,
    int write, int wait);

static int mpt_refresh_raid_data(struct mpt_softc *mpt);
static void mpt_schedule_raid_refresh(struct mpt_softc *mpt);

static uint32_t raid_handler_id = MPT_HANDLER_ID_NONE;

static const char *
mpt_vol_type(struct mpt_raid_volume *vol)
{
	switch (vol->config_page->VolumeType) {
	case MPI_RAID_VOL_TYPE_IS:
		return ("RAID-0");
	case MPI_RAID_VOL_TYPE_IME:
		return ("RAID-1E");
	case MPI_RAID_VOL_TYPE_IM:
		return ("RAID-1");
	default:
		return ("Unknown");
	}
}

static const char *
mpt_vol_state(struct mpt_raid_volume *vol)
{
	switch (vol->config_page->VolumeStatus.State) {
	case MPI_RAIDVOL0_STATUS_STATE_OPTIMAL:
		return ("Optimal");
	case MPI_RAIDVOL0_STATUS_STATE_DEGRADED:
		return ("Degraded");
	case MPI_RAIDVOL0_STATUS_STATE_FAILED:
		return ("Failed");
	default:
		return ("Unknown");
	}
}

static const char *
mpt_disk_state(struct mpt_raid_disk *disk)
{
	switch (disk->config_page.PhysDiskStatus.State) {
	case MPI_PHYSDISK0_STATUS_ONLINE:
		return ("Online");
	case MPI_PHYSDISK0_STATUS_MISSING:
		return ("Missing");
	case MPI_PHYSDISK0_STATUS_NOT_COMPATIBLE:
		return ("Incompatible");
	case MPI_PHYSDISK0_STATUS_FAILED:
		return ("Failed");
	case MPI_PHYSDISK0_STATUS_INITIALIZING:
		return ("Initializing");
	case MPI_PHYSDISK0_STATUS_OFFLINE_REQUESTED:
		return ("Offline Requested");
	case MPI_PHYSDISK0_STATUS_FAILED_REQUESTED:
		return ("Failed per Host Request");
	case MPI_PHYSDISK0_STATUS_OTHER_OFFLINE:
		return ("Offline");
	default:
		return ("Unknown");
	}
}

static void
mpt_vol_prt(struct mpt_softc *mpt, struct mpt_raid_volume *vol,
	    const char *fmt, ...)
{
	va_list ap;

	printf("%s:vol%d(%s:%d:%d): ", device_get_nameunit(mpt->dev),
	       (u_int)(vol - mpt->raid_volumes), device_get_nameunit(mpt->dev),
	       vol->config_page->VolumeBus, vol->config_page->VolumeID);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

static void
mpt_disk_prt(struct mpt_softc *mpt, struct mpt_raid_disk *disk,
	     const char *fmt, ...)
{
	va_list ap;

	if (disk->volume != NULL) {
		printf("(%s:vol%d:%d): ",
		       device_get_nameunit(mpt->dev),
		       disk->volume->config_page->VolumeID,
		       disk->member_number);
	} else {
		printf("(%s:%d:%d): ", device_get_nameunit(mpt->dev),
		       disk->config_page.PhysDiskBus,
		       disk->config_page.PhysDiskID);
	}
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

static void
mpt_raid_async(void *callback_arg, u_int32_t code,
	       struct cam_path *path, void *arg)
{
	struct mpt_softc *mpt;

	mpt = (struct mpt_softc*)callback_arg;
	switch (code) {
	case AC_FOUND_DEVICE:
	{
		struct ccb_getdev *cgd;
		struct mpt_raid_volume *mpt_vol;

		cgd = (struct ccb_getdev *)arg;
		if (cgd == NULL) {
			break;
		}

		mpt_lprt(mpt, MPT_PRT_DEBUG, "Callback for %d\n",
			 cgd->ccb_h.target_id);
		
		RAID_VOL_FOREACH(mpt, mpt_vol) {
			if ((mpt_vol->flags & MPT_RVF_ACTIVE) == 0)
				continue;

			if (mpt_vol->config_page->VolumeID 
			 == cgd->ccb_h.target_id) {
				mpt_adjust_queue_depth(mpt, mpt_vol, path);
				break;
			}
		}
	}
	default:
		break;
	}
}

static int
mpt_raid_probe(struct mpt_softc *mpt)
{

	if (mpt->ioc_page2 == NULL || mpt->ioc_page2->MaxPhysDisks == 0) {
		return (ENODEV);
	}
	return (0);
}

static int
mpt_raid_attach(struct mpt_softc *mpt)
{
	struct ccb_setasync csa;
	mpt_handler_t	 handler;
	int		 error;

	mpt_callout_init(mpt, &mpt->raid_timer);

	error = mpt_spawn_raid_thread(mpt);
	if (error != 0) {
		mpt_prt(mpt, "Unable to spawn RAID thread!\n");
		goto cleanup;
	}
 
	MPT_LOCK(mpt);
	handler.reply_handler = mpt_raid_reply_handler;
	error = mpt_register_handler(mpt, MPT_HANDLER_REPLY, handler,
				     &raid_handler_id);
	if (error != 0) {
		mpt_prt(mpt, "Unable to register RAID haandler!\n");
		goto cleanup;
	}

	xpt_setup_ccb(&csa.ccb_h, mpt->path, 5);
	csa.ccb_h.func_code = XPT_SASYNC_CB;
	csa.event_enable = AC_FOUND_DEVICE;
	csa.callback = mpt_raid_async;
	csa.callback_arg = mpt;
	xpt_action((union ccb *)&csa);
	if (csa.ccb_h.status != CAM_REQ_CMP) {
		mpt_prt(mpt, "mpt_raid_attach: Unable to register "
			"CAM async handler.\n");
	}
	MPT_UNLOCK(mpt);

	mpt_raid_sysctl_attach(mpt);
	return (0);
cleanup:
	MPT_UNLOCK(mpt);
	mpt_raid_detach(mpt);
	return (error);
}

static int
mpt_raid_enable(struct mpt_softc *mpt)
{

	return (0);
}

static void
mpt_raid_detach(struct mpt_softc *mpt)
{
	struct ccb_setasync csa;
	mpt_handler_t handler;

	mpt_callout_drain(mpt, &mpt->raid_timer);

	MPT_LOCK(mpt);
	mpt_terminate_raid_thread(mpt); 
	handler.reply_handler = mpt_raid_reply_handler;
	mpt_deregister_handler(mpt, MPT_HANDLER_REPLY, handler,
			       raid_handler_id);
	xpt_setup_ccb(&csa.ccb_h, mpt->path, /*priority*/5);
	csa.ccb_h.func_code = XPT_SASYNC_CB;
	csa.event_enable = 0;
	csa.callback = mpt_raid_async;
	csa.callback_arg = mpt;
	xpt_action((union ccb *)&csa);
	MPT_UNLOCK(mpt);
}

static void
mpt_raid_ioc_reset(struct mpt_softc *mpt, int type)
{

	/* Nothing to do yet. */
}

static const char *raid_event_txt[] =
{
	"Volume Created",
	"Volume Deleted",
	"Volume Settings Changed",
	"Volume Status Changed",
	"Volume Physical Disk Membership Changed",
	"Physical Disk Created",
	"Physical Disk Deleted",
	"Physical Disk Settings Changed",
	"Physical Disk Status Changed",
	"Domain Validation Required",
	"SMART Data Received",
	"Replace Action Started",
};

static int
mpt_raid_event(struct mpt_softc *mpt, request_t *req,
	       MSG_EVENT_NOTIFY_REPLY *msg)
{
	EVENT_DATA_RAID *raid_event;
	struct mpt_raid_volume *mpt_vol;
	struct mpt_raid_disk *mpt_disk;
	CONFIG_PAGE_RAID_VOL_0 *vol_pg;
	int i;
	int print_event;

	if (msg->Event != MPI_EVENT_INTEGRATED_RAID) {
		return (0);
	}

	raid_event = (EVENT_DATA_RAID *)&msg->Data;

	mpt_vol = NULL;
	vol_pg = NULL;
	if (mpt->raid_volumes != NULL && mpt->ioc_page2 != NULL) {
		for (i = 0; i < mpt->ioc_page2->MaxVolumes; i++) {
			mpt_vol = &mpt->raid_volumes[i];
			vol_pg = mpt_vol->config_page;

			if ((mpt_vol->flags & MPT_RVF_ACTIVE) == 0)
				continue;

			if (vol_pg->VolumeID == raid_event->VolumeID
			 && vol_pg->VolumeBus == raid_event->VolumeBus)
				break;
		}
		if (i >= mpt->ioc_page2->MaxVolumes) {
			mpt_vol = NULL;
			vol_pg = NULL;
		}
	}

	mpt_disk = NULL;
	if (raid_event->PhysDiskNum != 0xFF && mpt->raid_disks != NULL) {
		mpt_disk = mpt->raid_disks + raid_event->PhysDiskNum;
		if ((mpt_disk->flags & MPT_RDF_ACTIVE) == 0) {
			mpt_disk = NULL;
		}
	}

	print_event = 1;
	switch(raid_event->ReasonCode) {
	case MPI_EVENT_RAID_RC_VOLUME_CREATED:
	case MPI_EVENT_RAID_RC_VOLUME_DELETED:
		break;
	case MPI_EVENT_RAID_RC_VOLUME_STATUS_CHANGED:
		if (mpt_vol != NULL) {
			if ((mpt_vol->flags & MPT_RVF_UP2DATE) != 0) {
				mpt_vol->flags &= ~MPT_RVF_UP2DATE;
			} else {
				/*
				 * Coalesce status messages into one
				 * per background run of our RAID thread.
				 * This removes "spurious" status messages
				 * from our output.
				 */
				print_event = 0;
			}
		}
		break;
	case MPI_EVENT_RAID_RC_VOLUME_SETTINGS_CHANGED:
	case MPI_EVENT_RAID_RC_VOLUME_PHYSDISK_CHANGED:
		mpt->raid_rescan++;
		if (mpt_vol != NULL) {
			mpt_vol->flags &= ~(MPT_RVF_UP2DATE|MPT_RVF_ANNOUNCED);
		}
		break;
	case MPI_EVENT_RAID_RC_PHYSDISK_CREATED:
	case MPI_EVENT_RAID_RC_PHYSDISK_DELETED:
		mpt->raid_rescan++;
		break;
	case MPI_EVENT_RAID_RC_PHYSDISK_SETTINGS_CHANGED:
	case MPI_EVENT_RAID_RC_PHYSDISK_STATUS_CHANGED:
		mpt->raid_rescan++;
		if (mpt_disk != NULL) {
			mpt_disk->flags &= ~MPT_RDF_UP2DATE;
		}
		break;
	case MPI_EVENT_RAID_RC_DOMAIN_VAL_NEEDED:
		mpt->raid_rescan++;
		break;
	case MPI_EVENT_RAID_RC_SMART_DATA:
	case MPI_EVENT_RAID_RC_REPLACE_ACTION_STARTED:
		break;
	}

	if (print_event) {
		if (mpt_disk != NULL) {
			mpt_disk_prt(mpt, mpt_disk, "");
		} else if (mpt_vol != NULL) {
			mpt_vol_prt(mpt, mpt_vol, "");
		} else {
			mpt_prt(mpt, "Volume(%d:%d", raid_event->VolumeBus,
				raid_event->VolumeID);

			if (raid_event->PhysDiskNum != 0xFF)
				mpt_prtc(mpt, ":%d): ",
					 raid_event->PhysDiskNum);
			else
				mpt_prtc(mpt, "): ");
		}

		if (raid_event->ReasonCode >= NUM_ELEMENTS(raid_event_txt))
			mpt_prtc(mpt, "Unhandled RaidEvent %#x\n",
				 raid_event->ReasonCode);
		else
			mpt_prtc(mpt, "%s\n",
				 raid_event_txt[raid_event->ReasonCode]);
	}

	if (raid_event->ReasonCode == MPI_EVENT_RAID_RC_SMART_DATA) {
		/* XXX Use CAM's print sense for this... */
		if (mpt_disk != NULL)
			mpt_disk_prt(mpt, mpt_disk, "");
		else
			mpt_prt(mpt, "Volume(%d:%d:%d: ",
			    raid_event->VolumeBus, raid_event->VolumeID,
			    raid_event->PhysDiskNum);
		mpt_prtc(mpt, "ASC 0x%x, ASCQ 0x%x)\n",
			 raid_event->ASC, raid_event->ASCQ);
	}

	mpt_raid_wakeup(mpt);
	return (1);
}

static void
mpt_raid_shutdown(struct mpt_softc *mpt)
{
	struct mpt_raid_volume *mpt_vol;

	if (mpt->raid_mwce_setting != MPT_RAID_MWCE_REBUILD_ONLY) {
		return;
	}

	mpt->raid_mwce_setting = MPT_RAID_MWCE_OFF;
	RAID_VOL_FOREACH(mpt, mpt_vol) {
		mpt_verify_mwce(mpt, mpt_vol);
	}
}

static int
mpt_raid_reply_handler(struct mpt_softc *mpt, request_t *req,
    uint32_t reply_desc, MSG_DEFAULT_REPLY *reply_frame)
{
	int free_req;

	if (req == NULL)
		return (TRUE);

	free_req = TRUE;
	if (reply_frame != NULL)
		free_req = mpt_raid_reply_frame_handler(mpt, req, reply_frame);
#ifdef NOTYET
	else if (req->ccb != NULL) {
		/* Complete Quiesce CCB with error... */
	}
#endif

	req->state &= ~REQ_STATE_QUEUED;
	req->state |= REQ_STATE_DONE;
	TAILQ_REMOVE(&mpt->request_pending_list, req, links);

	if ((req->state & REQ_STATE_NEED_WAKEUP) != 0) {
		wakeup(req);
	} else if (free_req) {
		mpt_free_request(mpt, req);
	}

	return (TRUE);
}

/*
 * Parse additional completion information in the reply
 * frame for RAID I/O requests.
 */
static int
mpt_raid_reply_frame_handler(struct mpt_softc *mpt, request_t *req,
    MSG_DEFAULT_REPLY *reply_frame)
{
	MSG_RAID_ACTION_REPLY *reply;
	struct mpt_raid_action_result *action_result;
	MSG_RAID_ACTION_REQUEST *rap;

	reply = (MSG_RAID_ACTION_REPLY *)reply_frame;
	req->IOCStatus = le16toh(reply->IOCStatus);
	rap = (MSG_RAID_ACTION_REQUEST *)req->req_vbuf;
	
	switch (rap->Action) {
	case MPI_RAID_ACTION_QUIESCE_PHYS_IO:
		mpt_prt(mpt, "QUIESCE PHYSIO DONE\n");
		break;
	case MPI_RAID_ACTION_ENABLE_PHYS_IO:
		mpt_prt(mpt, "ENABLY PHYSIO DONE\n");
		break;
	default:
		break;
	}
	action_result = REQ_TO_RAID_ACTION_RESULT(req);
	memcpy(&action_result->action_data, &reply->ActionData,
	    sizeof(action_result->action_data));
	action_result->action_status = le16toh(reply->ActionStatus);
	return (TRUE);
}

/*
 * Utiltity routine to perform a RAID action command;
 */
static int
mpt_issue_raid_req(struct mpt_softc *mpt, struct mpt_raid_volume *vol,
		   struct mpt_raid_disk *disk, request_t *req, u_int Action,
		   uint32_t ActionDataWord, bus_addr_t addr, bus_size_t len,
		   int write, int wait)
{
	MSG_RAID_ACTION_REQUEST *rap;
	SGE_SIMPLE32 *se;

	rap = req->req_vbuf;
	memset(rap, 0, sizeof *rap);
	rap->Action = Action;
	rap->ActionDataWord = htole32(ActionDataWord);
	rap->Function = MPI_FUNCTION_RAID_ACTION;
	rap->VolumeID = vol->config_page->VolumeID;
	rap->VolumeBus = vol->config_page->VolumeBus;
	if (disk != NULL)
		rap->PhysDiskNum = disk->config_page.PhysDiskNum;
	else
		rap->PhysDiskNum = 0xFF;
	se = (SGE_SIMPLE32 *)&rap->ActionDataSGE;
	se->Address = htole32(addr);
	MPI_pSGE_SET_LENGTH(se, len);
	MPI_pSGE_SET_FLAGS(se, (MPI_SGE_FLAGS_SIMPLE_ELEMENT |
	    MPI_SGE_FLAGS_LAST_ELEMENT | MPI_SGE_FLAGS_END_OF_BUFFER |
	    MPI_SGE_FLAGS_END_OF_LIST |
	    (write ? MPI_SGE_FLAGS_HOST_TO_IOC : MPI_SGE_FLAGS_IOC_TO_HOST)));
	se->FlagsLength = htole32(se->FlagsLength);
	rap->MsgContext = htole32(req->index | raid_handler_id);

	mpt_check_doorbell(mpt);
	mpt_send_cmd(mpt, req);

	if (wait) {
		return (mpt_wait_req(mpt, req, REQ_STATE_DONE, REQ_STATE_DONE,
				     /*sleep_ok*/FALSE, /*time_ms*/2000));
	} else {
		return (0);
	}
}

/*************************** RAID Status Monitoring ***************************/
static int
mpt_spawn_raid_thread(struct mpt_softc *mpt)
{
	int error;

	/*
	 * Freeze out any CAM transactions until our thread
	 * is able to run at least once.  We need to update
	 * our RAID pages before acception I/O or we may
	 * reject I/O to an ID we later determine is for a
	 * hidden physdisk.
	 */
	MPT_LOCK(mpt);
	xpt_freeze_simq(mpt->phydisk_sim, 1);
	MPT_UNLOCK(mpt);
	error = kproc_create(mpt_raid_thread, mpt,
	    &mpt->raid_thread, /*flags*/0, /*altstack*/0,
	    "mpt_raid%d", mpt->unit);
	if (error != 0) {
		MPT_LOCK(mpt);
		xpt_release_simq(mpt->phydisk_sim, /*run_queue*/FALSE);
		MPT_UNLOCK(mpt);
	}
	return (error);
}

static void
mpt_terminate_raid_thread(struct mpt_softc *mpt)
{

	if (mpt->raid_thread == NULL) {
		return;
	}
	mpt->shutdwn_raid = 1;
	wakeup(&mpt->raid_volumes);
	/*
	 * Sleep on a slightly different location
	 * for this interlock just for added safety.
	 */
	mpt_sleep(mpt, &mpt->raid_thread, PUSER, "thtrm", 0);
}

static void
mpt_raid_thread(void *arg)
{
	struct mpt_softc *mpt;
	int firstrun;

	mpt = (struct mpt_softc *)arg;
	firstrun = 1;
	MPT_LOCK(mpt);
	while (mpt->shutdwn_raid == 0) {

		if (mpt->raid_wakeup == 0) {
			mpt_sleep(mpt, &mpt->raid_volumes, PUSER, "idle", 0);
			continue;
		}

		mpt->raid_wakeup = 0;

		if (mpt_refresh_raid_data(mpt)) {
			mpt_schedule_raid_refresh(mpt);	/* XX NOT QUITE RIGHT */
			continue;
		}

		/*
		 * Now that we have our first snapshot of RAID data,
		 * allow CAM to access our physical disk bus.
		 */
		if (firstrun) {
			firstrun = 0;
			xpt_release_simq(mpt->phydisk_sim, TRUE);
		}

		if (mpt->raid_rescan != 0) {
			union ccb *ccb;
			int error;

			mpt->raid_rescan = 0;
			MPT_UNLOCK(mpt);

			ccb = xpt_alloc_ccb();

			MPT_LOCK(mpt);
			error = xpt_create_path(&ccb->ccb_h.path, NULL,
			    cam_sim_path(mpt->phydisk_sim),
			    CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD);
			if (error != CAM_REQ_CMP) {
				xpt_free_ccb(ccb);
				mpt_prt(mpt, "Unable to rescan RAID Bus!\n");
			} else {
				xpt_rescan(ccb);
			}
		}
	}
	mpt->raid_thread = NULL;
	wakeup(&mpt->raid_thread);
	MPT_UNLOCK(mpt);
	kproc_exit(0);
}

#if 0
static void
mpt_raid_quiesce_timeout(void *arg)
{

	/* Complete the CCB with error */
	/* COWWWW */
}

static timeout_t mpt_raid_quiesce_timeout;
cam_status
mpt_raid_quiesce_disk(struct mpt_softc *mpt, struct mpt_raid_disk *mpt_disk,
		      request_t *req)
{
	union ccb *ccb;

	ccb = req->ccb;
	if ((mpt_disk->flags & MPT_RDF_QUIESCED) != 0)
		return (CAM_REQ_CMP);

	if ((mpt_disk->flags & MPT_RDF_QUIESCING) == 0) {
		int rv;

		mpt_disk->flags |= MPT_RDF_QUIESCING;
		xpt_freeze_devq(ccb->ccb_h.path, 1);
		
		rv = mpt_issue_raid_req(mpt, mpt_disk->volume, mpt_disk, req,
					MPI_RAID_ACTION_QUIESCE_PHYS_IO,
					/*ActionData*/0, /*addr*/0,
					/*len*/0, /*write*/FALSE,
					/*wait*/FALSE);
		if (rv != 0)
			return (CAM_REQ_CMP_ERR);

		mpt_req_timeout(req, mpt_raid_quiesce_timeout, ccb, 5 * hz);
#if 0
		if (rv == ETIMEDOUT) {
			mpt_disk_prt(mpt, mpt_disk, "mpt_raid_quiesce_disk: "
				     "Quiece Timed-out\n");
			xpt_release_devq(ccb->ccb_h.path, 1, /*run*/0);
			return (CAM_REQ_CMP_ERR);
		}

		ar = REQ_TO_RAID_ACTION_RESULT(req);
		if (rv != 0
		 || REQ_IOCSTATUS(req) != MPI_IOCSTATUS_SUCCESS
		 || (ar->action_status != MPI_RAID_ACTION_ASTATUS_SUCCESS)) {
			mpt_disk_prt(mpt, mpt_disk, "Quiece Failed"
				    "%d:%x:%x\n", rv, req->IOCStatus,
				    ar->action_status);
			xpt_release_devq(ccb->ccb_h.path, 1, /*run*/0);
			return (CAM_REQ_CMP_ERR);
		}
#endif
		return (CAM_REQ_INPROG);
	}
	return (CAM_REQUEUE_REQ);
}
#endif

/* XXX Ignores that there may be multiple buses/IOCs involved. */
cam_status
mpt_map_physdisk(struct mpt_softc *mpt, union ccb *ccb, target_id_t *tgt)
{
	struct mpt_raid_disk *mpt_disk;

	mpt_disk = mpt->raid_disks + ccb->ccb_h.target_id;
	if (ccb->ccb_h.target_id < mpt->raid_max_disks
	 && (mpt_disk->flags & MPT_RDF_ACTIVE) != 0) {
		*tgt = mpt_disk->config_page.PhysDiskID;
		return (0);
	}
	mpt_lprt(mpt, MPT_PRT_DEBUG1, "mpt_map_physdisk(%d) - Not Active\n",
		 ccb->ccb_h.target_id);
	return (-1);
}

/* XXX Ignores that there may be multiple buses/IOCs involved. */
int
mpt_is_raid_member(struct mpt_softc *mpt, target_id_t tgt)
{
	struct mpt_raid_disk *mpt_disk;
	int i;

	if (mpt->ioc_page2 == NULL || mpt->ioc_page2->MaxPhysDisks == 0)
		return (0);
	for (i = 0; i < mpt->ioc_page2->MaxPhysDisks; i++) {
		mpt_disk = &mpt->raid_disks[i];
		if ((mpt_disk->flags & MPT_RDF_ACTIVE) != 0 &&
		    mpt_disk->config_page.PhysDiskID == tgt)
			return (1);
	}
	return (0);
	
}

/* XXX Ignores that there may be multiple buses/IOCs involved. */
int
mpt_is_raid_volume(struct mpt_softc *mpt, target_id_t tgt)
{
	CONFIG_PAGE_IOC_2_RAID_VOL *ioc_vol;
	CONFIG_PAGE_IOC_2_RAID_VOL *ioc_last_vol;

	if (mpt->ioc_page2 == NULL || mpt->ioc_page2->MaxPhysDisks == 0) {
		return (0);
	}
	ioc_vol = mpt->ioc_page2->RaidVolume;
	ioc_last_vol = ioc_vol + mpt->ioc_page2->NumActiveVolumes;
	for (;ioc_vol != ioc_last_vol; ioc_vol++) {
		if (ioc_vol->VolumeID == tgt) {
			return (1);
		}
	}
	return (0);
}

#if 0
static void
mpt_enable_vol(struct mpt_softc *mpt, struct mpt_raid_volume *mpt_vol,
	       int enable)
{
	request_t *req;
	struct mpt_raid_action_result *ar;
	CONFIG_PAGE_RAID_VOL_0 *vol_pg;
	int enabled;
	int rv;

	vol_pg = mpt_vol->config_page;
	enabled = vol_pg->VolumeStatus.Flags & MPI_RAIDVOL0_STATUS_FLAG_ENABLED;

	/*
	 * If the setting matches the configuration,
	 * there is nothing to do.
	 */
	if ((enabled && enable)
	 || (!enabled && !enable))
		return;

	req = mpt_get_request(mpt, /*sleep_ok*/TRUE);
	if (req == NULL) {
		mpt_vol_prt(mpt, mpt_vol,
			    "mpt_enable_vol: Get request failed!\n");
		return;
	}

	rv = mpt_issue_raid_req(mpt, mpt_vol, /*disk*/NULL, req,
				enable ? MPI_RAID_ACTION_ENABLE_VOLUME
				       : MPI_RAID_ACTION_DISABLE_VOLUME,
				/*data*/0, /*addr*/0, /*len*/0,
				/*write*/FALSE, /*wait*/TRUE);
	if (rv == ETIMEDOUT) {
		mpt_vol_prt(mpt, mpt_vol, "mpt_enable_vol: "
			    "%s Volume Timed-out\n",
			    enable ? "Enable" : "Disable");
		return;
	}
	ar = REQ_TO_RAID_ACTION_RESULT(req);
	if (rv != 0
	 || REQ_IOCSTATUS(req) != MPI_IOCSTATUS_SUCCESS
	 || (ar->action_status != MPI_RAID_ACTION_ASTATUS_SUCCESS)) {
		mpt_vol_prt(mpt, mpt_vol, "%s Volume Failed: %d:%x:%x\n",
			    enable ? "Enable" : "Disable",
			    rv, req->IOCStatus, ar->action_status);
	}

	mpt_free_request(mpt, req);
}
#endif

static void
mpt_verify_mwce(struct mpt_softc *mpt, struct mpt_raid_volume *mpt_vol)
{
	request_t *req;
	struct mpt_raid_action_result *ar;
	CONFIG_PAGE_RAID_VOL_0 *vol_pg;
	uint32_t data;
	int rv;
	int resyncing;
	int mwce;

	vol_pg = mpt_vol->config_page;
	resyncing = vol_pg->VolumeStatus.Flags
		  & MPI_RAIDVOL0_STATUS_FLAG_RESYNC_IN_PROGRESS;
	mwce = vol_pg->VolumeSettings.Settings
	     & MPI_RAIDVOL0_SETTING_WRITE_CACHING_ENABLE;

	/*
	 * If the setting matches the configuration,
	 * there is nothing to do.
	 */
	switch (mpt->raid_mwce_setting) {
	case MPT_RAID_MWCE_REBUILD_ONLY:
		if ((resyncing && mwce) || (!resyncing && !mwce)) {
			return;
		}
		mpt_vol->flags ^= MPT_RVF_WCE_CHANGED;
		if ((mpt_vol->flags & MPT_RVF_WCE_CHANGED) == 0) {
			/*
			 * Wait one more status update to see if
			 * resyncing gets enabled.  It gets disabled
			 * temporarilly when WCE is changed.
			 */
			return;
		}
		break;
	case MPT_RAID_MWCE_ON:
		if (mwce)
			return;
		break;
	case MPT_RAID_MWCE_OFF:
		if (!mwce)
			return;
		break;
	case MPT_RAID_MWCE_NC:
		return;
	}

	req = mpt_get_request(mpt, /*sleep_ok*/TRUE);
	if (req == NULL) {
		mpt_vol_prt(mpt, mpt_vol,
			    "mpt_verify_mwce: Get request failed!\n");
		return;
	}

	vol_pg->VolumeSettings.Settings ^=
	    MPI_RAIDVOL0_SETTING_WRITE_CACHING_ENABLE;
	memcpy(&data, &vol_pg->VolumeSettings, sizeof(data));
	vol_pg->VolumeSettings.Settings ^=
	    MPI_RAIDVOL0_SETTING_WRITE_CACHING_ENABLE;
	rv = mpt_issue_raid_req(mpt, mpt_vol, /*disk*/NULL, req,
				MPI_RAID_ACTION_CHANGE_VOLUME_SETTINGS,
				data, /*addr*/0, /*len*/0,
				/*write*/FALSE, /*wait*/TRUE);
	if (rv == ETIMEDOUT) {
		mpt_vol_prt(mpt, mpt_vol, "mpt_verify_mwce: "
			    "Write Cache Enable Timed-out\n");
		return;
	}
	ar = REQ_TO_RAID_ACTION_RESULT(req);
	if (rv != 0
	 || REQ_IOCSTATUS(req) != MPI_IOCSTATUS_SUCCESS
	 || (ar->action_status != MPI_RAID_ACTION_ASTATUS_SUCCESS)) {
		mpt_vol_prt(mpt, mpt_vol, "Write Cache Enable Failed: "
			    "%d:%x:%x\n", rv, req->IOCStatus,
			    ar->action_status);
	} else {
		vol_pg->VolumeSettings.Settings ^=
		    MPI_RAIDVOL0_SETTING_WRITE_CACHING_ENABLE;
	}
	mpt_free_request(mpt, req);
}

static void
mpt_verify_resync_rate(struct mpt_softc *mpt, struct mpt_raid_volume *mpt_vol)
{
	request_t *req;
	struct mpt_raid_action_result *ar;
	CONFIG_PAGE_RAID_VOL_0	*vol_pg;
	u_int prio;
	int rv;

	vol_pg = mpt_vol->config_page;

	if (mpt->raid_resync_rate == MPT_RAID_RESYNC_RATE_NC)
		return;

	/*
	 * If the current RAID resync rate does not
	 * match our configured rate, update it.
	 */
	prio = vol_pg->VolumeSettings.Settings
	     & MPI_RAIDVOL0_SETTING_PRIORITY_RESYNC;
	if (vol_pg->ResyncRate != 0
	 && vol_pg->ResyncRate != mpt->raid_resync_rate) {

		req = mpt_get_request(mpt, /*sleep_ok*/TRUE);
		if (req == NULL) {
			mpt_vol_prt(mpt, mpt_vol, "mpt_verify_resync_rate: "
				    "Get request failed!\n");
			return;
		}

		rv = mpt_issue_raid_req(mpt, mpt_vol, /*disk*/NULL, req,
					MPI_RAID_ACTION_SET_RESYNC_RATE,
					mpt->raid_resync_rate, /*addr*/0,
					/*len*/0, /*write*/FALSE, /*wait*/TRUE);
		if (rv == ETIMEDOUT) {
			mpt_vol_prt(mpt, mpt_vol, "mpt_refresh_raid_data: "
				    "Resync Rate Setting Timed-out\n");
			return;
		}

		ar = REQ_TO_RAID_ACTION_RESULT(req);
		if (rv != 0
		 || REQ_IOCSTATUS(req) != MPI_IOCSTATUS_SUCCESS
		 || (ar->action_status != MPI_RAID_ACTION_ASTATUS_SUCCESS)) {
			mpt_vol_prt(mpt, mpt_vol, "Resync Rate Setting Failed: "
				    "%d:%x:%x\n", rv, req->IOCStatus,
				    ar->action_status);
		} else 
			vol_pg->ResyncRate = mpt->raid_resync_rate;
		mpt_free_request(mpt, req);
	} else if ((prio && mpt->raid_resync_rate < 128)
		|| (!prio && mpt->raid_resync_rate >= 128)) {
		uint32_t data;

		req = mpt_get_request(mpt, /*sleep_ok*/TRUE);
		if (req == NULL) {
			mpt_vol_prt(mpt, mpt_vol, "mpt_verify_resync_rate: "
				    "Get request failed!\n");
			return;
		}

		vol_pg->VolumeSettings.Settings ^=
		    MPI_RAIDVOL0_SETTING_PRIORITY_RESYNC;
		memcpy(&data, &vol_pg->VolumeSettings, sizeof(data));
		vol_pg->VolumeSettings.Settings ^=
		    MPI_RAIDVOL0_SETTING_PRIORITY_RESYNC;
		rv = mpt_issue_raid_req(mpt, mpt_vol, /*disk*/NULL, req,
					MPI_RAID_ACTION_CHANGE_VOLUME_SETTINGS,
					data, /*addr*/0, /*len*/0,
					/*write*/FALSE, /*wait*/TRUE);
		if (rv == ETIMEDOUT) {
			mpt_vol_prt(mpt, mpt_vol, "mpt_refresh_raid_data: "
				    "Resync Rate Setting Timed-out\n");
			return;
		}
		ar = REQ_TO_RAID_ACTION_RESULT(req);
		if (rv != 0
		 || REQ_IOCSTATUS(req) != MPI_IOCSTATUS_SUCCESS
		 || (ar->action_status != MPI_RAID_ACTION_ASTATUS_SUCCESS)) {
			mpt_vol_prt(mpt, mpt_vol, "Resync Rate Setting Failed: "
				    "%d:%x:%x\n", rv, req->IOCStatus,
				    ar->action_status);
		} else {
			vol_pg->VolumeSettings.Settings ^=
			    MPI_RAIDVOL0_SETTING_PRIORITY_RESYNC;
		}

		mpt_free_request(mpt, req);
	}
}

static void
mpt_adjust_queue_depth(struct mpt_softc *mpt, struct mpt_raid_volume *mpt_vol,
		       struct cam_path *path)
{
	struct ccb_relsim crs;

	xpt_setup_ccb(&crs.ccb_h, path, /*priority*/5);
	crs.ccb_h.func_code = XPT_REL_SIMQ;
	crs.ccb_h.flags = CAM_DEV_QFREEZE;
	crs.release_flags = RELSIM_ADJUST_OPENINGS;
	crs.openings = mpt->raid_queue_depth;
	xpt_action((union ccb *)&crs);
	if (crs.ccb_h.status != CAM_REQ_CMP)
		mpt_vol_prt(mpt, mpt_vol, "mpt_adjust_queue_depth failed "
			    "with CAM status %#x\n", crs.ccb_h.status);
}

static void
mpt_announce_vol(struct mpt_softc *mpt, struct mpt_raid_volume *mpt_vol)
{
	CONFIG_PAGE_RAID_VOL_0 *vol_pg;
	u_int i;

	vol_pg = mpt_vol->config_page;
	mpt_vol_prt(mpt, mpt_vol, "Settings (");
	for (i = 1; i <= 0x8000; i <<= 1) {
		switch (vol_pg->VolumeSettings.Settings & i) {
		case MPI_RAIDVOL0_SETTING_WRITE_CACHING_ENABLE:
			mpt_prtc(mpt, " Member-WCE");
			break;
		case MPI_RAIDVOL0_SETTING_OFFLINE_ON_SMART:
			mpt_prtc(mpt, " Offline-On-SMART-Err");
			break;
		case MPI_RAIDVOL0_SETTING_AUTO_CONFIGURE:
			mpt_prtc(mpt, " Hot-Plug-Spares");
			break;
		case MPI_RAIDVOL0_SETTING_PRIORITY_RESYNC:
			mpt_prtc(mpt, " High-Priority-ReSync");
			break;
		default:
			break;
		}
	}
	mpt_prtc(mpt, " )\n");
	if (vol_pg->VolumeSettings.HotSparePool != 0) {
		mpt_vol_prt(mpt, mpt_vol, "Using Spare Pool%s",
			    powerof2(vol_pg->VolumeSettings.HotSparePool)
			  ? ":" : "s:");
		for (i = 0; i < 8; i++) {
			u_int mask;

			mask = 0x1 << i;
			if ((vol_pg->VolumeSettings.HotSparePool & mask) == 0)
				continue;
			mpt_prtc(mpt, " %d", i);
		}
		mpt_prtc(mpt, "\n");
	}
	mpt_vol_prt(mpt, mpt_vol, "%d Members:\n", vol_pg->NumPhysDisks);
	for (i = 0; i < vol_pg->NumPhysDisks; i++){
		struct mpt_raid_disk *mpt_disk;
		CONFIG_PAGE_RAID_PHYS_DISK_0 *disk_pg;
		int pt_bus = cam_sim_bus(mpt->phydisk_sim);
		U8 f, s;

		mpt_disk = mpt->raid_disks + vol_pg->PhysDisk[i].PhysDiskNum;
		disk_pg = &mpt_disk->config_page;
		mpt_prtc(mpt, "      ");
		mpt_prtc(mpt, "(%s:%d:%d:0): ", device_get_nameunit(mpt->dev),
			 pt_bus, disk_pg->PhysDiskID);
		if (vol_pg->VolumeType == MPI_RAID_VOL_TYPE_IM) {
			mpt_prtc(mpt, "%s", mpt_disk->member_number == 0?
			    "Primary" : "Secondary");
		} else {
			mpt_prtc(mpt, "Stripe Position %d",
				 mpt_disk->member_number);
		}
		f = disk_pg->PhysDiskStatus.Flags;
		s = disk_pg->PhysDiskStatus.State;
		if (f & MPI_PHYSDISK0_STATUS_FLAG_OUT_OF_SYNC) {
			mpt_prtc(mpt, " Out of Sync");
		}
		if (f & MPI_PHYSDISK0_STATUS_FLAG_QUIESCED) {
			mpt_prtc(mpt, " Quiesced");
		}
		if (f & MPI_PHYSDISK0_STATUS_FLAG_INACTIVE_VOLUME) {
			mpt_prtc(mpt, " Inactive");
		}
		if (f & MPI_PHYSDISK0_STATUS_FLAG_OPTIMAL_PREVIOUS) {
			mpt_prtc(mpt, " Was Optimal");
		}
		if (f & MPI_PHYSDISK0_STATUS_FLAG_NOT_OPTIMAL_PREVIOUS) {
			mpt_prtc(mpt, " Was Non-Optimal");
		}
		switch (s) {
		case MPI_PHYSDISK0_STATUS_ONLINE:
			mpt_prtc(mpt, " Online");
			break;
		case MPI_PHYSDISK0_STATUS_MISSING:
			mpt_prtc(mpt, " Missing");
			break;
		case MPI_PHYSDISK0_STATUS_NOT_COMPATIBLE:
			mpt_prtc(mpt, " Incompatible");
			break;
		case MPI_PHYSDISK0_STATUS_FAILED:
			mpt_prtc(mpt, " Failed");
			break;
		case MPI_PHYSDISK0_STATUS_INITIALIZING:
			mpt_prtc(mpt, " Initializing");
			break;
		case MPI_PHYSDISK0_STATUS_OFFLINE_REQUESTED:
			mpt_prtc(mpt, " Requested Offline");
			break;
		case MPI_PHYSDISK0_STATUS_FAILED_REQUESTED:
			mpt_prtc(mpt, " Requested Failed");
			break;
		case MPI_PHYSDISK0_STATUS_OTHER_OFFLINE:
		default:
			mpt_prtc(mpt, " Offline Other (%x)", s);
			break;
		}
		mpt_prtc(mpt, "\n");
	}
}

static void
mpt_announce_disk(struct mpt_softc *mpt, struct mpt_raid_disk *mpt_disk)
{
	CONFIG_PAGE_RAID_PHYS_DISK_0 *disk_pg;
	int rd_bus = cam_sim_bus(mpt->sim);
	int pt_bus = cam_sim_bus(mpt->phydisk_sim);
	u_int i;

	disk_pg = &mpt_disk->config_page;
	mpt_disk_prt(mpt, mpt_disk,
		     "Physical (%s:%d:%d:0), Pass-thru (%s:%d:%d:0)\n",
		     device_get_nameunit(mpt->dev), rd_bus,
		     disk_pg->PhysDiskID, device_get_nameunit(mpt->dev),
		     pt_bus, mpt_disk - mpt->raid_disks);
	if (disk_pg->PhysDiskSettings.HotSparePool == 0)
		return;
	mpt_disk_prt(mpt, mpt_disk, "Member of Hot Spare Pool%s",
		     powerof2(disk_pg->PhysDiskSettings.HotSparePool)
		   ? ":" : "s:");
	for (i = 0; i < 8; i++) {
		u_int mask;

		mask = 0x1 << i;
		if ((disk_pg->PhysDiskSettings.HotSparePool & mask) == 0)
			continue;
		mpt_prtc(mpt, " %d", i);
	}
	mpt_prtc(mpt, "\n");
}

static void
mpt_refresh_raid_disk(struct mpt_softc *mpt, struct mpt_raid_disk *mpt_disk,
		      IOC_3_PHYS_DISK *ioc_disk)
{
	int rv;

	rv = mpt_read_cfg_header(mpt, MPI_CONFIG_PAGETYPE_RAID_PHYSDISK,
				 /*PageNumber*/0, ioc_disk->PhysDiskNum,
				 &mpt_disk->config_page.Header,
				 /*sleep_ok*/TRUE, /*timeout_ms*/5000);
	if (rv != 0) {
		mpt_prt(mpt, "mpt_refresh_raid_disk: "
			"Failed to read RAID Disk Hdr(%d)\n",
		 	ioc_disk->PhysDiskNum);
		return;
	}
	rv = mpt_read_cur_cfg_page(mpt, ioc_disk->PhysDiskNum,
				   &mpt_disk->config_page.Header,
				   sizeof(mpt_disk->config_page),
				   /*sleep_ok*/TRUE, /*timeout_ms*/5000);
	if (rv != 0)
		mpt_prt(mpt, "mpt_refresh_raid_disk: "
			"Failed to read RAID Disk Page(%d)\n",
		 	ioc_disk->PhysDiskNum);
	mpt2host_config_page_raid_phys_disk_0(&mpt_disk->config_page);
}

static void
mpt_refresh_raid_vol(struct mpt_softc *mpt, struct mpt_raid_volume *mpt_vol,
    CONFIG_PAGE_IOC_2_RAID_VOL *ioc_vol)
{
	CONFIG_PAGE_RAID_VOL_0 *vol_pg;
	struct mpt_raid_action_result *ar;
	request_t *req;
	int rv;
	int i;

	vol_pg = mpt_vol->config_page;
	mpt_vol->flags &= ~MPT_RVF_UP2DATE;

	rv = mpt_read_cfg_header(mpt, MPI_CONFIG_PAGETYPE_RAID_VOLUME, 0,
	    ioc_vol->VolumePageNumber, &vol_pg->Header, TRUE, 5000);
	if (rv != 0) {
		mpt_vol_prt(mpt, mpt_vol,
		    "mpt_refresh_raid_vol: Failed to read RAID Vol Hdr(%d)\n",
		    ioc_vol->VolumePageNumber);
		return;
	}

	rv = mpt_read_cur_cfg_page(mpt, ioc_vol->VolumePageNumber,
	    &vol_pg->Header, mpt->raid_page0_len, TRUE, 5000);
	if (rv != 0) {
		mpt_vol_prt(mpt, mpt_vol,
		    "mpt_refresh_raid_vol: Failed to read RAID Vol Page(%d)\n",
		    ioc_vol->VolumePageNumber);
		return;
	}
	mpt2host_config_page_raid_vol_0(vol_pg);

	mpt_vol->flags |= MPT_RVF_ACTIVE;

	/* Update disk entry array data. */
	for (i = 0; i < vol_pg->NumPhysDisks; i++) {
		struct mpt_raid_disk *mpt_disk;
		mpt_disk = mpt->raid_disks + vol_pg->PhysDisk[i].PhysDiskNum;
		mpt_disk->volume = mpt_vol;
		mpt_disk->member_number = vol_pg->PhysDisk[i].PhysDiskMap;
		if (vol_pg->VolumeType == MPI_RAID_VOL_TYPE_IM) {
			mpt_disk->member_number--;
		}
	}

	if ((vol_pg->VolumeStatus.Flags
	   & MPI_RAIDVOL0_STATUS_FLAG_RESYNC_IN_PROGRESS) == 0)
		return;

	req = mpt_get_request(mpt, TRUE);
	if (req == NULL) {
		mpt_vol_prt(mpt, mpt_vol,
		    "mpt_refresh_raid_vol: Get request failed!\n");
		return;
	}
	rv = mpt_issue_raid_req(mpt, mpt_vol, NULL, req,
	    MPI_RAID_ACTION_INDICATOR_STRUCT, 0, 0, 0, FALSE, TRUE);
	if (rv == ETIMEDOUT) {
		mpt_vol_prt(mpt, mpt_vol,
		    "mpt_refresh_raid_vol: Progress Indicator fetch timeout\n");
		mpt_free_request(mpt, req);
		return;
	}

	ar = REQ_TO_RAID_ACTION_RESULT(req);
	if (rv == 0
	 && ar->action_status == MPI_RAID_ACTION_ASTATUS_SUCCESS
	 && REQ_IOCSTATUS(req) == MPI_IOCSTATUS_SUCCESS) {
		memcpy(&mpt_vol->sync_progress,
		       &ar->action_data.indicator_struct,
		       sizeof(mpt_vol->sync_progress));
		mpt2host_mpi_raid_vol_indicator(&mpt_vol->sync_progress);
	} else {
		mpt_vol_prt(mpt, mpt_vol,
		    "mpt_refresh_raid_vol: Progress indicator fetch failed!\n");
	}
	mpt_free_request(mpt, req);
}

/*
 * Update in-core information about RAID support.  We update any entries
 * that didn't previously exists or have been marked as needing to
 * be updated by our event handler.  Interesting changes are displayed
 * to the console.
 */
static int
mpt_refresh_raid_data(struct mpt_softc *mpt)
{
	CONFIG_PAGE_IOC_2_RAID_VOL *ioc_vol;
	CONFIG_PAGE_IOC_2_RAID_VOL *ioc_last_vol;
	IOC_3_PHYS_DISK *ioc_disk;
	IOC_3_PHYS_DISK *ioc_last_disk;
	CONFIG_PAGE_RAID_VOL_0	*vol_pg;
	size_t len;
	int rv;
	int i;
	u_int nonopt_volumes;

	if (mpt->ioc_page2 == NULL || mpt->ioc_page3 == NULL) {
		return (0);
	}

	/*
	 * Mark all items as unreferenced by the configuration.
	 * This allows us to find, report, and discard stale
	 * entries.
	 */
	for (i = 0; i < mpt->ioc_page2->MaxPhysDisks; i++) {
		mpt->raid_disks[i].flags &= ~MPT_RDF_REFERENCED;
	}
	for (i = 0; i < mpt->ioc_page2->MaxVolumes; i++) {
		mpt->raid_volumes[i].flags &= ~MPT_RVF_REFERENCED;
	}

	/*
	 * Get Physical Disk information.
	 */
	len = mpt->ioc_page3->Header.PageLength * sizeof(uint32_t);
	rv = mpt_read_cur_cfg_page(mpt, /*PageAddress*/0,
				   &mpt->ioc_page3->Header, len,
				   /*sleep_ok*/TRUE, /*timeout_ms*/5000);
	if (rv) {
		mpt_prt(mpt,
		    "mpt_refresh_raid_data: Failed to read IOC Page 3\n");
		return (-1);
	}
	mpt2host_config_page_ioc3(mpt->ioc_page3);

	ioc_disk = mpt->ioc_page3->PhysDisk;
	ioc_last_disk = ioc_disk + mpt->ioc_page3->NumPhysDisks;
	for (; ioc_disk != ioc_last_disk; ioc_disk++) {
		struct mpt_raid_disk *mpt_disk;

		mpt_disk = mpt->raid_disks + ioc_disk->PhysDiskNum;
		mpt_disk->flags |= MPT_RDF_REFERENCED;
		if ((mpt_disk->flags & (MPT_RDF_ACTIVE|MPT_RDF_UP2DATE))
		 != (MPT_RDF_ACTIVE|MPT_RDF_UP2DATE)) {

			mpt_refresh_raid_disk(mpt, mpt_disk, ioc_disk);

		}
		mpt_disk->flags |= MPT_RDF_ACTIVE;
		mpt->raid_rescan++;
	}

	/*
	 * Refresh volume data.
	 */
	len = mpt->ioc_page2->Header.PageLength * sizeof(uint32_t);
	rv = mpt_read_cur_cfg_page(mpt, /*PageAddress*/0,
				   &mpt->ioc_page2->Header, len,
				   /*sleep_ok*/TRUE, /*timeout_ms*/5000);
	if (rv) {
		mpt_prt(mpt, "mpt_refresh_raid_data: "
			"Failed to read IOC Page 2\n");
		return (-1);
	}
	mpt2host_config_page_ioc2(mpt->ioc_page2);

	ioc_vol = mpt->ioc_page2->RaidVolume;
	ioc_last_vol = ioc_vol + mpt->ioc_page2->NumActiveVolumes;
	for (;ioc_vol != ioc_last_vol; ioc_vol++) {
		struct mpt_raid_volume *mpt_vol;

		mpt_vol = mpt->raid_volumes + ioc_vol->VolumePageNumber;
		mpt_vol->flags |= MPT_RVF_REFERENCED;
		vol_pg = mpt_vol->config_page;
		if (vol_pg == NULL)
			continue;
		if (((mpt_vol->flags & (MPT_RVF_ACTIVE|MPT_RVF_UP2DATE))
		  != (MPT_RVF_ACTIVE|MPT_RVF_UP2DATE))
		 || (vol_pg->VolumeStatus.Flags
		   & MPI_RAIDVOL0_STATUS_FLAG_RESYNC_IN_PROGRESS) != 0) {

			mpt_refresh_raid_vol(mpt, mpt_vol, ioc_vol);
		}
		mpt_vol->flags |= MPT_RVF_ACTIVE;
	}

	nonopt_volumes = 0;
	for (i = 0; i < mpt->ioc_page2->MaxVolumes; i++) {
		struct mpt_raid_volume *mpt_vol;
		uint64_t total;
		uint64_t left;
		int m;
		u_int prio;

		mpt_vol = &mpt->raid_volumes[i];

		if ((mpt_vol->flags & MPT_RVF_ACTIVE) == 0) {
			continue;
		}

		vol_pg = mpt_vol->config_page;
		if ((mpt_vol->flags & (MPT_RVF_REFERENCED|MPT_RVF_ANNOUNCED))
		 == MPT_RVF_ANNOUNCED) {
			mpt_vol_prt(mpt, mpt_vol, "No longer configured\n");
			mpt_vol->flags = 0;
			continue;
		}

		if ((mpt_vol->flags & MPT_RVF_ANNOUNCED) == 0) {
			mpt_announce_vol(mpt, mpt_vol);
			mpt_vol->flags |= MPT_RVF_ANNOUNCED;
		}

		if (vol_pg->VolumeStatus.State !=
		    MPI_RAIDVOL0_STATUS_STATE_OPTIMAL)
			nonopt_volumes++;

		if ((mpt_vol->flags & MPT_RVF_UP2DATE) != 0)
			continue;

		mpt_vol->flags |= MPT_RVF_UP2DATE;
		mpt_vol_prt(mpt, mpt_vol, "%s - %s\n",
		    mpt_vol_type(mpt_vol), mpt_vol_state(mpt_vol));
		mpt_verify_mwce(mpt, mpt_vol);

		if (vol_pg->VolumeStatus.Flags == 0) {
			continue;
		}

		mpt_vol_prt(mpt, mpt_vol, "Status (");
		for (m = 1; m <= 0x80; m <<= 1) {
			switch (vol_pg->VolumeStatus.Flags & m) {
			case MPI_RAIDVOL0_STATUS_FLAG_ENABLED:
				mpt_prtc(mpt, " Enabled");
				break;
			case MPI_RAIDVOL0_STATUS_FLAG_QUIESCED:
				mpt_prtc(mpt, " Quiesced");
				break;
			case MPI_RAIDVOL0_STATUS_FLAG_RESYNC_IN_PROGRESS:
				mpt_prtc(mpt, " Re-Syncing");
				break;
			case MPI_RAIDVOL0_STATUS_FLAG_VOLUME_INACTIVE:
				mpt_prtc(mpt, " Inactive");
				break;
			default:
				break;
			}
		}
		mpt_prtc(mpt, " )\n");

		if ((vol_pg->VolumeStatus.Flags
		   & MPI_RAIDVOL0_STATUS_FLAG_RESYNC_IN_PROGRESS) == 0)
			continue;

		mpt_verify_resync_rate(mpt, mpt_vol);

		left = MPT_U64_2_SCALAR(mpt_vol->sync_progress.BlocksRemaining);
		total = MPT_U64_2_SCALAR(mpt_vol->sync_progress.TotalBlocks);
		if (vol_pg->ResyncRate != 0) {

			prio = ((u_int)vol_pg->ResyncRate * 100000) / 0xFF;
			mpt_vol_prt(mpt, mpt_vol, "Rate %d.%d%%\n",
			    prio / 1000, prio % 1000);
		} else {
			prio = vol_pg->VolumeSettings.Settings
			     & MPI_RAIDVOL0_SETTING_PRIORITY_RESYNC;
			mpt_vol_prt(mpt, mpt_vol, "%s Priority Re-Sync\n",
			    prio ? "High" : "Low");
		}
		mpt_vol_prt(mpt, mpt_vol, "%ju of %ju "
			    "blocks remaining\n", (uintmax_t)left,
			    (uintmax_t)total);

		/* Periodically report on sync progress. */
		mpt_schedule_raid_refresh(mpt);
	}

	for (i = 0; i < mpt->ioc_page2->MaxPhysDisks; i++) {
		struct mpt_raid_disk *mpt_disk;
		CONFIG_PAGE_RAID_PHYS_DISK_0 *disk_pg;
		int m;

		mpt_disk = &mpt->raid_disks[i];
		disk_pg = &mpt_disk->config_page;

		if ((mpt_disk->flags & MPT_RDF_ACTIVE) == 0)
			continue;

		if ((mpt_disk->flags & (MPT_RDF_REFERENCED|MPT_RDF_ANNOUNCED))
		 == MPT_RDF_ANNOUNCED) {
			mpt_disk_prt(mpt, mpt_disk, "No longer configured\n");
			mpt_disk->flags = 0;
			mpt->raid_rescan++;
			continue;
		}

		if ((mpt_disk->flags & MPT_RDF_ANNOUNCED) == 0) {

			mpt_announce_disk(mpt, mpt_disk);
			mpt_disk->flags |= MPT_RVF_ANNOUNCED;
		}

		if ((mpt_disk->flags & MPT_RDF_UP2DATE) != 0)
			continue;

		mpt_disk->flags |= MPT_RDF_UP2DATE;
		mpt_disk_prt(mpt, mpt_disk, "%s\n", mpt_disk_state(mpt_disk));
		if (disk_pg->PhysDiskStatus.Flags == 0)
			continue;

		mpt_disk_prt(mpt, mpt_disk, "Status (");
		for (m = 1; m <= 0x80; m <<= 1) {
			switch (disk_pg->PhysDiskStatus.Flags & m) {
			case MPI_PHYSDISK0_STATUS_FLAG_OUT_OF_SYNC:
				mpt_prtc(mpt, " Out-Of-Sync");
				break;
			case MPI_PHYSDISK0_STATUS_FLAG_QUIESCED:
				mpt_prtc(mpt, " Quiesced");
				break;
			default:
				break;
			}
		}
		mpt_prtc(mpt, " )\n");
	}

	mpt->raid_nonopt_volumes = nonopt_volumes;
	return (0);
}

static void
mpt_raid_timer(void *arg)
{
	struct mpt_softc *mpt;

	mpt = (struct mpt_softc *)arg;
	MPT_LOCK_ASSERT(mpt);
	mpt_raid_wakeup(mpt);
}

static void
mpt_schedule_raid_refresh(struct mpt_softc *mpt)
{

	callout_reset(&mpt->raid_timer, MPT_RAID_SYNC_REPORT_INTERVAL,
		      mpt_raid_timer, mpt);
}

void
mpt_raid_free_mem(struct mpt_softc *mpt)
{

	if (mpt->raid_volumes) {
		struct mpt_raid_volume *mpt_raid;
		int i;
		for (i = 0; i < mpt->raid_max_volumes; i++) {
			mpt_raid = &mpt->raid_volumes[i];
			if (mpt_raid->config_page) {
				free(mpt_raid->config_page, M_DEVBUF);
				mpt_raid->config_page = NULL;
			}
		}
		free(mpt->raid_volumes, M_DEVBUF);
		mpt->raid_volumes = NULL;
	}
	if (mpt->raid_disks) {
		free(mpt->raid_disks, M_DEVBUF);
		mpt->raid_disks = NULL;
	}
	if (mpt->ioc_page2) {
		free(mpt->ioc_page2, M_DEVBUF);
		mpt->ioc_page2 = NULL;
	}
	if (mpt->ioc_page3) {
		free(mpt->ioc_page3, M_DEVBUF);
		mpt->ioc_page3 = NULL;
	}
	mpt->raid_max_volumes =  0;
	mpt->raid_max_disks =  0;
}

static int
mpt_raid_set_vol_resync_rate(struct mpt_softc *mpt, u_int rate)
{
	struct mpt_raid_volume *mpt_vol;

	if ((rate > MPT_RAID_RESYNC_RATE_MAX
	  || rate < MPT_RAID_RESYNC_RATE_MIN)
	 && rate != MPT_RAID_RESYNC_RATE_NC)
		return (EINVAL);

	MPT_LOCK(mpt);
	mpt->raid_resync_rate = rate;
	RAID_VOL_FOREACH(mpt, mpt_vol) {
		if ((mpt_vol->flags & MPT_RVF_ACTIVE) == 0) {
			continue;
		}
		mpt_verify_resync_rate(mpt, mpt_vol);
	}
	MPT_UNLOCK(mpt);
	return (0);
}

static int
mpt_raid_set_vol_queue_depth(struct mpt_softc *mpt, u_int vol_queue_depth)
{
	struct mpt_raid_volume *mpt_vol;

	if (vol_queue_depth > 255 || vol_queue_depth < 1)
		return (EINVAL);

	MPT_LOCK(mpt);
	mpt->raid_queue_depth = vol_queue_depth;
	RAID_VOL_FOREACH(mpt, mpt_vol) {
		struct cam_path *path;
		int error;

		if ((mpt_vol->flags & MPT_RVF_ACTIVE) == 0)
			continue;

		mpt->raid_rescan = 0;

		error = xpt_create_path(&path, NULL,
					cam_sim_path(mpt->sim),
					mpt_vol->config_page->VolumeID,
					/*lun*/0);
		if (error != CAM_REQ_CMP) {
			mpt_vol_prt(mpt, mpt_vol, "Unable to allocate path!\n");
			continue;
		}
		mpt_adjust_queue_depth(mpt, mpt_vol, path);
		xpt_free_path(path);
	}
	MPT_UNLOCK(mpt);
	return (0);
}

static int
mpt_raid_set_vol_mwce(struct mpt_softc *mpt, mpt_raid_mwce_t mwce)
{
	struct mpt_raid_volume *mpt_vol;
	int force_full_resync;

	MPT_LOCK(mpt);
	if (mwce == mpt->raid_mwce_setting) {
		MPT_UNLOCK(mpt);
		return (0);
	}

	/*
	 * Catch MWCE being left on due to a failed shutdown.  Since
	 * sysctls cannot be set by the loader, we treat the first
	 * setting of this varible specially and force a full volume
	 * resync if MWCE is enabled and a resync is in progress.
	 */
	force_full_resync = 0;
	if (mpt->raid_mwce_set == 0
	 && mpt->raid_mwce_setting == MPT_RAID_MWCE_NC
	 && mwce == MPT_RAID_MWCE_REBUILD_ONLY)
		force_full_resync = 1;

	mpt->raid_mwce_setting = mwce;
	RAID_VOL_FOREACH(mpt, mpt_vol) {
		CONFIG_PAGE_RAID_VOL_0 *vol_pg;
		int resyncing;
		int mwce;

		if ((mpt_vol->flags & MPT_RVF_ACTIVE) == 0)
			continue;

		vol_pg = mpt_vol->config_page;
		resyncing = vol_pg->VolumeStatus.Flags
			  & MPI_RAIDVOL0_STATUS_FLAG_RESYNC_IN_PROGRESS;
		mwce = vol_pg->VolumeSettings.Settings
		     & MPI_RAIDVOL0_SETTING_WRITE_CACHING_ENABLE;
		if (force_full_resync && resyncing && mwce) {

			/*
			 * XXX disable/enable volume should force a resync,
			 *     but we'll need to queice, drain, and restart
			 *     I/O to do that.
			 */
			mpt_vol_prt(mpt, mpt_vol, "WARNING - Unsafe shutdown "
				    "detected.  Suggest full resync.\n");
		}
		mpt_verify_mwce(mpt, mpt_vol);
	}
	mpt->raid_mwce_set = 1;
	MPT_UNLOCK(mpt);
	return (0);
}

static const char *mpt_vol_mwce_strs[] =
{
	"On",
	"Off",
	"On-During-Rebuild",
	"NC"
};

static int
mpt_raid_sysctl_vol_member_wce(SYSCTL_HANDLER_ARGS)
{
	char inbuf[20];
	struct mpt_softc *mpt;
	const char *str;
	int error;
	u_int size;
	u_int i;

	GIANT_REQUIRED;

	mpt = (struct mpt_softc *)arg1;
	str = mpt_vol_mwce_strs[mpt->raid_mwce_setting];
	error = SYSCTL_OUT(req, str, strlen(str) + 1);
	if (error || !req->newptr) {
		return (error);
	}

	size = req->newlen - req->newidx;
	if (size >= sizeof(inbuf)) {
		return (EINVAL);
	}

	error = SYSCTL_IN(req, inbuf, size);
	if (error) {
		return (error);
	}
	inbuf[size] = '\0'; 
	for (i = 0; i < NUM_ELEMENTS(mpt_vol_mwce_strs); i++) {
		if (strcmp(mpt_vol_mwce_strs[i], inbuf) == 0) {
			return (mpt_raid_set_vol_mwce(mpt, i));
		}
	}
	return (EINVAL);
}

static int
mpt_raid_sysctl_vol_resync_rate(SYSCTL_HANDLER_ARGS)
{
	struct mpt_softc *mpt;
	u_int raid_resync_rate;
	int error;

	GIANT_REQUIRED;

	mpt = (struct mpt_softc *)arg1;
	raid_resync_rate = mpt->raid_resync_rate;

	error = sysctl_handle_int(oidp, &raid_resync_rate, 0, req);
	if (error || !req->newptr) {
		return error;
	}

	return (mpt_raid_set_vol_resync_rate(mpt, raid_resync_rate));
}

static int
mpt_raid_sysctl_vol_queue_depth(SYSCTL_HANDLER_ARGS)
{
	struct mpt_softc *mpt;
	u_int raid_queue_depth;
	int error;

	GIANT_REQUIRED;

	mpt = (struct mpt_softc *)arg1;
	raid_queue_depth = mpt->raid_queue_depth;

	error = sysctl_handle_int(oidp, &raid_queue_depth, 0, req);
	if (error || !req->newptr) {
		return error;
	}

	return (mpt_raid_set_vol_queue_depth(mpt, raid_queue_depth));
}

static void
mpt_raid_sysctl_attach(struct mpt_softc *mpt)
{
	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(mpt->dev);
	struct sysctl_oid *tree = device_get_sysctl_tree(mpt->dev);

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
			"vol_member_wce", CTLTYPE_STRING | CTLFLAG_RW, mpt, 0,
			mpt_raid_sysctl_vol_member_wce, "A",
			"volume member WCE(On,Off,On-During-Rebuild,NC)");

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
			"vol_queue_depth", CTLTYPE_INT | CTLFLAG_RW, mpt, 0,
			mpt_raid_sysctl_vol_queue_depth, "I",
			"default volume queue depth");

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
			"vol_resync_rate", CTLTYPE_INT | CTLFLAG_RW, mpt, 0,
			mpt_raid_sysctl_vol_resync_rate, "I",
			"volume resync priority (0 == NC, 1 - 255)");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
			"nonoptimal_volumes", CTLFLAG_RD,
			&mpt->raid_nonopt_volumes, 0,
			"number of nonoptimal volumes");
}
