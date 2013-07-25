/*
 * Copyright Altera Corporation (C) 2013. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 */
#ifndef _ALTERA_MUTEX_H
#define _ALTERA_MUTEX_H

#include <linux/device.h>
#include <linux/platform_device.h>

struct altera_mutex {
	struct list_head	list;
	struct platform_device	*pdev;
	struct mutex		lock;
	void __iomem		*regs;
	bool			requested;
};

extern struct altera_mutex *altera_mutex_request(struct device_node *mutex_np);
extern int altera_mutex_free(struct altera_mutex *mutex);

extern int altera_mutex_lock(struct altera_mutex *mutex, u16 owner, u16 value);

extern int altera_mutex_trylock(struct altera_mutex *mutex, u16 owner,
	u16 value);
extern int altera_mutex_unlock(struct altera_mutex *mutex, u16 owner);
extern int altera_mutex_owned(struct altera_mutex *mutex, u16 owner);
extern int altera_mutex_is_locked(struct altera_mutex *mutex);

#endif /* _ALTERA_MUTEX_H */
