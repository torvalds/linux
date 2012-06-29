/*
 * Platform data for the chipidea USB dual role controller
 */

#ifndef __LINUX_USB_CHIPIDEA_H
#define __LINUX_USB_CHIPIDEA_H

struct ci13xxx;
struct ci13xxx_platform_data {
	const char	*name;
	/* offset of the capability registers */
	uintptr_t	 capoffset;
	unsigned	 power_budget;
	unsigned long	 flags;
#define CI13XXX_REGS_SHARED		BIT(0)
#define CI13XXX_REQUIRE_TRANSCEIVER	BIT(1)
#define CI13XXX_PULLUP_ON_VBUS		BIT(2)
#define CI13XXX_DISABLE_STREAMING	BIT(3)

#define CI13XXX_CONTROLLER_RESET_EVENT		0
#define CI13XXX_CONTROLLER_STOPPED_EVENT	1
	void	(*notify_event) (struct ci13xxx *udc, unsigned event);
};

/* Default offset of capability registers */
#define DEF_CAPOFFSET		0x100

#endif
