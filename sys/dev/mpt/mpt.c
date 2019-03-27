/*-
 * Generic routines for LSI Fusion adapters.
 * FreeBSD Version.
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD AND BSD-3-Clause
 *
 * Copyright (c) 2000, 2001 by Greg Ansley
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*-
 * Copyright (c) 2002, 2006 by Matthew Jacob
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
 *
 * Support from Chris Ellsworth in order to make SAS adapters work
 * is gratefully acknowledged.
 *
 *
 * Support from LSI-Logic has also gone a great deal toward making this a
 * workable subsystem and is gratefully acknowledged.
 */
/*-
 * Copyright (c) 2004, Avid Technology, Inc. and its contributors.
 * Copyright (c) 2005, WHEEL Sp. z o.o.
 * Copyright (c) 2004, 2005 Justin T. Gibbs
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <dev/mpt/mpt.h>
#include <dev/mpt/mpt_cam.h> /* XXX For static handler registration */
#include <dev/mpt/mpt_raid.h> /* XXX For static handler registration */

#include <dev/mpt/mpilib/mpi.h>
#include <dev/mpt/mpilib/mpi_ioc.h>
#include <dev/mpt/mpilib/mpi_fc.h>
#include <dev/mpt/mpilib/mpi_targ.h>

#include <sys/sysctl.h>

#define MPT_MAX_TRYS 3
#define MPT_MAX_WAIT 300000

static int maxwait_ack = 0;
static int maxwait_int = 0;
static int maxwait_state = 0;

static TAILQ_HEAD(, mpt_softc)	mpt_tailq = TAILQ_HEAD_INITIALIZER(mpt_tailq);
mpt_reply_handler_t *mpt_reply_handlers[MPT_NUM_REPLY_HANDLERS];

static mpt_reply_handler_t mpt_default_reply_handler;
static mpt_reply_handler_t mpt_config_reply_handler;
static mpt_reply_handler_t mpt_handshake_reply_handler;
static mpt_reply_handler_t mpt_event_reply_handler;
static void mpt_send_event_ack(struct mpt_softc *mpt, request_t *ack_req,
			       MSG_EVENT_NOTIFY_REPLY *msg, uint32_t context);
static int mpt_send_event_request(struct mpt_softc *mpt, int onoff);
static int mpt_soft_reset(struct mpt_softc *mpt);
static void mpt_hard_reset(struct mpt_softc *mpt);
static int mpt_dma_buf_alloc(struct mpt_softc *mpt);
static void mpt_dma_buf_free(struct mpt_softc *mpt);
static int mpt_configure_ioc(struct mpt_softc *mpt, int, int);
static int mpt_enable_ioc(struct mpt_softc *mpt, int);

/************************* Personality Module Support *************************/
/*
 * We include one extra entry that is guaranteed to be NULL
 * to simplify our itterator.
 */
static struct mpt_personality *mpt_personalities[MPT_MAX_PERSONALITIES + 1];
static __inline struct mpt_personality*
	mpt_pers_find(struct mpt_softc *, u_int);
static __inline struct mpt_personality*
	mpt_pers_find_reverse(struct mpt_softc *, u_int);

static __inline struct mpt_personality *
mpt_pers_find(struct mpt_softc *mpt, u_int start_at)
{
	KASSERT(start_at <= MPT_MAX_PERSONALITIES,
		("mpt_pers_find: starting position out of range"));

	while (start_at < MPT_MAX_PERSONALITIES
	    && (mpt->mpt_pers_mask & (0x1 << start_at)) == 0) {
		start_at++;
	}
	return (mpt_personalities[start_at]);
}

/*
 * Used infrequently, so no need to optimize like a forward
 * traversal where we use the MAX+1 is guaranteed to be NULL
 * trick.
 */
static __inline struct mpt_personality *
mpt_pers_find_reverse(struct mpt_softc *mpt, u_int start_at)
{
	while (start_at < MPT_MAX_PERSONALITIES
	    && (mpt->mpt_pers_mask & (0x1 << start_at)) == 0) {
		start_at--;
	}
	if (start_at < MPT_MAX_PERSONALITIES)
		return (mpt_personalities[start_at]);
	return (NULL);
}

#define MPT_PERS_FOREACH(mpt, pers)				\
	for (pers = mpt_pers_find(mpt, /*start_at*/0);		\
	     pers != NULL;					\
	     pers = mpt_pers_find(mpt, /*start_at*/pers->id+1))

#define MPT_PERS_FOREACH_REVERSE(mpt, pers)				\
	for (pers = mpt_pers_find_reverse(mpt, MPT_MAX_PERSONALITIES-1);\
	     pers != NULL;						\
	     pers = mpt_pers_find_reverse(mpt, /*start_at*/pers->id-1))

static mpt_load_handler_t      mpt_stdload;
static mpt_probe_handler_t     mpt_stdprobe;
static mpt_attach_handler_t    mpt_stdattach;
static mpt_enable_handler_t    mpt_stdenable;
static mpt_ready_handler_t     mpt_stdready;
static mpt_event_handler_t     mpt_stdevent;
static mpt_reset_handler_t     mpt_stdreset;
static mpt_shutdown_handler_t  mpt_stdshutdown;
static mpt_detach_handler_t    mpt_stddetach;
static mpt_unload_handler_t    mpt_stdunload;
static struct mpt_personality mpt_default_personality =
{
	.load		= mpt_stdload,
	.probe		= mpt_stdprobe,
	.attach		= mpt_stdattach,
	.enable		= mpt_stdenable,
	.ready		= mpt_stdready,
	.event		= mpt_stdevent,
	.reset		= mpt_stdreset,
	.shutdown	= mpt_stdshutdown,
	.detach		= mpt_stddetach,
	.unload		= mpt_stdunload
};

static mpt_load_handler_t      mpt_core_load;
static mpt_attach_handler_t    mpt_core_attach;
static mpt_enable_handler_t    mpt_core_enable;
static mpt_reset_handler_t     mpt_core_ioc_reset;
static mpt_event_handler_t     mpt_core_event;
static mpt_shutdown_handler_t  mpt_core_shutdown;
static mpt_shutdown_handler_t  mpt_core_detach;
static mpt_unload_handler_t    mpt_core_unload;
static struct mpt_personality mpt_core_personality =
{
	.name		= "mpt_core",
	.load		= mpt_core_load,
//	.attach		= mpt_core_attach,
//	.enable		= mpt_core_enable,
	.event		= mpt_core_event,
	.reset		= mpt_core_ioc_reset,
	.shutdown	= mpt_core_shutdown,
	.detach		= mpt_core_detach,
	.unload		= mpt_core_unload,
};

/*
 * Manual declaration so that DECLARE_MPT_PERSONALITY doesn't need
 * ordering information.  We want the core to always register FIRST.
 * other modules are set to SI_ORDER_SECOND.
 */
static moduledata_t mpt_core_mod = {
	"mpt_core", mpt_modevent, &mpt_core_personality
};
DECLARE_MODULE(mpt_core, mpt_core_mod, SI_SUB_DRIVERS, SI_ORDER_FIRST);
MODULE_VERSION(mpt_core, 1);

#define MPT_PERS_ATTACHED(pers, mpt) ((mpt)->mpt_pers_mask & (0x1 << pers->id))

int
mpt_modevent(module_t mod, int type, void *data)
{
	struct mpt_personality *pers;
	int error;

	pers = (struct mpt_personality *)data;

	error = 0;
	switch (type) {
	case MOD_LOAD:
	{
		mpt_load_handler_t **def_handler;
		mpt_load_handler_t **pers_handler;
		int i;

		for (i = 0; i < MPT_MAX_PERSONALITIES; i++) {
			if (mpt_personalities[i] == NULL)
				break;
		}
		if (i >= MPT_MAX_PERSONALITIES) {
			error = ENOMEM;
			break;
		}
		pers->id = i;
		mpt_personalities[i] = pers;

		/* Install standard/noop handlers for any NULL entries. */
		def_handler = MPT_PERS_FIRST_HANDLER(&mpt_default_personality);
		pers_handler = MPT_PERS_FIRST_HANDLER(pers);
		while (pers_handler <= MPT_PERS_LAST_HANDLER(pers)) {
			if (*pers_handler == NULL)
				*pers_handler = *def_handler;
			pers_handler++;
			def_handler++;
		}
		
		error = (pers->load(pers));
		if (error != 0)
			mpt_personalities[i] = NULL;
		break;
	}
	case MOD_SHUTDOWN:
		break;
	case MOD_QUIESCE:
		break;
	case MOD_UNLOAD:
		error = pers->unload(pers);
		mpt_personalities[pers->id] = NULL;
		break;
	default:
		error = EINVAL;
		break;
	}
	return (error);
}

static int
mpt_stdload(struct mpt_personality *pers)
{

	/* Load is always successful. */
	return (0);
}

static int
mpt_stdprobe(struct mpt_softc *mpt)
{

	/* Probe is always successful. */
	return (0);
}

static int
mpt_stdattach(struct mpt_softc *mpt)
{

	/* Attach is always successful. */
	return (0);
}

static int
mpt_stdenable(struct mpt_softc *mpt)
{

	/* Enable is always successful. */
	return (0);
}

static void
mpt_stdready(struct mpt_softc *mpt)
{

}

static int
mpt_stdevent(struct mpt_softc *mpt, request_t *req, MSG_EVENT_NOTIFY_REPLY *msg)
{

	mpt_lprt(mpt, MPT_PRT_DEBUG, "mpt_stdevent: 0x%x\n", msg->Event & 0xFF);
	/* Event was not for us. */
	return (0);
}

static void
mpt_stdreset(struct mpt_softc *mpt, int type)
{

}

static void
mpt_stdshutdown(struct mpt_softc *mpt)
{

}

static void
mpt_stddetach(struct mpt_softc *mpt)
{

}

static int
mpt_stdunload(struct mpt_personality *pers)
{

	/* Unload is always successful. */
	return (0);
}

/*
 * Post driver attachment, we may want to perform some global actions.
 * Here is the hook to do so.
 */

static void
mpt_postattach(void *unused)
{
	struct mpt_softc *mpt;
	struct mpt_personality *pers;

	TAILQ_FOREACH(mpt, &mpt_tailq, links) {
		MPT_PERS_FOREACH(mpt, pers)
			pers->ready(mpt);
	}
}
SYSINIT(mptdev, SI_SUB_CONFIGURE, SI_ORDER_MIDDLE, mpt_postattach, NULL);

/******************************* Bus DMA Support ******************************/
void
mpt_map_rquest(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	struct mpt_map_info *map_info;

	map_info = (struct mpt_map_info *)arg;
	map_info->error = error;
	map_info->phys = segs->ds_addr;
}

/**************************** Reply/Event Handling ****************************/
int
mpt_register_handler(struct mpt_softc *mpt, mpt_handler_type type,
		     mpt_handler_t handler, uint32_t *phandler_id)
{

	switch (type) {
	case MPT_HANDLER_REPLY:
	{
		u_int cbi;
		u_int free_cbi;

		if (phandler_id == NULL)
			return (EINVAL);

		free_cbi = MPT_HANDLER_ID_NONE;
		for (cbi = 0; cbi < MPT_NUM_REPLY_HANDLERS; cbi++) {
			/*
			 * If the same handler is registered multiple
			 * times, don't error out.  Just return the
			 * index of the original registration.
			 */
			if (mpt_reply_handlers[cbi] == handler.reply_handler) {
				*phandler_id = MPT_CBI_TO_HID(cbi);
				return (0);
			}

			/*
			 * Fill from the front in the hope that
			 * all registered handlers consume only a
			 * single cache line.
			 *
			 * We don't break on the first empty slot so
			 * that the full table is checked to see if
			 * this handler was previously registered.
			 */
			if (free_cbi == MPT_HANDLER_ID_NONE &&
			    (mpt_reply_handlers[cbi]
			  == mpt_default_reply_handler))
				free_cbi = cbi;
		}
		if (free_cbi == MPT_HANDLER_ID_NONE) {
			return (ENOMEM);
		}
		mpt_reply_handlers[free_cbi] = handler.reply_handler;
		*phandler_id = MPT_CBI_TO_HID(free_cbi);
		break;
	}
	default:
		mpt_prt(mpt, "mpt_register_handler unknown type %d\n", type);
		return (EINVAL);
	}
	return (0);
}

int
mpt_deregister_handler(struct mpt_softc *mpt, mpt_handler_type type,
		       mpt_handler_t handler, uint32_t handler_id)
{

	switch (type) {
	case MPT_HANDLER_REPLY:
	{
		u_int cbi;

		cbi = MPT_CBI(handler_id);
		if (cbi >= MPT_NUM_REPLY_HANDLERS
		 || mpt_reply_handlers[cbi] != handler.reply_handler)
			return (ENOENT);
		mpt_reply_handlers[cbi] = mpt_default_reply_handler;
		break;
	}
	default:
		mpt_prt(mpt, "mpt_deregister_handler unknown type %d\n", type);
		return (EINVAL);
	}
	return (0);
}

static int
mpt_default_reply_handler(struct mpt_softc *mpt, request_t *req,
	uint32_t reply_desc, MSG_DEFAULT_REPLY *reply_frame)
{

	mpt_prt(mpt,
	    "Default Handler Called: req=%p:%u reply_descriptor=%x frame=%p\n",
	    req, req->serno, reply_desc, reply_frame);

	if (reply_frame != NULL)
		mpt_dump_reply_frame(mpt, reply_frame);

	mpt_prt(mpt, "Reply Frame Ignored\n");

	return (/*free_reply*/TRUE);
}

static int
mpt_config_reply_handler(struct mpt_softc *mpt, request_t *req,
 uint32_t reply_desc, MSG_DEFAULT_REPLY *reply_frame)
{

	if (req != NULL) {
		if (reply_frame != NULL) {
			MSG_CONFIG *cfgp;
			MSG_CONFIG_REPLY *reply;

			cfgp = (MSG_CONFIG *)req->req_vbuf;
			reply = (MSG_CONFIG_REPLY *)reply_frame;
			req->IOCStatus = le16toh(reply_frame->IOCStatus);
			bcopy(&reply->Header, &cfgp->Header,
			      sizeof(cfgp->Header));
			cfgp->ExtPageLength = reply->ExtPageLength;
			cfgp->ExtPageType = reply->ExtPageType;
		}
		req->state &= ~REQ_STATE_QUEUED;
		req->state |= REQ_STATE_DONE;
		TAILQ_REMOVE(&mpt->request_pending_list, req, links);
		if ((req->state & REQ_STATE_NEED_WAKEUP) != 0) {
			wakeup(req);
		} else if ((req->state & REQ_STATE_TIMEDOUT) != 0) {
			/*
			 * Whew- we can free this request (late completion)
			 */
			mpt_free_request(mpt, req);
		}
	}

	return (TRUE);
}

