/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005-2009 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include <linux/pagemap.h>
#include <linux/seq_file.h>
#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/debugfs.h>
#include <linux/slab.h>

#include "dlm_internal.h"
#include "lock.h"

#define DLM_DEBUG_BUF_LEN 4096
static char debug_buf[DLM_DEBUG_BUF_LEN];
static struct mutex debug_buf_lock;

static struct dentry *dlm_root;

static char *print_lockmode(int mode)
{
	switch (mode) {
	case DLM_LOCK_IV:
		return "--";
	case DLM_LOCK_NL:
		return "NL";
	case DLM_LOCK_CR:
		return "CR";
	case DLM_LOCK_CW:
		return "CW";
	case DLM_LOCK_PR:
		return "PR";
	case DLM_LOCK_PW:
		return "PW";
	case DLM_LOCK_EX:
		return "EX";
	default:
		return "??";
	}
}

static int print_format1_lock(struct seq_file *s, struct dlm_lkb *lkb,
			      struct dlm_rsb *res)
{
	seq_printf(s, "%08x %s", lkb->lkb_id, print_lockmode(lkb->lkb_grmode));

	if (lkb->lkb_status == DLM_LKSTS_CONVERT ||
	    lkb->lkb_status == DLM_LKSTS_WAITING)
		seq_printf(s, " (%s)", print_lockmode(lkb->lkb_rqmode));

	if (lkb->lkb_nodeid) {
		if (lkb->lkb_nodeid != res->res_nodeid)
			seq_printf(s, " Remote: %3d %08x", lkb->lkb_nodeid,
				   lkb->lkb_remid);
		else
			seq_printf(s, " Master:     %08x", lkb->lkb_remid);
	}

	if (lkb->lkb_wait_type)
		seq_printf(s, " wait_type: %d", lkb->lkb_wait_type);

	return seq_printf(s, "\n");
}

static int print_format1(struct dlm_rsb *res, struct seq_file *s)
{
	struct dlm_lkb *lkb;
	int i, lvblen = res->res_ls->ls_lvblen, recover_list, root_list;
	int rv;

	lock_rsb(res);

	rv = seq_printf(s, "\nResource %p Name (len=%d) \"",
			res, res->res_length);
	if (rv)
		goto out;

	for (i = 0; i < res->res_length; i++) {
		if (isprint(res->res_name[i]))
			seq_printf(s, "%c", res->res_name[i]);
		else
			seq_printf(s, "%c", '.');
	}

	if (res->res_nodeid > 0)
		rv = seq_printf(s, "\"  \nLocal Copy, Master is node %d\n",
				res->res_nodeid);
	else if (res->res_nodeid == 0)
		rv = seq_printf(s, "\"  \nMaster Copy\n");
	else if (res->res_nodeid == -1)
		rv = seq_printf(s, "\"  \nLooking up master (lkid %x)\n",
			   	res->res_first_lkid);
	else
		rv = seq_printf(s, "\"  \nInvalid master %d\n",
				res->res_nodeid);
	if (rv)
		goto out;

	/* Print the LVB: */
	if (res->res_lvbptr) {
		seq_printf(s, "LVB: ");
		for (i = 0; i < lvblen; i++) {
			if (i == lvblen / 2)
				seq_printf(s, "\n     ");
			seq_printf(s, "%02x ",
				   (unsigned char) res->res_lvbptr[i]);
		}
		if (rsb_flag(res, RSB_VALNOTVALID))
			seq_printf(s, " (INVALID)");
		rv = seq_printf(s, "\n");
		if (rv)
			goto out;
	}

	root_list = !list_empty(&res->res_root_list);
	recover_list = !list_empty(&res->res_recover_list);

	if (root_list || recover_list) {
		rv = seq_printf(s, "Recovery: root %d recover %d flags %lx "
				"count %d\n", root_list, recover_list,
			   	res->res_flags, res->res_recover_locks_count);
		if (rv)
			goto out;
	}

	/* Print the locks attached to this resource */
	seq_printf(s, "Granted Queue\n");
	list_for_each_entry(lkb, &res->res_grantqueue, lkb_statequeue) {
		rv = print_format1_lock(s, lkb, res);
		if (rv)
			goto out;
	}

	seq_printf(s, "Conversion Queue\n");
	list_for_each_entry(lkb, &res->res_convertqueue, lkb_statequeue) {
		rv = print_format1_lock(s, lkb, res);
		if (rv)
			goto out;
	}

	seq_printf(s, "Waiting Queue\n");
	list_for_each_entry(lkb, &res->res_waitqueue, lkb_statequeue) {
		rv = print_format1_lock(s, lkb, res);
		if (rv)
			goto out;
	}

	if (list_empty(&res->res_lookup))
		goto out;

	seq_printf(s, "Lookup Queue\n");
	list_for_each_entry(lkb, &res->res_lookup, lkb_rsb_lookup) {
		rv = seq_printf(s, "%08x %s", lkb->lkb_id,
				print_lockmode(lkb->lkb_rqmode));
		if (lkb->lkb_wait_type)
			seq_printf(s, " wait_type: %d", lkb->lkb_wait_type);
		rv = seq_printf(s, "\n");
	}
 out:
	unlock_rsb(res);
	return rv;
}

