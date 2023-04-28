// SPDX-License-Identifier: GPL-2.0
#include "bcachefs.h"
#include "btree_update.h"
#include "errcode.h"
#include "error.h"
#include "inode.h"
#include "quota.h"
#include "subvolume.h"
#include "super-io.h"

static const char * const bch2_quota_types[] = {
	"user",
	"group",
	"project",
};

static const char * const bch2_quota_counters[] = {
	"space",
	"inodes",
};

static int bch2_sb_quota_validate(struct bch_sb *sb, struct bch_sb_field *f,
				  struct printbuf *err)
{
	struct bch_sb_field_quota *q = field_to_type(f, quota);

	if (vstruct_bytes(&q->field) < sizeof(*q)) {
		prt_printf(err, "wrong size (got %zu should be %zu)",
		       vstruct_bytes(&q->field), sizeof(*q));
		return -BCH_ERR_invalid_sb_quota;
	}

	return 0;
}

static void bch2_sb_quota_to_text(struct printbuf *out, struct bch_sb *sb,
				  struct bch_sb_field *f)
{
	struct bch_sb_field_quota *q = field_to_type(f, quota);
	unsigned qtyp, counter;

	for (qtyp = 0; qtyp < ARRAY_SIZE(q->q); qtyp++) {
		prt_printf(out, "%s: flags %llx",
		       bch2_quota_types[qtyp],
		       le64_to_cpu(q->q[qtyp].flags));

		for (counter = 0; counter < Q_COUNTERS; counter++)
			prt_printf(out, " %s timelimit %u warnlimit %u",
			       bch2_quota_counters[counter],
			       le32_to_cpu(q->q[qtyp].c[counter].timelimit),
			       le32_to_cpu(q->q[qtyp].c[counter].warnlimit));

		prt_newline(out);
	}
}

const struct bch_sb_field_ops bch_sb_field_ops_quota = {
	.validate	= bch2_sb_quota_validate,
	.to_text	= bch2_sb_quota_to_text,
};

int bch2_quota_invalid(const struct bch_fs *c, struct bkey_s_c k,
		       unsigned flags, struct printbuf *err)
{
	if (k.k->p.inode >= QTYP_NR) {
		prt_printf(err, "invalid quota type (%llu >= %u)",
		       k.k->p.inode, QTYP_NR);
		return -BCH_ERR_invalid_bkey;
	}

	return 0;
}

void bch2_quota_to_text(struct printbuf *out, struct bch_fs *c,
			struct bkey_s_c k)
{
	struct bkey_s_c_quota dq = bkey_s_c_to_quota(k);
	unsigned i;

	for (i = 0; i < Q_COUNTERS; i++)
		prt_printf(out, "%s hardlimit %llu softlimit %llu",
		       bch2_quota_counters[i],
		       le64_to_cpu(dq.v->c[i].hardlimit),
		       le64_to_cpu(dq.v->c[i].softlimit));
}

#ifdef CONFIG_BCACHEFS_QUOTA

#include <linux/cred.h>
#include <linux/fs.h>
#include <linux/quota.h>

static void qc_info_to_text(struct printbuf *out, struct qc_info *i)
{
	printbuf_tabstops_reset(out);
	printbuf_tabstop_push(out, 20);

	prt_str(out, "i_fieldmask");
	prt_tab(out);
	prt_printf(out, "%x", i->i_fieldmask);
	prt_newline(out);

	prt_str(out, "i_flags");
	prt_tab(out);
	prt_printf(out, "%u", i->i_flags);
	prt_newline(out);

	prt_str(out, "i_spc_timelimit");
	prt_tab(out);
	prt_printf(out, "%u", i->i_spc_timelimit);
	prt_newline(out);

	prt_str(out, "i_ino_timelimit");
	prt_tab(out);
	prt_printf(out, "%u", i->i_ino_timelimit);
	prt_newline(out);

	prt_str(out, "i_rt_spc_timelimit");
	prt_tab(out);
	prt_printf(out, "%u", i->i_rt_spc_timelimit);
	prt_newline(out);

	prt_str(out, "i_spc_warnlimit");
	prt_tab(out);
	prt_printf(out, "%u", i->i_spc_warnlimit);
	prt_newline(out);

	prt_str(out, "i_ino_warnlimit");
	prt_tab(out);
	prt_printf(out, "%u", i->i_ino_warnlimit);
	prt_newline(out);

	prt_str(out, "i_rt_spc_warnlimit");
	prt_tab(out);
	prt_printf(out, "%u", i->i_rt_spc_warnlimit);
	prt_newline(out);
}

