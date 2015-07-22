/*
 * Platform data for the chipidea USB dual role controller
 */

#ifndef __LINUX_USB_CHIPIDEA_H
#define __LINUX_USB_CHIPIDEA_H

#include <linux/usb/otg.h>

struct ci_hdrc;
struct ci_hdrc_platform_data {
	const char	*name;
	/* offset of the capability registers */
	uintptr_t	 capoffset;
	unsigned	 power_budget;
	struct phy	*phy;
	/* old usb_phy interface */
	struct usb_phy	*usb_phy;
	enum usb_phy_interface phy_mode;
	unsigned long	 flags;
#define CI_HDRC_REGS_SHARED		BIT(0)
#define CI_HDRC_DISABLE_DEVICE_STREAMING	BIT(1)
#define CI_HDRC_SUPPORTS_RUNTIME_PM	BIT(2)
#define CI_HDRC_DISABLE_HOST_STREAMING	BIT(3)
#define CI_HDRC_DISABLE_STREAMING (CI_HDRC_DISABLE_DEVICE_STREAMING |	\
		CI_HDRC_DISABLE_HOST_STREAMING)
	/*
	 * Only set it when DCCPARAMS.DC==1 and DCCPARAMS.HC==1,
	 * but otg is not supported (no register otgsc).
	 */
#define CI_HDRC_DUAL_ROLE_NOT_OTG	BIT(4)
#define CI_HDRC_IMX28_WRITE_FIX		BIT(5)
#define CI_HDRC_FORCE_FULLSPEED		BIT(6)
#define CI_HDRC_TURN_VBUS_EARLY_ON	BIT(7)
#define CI_HDRC_SET_NON_ZERO_TTHA	BIT(8)
#define CI_HDRC_OVERRIDE_AHB_BURST	BIT(9)
#define CI_HDRC_OVERRIDE_TX_BURST	BIT(10)
#define CI_HDRC_OVERRIDE_RX_BURST	BIT(11)
#define CI_HDRC_IMX_EHCI_QUIRK		BIT(12)
#define CI_HDRC_IMX_IS_HSIC		BIT(13)
	enum usb_dr_mode	dr_mode;
#define CI_HDRC_CONTROLLER_RESET_EVENT		0
#define CI_HDRC_CONTROLLER_STOPPED_EVENT	1
#define CI_HDRC_CONTROLLER_VBUS_EVENT		2
#define CI_HDRC_NOTIFY_RET_DEFER_EVENT		3
#define CI_HDRC_CONTROLLER_CHARGER_POST_EVENT	4
#define CI_HDRC_IMX_HSIC_ACTIVE_EVENT		5
#define CI_HDRC_IMX_HSIC_SUSPEND_EVENT		6
#define CI_HDRC_IMX_TERM_SELECT_OVERRIDE_FS	7
#define CI_HDRC_IMX_TERM_SELECT_OVERRIDE_OFF	8
	int	(*notify_event)(struct ci_hdrc *ci, unsigned event);
	struct regulator	*reg_vbus;
	struct usb_otg_caps	ci_otg_caps;
	bool			tpl_support;
	/* interrupt threshold setting */
	u32			itc_setting;
	u32			ahb_burst_config;
	u32			tx_burst_size;
	u32			rx_burst_size;
	u32			phy_clkgate_delay_us;
};

/* Default offset of capability registers */
#define DEF_CAPOFFSET		0x100

/* Add ci hdrc device */
struct platform_device *ci_hdrc_add_device(struct device *dev,
			struct resource *res, int nres,
			struct ci_hdrc_platform_data *platdata);
/* Remove ci hdrc device */
void ci_hdrc_remove_device(struct platform_device *pdev);

/* Get current available role */
enum usb_dr_mode ci_hdrc_query_available_role(struct platform_device *pdev);
#endif
