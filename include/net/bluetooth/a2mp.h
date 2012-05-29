/*
   Copyright (c) 2010,2011 Code Aurora Forum.  All rights reserved.
   Copyright (c) 2011,2012 Intel Corp.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 and
   only version 2 as published by the Free Software Foundation.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
*/

#ifndef __A2MP_H
#define __A2MP_H

struct amp_mgr {
	struct l2cap_conn	*l2cap_conn;
	struct l2cap_chan	*a2mp_chan;
	struct kref		kref;
	__u8			ident;
	__u8			handle;
	unsigned long		flags;
};

void amp_mgr_get(struct amp_mgr *mgr);
int amp_mgr_put(struct amp_mgr *mgr);

#endif /* __A2MP_H */
