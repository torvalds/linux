// SPDX-License-Identifier: GPL-2.0 OR MIT

/*
 * Xen para-virtual sound device
 *
 * Copyright (C) 2016-2018 EPAM Systems Inc.
 *
 * Author: Oleksandr Andrushchenko <oleksandr_andrushchenko@epam.com>
 */

#include <xen/events.h>
#include <xen/grant_table.h>
#include <xen/xen.h>
#include <xen/xenbus.h>

#include "xen_snd_front.h"
#include "xen_snd_front_alsa.h"
#include "xen_snd_front_cfg.h"
#include "xen_snd_front_evtchnl.h"

static irqreturn_t evtchnl_interrupt_req(int irq, void *dev_id)
{
	struct xen_snd_front_evtchnl *channel = dev_id;
	struct xen_snd_front_info *front_info = channel->front_info;
	struct xensnd_resp *resp;
	RING_IDX i, rp;

	if (unlikely(channel->state != EVTCHNL_STATE_CONNECTED))
		return IRQ_HANDLED;

	guard(mutex)(&channel->ring_io_lock);

again:
	rp = channel->u.req.ring.sring->rsp_prod;
	/* Ensure we see queued responses up to rp. */
	rmb();

	/*
	 * Assume that the backend is trusted to always write sane values
	 * to the ring counters, so no overflow checks on frontend side
	 * are required.
	 */
	for (i = channel->u.req.ring.rsp_cons; i != rp; i++) {
		resp = RING_GET_RESPONSE(&channel->u.req.ring, i);
		if (resp->id != channel->evt_id)
			continue;
		switch (resp->operation) {
		case XENSND_OP_OPEN:
		case XENSND_OP_CLOSE:
		case XENSND_OP_READ:
		case XENSND_OP_WRITE:
		case XENSND_OP_TRIGGER:
			channel->u.req.resp_status = resp->status;
			complete(&channel->u.req.completion);
			break;
		case XENSND_OP_HW_PARAM_QUERY:
			channel->u.req.resp_status = resp->status;
			channel->u.req.resp.hw_param =
					resp->resp.hw_param;
			complete(&channel->u.req.completion);
			break;

		default:
			dev_err(&front_info->xb_dev->dev,
				"Operation %d is not supported\n",
				resp->operation);
			break;
		}
	}

	channel->u.req.ring.rsp_cons = i;
	if (i != channel->u.req.ring.req_prod_pvt) {
		int more_to_do;

		RING_FINAL_CHECK_FOR_RESPONSES(&channel->u.req.ring,
					       more_to_do);
		if (more_to_do)
			goto again;
	} else {
		channel->u.req.ring.sring->rsp_event = i + 1;
	}

	return IRQ_HANDLED;
}

static irqreturn_t evtchnl_interrupt_evt(int irq, void *dev_id)
{
	struct xen_snd_front_evtchnl *channel = dev_id;
	struct xensnd_event_page *page = channel->u.evt.page;
	u32 cons, prod;

	if (unlikely(channel->state != EVTCHNL_STATE_CONNECTED))
		return IRQ_HANDLED;

	guard(mutex)(&channel->ring_io_lock);

	prod = page->in_prod;
	/* Ensure we see ring contents up to prod. */
	virt_rmb();
	if (prod == page->in_cons)
		return IRQ_HANDLED;

	/*
	 * Assume that the backend is trusted to always write sane values
	 * to the ring counters, so no overflow checks on frontend side
	 * are required.
	 */
	for (cons = page->in_cons; cons != prod; cons++) {
		struct xensnd_evt *event;

		event = &XENSND_IN_RING_REF(page, cons);
		if (unlikely(event->id != channel->evt_id++))
			continue;

		switch (event->type) {
		case XENSND_EVT_CUR_POS:
			xen_snd_front_alsa_handle_cur_pos(channel,
							  event->op.cur_pos.position);
			break;
		}
	}

	page->in_cons = cons;
	/* Ensure ring contents. */
	virt_wmb();

	return IRQ_HANDLED;
}

void xen_snd_front_evtchnl_flush(struct xen_snd_front_evtchnl *channel)
{
	int notify;

	channel->u.req.ring.req_prod_pvt++;
	RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&channel->u.req.ring, notify);
	if (notify)
		notify_remote_via_irq(channel->irq);
}

static void evtchnl_free(struct xen_snd_front_info *front_info,
			 struct xen_snd_front_evtchnl *channel)
{
	void *page = NULL;

	if (channel->type == EVTCHNL_TYPE_REQ)
		page = channel->u.req.ring.sring;
	else if (channel->type == EVTCHNL_TYPE_EVT)
		page = channel->u.evt.page;

	if (!page)
		return;

	channel->state = EVTCHNL_STATE_DISCONNECTED;
	if (channel->type == EVTCHNL_TYPE_REQ) {
		/* Release all who still waits for response if any. */
		channel->u.req.resp_status = -EIO;
		complete_all(&channel->u.req.completion);
	}

