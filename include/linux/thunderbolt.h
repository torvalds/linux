/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Thunderbolt service API
 *
 * Copyright (C) 2014 Andreas Noever <andreas.noever@gmail.com>
 * Copyright (C) 2017, Intel Corporation
 * Authors: Michael Jamet <michael.jamet@intel.com>
 *          Mika Westerberg <mika.westerberg@linux.intel.com>
 */

#ifndef THUNDERBOLT_H_
#define THUNDERBOLT_H_

#include <linux/types.h>

struct fwnode_handle;
struct device;

#if IS_REACHABLE(CONFIG_USB4)

#include <linux/device.h>
#include <linux/idr.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/mod_devicetable.h>
#include <linux/pci.h>
#include <linux/uuid.h>
#include <linux/workqueue.h>

enum tb_cfg_pkg_type {
	TB_CFG_PKG_READ = 1,
	TB_CFG_PKG_WRITE = 2,
	TB_CFG_PKG_ERROR = 3,
	TB_CFG_PKG_NOTIFY_ACK = 4,
	TB_CFG_PKG_EVENT = 5,
	TB_CFG_PKG_XDOMAIN_REQ = 6,
	TB_CFG_PKG_XDOMAIN_RESP = 7,
	TB_CFG_PKG_OVERRIDE = 8,
	TB_CFG_PKG_RESET = 9,
	TB_CFG_PKG_ICM_EVENT = 10,
	TB_CFG_PKG_ICM_CMD = 11,
	TB_CFG_PKG_ICM_RESP = 12,
};

/**
 * enum tb_security_level - Thunderbolt security level
 * @TB_SECURITY_NONE: No security, legacy mode
 * @TB_SECURITY_USER: User approval required at minimum
 * @TB_SECURITY_SECURE: One time saved key required at minimum
 * @TB_SECURITY_DPONLY: Only tunnel Display port (and USB)
 * @TB_SECURITY_USBONLY: Only tunnel USB controller of the connected
 *			 Thunderbolt dock (and Display Port). All PCIe
 *			 links downstream of the dock are removed.
 * @TB_SECURITY_NOPCIE: For USB4 systems this level is used when the
 *			PCIe tunneling is disabled from the BIOS.
 */
enum tb_security_level {
	TB_SECURITY_NONE,
	TB_SECURITY_USER,
	TB_SECURITY_SECURE,
	TB_SECURITY_DPONLY,
	TB_SECURITY_USBONLY,
	TB_SECURITY_NOPCIE,
};

/**
 * struct tb - main thunderbolt bus structure
 * @dev: Domain device
 * @lock: Big lock. Must be held when accessing any struct
 *	  tb_switch / struct tb_port.
 * @nhi: Pointer to the NHI structure
 * @ctl: Control channel for this domain
 * @wq: Ordered workqueue for all domain specific work
 * @root_switch: Root switch of this domain
 * @cm_ops: Connection manager specific operations vector
 * @index: Linux assigned domain number
 * @security_level: Current security level
 * @nboot_acl: Number of boot ACLs the domain supports
 * @privdata: Private connection manager specific data
 */
struct tb {
	struct device dev;
	struct mutex lock;
	struct tb_nhi *nhi;
	struct tb_ctl *ctl;
	struct workqueue_struct *wq;
	struct tb_switch *root_switch;
	const struct tb_cm_ops *cm_ops;
	int index;
	enum tb_security_level security_level;
	size_t nboot_acl;
	unsigned long privdata[];
};

extern const struct bus_type tb_bus_type;
extern const struct device_type tb_service_type;
extern const struct device_type tb_xdomain_type;

#define TB_LINKS_PER_PHY_PORT	2

static inline unsigned int tb_phy_port_from_link(unsigned int link)
{
	return (link - 1) / TB_LINKS_PER_PHY_PORT;
}

/**
 * struct tb_property_dir - XDomain property directory
 * @uuid: Directory UUID or %NULL if root directory
 * @properties: List of properties in this directory
 *
 * User needs to provide serialization if needed.
 */
struct tb_property_dir {
	const uuid_t *uuid;
	struct list_head properties;
};

enum tb_property_type {
	TB_PROPERTY_TYPE_UNKNOWN = 0x00,
	TB_PROPERTY_TYPE_DIRECTORY = 0x44,
	TB_PROPERTY_TYPE_DATA = 0x64,
	TB_PROPERTY_TYPE_TEXT = 0x74,
	TB_PROPERTY_TYPE_VALUE = 0x76,
};

