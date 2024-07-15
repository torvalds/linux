/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022, Linaro Ltd.
 *
 */
#ifndef _MHI_EP_H_
#define _MHI_EP_H_

#include <linux/dma-direction.h>
#include <linux/mhi.h>

#define MHI_EP_DEFAULT_MTU 0x8000

/**
 * struct mhi_ep_channel_config - Channel configuration structure for controller
 * @name: The name of this channel
 * @num: The number assigned to this channel
 * @num_elements: The number of elements that can be queued to this channel
 * @dir: Direction that data may flow on this channel
 */
struct mhi_ep_channel_config {
	char *name;
	u32 num;
	u32 num_elements;
	enum dma_data_direction dir;
};

/**
 * struct mhi_ep_cntrl_config - MHI Endpoint controller configuration
 * @mhi_version: MHI spec version supported by the controller
 * @max_channels: Maximum number of channels supported
 * @num_channels: Number of channels defined in @ch_cfg
 * @ch_cfg: Array of defined channels
 */
struct mhi_ep_cntrl_config {
	u32 mhi_version;
	u32 max_channels;
	u32 num_channels;
	const struct mhi_ep_channel_config *ch_cfg;
};

/**
 * struct mhi_ep_db_info - MHI Endpoint doorbell info
 * @mask: Mask of the doorbell interrupt
 * @status: Status of the doorbell interrupt
 */
struct mhi_ep_db_info {
	u32 mask;
	u32 status;
};

/**
 * struct mhi_ep_buf_info - MHI Endpoint transfer buffer info
 * @mhi_dev: MHI device associated with this buffer
 * @dev_addr: Address of the buffer in endpoint
 * @host_addr: Address of the bufffer in host
 * @size: Size of the buffer
 * @code: Transfer completion code
 * @cb: Callback to be executed by controller drivers after transfer completion (async)
 * @cb_buf: Opaque buffer to be passed to the callback
 */
struct mhi_ep_buf_info {
	struct mhi_ep_device *mhi_dev;
	void *dev_addr;
	u64 host_addr;
	size_t size;
	int code;

	void (*cb)(struct mhi_ep_buf_info *buf_info);
	void *cb_buf;
};

/**
 * struct mhi_ep_cntrl - MHI Endpoint controller structure
 * @cntrl_dev: Pointer to the struct device of physical bus acting as the MHI
 *             Endpoint controller
 * @mhi_dev: MHI Endpoint device instance for the controller
 * @mmio: MMIO region containing the MHI registers
 * @mhi_chan: Points to the channel configuration table
 * @mhi_event: Points to the event ring configurations table
 * @mhi_cmd: Points to the command ring configurations table
 * @sm: MHI Endpoint state machine
 * @ch_ctx_cache: Cache of host channel context data structure
 * @ev_ctx_cache: Cache of host event context data structure
 * @cmd_ctx_cache: Cache of host command context data structure
 * @ch_ctx_host_pa: Physical address of host channel context data structure
 * @ev_ctx_host_pa: Physical address of host event context data structure
 * @cmd_ctx_host_pa: Physical address of host command context data structure
 * @ch_ctx_cache_phys: Physical address of the host channel context cache
 * @ev_ctx_cache_phys: Physical address of the host event context cache
 * @cmd_ctx_cache_phys: Physical address of the host command context cache
 * @chdb: Array of channel doorbell interrupt info
 * @event_lock: Lock for protecting event rings
 * @state_lock: Lock for protecting state transitions
 * @list_lock: Lock for protecting state transition and channel doorbell lists
 * @st_transition_list: List of state transitions
 * @ch_db_list: List of queued channel doorbells
 * @wq: Dedicated workqueue for handling rings and state changes
 * @state_work: State transition worker
 * @reset_work: Worker for MHI Endpoint reset
 * @cmd_ring_work: Worker for processing command rings
 * @ch_ring_work: Worker for processing channel rings
 * @raise_irq: CB function for raising IRQ to the host
 * @alloc_map: CB function for allocating memory in endpoint for storing host context and mapping it
 * @unmap_free: CB function to unmap and free the allocated memory in endpoint for storing host context
 * @read_sync: CB function for reading from host memory synchronously
 * @write_sync: CB function for writing to host memory synchronously
 * @read_async: CB function for reading from host memory asynchronously
 * @write_async: CB function for writing to host memory asynchronously
 * @mhi_state: MHI Endpoint state
 * @max_chan: Maximum channels supported by the endpoint controller
 * @mru: MRU (Maximum Receive Unit) value of the endpoint controller
 * @event_rings: Number of event rings supported by the endpoint controller
 * @hw_event_rings: Number of hardware event rings supported by the endpoint controller
 * @chdb_offset: Channel doorbell offset set by the host
 * @erdb_offset: Event ring doorbell offset set by the host
 * @index: MHI Endpoint controller index
 * @irq: IRQ used by the endpoint controller
 * @enabled: Check if the endpoint controller is enabled or not
 */
