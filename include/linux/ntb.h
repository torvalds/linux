/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 *   redistributing this file, you may do so under either license.
 *
 *   GPL LICENSE SUMMARY
 *
 *   Copyright (C) 2015 EMC Corporation. All Rights Reserved.
 *   Copyright (C) 2016 T-Platforms. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of version 2 of the GNU General Public License as
 *   published by the Free Software Foundation.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   General Public License for more details.
 *
 *   BSD LICENSE
 *
 *   Copyright (C) 2015 EMC Corporation. All Rights Reserved.
 *   Copyright (C) 2016 T-Platforms. All Rights Reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copy
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * PCIe NTB Linux driver
 *
 * Contact Information:
 * Allen Hubbe <Allen.Hubbe@emc.com>
 */

#ifndef _NTB_H_
#define _NTB_H_

#include <linux/completion.h>
#include <linux/device.h>

struct ntb_client;
struct ntb_dev;
struct pci_dev;

/**
 * enum ntb_topo - NTB connection topology
 * @NTB_TOPO_NONE:	Topology is unknown or invalid.
 * @NTB_TOPO_PRI:	On primary side of local ntb.
 * @NTB_TOPO_SEC:	On secondary side of remote ntb.
 * @NTB_TOPO_B2B_USD:	On primary side of local ntb upstream of remote ntb.
 * @NTB_TOPO_B2B_DSD:	On primary side of local ntb downstream of remote ntb.
 */
enum ntb_topo {
	NTB_TOPO_NONE = -1,
	NTB_TOPO_PRI,
	NTB_TOPO_SEC,
	NTB_TOPO_B2B_USD,
	NTB_TOPO_B2B_DSD,
};

static inline int ntb_topo_is_b2b(enum ntb_topo topo)
{
	switch ((int)topo) {
	case NTB_TOPO_B2B_USD:
	case NTB_TOPO_B2B_DSD:
		return 1;
	}
	return 0;
}

static inline char *ntb_topo_string(enum ntb_topo topo)
{
	switch (topo) {
	case NTB_TOPO_NONE:	return "NTB_TOPO_NONE";
	case NTB_TOPO_PRI:	return "NTB_TOPO_PRI";
	case NTB_TOPO_SEC:	return "NTB_TOPO_SEC";
	case NTB_TOPO_B2B_USD:	return "NTB_TOPO_B2B_USD";
	case NTB_TOPO_B2B_DSD:	return "NTB_TOPO_B2B_DSD";
	}
	return "NTB_TOPO_INVALID";
}

/**
 * enum ntb_speed - NTB link training speed
 * @NTB_SPEED_AUTO:	Request the max supported speed.
 * @NTB_SPEED_NONE:	Link is not trained to any speed.
 * @NTB_SPEED_GEN1:	Link is trained to gen1 speed.
 * @NTB_SPEED_GEN2:	Link is trained to gen2 speed.
 * @NTB_SPEED_GEN3:	Link is trained to gen3 speed.
 * @NTB_SPEED_GEN4:	Link is trained to gen4 speed.
 */
enum ntb_speed {
	NTB_SPEED_AUTO = -1,
	NTB_SPEED_NONE = 0,
	NTB_SPEED_GEN1 = 1,
	NTB_SPEED_GEN2 = 2,
	NTB_SPEED_GEN3 = 3,
	NTB_SPEED_GEN4 = 4
};

/**
 * enum ntb_width - NTB link training width
 * @NTB_WIDTH_AUTO:	Request the max supported width.
 * @NTB_WIDTH_NONE:	Link is not trained to any width.
 * @NTB_WIDTH_1:	Link is trained to 1 lane width.
 * @NTB_WIDTH_2:	Link is trained to 2 lane width.
 * @NTB_WIDTH_4:	Link is trained to 4 lane width.
 * @NTB_WIDTH_8:	Link is trained to 8 lane width.
 * @NTB_WIDTH_12:	Link is trained to 12 lane width.
 * @NTB_WIDTH_16:	Link is trained to 16 lane width.
 * @NTB_WIDTH_32:	Link is trained to 32 lane width.
 */
enum ntb_width {
	NTB_WIDTH_AUTO = -1,
	NTB_WIDTH_NONE = 0,
	NTB_WIDTH_1 = 1,
	NTB_WIDTH_2 = 2,
	NTB_WIDTH_4 = 4,
	NTB_WIDTH_8 = 8,
	NTB_WIDTH_12 = 12,
	NTB_WIDTH_16 = 16,
	NTB_WIDTH_32 = 32,
};

/**
 * enum ntb_default_port - NTB default port number
 * @NTB_PORT_PRI_USD:	Default port of the NTB_TOPO_PRI/NTB_TOPO_B2B_USD
 *			topologies
 * @NTB_PORT_SEC_DSD:	Default port of the NTB_TOPO_SEC/NTB_TOPO_B2B_DSD
 *			topologies
 */
enum ntb_default_port {
	NTB_PORT_PRI_USD,
	NTB_PORT_SEC_DSD
};
#define NTB_DEF_PEER_CNT	(1)
#define NTB_DEF_PEER_IDX	(0)

/**
 * struct ntb_client_ops - ntb client operations
 * @probe:		Notify client of a new device.
 * @remove:		Notify client to remove a device.
 */
struct ntb_client_ops {
	int (*probe)(struct ntb_client *client, struct ntb_dev *ntb);
	void (*remove)(struct ntb_client *client, struct ntb_dev *ntb);
};

static inline int ntb_client_ops_is_valid(const struct ntb_client_ops *ops)
{
	/* commented callbacks are not required: */
	return
		ops->probe			&&
		ops->remove			&&
		1;
}

/**
 * struct ntb_ctx_ops - ntb driver context operations
 * @link_event:		See ntb_link_event().
 * @db_event:		See ntb_db_event().
 * @msg_event:		See ntb_msg_event().
 */
struct ntb_ctx_ops {
	void (*link_event)(void *ctx);
	void (*db_event)(void *ctx, int db_vector);
	void (*msg_event)(void *ctx);
};

static inline int ntb_ctx_ops_is_valid(const struct ntb_ctx_ops *ops)
{
	/* commented callbacks are not required: */
	return
		/* ops->link_event		&& */
		/* ops->db_event		&& */
		/* ops->msg_event		&& */
		1;
}