#define TB_PROPERTY_KEY_SIZE	8

/**
 * struct tb_property - XDomain property
 * @list: Used to link properties together in a directory
 * @key: Key for the property (always terminated).
 * @type: Type of the property
 * @length: Length of the property data in dwords
 * @value: Property value
 *
 * Users use @type to determine which field in @value is filled.
 */
struct tb_property {
	struct list_head list;
	char key[TB_PROPERTY_KEY_SIZE + 1];
	enum tb_property_type type;
	size_t length;
	union {
		struct tb_property_dir *dir;
		u8 *data;
		char *text;
		u32 immediate;
	} value;
};

struct tb_property_dir *tb_property_parse_dir(const u32 *block,
					      size_t block_len);
ssize_t tb_property_format_dir(const struct tb_property_dir *dir, u32 *block,
			       size_t block_len);
struct tb_property_dir *tb_property_copy_dir(const struct tb_property_dir *dir);
struct tb_property_dir *tb_property_create_dir(const uuid_t *uuid);
void tb_property_free_dir(struct tb_property_dir *dir);
int tb_property_add_immediate(struct tb_property_dir *parent, const char *key,
			      u32 value);
int tb_property_add_data(struct tb_property_dir *parent, const char *key,
			 const void *buf, size_t buflen);
int tb_property_add_text(struct tb_property_dir *parent, const char *key,
			 const char *text);
int tb_property_add_dir(struct tb_property_dir *parent, const char *key,
			struct tb_property_dir *dir);
void tb_property_remove(struct tb_property *tb_property);
struct tb_property *tb_property_find(struct tb_property_dir *dir,
			const char *key, enum tb_property_type type);
struct tb_property *tb_property_get_next(struct tb_property_dir *dir,
					 struct tb_property *prev);

#define tb_property_for_each(dir, property)			\
	for (property = tb_property_get_next(dir, NULL);	\
	     property;						\
	     property = tb_property_get_next(dir, property))

int tb_register_property_dir(const char *key, struct tb_property_dir *dir);
void tb_unregister_property_dir(const char *key, struct tb_property_dir *dir);

/**
 * enum tb_link_width - Thunderbolt/USB4 link width
 * @TB_LINK_WIDTH_SINGLE: Single lane link
 * @TB_LINK_WIDTH_DUAL: Dual lane symmetric link
 * @TB_LINK_WIDTH_ASYM_TX: Dual lane asymmetric Gen 4 link with 3 transmitters
 * @TB_LINK_WIDTH_ASYM_RX: Dual lane asymmetric Gen 4 link with 3 receivers
 */
enum tb_link_width {
	TB_LINK_WIDTH_SINGLE = BIT(0),
	TB_LINK_WIDTH_DUAL = BIT(1),
	TB_LINK_WIDTH_ASYM_TX = BIT(2),
	TB_LINK_WIDTH_ASYM_RX = BIT(3),
};

/**
 * struct tb_xdomain - Cross-domain (XDomain) connection
 * @dev: XDomain device
 * @tb: Pointer to the domain
 * @remote_uuid: UUID of the remote domain (host)
 * @local_uuid: Cached local UUID
 * @route: Route string the other domain can be reached
 * @vendor: Vendor ID of the remote domain
 * @device: Device ID of the demote domain
 * @local_max_hopid: Maximum input HopID of this host
 * @remote_max_hopid: Maximum input HopID of the remote host
 * @lock: Lock to serialize access to the following fields of this structure
 * @vendor_name: Name of the vendor (or %NULL if not known)
 * @device_name: Name of the device (or %NULL if not known)
 * @link_speed: Speed of the link in Gb/s
 * @link_width: Width of the downstream facing link
 * @link_usb4: Downstream link is USB4
 * @is_unplugged: The XDomain is unplugged
 * @needs_uuid: If the XDomain does not have @remote_uuid it will be
 *		queried first
 * @service_ids: Used to generate IDs for the services
 * @in_hopids: Input HopIDs for DMA tunneling
 * @out_hopids: Output HopIDs for DMA tunneling
 * @local_property_block: Local block of properties
 * @local_property_block_gen: Generation of @local_property_block
 * @local_property_block_len: Length of the @local_property_block in dwords
 * @remote_properties: Properties exported by the remote domain
 * @remote_property_block_gen: Generation of @remote_properties
 * @state: Next XDomain discovery state to run
 * @state_work: Work used to run the next state
 * @state_retries: Number of retries remain for the state
 * @properties_changed_work: Work used to notify the remote domain that
 *			     our properties have changed
 * @properties_changed_retries: Number of times left to send properties
 *				changed notification
 * @bonding_possible: True if lane bonding is possible on local side
 * @target_link_width: Target link width from the remote host
 * @link: Root switch link the remote domain is connected (ICM only)
 * @depth: Depth in the chain the remote domain is connected (ICM only)
 *
 * This structure represents connection across two domains (hosts).
 * Each XDomain contains zero or more services which are exposed as
 * &struct tb_service objects.
 *
 * Service drivers may access this structure if they need to enumerate
 * non-standard properties but they need hold @lock when doing so
 * because properties can be changed asynchronously in response to
 * changes in the remote domain.
 */
