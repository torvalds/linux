/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * S3C24XX USB 2.0 High-speed USB controller gadget driver
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * The S3C24XX USB 2.0 high-speed USB controller supports upto 9 endpoints.
 * Each endpoint can be configured as either in or out endpoint. Endpoints
 * can be configured for Bulk or Interrupt transfer mode.
*/

#ifndef __LINUX_USB_S3C_HSUDC_H
#define __LINUX_USB_S3C_HSUDC_H

/**
 * s3c24xx_hsudc_platdata - Platform data for USB High-Speed gadget controller.
 * @epnum: Number of endpoints to be instantiated by the controller driver.
 * @gpio_init: Platform specific USB related GPIO initialization.
 * @gpio_uninit: Platform specific USB releted GPIO uninitialzation.
 *
 * Representation of platform data for the S3C24XX USB 2.0 High Speed gadget
 * controllers.
 */
struct s3c24xx_hsudc_platdata {
	unsigned int	epnum;
	void		(*gpio_init)(void);
	void		(*gpio_uninit)(void);
	void		(*phy_init)(void);
	void		(*phy_uninit)(void);
};

#endif	/* __LINUX_USB_S3C_HSUDC_H */
