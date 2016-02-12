/*
 * HSI core header file.
 *
 * Copyright (C) 2010 Nokia Corporation. All rights reserved.
 *
 * Contact: Carlos Chinea <carlos.chinea@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#ifndef __LINUX_HSI_H__
#define __LINUX_HSI_H__

#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/scatterlist.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/notifier.h>

/* HSI message ttype */
#define HSI_MSG_READ	0
#define HSI_MSG_WRITE	1

/* HSI configuration values */
enum {
	HSI_MODE_STREAM	= 1,
	HSI_MODE_FRAME,
};

enum {
	HSI_FLOW_SYNC,	/* Synchronized flow */
	HSI_FLOW_PIPE,	/* Pipelined flow */
};

enum {
	HSI_ARB_RR,	/* Round-robin arbitration */
	HSI_ARB_PRIO,	/* Channel priority arbitration */
};

#define HSI_MAX_CHANNELS	16

/* HSI message status codes */
enum {
	HSI_STATUS_COMPLETED,	/* Message transfer is completed */
	HSI_STATUS_PENDING,	/* Message pending to be read/write (POLL) */
	HSI_STATUS_PROCEEDING,	/* Message transfer is ongoing */
	HSI_STATUS_QUEUED,	/* Message waiting to be served */
	HSI_STATUS_ERROR,	/* Error when message transfer was ongoing */
};

/* HSI port event codes */
enum {
	HSI_EVENT_START_RX,
	HSI_EVENT_STOP_RX,
};

/**
 * struct hsi_channel - channel resource used by the hsi clients
 * @id: Channel number
 * @name: Channel name
 */
struct hsi_channel {
	unsigned int	id;
	const char	*name;
};

/**
 * struct hsi_config - Configuration for RX/TX HSI modules
 * @mode: Bit transmission mode (STREAM or FRAME)
 * @channels: Channel resources used by the client
 * @num_channels: Number of channel resources
 * @num_hw_channels: Number of channels the transceiver is configured for [1..16]
 * @speed: Max bit transmission speed (Kbit/s)
 * @flow: RX flow type (SYNCHRONIZED or PIPELINE)
 * @arb_mode: Arbitration mode for TX frame (Round robin, priority)
 */
struct hsi_config {
	unsigned int		mode;
	struct hsi_channel	*channels;
	unsigned int		num_channels;
	unsigned int		num_hw_channels;
	unsigned int		speed;
	union {
		unsigned int	flow;		/* RX only */
		unsigned int	arb_mode;	/* TX only */
	};
};

/**
 * struct hsi_board_info - HSI client board info
 * @name: Name for the HSI device
 * @hsi_id: HSI controller id where the client sits
 * @port: Port number in the controller where the client sits
 * @tx_cfg: HSI TX configuration
 * @rx_cfg: HSI RX configuration
 * @platform_data: Platform related data
 * @archdata: Architecture-dependent device data
 */
struct hsi_board_info {
	const char		*name;
	unsigned int		hsi_id;
	unsigned int		port;
	struct hsi_config	tx_cfg;
	struct hsi_config	rx_cfg;
	void			*platform_data;
	struct dev_archdata	*archdata;
};

#ifdef CONFIG_HSI_BOARDINFO
extern int hsi_register_board_info(struct hsi_board_info const *info,
							unsigned int len);
#else
static inline int hsi_register_board_info(struct hsi_board_info const *info,
							unsigned int len)
{
	return 0;
}
#endif /* CONFIG_HSI_BOARDINFO */

/**
 * struct hsi_client - HSI client attached to an HSI port
 * @device: Driver model representation of the device
 * @tx_cfg: HSI TX configuration
 * @rx_cfg: HSI RX configuration
 */
struct hsi_client {
	struct device		device;
	struct hsi_config	tx_cfg;
	struct hsi_config	rx_cfg;
	/* private: */
	void			(*ehandler)(struct hsi_client *, unsigned long);
	unsigned int		pclaimed:1;
	struct notifier_block	nb;
};

#define to_hsi_client(dev) container_of(dev, struct hsi_client, device)

static inline void hsi_client_set_drvdata(struct hsi_client *cl, void *data)
{
	dev_set_drvdata(&cl->device, data);
}

static inline void *hsi_client_drvdata(struct hsi_client *cl)
{
	return dev_get_drvdata(&cl->device);
}

int hsi_register_port_event(struct hsi_client *cl,
			void (*handler)(struct hsi_client *, unsigned long));
