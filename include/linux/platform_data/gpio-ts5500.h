/*
 * GPIO (DIO) header for Technologic Systems TS-5500
 *
 * Copyright (c) 2012 Savoir-faire Linux Inc.
 *	Vivien Didelot <vivien.didelot@savoirfairelinux.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _PDATA_GPIO_TS5500_H
#define _PDATA_GPIO_TS5500_H

/**
 * struct ts5500_dio_platform_data - TS-5500 pin block configuration
 * @base:	The GPIO base number to use.
 * @strap:	The only pin connected to an interrupt in a block is input-only.
 *		If you need a bidirectional line which can trigger an IRQ, you
 *		may strap it with an in/out pin. This flag indicates this case.
 */
struct ts5500_dio_platform_data {
	int base;
	bool strap;
};

#endif /* _PDATA_GPIO_TS5500_H */