struct tb_xdomain {
	struct device dev;
	struct tb *tb;
	uuid_t *remote_uuid;
	const uuid_t *local_uuid;
	u64 route;
	u16 vendor;
	u16 device;
	unsigned int local_max_hopid;
	unsigned int remote_max_hopid;
	struct mutex lock;
	const char *vendor_name;
	const char *device_name;
	unsigned int link_speed;
	enum tb_link_width link_width;
	bool link_usb4;
	bool is_unplugged;
	bool needs_uuid;
	struct ida service_ids;
	struct ida in_hopids;
	struct ida out_hopids;
	u32 *local_property_block;
	u32 local_property_block_gen;
	u32 local_property_block_len;
	struct tb_property_dir *remote_properties;
	u32 remote_property_block_gen;
	int state;
	struct delayed_work state_work;
	int state_retries;
	struct delayed_work properties_changed_work;
	int properties_changed_retries;
	bool bonding_possible;
	u8 target_link_width;
	u8 link;
	u8 depth;
};

int tb_xdomain_lane_bonding_enable(struct tb_xdomain *xd);
void tb_xdomain_lane_bonding_disable(struct tb_xdomain *xd);
int tb_xdomain_alloc_in_hopid(struct tb_xdomain *xd, int hopid);
void tb_xdomain_release_in_hopid(struct tb_xdomain *xd, int hopid);
int tb_xdomain_alloc_out_hopid(struct tb_xdomain *xd, int hopid);
void tb_xdomain_release_out_hopid(struct tb_xdomain *xd, int hopid);
int tb_xdomain_enable_paths(struct tb_xdomain *xd, int transmit_path,
			    int transmit_ring, int receive_path,
			    int receive_ring);
int tb_xdomain_disable_paths(struct tb_xdomain *xd, int transmit_path,
			     int transmit_ring, int receive_path,
			     int receive_ring);

static inline int tb_xdomain_disable_all_paths(struct tb_xdomain *xd)
{
	return tb_xdomain_disable_paths(xd, -1, -1, -1, -1);
}

struct tb_xdomain *tb_xdomain_find_by_uuid(struct tb *tb, const uuid_t *uuid);
struct tb_xdomain *tb_xdomain_find_by_route(struct tb *tb, u64 route);

static inline struct tb_xdomain *
tb_xdomain_find_by_uuid_locked(struct tb *tb, const uuid_t *uuid)
{
	struct tb_xdomain *xd;

	mutex_lock(&tb->lock);
	xd = tb_xdomain_find_by_uuid(tb, uuid);
	mutex_unlock(&tb->lock);

	return xd;
}

static inline struct tb_xdomain *
tb_xdomain_find_by_route_locked(struct tb *tb, u64 route)
{
	struct tb_xdomain *xd;

	mutex_lock(&tb->lock);
	xd = tb_xdomain_find_by_route(tb, route);
	mutex_unlock(&tb->lock);

	return xd;
}

static inline struct tb_xdomain *tb_xdomain_get(struct tb_xdomain *xd)
{
	if (xd)
		get_device(&xd->dev);
	return xd;
}

static inline void tb_xdomain_put(struct tb_xdomain *xd)
{
	if (xd)
		put_device(&xd->dev);
}

static inline bool tb_is_xdomain(const struct device *dev)
{
	return dev->type == &tb_xdomain_type;
}

static inline struct tb_xdomain *tb_to_xdomain(struct device *dev)
{
	if (tb_is_xdomain(dev))
		return container_of(dev, struct tb_xdomain, dev);
	return NULL;
}

