/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Freescale Management Complex (MC) bus public interface
 *
 * Copyright (C) 2014-2016 Freescale Semiconductor, Inc.
 * Copyright 2019-2020 NXP
 * Author: German Rivera <German.Rivera@freescale.com>
 *
 */
#ifndef _FSL_MC_H_
#define _FSL_MC_H_

#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/interrupt.h>

#define FSL_MC_VENDOR_FREESCALE	0x1957

struct irq_domain;
struct msi_domain_info;

struct fsl_mc_device;
struct fsl_mc_io;

/**
 * struct fsl_mc_driver - MC object device driver object
 * @driver: Generic device driver
 * @match_id_table: table of supported device matching Ids
 * @probe: Function called when a device is added
 * @remove: Function called when a device is removed
 * @shutdown: Function called at shutdown time to quiesce the device
 * @suspend: Function called when a device is stopped
 * @resume: Function called when a device is resumed
 *
 * Generic DPAA device driver object for device drivers that are registered
 * with a DPRC bus. This structure is to be embedded in each device-specific
 * driver structure.
 */
struct fsl_mc_driver {
	struct device_driver driver;
	const struct fsl_mc_device_id *match_id_table;
	int (*probe)(struct fsl_mc_device *dev);
	int (*remove)(struct fsl_mc_device *dev);
	void (*shutdown)(struct fsl_mc_device *dev);
	int (*suspend)(struct fsl_mc_device *dev, pm_message_t state);
	int (*resume)(struct fsl_mc_device *dev);
};

#define to_fsl_mc_driver(_drv) \
	container_of(_drv, struct fsl_mc_driver, driver)

/**
 * enum fsl_mc_pool_type - Types of allocatable MC bus resources
 *
 * Entries in these enum are used as indices in the array of resource
 * pools of an fsl_mc_bus object.
 */
enum fsl_mc_pool_type {
	FSL_MC_POOL_DPMCP = 0x0,    /* corresponds to "dpmcp" in the MC */
	FSL_MC_POOL_DPBP,	    /* corresponds to "dpbp" in the MC */
	FSL_MC_POOL_DPCON,	    /* corresponds to "dpcon" in the MC */
	FSL_MC_POOL_IRQ,

	/*
	 * NOTE: New resource pool types must be added before this entry
	 */
	FSL_MC_NUM_POOL_TYPES
};

/**
 * struct fsl_mc_resource - MC generic resource
 * @type: type of resource
 * @id: unique MC resource Id within the resources of the same type
 * @data: pointer to resource-specific data if the resource is currently
 * allocated, or NULL if the resource is not currently allocated.
 * @parent_pool: pointer to the parent resource pool from which this
 * resource is allocated from.
 * @node: Node in the free list of the corresponding resource pool
 *
 * NOTE: This structure is to be embedded as a field of specific
 * MC resource structures.
 */
struct fsl_mc_resource {
	enum fsl_mc_pool_type type;
	s32 id;
	void *data;
	struct fsl_mc_resource_pool *parent_pool;
	struct list_head node;
};

/**
 * struct fsl_mc_device_irq - MC object device message-based interrupt
 * @msi_desc: pointer to MSI descriptor allocated by fsl_mc_msi_alloc_descs()
 * @mc_dev: MC object device that owns this interrupt
 * @dev_irq_index: device-relative IRQ index
 * @resource: MC generic resource associated with the interrupt
 */
struct fsl_mc_device_irq {
	struct msi_desc *msi_desc;
	struct fsl_mc_device *mc_dev;
	u8 dev_irq_index;
	struct fsl_mc_resource resource;
};

#define to_fsl_mc_irq(_mc_resource) \
	container_of(_mc_resource, struct fsl_mc_device_irq, resource)

/* Opened state - Indicates that an object is open by at least one owner */
#define FSL_MC_OBJ_STATE_OPEN		0x00000001
/* Plugged state - Indicates that the object is plugged */
#define FSL_MC_OBJ_STATE_PLUGGED	0x00000002

/**
 * Shareability flag - Object flag indicating no memory shareability.
 * the object generates memory accesses that are non coherent with other
 * masters;
 * user is responsible for proper memory handling through IOMMU configuration.
 */