/**
 * struct ntb_ctx_ops - ntb device operations
 * @port_number:	See ntb_port_number().
 * @peer_port_count:	See ntb_peer_port_count().
 * @peer_port_number:	See ntb_peer_port_number().
 * @peer_port_idx:	See ntb_peer_port_idx().
 * @link_is_up:		See ntb_link_is_up().
 * @link_enable:	See ntb_link_enable().
 * @link_disable:	See ntb_link_disable().
 * @mw_count:		See ntb_mw_count().
 * @mw_get_align:	See ntb_mw_get_align().
 * @mw_set_trans:	See ntb_mw_set_trans().
 * @mw_clear_trans:	See ntb_mw_clear_trans().
 * @peer_mw_count:	See ntb_peer_mw_count().
 * @peer_mw_get_addr:	See ntb_peer_mw_get_addr().
 * @peer_mw_set_trans:	See ntb_peer_mw_set_trans().
 * @peer_mw_clear_trans:See ntb_peer_mw_clear_trans().
 * @db_is_unsafe:	See ntb_db_is_unsafe().
 * @db_valid_mask:	See ntb_db_valid_mask().
 * @db_vector_count:	See ntb_db_vector_count().
 * @db_vector_mask:	See ntb_db_vector_mask().
 * @db_read:		See ntb_db_read().
 * @db_set:		See ntb_db_set().
 * @db_clear:		See ntb_db_clear().
 * @db_read_mask:	See ntb_db_read_mask().
 * @db_set_mask:	See ntb_db_set_mask().
 * @db_clear_mask:	See ntb_db_clear_mask().
 * @peer_db_addr:	See ntb_peer_db_addr().
 * @peer_db_read:	See ntb_peer_db_read().
 * @peer_db_set:	See ntb_peer_db_set().
 * @peer_db_clear:	See ntb_peer_db_clear().
 * @peer_db_read_mask:	See ntb_peer_db_read_mask().
 * @peer_db_set_mask:	See ntb_peer_db_set_mask().
 * @peer_db_clear_mask:	See ntb_peer_db_clear_mask().
 * @spad_is_unsafe:	See ntb_spad_is_unsafe().
 * @spad_count:		See ntb_spad_count().
 * @spad_read:		See ntb_spad_read().
 * @spad_write:		See ntb_spad_write().
 * @peer_spad_addr:	See ntb_peer_spad_addr().
 * @peer_spad_read:	See ntb_peer_spad_read().
 * @peer_spad_write:	See ntb_peer_spad_write().
 * @msg_count:		See ntb_msg_count().
 * @msg_inbits:		See ntb_msg_inbits().
 * @msg_outbits:	See ntb_msg_outbits().
 * @msg_read_sts:	See ntb_msg_read_sts().
 * @msg_clear_sts:	See ntb_msg_clear_sts().
 * @msg_set_mask:	See ntb_msg_set_mask().
 * @msg_clear_mask:	See ntb_msg_clear_mask().
 * @msg_read:		See ntb_msg_read().
 * @msg_write:		See ntb_msg_write().
 */
struct ntb_dev_ops {
	int (*port_number)(struct ntb_dev *ntb);
	int (*peer_port_count)(struct ntb_dev *ntb);
	int (*peer_port_number)(struct ntb_dev *ntb, int pidx);
	int (*peer_port_idx)(struct ntb_dev *ntb, int port);

	u64 (*link_is_up)(struct ntb_dev *ntb,
			  enum ntb_speed *speed, enum ntb_width *width);
	int (*link_enable)(struct ntb_dev *ntb,
			   enum ntb_speed max_speed, enum ntb_width max_width);
	int (*link_disable)(struct ntb_dev *ntb);

	int (*mw_count)(struct ntb_dev *ntb, int pidx);
	int (*mw_get_align)(struct ntb_dev *ntb, int pidx, int widx,
			    resource_size_t *addr_align,
			    resource_size_t *size_align,
			    resource_size_t *size_max);
	int (*mw_set_trans)(struct ntb_dev *ntb, int pidx, int widx,
			    dma_addr_t addr, resource_size_t size);
	int (*mw_clear_trans)(struct ntb_dev *ntb, int pidx, int widx);
	int (*peer_mw_count)(struct ntb_dev *ntb);
	int (*peer_mw_get_addr)(struct ntb_dev *ntb, int widx,
				phys_addr_t *base, resource_size_t *size);
	int (*peer_mw_set_trans)(struct ntb_dev *ntb, int pidx, int widx,
				 u64 addr, resource_size_t size);
	int (*peer_mw_clear_trans)(struct ntb_dev *ntb, int pidx, int widx);

	int (*db_is_unsafe)(struct ntb_dev *ntb);
	u64 (*db_valid_mask)(struct ntb_dev *ntb);
	int (*db_vector_count)(struct ntb_dev *ntb);
	u64 (*db_vector_mask)(struct ntb_dev *ntb, int db_vector);

	u64 (*db_read)(struct ntb_dev *ntb);
	int (*db_set)(struct ntb_dev *ntb, u64 db_bits);
	int (*db_clear)(struct ntb_dev *ntb, u64 db_bits);

	u64 (*db_read_mask)(struct ntb_dev *ntb);
	int (*db_set_mask)(struct ntb_dev *ntb, u64 db_bits);
	int (*db_clear_mask)(struct ntb_dev *ntb, u64 db_bits);

	int (*peer_db_addr)(struct ntb_dev *ntb,
			    phys_addr_t *db_addr, resource_size_t *db_size);
	u64 (*peer_db_read)(struct ntb_dev *ntb);
	int (*peer_db_set)(struct ntb_dev *ntb, u64 db_bits);
	int (*peer_db_clear)(struct ntb_dev *ntb, u64 db_bits);

	u64 (*peer_db_read_mask)(struct ntb_dev *ntb);
	int (*peer_db_set_mask)(struct ntb_dev *ntb, u64 db_bits);
	int (*peer_db_clear_mask)(struct ntb_dev *ntb, u64 db_bits);

	int (*spad_is_unsafe)(struct ntb_dev *ntb);
	int (*spad_count)(struct ntb_dev *ntb);

	u32 (*spad_read)(struct ntb_dev *ntb, int sidx);
	int (*spad_write)(struct ntb_dev *ntb, int sidx, u32 val);

	int (*peer_spad_addr)(struct ntb_dev *ntb, int pidx, int sidx,
			      phys_addr_t *spad_addr);
	u32 (*peer_spad_read)(struct ntb_dev *ntb, int pidx, int sidx);
	int (*peer_spad_write)(struct ntb_dev *ntb, int pidx, int sidx,
			       u32 val);

