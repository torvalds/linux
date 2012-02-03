/*
 * RWL module  of
 * Broadcom 802.11bang Networking Device Driver
 *
 * Copyright (C) 2011, Broadcom Corporation
 * All Rights Reserved.
 * 
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom Corporation;
 * the contents of this file may not be disclosed to third parties, copied
 * or duplicated in any form, in whole or in part, without the prior
 * written permission of Broadcom Corporation.
 *
 * $Id: wlc_rwl.c,v 1.7.2.2 2010/05/16 19:58:15 Exp $*
 *
 */

#ifndef WLRWL
#error "Cannot use this file without WLRWL defined"
#endif

#include <wlc_cfg.h>
#include <typedefs.h>
#include <bcmdefs.h>
#include <osl.h>
#include <bcmutils.h>
#include <siutils.h>
#include <wlioctl.h>
#include <proto/802.11.h>
#include <d11.h>
#include <wlc_rate.h>
#include <wlc_key.h>
#include <wlc_pub.h>
#include <wlc_bsscfg.h>
#include <wlc.h>
#include <wlc_phy_hal.h>
#include <wl_export.h>

#include <wlc_rwl.h>

enum {
	 IOV_RWLVS_ACTION_FRAME, /* RWL Vendor specific queue */
};

static const bcm_iovar_t rwl_iovars[] = {
	 {"rwlwifivsaction", IOV_RWLVS_ACTION_FRAME,
	 (0), IOVT_BUFFER, RWL_WIFI_ACTION_FRAME_SIZE
	 },
	 {NULL, 0, 0, 0, 0 }
};

static int wlc_rwl_doiovar(void *hdl, const bcm_iovar_t *vi, uint32 actionid,
	const char *name, void *params, uint p_len, void *arg, int len,
	int val_size, struct wlc_if *wlcif);

rwl_info_t *
wlc_rwl_attach(wlc_pub_t *pub, wlc_info_t *wlc)
{
	rwl_info_t *ri;

	WL_TRACE(("wl: wlc_rwl_attach\n"));

	if ((ri = (rwl_info_t *)MALLOC(pub->osh, sizeof(rwl_info_t))) == NULL) {
		WL_ERROR(("wlc_rwl_attach: out of memory, malloced %d bytes", MALLOCED(pub->osh)));
		goto fail;
	}
	bzero((char *)ri, sizeof(rwl_info_t));
	ri->wlc = (void*) wlc;
	ri->pub = pub;

	/* register module */
	if (wlc_module_register(pub, rwl_iovars, "rwl",
		ri, wlc_rwl_doiovar, NULL, NULL)) {
		WL_ERROR(("wl%d: rwl wlc_module_register() failed\n", pub->unit));
		goto fail;
	}

	return ri;

fail:
	if (ri) {
		MFREE(ri->pub->osh, ri, sizeof(rwl_info_t));
	}
	return NULL;
}

int
wlc_rwl_detach(rwl_info_t *ri)
{
	wlc_info_t *wlc;
	rwl_request_t *cleanup_node = (rwl_request_t *)(NULL);

	WL_TRACE(("wl: %s: ri = %p\n", __FUNCTION__, ri));

	wlc = (wlc_info_t*) ri->wlc;
	wlc_module_unregister(ri->pub, "rwl", ri);

	/* Clean up the queue only during the driver cleanup */
	while (ri->rwl_first_action_node != NULL) {
		cleanup_node = ri->rwl_first_action_node->next_request;
		MFREE(wlc->osh, ri->rwl_first_action_node,
		sizeof(rwl_request_t));
		ri->rwl_first_action_node = cleanup_node;
	}

	MFREE(ri->pub->osh, ri, sizeof(rwl_info_t));

	return 0;
}

void
wlc_rwl_init(rwl_info_t *ri)
{
}

void
wlc_rwl_deinit(rwl_info_t *ri)
{
}

void
wlc_rwl_up(wlc_info_t *wlc)
{
}

