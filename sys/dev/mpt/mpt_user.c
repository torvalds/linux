/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2008 Yahoo!, Inc.
 * All rights reserved.
 * Written by: John Baldwin <jhb@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 * LSI MPT-Fusion Host Adapter FreeBSD userland interface
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/ioccom.h>
#include <sys/mpt_ioctl.h>

#include <dev/mpt/mpt.h>

struct mpt_user_raid_action_result {
	uint32_t	volume_status;
	uint32_t	action_data[4];
	uint16_t	action_status;
};

struct mpt_page_memory {
	bus_dma_tag_t	tag;
	bus_dmamap_t	map;
	bus_addr_t	paddr;
	void		*vaddr;
};

static mpt_probe_handler_t	mpt_user_probe;
static mpt_attach_handler_t	mpt_user_attach;
static mpt_enable_handler_t	mpt_user_enable;
static mpt_ready_handler_t	mpt_user_ready;
static mpt_event_handler_t	mpt_user_event;
static mpt_reset_handler_t	mpt_user_reset;
static mpt_detach_handler_t	mpt_user_detach;

static struct mpt_personality mpt_user_personality = {
	.name		= "mpt_user",
	.probe		= mpt_user_probe,
	.attach		= mpt_user_attach,
	.enable		= mpt_user_enable,
	.ready		= mpt_user_ready,
	.event		= mpt_user_event,
	.reset		= mpt_user_reset,
	.detach		= mpt_user_detach,
};

DECLARE_MPT_PERSONALITY(mpt_user, SI_ORDER_SECOND);

static mpt_reply_handler_t	mpt_user_reply_handler;

static d_open_t		mpt_open;
static d_close_t	mpt_close;
static d_ioctl_t	mpt_ioctl;

static struct cdevsw mpt_cdevsw = {
	.d_version =	D_VERSION,
	.d_flags =	0,
	.d_open =	mpt_open,
	.d_close =	mpt_close,
	.d_ioctl =	mpt_ioctl,
	.d_name =	"mpt",
};

static MALLOC_DEFINE(M_MPTUSER, "mpt_user", "Buffers for mpt(4) ioctls");

static uint32_t user_handler_id = MPT_HANDLER_ID_NONE;

static int
mpt_user_probe(struct mpt_softc *mpt)
{

	/* Attach to every controller. */
	return (0);
}

static int
mpt_user_attach(struct mpt_softc *mpt)
{
	mpt_handler_t handler;
	int error, unit;

	MPT_LOCK(mpt);
	handler.reply_handler = mpt_user_reply_handler;
	error = mpt_register_handler(mpt, MPT_HANDLER_REPLY, handler,
				     &user_handler_id);
	MPT_UNLOCK(mpt);
	if (error != 0) {
		mpt_prt(mpt, "Unable to register user handler!\n");
		return (error);
	}
	unit = device_get_unit(mpt->dev);
	mpt->cdev = make_dev(&mpt_cdevsw, unit, UID_ROOT, GID_OPERATOR, 0640,
	    "mpt%d", unit);
	if (mpt->cdev == NULL) {
		MPT_LOCK(mpt);
		mpt_deregister_handler(mpt, MPT_HANDLER_REPLY, handler,
		    user_handler_id);
		MPT_UNLOCK(mpt);
		return (ENOMEM);
	}
	mpt->cdev->si_drv1 = mpt;
	return (0);
}

static int
mpt_user_enable(struct mpt_softc *mpt)
{

	return (0);
}

static void
mpt_user_ready(struct mpt_softc *mpt)
{

}

static int
mpt_user_event(struct mpt_softc *mpt, request_t *req,
    MSG_EVENT_NOTIFY_REPLY *msg)
{

	/* Someday we may want to let a user daemon listen for events? */
	return (0);
}

static void
mpt_user_reset(struct mpt_softc *mpt, int type)
{

}