static void qc_dqblk_to_text(struct printbuf *out, struct qc_dqblk *q)
{
	printbuf_tabstops_reset(out);
	printbuf_tabstop_push(out, 20);

	prt_str(out, "d_fieldmask");
	prt_tab(out);
	prt_printf(out, "%x", q->d_fieldmask);
	prt_newline(out);

	prt_str(out, "d_spc_hardlimit");
	prt_tab(out);
	prt_printf(out, "%llu", q->d_spc_hardlimit);
	prt_newline(out);

	prt_str(out, "d_spc_softlimit");
	prt_tab(out);
	prt_printf(out, "%llu", q->d_spc_softlimit);
	prt_newline(out);

	prt_str(out, "d_ino_hardlimit");
	prt_tab(out);
	prt_printf(out, "%llu", q->d_ino_hardlimit);
	prt_newline(out);

	prt_str(out, "d_ino_softlimit");
	prt_tab(out);
	prt_printf(out, "%llu", q->d_ino_softlimit);
	prt_newline(out);

	prt_str(out, "d_space");
	prt_tab(out);
	prt_printf(out, "%llu", q->d_space);
	prt_newline(out);

	prt_str(out, "d_ino_count");
	prt_tab(out);
	prt_printf(out, "%llu", q->d_ino_count);
	prt_newline(out);

	prt_str(out, "d_ino_timer");
	prt_tab(out);
	prt_printf(out, "%llu", q->d_ino_timer);
	prt_newline(out);

	prt_str(out, "d_spc_timer");
	prt_tab(out);
	prt_printf(out, "%llu", q->d_spc_timer);
	prt_newline(out);

	prt_str(out, "d_ino_warns");
	prt_tab(out);
	prt_printf(out, "%i", q->d_ino_warns);
	prt_newline(out);

	prt_str(out, "d_spc_warns");
	prt_tab(out);
	prt_printf(out, "%i", q->d_spc_warns);
	prt_newline(out);
}

static inline unsigned __next_qtype(unsigned i, unsigned qtypes)
{
	qtypes >>= i;
	return qtypes ? i + __ffs(qtypes) : QTYP_NR;
}

#define for_each_set_qtype(_c, _i, _q, _qtypes)				\
	for (_i = 0;							\
	     (_i = __next_qtype(_i, _qtypes),				\
	      _q = &(_c)->quotas[_i],					\
	      _i < QTYP_NR);						\
	     _i++)

static bool ignore_hardlimit(struct bch_memquota_type *q)
{
	if (capable(CAP_SYS_RESOURCE))
		return true;
#if 0
	struct mem_dqinfo *info = &sb_dqopt(dquot->dq_sb)->info[dquot->dq_id.type];

	return capable(CAP_SYS_RESOURCE) &&
	       (info->dqi_format->qf_fmt_id != QFMT_VFS_OLD ||
		!(info->dqi_flags & DQF_ROOT_SQUASH));
#endif
	return false;
}

enum quota_msg {
	SOFTWARN,	/* Softlimit reached */
	SOFTLONGWARN,	/* Grace time expired */
	HARDWARN,	/* Hardlimit reached */

	HARDBELOW,	/* Usage got below inode hardlimit */
	SOFTBELOW,	/* Usage got below inode softlimit */
};

static int quota_nl[][Q_COUNTERS] = {
	[HARDWARN][Q_SPC]	= QUOTA_NL_BHARDWARN,
	[SOFTLONGWARN][Q_SPC]	= QUOTA_NL_BSOFTLONGWARN,
	[SOFTWARN][Q_SPC]	= QUOTA_NL_BSOFTWARN,
	[HARDBELOW][Q_SPC]	= QUOTA_NL_BHARDBELOW,
	[SOFTBELOW][Q_SPC]	= QUOTA_NL_BSOFTBELOW,

	[HARDWARN][Q_INO]	= QUOTA_NL_IHARDWARN,
	[SOFTLONGWARN][Q_INO]	= QUOTA_NL_ISOFTLONGWARN,
	[SOFTWARN][Q_INO]	= QUOTA_NL_ISOFTWARN,
	[HARDBELOW][Q_INO]	= QUOTA_NL_IHARDBELOW,
	[SOFTBELOW][Q_INO]	= QUOTA_NL_ISOFTBELOW,
};

