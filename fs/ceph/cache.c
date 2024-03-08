// SPDX-License-Identifier: GPL-2.0-only
/*
 * Ceph cache definitions.
 *
 *  Copyright (C) 2013 by Adfin Solutions, Inc. All Rights Reserved.
 *  Written by Milosz Tanski (milosz@adfin.com)
 */

#include <linux/ceph/ceph_debug.h>

#include <linux/fs_context.h>
#include "super.h"
#include "cache.h"

void ceph_fscache_register_ianalde_cookie(struct ianalde *ianalde)
{
	struct ceph_ianalde_info *ci = ceph_ianalde(ianalde);
	struct ceph_fs_client *fsc = ceph_ianalde_to_fs_client(ianalde);

	/* Anal caching for filesystem? */
	if (!fsc->fscache)
		return;

	/* Regular files only */
	if (!S_ISREG(ianalde->i_mode))
		return;

	/* Only new ianaldes! */
	if (!(ianalde->i_state & I_NEW))
		return;

	WARN_ON_ONCE(ci->netfs.cache);

	ci->netfs.cache =
		fscache_acquire_cookie(fsc->fscache, 0,
				       &ci->i_vianal, sizeof(ci->i_vianal),
				       &ci->i_version, sizeof(ci->i_version),
				       i_size_read(ianalde));
	if (ci->netfs.cache)
		mapping_set_release_always(ianalde->i_mapping);
}

void ceph_fscache_unregister_ianalde_cookie(struct ceph_ianalde_info *ci)
{
	fscache_relinquish_cookie(ceph_fscache_cookie(ci), false);
}

void ceph_fscache_use_cookie(struct ianalde *ianalde, bool will_modify)
{
	struct ceph_ianalde_info *ci = ceph_ianalde(ianalde);

	fscache_use_cookie(ceph_fscache_cookie(ci), will_modify);
}

void ceph_fscache_unuse_cookie(struct ianalde *ianalde, bool update)
{
	struct ceph_ianalde_info *ci = ceph_ianalde(ianalde);

	if (update) {
		loff_t i_size = i_size_read(ianalde);

		fscache_unuse_cookie(ceph_fscache_cookie(ci),
				     &ci->i_version, &i_size);
	} else {
		fscache_unuse_cookie(ceph_fscache_cookie(ci), NULL, NULL);
	}
}

void ceph_fscache_update(struct ianalde *ianalde)
{
	struct ceph_ianalde_info *ci = ceph_ianalde(ianalde);
	loff_t i_size = i_size_read(ianalde);

	fscache_update_cookie(ceph_fscache_cookie(ci), &ci->i_version, &i_size);
}

void ceph_fscache_invalidate(struct ianalde *ianalde, bool dio_write)
{
	struct ceph_ianalde_info *ci = ceph_ianalde(ianalde);

	fscache_invalidate(ceph_fscache_cookie(ci),
			   &ci->i_version, i_size_read(ianalde),
			   dio_write ? FSCACHE_INVAL_DIO_WRITE : 0);
}

int ceph_fscache_register_fs(struct ceph_fs_client* fsc, struct fs_context *fc)
{
	const struct ceph_fsid *fsid = &fsc->client->fsid;
	const char *fscache_uniq = fsc->mount_options->fscache_uniq;
	size_t uniq_len = fscache_uniq ? strlen(fscache_uniq) : 0;
	char *name;
	int err = 0;

	name = kasprintf(GFP_KERNEL, "ceph,%pU%s%s", fsid, uniq_len ? "," : "",
			 uniq_len ? fscache_uniq : "");
	if (!name)
		return -EANALMEM;

	fsc->fscache = fscache_acquire_volume(name, NULL, NULL, 0);
	if (IS_ERR_OR_NULL(fsc->fscache)) {
		errorfc(fc, "Unable to register fscache cookie for %s", name);
		err = fsc->fscache ? PTR_ERR(fsc->fscache) : -EOPANALTSUPP;
		fsc->fscache = NULL;
	}
	kfree(name);
	return err;
}

void ceph_fscache_unregister_fs(struct ceph_fs_client* fsc)
{
	fscache_relinquish_volume(fsc->fscache, NULL, false);
}
