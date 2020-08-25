// SPDX-License-Identifier: GPL-2.0 OR MIT

/*
 * Xen para-virtual sound device
 *
 * Copyright (C) 2016-2018 EPAM Systems Inc.
 *
 * Author: Oleksandr Andrushchenko <oleksandr_andrushchenko@epam.com>
 */

#include <linux/delay.h>
#include <linux/module.h>

#include <xen/page.h>
#include <xen/platform_pci.h>
#include <xen/xen.h>
#include <xen/xenbus.h>

#include <xen/xen-front-pgdir-shbuf.h>
#include <xen/interface/io/sndif.h>

#include "xen_snd_front.h"
#include "xen_snd_front_alsa.h"
#include "xen_snd_front_evtchnl.h"

static struct xensnd_req *
be_stream_prepare_req(struct xen_snd_front_evtchnl *evtchnl, u8 operation)
{
	struct xensnd_req *req;

	req = RING_GET_REQUEST(&evtchnl->u.req.ring,
			       evtchnl->u.req.ring.req_prod_pvt);
	req->operation = operation;
	req->id = evtchnl->evt_next_id++;
	evtchnl->evt_id = req->id;
	return req;
}

static int be_stream_do_io(struct xen_snd_front_evtchnl *evtchnl)
{
	if (unlikely(evtchnl->state != EVTCHNL_STATE_CONNECTED))
		return -EIO;

	reinit_completion(&evtchnl->u.req.completion);
	xen_snd_front_evtchnl_flush(evtchnl);
	return 0;
}

static int be_stream_wait_io(struct xen_snd_front_evtchnl *evtchnl)
{
	if (wait_for_completion_timeout(&evtchnl->u.req.completion,
			msecs_to_jiffies(VSND_WAIT_BACK_MS)) <= 0)
		return -ETIMEDOUT;

	return evtchnl->u.req.resp_status;
}

int xen_snd_front_stream_query_hw_param(struct xen_snd_front_evtchnl *evtchnl,
					struct xensnd_query_hw_param *hw_param_req,
					struct xensnd_query_hw_param *hw_param_resp)
{
	struct xensnd_req *req;
	int ret;

	mutex_lock(&evtchnl->u.req.req_io_lock);

	mutex_lock(&evtchnl->ring_io_lock);
	req = be_stream_prepare_req(evtchnl, XENSND_OP_HW_PARAM_QUERY);
	req->op.hw_param = *hw_param_req;
	mutex_unlock(&evtchnl->ring_io_lock);

	ret = be_stream_do_io(evtchnl);

	if (ret == 0)
		ret = be_stream_wait_io(evtchnl);

	if (ret == 0)
		*hw_param_resp = evtchnl->u.req.resp.hw_param;

	mutex_unlock(&evtchnl->u.req.req_io_lock);
	return ret;
}

int xen_snd_front_stream_prepare(struct xen_snd_front_evtchnl *evtchnl,
				 struct xen_front_pgdir_shbuf *shbuf,
				 u8 format, unsigned int channels,
				 unsigned int rate, u32 buffer_sz,
				 u32 period_sz)
{
	struct xensnd_req *req;
	int ret;

	mutex_lock(&evtchnl->u.req.req_io_lock);

	mutex_lock(&evtchnl->ring_io_lock);
	req = be_stream_prepare_req(evtchnl, XENSND_OP_OPEN);
	req->op.open.pcm_format = format;
	req->op.open.pcm_channels = channels;
	req->op.open.pcm_rate = rate;
	req->op.open.buffer_sz = buffer_sz;
	req->op.open.period_sz = period_sz;
	req->op.open.gref_directory =
		xen_front_pgdir_shbuf_get_dir_start(shbuf);
	mutex_unlock(&evtchnl->ring_io_lock);

	ret = be_stream_do_io(evtchnl);

	if (ret == 0)
		ret = be_stream_wait_io(evtchnl);

	mutex_unlock(&evtchnl->u.req.req_io_lock);
	return ret;
}

int xen_snd_front_stream_close(struct xen_snd_front_evtchnl *evtchnl)
{
	__always_unused struct xensnd_req *req;
	int ret;

	mutex_lock(&evtchnl->u.req.req_io_lock);

	mutex_lock(&evtchnl->ring_io_lock);
	req = be_stream_prepare_req(evtchnl, XENSND_OP_CLOSE);
	mutex_unlock(&evtchnl->ring_io_lock);

	ret = be_stream_do_io(evtchnl);

	if (ret == 0)
		ret = be_stream_wait_io(evtchnl);

	mutex_unlock(&evtchnl->u.req.req_io_lock);
	return ret;
}