int hsi_unregister_port_event(struct hsi_client *cl);

/**
 * struct hsi_client_driver - Driver associated to an HSI client
 * @driver: Driver model representation of the driver
 */
struct hsi_client_driver {
	struct device_driver	driver;
};

#define to_hsi_client_driver(drv) container_of(drv, struct hsi_client_driver,\
									driver)

int hsi_register_client_driver(struct hsi_client_driver *drv);

static inline void hsi_unregister_client_driver(struct hsi_client_driver *drv)
{
	driver_unregister(&drv->driver);
}

/**
 * struct hsi_msg - HSI message descriptor
 * @link: Free to use by the current descriptor owner
 * @cl: HSI device client that issues the transfer
 * @sgt: Head of the scatterlist array
 * @context: Client context data associated to the transfer
 * @complete: Transfer completion callback
 * @destructor: Destructor to free resources when flushing
 * @status: Status of the transfer when completed
 * @actual_len: Actual length of data transferred on completion
 * @channel: Channel were to TX/RX the message
 * @ttype: Transfer type (TX if set, RX otherwise)
 * @break_frame: if true HSI will send/receive a break frame. Data buffers are
 *		ignored in the request.
 */
struct hsi_msg {
	struct list_head	link;
	struct hsi_client	*cl;
	struct sg_table		sgt;
	void			*context;

	void			(*complete)(struct hsi_msg *msg);
	void			(*destructor)(struct hsi_msg *msg);

	int			status;
	unsigned int		actual_len;
	unsigned int		channel;
	unsigned int		ttype:1;
	unsigned int		break_frame:1;
};

struct hsi_msg *hsi_alloc_msg(unsigned int n_frag, gfp_t flags);
void hsi_free_msg(struct hsi_msg *msg);

/**
 * struct hsi_port - HSI port device
 * @device: Driver model representation of the device
 * @tx_cfg: Current TX path configuration
 * @rx_cfg: Current RX path configuration
 * @num: Port number
 * @shared: Set when port can be shared by different clients
 * @claimed: Reference count of clients which claimed the port
 * @lock: Serialize port claim
 * @async: Asynchronous transfer callback
 * @setup: Callback to set the HSI client configuration
 * @flush: Callback to clean the HW state and destroy all pending transfers
 * @start_tx: Callback to inform that a client wants to TX data
 * @stop_tx: Callback to inform that a client no longer wishes to TX data
 * @release: Callback to inform that a client no longer uses the port
 * @n_head: Notifier chain for signaling port events to the clients.
 */
struct hsi_port {
	struct device			device;
	struct hsi_config		tx_cfg;
	struct hsi_config		rx_cfg;
	unsigned int			num;
	unsigned int			shared:1;
	int				claimed;
	struct mutex			lock;
	int				(*async)(struct hsi_msg *msg);
	int				(*setup)(struct hsi_client *cl);
	int				(*flush)(struct hsi_client *cl);
	int				(*start_tx)(struct hsi_client *cl);
	int				(*stop_tx)(struct hsi_client *cl);
	int				(*release)(struct hsi_client *cl);
	/* private */
	struct atomic_notifier_head	n_head;
};

#define to_hsi_port(dev) container_of(dev, struct hsi_port, device)
#define hsi_get_port(cl) to_hsi_port((cl)->device.parent)

int hsi_event(struct hsi_port *port, unsigned long event);
int hsi_claim_port(struct hsi_client *cl, unsigned int share);
void hsi_release_port(struct hsi_client *cl);

static inline int hsi_port_claimed(struct hsi_client *cl)
{
	return cl->pclaimed;
}

static inline void hsi_port_set_drvdata(struct hsi_port *port, void *data)
{
	dev_set_drvdata(&port->device, data);
}

static inline void *hsi_port_drvdata(struct hsi_port *port)
{
	return dev_get_drvdata(&port->device);
}

/**
 * struct hsi_controller - HSI controller device
 * @device: Driver model representation of the device
 * @owner: Pointer to the module owning the controller
 * @id: HSI controller ID
 * @num_ports: Number of ports in the HSI controller
 * @port: Array of HSI ports
 */
struct hsi_controller {
	struct device		device;
	struct module		*owner;
	unsigned int		id;
	unsigned int		num_ports;
	struct hsi_port		**port;
};

#define to_hsi_controller(dev) container_of(dev, struct hsi_controller, device)