static int print_format2_lock(struct seq_file *s, struct dlm_lkb *lkb,
			      struct dlm_rsb *r)
{
	u64 xid = 0;
	u64 us;
	int rv;

	if (lkb->lkb_flags & DLM_IFL_USER) {
		if (lkb->lkb_ua)
			xid = lkb->lkb_ua->xid;
	}

	/* microseconds since lkb was added to current queue */
	us = ktime_to_us(ktime_sub(ktime_get(), lkb->lkb_timestamp));

	/* id nodeid remid pid xid exflags flags sts grmode rqmode time_us
	   r_nodeid r_len r_name */

	rv = seq_printf(s, "%x %d %x %u %llu %x %x %d %d %d %llu %u %d \"%s\"\n",
			lkb->lkb_id,
			lkb->lkb_nodeid,
			lkb->lkb_remid,
			lkb->lkb_ownpid,
			(unsigned long long)xid,
			lkb->lkb_exflags,
			lkb->lkb_flags,
			lkb->lkb_status,
			lkb->lkb_grmode,
			lkb->lkb_rqmode,
			(unsigned long long)us,
			r->res_nodeid,
			r->res_length,
			r->res_name);
	return rv;
}

static int print_format2(struct dlm_rsb *r, struct seq_file *s)
{
	struct dlm_lkb *lkb;
	int rv = 0;

	lock_rsb(r);

	list_for_each_entry(lkb, &r->res_grantqueue, lkb_statequeue) {
		rv = print_format2_lock(s, lkb, r);
		if (rv)
			goto out;
	}

	list_for_each_entry(lkb, &r->res_convertqueue, lkb_statequeue) {
		rv = print_format2_lock(s, lkb, r);
		if (rv)
			goto out;
	}

	list_for_each_entry(lkb, &r->res_waitqueue, lkb_statequeue) {
		rv = print_format2_lock(s, lkb, r);
		if (rv)
			goto out;
	}
 out:
	unlock_rsb(r);
	return rv;
}

static int print_format3_lock(struct seq_file *s, struct dlm_lkb *lkb,
			      int rsb_lookup)
{
	u64 xid = 0;
	int rv;

	if (lkb->lkb_flags & DLM_IFL_USER) {
		if (lkb->lkb_ua)
			xid = lkb->lkb_ua->xid;
	}

	rv = seq_printf(s, "lkb %x %d %x %u %llu %x %x %d %d %d %d %d %d %u %llu %llu\n",
			lkb->lkb_id,
			lkb->lkb_nodeid,
			lkb->lkb_remid,
			lkb->lkb_ownpid,
			(unsigned long long)xid,
			lkb->lkb_exflags,
			lkb->lkb_flags,
			lkb->lkb_status,
			lkb->lkb_grmode,
			lkb->lkb_rqmode,
			lkb->lkb_last_bast.mode,
			rsb_lookup,
			lkb->lkb_wait_type,
			lkb->lkb_lvbseq,
			(unsigned long long)ktime_to_ns(lkb->lkb_timestamp),
			(unsigned long long)ktime_to_ns(lkb->lkb_last_bast_time));
	return rv;
}