#define FSL_MC_OBJ_FLAG_NO_MEM_SHAREABILITY	0x0001

/**
 * struct fsl_mc_obj_desc - Object descriptor
 * @type: Type of object: NULL terminated string
 * @id: ID of logical object resource
 * @vendor: Object vendor identifier
 * @ver_major: Major version number
 * @ver_minor:  Minor version number
 * @irq_count: Number of interrupts supported by the object
 * @region_count: Number of mappable regions supported by the object
 * @state: Object state: combination of FSL_MC_OBJ_STATE_ states
 * @label: Object label: NULL terminated string
 * @flags: Object's flags
 */
struct fsl_mc_obj_desc {
	char type[16];
	int id;
	u16 vendor;
	u16 ver_major;
	u16 ver_minor;
	u8 irq_count;
	u8 region_count;
	u32 state;
	char label[16];
	u16 flags;
};

/**
 * Bit masks for a MC object device (struct fsl_mc_device) flags
 */
#define FSL_MC_IS_DPRC	0x0001

/* Region flags */
/* Indicates that region can be mapped as cacheable */
#define FSL_MC_REGION_CACHEABLE	0x00000001

/* Indicates that region can be mapped as shareable */
#define FSL_MC_REGION_SHAREABLE	0x00000002

/**
 * struct fsl_mc_device - MC object device object
 * @dev: Linux driver model device object
 * @dma_mask: Default DMA mask
 * @flags: MC object device flags
 * @icid: Isolation context ID for the device
 * @mc_handle: MC handle for the corresponding MC object opened
 * @mc_io: Pointer to MC IO object assigned to this device or
 * NULL if none.
 * @obj_desc: MC description of the DPAA device
 * @regions: pointer to array of MMIO region entries
 * @irqs: pointer to array of pointers to interrupts allocated to this device
 * @resource: generic resource associated with this MC object device, if any.
 * @driver_override: driver name to force a match
 *
 * Generic device object for MC object devices that are "attached" to a
 * MC bus.
 *
 * NOTES:
 * - For a non-DPRC object its icid is the same as its parent DPRC's icid.
 * - The SMMU notifier callback gets invoked after device_add() has been
 *   called for an MC object device, but before the device-specific probe
 *   callback gets called.
 * - DP_OBJ_DPRC objects are the only MC objects that have built-in MC
 *   portals. For all other MC objects, their device drivers are responsible for
 *   allocating MC portals for them by calling fsl_mc_portal_allocate().
 * - Some types of MC objects (e.g., DP_OBJ_DPBP, DP_OBJ_DPCON) are
 *   treated as resources that can be allocated/deallocated from the
 *   corresponding resource pool in the object's parent DPRC, using the
 *   fsl_mc_object_allocate()/fsl_mc_object_free() functions. These MC objects
 *   are known as "allocatable" objects. For them, the corresponding
 *   fsl_mc_device's 'resource' points to the associated resource object.
 *   For MC objects that are not allocatable (e.g., DP_OBJ_DPRC, DP_OBJ_DPNI),
 *   'resource' is NULL.
 */
struct fsl_mc_device {
	struct device dev;
	u64 dma_mask;
	u16 flags;
	u32 icid;
	u16 mc_handle;
	struct fsl_mc_io *mc_io;
	struct fsl_mc_obj_desc obj_desc;
	struct resource *regions;
	struct fsl_mc_device_irq **irqs;
	struct fsl_mc_resource *resource;
	struct device_link *consumer_link;
	char   *driver_override;
};

#define to_fsl_mc_device(_dev) \
	container_of(_dev, struct fsl_mc_device, dev)

#define MC_CMD_NUM_OF_PARAMS	7

struct mc_cmd_header {
	u8 src_id;
	u8 flags_hw;
	u8 status;
	u8 flags_sw;
	__le16 token;
	__le16 cmd_id;
};

struct fsl_mc_command {
	__le64 header;
	__le64 params[MC_CMD_NUM_OF_PARAMS];
};