int tb_xdomain_response(struct tb_xdomain *xd, const void *response,
			size_t size, enum tb_cfg_pkg_type type);
int tb_xdomain_request(struct tb_xdomain *xd, const void *request,
		       size_t request_size, enum tb_cfg_pkg_type request_type,
		       void *response, size_t response_size,
		       enum tb_cfg_pkg_type response_type,
		       unsigned int timeout_msec);

/**
 * struct tb_protocol_handler - Protocol specific handler
 * @uuid: XDomain messages with this UUID are dispatched to this handler
 * @callback: Callback called with the XDomain message. Returning %1
 *	      here tells the XDomain core that the message was handled
 *	      by this handler and should not be forwared to other
 *	      handlers.
 * @data: Data passed with the callback
 * @list: Handlers are linked using this
 *
 * Thunderbolt services can hook into incoming XDomain requests by
 * registering protocol handler. Only limitation is that the XDomain
 * discovery protocol UUID cannot be registered since it is handled by
 * the core XDomain code.
 *
 * The @callback must check that the message is really directed to the
 * service the driver implements.
 */
struct tb_protocol_handler {
	const uuid_t *uuid;
	int (*callback)(const void *buf, size_t size, void *data);
	void *data;
	struct list_head list;
};

int tb_register_protocol_handler(struct tb_protocol_handler *handler);
void tb_unregister_protocol_handler(struct tb_protocol_handler *handler);

/**
 * struct tb_service - Thunderbolt service
 * @dev: XDomain device
 * @id: ID of the service (shown in sysfs)
 * @key: Protocol key from the properties directory
 * @prtcid: Protocol ID from the properties directory
 * @prtcvers: Protocol version from the properties directory
 * @prtcrevs: Protocol software revision from the properties directory
 * @prtcstns: Protocol settings mask from the properties directory
 * @debugfs_dir: Pointer to the service debugfs directory. Always created
 *		 when debugfs is enabled. Can be used by service drivers to
 *		 add their own entries under the service.
 *
 * Each domain exposes set of services it supports as collection of
 * properties. For each service there will be one corresponding
 * &struct tb_service. Service drivers are bound to these.
 */
struct tb_service {
	struct device dev;
	int id;
	const char *key;
	u32 prtcid;
	u32 prtcvers;
	u32 prtcrevs;
	u32 prtcstns;
	struct dentry *debugfs_dir;
};

static inline struct tb_service *tb_service_get(struct tb_service *svc)
{
	if (svc)
		get_device(&svc->dev);
	return svc;
}

static inline void tb_service_put(struct tb_service *svc)
{
	if (svc)
		put_device(&svc->dev);
}

static inline bool tb_is_service(const struct device *dev)
{
	return dev->type == &tb_service_type;
}

static inline struct tb_service *tb_to_service(struct device *dev)
{
	if (tb_is_service(dev))
		return container_of(dev, struct tb_service, dev);
	return NULL;
}

/**
 * struct tb_service_driver - Thunderbolt service driver
 * @driver: Driver structure
 * @probe: Called when the driver is probed
 * @remove: Called when the driver is removed (optional)
 * @shutdown: Called at shutdown time to stop the service (optional)
 * @id_table: Table of service identifiers the driver supports
 */
struct tb_service_driver {
	struct device_driver driver;
	int (*probe)(struct tb_service *svc, const struct tb_service_id *id);
	void (*remove)(struct tb_service *svc);
	void (*shutdown)(struct tb_service *svc);
	const struct tb_service_id *id_table;
};

#define TB_SERVICE(key, id)				\
	.match_flags = TBSVC_MATCH_PROTOCOL_KEY |	\
		       TBSVC_MATCH_PROTOCOL_ID,		\
	.protocol_key = (key),				\
	.protocol_id = (id)

int tb_register_service_driver(struct tb_service_driver *drv);
void tb_unregister_service_driver(struct tb_service_driver *drv);

static inline void *tb_service_get_drvdata(const struct tb_service *svc)
{
	return dev_get_drvdata(&svc->dev);
}

static inline void tb_service_set_drvdata(struct tb_service *svc, void *data)
{
	dev_set_drvdata(&svc->dev, data);
}

static inline struct tb_xdomain *tb_service_parent(struct tb_service *svc)
{
	return tb_to_xdomain(svc->dev.parent);
}

