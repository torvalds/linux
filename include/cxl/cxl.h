/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2020 Intel Corporation. */
/* Copyright(c) 2026 Advanced Micro Devices, Inc. */

#ifndef __CXL_CXL_H__
#define __CXL_CXL_H__

#include <linux/node.h>
#include <linux/ioport.h>
#include <cxl/mailbox.h>

/**
 * enum cxl_devtype - delineate type-2 from a generic type-3 device
 * @CXL_DEVTYPE_DEVMEM: Vendor specific CXL Type-2 device implementing HDM-D or
 *			 HDM-DB, no requirement that this device implements a
 *			 mailbox, or other memory-device-standard manageability
 *			 flows.
 * @CXL_DEVTYPE_CLASSMEM: Common class definition of a CXL Type-3 device with
 *			   HDM-H and class-mandatory memory device registers
 */
enum cxl_devtype {
	CXL_DEVTYPE_DEVMEM,
	CXL_DEVTYPE_CLASSMEM,
};

struct device;

/*
 * Using struct_group() allows for per register-block-type helper routines,
 * without requiring block-type agnostic code to include the prefix.
 */
struct cxl_regs {
	/*
	 * Common set of CXL Component register block base pointers
	 * @hdm_decoder: CXL 2.0 8.2.5.12 CXL HDM Decoder Capability Structure
	 * @ras: CXL 2.0 8.2.5.9 CXL RAS Capability Structure
	 */
	struct_group_tagged(cxl_component_regs, component,
		void __iomem *hdm_decoder;
		void __iomem *ras;
	);
	/*
	 * Common set of CXL Device register block base pointers
	 * @status: CXL 2.0 8.2.8.3 Device Status Registers
	 * @mbox: CXL 2.0 8.2.8.4 Mailbox Registers
	 * @memdev: CXL 2.0 8.2.8.5 Memory Device Registers
	 */
	struct_group_tagged(cxl_device_regs, device_regs,
		void __iomem *status, *mbox, *memdev;
	);

	struct_group_tagged(cxl_pmu_regs, pmu_regs,
		void __iomem *pmu;
	);

	/*
	 * RCH downstream port specific RAS register
	 * @aer: CXL 3.0 8.2.1.1 RCH Downstream Port RCRB
	 */
	struct_group_tagged(cxl_rch_regs, rch_regs,
		void __iomem *dport_aer;
	);

	/*
	 * RCD upstream port specific PCIe cap register
	 * @pcie_cap: CXL 3.0 8.2.1.2 RCD Upstream Port RCRB
	 */
	struct_group_tagged(cxl_rcd_regs, rcd_regs,
		void __iomem *rcd_pcie_cap;
	);
};

struct cxl_reg_map {
	bool valid;
	int id;
	unsigned long offset;
	unsigned long size;
};

struct cxl_component_reg_map {
	struct cxl_reg_map hdm_decoder;
	struct cxl_reg_map ras;
};

struct cxl_device_reg_map {
	struct cxl_reg_map status;
	struct cxl_reg_map mbox;
	struct cxl_reg_map memdev;
};

struct cxl_pmu_reg_map {
	struct cxl_reg_map pmu;
};

/**
 * struct cxl_register_map - DVSEC harvested register block mapping parameters
 * @host: device for devm operations and logging
 * @base: virtual base of the register-block-BAR + @block_offset
 * @resource: physical resource base of the register block
 * @max_size: maximum mapping size to perform register search
 * @reg_type: see enum cxl_regloc_type
 * @component_map: cxl_reg_map for component registers
 * @device_map: cxl_reg_maps for device registers
 * @pmu_map: cxl_reg_maps for CXL Performance Monitoring Units
 */
struct cxl_register_map {
	struct device *host;
	void __iomem *base;
	resource_size_t resource;
	resource_size_t max_size;
	u8 reg_type;
	union {
		struct cxl_component_reg_map component_map;
		struct cxl_device_reg_map device_map;
		struct cxl_pmu_reg_map pmu_map;
	};
};