enum mc_cmd_status {
	MC_CMD_STATUS_OK = 0x0, /* Completed successfully */
	MC_CMD_STATUS_READY = 0x1, /* Ready to be processed */
	MC_CMD_STATUS_AUTH_ERR = 0x3, /* Authentication error */
	MC_CMD_STATUS_NO_PRIVILEGE = 0x4, /* No privilege */
	MC_CMD_STATUS_DMA_ERR = 0x5, /* DMA or I/O error */
	MC_CMD_STATUS_CONFIG_ERR = 0x6, /* Configuration error */
	MC_CMD_STATUS_TIMEOUT = 0x7, /* Operation timed out */
	MC_CMD_STATUS_NO_RESOURCE = 0x8, /* No resources */
	MC_CMD_STATUS_NO_MEMORY = 0x9, /* No memory available */
	MC_CMD_STATUS_BUSY = 0xA, /* Device is busy */
	MC_CMD_STATUS_UNSUPPORTED_OP = 0xB, /* Unsupported operation */
	MC_CMD_STATUS_INVALID_STATE = 0xC /* Invalid state */
};

/*
 * MC command flags
 */

/* High priority flag */
#define MC_CMD_FLAG_PRI		0x80
/* Command completion flag */
#define MC_CMD_FLAG_INTR_DIS	0x01

static inline __le64 mc_encode_cmd_header(u16 cmd_id,
					  u32 cmd_flags,
					  u16 token)
{
	__le64 header = 0;
	struct mc_cmd_header *hdr = (struct mc_cmd_header *)&header;

	hdr->cmd_id = cpu_to_le16(cmd_id);
	hdr->token  = cpu_to_le16(token);
	hdr->status = MC_CMD_STATUS_READY;
	if (cmd_flags & MC_CMD_FLAG_PRI)
		hdr->flags_hw = MC_CMD_FLAG_PRI;
	if (cmd_flags & MC_CMD_FLAG_INTR_DIS)
		hdr->flags_sw = MC_CMD_FLAG_INTR_DIS;

	return header;
}

static inline u16 mc_cmd_hdr_read_token(struct fsl_mc_command *cmd)
{
	struct mc_cmd_header *hdr = (struct mc_cmd_header *)&cmd->header;
	u16 token = le16_to_cpu(hdr->token);

	return token;
}

struct mc_rsp_create {
	__le32 object_id;
};

struct mc_rsp_api_ver {
	__le16 major_ver;
	__le16 minor_ver;
};

static inline u32 mc_cmd_read_object_id(struct fsl_mc_command *cmd)
{
	struct mc_rsp_create *rsp_params;

	rsp_params = (struct mc_rsp_create *)cmd->params;
	return le32_to_cpu(rsp_params->object_id);
}

static inline void mc_cmd_read_api_version(struct fsl_mc_command *cmd,
					   u16 *major_ver,
					   u16 *minor_ver)
{
	struct mc_rsp_api_ver *rsp_params;

	rsp_params = (struct mc_rsp_api_ver *)cmd->params;
	*major_ver = le16_to_cpu(rsp_params->major_ver);
	*minor_ver = le16_to_cpu(rsp_params->minor_ver);
}

/**
 * Bit masks for a MC I/O object (struct fsl_mc_io) flags
 */
#define FSL_MC_IO_ATOMIC_CONTEXT_PORTAL	0x0001

/**
 * struct fsl_mc_io - MC I/O object to be passed-in to mc_send_command()
 * @dev: device associated with this Mc I/O object
 * @flags: flags for mc_send_command()
 * @portal_size: MC command portal size in bytes
 * @portal_phys_addr: MC command portal physical address
 * @portal_virt_addr: MC command portal virtual address
 * @dpmcp_dev: pointer to the DPMCP device associated with the MC portal.
 *
 * Fields are only meaningful if the FSL_MC_IO_ATOMIC_CONTEXT_PORTAL flag is not
 * set:
 * @mutex: Mutex to serialize mc_send_command() calls that use the same MC
 * portal, if the fsl_mc_io object was created with the
 * FSL_MC_IO_ATOMIC_CONTEXT_PORTAL flag off. mc_send_command() calls for this
 * fsl_mc_io object must be made only from non-atomic context.
 *
 * Fields are only meaningful if the FSL_MC_IO_ATOMIC_CONTEXT_PORTAL flag is
 * set:
 * @spinlock: Spinlock to serialize mc_send_command() calls that use the same MC
 * portal, if the fsl_mc_io object was created with the
 * FSL_MC_IO_ATOMIC_CONTEXT_PORTAL flag on. mc_send_command() calls for this
 * fsl_mc_io object can be made from atomic or non-atomic context.
 */