static int
mpt_handshake_reply_handler(struct mpt_softc *mpt, request_t *req,
 uint32_t reply_desc, MSG_DEFAULT_REPLY *reply_frame)
{

	/* Nothing to be done. */
	return (TRUE);
}

static int
mpt_event_reply_handler(struct mpt_softc *mpt, request_t *req,
    uint32_t reply_desc, MSG_DEFAULT_REPLY *reply_frame)
{
	int free_reply;

	KASSERT(reply_frame != NULL, ("null reply in mpt_event_reply_handler"));
	KASSERT(req != NULL, ("null request in mpt_event_reply_handler"));

	free_reply = TRUE;
	switch (reply_frame->Function) {
	case MPI_FUNCTION_EVENT_NOTIFICATION:
	{
		MSG_EVENT_NOTIFY_REPLY *msg;
		struct mpt_personality *pers;
		u_int handled;

		handled = 0;
		msg = (MSG_EVENT_NOTIFY_REPLY *)reply_frame;
		msg->EventDataLength = le16toh(msg->EventDataLength);
		msg->IOCStatus = le16toh(msg->IOCStatus);
		msg->IOCLogInfo = le32toh(msg->IOCLogInfo);
		msg->Event = le32toh(msg->Event);
		MPT_PERS_FOREACH(mpt, pers)
			handled += pers->event(mpt, req, msg);

		if (handled == 0 && mpt->mpt_pers_mask == 0) {
			mpt_lprt(mpt, MPT_PRT_INFO,
				"No Handlers For Any Event Notify Frames. "
				"Event %#x (ACK %sequired).\n",
				msg->Event, msg->AckRequired? "r" : "not r");
		} else if (handled == 0) {
			mpt_lprt(mpt,
				msg->AckRequired? MPT_PRT_WARN : MPT_PRT_INFO,
				"Unhandled Event Notify Frame. Event %#x "
				"(ACK %sequired).\n",
				msg->Event, msg->AckRequired? "r" : "not r");
		}

		if (msg->AckRequired) {
			request_t *ack_req;
			uint32_t context;

			context = req->index | MPT_REPLY_HANDLER_EVENTS;
			ack_req = mpt_get_request(mpt, FALSE);
			if (ack_req == NULL) {
				struct mpt_evtf_record *evtf;

				evtf = (struct mpt_evtf_record *)reply_frame;
				evtf->context = context;
				LIST_INSERT_HEAD(&mpt->ack_frames, evtf, links);
				free_reply = FALSE;
				break;
			}
			mpt_send_event_ack(mpt, ack_req, msg, context);
			/*
			 * Don't check for CONTINUATION_REPLY here
			 */
			return (free_reply);
		}
		break;
	}
	case MPI_FUNCTION_PORT_ENABLE:
		mpt_lprt(mpt, MPT_PRT_DEBUG , "enable port reply\n");
		break;
	case MPI_FUNCTION_EVENT_ACK:
		break;
	default:
		mpt_prt(mpt, "unknown event function: %x\n",
			reply_frame->Function);
		break;
	}

	/*
	 * I'm not sure that this continuation stuff works as it should.
	 *
	 * I've had FC async events occur that free the frame up because
	 * the continuation bit isn't set, and then additional async events
	 * then occur using the same context. As you might imagine, this
	 * leads to Very Bad Thing.
	 *
	 *  Let's just be safe for now and not free them up until we figure
	 * out what's actually happening here.
	 */
#if	0
	if ((reply_frame->MsgFlags & MPI_MSGFLAGS_CONTINUATION_REPLY) == 0) {
		TAILQ_REMOVE(&mpt->request_pending_list, req, links);
		mpt_free_request(mpt, req);
		mpt_prt(mpt, "event_reply %x for req %p:%u NOT a continuation",
		    reply_frame->Function, req, req->serno);
		if (reply_frame->Function == MPI_FUNCTION_EVENT_NOTIFICATION) {
			MSG_EVENT_NOTIFY_REPLY *msg =
			    (MSG_EVENT_NOTIFY_REPLY *)reply_frame;
			mpt_prtc(mpt, " Event=0x%x AckReq=%d",
			    msg->Event, msg->AckRequired);
		}
	} else {
		mpt_prt(mpt, "event_reply %x for %p:%u IS a continuation",
		    reply_frame->Function, req, req->serno);
		if (reply_frame->Function == MPI_FUNCTION_EVENT_NOTIFICATION) {
			MSG_EVENT_NOTIFY_REPLY *msg =
			    (MSG_EVENT_NOTIFY_REPLY *)reply_frame;
			mpt_prtc(mpt, " Event=0x%x AckReq=%d",
			    msg->Event, msg->AckRequired);
		}
		mpt_prtc(mpt, "\n");
	}
#endif
	return (free_reply);
}

/*
 * Process an asynchronous event from the IOC.
 */
static int
mpt_core_event(struct mpt_softc *mpt, request_t *req,
	       MSG_EVENT_NOTIFY_REPLY *msg)
{

	mpt_lprt(mpt, MPT_PRT_DEBUG, "mpt_core_event: 0x%x\n",
                 msg->Event & 0xFF);
	switch(msg->Event & 0xFF) {
	case MPI_EVENT_NONE:
		break;
	case MPI_EVENT_LOG_DATA:
	{
		int i;

		/* Some error occurred that LSI wants logged */
		mpt_prt(mpt, "EvtLogData: IOCLogInfo: 0x%08x\n",
			msg->IOCLogInfo);
		mpt_prt(mpt, "\tEvtLogData: Event Data:");
		for (i = 0; i < msg->EventDataLength; i++)
			mpt_prtc(mpt, "  %08x", msg->Data[i]);
		mpt_prtc(mpt, "\n");
		break;
	}
	case MPI_EVENT_EVENT_CHANGE:
		/*
		 * This is just an acknowledgement
		 * of our mpt_send_event_request.
		 */
		break;
	case MPI_EVENT_SAS_DEVICE_STATUS_CHANGE:
		break;
	default:
		return (0);
		break;
	}
	return (1);
}

static void
mpt_send_event_ack(struct mpt_softc *mpt, request_t *ack_req,
		   MSG_EVENT_NOTIFY_REPLY *msg, uint32_t context)
{
	MSG_EVENT_ACK *ackp;

	ackp = (MSG_EVENT_ACK *)ack_req->req_vbuf;
	memset(ackp, 0, sizeof (*ackp));
	ackp->Function = MPI_FUNCTION_EVENT_ACK;
	ackp->Event = htole32(msg->Event);
	ackp->EventContext = htole32(msg->EventContext);
	ackp->MsgContext = htole32(context);
	mpt_check_doorbell(mpt);
	mpt_send_cmd(mpt, ack_req);
}