struct quota_msgs {
	u8		nr;
	struct {
		u8	qtype;
		u8	msg;
	}		m[QTYP_NR * Q_COUNTERS];
};

static void prepare_msg(unsigned qtype,
			enum quota_counters counter,
			struct quota_msgs *msgs,
			enum quota_msg msg_type)
{
	BUG_ON(msgs->nr >= ARRAY_SIZE(msgs->m));

	msgs->m[msgs->nr].qtype	= qtype;
	msgs->m[msgs->nr].msg	= quota_nl[msg_type][counter];
	msgs->nr++;
}

static void prepare_warning(struct memquota_counter *qc,
			    unsigned qtype,
			    enum quota_counters counter,
			    struct quota_msgs *msgs,
			    enum quota_msg msg_type)
{
	if (qc->warning_issued & (1 << msg_type))
		return;

	prepare_msg(qtype, counter, msgs, msg_type);
}

static void flush_warnings(struct bch_qid qid,
			   struct super_block *sb,
			   struct quota_msgs *msgs)
{
	unsigned i;

	for (i = 0; i < msgs->nr; i++)
		quota_send_warning(make_kqid(&init_user_ns, msgs->m[i].qtype, qid.q[i]),
				   sb->s_dev, msgs->m[i].msg);
}

static int bch2_quota_check_limit(struct bch_fs *c,
				  unsigned qtype,
				  struct bch_memquota *mq,
				  struct quota_msgs *msgs,
				  enum quota_counters counter,
				  s64 v,
				  enum quota_acct_mode mode)
{
	struct bch_memquota_type *q = &c->quotas[qtype];
	struct memquota_counter *qc = &mq->c[counter];
	u64 n = qc->v + v;

	BUG_ON((s64) n < 0);

	if (mode == KEY_TYPE_QUOTA_NOCHECK)
		return 0;

	if (v <= 0) {
		if (n < qc->hardlimit &&
		    (qc->warning_issued & (1 << HARDWARN))) {
			qc->warning_issued &= ~(1 << HARDWARN);
			prepare_msg(qtype, counter, msgs, HARDBELOW);
		}

		if (n < qc->softlimit &&
		    (qc->warning_issued & (1 << SOFTWARN))) {
			qc->warning_issued &= ~(1 << SOFTWARN);
			prepare_msg(qtype, counter, msgs, SOFTBELOW);
		}

		qc->warning_issued = 0;
		return 0;
	}

	if (qc->hardlimit &&
	    qc->hardlimit < n &&
	    !ignore_hardlimit(q)) {
		prepare_warning(qc, qtype, counter, msgs, HARDWARN);
		return -EDQUOT;
	}

	if (qc->softlimit &&
	    qc->softlimit < n) {
		if (qc->timer == 0) {
			qc->timer = ktime_get_real_seconds() + q->limits[counter].timelimit;
			prepare_warning(qc, qtype, counter, msgs, SOFTWARN);
		} else if (ktime_get_real_seconds() >= qc->timer &&
			   !ignore_hardlimit(q)) {
			prepare_warning(qc, qtype, counter, msgs, SOFTLONGWARN);
			return -EDQUOT;
		}
	}

	return 0;
}

int bch2_quota_acct(struct bch_fs *c, struct bch_qid qid,
		    enum quota_counters counter, s64 v,
		    enum quota_acct_mode mode)
{
	unsigned qtypes = enabled_qtypes(c);
	struct bch_memquota_type *q;
	struct bch_memquota *mq[QTYP_NR];
	struct quota_msgs msgs;
	unsigned i;
	int ret = 0;

	memset(&msgs, 0, sizeof(msgs));

	for_each_set_qtype(c, i, q, qtypes) {
		mq[i] = genradix_ptr_alloc(&q->table, qid.q[i], GFP_KERNEL);
		if (!mq[i])
			return -ENOMEM;
	}

	for_each_set_qtype(c, i, q, qtypes)
		mutex_lock_nested(&q->lock, i);