uint
wlc_rwl_down(wlc_info_t *wlc)
{
	return 0;
}

/* Handling RWL related iovars */
static int
wlc_rwl_doiovar(void *hdl, const bcm_iovar_t *vi, uint32 actionid, const char *name,
	void *params, uint p_len, void *arg, int len, int val_size, struct wlc_if *wlcif)
{
	rwl_info_t *ri = (rwl_info_t *)hdl;
	int err = 0;
	int32 int_val;

	if ((err = wlc_iovar_check(ri->pub, vi, arg, len, IOV_ISSET(actionid))) != 0)
		return err;

	if (p_len >= (int)sizeof(int_val))
		bcopy(params, &int_val, sizeof(int_val));

	switch (actionid) {
		case IOV_GVAL(IOV_RWLVS_ACTION_FRAME):
		{
			dot11_action_wifi_vendor_specific_t *list;
			rwl_request_t *intermediate_node;
			list = (dot11_action_wifi_vendor_specific_t*)arg;
			if (ri->rwl_first_action_node != NULL) {
				/* pop from the list and copy to user buffer
				 * and move the node to next node
				 */
				bcopy((char*)&ri->rwl_first_action_node->action_frame,
				(char*)list, RWL_WIFI_ACTION_FRAME_SIZE);
				intermediate_node = ri->rwl_first_action_node->next_request;
				MFREE(ri->wlc->osh, ri->rwl_first_action_node,
				sizeof(rwl_request_t));
				ri->rwl_first_action_node = intermediate_node;
			}
			break;
		}
	default:
		err = BCME_UNSUPPORTED;
		break;
	}

	return err;
}

#ifdef WIFI_REFLECTOR
/* Allocate and initialize an wifi action frame */
static dot11_action_wifi_vendor_specific_t *
allocate_action_frame(wlc_info_t *wlc)
{
	dot11_action_wifi_vendor_specific_t *frame;

	if ((frame = (dot11_action_wifi_vendor_specific_t *)
	     MALLOC(wlc->osh, RWL_WIFI_ACTION_FRAME_SIZE)) == NULL)
		return NULL;

	frame->category  = DOT11_ACTION_CAT_VS;
	frame->OUI[0]    = RWL_WIFI_OUI_BYTE0;
	frame->OUI[1]    = RWL_WIFI_OUI_BYTE1;
	frame->OUI[2]    = RWL_WIFI_OUI_BYTE2;
	frame->type      = RWL_WIFI_DEFAULT_TYPE;
	frame->subtype   = RWL_WIFI_DEFAULT_SUBTYPE;

	return frame;
}

/* Send out the action frame */
static int
rwl_send_wifi_response(wlc_info_t *wlc,
                       dot11_action_wifi_vendor_specific_t *response,
                       const struct ether_addr * dest_addr_ptr)
{
	uint32 err = 0;

#ifdef WIFI_ACT_FRAME
	wl_action_frame_t *action_frame;

	if ((action_frame = (wl_action_frame_t *)
	     MALLOC(wlc->osh, sizeof(wl_action_frame_t))) == NULL)
		return BCME_NOMEM;

	memcpy(&action_frame->data, response, RWL_WIFI_ACTION_FRAME_SIZE);

	/* Set the dest addr */
	memcpy(&action_frame->da, dest_addr_ptr, ETHER_ADDR_LEN);

	/* set the length */
	action_frame->len = RWL_WIFI_ACTION_FRAME_SIZE;

	wlc_send_action_frame(wlc, wlc->cfg, NULL, (void *)action_frame);

	MFREE(wlc->osh, action_frame, sizeof(wl_action_frame_t));
#endif /* WIFI_ACT_FRAME */

	return err;
}

/* Function to specify the client that an invalid wl command has been received
 * This command cannot be processed by the In-dongle reflector.
 */
