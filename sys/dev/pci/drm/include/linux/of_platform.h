/* Public domain. */

#ifndef _LINUX_OF_PLATFORM_H
#define _LINUX_OF_PLATFORM_H

struct platform_device *of_platform_device_create(struct device_node *,
	    const char *, struct device *);
struct platform_device *of_find_device_by_node(struct device_node *);

#define of_platform_device_destroy(a, b)

#endif