	for_each_set_qtype(c, i, q, qtypes) {
		ret = bch2_quota_check_limit(c, i, mq[i], &msgs, counter, v, mode);
		if (ret)
			goto err;
	}

	for_each_set_qtype(c, i, q, qtypes)
		mq[i]->c[counter].v += v;
err:
	for_each_set_qtype(c, i, q, qtypes)
		mutex_unlock(&q->lock);

	flush_warnings(qid, c->vfs_sb, &msgs);

	return ret;
}

static void __bch2_quota_transfer(struct bch_memquota *src_q,
				  struct bch_memquota *dst_q,
				  enum quota_counters counter, s64 v)
{
	BUG_ON(v > src_q->c[counter].v);
	BUG_ON(v + dst_q->c[counter].v < v);

	src_q->c[counter].v -= v;
	dst_q->c[counter].v += v;
}

int bch2_quota_transfer(struct bch_fs *c, unsigned qtypes,
			struct bch_qid dst,
			struct bch_qid src, u64 space,
			enum quota_acct_mode mode)
{
	struct bch_memquota_type *q;
	struct bch_memquota *src_q[3], *dst_q[3];
	struct quota_msgs msgs;
	unsigned i;
	int ret = 0;

	qtypes &= enabled_qtypes(c);

	memset(&msgs, 0, sizeof(msgs));

	for_each_set_qtype(c, i, q, qtypes) {
		src_q[i] = genradix_ptr_alloc(&q->table, src.q[i], GFP_KERNEL);
		dst_q[i] = genradix_ptr_alloc(&q->table, dst.q[i], GFP_KERNEL);
		if (!src_q[i] || !dst_q[i])
			return -ENOMEM;
	}

	for_each_set_qtype(c, i, q, qtypes)
		mutex_lock_nested(&q->lock, i);

	for_each_set_qtype(c, i, q, qtypes) {
		ret = bch2_quota_check_limit(c, i, dst_q[i], &msgs, Q_SPC,
					     dst_q[i]->c[Q_SPC].v + space,
					     mode);
		if (ret)
			goto err;

		ret = bch2_quota_check_limit(c, i, dst_q[i], &msgs, Q_INO,
					     dst_q[i]->c[Q_INO].v + 1,
					     mode);
		if (ret)
			goto err;
	}

	for_each_set_qtype(c, i, q, qtypes) {
		__bch2_quota_transfer(src_q[i], dst_q[i], Q_SPC, space);
		__bch2_quota_transfer(src_q[i], dst_q[i], Q_INO, 1);
	}

err:
	for_each_set_qtype(c, i, q, qtypes)
		mutex_unlock(&q->lock);

	flush_warnings(dst, c->vfs_sb, &msgs);

	return ret;
}

static int __bch2_quota_set(struct bch_fs *c, struct bkey_s_c k,
			    struct qc_dqblk *qdq)
{
	struct bkey_s_c_quota dq;
	struct bch_memquota_type *q;
	struct bch_memquota *mq;
	unsigned i;

	BUG_ON(k.k->p.inode >= QTYP_NR);

	if (!((1U << k.k->p.inode) & enabled_qtypes(c)))
		return 0;

	switch (k.k->type) {
	case KEY_TYPE_quota:
		dq = bkey_s_c_to_quota(k);
		q = &c->quotas[k.k->p.inode];

		mutex_lock(&q->lock);
		mq = genradix_ptr_alloc(&q->table, k.k->p.offset, GFP_KERNEL);
		if (!mq) {
			mutex_unlock(&q->lock);
			return -ENOMEM;
		}

		for (i = 0; i < Q_COUNTERS; i++) {
			mq->c[i].hardlimit = le64_to_cpu(dq.v->c[i].hardlimit);
			mq->c[i].softlimit = le64_to_cpu(dq.v->c[i].softlimit);
		}

		if (qdq && qdq->d_fieldmask & QC_SPC_TIMER)
			mq->c[Q_SPC].timer	= cpu_to_le64(qdq->d_spc_timer);
		if (qdq && qdq->d_fieldmask & QC_SPC_WARNS)
			mq->c[Q_SPC].warns	= cpu_to_le64(qdq->d_spc_warns);
		if (qdq && qdq->d_fieldmask & QC_INO_TIMER)
			mq->c[Q_INO].timer	= cpu_to_le64(qdq->d_ino_timer);
		if (qdq && qdq->d_fieldmask & QC_INO_WARNS)
			mq->c[Q_INO].warns	= cpu_to_le64(qdq->d_ino_warns);

		mutex_unlock(&q->lock);
	}

