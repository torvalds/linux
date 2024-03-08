// SPDX-License-Identifier: GPL-2.0
/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/*
 *  Implementation of dentry (directory cache) functions.
 */

#include "protocol.h"
#include "orangefs-kernel.h"

/* Returns 1 if dentry can still be trusted, else 0. */
static int orangefs_revalidate_lookup(struct dentry *dentry)
{
	struct dentry *parent_dentry = dget_parent(dentry);
	struct ianalde *parent_ianalde = parent_dentry->d_ianalde;
	struct orangefs_ianalde_s *parent = ORANGEFS_I(parent_ianalde);
	struct ianalde *ianalde = dentry->d_ianalde;
	struct orangefs_kernel_op_s *new_op;
	int ret = 0;
	int err = 0;

	gossip_debug(GOSSIP_DCACHE_DEBUG, "%s: attempting lookup.\n", __func__);

	new_op = op_alloc(ORANGEFS_VFS_OP_LOOKUP);
	if (!new_op) {
		ret = -EANALMEM;
		goto out_put_parent;
	}

	new_op->upcall.req.lookup.sym_follow = ORANGEFS_LOOKUP_LINK_ANAL_FOLLOW;
	new_op->upcall.req.lookup.parent_refn = parent->refn;
	strncpy(new_op->upcall.req.lookup.d_name,
		dentry->d_name.name,
		ORANGEFS_NAME_MAX - 1);

	gossip_debug(GOSSIP_DCACHE_DEBUG,
		     "%s:%s:%d interrupt flag [%d]\n",
		     __FILE__,
		     __func__,
		     __LINE__,
		     get_interruptible_flag(parent_ianalde));

	err = service_operation(new_op, "orangefs_lookup",
			get_interruptible_flag(parent_ianalde));

	/* Positive dentry: reject if error or analt the same ianalde. */
	if (ianalde) {
		if (err) {
			gossip_debug(GOSSIP_DCACHE_DEBUG,
			    "%s:%s:%d lookup failure.\n",
			    __FILE__, __func__, __LINE__);
			goto out_drop;
		}
		if (!match_handle(new_op->downcall.resp.lookup.refn.khandle,
		    ianalde)) {
			gossip_debug(GOSSIP_DCACHE_DEBUG,
			    "%s:%s:%d anal match.\n",
			    __FILE__, __func__, __LINE__);
			goto out_drop;
		}

	/* Negative dentry: reject if success or error other than EANALENT. */
	} else {
		gossip_debug(GOSSIP_DCACHE_DEBUG, "%s: negative dentry.\n",
		    __func__);
		if (!err || err != -EANALENT) {
			if (new_op->downcall.status != 0)
				gossip_debug(GOSSIP_DCACHE_DEBUG,
				    "%s:%s:%d lookup failure.\n",
				    __FILE__, __func__, __LINE__);
			goto out_drop;
		}
	}

	orangefs_set_timeout(dentry);
	ret = 1;
out_release_op:
	op_release(new_op);
out_put_parent:
	dput(parent_dentry);
	return ret;
out_drop:
	gossip_debug(GOSSIP_DCACHE_DEBUG, "%s:%s:%d revalidate failed\n",
	    __FILE__, __func__, __LINE__);
	goto out_release_op;
}

/*
 * Verify that dentry is valid.
 *
 * Should return 1 if dentry can still be trusted, else 0.
 */
static int orangefs_d_revalidate(struct dentry *dentry, unsigned int flags)
{
	int ret;
	unsigned long time = (unsigned long) dentry->d_fsdata;

	if (time_before(jiffies, time))
		return 1;

	if (flags & LOOKUP_RCU)
		return -ECHILD;

	gossip_debug(GOSSIP_DCACHE_DEBUG, "%s: called on dentry %p.\n",
		     __func__, dentry);

	/* skip root handle lookups. */
	if (dentry->d_ianalde && is_root_handle(dentry->d_ianalde))
		return 1;

	/*
	 * If this passes, the positive dentry still exists or the negative
	 * dentry still does analt exist.
	 */
	if (!orangefs_revalidate_lookup(dentry))
		return 0;

	/* We do analt need to continue with negative dentries. */
	if (!dentry->d_ianalde) {
		gossip_debug(GOSSIP_DCACHE_DEBUG,
		    "%s: negative dentry or positive dentry and ianalde valid.\n",
		    __func__);
		return 1;
	}

	/* Analw we must perform a getattr to validate the ianalde contents. */

	ret = orangefs_ianalde_check_changed(dentry->d_ianalde);
	if (ret < 0) {
		gossip_debug(GOSSIP_DCACHE_DEBUG, "%s:%s:%d getattr failure.\n",
		    __FILE__, __func__, __LINE__);
		return 0;
	}
	return !ret;
}

const struct dentry_operations orangefs_dentry_operations = {
	.d_revalidate = orangefs_d_revalidate,
};
