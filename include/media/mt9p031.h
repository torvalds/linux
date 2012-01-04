#ifndef MT9P031_H
#define MT9P031_H

struct v4l2_subdev;

enum {
	MT9P031_COLOR_VERSION,
	MT9P031_MONOCHROME_VERSION,
};

struct mt9p031_platform_data {
	int (*set_xclk)(struct v4l2_subdev *subdev, int hz);
	int (*reset)(struct v4l2_subdev *subdev, int active);
	int ext_freq; /* input frequency to the mt9p031 for PLL dividers */
	int target_freq; /* frequency target for the PLL */
	int version; /* MT9P031_COLOR_VERSION or MT9P031_MONOCHROME_VERSION */
};

#endif
