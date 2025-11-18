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

/* DMB - Direct Memory Buffer
 * --------------------------
 * A dibs client provides a dmb as input buffer for a local receiving
 * dibs device for exactly one (remote) sending dibs device. Only this
 * sending device can send data into this dmb using move_data(). Sender
 * and receiver can be the same device. A dmb belongs to exactly one client.
 */
struct dibs_dmb {
	/* tok - Token for this dmb
	 * Used by remote and local devices and clients to address this dmb.
	 * Provided by dibs fabric. Unique per dibs fabric.
	 */
	u64 dmb_tok;
	/* rgid - GID of designated remote sending device */
	uuid_t rgid;
	/* cpu_addr - buffer address */
	void *cpu_addr;
	/* len - buffer length */
	u32 dmb_len;
	/* idx - Index of this DMB on this receiving device */
	u32 idx;
	/* VLAN support (deprecated)
	 * In order to write into a vlan-tagged dmb, the remote device needs
	 * to belong to the this vlan
	 */
	u32 vlan_valid;
	u32 vlan_id;
	/* optional, used by device driver */
	dma_addr_t dma_addr;
};

/* DIBS events
 * -----------
 * Dibs devices can optionally notify dibs clients about events that happened
 * in the fabric or at the remote device or remote dmb.
 */
enum dibs_event_type {
	/* Buffer event, e.g. a remote dmb was unregistered */
	DIBS_BUF_EVENT,
	/* Device event, e.g. a remote dibs device was disabled */
	DIBS_DEV_EVENT,
	/* Software event, a dibs client can send an event signal to a
	 * remote dibs device.
	 */
	DIBS_SW_EVENT,
	DIBS_OTHER_TYPE };

enum dibs_event_subtype {
	DIBS_BUF_UNREGISTERED,
	DIBS_DEV_DISABLED,
	DIBS_DEV_ERR_STATE,
	DIBS_OTHER_SUBTYPE
};

struct dibs_event {
	u32 type;
	u32 subtype;
	/* uuid_null if invalid */
	uuid_t gid;
	/* zero if invalid */
	u64 buffer_tok;
	u64 time;
	/* additional data or zero */
	u64 data;
};

struct dibs_dev;

/* DIBS client
 * -----------
 */
