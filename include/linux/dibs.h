/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Direct Internal Buffer Sharing
 *
 *  Definitions for the DIBS module
 *
 *  Copyright IBM Corp. 2025
 */
#ifndef _DIBS_H
#define _DIBS_H

#include <linux/device.h>
#include <linux/uuid.h>

/* DIBS - Direct Internal Buffer Sharing - concept
 * -----------------------------------------------
 * In the case of multiple system sharing the same hardware, dibs fabrics can
 * provide dibs devices to these systems. The systems use dibs devices of the
 * same fabric to communicate via dmbs (Direct Memory Buffers). Each dmb has
 * exactly one owning local dibs device and one remote using dibs device, that
 * is authorized to write into this dmb. This access control is provided by the
 * dibs fabric.
 *
 * Because the access to the dmb is based on access to physical memory, it is
 * lossless and synchronous. The remote devices can directly access any offset
 * of the dmb.
 *
 * Dibs fabrics, dibs devices and dmbs are identified by tokens and ids.
 * Dibs fabric id is unique within the same hardware (with the exception of the
 * dibs loopback fabric), dmb token is unique within the same fabric, dibs
 * device gids are guaranteed to be unique within the same fabric and
 * statistically likely to be globally unique. The exchange of these tokens and
 * ids between the systems is not part of the dibs concept.
 *
 * The dibs layer provides an abstraction between dibs device drivers and dibs
 * clients.
 */

struct dibs_dev;

/* DIBS client
 * -----------
 */
#define MAX_DIBS_CLIENTS	8
/* All dibs clients have access to all dibs devices.
 * A dibs client provides the following functions to be called by dibs layer or
 * dibs device drivers:
 */
struct dibs_client_ops {
	/**
	 *  add_dev() - add a dibs device
	 *  @dev: device that was added
	 *
	 * Will be called during dibs_register_client() for all existing
	 * dibs devices and whenever a new dibs device is registered.
	 * dev is usable until dibs_client.remove() is called.
	 * *dev is protected by device refcounting.
	 */
	void (*add_dev)(struct dibs_dev *dev);
	/**
	 * del_dev() - remove a dibs device
	 * @dev: device to be removed
	 *
	 * Will be called whenever a dibs device is removed.
	 * Will be called during dibs_unregister_client() for all existing
	 * dibs devices and whenever a dibs device is unregistered.
	 * The device has already stopped initiative for this client:
	 * No new handlers will be started.
	 * The device is no longer usable by this client after this call.
	 */
	void (*del_dev)(struct dibs_dev *dev);
};

struct dibs_client {
	/* client name for logging and debugging purposes */
	const char *name;
	const struct dibs_client_ops *ops;
	/* client index - provided and used by dibs layer */
	u8 id;
};

/* Functions to be called by dibs clients:
 */
/**
 * dibs_register_client() - register a client with dibs layer
 * @client: this client
 *
 * Will call client->ops->add_dev() for all existing dibs devices.
 * Return: zero on success.
 */
int dibs_register_client(struct dibs_client *client);
/**
 * dibs_unregister_client() - unregister a client with dibs layer
 * @client: this client
 *
 * Will call client->ops->del_dev() for all existing dibs devices.
 * Return: zero on success.
 */
int dibs_unregister_client(struct dibs_client *client);

/* dibs clients can call dibs device ops. */

/* DIBS devices
 * ------------
 */

/* Defined fabric id / CHID for all loopback devices:
 * All dibs loopback devices report this fabric id. In this case devices with
 * the same fabric id can NOT communicate via dibs. Only loopback devices with
 * the same dibs device gid can communicate (=same device with itself).
 */
#define DIBS_LOOPBACK_FABRIC	0xFFFF

/* A dibs device provides the following functions to be called by dibs clients.
 * They are mandatory, unless marked 'optional'.
 */
struct dibs_dev_ops {
	/**
	 * get_fabric_id()
	 * @dev: local dibs device
	 *
	 * Only devices on the same dibs fabric can communicate. Fabric_id is
	 * unique inside the same HW system. Use fabric_id for fast negative
	 * checks, but only query_remote_gid() can give a reliable positive
	 * answer:
	 * Different fabric_id: dibs is not possible
	 * Same fabric_id: dibs may be possible or not
	 *		   (e.g. different HW systems)
	 * EXCEPTION: DIBS_LOOPBACK_FABRIC denotes an ism_loopback device
	 *	      that can only communicate with itself. Use dibs_dev.gid
	 *	      or query_remote_gid()to determine whether sender and
	 *	      receiver use the same ism_loopback device.
	 * Return: 2 byte dibs fabric id
	 */
	u16 (*get_fabric_id)(struct dibs_dev *dev);
};

struct dibs_dev {
	struct list_head list;
	struct device dev;
	/* To be filled by device driver, before calling dibs_dev_add(): */
	const struct dibs_dev_ops *ops;
	uuid_t gid;
	/* priv pointer for device driver */
	void *drv_priv;

	/* priv pointer per client; for client usage only */
	void *priv[MAX_DIBS_CLIENTS];
};

static inline void dibs_set_priv(struct dibs_dev *dev,
				 struct dibs_client *client, void *priv)
{
	dev->priv[client->id] = priv;
}

static inline void *dibs_get_priv(struct dibs_dev *dev,
				  struct dibs_client *client)
{
	return dev->priv[client->id];
}

/* ------- End of client-only functions ----------- */

/* Functions to be called by dibs device drivers:
 */
/**
 * dibs_dev_alloc() - allocate and reference device structure
 *
 * The following fields will be valid upon successful return: dev
 * NOTE: Use put_device(dibs_get_dev(@dibs)) to give up your reference instead
 * of freeing @dibs @dev directly once you have successfully called this
 * function.
 * Return: Pointer to dibs device structure
 */
struct dibs_dev *dibs_dev_alloc(void);
/**
 * dibs_dev_add() - register with dibs layer and all clients
 * @dibs: dibs device
 *
 * The following fields must be valid upon entry: dev, ops, drv_priv
 * All fields will be valid upon successful return.
 * Return: zero on success
 */
int dibs_dev_add(struct dibs_dev *dibs);
/**
 * dibs_dev_del() - unregister from dibs layer and all clients
 * @dibs: dibs device
 */
void dibs_dev_del(struct dibs_dev *dibs);

#endif	/* _DIBS_H */