struct hsi_controller *hsi_alloc_controller(unsigned int n_ports, gfp_t flags);
void hsi_put_controller(struct hsi_controller *hsi);
int hsi_register_controller(struct hsi_controller *hsi);
void hsi_unregister_controller(struct hsi_controller *hsi);
struct hsi_client *hsi_new_client(struct hsi_port *port,
						struct hsi_board_info *info);
int hsi_remove_client(struct device *dev, void *data);
void hsi_port_unregister_clients(struct hsi_port *port);

#ifdef CONFIG_OF
void hsi_add_clients_from_dt(struct hsi_port *port,
			     struct device_node *clients);
#else
static inline void hsi_add_clients_from_dt(struct hsi_port *port,
					   struct device_node *clients)
{
	return;
}
#endif

static inline void hsi_controller_set_drvdata(struct hsi_controller *hsi,
								void *data)
{
	dev_set_drvdata(&hsi->device, data);
}

static inline void *hsi_controller_drvdata(struct hsi_controller *hsi)
{
	return dev_get_drvdata(&hsi->device);
}

static inline struct hsi_port *hsi_find_port_num(struct hsi_controller *hsi,
							unsigned int num)
{
	return (num < hsi->num_ports) ? hsi->port[num] : NULL;
}

/*
 * API for HSI clients
 */
int hsi_async(struct hsi_client *cl, struct hsi_msg *msg);

int hsi_get_channel_id_by_name(struct hsi_client *cl, char *name);

/**
 * hsi_id - Get HSI controller ID associated to a client
 * @cl: Pointer to a HSI client
 *
 * Return the controller id where the client is attached to
 */
static inline unsigned int hsi_id(struct hsi_client *cl)
{
	return	to_hsi_controller(cl->device.parent->parent)->id;
}

/**
 * hsi_port_id - Gets the port number a client is attached to
 * @cl: Pointer to HSI client
 *
 * Return the port number associated to the client
 */
static inline unsigned int hsi_port_id(struct hsi_client *cl)
{
	return	to_hsi_port(cl->device.parent)->num;
}

/**
 * hsi_setup - Configure the client's port
 * @cl: Pointer to the HSI client
 *
 * When sharing ports, clients should either relay on a single
 * client setup or have the same setup for all of them.
 *
 * Return -errno on failure, 0 on success
 */
static inline int hsi_setup(struct hsi_client *cl)
{
	if (!hsi_port_claimed(cl))
		return -EACCES;
	return	hsi_get_port(cl)->setup(cl);
}

/**
 * hsi_flush - Flush all pending transactions on the client's port
 * @cl: Pointer to the HSI client
 *
 * This function will destroy all pending hsi_msg in the port and reset
 * the HW port so it is ready to receive and transmit from a clean state.
 *
 * Return -errno on failure, 0 on success
 */
static inline int hsi_flush(struct hsi_client *cl)
{
	if (!hsi_port_claimed(cl))
		return -EACCES;
	return hsi_get_port(cl)->flush(cl);
}

/**
 * hsi_async_read - Submit a read transfer
 * @cl: Pointer to the HSI client
 * @msg: HSI message descriptor of the transfer
 *
 * Return -errno on failure, 0 on success
 */
static inline int hsi_async_read(struct hsi_client *cl, struct hsi_msg *msg)
{
	msg->ttype = HSI_MSG_READ;
	return hsi_async(cl, msg);
}

/**
 * hsi_async_write - Submit a write transfer
 * @cl: Pointer to the HSI client
 * @msg: HSI message descriptor of the transfer
 *
 * Return -errno on failure, 0 on success
 */
static inline int hsi_async_write(struct hsi_client *cl, struct hsi_msg *msg)
{
	msg->ttype = HSI_MSG_WRITE;
	return hsi_async(cl, msg);
}

/**
 * hsi_start_tx - Signal the port that the client wants to start a TX
 * @cl: Pointer to the HSI client
 *
 * Return -errno on failure, 0 on success
 */
static inline int hsi_start_tx(struct hsi_client *cl)
{
	if (!hsi_port_claimed(cl))
		return -EACCES;
	return hsi_get_port(cl)->start_tx(cl);
}

/**
 * hsi_stop_tx - Signal the port that the client no longer wants to transmit
 * @cl: Pointer to the HSI client
 *
 * Return -errno on failure, 0 on success
 */
static inline int hsi_stop_tx(struct hsi_client *cl)
{
	if (!hsi_port_claimed(cl))
		return -EACCES;
	return hsi_get_port(cl)->stop_tx(cl);
}
#endif /* __LINUX_HSI_H__ */
