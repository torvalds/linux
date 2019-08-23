/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * l4f00242t03.h -- Platform glue for Epson L4F00242T03 LCD
 *
 * Copyright (c) 2009 Alberto Panizzo <maramaopercheseimorto@gmail.com>
 * Based on Marek Vasut work in lms283gf05.h
*/

#ifndef _INCLUDE_LINUX_SPI_L4F00242T03_H_
#define _INCLUDE_LINUX_SPI_L4F00242T03_H_

struct l4f00242t03_pdata {
	unsigned int	reset_gpio;
	unsigned int	data_enable_gpio;
};

#endif /* _INCLUDE_LINUX_SPI_L4F00242T03_H_ */
