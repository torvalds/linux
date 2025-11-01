/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _INTEL_VSEC_H
#define _INTEL_VSEC_H

#include <linux/auxiliary_bus.h>
#include <linux/bits.h>
#include <linux/err.h>
#include <linux/intel_pmt_features.h>

/*
 * VSEC_CAP_UNUSED is reserved. It exists to prevent zero initialized
 * intel_vsec devices from being automatically set to a known
 * capability with ID 0
 */
#define VSEC_CAP_UNUSED		BIT(0)
#define VSEC_CAP_TELEMETRY	BIT(1)
#define VSEC_CAP_WATCHER	BIT(2)
#define VSEC_CAP_CRASHLOG	BIT(3)
#define VSEC_CAP_SDSI		BIT(4)
#define VSEC_CAP_TPMI		BIT(5)
#define VSEC_CAP_DISCOVERY	BIT(6)
#define VSEC_FEATURE_COUNT	7

/* Intel DVSEC offsets */
#define INTEL_DVSEC_ENTRIES		0xA
#define INTEL_DVSEC_SIZE		0xB
#define INTEL_DVSEC_TABLE		0xC
#define INTEL_DVSEC_TABLE_BAR(x)	((x) & GENMASK(2, 0))
#define INTEL_DVSEC_TABLE_OFFSET(x)	((x) & GENMASK(31, 3))
#define TABLE_OFFSET_SHIFT		3

struct pci_dev;
struct resource;

enum intel_vsec_id {
	VSEC_ID_TELEMETRY	= 2,
	VSEC_ID_WATCHER		= 3,
	VSEC_ID_CRASHLOG	= 4,
	VSEC_ID_DISCOVERY	= 12,
	VSEC_ID_SDSI		= 65,
	VSEC_ID_TPMI		= 66,
};

/**
 * struct intel_vsec_header - Common fields of Intel VSEC and DVSEC registers.
 * @rev:         Revision ID of the VSEC/DVSEC register space
 * @length:      Length of the VSEC/DVSEC register space
 * @id:          ID of the feature
 * @num_entries: Number of instances of the feature
 * @entry_size:  Size of the discovery table for each feature
 * @tbir:        BAR containing the discovery tables
 * @offset:      BAR offset of start of the first discovery table
 */
struct intel_vsec_header {
	u8	rev;
	u16	length;
	u16	id;
	u8	num_entries;
	u8	entry_size;
	u8	tbir;
	u32	offset;
};

enum intel_vsec_quirks {
	/* Watcher feature not supported */
	VSEC_QUIRK_NO_WATCHER	= BIT(0),

	/* Crashlog feature not supported */
	VSEC_QUIRK_NO_CRASHLOG	= BIT(1),

	/* Use shift instead of mask to read discovery table offset */
	VSEC_QUIRK_TABLE_SHIFT	= BIT(2),

	/* DVSEC not present (provided in driver data) */
	VSEC_QUIRK_NO_DVSEC	= BIT(3),

	/* Platforms requiring quirk in the auxiliary driver */
	VSEC_QUIRK_EARLY_HW     = BIT(4),
};

/**
 * struct pmt_callbacks - Callback infrastructure for PMT devices
 * ->read_telem() when specified, called by client driver to access PMT data (instead
 * of direct copy).
 * @pdev:  PCI device reference for the callback's use
 * @guid:  ID of data to acccss
 * @data:  buffer for the data to be copied
 * @off:   offset into the requested buffer
 * @count: size of buffer
 */
struct pmt_callbacks {
	int (*read_telem)(struct pci_dev *pdev, u32 guid, u64 *data, loff_t off, u32 count);
};

struct vsec_feature_dependency {
	unsigned long feature;
	unsigned long supplier_bitmap;
};

/**
 * struct intel_vsec_platform_info - Platform specific data
 * @parent:    parent device in the auxbus chain
 * @headers:   list of headers to define the PMT client devices to create
 * @deps:      array of feature dependencies
 * @priv_data: private data, usable by parent devices, currently a callback
 * @caps:      bitmask of PMT capabilities for the given headers
 * @quirks:    bitmask of VSEC device quirks
 * @base_addr: allow a base address to be specified (rather than derived)
 * @num_deps:  Count feature dependencies
 */
