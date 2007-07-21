#ifndef _ASM_FB_H_
#define _ASM_FB_H_
#include <linux/fb.h>
#include <asm/prom.h>

#define fb_pgprotect(...) do {} while (0)

static inline int fb_is_primary_device(struct fb_info *info)
{
	struct device *dev = info->device;
	struct device_node *node;

	node = dev->archdata.prom_node;
	if (node &&
	    node == of_console_device)
		return 1;

	return 0;
}

#endif /* _ASM_FB_H_ */