static void
mpt_user_detach(struct mpt_softc *mpt)
{
	mpt_handler_t handler;

	/* XXX: do a purge of pending requests? */
	destroy_dev(mpt->cdev);

	MPT_LOCK(mpt);
	handler.reply_handler = mpt_user_reply_handler;
	mpt_deregister_handler(mpt, MPT_HANDLER_REPLY, handler,
	    user_handler_id);
	MPT_UNLOCK(mpt);
}

static int
mpt_open(struct cdev *dev, int flags, int fmt, struct thread *td)
{

	return (0);
}

static int
mpt_close(struct cdev *dev, int flags, int fmt, struct thread *td)
{

	return (0);
}

static int
mpt_alloc_buffer(struct mpt_softc *mpt, struct mpt_page_memory *page_mem,
    size_t len)
{
	struct mpt_map_info mi;
	int error;

	page_mem->vaddr = NULL;

	/* Limit requests to 16M. */
	if (len > 16 * 1024 * 1024)
		return (ENOSPC);
	error = mpt_dma_tag_create(mpt, mpt->parent_dmat, 1, 0,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	    len, 1, len, 0, &page_mem->tag);
	if (error)
		return (error);
	error = bus_dmamem_alloc(page_mem->tag, &page_mem->vaddr,
	    BUS_DMA_NOWAIT | BUS_DMA_COHERENT, &page_mem->map);
	if (error) {
		bus_dma_tag_destroy(page_mem->tag);
		return (error);
	}
	mi.mpt = mpt;
	error = bus_dmamap_load(page_mem->tag, page_mem->map, page_mem->vaddr,
	    len, mpt_map_rquest, &mi, BUS_DMA_NOWAIT);
	if (error == 0)
		error = mi.error;
	if (error) {
		bus_dmamem_free(page_mem->tag, page_mem->vaddr, page_mem->map);
		bus_dma_tag_destroy(page_mem->tag);
		page_mem->vaddr = NULL;
		return (error);
	}
	page_mem->paddr = mi.phys;
	return (0);
}

static void
mpt_free_buffer(struct mpt_page_memory *page_mem)
{

	if (page_mem->vaddr == NULL)
		return;
	bus_dmamap_unload(page_mem->tag, page_mem->map);
	bus_dmamem_free(page_mem->tag, page_mem->vaddr, page_mem->map);
	bus_dma_tag_destroy(page_mem->tag);
	page_mem->vaddr = NULL;
}

static int
mpt_user_read_cfg_header(struct mpt_softc *mpt,
    struct mpt_cfg_page_req *page_req)
{
	request_t  *req;
	cfgparms_t params;
	MSG_CONFIG *cfgp;
	int	    error;

	req = mpt_get_request(mpt, TRUE);
	if (req == NULL) {
		mpt_prt(mpt, "mpt_user_read_cfg_header: Get request failed!\n");
		return (ENOMEM);
	}

	params.Action = MPI_CONFIG_ACTION_PAGE_HEADER;
	params.PageVersion = 0;
	params.PageLength = 0;
	params.PageNumber = page_req->header.PageNumber;
	params.PageType = page_req->header.PageType;
	params.PageAddress = le32toh(page_req->page_address);
	error = mpt_issue_cfg_req(mpt, req, &params, /*addr*/0, /*len*/0,
				  TRUE, 5000);
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

	page_req->ioc_status = htole16(req->IOCStatus);
	if ((req->IOCStatus & MPI_IOCSTATUS_MASK) == MPI_IOCSTATUS_SUCCESS) {
		cfgp = req->req_vbuf;
		bcopy(&cfgp->Header, &page_req->header,
		    sizeof(page_req->header));
	}
	mpt_free_request(mpt, req);
	return (0);
}

static int
mpt_user_read_cfg_page(struct mpt_softc *mpt, struct mpt_cfg_page_req *page_req,
    struct mpt_page_memory *mpt_page)
{
	CONFIG_PAGE_HEADER *hdr;
	request_t    *req;
	cfgparms_t    params;
	int	      error;

	req = mpt_get_request(mpt, TRUE);
	if (req == NULL) {
		mpt_prt(mpt, "mpt_user_read_cfg_page: Get request failed!\n");
		return (ENOMEM);
	}