struct mhi_ep_cntrl {
	struct device *cntrl_dev;
	struct mhi_ep_device *mhi_dev;
	void __iomem *mmio;

	struct mhi_ep_chan *mhi_chan;
	struct mhi_ep_event *mhi_event;
	struct mhi_ep_cmd *mhi_cmd;
	struct mhi_ep_sm *sm;

	struct mhi_chan_ctxt *ch_ctx_cache;
	struct mhi_event_ctxt *ev_ctx_cache;
	struct mhi_cmd_ctxt *cmd_ctx_cache;
	u64 ch_ctx_host_pa;
	u64 ev_ctx_host_pa;
	u64 cmd_ctx_host_pa;
	phys_addr_t ch_ctx_cache_phys;
	phys_addr_t ev_ctx_cache_phys;
	phys_addr_t cmd_ctx_cache_phys;

	struct mhi_ep_db_info chdb[4];
	struct mutex event_lock;
	struct mutex state_lock;
	spinlock_t list_lock;

	struct list_head st_transition_list;
	struct list_head ch_db_list;

	struct workqueue_struct *wq;
	struct work_struct state_work;
	struct work_struct reset_work;
	struct work_struct cmd_ring_work;
	struct work_struct ch_ring_work;
	struct kmem_cache *ring_item_cache;
	struct kmem_cache *ev_ring_el_cache;
	struct kmem_cache *tre_buf_cache;

	void (*raise_irq)(struct mhi_ep_cntrl *mhi_cntrl, u32 vector);
	int (*alloc_map)(struct mhi_ep_cntrl *mhi_cntrl, u64 pci_addr, phys_addr_t *phys_ptr,
			 void __iomem **virt, size_t size);
	void (*unmap_free)(struct mhi_ep_cntrl *mhi_cntrl, u64 pci_addr, phys_addr_t phys,
			   void __iomem *virt, size_t size);
	int (*read_sync)(struct mhi_ep_cntrl *mhi_cntrl, struct mhi_ep_buf_info *buf_info);
	int (*write_sync)(struct mhi_ep_cntrl *mhi_cntrl, struct mhi_ep_buf_info *buf_info);
	int (*read_async)(struct mhi_ep_cntrl *mhi_cntrl, struct mhi_ep_buf_info *buf_info);
	int (*write_async)(struct mhi_ep_cntrl *mhi_cntrl, struct mhi_ep_buf_info *buf_info);

	enum mhi_state mhi_state;

	u32 max_chan;
	u32 mru;
	u32 event_rings;
	u32 hw_event_rings;
	u32 chdb_offset;
	u32 erdb_offset;
	u32 index;
	int irq;
	bool enabled;
};

/**
 * struct mhi_ep_device - Structure representing an MHI Endpoint device that binds
 *                     to channels or is associated with controllers
 * @dev: Driver model device node for the MHI Endpoint device
 * @mhi_cntrl: Controller the device belongs to
 * @id: Pointer to MHI Endpoint device ID struct
 * @name: Name of the associated MHI Endpoint device
 * @ul_chan: UL (from host to endpoint) channel for the device
 * @dl_chan: DL (from endpoint to host) channel for the device
 * @dev_type: MHI device type
 */
struct mhi_ep_device {
	struct device dev;
	struct mhi_ep_cntrl *mhi_cntrl;
	const struct mhi_device_id *id;
	const char *name;
	struct mhi_ep_chan *ul_chan;
	struct mhi_ep_chan *dl_chan;
	enum mhi_device_type dev_type;
};