	int (*msg_count)(struct ntb_dev *ntb);
	u64 (*msg_inbits)(struct ntb_dev *ntb);
	u64 (*msg_outbits)(struct ntb_dev *ntb);
	u64 (*msg_read_sts)(struct ntb_dev *ntb);
	int (*msg_clear_sts)(struct ntb_dev *ntb, u64 sts_bits);
	int (*msg_set_mask)(struct ntb_dev *ntb, u64 mask_bits);
	int (*msg_clear_mask)(struct ntb_dev *ntb, u64 mask_bits);
	int (*msg_read)(struct ntb_dev *ntb, int midx, int *pidx, u32 *msg);
	int (*msg_write)(struct ntb_dev *ntb, int midx, int pidx, u32 msg);
};

static inline int ntb_dev_ops_is_valid(const struct ntb_dev_ops *ops)
{
	/* commented callbacks are not required: */
	return
		/* Port operations are required for multiport devices */
		!ops->peer_port_count == !ops->port_number	&&
		!ops->peer_port_number == !ops->port_number	&&
		!ops->peer_port_idx == !ops->port_number	&&

		/* Link operations are required */
		ops->link_is_up				&&
		ops->link_enable			&&
		ops->link_disable			&&

		/* One or both MW interfaces should be developed */
		ops->mw_count				&&
		ops->mw_get_align			&&
		(ops->mw_set_trans			||
		 ops->peer_mw_set_trans)		&&
		/* ops->mw_clear_trans			&& */
		ops->peer_mw_count			&&
		ops->peer_mw_get_addr			&&
		/* ops->peer_mw_clear_trans		&& */

		/* Doorbell operations are mostly required */
		/* ops->db_is_unsafe			&& */
		ops->db_valid_mask			&&
		/* both set, or both unset */
		(!ops->db_vector_count == !ops->db_vector_mask)	&&
		ops->db_read				&&
		/* ops->db_set				&& */
		ops->db_clear				&&
		/* ops->db_read_mask			&& */
		ops->db_set_mask			&&
		ops->db_clear_mask			&&
		/* ops->peer_db_addr			&& */
		/* ops->peer_db_read			&& */
		ops->peer_db_set			&&
		/* ops->peer_db_clear			&& */
		/* ops->peer_db_read_mask		&& */
		/* ops->peer_db_set_mask		&& */
		/* ops->peer_db_clear_mask		&& */

		/* Scrachpads interface is optional */
		/* !ops->spad_is_unsafe == !ops->spad_count	&& */
		!ops->spad_read == !ops->spad_count		&&
		!ops->spad_write == !ops->spad_count		&&
		/* !ops->peer_spad_addr == !ops->spad_count	&& */
		/* !ops->peer_spad_read == !ops->spad_count	&& */
		!ops->peer_spad_write == !ops->spad_count	&&

		/* Messaging interface is optional */
		!ops->msg_inbits == !ops->msg_count		&&
		!ops->msg_outbits == !ops->msg_count		&&
		!ops->msg_read_sts == !ops->msg_count		&&
		!ops->msg_clear_sts == !ops->msg_count		&&
		/* !ops->msg_set_mask == !ops->msg_count	&& */
		/* !ops->msg_clear_mask == !ops->msg_count	&& */
		!ops->msg_read == !ops->msg_count		&&
		!ops->msg_write == !ops->msg_count		&&
		1;
}

/**
 * struct ntb_client - client interested in ntb devices
 * @drv:		Linux driver object.
 * @ops:		See &ntb_client_ops.
 */
struct ntb_client {
	struct device_driver		drv;
	const struct ntb_client_ops	ops;
};
#define drv_ntb_client(__drv) container_of((__drv), struct ntb_client, drv)

/**
 * struct ntb_device - ntb device
 * @dev:		Linux device object.
 * @pdev:		PCI device entry of the ntb.
 * @topo:		Detected topology of the ntb.
 * @ops:		See &ntb_dev_ops.
 * @ctx:		See &ntb_ctx_ops.
 * @ctx_ops:		See &ntb_ctx_ops.
 */
struct ntb_dev {
	struct device			dev;
	struct pci_dev			*pdev;
	enum ntb_topo			topo;
	const struct ntb_dev_ops	*ops;
	void				*ctx;
	const struct ntb_ctx_ops	*ctx_ops;

	/* private: */

	/* synchronize setting, clearing, and calling ctx_ops */
	spinlock_t			ctx_lock;
	/* block unregister until device is fully released */
	struct completion		released;
};
#define dev_ntb(__dev) container_of((__dev), struct ntb_dev, dev)

/**
 * ntb_register_client() - register a client for interest in ntb devices
 * @client:	Client context.
 *
 * The client will be added to the list of clients interested in ntb devices.
 * The client will be notified of any ntb devices that are not already
 * associated with a client, or if ntb devices are registered later.
 *
 * Return: Zero if the client is registered, otherwise an error number.
 */
#define ntb_register_client(client) \
	__ntb_register_client((client), THIS_MODULE, KBUILD_MODNAME)

int __ntb_register_client(struct ntb_client *client, struct module *mod,
			  const char *mod_name);

/**
 * ntb_unregister_client() - unregister a client for interest in ntb devices
 * @client:	Client context.
 *
 * The client will be removed from the list of clients interested in ntb
 * devices.  If any ntb devices are associated with the client, the client will
 * be notified to remove those devices.
 */
void ntb_unregister_client(struct ntb_client *client);

#define module_ntb_client(__ntb_client) \
	module_driver(__ntb_client, ntb_register_client, \
			ntb_unregister_client)

/**
 * ntb_register_device() - register a ntb device
 * @ntb:	NTB device context.
 *
 * The device will be added to the list of ntb devices.  If any clients are
 * interested in ntb devices, each client will be notified of the ntb device,
 * until at most one client accepts the device.
 *
 * Return: Zero if the device is registered, otherwise an error number.
 */
int ntb_register_device(struct ntb_dev *ntb);

/**
 * ntb_register_device() - unregister a ntb device
 * @ntb:	NTB device context.
 *
 * The device will be removed from the list of ntb devices.  If the ntb device
 * is associated with a client, the client will be notified to remove the
 * device.
 */
void ntb_unregister_device(struct ntb_dev *ntb);

/**
 * ntb_set_ctx() - associate a driver context with an ntb device
 * @ntb:	NTB device context.
 * @ctx:	Driver context.
 * @ctx_ops:	Driver context operations.
 *
 * Associate a driver context and operations with a ntb device.  The context is
 * provided by the client driver, and the driver may associate a different
 * context with each ntb device.
 *
 * Return: Zero if the context is associated, otherwise an error number.
 */
int ntb_set_ctx(struct ntb_dev *ntb, void *ctx,
		const struct ntb_ctx_ops *ctx_ops);

/**
 * ntb_clear_ctx() - disassociate any driver context from an ntb device
 * @ntb:	NTB device context.
 *
 * Clear any association that may exist between a driver context and the ntb
 * device.
 */