	return 0;
}

void bch2_fs_quota_exit(struct bch_fs *c)
{
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(c->quotas); i++)
		genradix_free(&c->quotas[i].table);
}

void bch2_fs_quota_init(struct bch_fs *c)
{
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(c->quotas); i++)
		mutex_init(&c->quotas[i].lock);
}

static struct bch_sb_field_quota *bch2_sb_get_or_create_quota(struct bch_sb_handle *sb)
{
	struct bch_sb_field_quota *sb_quota = bch2_sb_get_quota(sb->sb);

	if (sb_quota)
		return sb_quota;

	sb_quota = bch2_sb_resize_quota(sb, sizeof(*sb_quota) / sizeof(u64));
	if (sb_quota) {
		unsigned qtype, qc;

		for (qtype = 0; qtype < QTYP_NR; qtype++)
			for (qc = 0; qc < Q_COUNTERS; qc++)
				sb_quota->q[qtype].c[qc].timelimit =
					cpu_to_le32(7 * 24 * 60 * 60);
	}

	return sb_quota;
}

static void bch2_sb_quota_read(struct bch_fs *c)
{
	struct bch_sb_field_quota *sb_quota;
	unsigned i, j;

	sb_quota = bch2_sb_get_quota(c->disk_sb.sb);
	if (!sb_quota)
		return;

	for (i = 0; i < QTYP_NR; i++) {
		struct bch_memquota_type *q = &c->quotas[i];

		for (j = 0; j < Q_COUNTERS; j++) {
			q->limits[j].timelimit =
				le32_to_cpu(sb_quota->q[i].c[j].timelimit);
			q->limits[j].warnlimit =
				le32_to_cpu(sb_quota->q[i].c[j].warnlimit);
		}
	}
}

static int bch2_fs_quota_read_inode(struct btree_trans *trans,
				    struct btree_iter *iter,
				    struct bkey_s_c k)
{
	struct bch_fs *c = trans->c;
	struct bch_inode_unpacked u;
	struct bch_snapshot_tree s_t;
	int ret;

	ret = bch2_snapshot_tree_lookup(trans,
			snapshot_t(c, k.k->p.snapshot)->tree, &s_t);
	bch2_fs_inconsistent_on(bch2_err_matches(ret, ENOENT), c,
			"%s: snapshot tree %u not found", __func__,
			snapshot_t(c, k.k->p.snapshot)->tree);
	if (ret)
		return ret;

	if (!s_t.master_subvol)
		goto advance;

	ret = bch2_inode_find_by_inum_trans(trans,
				(subvol_inum) {
					le32_to_cpu(s_t.master_subvol),
					k.k->p.offset,
				}, &u);
	if (ret)
		return ret;

	bch2_quota_acct(c, bch_qid(&u), Q_SPC, u.bi_sectors,
			KEY_TYPE_QUOTA_NOCHECK);
	bch2_quota_acct(c, bch_qid(&u), Q_INO, 1,
			KEY_TYPE_QUOTA_NOCHECK);
advance:
	bch2_btree_iter_set_pos(iter, bpos_nosnap_successor(iter->pos));
	return 0;
}

int bch2_fs_quota_read(struct bch_fs *c)
{
	struct bch_sb_field_quota *sb_quota;
	struct btree_trans trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret;

	mutex_lock(&c->sb_lock);
	sb_quota = bch2_sb_get_or_create_quota(&c->disk_sb);
	if (!sb_quota) {
		mutex_unlock(&c->sb_lock);
		return -BCH_ERR_ENOSPC_sb_quota;
	}

	bch2_sb_quota_read(c);
	mutex_unlock(&c->sb_lock);

	bch2_trans_init(&trans, c, 0, 0);

	ret = for_each_btree_key2(&trans, iter, BTREE_ID_quotas,
			POS_MIN, BTREE_ITER_PREFETCH, k,
		__bch2_quota_set(c, k, NULL)) ?:
	      for_each_btree_key2(&trans, iter, BTREE_ID_inodes,
			POS_MIN, BTREE_ITER_PREFETCH|BTREE_ITER_ALL_SNAPSHOTS, k,
		bch2_fs_quota_read_inode(&trans, &iter, k));
	if (ret)
		bch_err(c, "err in quota_read: %s", bch2_err_str(ret));

	bch2_trans_exit(&trans);
	return ret;
}

