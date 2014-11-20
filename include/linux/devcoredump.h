#ifndef __DEVCOREDUMP_H
#define __DEVCOREDUMP_H

#include <linux/device.h>
#include <linux/module.h>
#include <linux/vmalloc.h>

#ifdef CONFIG_DEV_COREDUMP
void dev_coredumpv(struct device *dev, const void *data, size_t datalen,
		   gfp_t gfp);

void dev_coredumpm(struct device *dev, struct module *owner,
		   const void *data, size_t datalen, gfp_t gfp,
		   ssize_t (*read)(char *buffer, loff_t offset, size_t count,
				   const void *data, size_t datalen),
		   void (*free)(const void *data));
#else
static inline void dev_coredumpv(struct device *dev, const void *data,
				 size_t datalen, gfp_t gfp)
{
	vfree(data);
}

static inline void
dev_coredumpm(struct device *dev, struct module *owner,
	      const void *data, size_t datalen, gfp_t gfp,
	      ssize_t (*read)(char *buffer, loff_t offset, size_t count,
			      const void *data, size_t datalen),
	      void (*free)(const void *data))
{
	free(data);
}
#endif /* CONFIG_DEV_COREDUMP */

#endif /* __DEVCOREDUMP_H */
