#ifndef _IPT_CONNMARK_H
#define _IPT_CONNMARK_H

/* Copyright (C) 2002,2004 MARA Systems AB <http://www.marasystems.com>
 * by Henrik Nordstrom <hno@marasystems.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

struct ipt_connmark_info {
	unsigned long mark, mask;
	u_int8_t invert;
};

#endif /*_IPT_CONNMARK_H*/
