#ifndef MT9P031_H
#define MT9P031_H

struct v4l2_subdev;

/*
 * struct mt9p031_platform_data - MT9P031 platform data
 * @set_xclk: Clock frequency set callback
 * @reset: Chip reset GPIO (set to -1 if not used)
 * @ext_freq: Input clock frequency
 * @target_freq: Pixel clock frequency
 */
struct mt9p031_platform_data {
	int (*set_xclk)(struct v4l2_subdev *subdev, int hz);
	int reset;
	int ext_freq;
	int target_freq;
};

#endif