static int
rwl_wifi_send_error(wlc_info_t *wlc, const struct ether_addr * dest_addr_ptr)
{
	int err;
	dot11_action_wifi_vendor_specific_t *response;
	rem_ioctl_t rem_cdc, *rem_ptr = &rem_cdc;
	const char *errmsg = "In-dongle does not support shell/DHD/ASD\n";

	if ((response = allocate_action_frame(wlc)) == NULL)
		return BCME_NOMEM;

	rem_ptr->msg.cmd = -1;
	rem_ptr->msg.len = rem_ptr->data_len = strlen(errmsg) + 1;
	rem_ptr->msg.flags = REMOTE_REPLY;

	memcpy(&response->data[RWL_WIFI_CDC_HEADER_OFFSET], rem_ptr, REMOTE_SIZE);
	memcpy(&response->data[REMOTE_SIZE], errmsg, rem_ptr->data_len);

	err = rwl_send_wifi_response(wlc, response, dest_addr_ptr);

	MFREE(wlc->osh, response, sizeof(dot11_action_wifi_vendor_specific_t));

	return err;
}

/* Function which responds to the findserver command when sent by the client
 * application. The client sends out packet in every channel available.
 * we recieve the packet on a particular channel we respond back by sending
 * our channel number
 */
static int
rwl_wifi_findserver_response(wlc_info_t *wlc,
                             dot11_action_wifi_vendor_specific_t *response,
                             const struct ether_addr * dest_addr_ptr)
{
	int err;
	channel_info_t ci;
	uint32 tx_count;

	/* Query the server channel */
	response->type = RWL_WIFI_FOUND_PEER;

	err = wlc_ioctl(wlc, WLC_GET_CHANNEL, &ci, sizeof(ci), NULL);
	if (err)
		return err;

	/* Match the client channel with our channel */
	if (response->data[RWL_WIFI_CLIENT_CHANNEL_OFFSET] == ci.hw_channel) {
		response->data[RWL_WIFI_SERVER_CHANNEL_OFFSET] = ci.hw_channel;
		for (tx_count = 0; tx_count < RWL_WIFI_SEND; ++tx_count) {
			err = rwl_send_wifi_response(wlc, response, dest_addr_ptr);
			if (err)
				break;
		}
	}

	return err;
}

/* Function which responds to the set command sent by the client. 
 * We call wlc_ioctl to set the specified value and send back the
 * results of setting to the client
 */
static int
rwl_wifi_set_cmd_response(wlc_info_t *wlc,
                          rem_packet_t *rem_packet_ptr,
                          const struct ether_addr * dest_addr_ptr)
{
	int err;
	rem_ioctl_t rem_cdc, *rem_ptr = &rem_cdc;
	rem_ioctl_t *rem_ioctl_ptr = (rem_ioctl_t *)&(rem_packet_ptr->rem_cdc);

	dot11_action_wifi_vendor_specific_t *rem_wifi_send = allocate_action_frame(wlc);

	if (rem_wifi_send == NULL)
		return BCME_NOMEM;

	/* Execute the command locally */

	err = wlc_ioctl(wlc, rem_ioctl_ptr->msg.cmd, rem_packet_ptr->message,
	                rem_ioctl_ptr->msg.len, NULL);

	rem_ptr->msg.cmd = err;
	rem_ptr->msg.len = 0;
	rem_ptr->msg.flags = REMOTE_REPLY;
	rem_ptr->data_len = 0;

	memcpy(&rem_wifi_send->data[RWL_WIFI_CDC_HEADER_OFFSET], rem_ptr, REMOTE_SIZE);

	err = rwl_send_wifi_response(wlc, rem_wifi_send, dest_addr_ptr);

	MFREE(wlc->osh, rem_wifi_send, sizeof(*rem_wifi_send));

	return err;
}

/* This function responds to the remote wl get command.
 * On reception of packet we call wlc_ioctl() to get the results.
 * If the result fits into a single packet we send it directly
 * else we fragment the results and send it though multiple packets
 */