struct fsl_mc_io {
	struct device *dev;
	u16 flags;
	u32 portal_size;
	phys_addr_t portal_phys_addr;
	void __iomem *portal_virt_addr;
	struct fsl_mc_device *dpmcp_dev;
	union {
		/*
		 * This field is only meaningful if the
		 * FSL_MC_IO_ATOMIC_CONTEXT_PORTAL flag is not set
		 */
		struct mutex mutex; /* serializes mc_send_command() */

		/*
		 * This field is only meaningful if the
		 * FSL_MC_IO_ATOMIC_CONTEXT_PORTAL flag is set
		 */
		raw_spinlock_t spinlock; /* serializes mc_send_command() */
	};
};

int mc_send_command(struct fsl_mc_io *mc_io, struct fsl_mc_command *cmd);

#ifdef CONFIG_FSL_MC_BUS
#define dev_is_fsl_mc(_dev) ((_dev)->bus == &fsl_mc_bus_type)
#else
/* If fsl-mc bus is not present device cannot belong to fsl-mc bus */
#define dev_is_fsl_mc(_dev) (0)
#endif

/* Macro to check if a device is a container device */
#define fsl_mc_is_cont_dev(_dev) (to_fsl_mc_device(_dev)->flags & \
	FSL_MC_IS_DPRC)

/* Macro to get the container device of a MC device */
#define fsl_mc_cont_dev(_dev) (fsl_mc_is_cont_dev(_dev) ? \
	(_dev) : (_dev)->parent)

/*
 * module_fsl_mc_driver() - Helper macro for drivers that don't do
 * anything special in module init/exit.  This eliminates a lot of
 * boilerplate.  Each module may only use this macro once, and
 * calling it replaces module_init() and module_exit()
 */
#define module_fsl_mc_driver(__fsl_mc_driver) \
	module_driver(__fsl_mc_driver, fsl_mc_driver_register, \
		      fsl_mc_driver_unregister)

/*
 * Macro to avoid include chaining to get THIS_MODULE
 */
#define fsl_mc_driver_register(drv) \
	__fsl_mc_driver_register(drv, THIS_MODULE)

int __must_check __fsl_mc_driver_register(struct fsl_mc_driver *fsl_mc_driver,
					  struct module *owner);

void fsl_mc_driver_unregister(struct fsl_mc_driver *driver);

/**
 * struct fsl_mc_version
 * @major: Major version number: incremented on API compatibility changes
 * @minor: Minor version number: incremented on API additions (that are
 *		backward compatible); reset when major version is incremented
 * @revision: Internal revision number: incremented on implementation changes
 *		and/or bug fixes that have no impact on API
 */
struct fsl_mc_version {
	u32 major;
	u32 minor;
	u32 revision;
};

struct fsl_mc_version *fsl_mc_get_version(void);

int __must_check fsl_mc_portal_allocate(struct fsl_mc_device *mc_dev,
					u16 mc_io_flags,
					struct fsl_mc_io **new_mc_io);

void fsl_mc_portal_free(struct fsl_mc_io *mc_io);

int fsl_mc_portal_reset(struct fsl_mc_io *mc_io);

int __must_check fsl_mc_object_allocate(struct fsl_mc_device *mc_dev,
					enum fsl_mc_pool_type pool_type,
					struct fsl_mc_device **new_mc_adev);

void fsl_mc_object_free(struct fsl_mc_device *mc_adev);

struct irq_domain *fsl_mc_msi_create_irq_domain(struct fwnode_handle *fwnode,
						struct msi_domain_info *info,
						struct irq_domain *parent);

int __must_check fsl_mc_allocate_irqs(struct fsl_mc_device *mc_dev);

void fsl_mc_free_irqs(struct fsl_mc_device *mc_dev);

struct fsl_mc_device *fsl_mc_get_endpoint(struct fsl_mc_device *mc_dev);