struct intel_vsec_platform_info {
	struct device *parent;
	struct intel_vsec_header **headers;
	const struct vsec_feature_dependency *deps;
	void *priv_data;
	unsigned long caps;
	unsigned long quirks;
	u64 base_addr;
	int num_deps;
};

/**
 * struct intel_sec_device - Auxbus specific device information
 * @auxdev:        auxbus device struct for auxbus access
 * @pcidev:        pci device associated with the device
 * @resource:      any resources shared by the parent
 * @ida:           id reference
 * @num_resources: number of resources
 * @id:            xarray id
 * @priv_data:     any private data needed
 * @quirks:        specified quirks
 * @base_addr:     base address of entries (if specified)
 * @cap_id:        the enumerated id of the vsec feature
 */
struct intel_vsec_device {
	struct auxiliary_device auxdev;
	struct pci_dev *pcidev;
	struct resource *resource;
	struct ida *ida;
	int num_resources;
	int id; /* xa */
	void *priv_data;
	size_t priv_data_size;
	unsigned long quirks;
	u64 base_addr;
	unsigned long cap_id;
};

/**
 * struct oobmsm_plat_info - Platform information for a device instance
 * @cdie_mask:       Mask of all compute dies in the partition
 * @package_id:      CPU Package id
 * @partition:       Package partition id when multiple VSEC PCI devices per package
 * @segment:         PCI segment ID
 * @bus_number:      PCI bus number
 * @device_number:   PCI device number
 * @function_number: PCI function number
 *
 * Structure to store platform data for a OOBMSM device instance.
 */
struct oobmsm_plat_info {
	u16 cdie_mask;
	u8 package_id;
	u8 partition;
	u8 segment;
	u8 bus_number;
	u8 device_number;
	u8 function_number;
};

struct telemetry_region {
	struct oobmsm_plat_info	plat_info;
	void __iomem		*addr;
	size_t			size;
	u32			guid;
	u32			num_rmids;
};

struct pmt_feature_group {
	enum pmt_feature_id	id;
	int			count;
	struct kref		kref;
	struct telemetry_region	regions[];
};

int intel_vsec_add_aux(struct pci_dev *pdev, struct device *parent,
		       struct intel_vsec_device *intel_vsec_dev,
		       const char *name);

static inline struct intel_vsec_device *dev_to_ivdev(struct device *dev)
{
	return container_of(dev, struct intel_vsec_device, auxdev.dev);
}

static inline struct intel_vsec_device *auxdev_to_ivdev(struct auxiliary_device *auxdev)
{
	return container_of(auxdev, struct intel_vsec_device, auxdev);
}

#if IS_ENABLED(CONFIG_INTEL_VSEC)
int intel_vsec_register(struct pci_dev *pdev,
			 struct intel_vsec_platform_info *info);
int intel_vsec_set_mapping(struct oobmsm_plat_info *plat_info,
			   struct intel_vsec_device *vsec_dev);
struct oobmsm_plat_info *intel_vsec_get_mapping(struct pci_dev *pdev);
#else
static inline int intel_vsec_register(struct pci_dev *pdev,
				       struct intel_vsec_platform_info *info)
{
	return -ENODEV;
}
static inline int intel_vsec_set_mapping(struct oobmsm_plat_info *plat_info,
					 struct intel_vsec_device *vsec_dev)
{
	return -ENODEV;
}
static inline struct oobmsm_plat_info *intel_vsec_get_mapping(struct pci_dev *pdev)
{
	return ERR_PTR(-ENODEV);
}
#endif

#if IS_ENABLED(CONFIG_INTEL_PMT_TELEMETRY)
struct pmt_feature_group *
intel_pmt_get_regions_by_feature(enum pmt_feature_id id);

void intel_pmt_put_feature_group(struct pmt_feature_group *feature_group);
#else
static inline struct pmt_feature_group *
intel_pmt_get_regions_by_feature(enum pmt_feature_id id)
{
	return ERR_PTR(-ENODEV);
}

static inline void
intel_pmt_put_feature_group(struct pmt_feature_group *feature_group) {}
#endif

#endif
