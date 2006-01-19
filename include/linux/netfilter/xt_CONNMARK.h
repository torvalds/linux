#ifndef _XT_CONNMARK_H_target
#define _XT_CONNMARK_H_target

/* Copyright (C) 2002,2004 MARA Systems AB <http://www.marasystems.com>
 * by Henrik Nordstrom <hno@marasystems.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

enum {
	XT_CONNMARK_SET = 0,
	XT_CONNMARK_SAVE,
	XT_CONNMARK_RESTORE
};

struct xt_connmark_target_info {
	unsigned long mark;
	unsigned long mask;
	u_int8_t mode;
};

#endif /*_XT_CONNMARK_H_target*/
