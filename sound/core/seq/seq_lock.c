// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Do sleep inside a spin-lock
 *  Copyright (c) 1999 by Takashi Iwai <tiwai@suse.de>
 */

#include <linux/export.h>
#include <sound/core.h>
#include "seq_lock.h"

/* wait until all locks are released */
void snd_use_lock_sync_helper(snd_use_lock_t *lockp, const char *file, int line)
{
	int warn_count = 5 * HZ;

	if (atomic_read(lockp) < 0) {
		pr_warn("ALSA: seq_lock: lock trouble [counter = %d] in %s:%d\n", atomic_read(lockp), file, line);
		return;
	}
	while (atomic_read(lockp) > 0) {
		if (warn_count-- == 0)
			pr_warn("ALSA: seq_lock: waiting [%d left] in %s:%d\n", atomic_read(lockp), file, line);
		schedule_timeout_uninterruptible(1);
	}
}
EXPORT_SYMBOL(snd_use_lock_sync_helper);