/* Enable/disable/delete quotas for an entire filesystem: */

static int bch2_quota_enable(struct super_block	*sb, unsigned uflags)
{
	struct bch_fs *c = sb->s_fs_info;
	struct bch_sb_field_quota *sb_quota;
	int ret = 0;

	if (sb->s_flags & SB_RDONLY)
		return -EROFS;

	/* Accounting must be enabled at mount time: */
	if (uflags & (FS_QUOTA_UDQ_ACCT|FS_QUOTA_GDQ_ACCT|FS_QUOTA_PDQ_ACCT))
		return -EINVAL;

	/* Can't enable enforcement without accounting: */
	if ((uflags & FS_QUOTA_UDQ_ENFD) && !c->opts.usrquota)
		return -EINVAL;

	if ((uflags & FS_QUOTA_GDQ_ENFD) && !c->opts.grpquota)
		return -EINVAL;

	if (uflags & FS_QUOTA_PDQ_ENFD && !c->opts.prjquota)
		return -EINVAL;

	mutex_lock(&c->sb_lock);
	sb_quota = bch2_sb_get_or_create_quota(&c->disk_sb);
	if (!sb_quota) {
		ret = -BCH_ERR_ENOSPC_sb_quota;
		goto unlock;
	}

	if (uflags & FS_QUOTA_UDQ_ENFD)
		SET_BCH_SB_USRQUOTA(c->disk_sb.sb, true);

	if (uflags & FS_QUOTA_GDQ_ENFD)
		SET_BCH_SB_GRPQUOTA(c->disk_sb.sb, true);

	if (uflags & FS_QUOTA_PDQ_ENFD)
		SET_BCH_SB_PRJQUOTA(c->disk_sb.sb, true);

	bch2_write_super(c);
unlock:
	mutex_unlock(&c->sb_lock);

	return bch2_err_class(ret);
}

static int bch2_quota_disable(struct super_block *sb, unsigned uflags)
{
	struct bch_fs *c = sb->s_fs_info;

	if (sb->s_flags & SB_RDONLY)
		return -EROFS;

	mutex_lock(&c->sb_lock);
	if (uflags & FS_QUOTA_UDQ_ENFD)
		SET_BCH_SB_USRQUOTA(c->disk_sb.sb, false);

	if (uflags & FS_QUOTA_GDQ_ENFD)
		SET_BCH_SB_GRPQUOTA(c->disk_sb.sb, false);

	if (uflags & FS_QUOTA_PDQ_ENFD)
		SET_BCH_SB_PRJQUOTA(c->disk_sb.sb, false);

	bch2_write_super(c);
	mutex_unlock(&c->sb_lock);

	return 0;
}

static int bch2_quota_remove(struct super_block *sb, unsigned uflags)
{
	struct bch_fs *c = sb->s_fs_info;
	int ret;

	if (sb->s_flags & SB_RDONLY)
		return -EROFS;

	if (uflags & FS_USER_QUOTA) {
		if (c->opts.usrquota)
			return -EINVAL;

		ret = bch2_btree_delete_range(c, BTREE_ID_quotas,
					      POS(QTYP_USR, 0),
					      POS(QTYP_USR, U64_MAX),
					      0, NULL);
		if (ret)
			return ret;
	}

	if (uflags & FS_GROUP_QUOTA) {
		if (c->opts.grpquota)
			return -EINVAL;

		ret = bch2_btree_delete_range(c, BTREE_ID_quotas,
					      POS(QTYP_GRP, 0),
					      POS(QTYP_GRP, U64_MAX),
					      0, NULL);
		if (ret)
			return ret;
	}

	if (uflags & FS_PROJ_QUOTA) {
		if (c->opts.prjquota)
			return -EINVAL;

		ret = bch2_btree_delete_range(c, BTREE_ID_quotas,
					      POS(QTYP_PRJ, 0),
					      POS(QTYP_PRJ, U64_MAX),
					      0, NULL);
		if (ret)
			return ret;
	}

	return 0;
}

