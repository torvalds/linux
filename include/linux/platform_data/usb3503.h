#ifndef __USB3503_H__
#define __USB3503_H__

#define USB3503_I2C_NAME	"usb3503"

enum usb3503_mode {
	USB3503_MODE_UNKNOWN,
	USB3503_MODE_HUB,
	USB3503_MODE_STANDBY,
};

enum usb3503_ref_clk {
	USB3503_REFCLK_24M,
	USB3503_REFCLK_26M,
};

struct usb3503_platform_data {
	enum usb3503_mode		initial_mode;
	enum usb3503_ref_clk	ref_clk;
	int	gpio_intn;
	int	gpio_connect;
	int	gpio_reset;
};

#endif
