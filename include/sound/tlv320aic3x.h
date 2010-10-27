/*
 * Platform data for Texas Instruments TLV320AIC3x codec
 *
 * Author: Jarkko Nikula <jhnikula@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __TLV320AIC3x_H__
#define __TLV320AIC3x_H__

struct aic3x_pdata {
	int gpio_reset; /* < 0 if not used */
};

#endif