/*
 * Return quota status information, such as enforcements, quota file inode
 * numbers etc.
 */
static int bch2_quota_get_state(struct super_block *sb, struct qc_state *state)
{
	struct bch_fs *c = sb->s_fs_info;
	unsigned qtypes = enabled_qtypes(c);
	unsigned i;

	memset(state, 0, sizeof(*state));

	for (i = 0; i < QTYP_NR; i++) {
		state->s_state[i].flags |= QCI_SYSFILE;

		if (!(qtypes & (1 << i)))
			continue;

		state->s_state[i].flags |= QCI_ACCT_ENABLED;

		state->s_state[i].spc_timelimit = c->quotas[i].limits[Q_SPC].timelimit;
		state->s_state[i].spc_warnlimit = c->quotas[i].limits[Q_SPC].warnlimit;

		state->s_state[i].ino_timelimit = c->quotas[i].limits[Q_INO].timelimit;
		state->s_state[i].ino_warnlimit = c->quotas[i].limits[Q_INO].warnlimit;
	}

	return 0;
}

/*
 * Adjust quota timers & warnings
 */
static int bch2_quota_set_info(struct super_block *sb, int type,
			       struct qc_info *info)
{
	struct bch_fs *c = sb->s_fs_info;
	struct bch_sb_field_quota *sb_quota;
	struct bch_memquota_type *q;
	int ret = 0;

	if (0) {
		struct printbuf buf = PRINTBUF;

		qc_info_to_text(&buf, info);
		pr_info("setting:\n%s", buf.buf);
		printbuf_exit(&buf);
	}

	if (sb->s_flags & SB_RDONLY)
		return -EROFS;

	if (type >= QTYP_NR)
		return -EINVAL;

	if (!((1 << type) & enabled_qtypes(c)))
		return -ESRCH;

	if (info->i_fieldmask &
	    ~(QC_SPC_TIMER|QC_INO_TIMER|QC_SPC_WARNS|QC_INO_WARNS))
		return -EINVAL;

	q = &c->quotas[type];

	mutex_lock(&c->sb_lock);
	sb_quota = bch2_sb_get_or_create_quota(&c->disk_sb);
	if (!sb_quota) {
		ret = -BCH_ERR_ENOSPC_sb_quota;
		goto unlock;
	}

	if (info->i_fieldmask & QC_SPC_TIMER)
		sb_quota->q[type].c[Q_SPC].timelimit =
			cpu_to_le32(info->i_spc_timelimit);

	if (info->i_fieldmask & QC_SPC_WARNS)
		sb_quota->q[type].c[Q_SPC].warnlimit =
			cpu_to_le32(info->i_spc_warnlimit);

	if (info->i_fieldmask & QC_INO_TIMER)
		sb_quota->q[type].c[Q_INO].timelimit =
			cpu_to_le32(info->i_ino_timelimit);

	if (info->i_fieldmask & QC_INO_WARNS)
		sb_quota->q[type].c[Q_INO].warnlimit =
			cpu_to_le32(info->i_ino_warnlimit);

	bch2_sb_quota_read(c);

	bch2_write_super(c);
unlock:
	mutex_unlock(&c->sb_lock);

	return bch2_err_class(ret);
}

/* Get/set individual quotas: */

static void __bch2_quota_get(struct qc_dqblk *dst, struct bch_memquota *src)
{
	dst->d_space		= src->c[Q_SPC].v << 9;
	dst->d_spc_hardlimit	= src->c[Q_SPC].hardlimit << 9;
	dst->d_spc_softlimit	= src->c[Q_SPC].softlimit << 9;
	dst->d_spc_timer	= src->c[Q_SPC].timer;
	dst->d_spc_warns	= src->c[Q_SPC].warns;

	dst->d_ino_count	= src->c[Q_INO].v;
	dst->d_ino_hardlimit	= src->c[Q_INO].hardlimit;
	dst->d_ino_softlimit	= src->c[Q_INO].softlimit;
	dst->d_ino_timer	= src->c[Q_INO].timer;
	dst->d_ino_warns	= src->c[Q_INO].warns;
}