void ntb_clear_ctx(struct ntb_dev *ntb);

/**
 * ntb_link_event() - notify driver context of a change in link status
 * @ntb:	NTB device context.
 *
 * Notify the driver context that the link status may have changed.  The driver
 * should call ntb_link_is_up() to get the current status.
 */
void ntb_link_event(struct ntb_dev *ntb);

/**
 * ntb_db_event() - notify driver context of a doorbell event
 * @ntb:	NTB device context.
 * @vector:	Interrupt vector number.
 *
 * Notify the driver context of a doorbell event.  If hardware supports
 * multiple interrupt vectors for doorbells, the vector number indicates which
 * vector received the interrupt.  The vector number is relative to the first
 * vector used for doorbells, starting at zero, and must be less than
 * ntb_db_vector_count().  The driver may call ntb_db_read() to check which
 * doorbell bits need service, and ntb_db_vector_mask() to determine which of
 * those bits are associated with the vector number.
 */
void ntb_db_event(struct ntb_dev *ntb, int vector);

/**
 * ntb_msg_event() - notify driver context of a message event
 * @ntb:	NTB device context.
 *
 * Notify the driver context of a message event.  If hardware supports
 * message registers, this event indicates, that a new message arrived in
 * some incoming message register or last sent message couldn't be delivered.
 * The events can be masked/unmasked by the methods ntb_msg_set_mask() and
 * ntb_msg_clear_mask().
 */
void ntb_msg_event(struct ntb_dev *ntb);

/**
 * ntb_default_port_number() - get the default local port number
 * @ntb:	NTB device context.
 *
 * If hardware driver doesn't specify port_number() callback method, the NTB
 * is considered with just two ports. So this method returns default local
 * port number in compliance with topology.
 *
 * NOTE Don't call this method directly. The ntb_port_number() function should
 * be used instead.
 *
 * Return: the default local port number
 */
int ntb_default_port_number(struct ntb_dev *ntb);

/**
 * ntb_default_port_count() - get the default number of peer device ports
 * @ntb:	NTB device context.
 *
 * By default hardware driver supports just one peer device.
 *
 * NOTE Don't call this method directly. The ntb_peer_port_count() function
 * should be used instead.
 *
 * Return: the default number of peer ports
 */
int ntb_default_peer_port_count(struct ntb_dev *ntb);

/**
 * ntb_default_peer_port_number() - get the default peer port by given index
 * @ntb:	NTB device context.
 * @idx:	Peer port index (should not differ from zero).
 *
 * By default hardware driver supports just one peer device, so this method
 * shall return the corresponding value from enum ntb_default_port.
 *
 * NOTE Don't call this method directly. The ntb_peer_port_number() function
 * should be used instead.
 *
 * Return: the peer device port or negative value indicating an error
 */
int ntb_default_peer_port_number(struct ntb_dev *ntb, int pidx);

/**
 * ntb_default_peer_port_idx() - get the default peer device port index by
 *				 given port number
 * @ntb:	NTB device context.
 * @port:	Peer port number (should be one of enum ntb_default_port).
 *
 * By default hardware driver supports just one peer device, so while
 * specified port-argument indicates peer port from enum ntb_default_port,
 * the return value shall be zero.
 *
 * NOTE Don't call this method directly. The ntb_peer_port_idx() function
 * should be used instead.
 *
 * Return: the peer port index or negative value indicating an error
 */
int ntb_default_peer_port_idx(struct ntb_dev *ntb, int port);

/**
 * ntb_port_number() - get the local port number
 * @ntb:	NTB device context.
 *
 * Hardware must support at least simple two-ports ntb connection
 *
 * Return: the local port number
 */
static inline int ntb_port_number(struct ntb_dev *ntb)
{
	if (!ntb->ops->port_number)
		return ntb_default_port_number(ntb);

	return ntb->ops->port_number(ntb);
}

/**
 * ntb_peer_port_count() - get the number of peer device ports
 * @ntb:	NTB device context.
 *
 * Hardware may support an access to memory of several remote domains
 * over multi-port NTB devices. This method returns the number of peers,
 * local device can have shared memory with.
 *
 * Return: the number of peer ports
 */
static inline int ntb_peer_port_count(struct ntb_dev *ntb)
{
	if (!ntb->ops->peer_port_count)
		return ntb_default_peer_port_count(ntb);

	return ntb->ops->peer_port_count(ntb);
}

/**
 * ntb_peer_port_number() - get the peer port by given index
 * @ntb:	NTB device context.
 * @pidx:	Peer port index.
 *
 * Peer ports are continuously enumerated by NTB API logic, so this method
 * lets to retrieve port real number by its index.
 *
 * Return: the peer device port or negative value indicating an error
 */
static inline int ntb_peer_port_number(struct ntb_dev *ntb, int pidx)
{
	if (!ntb->ops->peer_port_number)
		return ntb_default_peer_port_number(ntb, pidx);

	return ntb->ops->peer_port_number(ntb, pidx);
}

/**
 * ntb_peer_port_idx() - get the peer device port index by given port number
 * @ntb:	NTB device context.
 * @port:	Peer port number.
 *
 * Inverse operation of ntb_peer_port_number(), so one can get port index
 * by specified port number.
 *
 * Return: the peer port index or negative value indicating an error
 */
static inline int ntb_peer_port_idx(struct ntb_dev *ntb, int port)
{
	if (!ntb->ops->peer_port_idx)
		return ntb_default_peer_port_idx(ntb, port);

	return ntb->ops->peer_port_idx(ntb, port);
}

/**
 * ntb_link_is_up() - get the current ntb link state
 * @ntb:	NTB device context.
 * @speed:	OUT - The link speed expressed as PCIe generation number.
 * @width:	OUT - The link width expressed as the number of PCIe lanes.
 *
 * Get the current state of the ntb link.  It is recommended to query the link
 * state once after every link event.  It is safe to query the link state in
 * the context of the link event callback.
 *
 * Return: bitfield of indexed ports link state: bit is set/cleared if the
 *         link is up/down respectively.
 */
static inline u64 ntb_link_is_up(struct ntb_dev *ntb,
				 enum ntb_speed *speed, enum ntb_width *width)
{
	return ntb->ops->link_is_up(ntb, speed, width);
}

/**
 * ntb_link_enable() - enable the local port ntb connection
 * @ntb:	NTB device context.
 * @max_speed:	The maximum link speed expressed as PCIe generation number.
 * @max_width:	The maximum link width expressed as the number of PCIe lanes.
 *
 * Enable the NTB/PCIe link on the local or remote (for bridge-to-bridge
 * topology) side of the bridge. If it's supported the ntb device should train
 * the link to its maximum speed and width, or the requested speed and width,
 * whichever is smaller. Some hardware doesn't support PCIe link training, so
 * the last two arguments will be ignored then.
 *
 * Return: Zero on success, otherwise an error number.
 */
