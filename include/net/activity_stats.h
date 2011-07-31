/*
 * Copyright (C) 2010 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Author: Mike Chan (mike@android.com)
 */

#ifndef __activity_stats_h
#define __activity_stats_h

#ifdef CONFIG_NET_ACTIVITY_STATS
void activity_stats_update(void);
#else
#define activity_stats_update(void) {}
#endif

#endif /* _NET_ACTIVITY_STATS_H */