/**
 * struct cxl_dpa_perf - DPA performance property entry
 * @dpa_range: range for DPA address
 * @coord: QoS performance data (i.e. latency, bandwidth)
 * @cdat_coord: raw QoS performance data from CDAT
 * @qos_class: QoS Class cookies
 */
struct cxl_dpa_perf {
	struct range dpa_range;
	struct access_coordinate coord[ACCESS_COORDINATE_MAX];
	struct access_coordinate cdat_coord[ACCESS_COORDINATE_MAX];
	int qos_class;
};

enum cxl_partition_mode {
	CXL_PARTMODE_RAM,
	CXL_PARTMODE_PMEM,
};

/**
 * struct cxl_dpa_partition - DPA partition descriptor
 * @res: shortcut to the partition in the DPA resource tree (cxlds->dpa_res)
 * @perf: performance attributes of the partition from CDAT
 * @mode: operation mode for the DPA capacity, e.g. ram, pmem, dynamic...
 */
struct cxl_dpa_partition {
	struct resource res;
	struct cxl_dpa_perf perf;
	enum cxl_partition_mode mode;
};

#define CXL_NR_PARTITIONS_MAX 2

/**
 * struct cxl_dev_state - The driver device state
 *
 * cxl_dev_state represents the CXL driver/device state.  It provides an
 * interface to mailbox commands as well as some cached data about the device.
 * Currently only memory devices are represented.
 *
 * @dev: The device associated with this CXL state
 * @cxlmd: The device representing the CXL.mem capabilities of @dev
 * @reg_map: component and ras register mapping parameters
 * @regs: Parsed register blocks
 * @cxl_dvsec: Offset to the PCIe device DVSEC
 * @rcd: operating in RCD mode (CXL 3.0 9.11.8 CXL Devices Attached to an RCH)
 * @media_ready: Indicate whether the device media is usable
 * @dpa_res: Overall DPA resource tree for the device
 * @part: DPA partition array
 * @nr_partitions: Number of DPA partitions
 * @serial: PCIe Device Serial Number
 * @type: Generic Memory Class device or Vendor Specific Memory device
 * @cxl_mbox: CXL mailbox context
 * @cxlfs: CXL features context
 */
struct cxl_dev_state {
	/* public for Type2 drivers */
	struct device *dev;
	struct cxl_memdev *cxlmd;

	/* private for Type2 drivers */
	struct cxl_register_map reg_map;
	struct cxl_device_regs regs;
	int cxl_dvsec;
	bool rcd;
	bool media_ready;
	struct resource dpa_res;
	struct cxl_dpa_partition part[CXL_NR_PARTITIONS_MAX];
	unsigned int nr_partitions;
	u64 serial;
	enum cxl_devtype type;
	struct cxl_mailbox cxl_mbox;
#ifdef CONFIG_CXL_FEATURES
	struct cxl_features_state *cxlfs;
#endif
};

struct cxl_dev_state *_devm_cxl_dev_state_create(struct device *dev,
						 enum cxl_devtype type,
						 u64 serial, u16 dvsec,
						 size_t size, bool has_mbox);

/**
 * cxl_dev_state_create - safely create and cast a cxl dev state embedded in a
 * driver specific struct.
 *
 * @parent: device behind the request
 * @type: CXL device type
 * @serial: device identification
 * @dvsec: dvsec capability offset
 * @drv_struct: driver struct embedding a cxl_dev_state struct
 * @member: name of the struct cxl_dev_state member in drv_struct
 * @mbox: true if mailbox supported
 *
 * Returns a pointer to the drv_struct allocated and embedding a cxl_dev_state
 * struct initialized.
 *
 * Introduced for Type2 driver support.
 */
#define devm_cxl_dev_state_create(parent, type, serial, dvsec, drv_struct, member, mbox)	\
	({										\
		static_assert(__same_type(struct cxl_dev_state,				\
			      ((drv_struct *)NULL)->member));				\
		static_assert(offsetof(drv_struct, member) == 0);			\
		(drv_struct *)_devm_cxl_dev_state_create(parent, type, serial, dvsec,	\
						      sizeof(drv_struct), mbox);	\
	})
#endif /* __CXL_CXL_H__ */