static int
rwl_wifi_get_cmd_response(wlc_info_t *wlc,
                          rem_packet_t *rem_packet_ptr,
                          const struct ether_addr * dest_addr_ptr)
{
	int err;
	uint32 tx_count;
	uint32 totalframes;
	uchar *buf;
	rem_ioctl_t rem_cdc, *rem_ptr = &rem_cdc;
	rem_ioctl_t *rem_ioctl_ptr = (rem_ioctl_t *)&(rem_packet_ptr->rem_cdc);
	dot11_action_wifi_vendor_specific_t *rem_wifi_send = allocate_action_frame(wlc);

	if (rem_wifi_send == NULL)
		return BCME_NOMEM;

	if ((buf = MALLOC(wlc->osh, rem_ioctl_ptr->msg.len)) == NULL) {
		MFREE(wlc->osh, rem_wifi_send, sizeof(*rem_wifi_send));
		return BCME_NOMEM;
	}

	/* Execute the command locally */
	memcpy(buf, rem_packet_ptr->message, rem_ioctl_ptr->data_len);
	err = wlc_ioctl(wlc, rem_ioctl_ptr->msg.cmd, (void*)buf,
	                rem_ioctl_ptr->msg.len, NULL);

	rem_ptr->msg.cmd = err;
	rem_ptr->msg.len = rem_ioctl_ptr->msg.len;
	rem_ptr->msg.flags = REMOTE_REPLY;
	rem_ptr->data_len = rem_ioctl_ptr->msg.len;

	if (rem_ioctl_ptr->msg.len > RWL_WIFI_FRAG_DATA_SIZE) {
		totalframes = rem_ptr->msg.len / RWL_WIFI_FRAG_DATA_SIZE;
		memcpy(&rem_wifi_send->data[RWL_WIFI_CDC_HEADER_OFFSET], rem_ptr, REMOTE_SIZE);
		memcpy((char*)&rem_wifi_send->data[REMOTE_SIZE], &buf[0], RWL_WIFI_FRAG_DATA_SIZE);
		rem_wifi_send->type = RWL_ACTION_WIFI_FRAG_TYPE;
		rem_wifi_send->subtype = RWL_WIFI_DEFAULT_SUBTYPE;

		if ((err = rwl_send_wifi_response (wlc, rem_wifi_send, dest_addr_ptr)) != 0)
			goto exit;

		/* Send remaining bytes in fragments */
		for (tx_count = 1; tx_count < totalframes; tx_count++) {
			rem_wifi_send->type = RWL_ACTION_WIFI_FRAG_TYPE;
			rem_wifi_send->subtype = tx_count;
			/* First frame onwards , buf contains only data */
			memcpy((char*)&rem_wifi_send->data,
			       &buf[tx_count*RWL_WIFI_FRAG_DATA_SIZE], RWL_WIFI_FRAG_DATA_SIZE);
			if ((err = rwl_send_wifi_response (wlc,
			                                   rem_wifi_send,
			                                   dest_addr_ptr)) != 0) {
				goto exit;
			}

		}
		/* Check for remaining bytes to send */
		if ((totalframes * RWL_WIFI_FRAG_DATA_SIZE) != rem_ptr->msg.len) {
			rem_wifi_send->type = RWL_ACTION_WIFI_FRAG_TYPE;
			rem_wifi_send->subtype = tx_count;
			memcpy((char*)&rem_wifi_send->data,
			       &buf[tx_count*RWL_WIFI_FRAG_DATA_SIZE],
			       (rem_ptr->msg.len - (tx_count*RWL_WIFI_FRAG_DATA_SIZE)));
			err = rwl_send_wifi_response(wlc, rem_wifi_send, dest_addr_ptr);
		}
	} else {
		/* Packet fits into a single frame; send it off at one go */
		memcpy(&rem_wifi_send->data[RWL_WIFI_CDC_HEADER_OFFSET], rem_ptr, REMOTE_SIZE);
		memcpy((char*)&rem_wifi_send->data[REMOTE_SIZE],
		       buf, rem_ioctl_ptr->msg.len);

		err = rwl_send_wifi_response(wlc, rem_wifi_send, dest_addr_ptr);
	}
exit:
	MFREE(wlc->osh, rem_wifi_send,
		sizeof(dot11_action_wifi_vendor_specific_t));

	MFREE(wlc->osh, buf, rem_ioctl_ptr->msg.len);

	return err;
}
#endif /* WIFI_REFLECTOR */

