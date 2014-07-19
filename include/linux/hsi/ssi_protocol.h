/*
 * ssip_slave.h
 *
 * SSIP slave support header file
 *
 * Copyright (C) 2010 Nokia Corporation. All rights reserved.
 *
 * Contact: Carlos Chinea <carlos.chinea@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#ifndef __LINUX_SSIP_SLAVE_H__
#define __LINUX_SSIP_SLAVE_H__

#include <linux/hsi/hsi.h>

static inline void ssip_slave_put_master(struct hsi_client *master)
{
}

struct hsi_client *ssip_slave_get_master(struct hsi_client *slave);
int ssip_slave_start_tx(struct hsi_client *master);
int ssip_slave_stop_tx(struct hsi_client *master);
void ssip_reset_event(struct hsi_client *master);

int ssip_slave_running(struct hsi_client *master);

#endif /* __LINUX_SSIP_SLAVE_H__ */

