/* SPDX-License-Identifier: GPL-2.0 */
/*
 * FPGA Framework
 *
 *  Copyright (C) 2013-2016 Altera Corporation
 *  Copyright (C) 2017 Intel Corporation
 */
#ifndef _LINUX_FPGA_MGR_H
#define _LINUX_FPGA_MGR_H

#include <linux/mutex.h>
#include <linux/platform_device.h>

struct fpga_manager;
struct sg_table;

/**
 * enum fpga_mgr_states - fpga framework states
 * @FPGA_MGR_STATE_UNKNOWN: can't determine state
 * @FPGA_MGR_STATE_POWER_OFF: FPGA power is off
 * @FPGA_MGR_STATE_POWER_UP: FPGA reports power is up
 * @FPGA_MGR_STATE_RESET: FPGA in reset state
 * @FPGA_MGR_STATE_FIRMWARE_REQ: firmware request in progress
 * @FPGA_MGR_STATE_FIRMWARE_REQ_ERR: firmware request failed
 * @FPGA_MGR_STATE_WRITE_INIT: preparing FPGA for programming
 * @FPGA_MGR_STATE_WRITE_INIT_ERR: Error during WRITE_INIT stage
 * @FPGA_MGR_STATE_WRITE: writing image to FPGA
 * @FPGA_MGR_STATE_WRITE_ERR: Error while writing FPGA
 * @FPGA_MGR_STATE_WRITE_COMPLETE: Doing post programming steps
 * @FPGA_MGR_STATE_WRITE_COMPLETE_ERR: Error during WRITE_COMPLETE
 * @FPGA_MGR_STATE_OPERATING: FPGA is programmed and operating
 */
enum fpga_mgr_states {
	/* default FPGA states */
	FPGA_MGR_STATE_UNKNOWN,
	FPGA_MGR_STATE_POWER_OFF,
	FPGA_MGR_STATE_POWER_UP,
	FPGA_MGR_STATE_RESET,

	/* getting an image for loading */
	FPGA_MGR_STATE_FIRMWARE_REQ,
	FPGA_MGR_STATE_FIRMWARE_REQ_ERR,

	/* write sequence: init, write, complete */
	FPGA_MGR_STATE_WRITE_INIT,
	FPGA_MGR_STATE_WRITE_INIT_ERR,
	FPGA_MGR_STATE_WRITE,
	FPGA_MGR_STATE_WRITE_ERR,
	FPGA_MGR_STATE_WRITE_COMPLETE,
	FPGA_MGR_STATE_WRITE_COMPLETE_ERR,

	/* fpga is programmed and operating */
	FPGA_MGR_STATE_OPERATING,
};

/**
 * DOC: FPGA Manager flags
 *
 * Flags used in the &fpga_image_info->flags field
 *
 * %FPGA_MGR_PARTIAL_RECONFIG: do partial reconfiguration if supported
 *
 * %FPGA_MGR_EXTERNAL_CONFIG: FPGA has been configured prior to Linux booting
 *
 * %FPGA_MGR_ENCRYPTED_BITSTREAM: indicates bitstream is encrypted
 *
 * %FPGA_MGR_BITSTREAM_LSB_FIRST: SPI bitstream bit order is LSB first
 *
 * %FPGA_MGR_COMPRESSED_BITSTREAM: FPGA bitstream is compressed
 */
#define FPGA_MGR_PARTIAL_RECONFIG	BIT(0)
#define FPGA_MGR_EXTERNAL_CONFIG	BIT(1)
#define FPGA_MGR_ENCRYPTED_BITSTREAM	BIT(2)
#define FPGA_MGR_BITSTREAM_LSB_FIRST	BIT(3)
#define FPGA_MGR_COMPRESSED_BITSTREAM	BIT(4)

/**
 * struct fpga_image_info - information specific to a FPGA image
 * @flags: boolean flags as defined above
 * @enable_timeout_us: maximum time to enable traffic through bridge (uSec)
 * @disable_timeout_us: maximum time to disable traffic through bridge (uSec)
 * @config_complete_timeout_us: maximum time for FPGA to switch to operating
 *	   status in the write_complete op.
 * @firmware_name: name of FPGA image firmware file
 * @sgt: scatter/gather table containing FPGA image
 * @buf: contiguous buffer containing FPGA image
 * @count: size of buf
 * @region_id: id of target region
 * @dev: device that owns this
 * @overlay: Device Tree overlay
 */