	if (channel->irq)
		unbind_from_irqhandler(channel->irq, channel);

	if (channel->port)
		xenbus_free_evtchn(front_info->xb_dev, channel->port);

	/* End access and free the page. */
	xenbus_teardown_ring(&page, 1, &channel->gref);

	memset(channel, 0, sizeof(*channel));
}

void xen_snd_front_evtchnl_free_all(struct xen_snd_front_info *front_info)
{
	int i;

	if (!front_info->evt_pairs)
		return;

	for (i = 0; i < front_info->num_evt_pairs; i++) {
		evtchnl_free(front_info, &front_info->evt_pairs[i].req);
		evtchnl_free(front_info, &front_info->evt_pairs[i].evt);
	}

	kfree(front_info->evt_pairs);
	front_info->evt_pairs = NULL;
}

static int evtchnl_alloc(struct xen_snd_front_info *front_info, int index,
			 struct xen_snd_front_evtchnl *channel,
			 enum xen_snd_front_evtchnl_type type)
{
	struct xenbus_device *xb_dev = front_info->xb_dev;
	void *page;
	irq_handler_t handler;
	char *handler_name = NULL;
	int ret;

	memset(channel, 0, sizeof(*channel));
	channel->type = type;
	channel->index = index;
	channel->front_info = front_info;
	channel->state = EVTCHNL_STATE_DISCONNECTED;
	ret = xenbus_setup_ring(xb_dev, GFP_KERNEL, &page, 1, &channel->gref);
	if (ret)
		goto fail;

	handler_name = kasprintf(GFP_KERNEL, "%s-%s", XENSND_DRIVER_NAME,
				 type == EVTCHNL_TYPE_REQ ?
				 XENSND_FIELD_RING_REF :
				 XENSND_FIELD_EVT_RING_REF);
	if (!handler_name) {
		ret = -ENOMEM;
		goto fail;
	}

	mutex_init(&channel->ring_io_lock);

	if (type == EVTCHNL_TYPE_REQ) {
		struct xen_sndif_sring *sring = page;

		init_completion(&channel->u.req.completion);
		mutex_init(&channel->u.req.req_io_lock);
		XEN_FRONT_RING_INIT(&channel->u.req.ring, sring, XEN_PAGE_SIZE);

		handler = evtchnl_interrupt_req;
	} else {
		channel->u.evt.page = page;
		handler = evtchnl_interrupt_evt;
	}

	ret = xenbus_alloc_evtchn(xb_dev, &channel->port);
	if (ret < 0)
		goto fail;

	ret = bind_evtchn_to_irq(channel->port);
	if (ret < 0) {
		dev_err(&xb_dev->dev,
			"Failed to bind IRQ for domid %d port %d: %d\n",
			front_info->xb_dev->otherend_id, channel->port, ret);
		goto fail;
	}

	channel->irq = ret;

	ret = request_threaded_irq(channel->irq, NULL, handler,
				   IRQF_ONESHOT, handler_name, channel);
	if (ret < 0) {
		dev_err(&xb_dev->dev, "Failed to request IRQ %d: %d\n",
			channel->irq, ret);
		goto fail;
	}

	kfree(handler_name);
	return 0;

fail:
	kfree(handler_name);
	dev_err(&xb_dev->dev, "Failed to allocate ring: %d\n", ret);
	return ret;
}

int xen_snd_front_evtchnl_create_all(struct xen_snd_front_info *front_info,
				     int num_streams)
{
	struct xen_front_cfg_card *cfg = &front_info->cfg;
	struct device *dev = &front_info->xb_dev->dev;
	int d, ret = 0;

	front_info->evt_pairs =
			kcalloc(num_streams,
				sizeof(struct xen_snd_front_evtchnl_pair),
				GFP_KERNEL);
	if (!front_info->evt_pairs)
		return -ENOMEM;

	/* Iterate over devices and their streams and create event channels. */
	for (d = 0; d < cfg->num_pcm_instances; d++) {
		struct xen_front_cfg_pcm_instance *pcm_instance;
		int s, index;

		pcm_instance = &cfg->pcm_instances[d];

		for (s = 0; s < pcm_instance->num_streams_pb; s++) {
			index = pcm_instance->streams_pb[s].index;

			ret = evtchnl_alloc(front_info, index,
					    &front_info->evt_pairs[index].req,
					    EVTCHNL_TYPE_REQ);
			if (ret < 0) {
				dev_err(dev, "Error allocating control channel\n");
				goto fail;
			}

			ret = evtchnl_alloc(front_info, index,
					    &front_info->evt_pairs[index].evt,
					    EVTCHNL_TYPE_EVT);
			if (ret < 0) {
				dev_err(dev, "Error allocating in-event channel\n");
				goto fail;
			}
		}

		for (s = 0; s < pcm_instance->num_streams_cap; s++) {
			index = pcm_instance->streams_cap[s].index;

			ret = evtchnl_alloc(front_info, index,
					    &front_info->evt_pairs[index].req,
					    EVTCHNL_TYPE_REQ);
			if (ret < 0) {
				dev_err(dev, "Error allocating control channel\n");
				goto fail;
			}

			ret = evtchnl_alloc(front_info, index,
					    &front_info->evt_pairs[index].evt,
					    EVTCHNL_TYPE_EVT);
			if (ret < 0) {
				dev_err(dev, "Error allocating in-event channel\n");
				goto fail;
			}
		}
	}

	front_info->num_evt_pairs = num_streams;
	return 0;

fail:
	xen_snd_front_evtchnl_free_all(front_info);
	return ret;
}

