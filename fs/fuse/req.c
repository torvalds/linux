// SPDX-License-Identifier: GPL-2.0-only

#include "dev.h"
#include "fuse_i.h"

static int fuse_fill_creds(struct fuse_mount *fm, struct fuse_args *args, struct mnt_idmap *idmap)
{
	struct fuse_conn *fc = fm->fc;
	bool no_idmap = !fm->sb || (fm->sb->s_iflags & SB_I_NOIDMAP);
	kuid_t fsuid = mapped_fsuid(idmap, fc->user_ns);
	kgid_t fsgid = mapped_fsgid(idmap, fc->user_ns);

	args->pid = pid_nr_ns(task_pid(current), fc->pid_ns);

	if (args->force) {
		if (args->nocreds)
			return 0;

		if (no_idmap) {
			args->uid = from_kuid_munged(fc->user_ns, current_fsuid());
			args->gid = from_kgid_munged(fc->user_ns, current_fsgid());
		} else {
			args->uid = FUSE_INVALID_UIDGID;
			args->gid = FUSE_INVALID_UIDGID;
		}
		return 0;
	}

	WARN_ON(args->nocreds);
	/*
	 * Keep the old behavior when idmappings support was not
	 * declared by a FUSE server.
	 *
	 * For those FUSE servers who support idmapped mounts, we send UID/GID
	 * only along with "inode creation" fuse requests, otherwise idmap ==
	 * &invalid_mnt_idmap and req->in.h.{u,g}id will be equal to
	 * FUSE_INVALID_UIDGID.
	 */
	if (no_idmap) {
		fsuid = current_fsuid();
		fsgid = current_fsgid();
	}
	args->uid = from_kuid(fc->user_ns, fsuid);
	args->gid = from_kgid(fc->user_ns, fsgid);

	if (no_idmap && unlikely(args->uid == ((uid_t)-1) || args->gid == ((gid_t)-1)))
		return -EOVERFLOW;

	return 0;
}

static int fuse_req_prep(struct fuse_mount *fm, struct fuse_args *args, struct mnt_idmap *idmap)
{
	if (!args->force && fm->fc->conn_error)
		return -ECONNREFUSED;

	return fuse_fill_creds(fm, args, idmap);
}

ssize_t __fuse_simple_request(struct mnt_idmap *idmap, struct fuse_mount *fm,
			      struct fuse_args *args)
{
	struct fuse_conn *fc = fm->fc;
	int err = fuse_req_prep(fm, args, idmap);

	if (err)
		return err;

	return fuse_chan_send(fc->chan, args);
}

int fuse_simple_background(struct fuse_mount *fm, struct fuse_args *args, gfp_t gfp_flags)
{
	struct fuse_conn *fc = fm->fc;
	int err;

	WARN_ON(args->force && !args->nocreds);

	err = fuse_req_prep(fm, args, &invalid_mnt_idmap);
	if (err)
		return err;

	return fuse_chan_send_bg(fc->chan, args, gfp_flags);
}
EXPORT_SYMBOL_GPL(fuse_simple_background);

int fuse_simple_notify_reply(struct fuse_mount *fm, struct fuse_args *args, u64 unique)
{
	struct fuse_conn *fc = fm->fc;
	int err;

	WARN_ON(args->force && !args->nocreds);

	err = fuse_req_prep(fm, args, &invalid_mnt_idmap);
	if (err)
		return err;

	return fuse_chan_send_notify_reply(fc->chan, args, unique);
}
