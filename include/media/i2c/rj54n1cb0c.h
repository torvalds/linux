/*
 * RJ54N1CB0C Private data
 *
 * Copyright (C) 2009, Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __RJ54N1CB0C_H__
#define __RJ54N1CB0C_H__

struct rj54n1_pdata {
	unsigned int	mclk_freq;
	bool		ioctl_high;
};

#endif