static inline int ntb_link_enable(struct ntb_dev *ntb,
				  enum ntb_speed max_speed,
				  enum ntb_width max_width)
{
	return ntb->ops->link_enable(ntb, max_speed, max_width);
}

/**
 * ntb_link_disable() - disable the local port ntb connection
 * @ntb:	NTB device context.
 *
 * Disable the link on the local or remote (for b2b topology) of the ntb.
 * The ntb device should disable the link.  Returning from this call must
 * indicate that a barrier has passed, though with no more writes may pass in
 * either direction across the link, except if this call returns an error
 * number.
 *
 * Return: Zero on success, otherwise an error number.
 */
static inline int ntb_link_disable(struct ntb_dev *ntb)
{
	return ntb->ops->link_disable(ntb);
}

/**
 * ntb_mw_count() - get the number of inbound memory windows, which could
 *                  be created for a specified peer device
 * @ntb:	NTB device context.
 * @pidx:	Port index of peer device.
 *
 * Hardware and topology may support a different number of memory windows.
 * Moreover different peer devices can support different number of memory
 * windows. Simply speaking this method returns the number of possible inbound
 * memory windows to share with specified peer device. Note: this may return
 * zero if the link is not up yet.
 *
 * Return: the number of memory windows.
 */
static inline int ntb_mw_count(struct ntb_dev *ntb, int pidx)
{
	return ntb->ops->mw_count(ntb, pidx);
}

/**
 * ntb_mw_get_align() - get the restriction parameters of inbound memory window
 * @ntb:	NTB device context.
 * @pidx:	Port index of peer device.
 * @widx:	Memory window index.
 * @addr_align:	OUT - the base alignment for translating the memory window
 * @size_align:	OUT - the size alignment for translating the memory window
 * @size_max:	OUT - the maximum size of the memory window
 *
 * Get the alignments of an inbound memory window with specified index.
 * NULL may be given for any output parameter if the value is not needed.
 * The alignment and size parameters may be used for allocation of proper
 * shared memory. Note: this must only be called when the link is up.
 *
 * Return: Zero on success, otherwise a negative error number.
 */
static inline int ntb_mw_get_align(struct ntb_dev *ntb, int pidx, int widx,
				   resource_size_t *addr_align,
				   resource_size_t *size_align,
				   resource_size_t *size_max)
{
	if (!(ntb_link_is_up(ntb, NULL, NULL) & (1 << pidx)))
		return -ENOTCONN;

	return ntb->ops->mw_get_align(ntb, pidx, widx, addr_align, size_align,
				      size_max);
}

/**
 * ntb_mw_set_trans() - set the translation of an inbound memory window
 * @ntb:	NTB device context.
 * @pidx:	Port index of peer device.
 * @widx:	Memory window index.
 * @addr:	The dma address of local memory to expose to the peer.
 * @size:	The size of the local memory to expose to the peer.
 *
 * Set the translation of a memory window.  The peer may access local memory
 * through the window starting at the address, up to the size.  The address
 * and size must be aligned in compliance with restrictions of
 * ntb_mw_get_align(). The region size should not exceed the size_max parameter
 * of that method.
 *
 * This method may not be implemented due to the hardware specific memory
 * windows interface.
 *
 * Return: Zero on success, otherwise an error number.
 */
static inline int ntb_mw_set_trans(struct ntb_dev *ntb, int pidx, int widx,
				   dma_addr_t addr, resource_size_t size)
{
	if (!ntb->ops->mw_set_trans)
		return 0;

	return ntb->ops->mw_set_trans(ntb, pidx, widx, addr, size);
}

/**
 * ntb_mw_clear_trans() - clear the translation address of an inbound memory
 *                        window
 * @ntb:	NTB device context.
 * @pidx:	Port index of peer device.
 * @widx:	Memory window index.
 *
 * Clear the translation of an inbound memory window.  The peer may no longer
 * access local memory through the window.
 *
 * Return: Zero on success, otherwise an error number.
 */
static inline int ntb_mw_clear_trans(struct ntb_dev *ntb, int pidx, int widx)
{
	if (!ntb->ops->mw_clear_trans)
		return ntb_mw_set_trans(ntb, pidx, widx, 0, 0);

	return ntb->ops->mw_clear_trans(ntb, pidx, widx);
}

/**
 * ntb_peer_mw_count() - get the number of outbound memory windows, which could
 *                       be mapped to access a shared memory
 * @ntb:	NTB device context.
 *
 * Hardware and topology may support a different number of memory windows.
 * This method returns the number of outbound memory windows supported by
 * local device.
 *
 * Return: the number of memory windows.
 */
static inline int ntb_peer_mw_count(struct ntb_dev *ntb)
{
	return ntb->ops->peer_mw_count(ntb);
}

/**
 * ntb_peer_mw_get_addr() - get map address of an outbound memory window
 * @ntb:	NTB device context.
 * @widx:	Memory window index (within ntb_peer_mw_count() return value).
 * @base:	OUT - the base address of mapping region.
 * @size:	OUT - the size of mapping region.
 *
 * Get base and size of memory region to map.  NULL may be given for any output
 * parameter if the value is not needed.  The base and size may be used for
 * mapping the memory window, to access the peer memory.
 *
 * Return: Zero on success, otherwise a negative error number.
 */
static inline int ntb_peer_mw_get_addr(struct ntb_dev *ntb, int widx,
				      phys_addr_t *base, resource_size_t *size)
{
	return ntb->ops->peer_mw_get_addr(ntb, widx, base, size);
}

/**
 * ntb_peer_mw_set_trans() - set a translation address of a memory window
 *                           retrieved from a peer device
 * @ntb:	NTB device context.
 * @pidx:	Port index of peer device the translation address received from.
 * @widx:	Memory window index.
 * @addr:	The dma address of the shared memory to access.
 * @size:	The size of the shared memory to access.
 *
 * Set the translation of an outbound memory window.  The local device may
 * access shared memory allocated by a peer device sent the address.
 *
 * This method may not be implemented due to the hardware specific memory
 * windows interface, so a translation address can be only set on the side,
 * where shared memory (inbound memory windows) is allocated.
 *
 * Return: Zero on success, otherwise an error number.
 */
static inline int ntb_peer_mw_set_trans(struct ntb_dev *ntb, int pidx, int widx,
					u64 addr, resource_size_t size)
{
	if (!ntb->ops->peer_mw_set_trans)
		return 0;

	return ntb->ops->peer_mw_set_trans(ntb, pidx, widx, addr, size);
}

