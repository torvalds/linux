/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * MAX517 DAC driver
 *
 * Copyright 2011 Roland Stigge <stigge@antcom.de>
 */
#ifndef IIO_DAC_MAX517_H_
#define IIO_DAC_MAX517_H_

struct max517_platform_data {
	u16				vref_mv[8];
};

#endif /* IIO_DAC_MAX517_H_ */
