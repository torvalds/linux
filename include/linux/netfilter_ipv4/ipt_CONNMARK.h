#ifndef _IPT_CONNMARK_H_target
#define _IPT_CONNMARK_H_target

/* Copyright (C) 2002,2004 MARA Systems AB <http://www.marasystems.com>
 * by Henrik Nordstrom <hno@marasystems.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

enum {
	IPT_CONNMARK_SET = 0,
	IPT_CONNMARK_SAVE,
	IPT_CONNMARK_RESTORE
};

struct ipt_connmark_target_info {
	unsigned long mark;
	unsigned long mask;
	u_int8_t mode;
};

#endif /*_IPT_CONNMARK_H_target*/