int xen_snd_front_stream_write(struct xen_snd_front_evtchnl *evtchnl,
			       unsigned long pos, unsigned long count)
{
	struct xensnd_req *req;
	int ret;

	mutex_lock(&evtchnl->u.req.req_io_lock);

	mutex_lock(&evtchnl->ring_io_lock);
	req = be_stream_prepare_req(evtchnl, XENSND_OP_WRITE);
	req->op.rw.length = count;
	req->op.rw.offset = pos;
	mutex_unlock(&evtchnl->ring_io_lock);

	ret = be_stream_do_io(evtchnl);

	if (ret == 0)
		ret = be_stream_wait_io(evtchnl);

	mutex_unlock(&evtchnl->u.req.req_io_lock);
	return ret;
}

int xen_snd_front_stream_read(struct xen_snd_front_evtchnl *evtchnl,
			      unsigned long pos, unsigned long count)
{
	struct xensnd_req *req;
	int ret;

	mutex_lock(&evtchnl->u.req.req_io_lock);

	mutex_lock(&evtchnl->ring_io_lock);
	req = be_stream_prepare_req(evtchnl, XENSND_OP_READ);
	req->op.rw.length = count;
	req->op.rw.offset = pos;
	mutex_unlock(&evtchnl->ring_io_lock);

	ret = be_stream_do_io(evtchnl);

	if (ret == 0)
		ret = be_stream_wait_io(evtchnl);

	mutex_unlock(&evtchnl->u.req.req_io_lock);
	return ret;
}

int xen_snd_front_stream_trigger(struct xen_snd_front_evtchnl *evtchnl,
				 int type)
{
	struct xensnd_req *req;
	int ret;

	mutex_lock(&evtchnl->u.req.req_io_lock);

	mutex_lock(&evtchnl->ring_io_lock);
	req = be_stream_prepare_req(evtchnl, XENSND_OP_TRIGGER);
	req->op.trigger.type = type;
	mutex_unlock(&evtchnl->ring_io_lock);

	ret = be_stream_do_io(evtchnl);

	if (ret == 0)
		ret = be_stream_wait_io(evtchnl);

	mutex_unlock(&evtchnl->u.req.req_io_lock);
	return ret;
}

static void xen_snd_drv_fini(struct xen_snd_front_info *front_info)
{
	xen_snd_front_alsa_fini(front_info);
	xen_snd_front_evtchnl_free_all(front_info);
}

static int sndback_initwait(struct xen_snd_front_info *front_info)
{
	int num_streams;
	int ret;

	ret = xen_snd_front_cfg_card(front_info, &num_streams);
	if (ret < 0)
		return ret;

	/* create event channels for all streams and publish */
	ret = xen_snd_front_evtchnl_create_all(front_info, num_streams);
	if (ret < 0)
		return ret;

	return xen_snd_front_evtchnl_publish_all(front_info);
}

static int sndback_connect(struct xen_snd_front_info *front_info)
{
	return xen_snd_front_alsa_init(front_info);
}

static void sndback_disconnect(struct xen_snd_front_info *front_info)
{
	xen_snd_drv_fini(front_info);
	xenbus_switch_state(front_info->xb_dev, XenbusStateInitialising);
}

static void sndback_changed(struct xenbus_device *xb_dev,
			    enum xenbus_state backend_state)
{
	struct xen_snd_front_info *front_info = dev_get_drvdata(&xb_dev->dev);
	int ret;

	dev_dbg(&xb_dev->dev, "Backend state is %s, front is %s\n",
		xenbus_strstate(backend_state),
		xenbus_strstate(xb_dev->state));