static int bch2_get_quota(struct super_block *sb, struct kqid kqid,
			  struct qc_dqblk *qdq)
{
	struct bch_fs *c		= sb->s_fs_info;
	struct bch_memquota_type *q	= &c->quotas[kqid.type];
	qid_t qid			= from_kqid(&init_user_ns, kqid);
	struct bch_memquota *mq;

	memset(qdq, 0, sizeof(*qdq));

	mutex_lock(&q->lock);
	mq = genradix_ptr(&q->table, qid);
	if (mq)
		__bch2_quota_get(qdq, mq);
	mutex_unlock(&q->lock);

	return 0;
}

static int bch2_get_next_quota(struct super_block *sb, struct kqid *kqid,
			       struct qc_dqblk *qdq)
{
	struct bch_fs *c		= sb->s_fs_info;
	struct bch_memquota_type *q	= &c->quotas[kqid->type];
	qid_t qid			= from_kqid(&init_user_ns, *kqid);
	struct genradix_iter iter;
	struct bch_memquota *mq;
	int ret = 0;

	mutex_lock(&q->lock);

	genradix_for_each_from(&q->table, iter, mq, qid)
		if (memcmp(mq, page_address(ZERO_PAGE(0)), sizeof(*mq))) {
			__bch2_quota_get(qdq, mq);
			*kqid = make_kqid(current_user_ns(), kqid->type, iter.pos);
			goto found;
		}

	ret = -ENOENT;
found:
	mutex_unlock(&q->lock);
	return ret;
}

static int bch2_set_quota_trans(struct btree_trans *trans,
				struct bkey_i_quota *new_quota,
				struct qc_dqblk *qdq)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret;

	k = bch2_bkey_get_iter(trans, &iter, BTREE_ID_quotas, new_quota->k.p,
			       BTREE_ITER_SLOTS|BTREE_ITER_INTENT);
	ret = bkey_err(k);
	if (unlikely(ret))
		return ret;

	if (k.k->type == KEY_TYPE_quota)
		new_quota->v = *bkey_s_c_to_quota(k).v;

	if (qdq->d_fieldmask & QC_SPC_SOFT)
		new_quota->v.c[Q_SPC].softlimit = cpu_to_le64(qdq->d_spc_softlimit >> 9);
	if (qdq->d_fieldmask & QC_SPC_HARD)
		new_quota->v.c[Q_SPC].hardlimit = cpu_to_le64(qdq->d_spc_hardlimit >> 9);

	if (qdq->d_fieldmask & QC_INO_SOFT)
		new_quota->v.c[Q_INO].softlimit = cpu_to_le64(qdq->d_ino_softlimit);
	if (qdq->d_fieldmask & QC_INO_HARD)
		new_quota->v.c[Q_INO].hardlimit = cpu_to_le64(qdq->d_ino_hardlimit);

	ret = bch2_trans_update(trans, &iter, &new_quota->k_i, 0);
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static int bch2_set_quota(struct super_block *sb, struct kqid qid,
			  struct qc_dqblk *qdq)
{
	struct bch_fs *c = sb->s_fs_info;
	struct bkey_i_quota new_quota;
	int ret;

	if (0) {
		struct printbuf buf = PRINTBUF;

		qc_dqblk_to_text(&buf, qdq);
		pr_info("setting:\n%s", buf.buf);
		printbuf_exit(&buf);
	}

	if (sb->s_flags & SB_RDONLY)
		return -EROFS;

	bkey_quota_init(&new_quota.k_i);
	new_quota.k.p = POS(qid.type, from_kqid(&init_user_ns, qid));

	ret = bch2_trans_do(c, NULL, NULL, 0,
			    bch2_set_quota_trans(&trans, &new_quota, qdq)) ?:
		__bch2_quota_set(c, bkey_i_to_s_c(&new_quota.k_i), qdq);

	return ret;
}

const struct quotactl_ops bch2_quotactl_operations = {
	.quota_enable		= bch2_quota_enable,
	.quota_disable		= bch2_quota_disable,
	.rm_xquota		= bch2_quota_remove,

	.get_state		= bch2_quota_get_state,
	.set_info		= bch2_quota_set_info,

	.get_dqblk		= bch2_get_quota,
	.get_nextdqblk		= bch2_get_next_quota,
	.set_dqblk		= bch2_set_quota,
};

#endif /* CONFIG_BCACHEFS_QUOTA */