#define MAX_DIBS_CLIENTS	8
#define NO_DIBS_CLIENT		0xff
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
	/**
	 * handle_irq() - Handle signaling for a DMB
	 * @dev: device that owns the dmb
	 * @idx: Index of the dmb that got signalled
	 * @dmbemask: signaling mask of the dmb
	 *
	 * Handle signaling for a dmb that was registered by this client
	 * for this device.
	 * The dibs device can coalesce multiple signaling triggers into a
	 * single call of handle_irq(). dmbemask can be used to indicate
	 * different kinds of triggers.
	 *
	 * Context: Called in IRQ context by dibs device driver
	 */
	void (*handle_irq)(struct dibs_dev *dev, unsigned int idx,
			   u16 dmbemask);
	/**
	 * handle_event() - Handle control information sent by device
	 * @dev: device reporting the event
	 * @event: ism event structure
	 *
	 * * Context: Called in IRQ context by dibs device driver
	 */
	void (*handle_event)(struct dibs_dev *dev,
			     const struct dibs_event *event);
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
	/**
	 * query_remote_gid()
	 * @dev: local dibs device
	 * @rgid: gid of remote dibs device
	 * @vid_valid: if zero, vid will be ignored;
	 *	       deprecated, ignored if device does not support vlan
	 * @vid: VLAN id; deprecated, ignored if device does not support vlan
	 *
	 * Query whether a remote dibs device is reachable via this local device
	 * and this vlan id.
	 * Return: 0 if remote gid is reachable.
	 */
	int (*query_remote_gid)(struct dibs_dev *dev, const uuid_t *rgid,
				u32 vid_valid, u32 vid);
	/**
	 * max_dmbs()
	 * Return: Max number of DMBs that can be registered for this kind of
	 *	   dibs_dev
	 */
	int (*max_dmbs)(void);
	/**
	 * register_dmb() - allocate and register a dmb
	 * @dev: dibs device
	 * @dmb: dmb struct to be registered
	 * @client: dibs client
	 * @vid: VLAN id; deprecated, ignored if device does not support vlan
	 *
	 * The following fields of dmb must provide valid input:
	 *	@rgid: gid of remote user device
	 *	@dmb_len: buffer length
	 *	@idx: Optionally:requested idx (if non-zero)
	 *	@vlan_valid: if zero, vlan_id will be ignored;
	 *		     deprecated, ignored if device does not support vlan
	 *	@vlan_id: deprecated, ignored if device does not support vlan
	 * Upon return in addition the following fields will be valid:
	 *	@dmb_tok: for usage by remote and local devices and clients
	 *	@cpu_addr: allocated buffer
	 *	@idx: dmb index, unique per dibs device
	 *	@dma_addr: to be used by device driver,if applicable
	 *
	 * Allocate a dmb buffer and register it with this device and for this
	 * client.
	 * Return: zero on success
	 */
	int (*register_dmb)(struct dibs_dev *dev, struct dibs_dmb *dmb,
			    struct dibs_client *client);
	/**
	 * unregister_dmb() - unregister and free a dmb
	 * @dev: dibs device
	 * @dmb: dmb struct to be unregistered
	 * The following fields of dmb must provide valid input:
	 *	@dmb_tok
	 *	@cpu_addr
	 *	@idx
	 *
	 * Free dmb.cpu_addr and unregister the dmb from this device.
	 * Return: zero on success
	 */
	int (*unregister_dmb)(struct dibs_dev *dev, struct dibs_dmb *dmb);
	/**
	 * move_data() - write into a remote dmb
	 * @dev: Local sending dibs device
	 * @dmb_tok: Token of the remote dmb
	 * @idx: signaling index in dmbemask
	 * @sf: signaling flag;
	 *      if true, idx will be turned on at target dmbemask mask
	 *      and target device will be signaled.
	 * @offset: offset within target dmb
	 * @data: pointer to data to be sent
	 * @size: length of data to be sent, can be zero.
	 *
	 * Use dev to write data of size at offset into a remote dmb
	 * identified by dmb_tok. Data is moved synchronously, *data can
	 * be freed when this function returns.
	 *
	 * If signaling flag (sf) is true, bit number idx bit will be turned
	 * on in the dmbemask mask when handle_irq() is called at the remote
	 * dibs client that owns the target dmb. The target device may chose
	 * to coalesce the signaling triggers of multiple move_data() calls
	 * to the same target dmb into a single handle_irq() call.
	 * Return: zero on success
	 */
	int (*move_data)(struct dibs_dev *dev, u64 dmb_tok, unsigned int idx,
			 bool sf, unsigned int offset, void *data,
			 unsigned int size);
	/**
	 * add_vlan_id() - add dibs device to vlan (optional, deprecated)
	 * @dev: dibs device
	 * @vlan_id: vlan id
	 *
	 * In order to write into a vlan-tagged dmb, the remote device needs
	 * to belong to the this vlan. A device can belong to more than 1 vlan.
	 * Any device can access an untagged dmb.
	 * Deprecated, only supported for backwards compatibility.
	 * Return: zero on success
	 */
	int (*add_vlan_id)(struct dibs_dev *dev, u64 vlan_id);
	/**
	 * del_vlan_id() - remove dibs device from vlan (optional, deprecated)
	 * @dev: dibs device
	 * @vlan_id: vlan id
	 * Return: zero on success
	 */
	int (*del_vlan_id)(struct dibs_dev *dev, u64 vlan_id);
	/**
	 * signal_event() - trigger an event at a remote dibs device (optional)
	 * @dev: local dibs device
	 * @rgid: gid of remote dibs device
	 * trigger_irq: zero: notification may be coalesced with other events
	 *		non-zero: notify immediately
	 * @subtype: 4 byte event code, meaning is defined by dibs client
	 * @data: 8 bytes of additional information,
	 *	  meaning is defined by dibs client
	 *
	 * dibs devices can offer support for sending a control event of type
	 * EVENT_SWR to a remote dibs device.
	 * NOTE: handle_event() will be called for all registered dibs clients
	 * at the remote device.
	 * Return: zero on success
	 */
	int (*signal_event)(struct dibs_dev *dev, const uuid_t *rgid,
			    u32 trigger_irq, u32 event_code, u64 info);
	/**
	 * support_mmapped_rdmb() - can this device provide memory mapped
	 *			    remote dmbs? (optional)
	 * @dev: dibs device
	 *
	 * A dibs device can provide a kernel address + length, that represent
	 * a remote target dmb (like MMIO). Alternatively to calling
	 * move_data(), a dibs client can write into such a ghost-send-buffer
	 * (= to this kernel address) and the data will automatically
	 * immediately appear in the target dmb, even without calling
	 * move_data().
	 *
	 * Either all 3 function pointers for support_dmb_nocopy(),
	 * attach_dmb() and detach_dmb() are defined, or all of them must
	 * be NULL.
	 *
	 * Return: non-zero, if memory mapped remote dmbs are supported.
	 */
	int (*support_mmapped_rdmb)(struct dibs_dev *dev);
	/**
	 * attach_dmb() - attach local memory to a remote dmb
	 * @dev: Local sending ism device
	 * @dmb: all other parameters are passed in the form of a
	 *	 dmb struct
	 *	 TODO: (THIS IS CONFUSING, should be changed)
	 *  dmb_tok: (in) Token of the remote dmb, we want to attach to
	 *  cpu_addr: (out) MMIO address
	 *  dma_addr: (out) MMIO address (if applicable, invalid otherwise)
	 *  dmb_len: (out) length of local MMIO region,
	 *           equal to length of remote DMB.
	 *  sba_idx: (out) index of remote dmb (NOT HELPFUL, should be removed)
	 *
	 * Provides a memory address to the sender that can be used to
	 * directly write into the remote dmb.
	 * Memory is available until detach_dmb is called
	 *
	 * Return: Zero upon success, Error code otherwise
	 */
	int (*attach_dmb)(struct dibs_dev *dev, struct dibs_dmb *dmb);
	/**
	 * detach_dmb() - Detach the ghost buffer from a remote dmb
	 * @dev: ism device
	 * @token: dmb token of the remote dmb
	 *
	 * No need to free cpu_addr.
	 *
	 * Return: Zero upon success, Error code otherwise
	 */
	int (*detach_dmb)(struct dibs_dev *dev, u64 token);
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

	/* get this lock before accessing any of the fields below */
	spinlock_t lock;
	/* array of client ids indexed by dmb idx;
	 * can be used as indices into priv and subs arrays
	 */
	u8 *dmb_clientid_arr;
	/* Sparse array of all ISM clients */
	struct dibs_client *subs[MAX_DIBS_CLIENTS];
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
