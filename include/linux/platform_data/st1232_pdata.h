/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_ST1232_PDATA_H
#define _LINUX_ST1232_PDATA_H

/*
 * Optional platform data
 *
 * Use this if you want the driver to drive the reset pin.
 */
struct st1232_pdata {
	int reset_gpio;
};

#endif