static int evtchnl_publish(struct xenbus_transaction xbt,
			   struct xen_snd_front_evtchnl *channel,
			   const char *path, const char *node_ring,
			   const char *node_chnl)
{
	struct xenbus_device *xb_dev = channel->front_info->xb_dev;
	int ret;

	/* Write control channel ring reference. */
	ret = xenbus_printf(xbt, path, node_ring, "%u", channel->gref);
	if (ret < 0) {
		dev_err(&xb_dev->dev, "Error writing ring-ref: %d\n", ret);
		return ret;
	}

	/* Write event channel ring reference. */
	ret = xenbus_printf(xbt, path, node_chnl, "%u", channel->port);
	if (ret < 0) {
		dev_err(&xb_dev->dev, "Error writing event channel: %d\n", ret);
		return ret;
	}

	return 0;
}

int xen_snd_front_evtchnl_publish_all(struct xen_snd_front_info *front_info)
{
	struct xen_front_cfg_card *cfg = &front_info->cfg;
	struct xenbus_transaction xbt;
	int ret, d;

again:
	ret = xenbus_transaction_start(&xbt);
	if (ret < 0) {
		xenbus_dev_fatal(front_info->xb_dev, ret,
				 "starting transaction");
		return ret;
	}

	for (d = 0; d < cfg->num_pcm_instances; d++) {
		struct xen_front_cfg_pcm_instance *pcm_instance;
		int s, index;

		pcm_instance = &cfg->pcm_instances[d];

		for (s = 0; s < pcm_instance->num_streams_pb; s++) {
			index = pcm_instance->streams_pb[s].index;

			ret = evtchnl_publish(xbt,
					      &front_info->evt_pairs[index].req,
					      pcm_instance->streams_pb[s].xenstore_path,
					      XENSND_FIELD_RING_REF,
					      XENSND_FIELD_EVT_CHNL);
			if (ret < 0)
				goto fail;

			ret = evtchnl_publish(xbt,
					      &front_info->evt_pairs[index].evt,
					      pcm_instance->streams_pb[s].xenstore_path,
					      XENSND_FIELD_EVT_RING_REF,
					      XENSND_FIELD_EVT_EVT_CHNL);
			if (ret < 0)
				goto fail;
		}

		for (s = 0; s < pcm_instance->num_streams_cap; s++) {
			index = pcm_instance->streams_cap[s].index;

			ret = evtchnl_publish(xbt,
					      &front_info->evt_pairs[index].req,
					      pcm_instance->streams_cap[s].xenstore_path,
					      XENSND_FIELD_RING_REF,
					      XENSND_FIELD_EVT_CHNL);
			if (ret < 0)
				goto fail;

			ret = evtchnl_publish(xbt,
					      &front_info->evt_pairs[index].evt,
					      pcm_instance->streams_cap[s].xenstore_path,
					      XENSND_FIELD_EVT_RING_REF,
					      XENSND_FIELD_EVT_EVT_CHNL);
			if (ret < 0)
				goto fail;
		}
	}
	ret = xenbus_transaction_end(xbt, 0);
	if (ret < 0) {
		if (ret == -EAGAIN)
			goto again;

		xenbus_dev_fatal(front_info->xb_dev, ret,
				 "completing transaction");
		goto fail_to_end;
	}
	return 0;
fail:
	xenbus_transaction_end(xbt, 1);
fail_to_end:
	xenbus_dev_fatal(front_info->xb_dev, ret, "writing XenStore");
	return ret;
}

void xen_snd_front_evtchnl_pair_set_connected(struct xen_snd_front_evtchnl_pair *evt_pair,
					      bool is_connected)
{
	enum xen_snd_front_evtchnl_state state;

	if (is_connected)
		state = EVTCHNL_STATE_CONNECTED;
	else
		state = EVTCHNL_STATE_DISCONNECTED;

	scoped_guard(mutex, &evt_pair->req.ring_io_lock) {
		evt_pair->req.state = state;
	}

	scoped_guard(mutex, &evt_pair->evt.ring_io_lock) {
		evt_pair->evt.state = state;
	}
}

void xen_snd_front_evtchnl_pair_clear(struct xen_snd_front_evtchnl_pair *evt_pair)
{
	scoped_guard(mutex, &evt_pair->req.ring_io_lock) {
		evt_pair->req.evt_next_id = 0;
	}

	scoped_guard(mutex, &evt_pair->evt.ring_io_lock) {
		evt_pair->evt.evt_next_id = 0;
	}
}