/**
 * struct tb_nhi - thunderbolt native host interface
 * @lock: Must be held during ring creation/destruction. Is acquired by
 *	  interrupt_work when dispatching interrupts to individual rings.
 * @pdev: Pointer to the PCI device
 * @ops: NHI specific optional ops
 * @iobase: MMIO space of the NHI
 * @tx_rings: All Tx rings available on this host controller
 * @rx_rings: All Rx rings available on this host controller
 * @msix_ida: Used to allocate MSI-X vectors for rings
 * @going_away: The host controller device is about to disappear so when
 *		this flag is set, avoid touching the hardware anymore.
 * @iommu_dma_protection: An IOMMU will isolate external-facing ports.
 * @interrupt_work: Work scheduled to handle ring interrupt when no
 *		    MSI-X is used.
 * @hop_count: Number of rings (end point hops) supported by NHI.
 * @quirks: NHI specific quirks if any
 */
struct tb_nhi {
	spinlock_t lock;
	struct pci_dev *pdev;
	const struct tb_nhi_ops *ops;
	void __iomem *iobase;
	struct tb_ring **tx_rings;
	struct tb_ring **rx_rings;
	struct ida msix_ida;
	bool going_away;
	bool iommu_dma_protection;
	struct work_struct interrupt_work;
	u32 hop_count;
	unsigned long quirks;
};

/**
 * struct tb_ring - thunderbolt TX or RX ring associated with a NHI
 * @lock: Lock serializing actions to this ring. Must be acquired after
 *	  nhi->lock.
 * @nhi: Pointer to the native host controller interface
 * @size: Size of the ring
 * @hop: Hop (DMA channel) associated with this ring
 * @head: Head of the ring (write next descriptor here)
 * @tail: Tail of the ring (complete next descriptor here)
 * @descriptors: Allocated descriptors for this ring
 * @descriptors_dma: DMA address of descriptors for this ring
 * @queue: Queue holding frames to be transferred over this ring
 * @in_flight: Queue holding frames that are currently in flight
 * @work: Interrupt work structure
 * @is_tx: Is the ring Tx or Rx
 * @running: Is the ring running
 * @irq: MSI-X irq number if the ring uses MSI-X. %0 otherwise.
 * @vector: MSI-X vector number the ring uses (only set if @irq is > 0)
 * @flags: Ring specific flags
 * @e2e_tx_hop: Transmit HopID when E2E is enabled. Only applicable to
 *		RX ring. For TX ring this should be set to %0.
 * @sof_mask: Bit mask used to detect start of frame PDF
 * @eof_mask: Bit mask used to detect end of frame PDF
 * @start_poll: Called when ring interrupt is triggered to start
 *		polling. Passing %NULL keeps the ring in interrupt mode.
 * @poll_data: Data passed to @start_poll
 */
struct tb_ring {
	spinlock_t lock;
	struct tb_nhi *nhi;
	int size;
	int hop;
	int head;
	int tail;
	struct ring_desc *descriptors;
	dma_addr_t descriptors_dma;
	struct list_head queue;
	struct list_head in_flight;
	struct work_struct work;
	bool is_tx:1;
	bool running:1;
	int irq;
	u8 vector;
	unsigned int flags;
	int e2e_tx_hop;
	u16 sof_mask;
	u16 eof_mask;
	void (*start_poll)(void *data);
	void *poll_data;
};

/* Leave ring interrupt enabled on suspend */
#define RING_FLAG_NO_SUSPEND	BIT(0)
/* Configure the ring to be in frame mode */
#define RING_FLAG_FRAME		BIT(1)
/* Enable end-to-end flow control */
#define RING_FLAG_E2E		BIT(2)

struct ring_frame;
typedef void (*ring_cb)(struct tb_ring *, struct ring_frame *, bool canceled);

/**
 * enum ring_desc_flags - Flags for DMA ring descriptor
 * @RING_DESC_ISOCH: Enable isonchronous DMA (Tx only)
 * @RING_DESC_CRC_ERROR: In frame mode CRC check failed for the frame (Rx only)
 * @RING_DESC_COMPLETED: Descriptor completed (set by NHI)
 * @RING_DESC_POSTED: Always set this
 * @RING_DESC_BUFFER_OVERRUN: RX buffer overrun
 * @RING_DESC_INTERRUPT: Request an interrupt on completion
 */
