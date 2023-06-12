// SPDX-License-Identifier: GPL-2.0-only
/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005-2009 Red Hat, Inc.  All rights reserved.
**
**
*******************************************************************************
******************************************************************************/

#include <linux/pagemap.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/ctype.h>
#include <linux/debugfs.h>
#include <linux/slab.h>

#include "dlm_internal.h"
#include "midcomms.h"
#include "lock.h"

#define DLM_DEBUG_BUF_LEN 4096
static char debug_buf[DLM_DEBUG_BUF_LEN];
static struct mutex debug_buf_lock;

static struct dentry *dlm_root;
static struct dentry *dlm_comms;

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

static void print_format1_lock(struct seq_file *s, struct dlm_lkb *lkb,
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

	seq_putc(s, '\n');
}

static void print_format1(struct dlm_rsb *res, struct seq_file *s)
{
	struct dlm_lkb *lkb;
	int i, lvblen = res->res_ls->ls_lvblen, recover_list, root_list;

	lock_rsb(res);

	seq_printf(s, "\nResource %p Name (len=%d) \"", res, res->res_length);

	for (i = 0; i < res->res_length; i++) {
		if (isprint(res->res_name[i]))
			seq_printf(s, "%c", res->res_name[i]);
		else
			seq_printf(s, "%c", '.');
	}

	if (res->res_nodeid > 0)
		seq_printf(s, "\"\nLocal Copy, Master is node %d\n",
			   res->res_nodeid);
	else if (res->res_nodeid == 0)
		seq_puts(s, "\"\nMaster Copy\n");
	else if (res->res_nodeid == -1)
		seq_printf(s, "\"\nLooking up master (lkid %x)\n",
			   res->res_first_lkid);
	else
		seq_printf(s, "\"\nInvalid master %d\n", res->res_nodeid);
	if (seq_has_overflowed(s))
		goto out;

	/* Print the LVB: */
	if (res->res_lvbptr) {
		seq_puts(s, "LVB: ");
		for (i = 0; i < lvblen; i++) {
			if (i == lvblen / 2)
				seq_puts(s, "\n     ");
			seq_printf(s, "%02x ",
				   (unsigned char) res->res_lvbptr[i]);
		}
		if (rsb_flag(res, RSB_VALNOTVALID))
			seq_puts(s, " (INVALID)");
		seq_putc(s, '\n');
		if (seq_has_overflowed(s))
			goto out;
	}

	root_list = !list_empty(&res->res_root_list);
	recover_list = !list_empty(&res->res_recover_list);

	if (root_list || recover_list) {
		seq_printf(s, "Recovery: root %d recover %d flags %lx count %d\n",
			   root_list, recover_list,
			   res->res_flags, res->res_recover_locks_count);
	}

	/* Print the locks attached to this resource */
	seq_puts(s, "Granted Queue\n");
	list_for_each_entry(lkb, &res->res_grantqueue, lkb_statequeue) {
		print_format1_lock(s, lkb, res);
		if (seq_has_overflowed(s))
			goto out;
	}

	seq_puts(s, "Conversion Queue\n");
	list_for_each_entry(lkb, &res->res_convertqueue, lkb_statequeue) {
		print_format1_lock(s, lkb, res);
		if (seq_has_overflowed(s))
			goto out;
	}

	seq_puts(s, "Waiting Queue\n");
	list_for_each_entry(lkb, &res->res_waitqueue, lkb_statequeue) {
		print_format1_lock(s, lkb, res);
		if (seq_has_overflowed(s))
			goto out;
	}

	if (list_empty(&res->res_lookup))
		goto out;

	seq_puts(s, "Lookup Queue\n");
	list_for_each_entry(lkb, &res->res_lookup, lkb_rsb_lookup) {
		seq_printf(s, "%08x %s",
			   lkb->lkb_id, print_lockmode(lkb->lkb_rqmode));
		if (lkb->lkb_wait_type)
			seq_printf(s, " wait_type: %d", lkb->lkb_wait_type);
		seq_putc(s, '\n');
		if (seq_has_overflowed(s))
			goto out;
	}
 out:
	unlock_rsb(res);
}

static void print_format2_lock(struct seq_file *s, struct dlm_lkb *lkb,
			       struct dlm_rsb *r)
{
	u64 xid = 0;
	u64 us;

	if (test_bit(DLM_DFL_USER_BIT, &lkb->lkb_dflags)) {
		if (lkb->lkb_ua)
			xid = lkb->lkb_ua->xid;
	}

	/* microseconds since lkb was added to current queue */
	us = ktime_to_us(ktime_sub(ktime_get(), lkb->lkb_timestamp));

