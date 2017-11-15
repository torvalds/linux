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
 * @priv: private data
 * @get_bridges: optional function to get bridges to a list
 */
struct fpga_region {
	struct device dev;
	struct mutex mutex; /* for exclusive reference to region */
	struct list_head bridge_list;
	struct fpga_manager *mgr;
	struct fpga_image_info *info;
	void *priv;
	int (*get_bridges)(struct fpga_region *region);
};

#define to_fpga_region(d) container_of(d, struct fpga_region, dev)

int fpga_region_program_fpga(struct fpga_region *region);

int fpga_region_register(struct device *dev, struct fpga_region *region);
int fpga_region_unregister(struct fpga_region *region);

#endif /* _FPGA_REGION_H */
