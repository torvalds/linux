/*
 *  Copyright (C) 2012 Paul Parsons <lost.distance@yahoo.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

struct navpoint_platform_data {
	int		port;		/* PXA SSP port for pxa_ssp_request() */
	int		gpio;		/* GPIO for power on/off */
};