	/* id nodeid remid pid xid exflags flags sts grmode rqmode time_us
	   r_nodeid r_len r_name */

	seq_printf(s, "%x %d %x %u %llu %x %x %d %d %d %llu %u %d \"%s\"\n",
		   lkb->lkb_id,
		   lkb->lkb_nodeid,
		   lkb->lkb_remid,
		   lkb->lkb_ownpid,
		   (unsigned long long)xid,
		   lkb->lkb_exflags,
		   dlm_iflags_val(lkb),
		   lkb->lkb_status,
		   lkb->lkb_grmode,
		   lkb->lkb_rqmode,
		   (unsigned long long)us,
		   r->res_nodeid,
		   r->res_length,
		   r->res_name);
}

static void print_format2(struct dlm_rsb *r, struct seq_file *s)
{
	struct dlm_lkb *lkb;

	lock_rsb(r);

	list_for_each_entry(lkb, &r->res_grantqueue, lkb_statequeue) {
		print_format2_lock(s, lkb, r);
		if (seq_has_overflowed(s))
			goto out;
	}

	list_for_each_entry(lkb, &r->res_convertqueue, lkb_statequeue) {
		print_format2_lock(s, lkb, r);
		if (seq_has_overflowed(s))
			goto out;
	}

	list_for_each_entry(lkb, &r->res_waitqueue, lkb_statequeue) {
		print_format2_lock(s, lkb, r);
		if (seq_has_overflowed(s))
			goto out;
	}
 out:
	unlock_rsb(r);
}

static void print_format3_lock(struct seq_file *s, struct dlm_lkb *lkb,
			      int rsb_lookup)
{
	u64 xid = 0;

	if (test_bit(DLM_DFL_USER_BIT, &lkb->lkb_dflags)) {
		if (lkb->lkb_ua)
			xid = lkb->lkb_ua->xid;
	}

	seq_printf(s, "lkb %x %d %x %u %llu %x %x %d %d %d %d %d %d %u %llu %llu\n",
		   lkb->lkb_id,
		   lkb->lkb_nodeid,
		   lkb->lkb_remid,
		   lkb->lkb_ownpid,
		   (unsigned long long)xid,
		   lkb->lkb_exflags,
		   dlm_iflags_val(lkb),
		   lkb->lkb_status,
		   lkb->lkb_grmode,
		   lkb->lkb_rqmode,
		   lkb->lkb_last_bast_mode,
		   rsb_lookup,
		   lkb->lkb_wait_type,
		   lkb->lkb_lvbseq,
		   (unsigned long long)ktime_to_ns(lkb->lkb_timestamp),
		   (unsigned long long)ktime_to_ns(lkb->lkb_last_bast_time));
}

static void print_format3(struct dlm_rsb *r, struct seq_file *s)
{
	struct dlm_lkb *lkb;
	int i, lvblen = r->res_ls->ls_lvblen;
	int print_name = 1;

	lock_rsb(r);

	seq_printf(s, "rsb %p %d %x %lx %d %d %u %d ",
		   r,
		   r->res_nodeid,
		   r->res_first_lkid,
		   r->res_flags,
		   !list_empty(&r->res_root_list),
		   !list_empty(&r->res_recover_list),
		   r->res_recover_locks_count,
		   r->res_length);
	if (seq_has_overflowed(s))
		goto out;

	for (i = 0; i < r->res_length; i++) {
		if (!isascii(r->res_name[i]) || !isprint(r->res_name[i]))
			print_name = 0;
	}

	seq_puts(s, print_name ? "str " : "hex");

	for (i = 0; i < r->res_length; i++) {
		if (print_name)
			seq_printf(s, "%c", r->res_name[i]);
		else
			seq_printf(s, " %02x", (unsigned char)r->res_name[i]);
	}
	seq_putc(s, '\n');
	if (seq_has_overflowed(s))
		goto out;

	if (!r->res_lvbptr)
		goto do_locks;

	seq_printf(s, "lvb %u %d", r->res_lvbseq, lvblen);

	for (i = 0; i < lvblen; i++)
		seq_printf(s, " %02x", (unsigned char)r->res_lvbptr[i]);
	seq_putc(s, '\n');
	if (seq_has_overflowed(s))
		goto out;

 do_locks:
	list_for_each_entry(lkb, &r->res_grantqueue, lkb_statequeue) {
		print_format3_lock(s, lkb, 0);
		if (seq_has_overflowed(s))
			goto out;
	}

	list_for_each_entry(lkb, &r->res_convertqueue, lkb_statequeue) {
		print_format3_lock(s, lkb, 0);
		if (seq_has_overflowed(s))
			goto out;
	}

	list_for_each_entry(lkb, &r->res_waitqueue, lkb_statequeue) {
		print_format3_lock(s, lkb, 0);
		if (seq_has_overflowed(s))
			goto out;
	}

	list_for_each_entry(lkb, &r->res_lookup, lkb_rsb_lookup) {
		print_format3_lock(s, lkb, 1);
		if (seq_has_overflowed(s))
			goto out;
	}
 out:
	unlock_rsb(r);
}