static int print_format3(struct dlm_rsb *r, struct seq_file *s)
{
	struct dlm_lkb *lkb;
	int i, lvblen = r->res_ls->ls_lvblen;
	int print_name = 1;
	int rv;

	lock_rsb(r);

	rv = seq_printf(s, "rsb %p %d %x %lx %d %d %u %d ",
			r,
			r->res_nodeid,
			r->res_first_lkid,
			r->res_flags,
			!list_empty(&r->res_root_list),
			!list_empty(&r->res_recover_list),
			r->res_recover_locks_count,
			r->res_length);
	if (rv)
		goto out;

	for (i = 0; i < r->res_length; i++) {
		if (!isascii(r->res_name[i]) || !isprint(r->res_name[i]))
			print_name = 0;
	}

	seq_printf(s, "%s", print_name ? "str " : "hex");

	for (i = 0; i < r->res_length; i++) {
		if (print_name)
			seq_printf(s, "%c", r->res_name[i]);
		else
			seq_printf(s, " %02x", (unsigned char)r->res_name[i]);
	}
	rv = seq_printf(s, "\n");
	if (rv)
		goto out;

	if (!r->res_lvbptr)
		goto do_locks;

	seq_printf(s, "lvb %u %d", r->res_lvbseq, lvblen);

	for (i = 0; i < lvblen; i++)
		seq_printf(s, " %02x", (unsigned char)r->res_lvbptr[i]);
	rv = seq_printf(s, "\n");
	if (rv)
		goto out;

 do_locks:
	list_for_each_entry(lkb, &r->res_grantqueue, lkb_statequeue) {
		rv = print_format3_lock(s, lkb, 0);
		if (rv)
			goto out;
	}

	list_for_each_entry(lkb, &r->res_convertqueue, lkb_statequeue) {
		rv = print_format3_lock(s, lkb, 0);
		if (rv)
			goto out;
	}

	list_for_each_entry(lkb, &r->res_waitqueue, lkb_statequeue) {
		rv = print_format3_lock(s, lkb, 0);
		if (rv)
			goto out;
	}

	list_for_each_entry(lkb, &r->res_lookup, lkb_rsb_lookup) {
		rv = print_format3_lock(s, lkb, 1);
		if (rv)
			goto out;
	}
 out:
	unlock_rsb(r);
	return rv;
}

struct rsbtbl_iter {
	struct dlm_rsb *rsb;
	unsigned bucket;
	int format;
	int header;
};

/* seq_printf returns -1 if the buffer is full, and 0 otherwise.
   If the buffer is full, seq_printf can be called again, but it
   does nothing and just returns -1.  So, the these printing routines
   periodically check the return value to avoid wasting too much time
   trying to print to a full buffer. */

static int table_seq_show(struct seq_file *seq, void *iter_ptr)
{
	struct rsbtbl_iter *ri = iter_ptr;
	int rv = 0;

	switch (ri->format) {
	case 1:
		rv = print_format1(ri->rsb, seq);
		break;
	case 2:
		if (ri->header) {
			seq_printf(seq, "id nodeid remid pid xid exflags "
					"flags sts grmode rqmode time_ms "
					"r_nodeid r_len r_name\n");
			ri->header = 0;
		}
		rv = print_format2(ri->rsb, seq);
		break;
	case 3:
		if (ri->header) {
			seq_printf(seq, "version rsb 1.1 lvb 1.1 lkb 1.1\n");
			ri->header = 0;
		}
		rv = print_format3(ri->rsb, seq);
		break;
	}

	return rv;
}

static const struct seq_operations format1_seq_ops;
static const struct seq_operations format2_seq_ops;
static const struct seq_operations format3_seq_ops;

static void *table_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct rb_node *node;
	struct dlm_ls *ls = seq->private;
	struct rsbtbl_iter *ri;
	struct dlm_rsb *r;
	loff_t n = *pos;
	unsigned bucket, entry;

	bucket = n >> 32;
	entry = n & ((1LL << 32) - 1);

	if (bucket >= ls->ls_rsbtbl_size)
		return NULL;

	ri = kzalloc(sizeof(struct rsbtbl_iter), GFP_NOFS);
	if (!ri)
		return NULL;
	if (n == 0)
		ri->header = 1;
	if (seq->op == &format1_seq_ops)
		ri->format = 1;
	if (seq->op == &format2_seq_ops)
		ri->format = 2;
	if (seq->op == &format3_seq_ops)
		ri->format = 3;

	spin_lock(&ls->ls_rsbtbl[bucket].lock);
	if (!RB_EMPTY_ROOT(&ls->ls_rsbtbl[bucket].keep)) {
		for (node = rb_first(&ls->ls_rsbtbl[bucket].keep); node;
		     node = rb_next(node)) {
			r = rb_entry(node, struct dlm_rsb, res_hashnode);
			if (!entry--) {
				dlm_hold_rsb(r);
				ri->rsb = r;
				ri->bucket = bucket;
				spin_unlock(&ls->ls_rsbtbl[bucket].lock);
				return ri;
			}
		}
	}
	spin_unlock(&ls->ls_rsbtbl[bucket].lock);

	/*
	 * move to the first rsb in the next non-empty bucket
	 */

	/* zero the entry */
	n &= ~((1LL << 32) - 1);

	while (1) {
		bucket++;
		n += 1LL << 32;

		if (bucket >= ls->ls_rsbtbl_size) {
			kfree(ri);
			return NULL;
		}

		spin_lock(&ls->ls_rsbtbl[bucket].lock);
		if (!RB_EMPTY_ROOT(&ls->ls_rsbtbl[bucket].keep)) {
			node = rb_first(&ls->ls_rsbtbl[bucket].keep);
			r = rb_entry(node, struct dlm_rsb, res_hashnode);
			dlm_hold_rsb(r);
			ri->rsb = r;
			ri->bucket = bucket;
			spin_unlock(&ls->ls_rsbtbl[bucket].lock);
			*pos = n;
			return ri;
		}
		spin_unlock(&ls->ls_rsbtbl[bucket].lock);
	}
}

