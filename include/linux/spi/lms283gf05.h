/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * lms283gf05.h - Platform glue for Samsung LMS283GF05 LCD
 *
 * Copyright (C) 2009 Marek Vasut <marek.vasut@gmail.com>
*/

#ifndef _INCLUDE_LINUX_SPI_LMS283GF05_H_
#define _INCLUDE_LINUX_SPI_LMS283GF05_H_

struct lms283gf05_pdata {
	unsigned long	reset_gpio;
	bool		reset_inverted;
};

#endif /* _INCLUDE_LINUX_SPI_LMS283GF05_H_ */