	hdr = mpt_page->vaddr;
	params.Action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;
	params.PageVersion = hdr->PageVersion;
	params.PageLength = hdr->PageLength;
	params.PageNumber = hdr->PageNumber;
	params.PageType = hdr->PageType & MPI_CONFIG_PAGETYPE_MASK;
	params.PageAddress = le32toh(page_req->page_address);
	bus_dmamap_sync(mpt_page->tag, mpt_page->map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	error = mpt_issue_cfg_req(mpt, req, &params, mpt_page->paddr,
	    le32toh(page_req->len), TRUE, 5000);
	if (error != 0) {
		mpt_prt(mpt, "mpt_user_read_cfg_page timed out\n");
		return (ETIMEDOUT);
	}

	page_req->ioc_status = htole16(req->IOCStatus);
	if ((req->IOCStatus & MPI_IOCSTATUS_MASK) == MPI_IOCSTATUS_SUCCESS)
		bus_dmamap_sync(mpt_page->tag, mpt_page->map,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	mpt_free_request(mpt, req);
	return (0);
}

static int
mpt_user_read_extcfg_header(struct mpt_softc *mpt,
    struct mpt_ext_cfg_page_req *ext_page_req)
{
	request_t  *req;
	cfgparms_t params;
	MSG_CONFIG_REPLY *cfgp;
	int	    error;

	req = mpt_get_request(mpt, TRUE);
	if (req == NULL) {
		mpt_prt(mpt, "mpt_user_read_extcfg_header: Get request failed!\n");
		return (ENOMEM);
	}

	params.Action = MPI_CONFIG_ACTION_PAGE_HEADER;
	params.PageVersion = ext_page_req->header.PageVersion;
	params.PageLength = 0;
	params.PageNumber = ext_page_req->header.PageNumber;
	params.PageType = MPI_CONFIG_PAGETYPE_EXTENDED;
	params.PageAddress = le32toh(ext_page_req->page_address);
	params.ExtPageType = ext_page_req->header.ExtPageType;
	params.ExtPageLength = 0;
	error = mpt_issue_cfg_req(mpt, req, &params, /*addr*/0, /*len*/0,
				  TRUE, 5000);
	if (error != 0) {
		/*
		 * Leave the request. Without resetting the chip, it's
		 * still owned by it and we'll just get into trouble
		 * freeing it now. Mark it as abandoned so that if it
		 * shows up later it can be freed.
		 */
		mpt_prt(mpt, "mpt_user_read_extcfg_header timed out\n");
		return (ETIMEDOUT);
	}

	ext_page_req->ioc_status = htole16(req->IOCStatus);
	if ((req->IOCStatus & MPI_IOCSTATUS_MASK) == MPI_IOCSTATUS_SUCCESS) {
		cfgp = req->req_vbuf;
		ext_page_req->header.PageVersion = cfgp->Header.PageVersion;
		ext_page_req->header.PageNumber = cfgp->Header.PageNumber;
		ext_page_req->header.PageType = cfgp->Header.PageType;
		ext_page_req->header.ExtPageLength = cfgp->ExtPageLength;
		ext_page_req->header.ExtPageType = cfgp->ExtPageType;
	}
	mpt_free_request(mpt, req);
	return (0);
}

static int
mpt_user_read_extcfg_page(struct mpt_softc *mpt,
    struct mpt_ext_cfg_page_req *ext_page_req, struct mpt_page_memory *mpt_page)
{
	CONFIG_EXTENDED_PAGE_HEADER *hdr;
	request_t    *req;
	cfgparms_t    params;
	int	      error;

	req = mpt_get_request(mpt, TRUE);
	if (req == NULL) {
		mpt_prt(mpt, "mpt_user_read_extcfg_page: Get request failed!\n");
		return (ENOMEM);
	}

	hdr = mpt_page->vaddr;
	params.Action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;
	params.PageVersion = hdr->PageVersion;
	params.PageLength = 0;
	params.PageNumber = hdr->PageNumber;
	params.PageType = MPI_CONFIG_PAGETYPE_EXTENDED;
	params.PageAddress = le32toh(ext_page_req->page_address);
	params.ExtPageType = hdr->ExtPageType;
	params.ExtPageLength = hdr->ExtPageLength;
	bus_dmamap_sync(mpt_page->tag, mpt_page->map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	error = mpt_issue_cfg_req(mpt, req, &params, mpt_page->paddr,
	    le32toh(ext_page_req->len), TRUE, 5000);
	if (error != 0) {
		mpt_prt(mpt, "mpt_user_read_extcfg_page timed out\n");
		return (ETIMEDOUT);
	}

	ext_page_req->ioc_status = htole16(req->IOCStatus);
	if ((req->IOCStatus & MPI_IOCSTATUS_MASK) == MPI_IOCSTATUS_SUCCESS)
		bus_dmamap_sync(mpt_page->tag, mpt_page->map,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	mpt_free_request(mpt, req);
	return (0);
}

static int
mpt_user_write_cfg_page(struct mpt_softc *mpt,
    struct mpt_cfg_page_req *page_req, struct mpt_page_memory *mpt_page)
{
	CONFIG_PAGE_HEADER *hdr;
	request_t    *req;
	cfgparms_t    params;
	u_int	      hdr_attr;
	int	      error;

	hdr = mpt_page->vaddr;
	hdr_attr = hdr->PageType & MPI_CONFIG_PAGEATTR_MASK;
	if (hdr_attr != MPI_CONFIG_PAGEATTR_CHANGEABLE &&
	    hdr_attr != MPI_CONFIG_PAGEATTR_PERSISTENT) {
		mpt_prt(mpt, "page type 0x%x not changeable\n",
			hdr->PageType & MPI_CONFIG_PAGETYPE_MASK);
		return (EINVAL);
	}

#if	0
	/*
	 * We shouldn't mask off other bits here.
	 */
	hdr->PageType &= ~MPI_CONFIG_PAGETYPE_MASK;
#endif

	req = mpt_get_request(mpt, TRUE);
	if (req == NULL)
		return (ENOMEM);

	bus_dmamap_sync(mpt_page->tag, mpt_page->map, BUS_DMASYNC_PREREAD |
	    BUS_DMASYNC_PREWRITE);

	/*
	 * There isn't any point in restoring stripped out attributes
	 * if you then mask them going down to issue the request.
	 */

	params.Action = MPI_CONFIG_ACTION_PAGE_WRITE_CURRENT;
	params.PageVersion = hdr->PageVersion;
	params.PageLength = hdr->PageLength;
	params.PageNumber = hdr->PageNumber;
	params.PageAddress = le32toh(page_req->page_address);
#if	0
	/* Restore stripped out attributes */
	hdr->PageType |= hdr_attr;
	params.PageType = hdr->PageType & MPI_CONFIG_PAGETYPE_MASK;
#else
	params.PageType = hdr->PageType;
#endif
	error = mpt_issue_cfg_req(mpt, req, &params, mpt_page->paddr,
	    le32toh(page_req->len), TRUE, 5000);
	if (error != 0) {
		mpt_prt(mpt, "mpt_write_cfg_page timed out\n");
		return (ETIMEDOUT);
	}

	page_req->ioc_status = htole16(req->IOCStatus);
	bus_dmamap_sync(mpt_page->tag, mpt_page->map, BUS_DMASYNC_POSTREAD |
	    BUS_DMASYNC_POSTWRITE);
	mpt_free_request(mpt, req);
	return (0);
}

static int
mpt_user_reply_handler(struct mpt_softc *mpt, request_t *req,
    uint32_t reply_desc, MSG_DEFAULT_REPLY *reply_frame)
{
	MSG_RAID_ACTION_REPLY *reply;
	struct mpt_user_raid_action_result *res;

	if (req == NULL)
		return (TRUE);

	if (reply_frame != NULL) {
		reply = (MSG_RAID_ACTION_REPLY *)reply_frame;
		req->IOCStatus = le16toh(reply->IOCStatus);
		res = (struct mpt_user_raid_action_result *)
		    (((uint8_t *)req->req_vbuf) + MPT_RQSL(mpt));
		res->action_status = reply->ActionStatus;
		res->volume_status = reply->VolumeStatus;
		bcopy(&reply->ActionData, res->action_data,
		    sizeof(res->action_data));
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

	return (TRUE);
}

/*
 * We use the first part of the request buffer after the request frame
 * to hold the action data and action status from the RAID reply.  The
 * rest of the request buffer is used to hold the buffer for the
 * action SGE.
 */
static int
mpt_user_raid_action(struct mpt_softc *mpt, struct mpt_raid_action *raid_act,
	struct mpt_page_memory *mpt_page)
{
	request_t *req;
	struct mpt_user_raid_action_result *res;
	MSG_RAID_ACTION_REQUEST *rap;
	SGE_SIMPLE32 *se;
	int error;

	req = mpt_get_request(mpt, TRUE);
	if (req == NULL)
		return (ENOMEM);
	rap = req->req_vbuf;
	memset(rap, 0, sizeof *rap);
	rap->Action = raid_act->action;
	rap->ActionDataWord = raid_act->action_data_word;
	rap->Function = MPI_FUNCTION_RAID_ACTION;
	rap->VolumeID = raid_act->volume_id;
	rap->VolumeBus = raid_act->volume_bus;
	rap->PhysDiskNum = raid_act->phys_disk_num;
	se = (SGE_SIMPLE32 *)&rap->ActionDataSGE;
	if (mpt_page->vaddr != NULL && raid_act->len != 0) {
		bus_dmamap_sync(mpt_page->tag, mpt_page->map,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
		se->Address = htole32(mpt_page->paddr);
		MPI_pSGE_SET_LENGTH(se, le32toh(raid_act->len));
		MPI_pSGE_SET_FLAGS(se, (MPI_SGE_FLAGS_SIMPLE_ELEMENT |
		    MPI_SGE_FLAGS_LAST_ELEMENT | MPI_SGE_FLAGS_END_OF_BUFFER |
		    MPI_SGE_FLAGS_END_OF_LIST |
		    (raid_act->write ? MPI_SGE_FLAGS_HOST_TO_IOC :
		    MPI_SGE_FLAGS_IOC_TO_HOST)));
	}
	se->FlagsLength = htole32(se->FlagsLength);
	rap->MsgContext = htole32(req->index | user_handler_id);

	mpt_check_doorbell(mpt);
	mpt_send_cmd(mpt, req);

	error = mpt_wait_req(mpt, req, REQ_STATE_DONE, REQ_STATE_DONE, TRUE,
	    2000);
	if (error != 0) {
		/*
		 * Leave request so it can be cleaned up later.
		 */
		mpt_prt(mpt, "mpt_user_raid_action timed out\n");
		return (error);
	}

	raid_act->ioc_status = htole16(req->IOCStatus);
	if ((req->IOCStatus & MPI_IOCSTATUS_MASK) != MPI_IOCSTATUS_SUCCESS) {
		mpt_free_request(mpt, req);
		return (0);
	}
	
	res = (struct mpt_user_raid_action_result *)
	    (((uint8_t *)req->req_vbuf) + MPT_RQSL(mpt));
	raid_act->volume_status = res->volume_status;
	raid_act->action_status = res->action_status;
	bcopy(res->action_data, raid_act->action_data,
	    sizeof(res->action_data));
	if (mpt_page->vaddr != NULL)
		bus_dmamap_sync(mpt_page->tag, mpt_page->map,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	mpt_free_request(mpt, req);
	return (0);
}

#ifdef __amd64__
#define	PTRIN(p)		((void *)(uintptr_t)(p))
#define PTROUT(v)		((u_int32_t)(uintptr_t)(v))
#endif

static int
mpt_ioctl(struct cdev *dev, u_long cmd, caddr_t arg, int flag, struct thread *td)
{
	struct mpt_softc *mpt;
	struct mpt_cfg_page_req *page_req;
	struct mpt_ext_cfg_page_req *ext_page_req;
	struct mpt_raid_action *raid_act;
	struct mpt_page_memory mpt_page;
#ifdef __amd64__
	struct mpt_cfg_page_req32 *page_req32;
	struct mpt_cfg_page_req page_req_swab;
	struct mpt_ext_cfg_page_req32 *ext_page_req32;
	struct mpt_ext_cfg_page_req ext_page_req_swab;
	struct mpt_raid_action32 *raid_act32;
	struct mpt_raid_action raid_act_swab;
#endif
	int error;

	mpt = dev->si_drv1;
	page_req = (void *)arg;
	ext_page_req = (void *)arg;
	raid_act = (void *)arg;
	mpt_page.vaddr = NULL;

#ifdef __amd64__
	/* Convert 32-bit structs to native ones. */
	page_req32 = (void *)arg;
	ext_page_req32 = (void *)arg;
	raid_act32 = (void *)arg;
	switch (cmd) {
	case MPTIO_READ_CFG_HEADER32:
	case MPTIO_READ_CFG_PAGE32:
	case MPTIO_WRITE_CFG_PAGE32:
		page_req = &page_req_swab;
		page_req->header = page_req32->header;
		page_req->page_address = page_req32->page_address;
		page_req->buf = PTRIN(page_req32->buf);
		page_req->len = page_req32->len;
		page_req->ioc_status = page_req32->ioc_status;
		break;
	case MPTIO_READ_EXT_CFG_HEADER32:
	case MPTIO_READ_EXT_CFG_PAGE32:
		ext_page_req = &ext_page_req_swab;
		ext_page_req->header = ext_page_req32->header;
		ext_page_req->page_address = ext_page_req32->page_address;
		ext_page_req->buf = PTRIN(ext_page_req32->buf);
		ext_page_req->len = ext_page_req32->len;
		ext_page_req->ioc_status = ext_page_req32->ioc_status;
		break;
	case MPTIO_RAID_ACTION32:
		raid_act = &raid_act_swab;
		raid_act->action = raid_act32->action;
		raid_act->volume_bus = raid_act32->volume_bus;
		raid_act->volume_id = raid_act32->volume_id;
		raid_act->phys_disk_num = raid_act32->phys_disk_num;
		raid_act->action_data_word = raid_act32->action_data_word;
		raid_act->buf = PTRIN(raid_act32->buf);
		raid_act->len = raid_act32->len;
		raid_act->volume_status = raid_act32->volume_status;
		bcopy(raid_act32->action_data, raid_act->action_data,
		    sizeof(raid_act->action_data));
		raid_act->action_status = raid_act32->action_status;
		raid_act->ioc_status = raid_act32->ioc_status;
		raid_act->write = raid_act32->write;
		break;
	}
#endif

	switch (cmd) {
#ifdef __amd64__
	case MPTIO_READ_CFG_HEADER32:
#endif
	case MPTIO_READ_CFG_HEADER:
		MPT_LOCK(mpt);
		error = mpt_user_read_cfg_header(mpt, page_req);
		MPT_UNLOCK(mpt);
		break;
#ifdef __amd64__
	case MPTIO_READ_CFG_PAGE32:
#endif
	case MPTIO_READ_CFG_PAGE:
		error = mpt_alloc_buffer(mpt, &mpt_page, page_req->len);
		if (error)
			break;
		error = copyin(page_req->buf, mpt_page.vaddr,
		    sizeof(CONFIG_PAGE_HEADER));
		if (error)
			break;
		MPT_LOCK(mpt);
		error = mpt_user_read_cfg_page(mpt, page_req, &mpt_page);
		MPT_UNLOCK(mpt);
		if (error)
			break;
		error = copyout(mpt_page.vaddr, page_req->buf, page_req->len);
		break;
#ifdef __amd64__
	case MPTIO_READ_EXT_CFG_HEADER32:
#endif
	case MPTIO_READ_EXT_CFG_HEADER:
		MPT_LOCK(mpt);
		error = mpt_user_read_extcfg_header(mpt, ext_page_req);
		MPT_UNLOCK(mpt);
		break;
#ifdef __amd64__
	case MPTIO_READ_EXT_CFG_PAGE32:
#endif
	case MPTIO_READ_EXT_CFG_PAGE:
		error = mpt_alloc_buffer(mpt, &mpt_page, ext_page_req->len);
		if (error)
			break;
		error = copyin(ext_page_req->buf, mpt_page.vaddr,
		    sizeof(CONFIG_EXTENDED_PAGE_HEADER));
		if (error)
			break;
		MPT_LOCK(mpt);
		error = mpt_user_read_extcfg_page(mpt, ext_page_req, &mpt_page);
		MPT_UNLOCK(mpt);
		if (error)
			break;
		error = copyout(mpt_page.vaddr, ext_page_req->buf,
		    ext_page_req->len);
		break;
#ifdef __amd64__
	case MPTIO_WRITE_CFG_PAGE32:
#endif
	case MPTIO_WRITE_CFG_PAGE:
		error = mpt_alloc_buffer(mpt, &mpt_page, page_req->len);
		if (error)
			break;
		error = copyin(page_req->buf, mpt_page.vaddr, page_req->len);
		if (error)
			break;
		MPT_LOCK(mpt);
		error = mpt_user_write_cfg_page(mpt, page_req, &mpt_page);
		MPT_UNLOCK(mpt);
		break;
#ifdef __amd64__
	case MPTIO_RAID_ACTION32:
#endif
	case MPTIO_RAID_ACTION:
		if (raid_act->buf != NULL) {
			error = mpt_alloc_buffer(mpt, &mpt_page, raid_act->len);
			if (error)
				break;
			error = copyin(raid_act->buf, mpt_page.vaddr,
			    raid_act->len);
			if (error)
				break;
		}
		MPT_LOCK(mpt);
		error = mpt_user_raid_action(mpt, raid_act, &mpt_page);
		MPT_UNLOCK(mpt);
		if (error)
			break;
		if (raid_act->buf != NULL)
			error = copyout(mpt_page.vaddr, raid_act->buf,
			    raid_act->len);
		break;
	default:
		error = ENOIOCTL;
		break;
	}

	mpt_free_buffer(&mpt_page);

	if (error)
		return (error);

#ifdef __amd64__
	/* Convert native structs to 32-bit ones. */
	switch (cmd) {
	case MPTIO_READ_CFG_HEADER32:
	case MPTIO_READ_CFG_PAGE32:
	case MPTIO_WRITE_CFG_PAGE32:
		page_req32->header = page_req->header;
		page_req32->page_address = page_req->page_address;
		page_req32->buf = PTROUT(page_req->buf);
		page_req32->len = page_req->len;
		page_req32->ioc_status = page_req->ioc_status;
		break;
	case MPTIO_READ_EXT_CFG_HEADER32:
	case MPTIO_READ_EXT_CFG_PAGE32:		
		ext_page_req32->header = ext_page_req->header;
		ext_page_req32->page_address = ext_page_req->page_address;
		ext_page_req32->buf = PTROUT(ext_page_req->buf);
		ext_page_req32->len = ext_page_req->len;
		ext_page_req32->ioc_status = ext_page_req->ioc_status;
		break;
	case MPTIO_RAID_ACTION32:
		raid_act32->action = raid_act->action;
		raid_act32->volume_bus = raid_act->volume_bus;
		raid_act32->volume_id = raid_act->volume_id;
		raid_act32->phys_disk_num = raid_act->phys_disk_num;
		raid_act32->action_data_word = raid_act->action_data_word;
		raid_act32->buf = PTROUT(raid_act->buf);
		raid_act32->len = raid_act->len;
		raid_act32->volume_status = raid_act->volume_status;
		bcopy(raid_act->action_data, raid_act32->action_data,
		    sizeof(raid_act->action_data));
		raid_act32->action_status = raid_act->action_status;
		raid_act32->ioc_status = raid_act->ioc_status;
		raid_act32->write = raid_act->write;
		break;
	}
#endif

	return (0);
}
