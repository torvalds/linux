/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright (C) 2012 Paul Parsons <lost.distance@yahoo.com>
 */

struct navpoint_platform_data {
	int		port;		/* PXA SSP port for pxa_ssp_request() */
	int		gpio;		/* GPIO for power on/off */
};