extern struct bus_type fsl_mc_bus_type;

extern struct device_type fsl_mc_bus_dprc_type;
extern struct device_type fsl_mc_bus_dpni_type;
extern struct device_type fsl_mc_bus_dpio_type;
extern struct device_type fsl_mc_bus_dpsw_type;
extern struct device_type fsl_mc_bus_dpbp_type;
extern struct device_type fsl_mc_bus_dpcon_type;
extern struct device_type fsl_mc_bus_dpmcp_type;
extern struct device_type fsl_mc_bus_dpmac_type;
extern struct device_type fsl_mc_bus_dprtc_type;
extern struct device_type fsl_mc_bus_dpseci_type;
extern struct device_type fsl_mc_bus_dpdmux_type;
extern struct device_type fsl_mc_bus_dpdcei_type;
extern struct device_type fsl_mc_bus_dpaiop_type;
extern struct device_type fsl_mc_bus_dpci_type;
extern struct device_type fsl_mc_bus_dpdmai_type;

static inline bool is_fsl_mc_bus_dprc(const struct fsl_mc_device *mc_dev)
{
	return mc_dev->dev.type == &fsl_mc_bus_dprc_type;
}

static inline bool is_fsl_mc_bus_dpni(const struct fsl_mc_device *mc_dev)
{
	return mc_dev->dev.type == &fsl_mc_bus_dpni_type;
}

static inline bool is_fsl_mc_bus_dpio(const struct fsl_mc_device *mc_dev)
{
	return mc_dev->dev.type == &fsl_mc_bus_dpio_type;
}

static inline bool is_fsl_mc_bus_dpsw(const struct fsl_mc_device *mc_dev)
{
	return mc_dev->dev.type == &fsl_mc_bus_dpsw_type;
}

static inline bool is_fsl_mc_bus_dpdmux(const struct fsl_mc_device *mc_dev)
{
	return mc_dev->dev.type == &fsl_mc_bus_dpdmux_type;
}

static inline bool is_fsl_mc_bus_dpbp(const struct fsl_mc_device *mc_dev)
{
	return mc_dev->dev.type == &fsl_mc_bus_dpbp_type;
}

static inline bool is_fsl_mc_bus_dpcon(const struct fsl_mc_device *mc_dev)
{
	return mc_dev->dev.type == &fsl_mc_bus_dpcon_type;
}

static inline bool is_fsl_mc_bus_dpmcp(const struct fsl_mc_device *mc_dev)
{
	return mc_dev->dev.type == &fsl_mc_bus_dpmcp_type;
}

static inline bool is_fsl_mc_bus_dpmac(const struct fsl_mc_device *mc_dev)
{
	return mc_dev->dev.type == &fsl_mc_bus_dpmac_type;
}

static inline bool is_fsl_mc_bus_dprtc(const struct fsl_mc_device *mc_dev)
{
	return mc_dev->dev.type == &fsl_mc_bus_dprtc_type;
}

static inline bool is_fsl_mc_bus_dpseci(const struct fsl_mc_device *mc_dev)
{
	return mc_dev->dev.type == &fsl_mc_bus_dpseci_type;
}

static inline bool is_fsl_mc_bus_dpdcei(const struct fsl_mc_device *mc_dev)
{
	return mc_dev->dev.type == &fsl_mc_bus_dpdcei_type;
}

static inline bool is_fsl_mc_bus_dpaiop(const struct fsl_mc_device *mc_dev)
{
	return mc_dev->dev.type == &fsl_mc_bus_dpaiop_type;
}

static inline bool is_fsl_mc_bus_dpci(const struct fsl_mc_device *mc_dev)
{
	return mc_dev->dev.type == &fsl_mc_bus_dpci_type;
}

static inline bool is_fsl_mc_bus_dpdmai(const struct fsl_mc_device *mc_dev)
{
	return mc_dev->dev.type == &fsl_mc_bus_dpdmai_type;
}

#define DPRC_RESET_OPTION_NON_RECURSIVE                0x00000001
int dprc_reset_container(struct fsl_mc_io *mc_io,
			 u32 cmd_flags,
			 u16 token,
			 int child_container_id,
			 u32 options);