enum ring_desc_flags {
	RING_DESC_ISOCH = 0x1,
	RING_DESC_CRC_ERROR = 0x1,
	RING_DESC_COMPLETED = 0x2,
	RING_DESC_POSTED = 0x4,
	RING_DESC_BUFFER_OVERRUN = 0x04,
	RING_DESC_INTERRUPT = 0x8,
};

/**
 * struct ring_frame - For use with ring_rx/ring_tx
 * @buffer_phy: DMA mapped address of the frame
 * @callback: Callback called when the frame is finished (optional)
 * @list: Frame is linked to a queue using this
 * @size: Size of the frame in bytes (%0 means %4096)
 * @flags: Flags for the frame (see &enum ring_desc_flags)
 * @eof: End of frame protocol defined field
 * @sof: Start of frame protocol defined field
 */
struct ring_frame {
	dma_addr_t buffer_phy;
	ring_cb callback;
	struct list_head list;
	u32 size:12;
	u32 flags:12;
	u32 eof:4;
	u32 sof:4;
};

/* Minimum size for ring_rx */
#define TB_FRAME_SIZE		0x100

struct tb_ring *tb_ring_alloc_tx(struct tb_nhi *nhi, int hop, int size,
				 unsigned int flags);
struct tb_ring *tb_ring_alloc_rx(struct tb_nhi *nhi, int hop, int size,
				 unsigned int flags, int e2e_tx_hop,
				 u16 sof_mask, u16 eof_mask,
				 void (*start_poll)(void *), void *poll_data);
void tb_ring_start(struct tb_ring *ring);
void tb_ring_stop(struct tb_ring *ring);
void tb_ring_free(struct tb_ring *ring);

int __tb_ring_enqueue(struct tb_ring *ring, struct ring_frame *frame);

/**
 * tb_ring_rx() - enqueue a frame on an RX ring
 * @ring: Ring to enqueue the frame
 * @frame: Frame to enqueue
 *
 * @frame->buffer, @frame->buffer_phy have to be set. The buffer must
 * contain at least %TB_FRAME_SIZE bytes.
 *
 * @frame->callback will be invoked with @frame->size, @frame->flags,
 * @frame->eof, @frame->sof set once the frame has been received.
 *
 * If ring_stop() is called after the packet has been enqueued
 * @frame->callback will be called with canceled set to true.
 *
 * Return: %-ESHUTDOWN if ring_stop() has been called, %0 otherwise.
 */
static inline int tb_ring_rx(struct tb_ring *ring, struct ring_frame *frame)
{
	WARN_ON(ring->is_tx);
	return __tb_ring_enqueue(ring, frame);
}

/**
 * tb_ring_tx() - enqueue a frame on an TX ring
 * @ring: Ring the enqueue the frame
 * @frame: Frame to enqueue
 *
 * @frame->buffer, @frame->buffer_phy, @frame->size, @frame->eof and
 * @frame->sof have to be set.
 *
 * @frame->callback will be invoked with once the frame has been transmitted.
 *
 * If ring_stop() is called after the packet has been enqueued @frame->callback
 * will be called with canceled set to true.
 *
 * Return: %-ESHUTDOWN if ring_stop has been called, %0 otherwise.
 */
static inline int tb_ring_tx(struct tb_ring *ring, struct ring_frame *frame)
{
	WARN_ON(!ring->is_tx);
	return __tb_ring_enqueue(ring, frame);
}

/* Used only when the ring is in polling mode */
struct ring_frame *tb_ring_poll(struct tb_ring *ring);
void tb_ring_poll_complete(struct tb_ring *ring);

/**
 * tb_ring_dma_device() - Return device used for DMA mapping
 * @ring: Ring whose DMA device is retrieved
 *
 * Use this function when you are mapping DMA for buffers that are
 * passed to the ring for sending/receiving.
 *
 * Return: Pointer to device used for DMA mapping.
 */
static inline struct device *tb_ring_dma_device(struct tb_ring *ring)
{
	return &ring->nhi->pdev->dev;
}

bool usb4_usb3_port_match(struct device *usb4_port_dev,
			  const struct fwnode_handle *usb3_port_fwnode);

#else /* CONFIG_USB4 */
static inline bool usb4_usb3_port_match(struct device *usb4_port_dev,
					const struct fwnode_handle *usb3_port_fwnode)
{
	return false;
}
#endif /* CONFIG_USB4 */

#endif /* THUNDERBOLT_H_ */
