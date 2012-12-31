
#include <linux/device.h>
#include <media/v4l2-mediabus.h>

struct mt9m113_platform_data {
	unsigned int default_width;
	unsigned int default_height;
	unsigned int pixelformat;
	int freq;	/* MCLK in KHz */

	/* This SoC supports Parallel & CSI-2 */
	int is_mipi;
};
	
struct mt9m113_mbus_platform_data {
	int id;
	struct v4l2_mbus_framefmt fmt;
	unsigned long clk_rate; /* master clock frequency in Hz */
	int (*set_power)(int on);
	int (*set_clock)(struct device *dev, int on);
};

