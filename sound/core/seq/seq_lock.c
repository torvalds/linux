/*
 *  Do sleep inside a spin-lock
 *  Copyright (c) 1999 by Takashi Iwai <tiwai@suse.de>
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <sound/driver.h>
#include <sound/core.h>
#include "seq_lock.h"

#if defined(CONFIG_SMP) || defined(CONFIG_SND_DEBUG)

/* wait until all locks are released */
void snd_use_lock_sync_helper(snd_use_lock_t *lockp, const char *file, int line)
{
	int max_count = 5 * HZ;

	if (atomic_read(lockp) < 0) {
		printk(KERN_WARNING "seq_lock: lock trouble [counter = %d] in %s:%d\n", atomic_read(lockp), file, line);
		return;
	}
	while (atomic_read(lockp) > 0) {
		if (max_count == 0) {
			snd_printk(KERN_WARNING "seq_lock: timeout [%d left] in %s:%d\n", atomic_read(lockp), file, line);
			break;
		}
		schedule_timeout_uninterruptible(1);
		max_count--;
	}
}

EXPORT_SYMBOL(snd_use_lock_sync_helper);

#endif