static void print_format4(struct dlm_rsb *r, struct seq_file *s)
{
	int our_nodeid = dlm_our_nodeid();
	int print_name = 1;
	int i;

	lock_rsb(r);

	seq_printf(s, "rsb %p %d %d %d %d %lu %lx %d ",
		   r,
		   r->res_nodeid,
		   r->res_master_nodeid,
		   r->res_dir_nodeid,
		   our_nodeid,
		   r->res_toss_time,
		   r->res_flags,
		   r->res_length);

	for (i = 0; i < r->res_length; i++) {
		if (!isascii(r->res_name[i]) || !isprint(r->res_name[i]))
			print_name = 0;
	}

	seq_puts(s, print_name ? "str " : "hex");

	for (i = 0; i < r->res_length; i++) {
		if (print_name)
			seq_printf(s, "%c", r->res_name[i]);
		else
			seq_printf(s, " %02x", (unsigned char)r->res_name[i]);
	}
	seq_putc(s, '\n');
	unlock_rsb(r);
}

struct rsbtbl_iter {
	struct dlm_rsb *rsb;
	unsigned bucket;
	int format;
	int header;
};

/*
 * If the buffer is full, seq_printf can be called again, but it
 * does nothing.  So, the these printing routines periodically check
 * seq_has_overflowed to avoid wasting too much time trying to print to
 * a full buffer.
 */

static int table_seq_show(struct seq_file *seq, void *iter_ptr)
{
	struct rsbtbl_iter *ri = iter_ptr;

	switch (ri->format) {
	case 1:
		print_format1(ri->rsb, seq);
		break;
	case 2:
		if (ri->header) {
			seq_puts(seq, "id nodeid remid pid xid exflags flags sts grmode rqmode time_ms r_nodeid r_len r_name\n");
			ri->header = 0;
		}
		print_format2(ri->rsb, seq);
		break;
	case 3:
		if (ri->header) {
			seq_puts(seq, "version rsb 1.1 lvb 1.1 lkb 1.1\n");
			ri->header = 0;
		}
		print_format3(ri->rsb, seq);
		break;
	case 4:
		if (ri->header) {
			seq_puts(seq, "version 4 rsb 2\n");
			ri->header = 0;
		}
		print_format4(ri->rsb, seq);
		break;
	}

	return 0;
}

static const struct seq_operations format1_seq_ops;
static const struct seq_operations format2_seq_ops;
static const struct seq_operations format3_seq_ops;
static const struct seq_operations format4_seq_ops;

