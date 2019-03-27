/*-
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0
 *
 * Copyright (c) 2004, 2005 Mellanox Technologies Ltd.  All rights reserved.
 * Copyright (c) 2004, 2005 Infinicon Corporation.  All rights reserved.
 * Copyright (c) 2004, 2005 Intel Corporation.  All rights reserved.
 * Copyright (c) 2004, 2005 Topspin Corporation.  All rights reserved.
 * Copyright (c) 2004-2007 Voltaire Corporation.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <linux/slab.h>
#include <linux/string.h>

#include "agent.h"
#include "smi.h"
#include "mad_priv.h"

#define SPFX "ib_agent: "

struct ib_agent_port_private {
	struct list_head port_list;
	struct ib_mad_agent *agent[2];
};

static DEFINE_SPINLOCK(ib_agent_port_list_lock);
static LIST_HEAD(ib_agent_port_list);

static struct ib_agent_port_private *
__ib_get_agent_port(const struct ib_device *device, int port_num)
{
	struct ib_agent_port_private *entry;

	list_for_each_entry(entry, &ib_agent_port_list, port_list) {
		if (entry->agent[1]->device == device &&
		    entry->agent[1]->port_num == port_num)
			return entry;
	}
	return NULL;
}

static struct ib_agent_port_private *
ib_get_agent_port(const struct ib_device *device, int port_num)
{
	struct ib_agent_port_private *entry;
	unsigned long flags;

	spin_lock_irqsave(&ib_agent_port_list_lock, flags);
	entry = __ib_get_agent_port(device, port_num);
	spin_unlock_irqrestore(&ib_agent_port_list_lock, flags);
	return entry;
}

void agent_send_response(const struct ib_mad_hdr *mad_hdr, const struct ib_grh *grh,
			 const struct ib_wc *wc, const struct ib_device *device,
			 int port_num, int qpn, size_t resp_mad_len, bool opa)
{
	struct ib_agent_port_private *port_priv;
	struct ib_mad_agent *agent;
	struct ib_mad_send_buf *send_buf;
	struct ib_ah *ah;
	struct ib_mad_send_wr_private *mad_send_wr;

	if (rdma_cap_ib_switch(device))
		port_priv = ib_get_agent_port(device, 0);
	else
		port_priv = ib_get_agent_port(device, port_num);

	if (!port_priv) {
		dev_err(&device->dev, "Unable to find port agent\n");
		return;
	}

	agent = port_priv->agent[qpn];
	ah = ib_create_ah_from_wc(agent->qp->pd, wc, grh, port_num);
	if (IS_ERR(ah)) {
		dev_err(&device->dev, "ib_create_ah_from_wc error %ld\n",
			PTR_ERR(ah));
		return;
	}

	if (opa && mad_hdr->base_version != OPA_MGMT_BASE_VERSION)
		resp_mad_len = IB_MGMT_MAD_SIZE;

	send_buf = ib_create_send_mad(agent, wc->src_qp, wc->pkey_index, 0,
				      IB_MGMT_MAD_HDR,
				      resp_mad_len - IB_MGMT_MAD_HDR,
				      GFP_KERNEL,
				      mad_hdr->base_version);
	if (IS_ERR(send_buf)) {
		dev_err(&device->dev, "ib_create_send_mad error\n");
		goto err1;
	}

	memcpy(send_buf->mad, mad_hdr, resp_mad_len);
	send_buf->ah = ah;

	if (rdma_cap_ib_switch(device)) {
		mad_send_wr = container_of(send_buf,
					   struct ib_mad_send_wr_private,
					   send_buf);
		mad_send_wr->send_wr.port_num = port_num;
	}

	if (ib_post_send_mad(send_buf, NULL)) {
		dev_err(&device->dev, "ib_post_send_mad error\n");
		goto err2;
	}
	return;
err2:
	ib_free_send_mad(send_buf);
err1:
	ib_destroy_ah(ah);
}

static void agent_send_handler(struct ib_mad_agent *mad_agent,
			       struct ib_mad_send_wc *mad_send_wc)
{
	ib_destroy_ah(mad_send_wc->send_buf->ah);
	ib_free_send_mad(mad_send_wc->send_buf);
}

int ib_agent_port_open(struct ib_device *device, int port_num)
{
	struct ib_agent_port_private *port_priv;
	unsigned long flags;
	int ret;

	/* Create new device info */
	port_priv = kzalloc(sizeof *port_priv, GFP_KERNEL);
	if (!port_priv) {
		dev_err(&device->dev, "No memory for ib_agent_port_private\n");
		ret = -ENOMEM;
		goto error1;
	}

	if (rdma_cap_ib_smi(device, port_num)) {
		/* Obtain send only MAD agent for SMI QP */
		port_priv->agent[0] = ib_register_mad_agent(device, port_num,
							    IB_QPT_SMI, NULL, 0,
							    &agent_send_handler,
							    NULL, NULL, 0);
		if (IS_ERR(port_priv->agent[0])) {
			ret = PTR_ERR(port_priv->agent[0]);
			goto error2;
		}
	}

	/* Obtain send only MAD agent for GSI QP */
	port_priv->agent[1] = ib_register_mad_agent(device, port_num,
						    IB_QPT_GSI, NULL, 0,
						    &agent_send_handler,
						    NULL, NULL, 0);
	if (IS_ERR(port_priv->agent[1])) {
		ret = PTR_ERR(port_priv->agent[1]);
		goto error3;
	}

	spin_lock_irqsave(&ib_agent_port_list_lock, flags);
	list_add_tail(&port_priv->port_list, &ib_agent_port_list);
	spin_unlock_irqrestore(&ib_agent_port_list_lock, flags);

	return 0;

error3:
	if (port_priv->agent[0])
		ib_unregister_mad_agent(port_priv->agent[0]);
error2:
	kfree(port_priv);
error1:
	return ret;
}

int ib_agent_port_close(struct ib_device *device, int port_num)
{
	struct ib_agent_port_private *port_priv;
	unsigned long flags;

	spin_lock_irqsave(&ib_agent_port_list_lock, flags);
	port_priv = __ib_get_agent_port(device, port_num);
	if (port_priv == NULL) {
		spin_unlock_irqrestore(&ib_agent_port_list_lock, flags);
		dev_err(&device->dev, "Port %d not found\n", port_num);
		return -ENODEV;
	}
	list_del(&port_priv->port_list);
	spin_unlock_irqrestore(&ib_agent_port_list_lock, flags);

	ib_unregister_mad_agent(port_priv->agent[1]);
	if (port_priv->agent[0])
		ib_unregister_mad_agent(port_priv->agent[0]);

	kfree(port_priv);
	return 0;
}
