#ifndef __LINUX_USB_NOP_XCEIV_H
#define __LINUX_USB_NOP_XCEIV_H

#include <linux/usb/otg.h>
#include <linux/gpio/consumer.h>

struct usb_phy_generic_platform_data {
	enum usb_phy_type type;
	unsigned long clk_rate;

	/* if set fails with -EPROBE_DEFER if can't get regulator */
	unsigned int needs_vcc:1;
	unsigned int needs_reset:1;	/* deprecated */
	int gpio_reset;
	struct gpio_desc *gpiod_vbus;
};

#if IS_ENABLED(CONFIG_NOP_USB_XCEIV)
/* sometimes transceivers are accessed only through e.g. ULPI */
extern struct platform_device *usb_phy_generic_register(void);
extern void usb_phy_generic_unregister(struct platform_device *);
#else
static inline struct platform_device *usb_phy_generic_register(void)
{
	return NULL;
}

static inline void usb_phy_generic_unregister(struct platform_device *pdev)
{
}
#endif

#endif /* __LINUX_USB_NOP_XCEIV_H */