struct fpga_image_info {
	u32 flags;
	u32 enable_timeout_us;
	u32 disable_timeout_us;
	u32 config_complete_timeout_us;
	char *firmware_name;
	struct sg_table *sgt;
	const char *buf;
	size_t count;
	int region_id;
	struct device *dev;
#ifdef CONFIG_OF
	struct device_node *overlay;
#endif
};

/**
 * struct fpga_manager_ops - ops for low level fpga manager drivers
 * @initial_header_size: Maximum number of bytes that should be passed into write_init
 * @state: returns an enum value of the FPGA's state
 * @status: returns status of the FPGA, including reconfiguration error code
 * @write_init: prepare the FPGA to receive confuration data
 * @write: write count bytes of configuration data to the FPGA
 * @write_sg: write the scatter list of configuration data to the FPGA
 * @write_complete: set FPGA to operating state after writing is done
 * @fpga_remove: optional: Set FPGA into a specific state during driver remove
 * @groups: optional attribute groups.
 *
 * fpga_manager_ops are the low level functions implemented by a specific
 * fpga manager driver.  The optional ones are tested for NULL before being
 * called, so leaving them out is fine.
 */
struct fpga_manager_ops {
	size_t initial_header_size;
	enum fpga_mgr_states (*state)(struct fpga_manager *mgr);
	u64 (*status)(struct fpga_manager *mgr);
	int (*write_init)(struct fpga_manager *mgr,
			  struct fpga_image_info *info,
			  const char *buf, size_t count);
	int (*write)(struct fpga_manager *mgr, const char *buf, size_t count);
	int (*write_sg)(struct fpga_manager *mgr, struct sg_table *sgt);
	int (*write_complete)(struct fpga_manager *mgr,
			      struct fpga_image_info *info);
	void (*fpga_remove)(struct fpga_manager *mgr);
	const struct attribute_group **groups;
};

/* FPGA manager status: Partial/Full Reconfiguration errors */
#define FPGA_MGR_STATUS_OPERATION_ERR		BIT(0)
#define FPGA_MGR_STATUS_CRC_ERR			BIT(1)
#define FPGA_MGR_STATUS_INCOMPATIBLE_IMAGE_ERR	BIT(2)
#define FPGA_MGR_STATUS_IP_PROTOCOL_ERR		BIT(3)
#define FPGA_MGR_STATUS_FIFO_OVERFLOW_ERR	BIT(4)

/**
 * struct fpga_compat_id - id for compatibility check
 *
 * @id_h: high 64bit of the compat_id
 * @id_l: low 64bit of the compat_id
 */
struct fpga_compat_id {
	u64 id_h;
	u64 id_l;
};

/**
 * struct fpga_manager - fpga manager structure
 * @name: name of low level fpga manager
 * @dev: fpga manager device
 * @ref_mutex: only allows one reference to fpga manager
 * @state: state of fpga manager
 * @compat_id: FPGA manager id for compatibility check.
 * @mops: pointer to struct of fpga manager ops
 * @priv: low level driver private date
 */
struct fpga_manager {
	const char *name;
	struct device dev;
	struct mutex ref_mutex;
	enum fpga_mgr_states state;
	struct fpga_compat_id *compat_id;
	const struct fpga_manager_ops *mops;
	void *priv;
};

#define to_fpga_manager(d) container_of(d, struct fpga_manager, dev)

struct fpga_image_info *fpga_image_info_alloc(struct device *dev);

void fpga_image_info_free(struct fpga_image_info *info);

int fpga_mgr_load(struct fpga_manager *mgr, struct fpga_image_info *info);

int fpga_mgr_lock(struct fpga_manager *mgr);
void fpga_mgr_unlock(struct fpga_manager *mgr);

struct fpga_manager *of_fpga_mgr_get(struct device_node *node);

struct fpga_manager *fpga_mgr_get(struct device *dev);

void fpga_mgr_put(struct fpga_manager *mgr);

struct fpga_manager *fpga_mgr_create(struct device *dev, const char *name,
				     const struct fpga_manager_ops *mops,
				     void *priv);
void fpga_mgr_free(struct fpga_manager *mgr);
int fpga_mgr_register(struct fpga_manager *mgr);
void fpga_mgr_unregister(struct fpga_manager *mgr);

#endif /*_LINUX_FPGA_MGR_H */