/**
 * ntb_peer_mw_clear_trans() - clear the translation address of an outbound
 *                             memory window
 * @ntb:	NTB device context.
 * @pidx:	Port index of peer device.
 * @widx:	Memory window index.
 *
 * Clear the translation of a outbound memory window.  The local device may no
 * longer access a shared memory through the window.
 *
 * This method may not be implemented due to the hardware specific memory
 * windows interface.
 *
 * Return: Zero on success, otherwise an error number.
 */
static inline int ntb_peer_mw_clear_trans(struct ntb_dev *ntb, int pidx,
					  int widx)
{
	if (!ntb->ops->peer_mw_clear_trans)
		return ntb_peer_mw_set_trans(ntb, pidx, widx, 0, 0);

	return ntb->ops->peer_mw_clear_trans(ntb, pidx, widx);
}

/**
 * ntb_db_is_unsafe() - check if it is safe to use hardware doorbell
 * @ntb:	NTB device context.
 *
 * It is possible for some ntb hardware to be affected by errata.  Hardware
 * drivers can advise clients to avoid using doorbells.  Clients may ignore
 * this advice, though caution is recommended.
 *
 * Return: Zero if it is safe to use doorbells, or One if it is not safe.
 */
static inline int ntb_db_is_unsafe(struct ntb_dev *ntb)
{
	if (!ntb->ops->db_is_unsafe)
		return 0;

	return ntb->ops->db_is_unsafe(ntb);
}

/**
 * ntb_db_valid_mask() - get a mask of doorbell bits supported by the ntb
 * @ntb:	NTB device context.
 *
 * Hardware may support different number or arrangement of doorbell bits.
 *
 * Return: A mask of doorbell bits supported by the ntb.
 */
static inline u64 ntb_db_valid_mask(struct ntb_dev *ntb)
{
	return ntb->ops->db_valid_mask(ntb);
}

/**
 * ntb_db_vector_count() - get the number of doorbell interrupt vectors
 * @ntb:	NTB device context.
 *
 * Hardware may support different number of interrupt vectors.
 *
 * Return: The number of doorbell interrupt vectors.
 */
static inline int ntb_db_vector_count(struct ntb_dev *ntb)
{
	if (!ntb->ops->db_vector_count)
		return 1;

	return ntb->ops->db_vector_count(ntb);
}

/**
 * ntb_db_vector_mask() - get a mask of doorbell bits serviced by a vector
 * @ntb:	NTB device context.
 * @vector:	Doorbell vector number.
 *
 * Each interrupt vector may have a different number or arrangement of bits.
 *
 * Return: A mask of doorbell bits serviced by a vector.
 */
static inline u64 ntb_db_vector_mask(struct ntb_dev *ntb, int vector)
{
	if (!ntb->ops->db_vector_mask)
		return ntb_db_valid_mask(ntb);

	return ntb->ops->db_vector_mask(ntb, vector);
}

/**
 * ntb_db_read() - read the local doorbell register
 * @ntb:	NTB device context.
 *
 * Read the local doorbell register, and return the bits that are set.
 *
 * Return: The bits currently set in the local doorbell register.
 */
static inline u64 ntb_db_read(struct ntb_dev *ntb)
{
	return ntb->ops->db_read(ntb);
}

/**
 * ntb_db_set() - set bits in the local doorbell register
 * @ntb:	NTB device context.
 * @db_bits:	Doorbell bits to set.
 *
 * Set bits in the local doorbell register, which may generate a local doorbell
 * interrupt.  Bits that were already set must remain set.
 *
 * This is unusual, and hardware may not support it.
 *
 * Return: Zero on success, otherwise an error number.
 */
static inline int ntb_db_set(struct ntb_dev *ntb, u64 db_bits)
{
	if (!ntb->ops->db_set)
		return -EINVAL;

	return ntb->ops->db_set(ntb, db_bits);
}

/**
 * ntb_db_clear() - clear bits in the local doorbell register
 * @ntb:	NTB device context.
 * @db_bits:	Doorbell bits to clear.
 *
 * Clear bits in the local doorbell register, arming the bits for the next
 * doorbell.
 *
 * Return: Zero on success, otherwise an error number.
 */
static inline int ntb_db_clear(struct ntb_dev *ntb, u64 db_bits)
{
	return ntb->ops->db_clear(ntb, db_bits);
}

/**
 * ntb_db_read_mask() - read the local doorbell mask
 * @ntb:	NTB device context.
 *
 * Read the local doorbell mask register, and return the bits that are set.
 *
 * This is unusual, though hardware is likely to support it.
 *
 * Return: The bits currently set in the local doorbell mask register.
 */
static inline u64 ntb_db_read_mask(struct ntb_dev *ntb)
{
	if (!ntb->ops->db_read_mask)
		return 0;

	return ntb->ops->db_read_mask(ntb);
}

/**
 * ntb_db_set_mask() - set bits in the local doorbell mask
 * @ntb:	NTB device context.
 * @db_bits:	Doorbell mask bits to set.
 *
 * Set bits in the local doorbell mask register, preventing doorbell interrupts
 * from being generated for those doorbell bits.  Bits that were already set
 * must remain set.
 *
 * Return: Zero on success, otherwise an error number.
 */
static inline int ntb_db_set_mask(struct ntb_dev *ntb, u64 db_bits)
{
	return ntb->ops->db_set_mask(ntb, db_bits);
}

/**
 * ntb_db_clear_mask() - clear bits in the local doorbell mask
 * @ntb:	NTB device context.
 * @db_bits:	Doorbell bits to clear.
 *
 * Clear bits in the local doorbell mask register, allowing doorbell interrupts
 * from being generated for those doorbell bits.  If a doorbell bit is already
 * set at the time the mask is cleared, and the corresponding mask bit is
 * changed from set to clear, then the ntb driver must ensure that
 * ntb_db_event() is called.  If the hardware does not generate the interrupt
 * on clearing the mask bit, then the driver must call ntb_db_event() anyway.
 *
 * Return: Zero on success, otherwise an error number.
 */
static inline int ntb_db_clear_mask(struct ntb_dev *ntb, u64 db_bits)
{
	return ntb->ops->db_clear_mask(ntb, db_bits);
}