static void *table_seq_next(struct seq_file *seq, void *iter_ptr, loff_t *pos)
{
	struct dlm_ls *ls = seq->private;
	struct rsbtbl_iter *ri = iter_ptr;
	struct rb_node *next;
	struct dlm_rsb *r, *rp;
	loff_t n = *pos;
	unsigned bucket;

	bucket = n >> 32;

	/*
	 * move to the next rsb in the same bucket
	 */

	spin_lock(&ls->ls_rsbtbl[bucket].lock);
	rp = ri->rsb;
	next = rb_next(&rp->res_hashnode);

	if (next) {
		r = rb_entry(next, struct dlm_rsb, res_hashnode);
		dlm_hold_rsb(r);
		ri->rsb = r;
		spin_unlock(&ls->ls_rsbtbl[bucket].lock);
		dlm_put_rsb(rp);
		++*pos;
		return ri;
	}
	spin_unlock(&ls->ls_rsbtbl[bucket].lock);
	dlm_put_rsb(rp);

	/*
	 * move to the first rsb in the next non-empty bucket
	 */

	/* zero the entry */
	n &= ~((1LL << 32) - 1);

	while (1) {
		bucket++;
		n += 1LL << 32;

		if (bucket >= ls->ls_rsbtbl_size) {
			kfree(ri);
			return NULL;
		}

		spin_lock(&ls->ls_rsbtbl[bucket].lock);
		if (!RB_EMPTY_ROOT(&ls->ls_rsbtbl[bucket].keep)) {
			next = rb_first(&ls->ls_rsbtbl[bucket].keep);
			r = rb_entry(next, struct dlm_rsb, res_hashnode);
			dlm_hold_rsb(r);
			ri->rsb = r;
			ri->bucket = bucket;
			spin_unlock(&ls->ls_rsbtbl[bucket].lock);
			*pos = n;
			return ri;
		}
		spin_unlock(&ls->ls_rsbtbl[bucket].lock);
	}
}

static void table_seq_stop(struct seq_file *seq, void *iter_ptr)
{
	struct rsbtbl_iter *ri = iter_ptr;

	if (ri) {
		dlm_put_rsb(ri->rsb);
		kfree(ri);
	}
}

static const struct seq_operations format1_seq_ops = {
	.start = table_seq_start,
	.next  = table_seq_next,
	.stop  = table_seq_stop,
	.show  = table_seq_show,
};

static const struct seq_operations format2_seq_ops = {
	.start = table_seq_start,
	.next  = table_seq_next,
	.stop  = table_seq_stop,
	.show  = table_seq_show,
};

static const struct seq_operations format3_seq_ops = {
	.start = table_seq_start,
	.next  = table_seq_next,
	.stop  = table_seq_stop,
	.show  = table_seq_show,
};

static const struct file_operations format1_fops;
static const struct file_operations format2_fops;
static const struct file_operations format3_fops;

static int table_open(struct inode *inode, struct file *file)
{
	struct seq_file *seq;
	int ret = -1;

	if (file->f_op == &format1_fops)
		ret = seq_open(file, &format1_seq_ops);
	else if (file->f_op == &format2_fops)
		ret = seq_open(file, &format2_seq_ops);
	else if (file->f_op == &format3_fops)
		ret = seq_open(file, &format3_seq_ops);

	if (ret)
		return ret;

	seq = file->private_data;
	seq->private = inode->i_private; /* the dlm_ls */
	return 0;
}

