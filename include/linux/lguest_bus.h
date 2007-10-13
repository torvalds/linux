#ifndef _ASM_LGUEST_DEVICE_H
#define _ASM_LGUEST_DEVICE_H
/* Everything you need to know about lguest devices. */
#include <linux/device.h>
#include <linux/lguest.h>
#include <linux/lguest_launcher.h>

struct lguest_device {
	/* Unique busid, and index into lguest_page->devices[] */
	unsigned int index;

	struct device dev;

	/* Driver can hang data off here. */
	void *private;
};

/*D:380 Since interrupt numbers are arbitrary, we use a convention: each device
 * can use the interrupt number corresponding to its index.  The +1 is because
 * interrupt 0 is not usable (it's actually the timer interrupt). */
static inline int lgdev_irq(const struct lguest_device *dev)
{
	return dev->index + 1;
}
/*:*/

/* dma args must not be vmalloced! */
void lguest_send_dma(unsigned long key, struct lguest_dma *dma);
int lguest_bind_dma(unsigned long key, struct lguest_dma *dmas,
		    unsigned int num, u8 irq);
void lguest_unbind_dma(unsigned long key, struct lguest_dma *dmas);

/* Map the virtual device space */
void *lguest_map(unsigned long phys_addr, unsigned long pages);
void lguest_unmap(void *);

struct lguest_driver {
	const char *name;
	struct module *owner;
	u16 device_type;
	int (*probe)(struct lguest_device *dev);
	void (*remove)(struct lguest_device *dev);

	struct device_driver drv;
};

extern int register_lguest_driver(struct lguest_driver *drv);
extern void unregister_lguest_driver(struct lguest_driver *drv);

extern struct lguest_device_desc *lguest_devices; /* Just past max_pfn */
#endif /* _ASM_LGUEST_DEVICE_H */