/**
 * ntb_peer_db_addr() - address and size of the peer doorbell register
 * @ntb:	NTB device context.
 * @db_addr:	OUT - The address of the peer doorbell register.
 * @db_size:	OUT - The number of bytes to write the peer doorbell register.
 *
 * Return the address of the peer doorbell register.  This may be used, for
 * example, by drivers that offload memory copy operations to a dma engine.
 * The drivers may wish to ring the peer doorbell at the completion of memory
 * copy operations.  For efficiency, and to simplify ordering of operations
 * between the dma memory copies and the ringing doorbell, the driver may
 * append one additional dma memory copy with the doorbell register as the
 * destination, after the memory copy operations.
 *
 * Return: Zero on success, otherwise an error number.
 */
static inline int ntb_peer_db_addr(struct ntb_dev *ntb,
				   phys_addr_t *db_addr,
				   resource_size_t *db_size)
{
	if (!ntb->ops->peer_db_addr)
		return -EINVAL;

	return ntb->ops->peer_db_addr(ntb, db_addr, db_size);
}

/**
 * ntb_peer_db_read() - read the peer doorbell register
 * @ntb:	NTB device context.
 *
 * Read the peer doorbell register, and return the bits that are set.
 *
 * This is unusual, and hardware may not support it.
 *
 * Return: The bits currently set in the peer doorbell register.
 */
static inline u64 ntb_peer_db_read(struct ntb_dev *ntb)
{
	if (!ntb->ops->peer_db_read)
		return 0;

	return ntb->ops->peer_db_read(ntb);
}

/**
 * ntb_peer_db_set() - set bits in the peer doorbell register
 * @ntb:	NTB device context.
 * @db_bits:	Doorbell bits to set.
 *
 * Set bits in the peer doorbell register, which may generate a peer doorbell
 * interrupt.  Bits that were already set must remain set.
 *
 * Return: Zero on success, otherwise an error number.
 */
static inline int ntb_peer_db_set(struct ntb_dev *ntb, u64 db_bits)
{
	return ntb->ops->peer_db_set(ntb, db_bits);
}

/**
 * ntb_peer_db_clear() - clear bits in the peer doorbell register
 * @ntb:	NTB device context.
 * @db_bits:	Doorbell bits to clear.
 *
 * Clear bits in the peer doorbell register, arming the bits for the next
 * doorbell.
 *
 * This is unusual, and hardware may not support it.
 *
 * Return: Zero on success, otherwise an error number.
 */
static inline int ntb_peer_db_clear(struct ntb_dev *ntb, u64 db_bits)
{
	if (!ntb->ops->db_clear)
		return -EINVAL;

	return ntb->ops->peer_db_clear(ntb, db_bits);
}

/**
 * ntb_peer_db_read_mask() - read the peer doorbell mask
 * @ntb:	NTB device context.
 *
 * Read the peer doorbell mask register, and return the bits that are set.
 *
 * This is unusual, and hardware may not support it.
 *
 * Return: The bits currently set in the peer doorbell mask register.
 */
static inline u64 ntb_peer_db_read_mask(struct ntb_dev *ntb)
{
	if (!ntb->ops->db_read_mask)
		return 0;

	return ntb->ops->peer_db_read_mask(ntb);
}

/**
 * ntb_peer_db_set_mask() - set bits in the peer doorbell mask
 * @ntb:	NTB device context.
 * @db_bits:	Doorbell mask bits to set.
 *
 * Set bits in the peer doorbell mask register, preventing doorbell interrupts
 * from being generated for those doorbell bits.  Bits that were already set
 * must remain set.
 *
 * This is unusual, and hardware may not support it.
 *
 * Return: Zero on success, otherwise an error number.
 */
static inline int ntb_peer_db_set_mask(struct ntb_dev *ntb, u64 db_bits)
{
	if (!ntb->ops->db_set_mask)
		return -EINVAL;

	return ntb->ops->peer_db_set_mask(ntb, db_bits);
}

/**
 * ntb_peer_db_clear_mask() - clear bits in the peer doorbell mask
 * @ntb:	NTB device context.
 * @db_bits:	Doorbell bits to clear.
 *
 * Clear bits in the peer doorbell mask register, allowing doorbell interrupts
 * from being generated for those doorbell bits.  If the hardware does not
 * generate the interrupt on clearing the mask bit, then the driver should not
 * implement this function!
 *
 * This is unusual, and hardware may not support it.
 *
 * Return: Zero on success, otherwise an error number.
 */
static inline int ntb_peer_db_clear_mask(struct ntb_dev *ntb, u64 db_bits)
{
	if (!ntb->ops->db_clear_mask)
		return -EINVAL;

	return ntb->ops->peer_db_clear_mask(ntb, db_bits);
}

/**
 * ntb_spad_is_unsafe() - check if it is safe to use the hardware scratchpads
 * @ntb:	NTB device context.
 *
 * It is possible for some ntb hardware to be affected by errata.  Hardware
 * drivers can advise clients to avoid using scratchpads.  Clients may ignore
 * this advice, though caution is recommended.
 *
 * Return: Zero if it is safe to use scratchpads, or One if it is not safe.
 */
static inline int ntb_spad_is_unsafe(struct ntb_dev *ntb)
{
	if (!ntb->ops->spad_is_unsafe)
		return 0;

	return ntb->ops->spad_is_unsafe(ntb);
}

/**
 * ntb_spad_count() - get the number of scratchpads
 * @ntb:	NTB device context.
 *
 * Hardware and topology may support a different number of scratchpads.
 * Although it must be the same for all ports per NTB device.
 *
 * Return: the number of scratchpads.
 */
static inline int ntb_spad_count(struct ntb_dev *ntb)
{
	if (!ntb->ops->spad_count)
		return 0;

	return ntb->ops->spad_count(ntb);
}

/**
 * ntb_spad_read() - read the local scratchpad register
 * @ntb:	NTB device context.
 * @sidx:	Scratchpad index.
 *
 * Read the local scratchpad register, and return the value.
 *
 * Return: The value of the local scratchpad register.
 */
static inline u32 ntb_spad_read(struct ntb_dev *ntb, int sidx)
{
	if (!ntb->ops->spad_read)
		return ~(u32)0;

	return ntb->ops->spad_read(ntb, sidx);
}

/**
 * ntb_spad_write() - write the local scratchpad register
 * @ntb:	NTB device context.
 * @sidx:	Scratchpad index.
 * @val:	Scratchpad value.
 *
 * Write the value to the local scratchpad register.
 *
 * Return: Zero on success, otherwise an error number.
 */
static inline int ntb_spad_write(struct ntb_dev *ntb, int sidx, u32 val)
{
	if (!ntb->ops->spad_write)
		return -EINVAL;

	return ntb->ops->spad_write(ntb, sidx, val);
}

