/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _FPGA_REGION_H
#define _FPGA_REGION_H

#include <linux/device.h>
#include <linux/fpga/fpga-mgr.h>
#include <linux/fpga/fpga-bridge.h>

/**
 * struct fpga_region - FPGA Region structure
 * @dev: FPGA Region device
 * @mutex: enforces exclusive reference to region
 * @bridge_list: list of FPGA bridges specified in region
 * @mgr: FPGA manager
 * @info: FPGA image info
 * @compat_id: FPGA region id for compatibility check.
 * @priv: private data
 * @get_bridges: optional function to get bridges to a list
 */
struct fpga_region {
	struct device dev;
	struct mutex mutex; /* for exclusive reference to region */
	struct list_head bridge_list;
	struct fpga_manager *mgr;
	struct fpga_image_info *info;
	struct fpga_compat_id *compat_id;
	void *priv;
	int (*get_bridges)(struct fpga_region *region);
};

#define to_fpga_region(d) container_of(d, struct fpga_region, dev)

struct fpga_region *fpga_region_class_find(
	struct device *start, const void *data,
	int (*match)(struct device *, const void *));

int fpga_region_program_fpga(struct fpga_region *region);

struct fpga_region
*fpga_region_create(struct device *dev, struct fpga_manager *mgr,
		    int (*get_bridges)(struct fpga_region *));
void fpga_region_free(struct fpga_region *region);
int fpga_region_register(struct fpga_region *region);
void fpga_region_unregister(struct fpga_region *region);

struct fpga_region
*devm_fpga_region_create(struct device *dev, struct fpga_manager *mgr,
			int (*get_bridges)(struct fpga_region *));

#endif /* _FPGA_REGION_H */
