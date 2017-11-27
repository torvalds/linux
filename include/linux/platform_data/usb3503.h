/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __USB3503_H__
#define __USB3503_H__

#define USB3503_I2C_NAME	"usb3503"

#define USB3503_OFF_PORT1	(1 << 1)
#define USB3503_OFF_PORT2	(1 << 2)
#define USB3503_OFF_PORT3	(1 << 3)

enum usb3503_mode {
	USB3503_MODE_UNKNOWN,
	USB3503_MODE_HUB,
	USB3503_MODE_STANDBY,
};

struct usb3503_platform_data {
	enum usb3503_mode	initial_mode;
	u8	port_off_mask;
	int	gpio_intn;
	int	gpio_connect;
	int	gpio_reset;
};

#endif
