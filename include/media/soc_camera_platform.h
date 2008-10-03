#ifndef __SOC_CAMERA_H__
#define __SOC_CAMERA_H__

#include <linux/videodev2.h>

struct soc_camera_platform_info {
	int iface;
	char *format_name;
	unsigned long format_depth;
	struct v4l2_pix_format format;
	unsigned long bus_param;
	int (*set_capture)(struct soc_camera_platform_info *info, int enable);
};

#endif /* __SOC_CAMERA_H__ */