static const struct file_operations format1_fops = {
	.owner   = THIS_MODULE,
	.open    = table_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release
};

static const struct file_operations format2_fops = {
	.owner   = THIS_MODULE,
	.open    = table_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release
};

static const struct file_operations format3_fops = {
	.owner   = THIS_MODULE,
	.open    = table_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release
};

/*
 * dump lkb's on the ls_waiters list
 */
static ssize_t waiters_read(struct file *file, char __user *userbuf,
			    size_t count, loff_t *ppos)
{
	struct dlm_ls *ls = file->private_data;
	struct dlm_lkb *lkb;
	size_t len = DLM_DEBUG_BUF_LEN, pos = 0, ret, rv;

	mutex_lock(&debug_buf_lock);
	mutex_lock(&ls->ls_waiters_mutex);
	memset(debug_buf, 0, sizeof(debug_buf));

	list_for_each_entry(lkb, &ls->ls_waiters, lkb_wait_reply) {
		ret = snprintf(debug_buf + pos, len - pos, "%x %d %d %s\n",
			       lkb->lkb_id, lkb->lkb_wait_type,
			       lkb->lkb_nodeid, lkb->lkb_resource->res_name);
		if (ret >= len - pos)
			break;
		pos += ret;
	}
	mutex_unlock(&ls->ls_waiters_mutex);

	rv = simple_read_from_buffer(userbuf, count, ppos, debug_buf, pos);
	mutex_unlock(&debug_buf_lock);
	return rv;
}

static const struct file_operations waiters_fops = {
	.owner   = THIS_MODULE,
	.open    = simple_open,
	.read    = waiters_read,
	.llseek  = default_llseek,
};

void dlm_delete_debug_file(struct dlm_ls *ls)
{
	if (ls->ls_debug_rsb_dentry)
		debugfs_remove(ls->ls_debug_rsb_dentry);
	if (ls->ls_debug_waiters_dentry)
		debugfs_remove(ls->ls_debug_waiters_dentry);
	if (ls->ls_debug_locks_dentry)
		debugfs_remove(ls->ls_debug_locks_dentry);
	if (ls->ls_debug_all_dentry)
		debugfs_remove(ls->ls_debug_all_dentry);
}

int dlm_create_debug_file(struct dlm_ls *ls)
{
	char name[DLM_LOCKSPACE_LEN+8];

	/* format 1 */

	ls->ls_debug_rsb_dentry = debugfs_create_file(ls->ls_name,
						      S_IFREG | S_IRUGO,
						      dlm_root,
						      ls,
						      &format1_fops);
	if (!ls->ls_debug_rsb_dentry)
		goto fail;

	/* format 2 */

	memset(name, 0, sizeof(name));
	snprintf(name, DLM_LOCKSPACE_LEN+8, "%s_locks", ls->ls_name);

	ls->ls_debug_locks_dentry = debugfs_create_file(name,
							S_IFREG | S_IRUGO,
							dlm_root,
							ls,
							&format2_fops);
	if (!ls->ls_debug_locks_dentry)
		goto fail;

	/* format 3 */

	memset(name, 0, sizeof(name));
	snprintf(name, DLM_LOCKSPACE_LEN+8, "%s_all", ls->ls_name);

	ls->ls_debug_all_dentry = debugfs_create_file(name,
						      S_IFREG | S_IRUGO,
						      dlm_root,
						      ls,
						      &format3_fops);
	if (!ls->ls_debug_all_dentry)
		goto fail;

	memset(name, 0, sizeof(name));
	snprintf(name, DLM_LOCKSPACE_LEN+8, "%s_waiters", ls->ls_name);

	ls->ls_debug_waiters_dentry = debugfs_create_file(name,
							  S_IFREG | S_IRUGO,
							  dlm_root,
							  ls,
							  &waiters_fops);
	if (!ls->ls_debug_waiters_dentry)
		goto fail;

	return 0;

 fail:
	dlm_delete_debug_file(ls);
	return -ENOMEM;
}

int __init dlm_register_debugfs(void)
{
	mutex_init(&debug_buf_lock);
	dlm_root = debugfs_create_dir("dlm", NULL);
	return dlm_root ? 0 : -ENOMEM;
}

void dlm_unregister_debugfs(void)
{
	debugfs_remove(dlm_root);
}

