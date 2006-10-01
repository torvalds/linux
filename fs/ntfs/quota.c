/*
 * quota.c - NTFS kernel quota ($Quota) handling.  Part of the Linux-NTFS
 *	     project.
 *
 * Copyright (c) 2004 Anton Altaparmakov
 *
 * This program/include file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program/include file is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the Linux-NTFS
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation,Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef NTFS_RW

#include "index.h"
#include "quota.h"
#include "debug.h"
#include "ntfs.h"

/**
 * ntfs_mark_quotas_out_of_date - mark the quotas out of date on an ntfs volume
 * @vol:	ntfs volume on which to mark the quotas out of date
 *
 * Mark the quotas out of date on the ntfs volume @vol and return 'true' on
 * success and 'false' on error.
 */
bool ntfs_mark_quotas_out_of_date(ntfs_volume *vol)
{
	ntfs_index_context *ictx;
	QUOTA_CONTROL_ENTRY *qce;
	const le32 qid = QUOTA_DEFAULTS_ID;
	int err;

	ntfs_debug("Entering.");
	if (NVolQuotaOutOfDate(vol))
		goto done;
	if (!vol->quota_ino || !vol->quota_q_ino) {
		ntfs_error(vol->sb, "Quota inodes are not open.");
		return false;
	}
	mutex_lock(&vol->quota_q_ino->i_mutex);
	ictx = ntfs_index_ctx_get(NTFS_I(vol->quota_q_ino));
	if (!ictx) {
		ntfs_error(vol->sb, "Failed to get index context.");
		goto err_out;
	}
	err = ntfs_index_lookup(&qid, sizeof(qid), ictx);
	if (err) {
		if (err == -ENOENT)
			ntfs_error(vol->sb, "Quota defaults entry is not "
					"present.");
		else
			ntfs_error(vol->sb, "Lookup of quota defaults entry "
					"failed.");
		goto err_out;
	}
	if (ictx->data_len < offsetof(QUOTA_CONTROL_ENTRY, sid)) {
		ntfs_error(vol->sb, "Quota defaults entry size is invalid.  "
				"Run chkdsk.");
		goto err_out;
	}
	qce = (QUOTA_CONTROL_ENTRY*)ictx->data;
	if (le32_to_cpu(qce->version) != QUOTA_VERSION) {
		ntfs_error(vol->sb, "Quota defaults entry version 0x%x is not "
				"supported.", le32_to_cpu(qce->version));
		goto err_out;
	}
	ntfs_debug("Quota defaults flags = 0x%x.", le32_to_cpu(qce->flags));
	/* If quotas are already marked out of date, no need to do anything. */
	if (qce->flags & QUOTA_FLAG_OUT_OF_DATE)
		goto set_done;
	/*
	 * If quota tracking is neither requested, nor enabled and there are no
	 * pending deletes, no need to mark the quotas out of date.
	 */
	if (!(qce->flags & (QUOTA_FLAG_TRACKING_ENABLED |
			QUOTA_FLAG_TRACKING_REQUESTED |
			QUOTA_FLAG_PENDING_DELETES)))
		goto set_done;
	/*
	 * Set the QUOTA_FLAG_OUT_OF_DATE bit thus marking quotas out of date.
	 * This is verified on WinXP to be sufficient to cause windows to
	 * rescan the volume on boot and update all quota entries.
	 */
	qce->flags |= QUOTA_FLAG_OUT_OF_DATE;
	/* Ensure the modified flags are written to disk. */
	ntfs_index_entry_flush_dcache_page(ictx);
	ntfs_index_entry_mark_dirty(ictx);
set_done:
	ntfs_index_ctx_put(ictx);
	mutex_unlock(&vol->quota_q_ino->i_mutex);
	/*
	 * We set the flag so we do not try to mark the quotas out of date
	 * again on remount.
	 */
	NVolSetQuotaOutOfDate(vol);
done:
	ntfs_debug("Done.");
	return true;
err_out:
	if (ictx)
		ntfs_index_ctx_put(ictx);
	mutex_unlock(&vol->quota_q_ino->i_mutex);
	return false;
}

#endif /* NTFS_RW */
