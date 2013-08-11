/* include/linux/uid_stat.h
 *
 * Copyright (C) 2008-2009 Google, Inc.
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
 */

#ifndef __uid_stat_h
#define __uid_stat_h

/* Contains definitions for resource tracking per uid. */

#ifdef CONFIG_UID_STAT
int uid_stat_tcp_snd(uid_t uid, int size);
int uid_stat_tcp_rcv(uid_t uid, int size);
#else
#define uid_stat_tcp_snd(uid, size) do {} while (0);
#define uid_stat_tcp_rcv(uid, size) do {} while (0);
#endif

#endif /* _LINUX_UID_STAT_H */