/**
 * struct mhi_ep_driver - Structure representing a MHI Endpoint client driver
 * @id_table: Pointer to MHI Endpoint device ID table
 * @driver: Device driver model driver
 * @probe: CB function for client driver probe function
 * @remove: CB function for client driver remove function
 * @ul_xfer_cb: CB function for UL (from host to endpoint) data transfer
 * @dl_xfer_cb: CB function for DL (from endpoint to host) data transfer
 */
struct mhi_ep_driver {
	const struct mhi_device_id *id_table;
	struct device_driver driver;
	int (*probe)(struct mhi_ep_device *mhi_ep,
		     const struct mhi_device_id *id);
	void (*remove)(struct mhi_ep_device *mhi_ep);
	void (*ul_xfer_cb)(struct mhi_ep_device *mhi_dev,
			   struct mhi_result *result);
	void (*dl_xfer_cb)(struct mhi_ep_device *mhi_dev,
			   struct mhi_result *result);
};

#define to_mhi_ep_device(dev) container_of(dev, struct mhi_ep_device, dev)
#define to_mhi_ep_driver(drv) container_of(drv, struct mhi_ep_driver, driver)

/*
 * module_mhi_ep_driver() - Helper macro for drivers that don't do
 * anything special other than using default mhi_ep_driver_register() and
 * mhi_ep_driver_unregister().  This eliminates a lot of boilerplate.
 * Each module may only use this macro once.
 */
#define module_mhi_ep_driver(mhi_drv) \
	module_driver(mhi_drv, mhi_ep_driver_register, \
		      mhi_ep_driver_unregister)

/*
 * Macro to avoid include chaining to get THIS_MODULE
 */
#define mhi_ep_driver_register(mhi_drv) \
	__mhi_ep_driver_register(mhi_drv, THIS_MODULE)

/**
 * __mhi_ep_driver_register - Register a driver with MHI Endpoint bus
 * @mhi_drv: Driver to be associated with the device
 * @owner: The module owner
 *
 * Return: 0 if driver registrations succeeds, a negative error code otherwise.
 */
int __mhi_ep_driver_register(struct mhi_ep_driver *mhi_drv, struct module *owner);

/**
 * mhi_ep_driver_unregister - Unregister a driver from MHI Endpoint bus
 * @mhi_drv: Driver associated with the device
 */
void mhi_ep_driver_unregister(struct mhi_ep_driver *mhi_drv);

/**
 * mhi_ep_register_controller - Register MHI Endpoint controller
 * @mhi_cntrl: MHI Endpoint controller to register
 * @config: Configuration to use for the controller
 *
 * Return: 0 if controller registrations succeeds, a negative error code otherwise.
 */
int mhi_ep_register_controller(struct mhi_ep_cntrl *mhi_cntrl,
			       const struct mhi_ep_cntrl_config *config);

/**
 * mhi_ep_unregister_controller - Unregister MHI Endpoint controller
 * @mhi_cntrl: MHI Endpoint controller to unregister
 */
void mhi_ep_unregister_controller(struct mhi_ep_cntrl *mhi_cntrl);

/**
 * mhi_ep_power_up - Power up the MHI endpoint stack
 * @mhi_cntrl: MHI Endpoint controller
 *
 * Return: 0 if power up succeeds, a negative error code otherwise.
 */
int mhi_ep_power_up(struct mhi_ep_cntrl *mhi_cntrl);

/**
 * mhi_ep_power_down - Power down the MHI endpoint stack
 * @mhi_cntrl: MHI controller
 */
void mhi_ep_power_down(struct mhi_ep_cntrl *mhi_cntrl);

/**
 * mhi_ep_queue_is_empty - Determine whether the transfer queue is empty
 * @mhi_dev: Device associated with the channels
 * @dir: DMA direction for the channel
 *
 * Return: true if the queue is empty, false otherwise.
 */
bool mhi_ep_queue_is_empty(struct mhi_ep_device *mhi_dev, enum dma_data_direction dir);

/**
 * mhi_ep_queue_skb - Send SKBs to host over MHI Endpoint
 * @mhi_dev: Device associated with the DL channel
 * @skb: SKBs to be queued
 *
 * Return: 0 if the SKBs has been sent successfully, a negative error code otherwise.
 */
int mhi_ep_queue_skb(struct mhi_ep_device *mhi_dev, struct sk_buff *skb);

#endif