/* If the management frame is an RWL action frame then the action frame will be queued.
 * This queued frame will be read by the application.
 */
void
wlc_recv_wifi_mgmtact(rwl_info_t *rwlh, uint8 *body, const struct ether_addr *sa)
{

#ifdef WIFI_REFLECTOR
	rem_packet_t *rem_packet_ptr;
	rem_ioctl_t *rem_ioctl_ptr;
#endif
	rwl_request_t *rwl_new_action_node =
	(rwl_request_t *)(NULL);
	wlc_info_t *wlc = (wlc_info_t*) rwlh->wlc;

	if ((rwl_new_action_node = (rwl_request_t*)MALLOC(wlc->osh,
		sizeof(rwl_request_t))) == NULL) {
		return;
	}

	bcopy((char*)body, (char*)&rwl_new_action_node->action_frame, RWL_WIFI_ACTION_FRAME_SIZE);
#ifdef WIFI_REFLECTOR
	rem_packet_ptr = (rem_packet_t *)&(rwl_new_action_node->action_frame.data[0]);
	rem_ioctl_ptr = (rem_ioctl_t *)&(rem_packet_ptr->rem_cdc);

	/* Do not queue the packets if it is an RWL command.
	 * Just parse, process and send back the results depending
	 * upon the query packet. Do not queue any packets, as
	 * after a certain number of packets the dongle memory would
	 * be exhausted.
	 */
	if (rem_ioctl_ptr->msg.flags & REMOTE_FINDSERVER_CMD)
		rwl_wifi_findserver_response(wlc, &rwl_new_action_node->action_frame, sa);
	else if (rem_ioctl_ptr->msg.flags & REMOTE_SET_CMD)
		rwl_wifi_set_cmd_response(wlc, rem_packet_ptr, sa);
	else if (rem_ioctl_ptr->msg.flags & REMOTE_GET_CMD)
		rwl_wifi_get_cmd_response(wlc, rem_packet_ptr, sa);
	else
		rwl_wifi_send_error(wlc, sa);

	MFREE(wlc->osh, rwl_new_action_node, sizeof(rwl_request_t));
	return;

#endif /* WIFI_REFLECTOR */
	if (rwlh->rwl_first_action_node == NULL) {
		 rwlh->rwl_first_action_node = rwl_new_action_node;
		 rwlh->rwl_last_action_node = rwlh->rwl_first_action_node;
		 rwlh->rwl_first_action_node->next_request = NULL;
	} else {
		/* insert the all incoming frame at the end of the queue */
		rwlh->rwl_last_action_node->next_request = rwl_new_action_node;
		rwlh->rwl_last_action_node = rwl_new_action_node;
		rwlh->rwl_last_action_node->next_request = NULL;
	}
}

void
wlc_rwl_frameaction(rwl_info_t *rwlh, struct dot11_management_header *hdr,
	uint8 *body, int body_len)
{
	uint action_id;

	if (body_len > DOT11_OUI_LEN) {
		action_id = (uint)body[DOT11_OUI_LEN+1];
		if ((action_id == RWL_WIFI_FIND_MY_PEER) ||
			(action_id == RWL_WIFI_FOUND_PEER) ||
			(action_id == RWL_WIFI_DEFAULT) ||
			(action_id == RWL_ACTION_WIFI_FRAG_TYPE)) {

			/* this is a Remote WL command */
			wlc_recv_wifi_mgmtact(rwlh, body, &hdr->sa);
		}
	}
}