/***************************** Interrupt Handling *****************************/
void
mpt_intr(void *arg)
{
	struct mpt_softc *mpt;
	uint32_t reply_desc;
	int ntrips = 0;

	mpt = (struct mpt_softc *)arg;
	mpt_lprt(mpt, MPT_PRT_DEBUG2, "enter mpt_intr\n");
	MPT_LOCK_ASSERT(mpt);

	while ((reply_desc = mpt_pop_reply_queue(mpt)) != MPT_REPLY_EMPTY) {
		request_t	  *req;
		MSG_DEFAULT_REPLY *reply_frame;
		uint32_t	   reply_baddr;
		uint32_t           ctxt_idx;
		u_int		   cb_index;
		u_int		   req_index;
		u_int		   offset;
		int		   free_rf;

		req = NULL;
		reply_frame = NULL;
		reply_baddr = 0;
		offset = 0;
		if ((reply_desc & MPI_ADDRESS_REPLY_A_BIT) != 0) {
			/*
			 * Ensure that the reply frame is coherent.
			 */
			reply_baddr = MPT_REPLY_BADDR(reply_desc);
			offset = reply_baddr - (mpt->reply_phys & 0xFFFFFFFF);
			bus_dmamap_sync_range(mpt->reply_dmat,
			    mpt->reply_dmap, offset, MPT_REPLY_SIZE,
			    BUS_DMASYNC_POSTREAD);
			reply_frame = MPT_REPLY_OTOV(mpt, offset);
			ctxt_idx = le32toh(reply_frame->MsgContext);
		} else {
			uint32_t type;

			type = MPI_GET_CONTEXT_REPLY_TYPE(reply_desc);
			ctxt_idx = reply_desc;
			mpt_lprt(mpt, MPT_PRT_DEBUG1, "Context Reply: 0x%08x\n",
				    reply_desc);

			switch (type) {
			case MPI_CONTEXT_REPLY_TYPE_SCSI_INIT:
				ctxt_idx &= MPI_CONTEXT_REPLY_CONTEXT_MASK;
				break;
			case MPI_CONTEXT_REPLY_TYPE_SCSI_TARGET:
				ctxt_idx = GET_IO_INDEX(reply_desc);
				if (mpt->tgt_cmd_ptrs == NULL) {
					mpt_prt(mpt,
					    "mpt_intr: no target cmd ptrs\n");
					reply_desc = MPT_REPLY_EMPTY;
					break;
				}
				if (ctxt_idx >= mpt->tgt_cmds_allocated) {
					mpt_prt(mpt,
					    "mpt_intr: bad tgt cmd ctxt %u\n",
					    ctxt_idx);
					reply_desc = MPT_REPLY_EMPTY;
					ntrips = 1000;
					break;
				}
				req = mpt->tgt_cmd_ptrs[ctxt_idx];
				if (req == NULL) {
					mpt_prt(mpt, "no request backpointer "
					    "at index %u", ctxt_idx);
					reply_desc = MPT_REPLY_EMPTY;
					ntrips = 1000;
					break;
				}
				/*
				 * Reformulate ctxt_idx to be just as if
				 * it were another type of context reply
				 * so the code below will find the request
				 * via indexing into the pool.
				 */
				ctxt_idx =
				    req->index | mpt->scsi_tgt_handler_id;
				req = NULL;
				break;
			case MPI_CONTEXT_REPLY_TYPE_LAN:
				mpt_prt(mpt, "LAN CONTEXT REPLY: 0x%08x\n",
				    reply_desc);
				reply_desc = MPT_REPLY_EMPTY;
				break;
			default:
				mpt_prt(mpt, "Context Reply 0x%08x?\n", type);
				reply_desc = MPT_REPLY_EMPTY;
				break;
			}
			if (reply_desc == MPT_REPLY_EMPTY) {
				if (ntrips++ > 1000) {
					break;
				}
				continue;
			}
		}

		cb_index = MPT_CONTEXT_TO_CBI(ctxt_idx);
		req_index = MPT_CONTEXT_TO_REQI(ctxt_idx);
		if (req_index < MPT_MAX_REQUESTS(mpt)) {
			req = &mpt->request_pool[req_index];
		} else {
			mpt_prt(mpt, "WARN: mpt_intr index == %d (reply_desc =="
			    " 0x%x)\n", req_index, reply_desc);
		}

		bus_dmamap_sync(mpt->request_dmat, mpt->request_dmap,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
		free_rf = mpt_reply_handlers[cb_index](mpt, req,
		    reply_desc, reply_frame);

		if (reply_frame != NULL && free_rf) {
			bus_dmamap_sync_range(mpt->reply_dmat,
			    mpt->reply_dmap, offset, MPT_REPLY_SIZE,
			    BUS_DMASYNC_PREREAD);
			mpt_free_reply(mpt, reply_baddr);
		}

		/*
		 * If we got ourselves disabled, don't get stuck in a loop
		 */
		if (mpt->disabled) {
			mpt_disable_ints(mpt);
			break;
		}
		if (ntrips++ > 1000) {
			break;
		}
	}
	mpt_lprt(mpt, MPT_PRT_DEBUG2, "exit mpt_intr\n");
}

/******************************* Error Recovery *******************************/
void
mpt_complete_request_chain(struct mpt_softc *mpt, struct req_queue *chain,
			    u_int iocstatus)
{
	MSG_DEFAULT_REPLY  ioc_status_frame;
	request_t	  *req;

	memset(&ioc_status_frame, 0, sizeof(ioc_status_frame));
	ioc_status_frame.MsgLength = roundup2(sizeof(ioc_status_frame), 4);
	ioc_status_frame.IOCStatus = iocstatus;
	while((req = TAILQ_FIRST(chain)) != NULL) {
		MSG_REQUEST_HEADER *msg_hdr;
		u_int		    cb_index;

		bus_dmamap_sync(mpt->request_dmat, mpt->request_dmap,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
		msg_hdr = (MSG_REQUEST_HEADER *)req->req_vbuf;
		ioc_status_frame.Function = msg_hdr->Function;
		ioc_status_frame.MsgContext = msg_hdr->MsgContext;
		cb_index = MPT_CONTEXT_TO_CBI(le32toh(msg_hdr->MsgContext));
		mpt_reply_handlers[cb_index](mpt, req, msg_hdr->MsgContext,
		    &ioc_status_frame);
		if (mpt_req_on_pending_list(mpt, req) != 0)
			TAILQ_REMOVE(chain, req, links);
	}
}

/********************************* Diagnostics ********************************/
/*
 * Perform a diagnostic dump of a reply frame.
 */
void
mpt_dump_reply_frame(struct mpt_softc *mpt, MSG_DEFAULT_REPLY *reply_frame)
{

	mpt_prt(mpt, "Address Reply:\n");
	mpt_print_reply(reply_frame);
}

/******************************* Doorbell Access ******************************/
static __inline uint32_t mpt_rd_db(struct mpt_softc *mpt);
static __inline  uint32_t mpt_rd_intr(struct mpt_softc *mpt);

static __inline uint32_t
mpt_rd_db(struct mpt_softc *mpt)
{

	return mpt_read(mpt, MPT_OFFSET_DOORBELL);
}

static __inline uint32_t
mpt_rd_intr(struct mpt_softc *mpt)
{

	return mpt_read(mpt, MPT_OFFSET_INTR_STATUS);
}

/* Busy wait for a door bell to be read by IOC */
static int
mpt_wait_db_ack(struct mpt_softc *mpt)
{
	int i;

	for (i=0; i < MPT_MAX_WAIT; i++) {
		if (!MPT_DB_IS_BUSY(mpt_rd_intr(mpt))) {
			maxwait_ack = i > maxwait_ack ? i : maxwait_ack;
			return (MPT_OK);
		}
		DELAY(200);
	}
	return (MPT_FAIL);
}

/* Busy wait for a door bell interrupt */
static int
mpt_wait_db_int(struct mpt_softc *mpt)
{
	int i;

	for (i = 0; i < MPT_MAX_WAIT; i++) {
		if (MPT_DB_INTR(mpt_rd_intr(mpt))) {
			maxwait_int = i > maxwait_int ? i : maxwait_int;
			return MPT_OK;
		}
		DELAY(100);
	}
	return (MPT_FAIL);
}

/* Wait for IOC to transition to a give state */
void
mpt_check_doorbell(struct mpt_softc *mpt)
{
	uint32_t db = mpt_rd_db(mpt);

	if (MPT_STATE(db) != MPT_DB_STATE_RUNNING) {
		mpt_prt(mpt, "Device not running\n");
		mpt_print_db(db);
	}
}

/* Wait for IOC to transition to a give state */
static int
mpt_wait_state(struct mpt_softc *mpt, enum DB_STATE_BITS state)
{
	int i;

	for (i = 0; i < MPT_MAX_WAIT; i++) {
		uint32_t db = mpt_rd_db(mpt);
		if (MPT_STATE(db) == state) {
			maxwait_state = i > maxwait_state ? i : maxwait_state;
			return (MPT_OK);
		}
		DELAY(100);
	}
	return (MPT_FAIL);
}


/************************* Initialization/Configuration ************************/
static int mpt_download_fw(struct mpt_softc *mpt);

/* Issue the reset COMMAND to the IOC */
static int
mpt_soft_reset(struct mpt_softc *mpt)
{

	mpt_lprt(mpt, MPT_PRT_DEBUG, "soft reset\n");

	/* Have to use hard reset if we are not in Running state */
	if (MPT_STATE(mpt_rd_db(mpt)) != MPT_DB_STATE_RUNNING) {
		mpt_prt(mpt, "soft reset failed: device not running\n");
		return (MPT_FAIL);
	}

	/* If door bell is in use we don't have a chance of getting
	 * a word in since the IOC probably crashed in message
	 * processing. So don't waste our time.
	 */
	if (MPT_DB_IS_IN_USE(mpt_rd_db(mpt))) {
		mpt_prt(mpt, "soft reset failed: doorbell wedged\n");
		return (MPT_FAIL);
	}

	/* Send the reset request to the IOC */
	mpt_write(mpt, MPT_OFFSET_DOORBELL,
	    MPI_FUNCTION_IOC_MESSAGE_UNIT_RESET << MPI_DOORBELL_FUNCTION_SHIFT);
	if (mpt_wait_db_ack(mpt) != MPT_OK) {
		mpt_prt(mpt, "soft reset failed: ack timeout\n");
		return (MPT_FAIL);
	}

	/* Wait for the IOC to reload and come out of reset state */
	if (mpt_wait_state(mpt, MPT_DB_STATE_READY) != MPT_OK) {
		mpt_prt(mpt, "soft reset failed: device did not restart\n");
		return (MPT_FAIL);
	}

	return MPT_OK;
}

static int
mpt_enable_diag_mode(struct mpt_softc *mpt)
{
	int try;

	try = 20;
	while (--try) {

		if ((mpt_read(mpt, MPT_OFFSET_DIAGNOSTIC) & MPI_DIAG_DRWE) != 0)
			break;

		/* Enable diagnostic registers */
		mpt_write(mpt, MPT_OFFSET_SEQUENCE, 0xFF);
		mpt_write(mpt, MPT_OFFSET_SEQUENCE, MPI_WRSEQ_1ST_KEY_VALUE);
		mpt_write(mpt, MPT_OFFSET_SEQUENCE, MPI_WRSEQ_2ND_KEY_VALUE);
		mpt_write(mpt, MPT_OFFSET_SEQUENCE, MPI_WRSEQ_3RD_KEY_VALUE);
		mpt_write(mpt, MPT_OFFSET_SEQUENCE, MPI_WRSEQ_4TH_KEY_VALUE);
		mpt_write(mpt, MPT_OFFSET_SEQUENCE, MPI_WRSEQ_5TH_KEY_VALUE);

		DELAY(100000);
	}
	if (try == 0)
		return (EIO);
	return (0);
}

static void
mpt_disable_diag_mode(struct mpt_softc *mpt)
{

	mpt_write(mpt, MPT_OFFSET_SEQUENCE, 0xFFFFFFFF);
}

/* This is a magic diagnostic reset that resets all the ARM
 * processors in the chip.
 */
static void
mpt_hard_reset(struct mpt_softc *mpt)
{
	int error;
	int wait;
	uint32_t diagreg;

	mpt_lprt(mpt, MPT_PRT_DEBUG, "hard reset\n");

	if (mpt->is_1078) {
		mpt_write(mpt, MPT_OFFSET_RESET_1078, 0x07);
		DELAY(1000);
		return;
	}

	error = mpt_enable_diag_mode(mpt);
	if (error) {
		mpt_prt(mpt, "WARNING - Could not enter diagnostic mode !\n");
		mpt_prt(mpt, "Trying to reset anyway.\n");
	}

	diagreg = mpt_read(mpt, MPT_OFFSET_DIAGNOSTIC);

	/*
	 * This appears to be a workaround required for some
	 * firmware or hardware revs.
	 */
	mpt_write(mpt, MPT_OFFSET_DIAGNOSTIC, diagreg | MPI_DIAG_DISABLE_ARM);
	DELAY(1000);

	/* Diag. port is now active so we can now hit the reset bit */
	mpt_write(mpt, MPT_OFFSET_DIAGNOSTIC, diagreg | MPI_DIAG_RESET_ADAPTER);

        /*
         * Ensure that the reset has finished.  We delay 1ms
         * prior to reading the register to make sure the chip
         * has sufficiently completed its reset to handle register
         * accesses.
         */
	wait = 5000;
	do {
		DELAY(1000);
		diagreg = mpt_read(mpt, MPT_OFFSET_DIAGNOSTIC);
	} while (--wait && (diagreg & MPI_DIAG_RESET_ADAPTER) == 0);

	if (wait == 0) {
		mpt_prt(mpt, "WARNING - Failed hard reset! "
			"Trying to initialize anyway.\n");
	}

	/*
	 * If we have firmware to download, it must be loaded before
	 * the controller will become operational.  Do so now.
	 */
	if (mpt->fw_image != NULL) {

		error = mpt_download_fw(mpt);

		if (error) {
			mpt_prt(mpt, "WARNING - Firmware Download Failed!\n");
			mpt_prt(mpt, "Trying to initialize anyway.\n");
		}
	}

	/*
	 * Reseting the controller should have disabled write
	 * access to the diagnostic registers, but disable
	 * manually to be sure.
	 */
	mpt_disable_diag_mode(mpt);
}

static void
mpt_core_ioc_reset(struct mpt_softc *mpt, int type)
{

	/*
	 * Complete all pending requests with a status
	 * appropriate for an IOC reset.
	 */
	mpt_complete_request_chain(mpt, &mpt->request_pending_list,
				   MPI_IOCSTATUS_INVALID_STATE);
}

/*
 * Reset the IOC when needed. Try software command first then if needed
 * poke at the magic diagnostic reset. Note that a hard reset resets
 * *both* IOCs on dual function chips (FC929 && LSI1030) as well as
 * fouls up the PCI configuration registers.
 */
int
mpt_reset(struct mpt_softc *mpt, int reinit)
{
	struct	mpt_personality *pers;
	int	ret;
	int	retry_cnt = 0;

	/*
	 * Try a soft reset. If that fails, get out the big hammer.
	 */
 again:
	if ((ret = mpt_soft_reset(mpt)) != MPT_OK) {
		int	cnt;
		for (cnt = 0; cnt < 5; cnt++) {
			/* Failed; do a hard reset */
			mpt_hard_reset(mpt);

			/*
			 * Wait for the IOC to reload
			 * and come out of reset state
			 */
			ret = mpt_wait_state(mpt, MPT_DB_STATE_READY);
			if (ret == MPT_OK) {
				break;
			}
			/*
			 * Okay- try to check again...
			 */
			ret = mpt_wait_state(mpt, MPT_DB_STATE_READY);
			if (ret == MPT_OK) {
				break;
			}
			mpt_prt(mpt, "mpt_reset: failed hard reset (%d:%d)\n",
			    retry_cnt, cnt);
		}
	}

	if (retry_cnt == 0) {
		/*
		 * Invoke reset handlers.  We bump the reset count so
		 * that mpt_wait_req() understands that regardless of
		 * the specified wait condition, it should stop its wait.
		 */
		mpt->reset_cnt++;
		MPT_PERS_FOREACH(mpt, pers)
			pers->reset(mpt, ret);
	}

	if (reinit) {
		ret = mpt_enable_ioc(mpt, 1);
		if (ret == MPT_OK) {
			mpt_enable_ints(mpt);
		}
	}
	if (ret != MPT_OK && retry_cnt++ < 2) {
		goto again;
	}
	return ret;
}

/* Return a command buffer to the free queue */
void
mpt_free_request(struct mpt_softc *mpt, request_t *req)
{
	request_t *nxt;
	struct mpt_evtf_record *record;
	uint32_t offset, reply_baddr;
	
	if (req == NULL || req != &mpt->request_pool[req->index]) {
		panic("mpt_free_request: bad req ptr");
	}
	if ((nxt = req->chain) != NULL) {
		req->chain = NULL;
		mpt_free_request(mpt, nxt);	/* NB: recursion */
	}
	KASSERT(req->state != REQ_STATE_FREE, ("freeing free request"));
	KASSERT(!(req->state & REQ_STATE_LOCKED), ("freeing locked request"));
	MPT_LOCK_ASSERT(mpt);
	KASSERT(mpt_req_on_free_list(mpt, req) == 0,
	    ("mpt_free_request: req %p:%u func %x already on freelist",
	    req, req->serno, ((MSG_REQUEST_HEADER *)req->req_vbuf)->Function));
	KASSERT(mpt_req_on_pending_list(mpt, req) == 0,
	    ("mpt_free_request: req %p:%u func %x on pending list",
	    req, req->serno, ((MSG_REQUEST_HEADER *)req->req_vbuf)->Function));
#ifdef	INVARIANTS
	mpt_req_not_spcl(mpt, req, "mpt_free_request", __LINE__);
#endif

	req->ccb = NULL;
	if (LIST_EMPTY(&mpt->ack_frames)) {
		/*
		 * Insert free ones at the tail
		 */
		req->serno = 0;
		req->state = REQ_STATE_FREE;
#ifdef	INVARIANTS
		memset(req->req_vbuf, 0xff, sizeof (MSG_REQUEST_HEADER));
#endif
		TAILQ_INSERT_TAIL(&mpt->request_free_list, req, links);
		if (mpt->getreqwaiter != 0) {
			mpt->getreqwaiter = 0;
			wakeup(&mpt->request_free_list);
		}
		return;
	}

	/*
	 * Process an ack frame deferred due to resource shortage.
	 */
	record = LIST_FIRST(&mpt->ack_frames);
	LIST_REMOVE(record, links);
	req->state = REQ_STATE_ALLOCATED;
	mpt_assign_serno(mpt, req);
	mpt_send_event_ack(mpt, req, &record->reply, record->context);
	offset = (uint32_t)((uint8_t *)record - mpt->reply);
	reply_baddr = offset + (mpt->reply_phys & 0xFFFFFFFF);
	bus_dmamap_sync_range(mpt->reply_dmat, mpt->reply_dmap, offset,
	    MPT_REPLY_SIZE, BUS_DMASYNC_PREREAD);
	mpt_free_reply(mpt, reply_baddr);
}

/* Get a command buffer from the free queue */
request_t *
mpt_get_request(struct mpt_softc *mpt, int sleep_ok)
{
	request_t *req;

retry:
	MPT_LOCK_ASSERT(mpt);
	req = TAILQ_FIRST(&mpt->request_free_list);
	if (req != NULL) {
		KASSERT(req == &mpt->request_pool[req->index],
		    ("mpt_get_request: corrupted request free list"));
		KASSERT(req->state == REQ_STATE_FREE,
		    ("req %p:%u not free on free list %x index %d function %x",
		    req, req->serno, req->state, req->index,
		    ((MSG_REQUEST_HEADER *)req->req_vbuf)->Function));
		TAILQ_REMOVE(&mpt->request_free_list, req, links);
		req->state = REQ_STATE_ALLOCATED;
		req->chain = NULL;
		mpt_assign_serno(mpt, req);
	} else if (sleep_ok != 0) {
		mpt->getreqwaiter = 1;
		mpt_sleep(mpt, &mpt->request_free_list, PUSER, "mptgreq", 0);
		goto retry;
	}
	return (req);
}

/* Pass the command to the IOC */
void
mpt_send_cmd(struct mpt_softc *mpt, request_t *req)
{

	if (mpt->verbose > MPT_PRT_DEBUG2) {
		mpt_dump_request(mpt, req);
	}
	bus_dmamap_sync(mpt->request_dmat, mpt->request_dmap,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	req->state |= REQ_STATE_QUEUED;
	KASSERT(mpt_req_on_free_list(mpt, req) == 0,
	    ("req %p:%u func %x on freelist list in mpt_send_cmd",
	    req, req->serno, ((MSG_REQUEST_HEADER *)req->req_vbuf)->Function));
	KASSERT(mpt_req_on_pending_list(mpt, req) == 0,
	    ("req %p:%u func %x already on pending list in mpt_send_cmd",
	    req, req->serno, ((MSG_REQUEST_HEADER *)req->req_vbuf)->Function));
	TAILQ_INSERT_HEAD(&mpt->request_pending_list, req, links);
	mpt_write(mpt, MPT_OFFSET_REQUEST_Q, (uint32_t) req->req_pbuf);
}

/*
 * Wait for a request to complete.
 *
 * Inputs:
 *	mpt		softc of controller executing request
 *	req		request to wait for
 *	sleep_ok	nonzero implies may sleep in this context
 *	time_ms		timeout in ms.  0 implies no timeout.
 *
 * Return Values:
 *	0		Request completed
 *	non-0		Timeout fired before request completion.
 */
int
mpt_wait_req(struct mpt_softc *mpt, request_t *req,
	     mpt_req_state_t state, mpt_req_state_t mask,
	     int sleep_ok, int time_ms)
{
	int   timeout;
	u_int saved_cnt;
	sbintime_t sbt;

	/*
	 * time_ms is in ms, 0 indicates infinite wait.
	 * Convert to sbintime_t or 500us units depending on
	 * our sleep mode.
	 */
	if (sleep_ok != 0) {
		sbt = SBT_1MS * time_ms;
		/* Set timeout as well so final timeout check works. */
		timeout = time_ms;
	} else {
		sbt = 0; /* Squelch bogus gcc warning. */
		timeout = time_ms * 2;
	}
	req->state |= REQ_STATE_NEED_WAKEUP;
	mask &= ~REQ_STATE_NEED_WAKEUP;
	saved_cnt = mpt->reset_cnt;
	while ((req->state & mask) != state && mpt->reset_cnt == saved_cnt) {
		if (sleep_ok != 0) {
			if (mpt_sleep(mpt, req, PUSER, "mptreq", sbt) ==
			    EWOULDBLOCK) {
				timeout = 0;
				break;
			}
		} else {
			if (time_ms != 0 && --timeout == 0) {
				break;
			}
			DELAY(500);
			mpt_intr(mpt);
		}
	}
	req->state &= ~REQ_STATE_NEED_WAKEUP;
	if (mpt->reset_cnt != saved_cnt) {
		return (EIO);
	}
	if (time_ms && timeout <= 0) {
		MSG_REQUEST_HEADER *msg_hdr = req->req_vbuf;
		req->state |= REQ_STATE_TIMEDOUT;
		mpt_prt(mpt, "mpt_wait_req(%x) timed out\n", msg_hdr->Function);
		return (ETIMEDOUT);
	}
	return (0);
}

/*
 * Send a command to the IOC via the handshake register.
 *
 * Only done at initialization time and for certain unusual
 * commands such as device/bus reset as specified by LSI.
 */
int
mpt_send_handshake_cmd(struct mpt_softc *mpt, size_t len, void *cmd)
{
	int i;
	uint32_t data, *data32;

	/* Check condition of the IOC */
	data = mpt_rd_db(mpt);
	if ((MPT_STATE(data) != MPT_DB_STATE_READY
	  && MPT_STATE(data) != MPT_DB_STATE_RUNNING
	  && MPT_STATE(data) != MPT_DB_STATE_FAULT)
	 || MPT_DB_IS_IN_USE(data)) {
		mpt_prt(mpt, "handshake aborted - invalid doorbell state\n");
		mpt_print_db(data);
		return (EBUSY);
	}

	/* We move things in 32 bit chunks */
	len = (len + 3) >> 2;
	data32 = cmd;

	/* Clear any left over pending doorbell interrupts */
	if (MPT_DB_INTR(mpt_rd_intr(mpt)))
		mpt_write(mpt, MPT_OFFSET_INTR_STATUS, 0);

	/*
	 * Tell the handshake reg. we are going to send a command
         * and how long it is going to be.
	 */
	data = (MPI_FUNCTION_HANDSHAKE << MPI_DOORBELL_FUNCTION_SHIFT) |
	    (len << MPI_DOORBELL_ADD_DWORDS_SHIFT);
	mpt_write(mpt, MPT_OFFSET_DOORBELL, data);

	/* Wait for the chip to notice */
	if (mpt_wait_db_int(mpt) != MPT_OK) {
		mpt_prt(mpt, "mpt_send_handshake_cmd: db ignored\n");
		return (ETIMEDOUT);
	}

	/* Clear the interrupt */
	mpt_write(mpt, MPT_OFFSET_INTR_STATUS, 0);

	if (mpt_wait_db_ack(mpt) != MPT_OK) {
		mpt_prt(mpt, "mpt_send_handshake_cmd: db ack timed out\n");
		return (ETIMEDOUT);
	}

	/* Send the command */
	for (i = 0; i < len; i++) {
		mpt_write_stream(mpt, MPT_OFFSET_DOORBELL, *data32++);
		if (mpt_wait_db_ack(mpt) != MPT_OK) {
			mpt_prt(mpt,
			    "mpt_send_handshake_cmd: timeout @ index %d\n", i);
			return (ETIMEDOUT);
		}
	}
	return MPT_OK;
}

/* Get the response from the handshake register */
int
mpt_recv_handshake_reply(struct mpt_softc *mpt, size_t reply_len, void *reply)
{
	int left, reply_left;
	u_int16_t *data16;
	uint32_t data;
	MSG_DEFAULT_REPLY *hdr;

	/* We move things out in 16 bit chunks */
	reply_len >>= 1;
	data16 = (u_int16_t *)reply;

	hdr = (MSG_DEFAULT_REPLY *)reply;

	/* Get first word */
	if (mpt_wait_db_int(mpt) != MPT_OK) {
		mpt_prt(mpt, "mpt_recv_handshake_cmd timeout1\n");
		return ETIMEDOUT;
	}
	data = mpt_read(mpt, MPT_OFFSET_DOORBELL);
	*data16++ = le16toh(data & MPT_DB_DATA_MASK);
	mpt_write(mpt, MPT_OFFSET_INTR_STATUS, 0);

	/* Get second word */
	if (mpt_wait_db_int(mpt) != MPT_OK) {
		mpt_prt(mpt, "mpt_recv_handshake_cmd timeout2\n");
		return ETIMEDOUT;
	}
	data = mpt_read(mpt, MPT_OFFSET_DOORBELL);
	*data16++ = le16toh(data & MPT_DB_DATA_MASK);
	mpt_write(mpt, MPT_OFFSET_INTR_STATUS, 0);

	/*
	 * With the second word, we can now look at the length.
	 * Warn about a reply that's too short (except for IOC FACTS REPLY)
	 */
	if ((reply_len >> 1) != hdr->MsgLength &&
	    (hdr->Function != MPI_FUNCTION_IOC_FACTS)){
		mpt_prt(mpt, "reply length does not match message length: "
			"got %x; expected %zx for function %x\n",
			hdr->MsgLength << 2, reply_len << 1, hdr->Function);
	}

	/* Get rest of the reply; but don't overflow the provided buffer */
	left = (hdr->MsgLength << 1) - 2;
	reply_left =  reply_len - 2;
	while (left--) {
		if (mpt_wait_db_int(mpt) != MPT_OK) {
			mpt_prt(mpt, "mpt_recv_handshake_cmd timeout3\n");
			return ETIMEDOUT;
		}
		data = mpt_read(mpt, MPT_OFFSET_DOORBELL);
		if (reply_left-- > 0)
			*data16++ = le16toh(data & MPT_DB_DATA_MASK);
		mpt_write(mpt, MPT_OFFSET_INTR_STATUS, 0);
	}

	/* One more wait & clear at the end */
	if (mpt_wait_db_int(mpt) != MPT_OK) {
		mpt_prt(mpt, "mpt_recv_handshake_cmd timeout4\n");
		return ETIMEDOUT;
	}
	mpt_write(mpt, MPT_OFFSET_INTR_STATUS, 0);

	if ((hdr->IOCStatus & MPI_IOCSTATUS_MASK) != MPI_IOCSTATUS_SUCCESS) {
		if (mpt->verbose >= MPT_PRT_TRACE)
			mpt_print_reply(hdr);
		return (MPT_FAIL | hdr->IOCStatus);
	}

	return (0);
}

static int
mpt_get_iocfacts(struct mpt_softc *mpt, MSG_IOC_FACTS_REPLY *freplp)
{
	MSG_IOC_FACTS f_req;
	int error;
	
	memset(&f_req, 0, sizeof f_req);
	f_req.Function = MPI_FUNCTION_IOC_FACTS;
	f_req.MsgContext = htole32(MPT_REPLY_HANDLER_HANDSHAKE);
	error = mpt_send_handshake_cmd(mpt, sizeof f_req, &f_req);
	if (error) {
		return(error);
	}
	error = mpt_recv_handshake_reply(mpt, sizeof (*freplp), freplp);
	return (error);
}

static int
mpt_get_portfacts(struct mpt_softc *mpt, U8 port, MSG_PORT_FACTS_REPLY *freplp)
{
	MSG_PORT_FACTS f_req;
	int error;
	
	memset(&f_req, 0, sizeof f_req);
	f_req.Function = MPI_FUNCTION_PORT_FACTS;
	f_req.PortNumber = port;
	f_req.MsgContext = htole32(MPT_REPLY_HANDLER_HANDSHAKE);
	error = mpt_send_handshake_cmd(mpt, sizeof f_req, &f_req);
	if (error) {
		return(error);
	}
	error = mpt_recv_handshake_reply(mpt, sizeof (*freplp), freplp);
	return (error);
}

/*
 * Send the initialization request. This is where we specify how many
 * SCSI buses and how many devices per bus we wish to emulate.
 * This is also the command that specifies the max size of the reply
 * frames from the IOC that we will be allocating.
 */
static int
mpt_send_ioc_init(struct mpt_softc *mpt, uint32_t who)
{
	int error = 0;
	MSG_IOC_INIT init;
	MSG_IOC_INIT_REPLY reply;

	memset(&init, 0, sizeof init);
	init.WhoInit = who;
	init.Function = MPI_FUNCTION_IOC_INIT;
	init.MaxDevices = 0;	/* at least 256 devices per bus */
	init.MaxBuses = 16;	/* at least 16 buses */

	init.MsgVersion = htole16(MPI_VERSION);
	init.HeaderVersion = htole16(MPI_HEADER_VERSION);
	init.ReplyFrameSize = htole16(MPT_REPLY_SIZE);
	init.MsgContext = htole32(MPT_REPLY_HANDLER_HANDSHAKE);

	if ((error = mpt_send_handshake_cmd(mpt, sizeof init, &init)) != 0) {
		return(error);
	}

	error = mpt_recv_handshake_reply(mpt, sizeof reply, &reply);
	return (error);
}


/*
 * Utiltity routine to read configuration headers and pages
 */
int
mpt_issue_cfg_req(struct mpt_softc *mpt, request_t *req, cfgparms_t *params,
		  bus_addr_t addr, bus_size_t len, int sleep_ok, int timeout_ms)
{
	MSG_CONFIG *cfgp;
	SGE_SIMPLE32 *se;

	cfgp = req->req_vbuf;
	memset(cfgp, 0, sizeof *cfgp);
	cfgp->Action = params->Action;
	cfgp->Function = MPI_FUNCTION_CONFIG;
	cfgp->Header.PageVersion = params->PageVersion;
	cfgp->Header.PageNumber = params->PageNumber;
	cfgp->PageAddress = htole32(params->PageAddress);
	if ((params->PageType & MPI_CONFIG_PAGETYPE_MASK) ==
	    MPI_CONFIG_PAGETYPE_EXTENDED) {
		cfgp->Header.PageType = MPI_CONFIG_PAGETYPE_EXTENDED;
		cfgp->Header.PageLength = 0;
		cfgp->ExtPageLength = htole16(params->ExtPageLength);
		cfgp->ExtPageType = params->ExtPageType;
	} else {
		cfgp->Header.PageType = params->PageType;
		cfgp->Header.PageLength = params->PageLength;
	}
	se = (SGE_SIMPLE32 *)&cfgp->PageBufferSGE;
	se->Address = htole32(addr);
	MPI_pSGE_SET_LENGTH(se, len);
	MPI_pSGE_SET_FLAGS(se, (MPI_SGE_FLAGS_SIMPLE_ELEMENT |
	    MPI_SGE_FLAGS_LAST_ELEMENT | MPI_SGE_FLAGS_END_OF_BUFFER |
	    MPI_SGE_FLAGS_END_OF_LIST |
	    ((params->Action == MPI_CONFIG_ACTION_PAGE_WRITE_CURRENT
	  || params->Action == MPI_CONFIG_ACTION_PAGE_WRITE_NVRAM)
	   ? MPI_SGE_FLAGS_HOST_TO_IOC : MPI_SGE_FLAGS_IOC_TO_HOST)));
	se->FlagsLength = htole32(se->FlagsLength);
	cfgp->MsgContext = htole32(req->index | MPT_REPLY_HANDLER_CONFIG);

	mpt_check_doorbell(mpt);
	mpt_send_cmd(mpt, req);
	return (mpt_wait_req(mpt, req, REQ_STATE_DONE, REQ_STATE_DONE,
			     sleep_ok, timeout_ms));
}

int
mpt_read_extcfg_header(struct mpt_softc *mpt, int PageVersion, int PageNumber,
		       uint32_t PageAddress, int ExtPageType,
		       CONFIG_EXTENDED_PAGE_HEADER *rslt,
		       int sleep_ok, int timeout_ms)
{
	request_t  *req;
	cfgparms_t params;
	MSG_CONFIG_REPLY *cfgp;
	int	    error;

	req = mpt_get_request(mpt, sleep_ok);
	if (req == NULL) {
		mpt_prt(mpt, "mpt_extread_cfg_header: Get request failed!\n");
		return (ENOMEM);
	}

	params.Action = MPI_CONFIG_ACTION_PAGE_HEADER;
	params.PageVersion = PageVersion;
	params.PageLength = 0;
	params.PageNumber = PageNumber;
	params.PageType = MPI_CONFIG_PAGETYPE_EXTENDED;
	params.PageAddress = PageAddress;
	params.ExtPageType = ExtPageType;
	params.ExtPageLength = 0;
	error = mpt_issue_cfg_req(mpt, req, &params, /*addr*/0, /*len*/0,
				  sleep_ok, timeout_ms);
	if (error != 0) {
		/*
		 * Leave the request. Without resetting the chip, it's
		 * still owned by it and we'll just get into trouble
		 * freeing it now. Mark it as abandoned so that if it
		 * shows up later it can be freed.
		 */
		mpt_prt(mpt, "read_extcfg_header timed out\n");
		return (ETIMEDOUT);
	}

        switch (req->IOCStatus & MPI_IOCSTATUS_MASK) {
	case MPI_IOCSTATUS_SUCCESS:
		cfgp = req->req_vbuf;
		rslt->PageVersion = cfgp->Header.PageVersion;
		rslt->PageNumber = cfgp->Header.PageNumber;
		rslt->PageType = cfgp->Header.PageType;
		rslt->ExtPageLength = le16toh(cfgp->ExtPageLength);
		rslt->ExtPageType = cfgp->ExtPageType;
		error = 0;
		break;
	case MPI_IOCSTATUS_CONFIG_INVALID_PAGE:
		mpt_lprt(mpt, MPT_PRT_DEBUG,
		    "Invalid Page Type %d Number %d Addr 0x%0x\n",
		    MPI_CONFIG_PAGETYPE_EXTENDED, PageNumber, PageAddress);
		error = EINVAL;
		break;
	default:
		mpt_prt(mpt, "mpt_read_extcfg_header: Config Info Status %x\n",
			req->IOCStatus);
		error = EIO;
		break;
	}
	mpt_free_request(mpt, req);
	return (error);
}

int
mpt_read_extcfg_page(struct mpt_softc *mpt, int Action, uint32_t PageAddress,
		     CONFIG_EXTENDED_PAGE_HEADER *hdr, void *buf, size_t len,
		     int sleep_ok, int timeout_ms)
{
	request_t    *req;
	cfgparms_t    params;
	int	      error;

	req = mpt_get_request(mpt, sleep_ok);
	if (req == NULL) {
		mpt_prt(mpt, "mpt_read_extcfg_page: Get request failed!\n");
		return (-1);
	}

	params.Action = Action;
	params.PageVersion = hdr->PageVersion;
	params.PageLength = 0;
	params.PageNumber = hdr->PageNumber;
	params.PageType = MPI_CONFIG_PAGETYPE_EXTENDED;
	params.PageAddress = PageAddress;
	params.ExtPageType = hdr->ExtPageType;
	params.ExtPageLength = hdr->ExtPageLength;
	error = mpt_issue_cfg_req(mpt, req, &params,
				  req->req_pbuf + MPT_RQSL(mpt),
				  len, sleep_ok, timeout_ms);
	if (error != 0) {
		mpt_prt(mpt, "read_extcfg_page(%d) timed out\n", Action);
		return (-1);
	}

	if ((req->IOCStatus & MPI_IOCSTATUS_MASK) != MPI_IOCSTATUS_SUCCESS) {
		mpt_prt(mpt, "mpt_read_extcfg_page: Config Info Status %x\n",
			req->IOCStatus);
		mpt_free_request(mpt, req);
		return (-1);
	}
	memcpy(buf, ((uint8_t *)req->req_vbuf)+MPT_RQSL(mpt), len);
	mpt_free_request(mpt, req);
	return (0);
}

int
mpt_read_cfg_header(struct mpt_softc *mpt, int PageType, int PageNumber,
		    uint32_t PageAddress, CONFIG_PAGE_HEADER *rslt,
		    int sleep_ok, int timeout_ms)
{
	request_t  *req;
	cfgparms_t params;
	MSG_CONFIG *cfgp;
	int	    error;

	req = mpt_get_request(mpt, sleep_ok);
	if (req == NULL) {
		mpt_prt(mpt, "mpt_read_cfg_header: Get request failed!\n");
		return (ENOMEM);
	}

	params.Action = MPI_CONFIG_ACTION_PAGE_HEADER;
	params.PageVersion = 0;
	params.PageLength = 0;
	params.PageNumber = PageNumber;
	params.PageType = PageType;
	params.PageAddress = PageAddress;
	error = mpt_issue_cfg_req(mpt, req, &params, /*addr*/0, /*len*/0,
				  sleep_ok, timeout_ms);
	if (error != 0) {
		/*
		 * Leave the request. Without resetting the chip, it's
		 * still owned by it and we'll just get into trouble
		 * freeing it now. Mark it as abandoned so that if it
		 * shows up later it can be freed.
		 */
		mpt_prt(mpt, "read_cfg_header timed out\n");
		return (ETIMEDOUT);
	}

        switch (req->IOCStatus & MPI_IOCSTATUS_MASK) {
	case MPI_IOCSTATUS_SUCCESS:
		cfgp = req->req_vbuf;
		bcopy(&cfgp->Header, rslt, sizeof(*rslt));
		error = 0;
		break;
	case MPI_IOCSTATUS_CONFIG_INVALID_PAGE:
		mpt_lprt(mpt, MPT_PRT_DEBUG,
		    "Invalid Page Type %d Number %d Addr 0x%0x\n",
		    PageType, PageNumber, PageAddress);
		error = EINVAL;
		break;
	default:
		mpt_prt(mpt, "mpt_read_cfg_header: Config Info Status %x\n",
			req->IOCStatus);
		error = EIO;
		break;
	}
	mpt_free_request(mpt, req);
	return (error);
}

int
mpt_read_cfg_page(struct mpt_softc *mpt, int Action, uint32_t PageAddress,
		  CONFIG_PAGE_HEADER *hdr, size_t len, int sleep_ok,
		  int timeout_ms)
{
	request_t    *req;
	cfgparms_t    params;
	int	      error;

	req = mpt_get_request(mpt, sleep_ok);
	if (req == NULL) {
		mpt_prt(mpt, "mpt_read_cfg_page: Get request failed!\n");
		return (-1);
	}

	params.Action = Action;
	params.PageVersion = hdr->PageVersion;
	params.PageLength = hdr->PageLength;
	params.PageNumber = hdr->PageNumber;
	params.PageType = hdr->PageType & MPI_CONFIG_PAGETYPE_MASK;
	params.PageAddress = PageAddress;
	error = mpt_issue_cfg_req(mpt, req, &params,
				  req->req_pbuf + MPT_RQSL(mpt),
				  len, sleep_ok, timeout_ms);
	if (error != 0) {
		mpt_prt(mpt, "read_cfg_page(%d) timed out\n", Action);
		return (-1);
	}

	if ((req->IOCStatus & MPI_IOCSTATUS_MASK) != MPI_IOCSTATUS_SUCCESS) {
		mpt_prt(mpt, "mpt_read_cfg_page: Config Info Status %x\n",
			req->IOCStatus);
		mpt_free_request(mpt, req);
		return (-1);
	}
	memcpy(hdr, ((uint8_t *)req->req_vbuf)+MPT_RQSL(mpt), len);
	mpt_free_request(mpt, req);
	return (0);
}

int
mpt_write_cfg_page(struct mpt_softc *mpt, int Action, uint32_t PageAddress,
		   CONFIG_PAGE_HEADER *hdr, size_t len, int sleep_ok,
		   int timeout_ms)
{
	request_t    *req;
	cfgparms_t    params;
	u_int	      hdr_attr;
	int	      error;

	hdr_attr = hdr->PageType & MPI_CONFIG_PAGEATTR_MASK;
	if (hdr_attr != MPI_CONFIG_PAGEATTR_CHANGEABLE &&
	    hdr_attr != MPI_CONFIG_PAGEATTR_PERSISTENT) {
		mpt_prt(mpt, "page type 0x%x not changeable\n",
			hdr->PageType & MPI_CONFIG_PAGETYPE_MASK);
		return (-1);
	}

#if	0
	/*
	 * We shouldn't mask off other bits here.
	 */
	hdr->PageType &= MPI_CONFIG_PAGETYPE_MASK;
#endif

	req = mpt_get_request(mpt, sleep_ok);
	if (req == NULL)
		return (-1);

	memcpy(((caddr_t)req->req_vbuf) + MPT_RQSL(mpt), hdr, len);

	/*
	 * There isn't any point in restoring stripped out attributes
	 * if you then mask them going down to issue the request.
	 */

	params.Action = Action;
	params.PageVersion = hdr->PageVersion;
	params.PageLength = hdr->PageLength;
	params.PageNumber = hdr->PageNumber;
	params.PageAddress = PageAddress;
#if	0
	/* Restore stripped out attributes */
	hdr->PageType |= hdr_attr;
	params.PageType = hdr->PageType & MPI_CONFIG_PAGETYPE_MASK;
#else
	params.PageType = hdr->PageType;
#endif
	error = mpt_issue_cfg_req(mpt, req, &params,
				  req->req_pbuf + MPT_RQSL(mpt),
				  len, sleep_ok, timeout_ms);
	if (error != 0) {
		mpt_prt(mpt, "mpt_write_cfg_page timed out\n");
		return (-1);
	}

        if ((req->IOCStatus & MPI_IOCSTATUS_MASK) != MPI_IOCSTATUS_SUCCESS) {
		mpt_prt(mpt, "mpt_write_cfg_page: Config Info Status %x\n",
			req->IOCStatus);
		mpt_free_request(mpt, req);
		return (-1);
	}
	mpt_free_request(mpt, req);
	return (0);
}

/*
 * Read IOC configuration information
 */
static int
mpt_read_config_info_ioc(struct mpt_softc *mpt)
{
	CONFIG_PAGE_HEADER hdr;
	struct mpt_raid_volume *mpt_raid;
	int rv;
	int i;
	size_t len;

	rv = mpt_read_cfg_header(mpt, MPI_CONFIG_PAGETYPE_IOC,
		2, 0, &hdr, FALSE, 5000);
	/*
	 * If it's an invalid page, so what? Not a supported function....
	 */
	if (rv == EINVAL) {
		return (0);
	}
	if (rv) {
		return (rv);
	}

	mpt_lprt(mpt, MPT_PRT_DEBUG,
	    "IOC Page 2 Header: Version %x len %x PageNumber %x PageType %x\n",
	    hdr.PageVersion, hdr.PageLength << 2,
	    hdr.PageNumber, hdr.PageType);

	len = hdr.PageLength * sizeof(uint32_t);
	mpt->ioc_page2 = malloc(len, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (mpt->ioc_page2 == NULL) {
		mpt_prt(mpt, "unable to allocate memory for IOC page 2\n");
		mpt_raid_free_mem(mpt);
		return (ENOMEM);
	}
	memcpy(&mpt->ioc_page2->Header, &hdr, sizeof(hdr));
	rv = mpt_read_cur_cfg_page(mpt, 0,
	    &mpt->ioc_page2->Header, len, FALSE, 5000);
	if (rv) {
		mpt_prt(mpt, "failed to read IOC Page 2\n");
		mpt_raid_free_mem(mpt);
		return (EIO);
	}
	mpt2host_config_page_ioc2(mpt->ioc_page2);

	if (mpt->ioc_page2->CapabilitiesFlags != 0) {
		uint32_t mask;

		mpt_prt(mpt, "Capabilities: (");
		for (mask = 1; mask != 0; mask <<= 1) {
			if ((mpt->ioc_page2->CapabilitiesFlags & mask) == 0) {
				continue;
			}
			switch (mask) {
			case MPI_IOCPAGE2_CAP_FLAGS_IS_SUPPORT:
				mpt_prtc(mpt, " RAID-0");
				break;
			case MPI_IOCPAGE2_CAP_FLAGS_IME_SUPPORT:
				mpt_prtc(mpt, " RAID-1E");
				break;
			case MPI_IOCPAGE2_CAP_FLAGS_IM_SUPPORT:
				mpt_prtc(mpt, " RAID-1");
				break;
			case MPI_IOCPAGE2_CAP_FLAGS_SES_SUPPORT:
				mpt_prtc(mpt, " SES");
				break;
			case MPI_IOCPAGE2_CAP_FLAGS_SAFTE_SUPPORT:
				mpt_prtc(mpt, " SAFTE");
				break;
			case MPI_IOCPAGE2_CAP_FLAGS_CROSS_CHANNEL_SUPPORT:
				mpt_prtc(mpt, " Multi-Channel-Arrays");
			default:
				break;
			}
		}
		mpt_prtc(mpt, " )\n");
		if ((mpt->ioc_page2->CapabilitiesFlags
		   & (MPI_IOCPAGE2_CAP_FLAGS_IS_SUPPORT
		    | MPI_IOCPAGE2_CAP_FLAGS_IME_SUPPORT
		    | MPI_IOCPAGE2_CAP_FLAGS_IM_SUPPORT)) != 0) {
			mpt_prt(mpt, "%d Active Volume%s(%d Max)\n",
				mpt->ioc_page2->NumActiveVolumes,
				mpt->ioc_page2->NumActiveVolumes != 1
			      ? "s " : " ",
				mpt->ioc_page2->MaxVolumes);
			mpt_prt(mpt, "%d Hidden Drive Member%s(%d Max)\n",
				mpt->ioc_page2->NumActivePhysDisks,
				mpt->ioc_page2->NumActivePhysDisks != 1
			      ? "s " : " ",
				mpt->ioc_page2->MaxPhysDisks);
		}
	}

	len = mpt->ioc_page2->MaxVolumes * sizeof(struct mpt_raid_volume);
	mpt->raid_volumes = malloc(len, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (mpt->raid_volumes == NULL) {
		mpt_prt(mpt, "Could not allocate RAID volume data\n");
		mpt_raid_free_mem(mpt);
		return (ENOMEM);
	}

	/*
	 * Copy critical data out of ioc_page2 so that we can
	 * safely refresh the page without windows of unreliable
	 * data.
	 */
	mpt->raid_max_volumes =  mpt->ioc_page2->MaxVolumes;

	len = sizeof(*mpt->raid_volumes->config_page) +
	    (sizeof (RAID_VOL0_PHYS_DISK) * (mpt->ioc_page2->MaxPhysDisks - 1));
	for (i = 0; i < mpt->ioc_page2->MaxVolumes; i++) {
		mpt_raid = &mpt->raid_volumes[i];
		mpt_raid->config_page =
		    malloc(len, M_DEVBUF, M_NOWAIT | M_ZERO);
		if (mpt_raid->config_page == NULL) {
			mpt_prt(mpt, "Could not allocate RAID page data\n");
			mpt_raid_free_mem(mpt);
			return (ENOMEM);
		}
	}
	mpt->raid_page0_len = len;

	len = mpt->ioc_page2->MaxPhysDisks * sizeof(struct mpt_raid_disk);
	mpt->raid_disks = malloc(len, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (mpt->raid_disks == NULL) {
		mpt_prt(mpt, "Could not allocate RAID disk data\n");
		mpt_raid_free_mem(mpt);
		return (ENOMEM);
	}
	mpt->raid_max_disks =  mpt->ioc_page2->MaxPhysDisks;

	/*
	 * Load page 3.
	 */
	rv = mpt_read_cfg_header(mpt, MPI_CONFIG_PAGETYPE_IOC,
	    3, 0, &hdr, FALSE, 5000);
	if (rv) {
		mpt_raid_free_mem(mpt);
		return (EIO);
	}

	mpt_lprt(mpt, MPT_PRT_DEBUG, "IOC Page 3 Header: %x %x %x %x\n",
	    hdr.PageVersion, hdr.PageLength, hdr.PageNumber, hdr.PageType);

	len = hdr.PageLength * sizeof(uint32_t);
	mpt->ioc_page3 = malloc(len, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (mpt->ioc_page3 == NULL) {
		mpt_prt(mpt, "unable to allocate memory for IOC page 3\n");
		mpt_raid_free_mem(mpt);
		return (ENOMEM);
	}
	memcpy(&mpt->ioc_page3->Header, &hdr, sizeof(hdr));
	rv = mpt_read_cur_cfg_page(mpt, 0,
	    &mpt->ioc_page3->Header, len, FALSE, 5000);
	if (rv) {
		mpt_raid_free_mem(mpt);
		return (EIO);
	}
	mpt2host_config_page_ioc3(mpt->ioc_page3);
	mpt_raid_wakeup(mpt);
	return (0);
}

/*
 * Enable IOC port
 */
static int
mpt_send_port_enable(struct mpt_softc *mpt, int port)
{
	request_t	*req;
	MSG_PORT_ENABLE *enable_req;
	int		 error;

	req = mpt_get_request(mpt, /*sleep_ok*/FALSE);
	if (req == NULL)
		return (-1);

	enable_req = req->req_vbuf;
	memset(enable_req, 0,  MPT_RQSL(mpt));

	enable_req->Function   = MPI_FUNCTION_PORT_ENABLE;
	enable_req->MsgContext = htole32(req->index | MPT_REPLY_HANDLER_CONFIG);
	enable_req->PortNumber = port;

	mpt_check_doorbell(mpt);
	mpt_lprt(mpt, MPT_PRT_DEBUG, "enabling port %d\n", port);

	mpt_send_cmd(mpt, req);
	error = mpt_wait_req(mpt, req, REQ_STATE_DONE, REQ_STATE_DONE,
	    FALSE, (mpt->is_sas || mpt->is_fc)? 300000 : 30000);
	if (error != 0) {
		mpt_prt(mpt, "port %d enable timed out\n", port);
		return (-1);
	}
	mpt_free_request(mpt, req);
	mpt_lprt(mpt, MPT_PRT_DEBUG, "enabled port %d\n", port);
	return (0);
}

/*
 * Enable/Disable asynchronous event reporting.
 */
static int
mpt_send_event_request(struct mpt_softc *mpt, int onoff)
{
	request_t *req;
	MSG_EVENT_NOTIFY *enable_req;

	req = mpt_get_request(mpt, FALSE);
	if (req == NULL) {
		return (ENOMEM);
	}
	enable_req = req->req_vbuf;
	memset(enable_req, 0, sizeof *enable_req);

	enable_req->Function   = MPI_FUNCTION_EVENT_NOTIFICATION;
	enable_req->MsgContext = htole32(req->index | MPT_REPLY_HANDLER_EVENTS);
	enable_req->Switch     = onoff;

	mpt_check_doorbell(mpt);
	mpt_lprt(mpt, MPT_PRT_DEBUG, "%sabling async events\n",
	    onoff ? "en" : "dis");
	/*
	 * Send the command off, but don't wait for it.
	 */
	mpt_send_cmd(mpt, req);
	return (0);
}

/*
 * Un-mask the interrupts on the chip.
 */
void
mpt_enable_ints(struct mpt_softc *mpt)
{

	/* Unmask every thing except door bell int */
	mpt_write(mpt, MPT_OFFSET_INTR_MASK, MPT_INTR_DB_MASK);
}

/*
 * Mask the interrupts on the chip.
 */
void
mpt_disable_ints(struct mpt_softc *mpt)
{

	/* Mask all interrupts */
	mpt_write(mpt, MPT_OFFSET_INTR_MASK,
	    MPT_INTR_REPLY_MASK | MPT_INTR_DB_MASK);
}

static void
mpt_sysctl_attach(struct mpt_softc *mpt)
{
	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(mpt->dev);
	struct sysctl_oid *tree = device_get_sysctl_tree(mpt->dev);

	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		       "debug", CTLFLAG_RW, &mpt->verbose, 0,
		       "Debugging/Verbose level");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		       "role", CTLFLAG_RD, &mpt->role, 0,
		       "HBA role");
#ifdef	MPT_TEST_MULTIPATH
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		       "failure_id", CTLFLAG_RW, &mpt->failure_id, -1,
		       "Next Target to Fail");
#endif
}

int
mpt_attach(struct mpt_softc *mpt)
{
	struct mpt_personality *pers;
	int i;
	int error;

	mpt_core_attach(mpt);
	mpt_core_enable(mpt);

	TAILQ_INSERT_TAIL(&mpt_tailq, mpt, links);
	for (i = 0; i < MPT_MAX_PERSONALITIES; i++) {
		pers = mpt_personalities[i];
		if (pers == NULL) {
			continue;
		}
		if (pers->probe(mpt) == 0) {
			error = pers->attach(mpt);
			if (error != 0) {
				mpt_detach(mpt);
				return (error);
			}
			mpt->mpt_pers_mask |= (0x1 << pers->id);
			pers->use_count++;
		}
	}

	/*
	 * Now that we've attached everything, do the enable function
	 * for all of the personalities. This allows the personalities
	 * to do setups that are appropriate for them prior to enabling
	 * any ports.
	 */
	for (i = 0; i < MPT_MAX_PERSONALITIES; i++) {
		pers = mpt_personalities[i];
		if (pers != NULL  && MPT_PERS_ATTACHED(pers, mpt) != 0) {
			error = pers->enable(mpt);
			if (error != 0) {
				mpt_prt(mpt, "personality %s attached but would"
				    " not enable (%d)\n", pers->name, error);
				mpt_detach(mpt);
				return (error);
			}
		}
	}
	return (0);
}

int
mpt_shutdown(struct mpt_softc *mpt)
{
	struct mpt_personality *pers;

	MPT_PERS_FOREACH_REVERSE(mpt, pers) {
		pers->shutdown(mpt);
	}
	return (0);
}

int
mpt_detach(struct mpt_softc *mpt)
{
	struct mpt_personality *pers;

	MPT_PERS_FOREACH_REVERSE(mpt, pers) {
		pers->detach(mpt);
		mpt->mpt_pers_mask &= ~(0x1 << pers->id);
		pers->use_count--;
	}
	TAILQ_REMOVE(&mpt_tailq, mpt, links);
	return (0);
}

static int
mpt_core_load(struct mpt_personality *pers)
{
	int i;

	/*
	 * Setup core handlers and insert the default handler
	 * into all "empty slots".
	 */
	for (i = 0; i < MPT_NUM_REPLY_HANDLERS; i++) {
		mpt_reply_handlers[i] = mpt_default_reply_handler;
	}

	mpt_reply_handlers[MPT_CBI(MPT_REPLY_HANDLER_EVENTS)] =
	    mpt_event_reply_handler;
	mpt_reply_handlers[MPT_CBI(MPT_REPLY_HANDLER_CONFIG)] =
	    mpt_config_reply_handler;
	mpt_reply_handlers[MPT_CBI(MPT_REPLY_HANDLER_HANDSHAKE)] =
	    mpt_handshake_reply_handler;
	return (0);
}

/*
 * Initialize per-instance driver data and perform
 * initial controller configuration.
 */
static int
mpt_core_attach(struct mpt_softc *mpt)
{
        int val, error;

	LIST_INIT(&mpt->ack_frames);
	/* Put all request buffers on the free list */
	TAILQ_INIT(&mpt->request_pending_list);
	TAILQ_INIT(&mpt->request_free_list);
	TAILQ_INIT(&mpt->request_timeout_list);
	for (val = 0; val < MPT_MAX_LUNS; val++) {
		STAILQ_INIT(&mpt->trt[val].atios);
		STAILQ_INIT(&mpt->trt[val].inots);
	}
	STAILQ_INIT(&mpt->trt_wildcard.atios);
	STAILQ_INIT(&mpt->trt_wildcard.inots);
#ifdef	MPT_TEST_MULTIPATH
	mpt->failure_id = -1;
#endif
	mpt->scsi_tgt_handler_id = MPT_HANDLER_ID_NONE;
	mpt_sysctl_attach(mpt);
	mpt_lprt(mpt, MPT_PRT_DEBUG, "doorbell req = %s\n",
	    mpt_ioc_diag(mpt_read(mpt, MPT_OFFSET_DOORBELL)));

	MPT_LOCK(mpt);
	error = mpt_configure_ioc(mpt, 0, 0);
	MPT_UNLOCK(mpt);

	return (error);
}

static int
mpt_core_enable(struct mpt_softc *mpt)
{

	/*
	 * We enter with the IOC enabled, but async events
	 * not enabled, ports not enabled and interrupts
	 * not enabled.
	 */
	MPT_LOCK(mpt);

	/*
	 * Enable asynchronous event reporting- all personalities
	 * have attached so that they should be able to now field
	 * async events.
	 */
	mpt_send_event_request(mpt, 1);

	/*
	 * Catch any pending interrupts
	 *
	 * This seems to be crucial- otherwise
	 * the portenable below times out.
	 */
	mpt_intr(mpt);

	/*
	 * Enable Interrupts
	 */
	mpt_enable_ints(mpt);

	/*
	 * Catch any pending interrupts
	 *
	 * This seems to be crucial- otherwise
	 * the portenable below times out.
	 */
	mpt_intr(mpt);

	/*
	 * Enable the port.
	 */
	if (mpt_send_port_enable(mpt, 0) != MPT_OK) {
		mpt_prt(mpt, "failed to enable port 0\n");
		MPT_UNLOCK(mpt);
		return (ENXIO);
	}
	MPT_UNLOCK(mpt);
	return (0);
}

static void
mpt_core_shutdown(struct mpt_softc *mpt)
{

	mpt_disable_ints(mpt);
}

static void
mpt_core_detach(struct mpt_softc *mpt)
{
	int val;

	/*
	 * XXX: FREE MEMORY 
	 */
	mpt_disable_ints(mpt);

	/* Make sure no request has pending timeouts. */
	for (val = 0; val < MPT_MAX_REQUESTS(mpt); val++) {
		request_t *req = &mpt->request_pool[val];
		mpt_callout_drain(mpt, &req->callout);
	}

	mpt_dma_buf_free(mpt);
}

static int
mpt_core_unload(struct mpt_personality *pers)
{

	/* Unload is always successful. */
	return (0);
}

#define FW_UPLOAD_REQ_SIZE				\
	(sizeof(MSG_FW_UPLOAD) - sizeof(SGE_MPI_UNION)	\
       + sizeof(FW_UPLOAD_TCSGE) + sizeof(SGE_SIMPLE32))

static int
mpt_upload_fw(struct mpt_softc *mpt)
{
	uint8_t fw_req_buf[FW_UPLOAD_REQ_SIZE];
	MSG_FW_UPLOAD_REPLY fw_reply;
	MSG_FW_UPLOAD *fw_req;
	FW_UPLOAD_TCSGE *tsge;
	SGE_SIMPLE32 *sge;
	uint32_t flags;
	int error;
	
	memset(&fw_req_buf, 0, sizeof(fw_req_buf));
	fw_req = (MSG_FW_UPLOAD *)fw_req_buf;
	fw_req->ImageType = MPI_FW_UPLOAD_ITYPE_FW_IOC_MEM;
	fw_req->Function = MPI_FUNCTION_FW_UPLOAD;
	fw_req->MsgContext = htole32(MPT_REPLY_HANDLER_HANDSHAKE);
	tsge = (FW_UPLOAD_TCSGE *)&fw_req->SGL;
	tsge->DetailsLength = 12;
	tsge->Flags = MPI_SGE_FLAGS_TRANSACTION_ELEMENT;
	tsge->ImageSize = htole32(mpt->fw_image_size);
	sge = (SGE_SIMPLE32 *)(tsge + 1);
	flags = (MPI_SGE_FLAGS_LAST_ELEMENT | MPI_SGE_FLAGS_END_OF_BUFFER
	      | MPI_SGE_FLAGS_END_OF_LIST | MPI_SGE_FLAGS_SIMPLE_ELEMENT
	      | MPI_SGE_FLAGS_32_BIT_ADDRESSING | MPI_SGE_FLAGS_IOC_TO_HOST);
	flags <<= MPI_SGE_FLAGS_SHIFT;
	sge->FlagsLength = htole32(flags | mpt->fw_image_size);
	sge->Address = htole32(mpt->fw_phys);
	bus_dmamap_sync(mpt->fw_dmat, mpt->fw_dmap, BUS_DMASYNC_PREREAD);
	error = mpt_send_handshake_cmd(mpt, sizeof(fw_req_buf), &fw_req_buf);
	if (error)
		return(error);
	error = mpt_recv_handshake_reply(mpt, sizeof(fw_reply), &fw_reply);
	bus_dmamap_sync(mpt->fw_dmat, mpt->fw_dmap, BUS_DMASYNC_POSTREAD);
	return (error);
}

static void
mpt_diag_outsl(struct mpt_softc *mpt, uint32_t addr,
	       uint32_t *data, bus_size_t len)
{
	uint32_t *data_end;

	data_end = data + (roundup2(len, sizeof(uint32_t)) / 4);
	if (mpt->is_sas) {
		pci_enable_io(mpt->dev, SYS_RES_IOPORT);
	}
	mpt_pio_write(mpt, MPT_OFFSET_DIAG_ADDR, addr);
	while (data != data_end) {
		mpt_pio_write(mpt, MPT_OFFSET_DIAG_DATA, *data);
		data++;
	}
	if (mpt->is_sas) {
		pci_disable_io(mpt->dev, SYS_RES_IOPORT);
	}
}

static int
mpt_download_fw(struct mpt_softc *mpt)
{
	MpiFwHeader_t *fw_hdr;
	int error;
	uint32_t ext_offset;
	uint32_t data;

	if (mpt->pci_pio_reg == NULL) {
		mpt_prt(mpt, "No PIO resource!\n");
		return (ENXIO);
	}

	mpt_prt(mpt, "Downloading Firmware - Image Size %d\n",
		mpt->fw_image_size);

	error = mpt_enable_diag_mode(mpt);
	if (error != 0) {
		mpt_prt(mpt, "Could not enter diagnostic mode!\n");
		return (EIO);
	}

	mpt_write(mpt, MPT_OFFSET_DIAGNOSTIC,
		  MPI_DIAG_RW_ENABLE|MPI_DIAG_DISABLE_ARM);

	fw_hdr = (MpiFwHeader_t *)mpt->fw_image;
	bus_dmamap_sync(mpt->fw_dmat, mpt->fw_dmap, BUS_DMASYNC_PREWRITE);
	mpt_diag_outsl(mpt, fw_hdr->LoadStartAddress, (uint32_t*)fw_hdr,
		       fw_hdr->ImageSize);
	bus_dmamap_sync(mpt->fw_dmat, mpt->fw_dmap, BUS_DMASYNC_POSTWRITE);

	ext_offset = fw_hdr->NextImageHeaderOffset;
	while (ext_offset != 0) {
		MpiExtImageHeader_t *ext;

		ext = (MpiExtImageHeader_t *)((uintptr_t)fw_hdr + ext_offset);
		ext_offset = ext->NextImageHeaderOffset;
		bus_dmamap_sync(mpt->fw_dmat, mpt->fw_dmap,
		    BUS_DMASYNC_PREWRITE);
		mpt_diag_outsl(mpt, ext->LoadStartAddress, (uint32_t*)ext,
			       ext->ImageSize);
		bus_dmamap_sync(mpt->fw_dmat, mpt->fw_dmap,
		    BUS_DMASYNC_POSTWRITE);
	}

	if (mpt->is_sas) {
		pci_enable_io(mpt->dev, SYS_RES_IOPORT);
	}
	/* Setup the address to jump to on reset. */
	mpt_pio_write(mpt, MPT_OFFSET_DIAG_ADDR, fw_hdr->IopResetRegAddr);
	mpt_pio_write(mpt, MPT_OFFSET_DIAG_DATA, fw_hdr->IopResetVectorValue);

	/*
	 * The controller sets the "flash bad" status after attempting
	 * to auto-boot from flash.  Clear the status so that the controller
	 * will continue the boot process with our newly installed firmware.
	 */
	mpt_pio_write(mpt, MPT_OFFSET_DIAG_ADDR, MPT_DIAG_MEM_CFG_BASE);
	data = mpt_pio_read(mpt, MPT_OFFSET_DIAG_DATA) | MPT_DIAG_MEM_CFG_BADFL;
	mpt_pio_write(mpt, MPT_OFFSET_DIAG_ADDR, MPT_DIAG_MEM_CFG_BASE);
	mpt_pio_write(mpt, MPT_OFFSET_DIAG_DATA, data);

	if (mpt->is_sas) {
		pci_disable_io(mpt->dev, SYS_RES_IOPORT);
	}

	/*
	 * Re-enable the processor and clear the boot halt flag.
	 */
	data = mpt_read(mpt, MPT_OFFSET_DIAGNOSTIC);
	data &= ~(MPI_DIAG_PREVENT_IOC_BOOT|MPI_DIAG_DISABLE_ARM);
	mpt_write(mpt, MPT_OFFSET_DIAGNOSTIC, data);

	mpt_disable_diag_mode(mpt);
	return (0);
}

static int
mpt_dma_buf_alloc(struct mpt_softc *mpt)
{
	struct mpt_map_info mi;
	uint8_t *vptr;
	uint32_t pptr, end;
	int i, error;

	/* Create a child tag for data buffers */
	if (mpt_dma_tag_create(mpt, mpt->parent_dmat, 1,
	    0, BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR,
	    NULL, NULL, (mpt->max_cam_seg_cnt - 1) * PAGE_SIZE,
	    mpt->max_cam_seg_cnt, BUS_SPACE_MAXSIZE_32BIT, 0,
	    &mpt->buffer_dmat) != 0) {
		mpt_prt(mpt, "cannot create a dma tag for data buffers\n");
		return (1);
	}

	/* Create a child tag for request buffers */
	if (mpt_dma_tag_create(mpt, mpt->parent_dmat, PAGE_SIZE, 0,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR,
	    NULL, NULL, MPT_REQ_MEM_SIZE(mpt), 1, BUS_SPACE_MAXSIZE_32BIT, 0,
	    &mpt->request_dmat) != 0) {
		mpt_prt(mpt, "cannot create a dma tag for requests\n");
		return (1);
	}

	/* Allocate some DMA accessible memory for requests */
	if (bus_dmamem_alloc(mpt->request_dmat, (void **)&mpt->request,
	    BUS_DMA_NOWAIT | BUS_DMA_COHERENT, &mpt->request_dmap) != 0) {
		mpt_prt(mpt, "cannot allocate %d bytes of request memory\n",
		    MPT_REQ_MEM_SIZE(mpt));
		return (1);
	}

	mi.mpt = mpt;
	mi.error = 0;

	/* Load and lock it into "bus space" */
	bus_dmamap_load(mpt->request_dmat, mpt->request_dmap, mpt->request,
	    MPT_REQ_MEM_SIZE(mpt), mpt_map_rquest, &mi, 0);

	if (mi.error) {
		mpt_prt(mpt, "error %d loading dma map for DMA request queue\n",
		    mi.error);
		return (1);
	}
	mpt->request_phys = mi.phys;

	/*
	 * Now create per-request dma maps
	 */
	i = 0;
	pptr =  mpt->request_phys;
	vptr =  mpt->request;
	end = pptr + MPT_REQ_MEM_SIZE(mpt);
	while(pptr < end) {
		request_t *req = &mpt->request_pool[i];
		req->index = i++;

		/* Store location of Request Data */
		req->req_pbuf = pptr;
		req->req_vbuf = vptr;

		pptr += MPT_REQUEST_AREA;
		vptr += MPT_REQUEST_AREA;

		req->sense_pbuf = (pptr - MPT_SENSE_SIZE);
		req->sense_vbuf = (vptr - MPT_SENSE_SIZE);

		error = bus_dmamap_create(mpt->buffer_dmat, 0, &req->dmap);
		if (error) {
			mpt_prt(mpt, "error %d creating per-cmd DMA maps\n",
			    error);
			return (1);
		}
	}

	return (0);
}

static void
mpt_dma_buf_free(struct mpt_softc *mpt)
{
	int i;

	if (mpt->request_dmat == 0) {
		mpt_lprt(mpt, MPT_PRT_DEBUG, "already released dma memory\n");
		return;
	}
	for (i = 0; i < MPT_MAX_REQUESTS(mpt); i++) {
		bus_dmamap_destroy(mpt->buffer_dmat, mpt->request_pool[i].dmap);
	}
	bus_dmamap_unload(mpt->request_dmat, mpt->request_dmap);
	bus_dmamem_free(mpt->request_dmat, mpt->request, mpt->request_dmap);
	bus_dma_tag_destroy(mpt->request_dmat);
	mpt->request_dmat = 0;
	bus_dma_tag_destroy(mpt->buffer_dmat);
}

/*
 * Allocate/Initialize data structures for the controller.  Called
 * once at instance startup.
 */
static int
mpt_configure_ioc(struct mpt_softc *mpt, int tn, int needreset)
{
	PTR_MSG_PORT_FACTS_REPLY pfp;
	int error, port, val;
	size_t len;

	if (tn == MPT_MAX_TRYS) {
		return (-1);
	}

	/*
	 * No need to reset if the IOC is already in the READY state.
	 *
	 * Force reset if initialization failed previously.
	 * Note that a hard_reset of the second channel of a '929
	 * will stop operation of the first channel.  Hopefully, if the
	 * first channel is ok, the second will not require a hard
	 * reset.
	 */
	if (needreset || MPT_STATE(mpt_rd_db(mpt)) != MPT_DB_STATE_READY) {
		if (mpt_reset(mpt, FALSE) != MPT_OK) {
			return (mpt_configure_ioc(mpt, tn++, 1));
		}
		needreset = 0;
	}

	if (mpt_get_iocfacts(mpt, &mpt->ioc_facts) != MPT_OK) {
		mpt_prt(mpt, "mpt_get_iocfacts failed\n");
		return (mpt_configure_ioc(mpt, tn++, 1));
	}
	mpt2host_iocfacts_reply(&mpt->ioc_facts);

	mpt_prt(mpt, "MPI Version=%d.%d.%d.%d\n",
	    mpt->ioc_facts.MsgVersion >> 8,
	    mpt->ioc_facts.MsgVersion & 0xFF,
	    mpt->ioc_facts.HeaderVersion >> 8,
	    mpt->ioc_facts.HeaderVersion & 0xFF);

	/*
	 * Now that we know request frame size, we can calculate
	 * the actual (reasonable) segment limit for read/write I/O.
	 *
	 * This limit is constrained by:
	 *
	 *  + The size of each area we allocate per command (and how
	 *    many chain segments we can fit into it).
	 *  + The total number of areas we've set up.
	 *  + The actual chain depth the card will allow.
	 *
	 * The first area's segment count is limited by the I/O request
	 * at the head of it. We cannot allocate realistically more
	 * than MPT_MAX_REQUESTS areas. Therefore, to account for both
	 * conditions, we'll just start out with MPT_MAX_REQUESTS-2.
	 *
	 */
	/* total number of request areas we (can) allocate */
	mpt->max_seg_cnt = MPT_MAX_REQUESTS(mpt) - 2;

	/* converted to the number of chain areas possible */
	mpt->max_seg_cnt *= MPT_NRFM(mpt);

	/* limited by the number of chain areas the card will support */
	if (mpt->max_seg_cnt > mpt->ioc_facts.MaxChainDepth) {
		mpt_lprt(mpt, MPT_PRT_INFO,
		    "chain depth limited to %u (from %u)\n",
		    mpt->ioc_facts.MaxChainDepth, mpt->max_seg_cnt);
		mpt->max_seg_cnt = mpt->ioc_facts.MaxChainDepth;
	}

	/* converted to the number of simple sges in chain segments. */
	mpt->max_seg_cnt *= (MPT_NSGL(mpt) - 1);

	/*
	 * Use this as the basis for reporting the maximum I/O size to CAM.
	 */
	mpt->max_cam_seg_cnt = min(mpt->max_seg_cnt, (MAXPHYS / PAGE_SIZE) + 1);

	/* XXX Lame Locking! */
	MPT_UNLOCK(mpt);
	error = mpt_dma_buf_alloc(mpt);
	MPT_LOCK(mpt);

	if (error != 0) {
		mpt_prt(mpt, "mpt_dma_buf_alloc() failed!\n");
		return (EIO);
	}

	for (val = 0; val < MPT_MAX_REQUESTS(mpt); val++) {
		request_t *req = &mpt->request_pool[val];
		req->state = REQ_STATE_ALLOCATED;
		mpt_callout_init(mpt, &req->callout);
		mpt_free_request(mpt, req);
	}

	mpt_lprt(mpt, MPT_PRT_INFO, "Maximum Segment Count: %u, Maximum "
		 "CAM Segment Count: %u\n", mpt->max_seg_cnt,
		 mpt->max_cam_seg_cnt);

	mpt_lprt(mpt, MPT_PRT_INFO, "MsgLength=%u IOCNumber = %d\n",
	    mpt->ioc_facts.MsgLength, mpt->ioc_facts.IOCNumber);
	mpt_lprt(mpt, MPT_PRT_INFO,
	    "IOCFACTS: GlobalCredits=%d BlockSize=%u bytes "
	    "Request Frame Size %u bytes Max Chain Depth %u\n",
	    mpt->ioc_facts.GlobalCredits, mpt->ioc_facts.BlockSize,
	    mpt->ioc_facts.RequestFrameSize << 2,
	    mpt->ioc_facts.MaxChainDepth);
	mpt_lprt(mpt, MPT_PRT_INFO, "IOCFACTS: Num Ports %d, FWImageSize %d, "
	    "Flags=%#x\n", mpt->ioc_facts.NumberOfPorts,
	    mpt->ioc_facts.FWImageSize, mpt->ioc_facts.Flags);

	len = mpt->ioc_facts.NumberOfPorts * sizeof (MSG_PORT_FACTS_REPLY);
	mpt->port_facts = malloc(len, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (mpt->port_facts == NULL) {
		mpt_prt(mpt, "unable to allocate memory for port facts\n");
		return (ENOMEM);
	}


	if ((mpt->ioc_facts.Flags & MPI_IOCFACTS_FLAGS_FW_DOWNLOAD_BOOT) &&
	    (mpt->fw_uploaded == 0)) {
		struct mpt_map_info mi;

		/*
		 * In some configurations, the IOC's firmware is
		 * stored in a shared piece of system NVRAM that
		 * is only accessible via the BIOS.  In this
		 * case, the firmware keeps a copy of firmware in
		 * RAM until the OS driver retrieves it.  Once
		 * retrieved, we are responsible for re-downloading
		 * the firmware after any hard-reset.
		 */
		MPT_UNLOCK(mpt);
		mpt->fw_image_size = mpt->ioc_facts.FWImageSize;
		error = mpt_dma_tag_create(mpt, mpt->parent_dmat, 1, 0,
		    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
		    mpt->fw_image_size, 1, mpt->fw_image_size, 0,
		    &mpt->fw_dmat);
		if (error != 0) {
			mpt_prt(mpt, "cannot create firmware dma tag\n");
			MPT_LOCK(mpt);
			return (ENOMEM);
		}
		error = bus_dmamem_alloc(mpt->fw_dmat,
		    (void **)&mpt->fw_image, BUS_DMA_NOWAIT |
		    BUS_DMA_COHERENT, &mpt->fw_dmap);
		if (error != 0) {
			mpt_prt(mpt, "cannot allocate firmware memory\n");
			bus_dma_tag_destroy(mpt->fw_dmat);
			MPT_LOCK(mpt);
			return (ENOMEM);
		}
		mi.mpt = mpt;
		mi.error = 0;
		bus_dmamap_load(mpt->fw_dmat, mpt->fw_dmap,
		    mpt->fw_image, mpt->fw_image_size, mpt_map_rquest, &mi, 0);
		mpt->fw_phys = mi.phys;

		MPT_LOCK(mpt);
		error = mpt_upload_fw(mpt);
		if (error != 0) {
			mpt_prt(mpt, "firmware upload failed.\n");
			bus_dmamap_unload(mpt->fw_dmat, mpt->fw_dmap);
			bus_dmamem_free(mpt->fw_dmat, mpt->fw_image,
			    mpt->fw_dmap);
			bus_dma_tag_destroy(mpt->fw_dmat);
			mpt->fw_image = NULL;
			return (EIO);
		}
		mpt->fw_uploaded = 1;
	}

	for (port = 0; port < mpt->ioc_facts.NumberOfPorts; port++) {
		pfp = &mpt->port_facts[port];
		error = mpt_get_portfacts(mpt, 0, pfp);
		if (error != MPT_OK) {
			mpt_prt(mpt,
			    "mpt_get_portfacts on port %d failed\n", port);
			free(mpt->port_facts, M_DEVBUF);
			mpt->port_facts = NULL;
			return (mpt_configure_ioc(mpt, tn++, 1));
		}
		mpt2host_portfacts_reply(pfp);

		if (port > 0) {
			error = MPT_PRT_INFO;
		} else {
			error = MPT_PRT_DEBUG;
		}
		mpt_lprt(mpt, error,
		    "PORTFACTS[%d]: Type %x PFlags %x IID %d MaxDev %d\n",
		    port, pfp->PortType, pfp->ProtocolFlags, pfp->PortSCSIID,
		    pfp->MaxDevices);

	}

	/*
	 * XXX: Not yet supporting more than port 0
	 */
	pfp = &mpt->port_facts[0];
	if (pfp->PortType == MPI_PORTFACTS_PORTTYPE_FC) {
		mpt->is_fc = 1;
		mpt->is_sas = 0;
		mpt->is_spi = 0;
	} else if (pfp->PortType == MPI_PORTFACTS_PORTTYPE_SAS) {
		mpt->is_fc = 0;
		mpt->is_sas = 1;
		mpt->is_spi = 0;
	} else if (pfp->PortType == MPI_PORTFACTS_PORTTYPE_SCSI) {
		mpt->is_fc = 0;
		mpt->is_sas = 0;
		mpt->is_spi = 1;
		if (mpt->mpt_ini_id == MPT_INI_ID_NONE)
			mpt->mpt_ini_id = pfp->PortSCSIID;
	} else if (pfp->PortType == MPI_PORTFACTS_PORTTYPE_ISCSI) {
		mpt_prt(mpt, "iSCSI not supported yet\n");
		return (ENXIO);
	} else if (pfp->PortType == MPI_PORTFACTS_PORTTYPE_INACTIVE) {
		mpt_prt(mpt, "Inactive Port\n");
		return (ENXIO);
	} else {
		mpt_prt(mpt, "unknown Port Type %#x\n", pfp->PortType);
		return (ENXIO);
	}

	/*
	 * Set our role with what this port supports.
	 *
	 * Note this might be changed later in different modules
	 * if this is different from what is wanted.
	 */
	mpt->role = MPT_ROLE_NONE;
	if (pfp->ProtocolFlags & MPI_PORTFACTS_PROTOCOL_INITIATOR) {
		mpt->role |= MPT_ROLE_INITIATOR;
	}
	if (pfp->ProtocolFlags & MPI_PORTFACTS_PROTOCOL_TARGET) {
		mpt->role |= MPT_ROLE_TARGET;
	}

	/*
	 * Enable the IOC
	 */
	if (mpt_enable_ioc(mpt, 1) != MPT_OK) {
		mpt_prt(mpt, "unable to initialize IOC\n");
		return (ENXIO);
	}

	/*
	 * Read IOC configuration information.
	 *
	 * We need this to determine whether or not we have certain
	 * settings for Integrated Mirroring (e.g.).
	 */
	mpt_read_config_info_ioc(mpt);

	return (0);
}

static int
mpt_enable_ioc(struct mpt_softc *mpt, int portenable)
{
	uint32_t pptr;
	int val;

	if (mpt_send_ioc_init(mpt, MPI_WHOINIT_HOST_DRIVER) != MPT_OK) {
		mpt_prt(mpt, "mpt_send_ioc_init failed\n");
		return (EIO);
	}

	mpt_lprt(mpt, MPT_PRT_DEBUG, "mpt_send_ioc_init ok\n");

	if (mpt_wait_state(mpt, MPT_DB_STATE_RUNNING) != MPT_OK) {
		mpt_prt(mpt, "IOC failed to go to run state\n");
		return (ENXIO);
	}
	mpt_lprt(mpt, MPT_PRT_DEBUG, "IOC now at RUNSTATE\n");

	/*
	 * Give it reply buffers
	 *
	 * Do *not* exceed global credits.
	 */
	for (val = 0, pptr = mpt->reply_phys;
	    (pptr + MPT_REPLY_SIZE) < (mpt->reply_phys + PAGE_SIZE);
	     pptr += MPT_REPLY_SIZE) {
		mpt_free_reply(mpt, pptr);
		if (++val == mpt->ioc_facts.GlobalCredits - 1)
			break;
	}


	/*
	 * Enable the port if asked. This is only done if we're resetting
	 * the IOC after initial startup.
	 */
	if (portenable) {
		/*
		 * Enable asynchronous event reporting
		 */
		mpt_send_event_request(mpt, 1);

		if (mpt_send_port_enable(mpt, 0) != MPT_OK) {
			mpt_prt(mpt, "%s: failed to enable port 0\n", __func__);
			return (ENXIO);
		}
	}
	return (MPT_OK);
}

/*
 * Endian Conversion Functions- only used on Big Endian machines
 */
#if	_BYTE_ORDER == _BIG_ENDIAN
void
mpt2host_sge_simple_union(SGE_SIMPLE_UNION *sge)
{

	MPT_2_HOST32(sge, FlagsLength);
	MPT_2_HOST32(sge, u.Address64.Low);
	MPT_2_HOST32(sge, u.Address64.High);
}

void
mpt2host_iocfacts_reply(MSG_IOC_FACTS_REPLY *rp)
{

	MPT_2_HOST16(rp, MsgVersion);
	MPT_2_HOST16(rp, HeaderVersion);
	MPT_2_HOST32(rp, MsgContext);
	MPT_2_HOST16(rp, IOCExceptions);
	MPT_2_HOST16(rp, IOCStatus);
	MPT_2_HOST32(rp, IOCLogInfo);
	MPT_2_HOST16(rp, ReplyQueueDepth);
	MPT_2_HOST16(rp, RequestFrameSize);
	MPT_2_HOST16(rp, Reserved_0101_FWVersion);
	MPT_2_HOST16(rp, ProductID);
	MPT_2_HOST32(rp, CurrentHostMfaHighAddr);
	MPT_2_HOST16(rp, GlobalCredits);
	MPT_2_HOST32(rp, CurrentSenseBufferHighAddr);
	MPT_2_HOST16(rp, CurReplyFrameSize);
	MPT_2_HOST32(rp, FWImageSize);
	MPT_2_HOST32(rp, IOCCapabilities);
	MPT_2_HOST32(rp, FWVersion.Word);
	MPT_2_HOST16(rp, HighPriorityQueueDepth);
	MPT_2_HOST16(rp, Reserved2);
	mpt2host_sge_simple_union(&rp->HostPageBufferSGE);
	MPT_2_HOST32(rp, ReplyFifoHostSignalingAddr);
}

void
mpt2host_portfacts_reply(MSG_PORT_FACTS_REPLY *pfp)
{

	MPT_2_HOST16(pfp, Reserved);
	MPT_2_HOST16(pfp, Reserved1);
	MPT_2_HOST32(pfp, MsgContext);
	MPT_2_HOST16(pfp, Reserved2);
	MPT_2_HOST16(pfp, IOCStatus);
	MPT_2_HOST32(pfp, IOCLogInfo);
	MPT_2_HOST16(pfp, MaxDevices);
	MPT_2_HOST16(pfp, PortSCSIID);
	MPT_2_HOST16(pfp, ProtocolFlags);
	MPT_2_HOST16(pfp, MaxPostedCmdBuffers);
	MPT_2_HOST16(pfp, MaxPersistentIDs);
	MPT_2_HOST16(pfp, MaxLanBuckets);
	MPT_2_HOST16(pfp, Reserved4);
	MPT_2_HOST32(pfp, Reserved5);
}

void
mpt2host_config_page_ioc2(CONFIG_PAGE_IOC_2 *ioc2)
{
	int i;

	MPT_2_HOST32(ioc2, CapabilitiesFlags);
	for (i = 0; i < MPI_IOC_PAGE_2_RAID_VOLUME_MAX; i++) {
		MPT_2_HOST16(ioc2, RaidVolume[i].Reserved3);
	}
}

void
mpt2host_config_page_ioc3(CONFIG_PAGE_IOC_3 *ioc3)
{

	MPT_2_HOST16(ioc3, Reserved2);
}

void
mpt2host_config_page_scsi_port_0(CONFIG_PAGE_SCSI_PORT_0 *sp0)
{

	MPT_2_HOST32(sp0, Capabilities);
	MPT_2_HOST32(sp0, PhysicalInterface);
}

void
mpt2host_config_page_scsi_port_1(CONFIG_PAGE_SCSI_PORT_1 *sp1)
{

	MPT_2_HOST32(sp1, Configuration);
	MPT_2_HOST32(sp1, OnBusTimerValue);
	MPT_2_HOST16(sp1, IDConfig);
}

void
host2mpt_config_page_scsi_port_1(CONFIG_PAGE_SCSI_PORT_1 *sp1)
{

	HOST_2_MPT32(sp1, Configuration);
	HOST_2_MPT32(sp1, OnBusTimerValue);
	HOST_2_MPT16(sp1, IDConfig);
}

void
mpt2host_config_page_scsi_port_2(CONFIG_PAGE_SCSI_PORT_2 *sp2)
{
	int i;

	MPT_2_HOST32(sp2, PortFlags);
	MPT_2_HOST32(sp2, PortSettings);
	for (i = 0; i < sizeof(sp2->DeviceSettings) /
	    sizeof(*sp2->DeviceSettings); i++) {
		MPT_2_HOST16(sp2, DeviceSettings[i].DeviceFlags);
	}
}

void
mpt2host_config_page_scsi_device_0(CONFIG_PAGE_SCSI_DEVICE_0 *sd0)
{

	MPT_2_HOST32(sd0, NegotiatedParameters);
	MPT_2_HOST32(sd0, Information);
}

void
mpt2host_config_page_scsi_device_1(CONFIG_PAGE_SCSI_DEVICE_1 *sd1)
{

	MPT_2_HOST32(sd1, RequestedParameters);
	MPT_2_HOST32(sd1, Reserved);
	MPT_2_HOST32(sd1, Configuration);
}

void
host2mpt_config_page_scsi_device_1(CONFIG_PAGE_SCSI_DEVICE_1 *sd1)
{

	HOST_2_MPT32(sd1, RequestedParameters);
	HOST_2_MPT32(sd1, Reserved);
	HOST_2_MPT32(sd1, Configuration);
}

void
mpt2host_config_page_fc_port_0(CONFIG_PAGE_FC_PORT_0 *fp0)
{

	MPT_2_HOST32(fp0, Flags);
	MPT_2_HOST32(fp0, PortIdentifier);
	MPT_2_HOST32(fp0, WWNN.Low);
	MPT_2_HOST32(fp0, WWNN.High);
	MPT_2_HOST32(fp0, WWPN.Low);
	MPT_2_HOST32(fp0, WWPN.High);
	MPT_2_HOST32(fp0, SupportedServiceClass);
	MPT_2_HOST32(fp0, SupportedSpeeds);
	MPT_2_HOST32(fp0, CurrentSpeed);
	MPT_2_HOST32(fp0, MaxFrameSize);
	MPT_2_HOST32(fp0, FabricWWNN.Low);
	MPT_2_HOST32(fp0, FabricWWNN.High);
	MPT_2_HOST32(fp0, FabricWWPN.Low);
	MPT_2_HOST32(fp0, FabricWWPN.High);
	MPT_2_HOST32(fp0, DiscoveredPortsCount);
	MPT_2_HOST32(fp0, MaxInitiators);
}

void
mpt2host_config_page_fc_port_1(CONFIG_PAGE_FC_PORT_1 *fp1)
{

	MPT_2_HOST32(fp1, Flags);
	MPT_2_HOST32(fp1, NoSEEPROMWWNN.Low);
	MPT_2_HOST32(fp1, NoSEEPROMWWNN.High);
	MPT_2_HOST32(fp1, NoSEEPROMWWPN.Low);
	MPT_2_HOST32(fp1, NoSEEPROMWWPN.High);
}

void
host2mpt_config_page_fc_port_1(CONFIG_PAGE_FC_PORT_1 *fp1)
{

	HOST_2_MPT32(fp1, Flags);
	HOST_2_MPT32(fp1, NoSEEPROMWWNN.Low);
	HOST_2_MPT32(fp1, NoSEEPROMWWNN.High);
	HOST_2_MPT32(fp1, NoSEEPROMWWPN.Low);
	HOST_2_MPT32(fp1, NoSEEPROMWWPN.High);
}

void
mpt2host_config_page_raid_vol_0(CONFIG_PAGE_RAID_VOL_0 *volp)
{
	int i;

	MPT_2_HOST16(volp, VolumeStatus.Reserved);
	MPT_2_HOST16(volp, VolumeSettings.Settings);
	MPT_2_HOST32(volp, MaxLBA);
	MPT_2_HOST32(volp, MaxLBAHigh);
	MPT_2_HOST32(volp, StripeSize);
	MPT_2_HOST32(volp, Reserved2);
	MPT_2_HOST32(volp, Reserved3);
	for (i = 0; i < MPI_RAID_VOL_PAGE_0_PHYSDISK_MAX; i++) {
		MPT_2_HOST16(volp, PhysDisk[i].Reserved);
	}
}

void
mpt2host_config_page_raid_phys_disk_0(CONFIG_PAGE_RAID_PHYS_DISK_0 *rpd0)
{

	MPT_2_HOST32(rpd0, Reserved1);
	MPT_2_HOST16(rpd0, PhysDiskStatus.Reserved);
	MPT_2_HOST32(rpd0, MaxLBA);
	MPT_2_HOST16(rpd0, ErrorData.Reserved);
	MPT_2_HOST16(rpd0, ErrorData.ErrorCount);
	MPT_2_HOST16(rpd0, ErrorData.SmartCount);
}

void
mpt2host_mpi_raid_vol_indicator(MPI_RAID_VOL_INDICATOR *vi)
{

	MPT_2_HOST16(vi, TotalBlocks.High);
	MPT_2_HOST16(vi, TotalBlocks.Low);
	MPT_2_HOST16(vi, BlocksRemaining.High);
	MPT_2_HOST16(vi, BlocksRemaining.Low);
}
#endif
