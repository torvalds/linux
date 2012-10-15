/*
 * MAX517 DAC driver
 *
 * Copyright 2011 Roland Stigge <stigge@antcom.de>
 *
 * Licensed under the GPL-2 or later.
 */
#ifndef IIO_DAC_MAX517_H_
#define IIO_DAC_MAX517_H_

struct max517_platform_data {
	u16				vref_mv[2];
};

#endif /* IIO_DAC_MAX517_H_ */