/**
 * ntb_peer_spad_addr() - address of the peer scratchpad register
 * @ntb:	NTB device context.
 * @pidx:	Port index of peer device.
 * @sidx:	Scratchpad index.
 * @spad_addr:	OUT - The address of the peer scratchpad register.
 *
 * Return the address of the peer doorbell register.  This may be used, for
 * example, by drivers that offload memory copy operations to a dma engine.
 *
 * Return: Zero on success, otherwise an error number.
 */
static inline int ntb_peer_spad_addr(struct ntb_dev *ntb, int pidx, int sidx,
				     phys_addr_t *spad_addr)
{
	if (!ntb->ops->peer_spad_addr)
		return -EINVAL;

	return ntb->ops->peer_spad_addr(ntb, pidx, sidx, spad_addr);
}

/**
 * ntb_peer_spad_read() - read the peer scratchpad register
 * @ntb:	NTB device context.
 * @pidx:	Port index of peer device.
 * @sidx:	Scratchpad index.
 *
 * Read the peer scratchpad register, and return the value.
 *
 * Return: The value of the local scratchpad register.
 */
static inline u32 ntb_peer_spad_read(struct ntb_dev *ntb, int pidx, int sidx)
{
	if (!ntb->ops->peer_spad_read)
		return ~(u32)0;

	return ntb->ops->peer_spad_read(ntb, pidx, sidx);
}

/**
 * ntb_peer_spad_write() - write the peer scratchpad register
 * @ntb:	NTB device context.
 * @pidx:	Port index of peer device.
 * @sidx:	Scratchpad index.
 * @val:	Scratchpad value.
 *
 * Write the value to the peer scratchpad register.
 *
 * Return: Zero on success, otherwise an error number.
 */
static inline int ntb_peer_spad_write(struct ntb_dev *ntb, int pidx, int sidx,
				      u32 val)
{
	if (!ntb->ops->peer_spad_write)
		return -EINVAL;

	return ntb->ops->peer_spad_write(ntb, pidx, sidx, val);
}

/**
 * ntb_msg_count() - get the number of message registers
 * @ntb:	NTB device context.
 *
 * Hardware may support a different number of message registers.
 *
 * Return: the number of message registers.
 */
static inline int ntb_msg_count(struct ntb_dev *ntb)
{
	if (!ntb->ops->msg_count)
		return 0;

	return ntb->ops->msg_count(ntb);
}

/**
 * ntb_msg_inbits() - get a bitfield of inbound message registers status
 * @ntb:	NTB device context.
 *
 * The method returns the bitfield of status and mask registers, which related
 * to inbound message registers.
 *
 * Return: bitfield of inbound message registers.
 */
static inline u64 ntb_msg_inbits(struct ntb_dev *ntb)
{
	if (!ntb->ops->msg_inbits)
		return 0;

	return ntb->ops->msg_inbits(ntb);
}

/**
 * ntb_msg_outbits() - get a bitfield of outbound message registers status
 * @ntb:	NTB device context.
 *
 * The method returns the bitfield of status and mask registers, which related
 * to outbound message registers.
 *
 * Return: bitfield of outbound message registers.
 */
static inline u64 ntb_msg_outbits(struct ntb_dev *ntb)
{
	if (!ntb->ops->msg_outbits)
		return 0;

	return ntb->ops->msg_outbits(ntb);
}

/**
 * ntb_msg_read_sts() - read the message registers status
 * @ntb:	NTB device context.
 *
 * Read the status of message register. Inbound and outbound message registers
 * related bits can be filtered by masks retrieved from ntb_msg_inbits() and
 * ntb_msg_outbits().
 *
 * Return: status bits of message registers
 */
static inline u64 ntb_msg_read_sts(struct ntb_dev *ntb)
{
	if (!ntb->ops->msg_read_sts)
		return 0;

	return ntb->ops->msg_read_sts(ntb);
}

/**
 * ntb_msg_clear_sts() - clear status bits of message registers
 * @ntb:	NTB device context.
 * @sts_bits:	Status bits to clear.
 *
 * Clear bits in the status register.
 *
 * Return: Zero on success, otherwise a negative error number.
 */
static inline int ntb_msg_clear_sts(struct ntb_dev *ntb, u64 sts_bits)
{
	if (!ntb->ops->msg_clear_sts)
		return -EINVAL;

	return ntb->ops->msg_clear_sts(ntb, sts_bits);
}

/**
 * ntb_msg_set_mask() - set mask of message register status bits
 * @ntb:	NTB device context.
 * @mask_bits:	Mask bits.
 *
 * Mask the message registers status bits from raising the message event.
 *
 * Return: Zero on success, otherwise a negative error number.
 */
static inline int ntb_msg_set_mask(struct ntb_dev *ntb, u64 mask_bits)
{
	if (!ntb->ops->msg_set_mask)
		return -EINVAL;

	return ntb->ops->msg_set_mask(ntb, mask_bits);
}

/**
 * ntb_msg_clear_mask() - clear message registers mask
 * @ntb:	NTB device context.
 * @mask_bits:	Mask bits to clear.
 *
 * Clear bits in the message events mask register.
 *
 * Return: Zero on success, otherwise a negative error number.
 */
static inline int ntb_msg_clear_mask(struct ntb_dev *ntb, u64 mask_bits)
{
	if (!ntb->ops->msg_clear_mask)
		return -EINVAL;

	return ntb->ops->msg_clear_mask(ntb, mask_bits);
}

/**
 * ntb_msg_read() - read message register with specified index
 * @ntb:	NTB device context.
 * @midx:	Message register index
 * @pidx:	OUT - Port index of peer device a message retrieved from
 * @msg:	OUT - Data
 *
 * Read data from the specified message register. Source port index of a
 * message is retrieved as well.
 *
 * Return: Zero on success, otherwise a negative error number.
 */
static inline int ntb_msg_read(struct ntb_dev *ntb, int midx, int *pidx,
			       u32 *msg)
{
	if (!ntb->ops->msg_read)
		return -EINVAL;

	return ntb->ops->msg_read(ntb, midx, pidx, msg);
}

/**
 * ntb_msg_write() - write data to the specified message register
 * @ntb:	NTB device context.
 * @midx:	Message register index
 * @pidx:	Port index of peer device a message being sent to
 * @msg:	Data to send
 *
 * Send data to a specified peer device using the defined message register.
 * Message event can be raised if the midx registers isn't empty while
 * calling this method and the corresponding interrupt isn't masked.
 *
 * Return: Zero on success, otherwise a negative error number.
 */
static inline int ntb_msg_write(struct ntb_dev *ntb, int midx, int pidx,
				u32 msg)
{
	if (!ntb->ops->msg_write)
		return -EINVAL;

	return ntb->ops->msg_write(ntb, midx, pidx, msg);
}

#endif