static void *table_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct rb_root *tree;
	struct rb_node *node;
	struct dlm_ls *ls = seq->private;
	struct rsbtbl_iter *ri;
	struct dlm_rsb *r;
	loff_t n = *pos;
	unsigned bucket, entry;
	int toss = (seq->op == &format4_seq_ops);

	bucket = n >> 32;
	entry = n & ((1LL << 32) - 1);

	if (bucket >= ls->ls_rsbtbl_size)
		return NULL;

	ri = kzalloc(sizeof(*ri), GFP_NOFS);
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
	if (seq->op == &format4_seq_ops)
		ri->format = 4;

	tree = toss ? &ls->ls_rsbtbl[bucket].toss : &ls->ls_rsbtbl[bucket].keep;

	spin_lock(&ls->ls_rsbtbl[bucket].lock);
	if (!RB_EMPTY_ROOT(tree)) {
		for (node = rb_first(tree); node; node = rb_next(node)) {
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
		tree = toss ? &ls->ls_rsbtbl[bucket].toss : &ls->ls_rsbtbl[bucket].keep;

		spin_lock(&ls->ls_rsbtbl[bucket].lock);
		if (!RB_EMPTY_ROOT(tree)) {
			node = rb_first(tree);
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
	struct rb_root *tree;
	struct rb_node *next;
	struct dlm_rsb *r, *rp;
	loff_t n = *pos;
	unsigned bucket;
	int toss = (seq->op == &format4_seq_ops);

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
			++*pos;
			return NULL;
		}
		tree = toss ? &ls->ls_rsbtbl[bucket].toss : &ls->ls_rsbtbl[bucket].keep;

		spin_lock(&ls->ls_rsbtbl[bucket].lock);
		if (!RB_EMPTY_ROOT(tree)) {
			next = rb_first(tree);
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

static const struct seq_operations format4_seq_ops = {
	.start = table_seq_start,
	.next  = table_seq_next,
	.stop  = table_seq_stop,
	.show  = table_seq_show,
};

static const struct file_operations format1_fops;
static const struct file_operations format2_fops;
static const struct file_operations format3_fops;
static const struct file_operations format4_fops;

static int table_open1(struct inode *inode, struct file *file)
{
	struct seq_file *seq;
	int ret;

	ret = seq_open(file, &format1_seq_ops);
	if (ret)
		return ret;

	seq = file->private_data;
	seq->private = inode->i_private; /* the dlm_ls */
	return 0;
}

static int table_open2(struct inode *inode, struct file *file)
{
	struct seq_file *seq;
	int ret;

	ret = seq_open(file, &format2_seq_ops);
	if (ret)
		return ret;

	seq = file->private_data;
	seq->private = inode->i_private; /* the dlm_ls */
	return 0;
}

static ssize_t table_write2(struct file *file, const char __user *user_buf,
			    size_t count, loff_t *ppos)
{
	struct seq_file *seq = file->private_data;
	int n, len, lkb_nodeid, lkb_status, error;
	char name[DLM_RESNAME_MAXLEN + 1] = {};
	struct dlm_ls *ls = seq->private;
	unsigned int lkb_flags;
	char buf[256] = {};
	uint32_t lkb_id;

	if (copy_from_user(buf, user_buf,
			   min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	n = sscanf(buf, "%x %" __stringify(DLM_RESNAME_MAXLEN) "s %x %d %d",
		   &lkb_id, name, &lkb_flags, &lkb_nodeid, &lkb_status);
	if (n != 5)
		return -EINVAL;

	len = strnlen(name, DLM_RESNAME_MAXLEN);
	error = dlm_debug_add_lkb(ls, lkb_id, name, len, lkb_flags,
				  lkb_nodeid, lkb_status);
	if (error)
		return error;

	return count;
}

static int table_open3(struct inode *inode, struct file *file)
{
	struct seq_file *seq;
	int ret;

	ret = seq_open(file, &format3_seq_ops);
	if (ret)
		return ret;

	seq = file->private_data;
	seq->private = inode->i_private; /* the dlm_ls */
	return 0;
}

static int table_open4(struct inode *inode, struct file *file)
{
	struct seq_file *seq;
	int ret;

	ret = seq_open(file, &format4_seq_ops);
	if (ret)
		return ret;

	seq = file->private_data;
	seq->private = inode->i_private; /* the dlm_ls */
	return 0;
}

static const struct file_operations format1_fops = {
	.owner   = THIS_MODULE,
	.open    = table_open1,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release
};

static const struct file_operations format2_fops = {
	.owner   = THIS_MODULE,
	.open    = table_open2,
	.read    = seq_read,
	.write   = table_write2,
	.llseek  = seq_lseek,
	.release = seq_release
};

static const struct file_operations format3_fops = {
	.owner   = THIS_MODULE,
	.open    = table_open3,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release
};

static const struct file_operations format4_fops = {
	.owner   = THIS_MODULE,
	.open    = table_open4,
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

static ssize_t waiters_write(struct file *file, const char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	struct dlm_ls *ls = file->private_data;
	int mstype, to_nodeid;
	char buf[128] = {};
	uint32_t lkb_id;
	int n, error;

	if (copy_from_user(buf, user_buf,
			   min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	n = sscanf(buf, "%x %d %d", &lkb_id, &mstype, &to_nodeid);
	if (n != 3)
		return -EINVAL;

	error = dlm_debug_add_lkb_to_waiters(ls, lkb_id, mstype, to_nodeid);
	if (error)
		return error;

	return count;
}

static const struct file_operations waiters_fops = {
	.owner   = THIS_MODULE,
	.open    = simple_open,
	.read    = waiters_read,
	.write   = waiters_write,
	.llseek  = default_llseek,
};

void dlm_delete_debug_file(struct dlm_ls *ls)
{
	debugfs_remove(ls->ls_debug_rsb_dentry);
	debugfs_remove(ls->ls_debug_waiters_dentry);
	debugfs_remove(ls->ls_debug_locks_dentry);
	debugfs_remove(ls->ls_debug_all_dentry);
	debugfs_remove(ls->ls_debug_toss_dentry);
}

static int dlm_state_show(struct seq_file *file, void *offset)
{
	seq_printf(file, "%s\n", dlm_midcomms_state(file->private));
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(dlm_state);

static int dlm_flags_show(struct seq_file *file, void *offset)
{
	seq_printf(file, "%lu\n", dlm_midcomms_flags(file->private));
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(dlm_flags);

static int dlm_send_queue_cnt_show(struct seq_file *file, void *offset)
{
	seq_printf(file, "%d\n", dlm_midcomms_send_queue_cnt(file->private));
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(dlm_send_queue_cnt);

static int dlm_version_show(struct seq_file *file, void *offset)
{
	seq_printf(file, "0x%08x\n", dlm_midcomms_version(file->private));
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(dlm_version);

static ssize_t dlm_rawmsg_write(struct file *fp, const char __user *user_buf,
				size_t count, loff_t *ppos)
{
	void *buf;
	int ret;

	if (count > PAGE_SIZE || count < sizeof(struct dlm_header))
		return -EINVAL;

	buf = kmalloc(PAGE_SIZE, GFP_NOFS);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, user_buf, count)) {
		ret = -EFAULT;
		goto out;
	}

	ret = dlm_midcomms_rawmsg_send(fp->private_data, buf, count);
	if (ret)
		goto out;

	kfree(buf);
	return count;

out:
	kfree(buf);
	return ret;
}

static const struct file_operations dlm_rawmsg_fops = {
	.open	= simple_open,
	.write	= dlm_rawmsg_write,
	.llseek	= no_llseek,
};

void *dlm_create_debug_comms_file(int nodeid, void *data)
{
	struct dentry *d_node;
	char name[256];

	memset(name, 0, sizeof(name));
	snprintf(name, 256, "%d", nodeid);

	d_node = debugfs_create_dir(name, dlm_comms);
	debugfs_create_file("state", 0444, d_node, data, &dlm_state_fops);
	debugfs_create_file("flags", 0444, d_node, data, &dlm_flags_fops);
	debugfs_create_file("send_queue_count", 0444, d_node, data,
			    &dlm_send_queue_cnt_fops);
	debugfs_create_file("version", 0444, d_node, data, &dlm_version_fops);
	debugfs_create_file("rawmsg", 0200, d_node, data, &dlm_rawmsg_fops);

	return d_node;
}

void dlm_delete_debug_comms_file(void *ctx)
{
	debugfs_remove(ctx);
}

void dlm_create_debug_file(struct dlm_ls *ls)
{
	char name[DLM_LOCKSPACE_LEN + 8];

	/* format 1 */

	ls->ls_debug_rsb_dentry = debugfs_create_file(ls->ls_name,
						      S_IFREG | S_IRUGO,
						      dlm_root,
						      ls,
						      &format1_fops);

	/* format 2 */

	memset(name, 0, sizeof(name));
	snprintf(name, DLM_LOCKSPACE_LEN + 8, "%s_locks", ls->ls_name);

	ls->ls_debug_locks_dentry = debugfs_create_file(name,
							0644,
							dlm_root,
							ls,
							&format2_fops);

	/* format 3 */

	memset(name, 0, sizeof(name));
	snprintf(name, DLM_LOCKSPACE_LEN + 8, "%s_all", ls->ls_name);

	ls->ls_debug_all_dentry = debugfs_create_file(name,
						      S_IFREG | S_IRUGO,
						      dlm_root,
						      ls,
						      &format3_fops);

	/* format 4 */

	memset(name, 0, sizeof(name));
	snprintf(name, DLM_LOCKSPACE_LEN + 8, "%s_toss", ls->ls_name);

	ls->ls_debug_toss_dentry = debugfs_create_file(name,
						       S_IFREG | S_IRUGO,
						       dlm_root,
						       ls,
						       &format4_fops);

	memset(name, 0, sizeof(name));
	snprintf(name, DLM_LOCKSPACE_LEN + 8, "%s_waiters", ls->ls_name);

	ls->ls_debug_waiters_dentry = debugfs_create_file(name,
							  0644,
							  dlm_root,
							  ls,
							  &waiters_fops);
}

void __init dlm_register_debugfs(void)
{
	mutex_init(&debug_buf_lock);
	dlm_root = debugfs_create_dir("dlm", NULL);
	dlm_comms = debugfs_create_dir("comms", dlm_root);
}

void dlm_unregister_debugfs(void)
{
	debugfs_remove(dlm_root);
}