int dprc_scan_container(struct fsl_mc_device *mc_bus_dev,
			bool alloc_interrupts);

void dprc_remove_devices(struct fsl_mc_device *mc_bus_dev,
			 struct fsl_mc_obj_desc *obj_desc_array,
			 int num_child_objects_in_mc);

int dprc_cleanup(struct fsl_mc_device *mc_dev);

int dprc_setup(struct fsl_mc_device *mc_dev);

/**
 * Maximum number of total IRQs that can be pre-allocated for an MC bus'
 * IRQ pool
 */
#define FSL_MC_IRQ_POOL_MAX_TOTAL_IRQS	256

int fsl_mc_populate_irq_pool(struct fsl_mc_device *mc_bus_dev,
			     unsigned int irq_count);

void fsl_mc_cleanup_irq_pool(struct fsl_mc_device *mc_bus_dev);

/*
 * Data Path Buffer Pool (DPBP) API
 * Contains initialization APIs and runtime control APIs for DPBP
 */

int dpbp_open(struct fsl_mc_io *mc_io,
	      u32 cmd_flags,
	      int dpbp_id,
	      u16 *token);

int dpbp_close(struct fsl_mc_io *mc_io,
	       u32 cmd_flags,
	       u16 token);

int dpbp_enable(struct fsl_mc_io *mc_io,
		u32 cmd_flags,
		u16 token);

int dpbp_disable(struct fsl_mc_io *mc_io,
		 u32 cmd_flags,
		 u16 token);

int dpbp_reset(struct fsl_mc_io *mc_io,
	       u32 cmd_flags,
	       u16 token);

/**
 * struct dpbp_attr - Structure representing DPBP attributes
 * @id:		DPBP object ID
 * @bpid:	Hardware buffer pool ID; should be used as an argument in
 *		acquire/release operations on buffers
 */
struct dpbp_attr {
	int id;
	u16 bpid;
};

int dpbp_get_attributes(struct fsl_mc_io *mc_io,
			u32 cmd_flags,
			u16 token,
			struct dpbp_attr *attr);

/* Data Path Concentrator (DPCON) API
 * Contains initialization APIs and runtime control APIs for DPCON
 */

/**
 * Use it to disable notifications; see dpcon_set_notification()
 */
#define DPCON_INVALID_DPIO_ID		(int)(-1)

int dpcon_open(struct fsl_mc_io *mc_io,
	       u32 cmd_flags,
	       int dpcon_id,
	       u16 *token);

int dpcon_close(struct fsl_mc_io *mc_io,
		u32 cmd_flags,
		u16 token);

int dpcon_enable(struct fsl_mc_io *mc_io,
		 u32 cmd_flags,
		 u16 token);

int dpcon_disable(struct fsl_mc_io *mc_io,
		  u32 cmd_flags,
		  u16 token);

int dpcon_reset(struct fsl_mc_io *mc_io,
		u32 cmd_flags,
		u16 token);

/**
 * struct dpcon_attr - Structure representing DPCON attributes
 * @id: DPCON object ID
 * @qbman_ch_id: Channel ID to be used by dequeue operation
 * @num_priorities: Number of priorities for the DPCON channel (1-8)
 */
struct dpcon_attr {
	int id;
	u16 qbman_ch_id;
	u8 num_priorities;
};

int dpcon_get_attributes(struct fsl_mc_io *mc_io,
			 u32 cmd_flags,
			 u16 token,
			 struct dpcon_attr *attr);

/**
 * struct dpcon_notification_cfg - Structure representing notification params
 * @dpio_id:	DPIO object ID; must be configured with a notification channel;
 *	to disable notifications set it to 'DPCON_INVALID_DPIO_ID';
 * @priority:	Priority selection within the DPIO channel; valid values
 *		are 0-7, depending on the number of priorities in that channel
 * @user_ctx:	User context value provided with each CDAN message
 */
struct dpcon_notification_cfg {
	int dpio_id;
	u8 priority;
	u64 user_ctx;
};

int dpcon_set_notification(struct fsl_mc_io *mc_io,
			   u32 cmd_flags,
			   u16 token,
			   struct dpcon_notification_cfg *cfg);

#endif /* _FSL_MC_H_ */