	switch (backend_state) {
	case XenbusStateReconfiguring:
	case XenbusStateReconfigured:
	case XenbusStateInitialised:
		break;

	case XenbusStateInitialising:
		/* Recovering after backend unexpected closure. */
		sndback_disconnect(front_info);
		break;

	case XenbusStateInitWait:
		/* Recovering after backend unexpected closure. */
		sndback_disconnect(front_info);

		ret = sndback_initwait(front_info);
		if (ret < 0)
			xenbus_dev_fatal(xb_dev, ret, "initializing frontend");
		else
			xenbus_switch_state(xb_dev, XenbusStateInitialised);
		break;

	case XenbusStateConnected:
		if (xb_dev->state != XenbusStateInitialised)
			break;

		ret = sndback_connect(front_info);
		if (ret < 0)
			xenbus_dev_fatal(xb_dev, ret, "initializing frontend");
		else
			xenbus_switch_state(xb_dev, XenbusStateConnected);
		break;

	case XenbusStateClosing:
		/*
		 * In this state backend starts freeing resources,
		 * so let it go into closed state first, so we can also
		 * remove ours.
		 */
		break;

	case XenbusStateUnknown:
	case XenbusStateClosed:
		if (xb_dev->state == XenbusStateClosed)
			break;

		sndback_disconnect(front_info);
		break;
	}
}

static int xen_drv_probe(struct xenbus_device *xb_dev,
			 const struct xenbus_device_id *id)
{
	struct xen_snd_front_info *front_info;

	front_info = devm_kzalloc(&xb_dev->dev,
				  sizeof(*front_info), GFP_KERNEL);
	if (!front_info)
		return -ENOMEM;

	front_info->xb_dev = xb_dev;
	dev_set_drvdata(&xb_dev->dev, front_info);

	return xenbus_switch_state(xb_dev, XenbusStateInitialising);
}

static int xen_drv_remove(struct xenbus_device *dev)
{
	struct xen_snd_front_info *front_info = dev_get_drvdata(&dev->dev);
	int to = 100;

	xenbus_switch_state(dev, XenbusStateClosing);

	/*
	 * On driver removal it is disconnected from XenBus,
	 * so no backend state change events come via .otherend_changed
	 * callback. This prevents us from exiting gracefully, e.g.
	 * signaling the backend to free event channels, waiting for its
	 * state to change to XenbusStateClosed and cleaning at our end.
	 * Normally when front driver removed backend will finally go into
	 * XenbusStateInitWait state.
	 *
	 * Workaround: read backend's state manually and wait with time-out.
	 */
	while ((xenbus_read_unsigned(front_info->xb_dev->otherend, "state",
				     XenbusStateUnknown) != XenbusStateInitWait) &&
	       --to)
		msleep(10);

	if (!to) {
		unsigned int state;

		state = xenbus_read_unsigned(front_info->xb_dev->otherend,
					     "state", XenbusStateUnknown);
		pr_err("Backend state is %s while removing driver\n",
		       xenbus_strstate(state));
	}

	xen_snd_drv_fini(front_info);
	xenbus_frontend_closed(dev);
	return 0;
}

static const struct xenbus_device_id xen_drv_ids[] = {
	{ XENSND_DRIVER_NAME },
	{ "" }
};

static struct xenbus_driver xen_driver = {
	.ids = xen_drv_ids,
	.probe = xen_drv_probe,
	.remove = xen_drv_remove,
	.otherend_changed = sndback_changed,
};

static int __init xen_drv_init(void)
{
	if (!xen_domain())
		return -ENODEV;

	if (!xen_has_pv_devices())
		return -ENODEV;

	/* At the moment we only support case with XEN_PAGE_SIZE == PAGE_SIZE */
	if (XEN_PAGE_SIZE != PAGE_SIZE) {
		pr_err(XENSND_DRIVER_NAME ": different kernel and Xen page sizes are not supported: XEN_PAGE_SIZE (%lu) != PAGE_SIZE (%lu)\n",
		       XEN_PAGE_SIZE, PAGE_SIZE);
		return -ENODEV;
	}

	pr_info("Initialising Xen " XENSND_DRIVER_NAME " frontend driver\n");
	return xenbus_register_frontend(&xen_driver);
}

static void __exit xen_drv_fini(void)
{
	pr_info("Unregistering Xen " XENSND_DRIVER_NAME " frontend driver\n");
	xenbus_unregister_driver(&xen_driver);
}

module_init(xen_drv_init);
module_exit(xen_drv_fini);

MODULE_DESCRIPTION("Xen virtual sound device frontend");
MODULE_LICENSE("GPL");
MODULE_ALIAS("xen:" XENSND_DRIVER_NAME);
MODULE_SUPPORTED_DEVICE("{{ALSA,Virtual soundcard}}");
