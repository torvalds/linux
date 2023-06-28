// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "disk_groups.h"
#include "opts.h"
#include "replicas.h"
#include "sb-members.h"
#include "super-io.h"

#define x(t, n, ...) [n] = #t,
static const char * const bch2_iops_measurements[] = {
	BCH_IOPS_MEASUREMENTS()
	NULL
};

char * const bch2_member_error_strs[] = {
	BCH_MEMBER_ERROR_TYPES()
	NULL
};
#undef x

/* Code for bch_sb_field_members_v1: */

struct bch_member *bch2_members_v2_get_mut(struct bch_sb *sb, int i)
{
	return __bch2_members_v2_get_mut(bch2_sb_field_get(sb, members_v2), i);
}

static struct bch_member members_v2_get(struct bch_sb_field_members_v2 *mi, int i)
{
	struct bch_member ret, *p = __bch2_members_v2_get_mut(mi, i);
	memset(&ret, 0, sizeof(ret));
	memcpy(&ret, p, min_t(size_t, le16_to_cpu(mi->member_bytes), sizeof(ret)));
	return ret;
}

static struct bch_member *members_v1_get_mut(struct bch_sb_field_members_v1 *mi, int i)
{
	return (void *) mi->_members + (i * BCH_MEMBER_V1_BYTES);
}

static struct bch_member members_v1_get(struct bch_sb_field_members_v1 *mi, int i)
{
	struct bch_member ret, *p = members_v1_get_mut(mi, i);
	memset(&ret, 0, sizeof(ret));
	memcpy(&ret, p, min_t(size_t, BCH_MEMBER_V1_BYTES, sizeof(ret)));
	return ret;
}

struct bch_member bch2_sb_member_get(struct bch_sb *sb, int i)
{
	struct bch_sb_field_members_v2 *mi2 = bch2_sb_field_get(sb, members_v2);
	if (mi2)
		return members_v2_get(mi2, i);
	struct bch_sb_field_members_v1 *mi1 = bch2_sb_field_get(sb, members_v1);
	return members_v1_get(mi1, i);
}

static int sb_members_v2_resize_entries(struct bch_fs *c)
{
	struct bch_sb_field_members_v2 *mi = bch2_sb_field_get(c->disk_sb.sb, members_v2);

	if (le16_to_cpu(mi->member_bytes) < sizeof(struct bch_member)) {
		unsigned u64s = DIV_ROUND_UP((sizeof(*mi) + sizeof(mi->_members[0]) *
					      c->disk_sb.sb->nr_devices), 8);

		mi = bch2_sb_field_resize(&c->disk_sb, members_v2, u64s);
		if (!mi)
			return -BCH_ERR_ENOSPC_sb_members_v2;

		for (int i = c->disk_sb.sb->nr_devices - 1; i >= 0; --i) {
			void *dst = (void *) mi->_members + (i * sizeof(struct bch_member));
			memmove(dst, __bch2_members_v2_get_mut(mi, i), le16_to_cpu(mi->member_bytes));
			memset(dst + le16_to_cpu(mi->member_bytes),
			       0, (sizeof(struct bch_member) - le16_to_cpu(mi->member_bytes)));
		}
		mi->member_bytes = cpu_to_le16(sizeof(struct bch_member));
	}
	return 0;
}

int bch2_sb_members_v2_init(struct bch_fs *c)
{
	struct bch_sb_field_members_v1 *mi1;
	struct bch_sb_field_members_v2 *mi2;

	if (!bch2_sb_field_get(c->disk_sb.sb, members_v2)) {
		mi2 = bch2_sb_field_resize(&c->disk_sb, members_v2,
				DIV_ROUND_UP(sizeof(*mi2) +
					     sizeof(struct bch_member) * c->sb.nr_devices,
					     sizeof(u64)));
		mi1 = bch2_sb_field_get(c->disk_sb.sb, members_v1);
		memcpy(&mi2->_members[0], &mi1->_members[0],
		       BCH_MEMBER_V1_BYTES * c->sb.nr_devices);
		memset(&mi2->pad[0], 0, sizeof(mi2->pad));
		mi2->member_bytes = cpu_to_le16(BCH_MEMBER_V1_BYTES);
	}

	return sb_members_v2_resize_entries(c);
}

int bch2_sb_members_cpy_v2_v1(struct bch_sb_handle *disk_sb)
{
	struct bch_sb_field_members_v1 *mi1;
	struct bch_sb_field_members_v2 *mi2;

	mi1 = bch2_sb_field_resize(disk_sb, members_v1,
			DIV_ROUND_UP(sizeof(*mi1) + BCH_MEMBER_V1_BYTES *
				     disk_sb->sb->nr_devices, sizeof(u64)));
	if (!mi1)
		return -BCH_ERR_ENOSPC_sb_members;

	mi2 = bch2_sb_field_get(disk_sb->sb, members_v2);

	for (unsigned i = 0; i < disk_sb->sb->nr_devices; i++)
		memcpy(members_v1_get_mut(mi1, i), __bch2_members_v2_get_mut(mi2, i), BCH_MEMBER_V1_BYTES);

	return 0;
}

static int validate_member(struct printbuf *err,
			   struct bch_member m,
			   struct bch_sb *sb,
			   int i)
{
	if (le64_to_cpu(m.nbuckets) > LONG_MAX) {
		prt_printf(err, "device %u: too many buckets (got %llu, max %lu)",
			   i, le64_to_cpu(m.nbuckets), LONG_MAX);
		return -BCH_ERR_invalid_sb_members;
	}

	if (le64_to_cpu(m.nbuckets) -
	    le16_to_cpu(m.first_bucket) < BCH_MIN_NR_NBUCKETS) {
		prt_printf(err, "device %u: not enough buckets (got %llu, max %u)",
			   i, le64_to_cpu(m.nbuckets), BCH_MIN_NR_NBUCKETS);
		return -BCH_ERR_invalid_sb_members;
	}

	if (le16_to_cpu(m.bucket_size) <
	    le16_to_cpu(sb->block_size)) {
		prt_printf(err, "device %u: bucket size %u smaller than block size %u",
			   i, le16_to_cpu(m.bucket_size), le16_to_cpu(sb->block_size));
		return -BCH_ERR_invalid_sb_members;
	}

	if (le16_to_cpu(m.bucket_size) <
	    BCH_SB_BTREE_NODE_SIZE(sb)) {
		prt_printf(err, "device %u: bucket size %u smaller than btree node size %llu",
			   i, le16_to_cpu(m.bucket_size), BCH_SB_BTREE_NODE_SIZE(sb));
		return -BCH_ERR_invalid_sb_members;
	}

	return 0;
}

static void member_to_text(struct printbuf *out,
			   struct bch_member m,
			   struct bch_sb_field_disk_groups *gi,
			   struct bch_sb *sb,
			   int i)
{
	unsigned data_have = bch2_sb_dev_has_data(sb, i);
	u64 bucket_size = le16_to_cpu(m.bucket_size);
	u64 device_size = le64_to_cpu(m.nbuckets) * bucket_size;

	if (!bch2_member_exists(&m))
		return;

	prt_printf(out, "Device:");
	prt_tab(out);
	prt_printf(out, "%u", i);
	prt_newline(out);

	printbuf_indent_add(out, 2);

	prt_printf(out, "Label:");
	prt_tab(out);
	if (BCH_MEMBER_GROUP(&m)) {
		unsigned idx = BCH_MEMBER_GROUP(&m) - 1;

		if (idx < disk_groups_nr(gi))
			prt_printf(out, "%s (%u)",
				   gi->entries[idx].label, idx);
		else
			prt_printf(out, "(bad disk labels section)");
	} else {
		prt_printf(out, "(none)");
	}
	prt_newline(out);

	prt_printf(out, "UUID:");
	prt_tab(out);
	pr_uuid(out, m.uuid.b);
	prt_newline(out);

	prt_printf(out, "Size:");
	prt_tab(out);
	prt_units_u64(out, device_size << 9);
	prt_newline(out);

	for (unsigned i = 0; i < BCH_MEMBER_ERROR_NR; i++) {
		prt_printf(out, "%s errors:", bch2_member_error_strs[i]);
		prt_tab(out);
		prt_u64(out, le64_to_cpu(m.errors[i]));
		prt_newline(out);
	}

	for (unsigned i = 0; i < BCH_IOPS_NR; i++) {
		prt_printf(out, "%s iops:", bch2_iops_measurements[i]);
		prt_tab(out);
		prt_printf(out, "%u", le32_to_cpu(m.iops[i]));
		prt_newline(out);
	}

	prt_printf(out, "Bucket size:");
	prt_tab(out);
	prt_units_u64(out, bucket_size << 9);
	prt_newline(out);

	prt_printf(out, "First bucket:");
	prt_tab(out);
	prt_printf(out, "%u", le16_to_cpu(m.first_bucket));
	prt_newline(out);

	prt_printf(out, "Buckets:");
	prt_tab(out);
	prt_printf(out, "%llu", le64_to_cpu(m.nbuckets));
	prt_newline(out);

	prt_printf(out, "Last mount:");
	prt_tab(out);
	if (m.last_mount)
		bch2_prt_datetime(out, le64_to_cpu(m.last_mount));
	else
		prt_printf(out, "(never)");
	prt_newline(out);

	prt_printf(out, "Last superblock write:");
	prt_tab(out);
	prt_u64(out, le64_to_cpu(m.seq));
	prt_newline(out);

	prt_printf(out, "State:");
	prt_tab(out);
	prt_printf(out, "%s",
		   BCH_MEMBER_STATE(&m) < BCH_MEMBER_STATE_NR
		   ? bch2_member_states[BCH_MEMBER_STATE(&m)]
		   : "unknown");
	prt_newline(out);

	prt_printf(out, "Data allowed:");
	prt_tab(out);
	if (BCH_MEMBER_DATA_ALLOWED(&m))
		prt_bitflags(out, bch2_data_types, BCH_MEMBER_DATA_ALLOWED(&m));
	else
		prt_printf(out, "(none)");
	prt_newline(out);

	prt_printf(out, "Has data:");
	prt_tab(out);
	if (data_have)
		prt_bitflags(out, bch2_data_types, data_have);
	else
		prt_printf(out, "(none)");
	prt_newline(out);

	prt_str(out, "Durability:");
	prt_tab(out);
	prt_printf(out, "%llu", BCH_MEMBER_DURABILITY(&m));
	prt_newline(out);

	prt_printf(out, "Discard:");
	prt_tab(out);
	prt_printf(out, "%llu", BCH_MEMBER_DISCARD(&m));
	prt_newline(out);

	prt_printf(out, "Freespace initialized:");
	prt_tab(out);
	prt_printf(out, "%llu", BCH_MEMBER_FREESPACE_INITIALIZED(&m));
	prt_newline(out);

	printbuf_indent_sub(out, 2);
}

static int bch2_sb_members_v1_validate(struct bch_sb *sb,
				    struct bch_sb_field *f,
				    struct printbuf *err)
{
	struct bch_sb_field_members_v1 *mi = field_to_type(f, members_v1);
	unsigned i;

	if ((void *) members_v1_get_mut(mi, sb->nr_devices) > vstruct_end(&mi->field)) {
		prt_printf(err, "too many devices for section size");
		return -BCH_ERR_invalid_sb_members;
	}

	for (i = 0; i < sb->nr_devices; i++) {
		struct bch_member m = members_v1_get(mi, i);

		int ret = validate_member(err, m, sb, i);
		if (ret)
			return ret;
	}

	return 0;
}

static void bch2_sb_members_v1_to_text(struct printbuf *out, struct bch_sb *sb,
				       struct bch_sb_field *f)
{
	struct bch_sb_field_members_v1 *mi = field_to_type(f, members_v1);
	struct bch_sb_field_disk_groups *gi = bch2_sb_field_get(sb, disk_groups);
	unsigned i;

	for (i = 0; i < sb->nr_devices; i++)
		member_to_text(out, members_v1_get(mi, i), gi, sb, i);
}

const struct bch_sb_field_ops bch_sb_field_ops_members_v1 = {
	.validate	= bch2_sb_members_v1_validate,
	.to_text	= bch2_sb_members_v1_to_text,
};

static void bch2_sb_members_v2_to_text(struct printbuf *out, struct bch_sb *sb,
				       struct bch_sb_field *f)
{
	struct bch_sb_field_members_v2 *mi = field_to_type(f, members_v2);
	struct bch_sb_field_disk_groups *gi = bch2_sb_field_get(sb, disk_groups);
	unsigned i;

	for (i = 0; i < sb->nr_devices; i++)
		member_to_text(out, members_v2_get(mi, i), gi, sb, i);
}

static int bch2_sb_members_v2_validate(struct bch_sb *sb,
				       struct bch_sb_field *f,
				       struct printbuf *err)
{
	struct bch_sb_field_members_v2 *mi = field_to_type(f, members_v2);
	size_t mi_bytes = (void *) __bch2_members_v2_get_mut(mi, sb->nr_devices) -
		(void *) mi;

	if (mi_bytes > vstruct_bytes(&mi->field)) {
		prt_printf(err, "section too small (%zu > %zu)",
			   mi_bytes, vstruct_bytes(&mi->field));
		return -BCH_ERR_invalid_sb_members;
	}

	for (unsigned i = 0; i < sb->nr_devices; i++) {
		int ret = validate_member(err, members_v2_get(mi, i), sb, i);
		if (ret)
			return ret;
	}

	return 0;
}

const struct bch_sb_field_ops bch_sb_field_ops_members_v2 = {
	.validate	= bch2_sb_members_v2_validate,
	.to_text	= bch2_sb_members_v2_to_text,
};

void bch2_sb_members_from_cpu(struct bch_fs *c)
{
	struct bch_sb_field_members_v2 *mi = bch2_sb_field_get(c->disk_sb.sb, members_v2);

	rcu_read_lock();
	for_each_member_device_rcu(c, ca, NULL) {
		struct bch_member *m = __bch2_members_v2_get_mut(mi, ca->dev_idx);

		for (unsigned e = 0; e < BCH_MEMBER_ERROR_NR; e++)
			m->errors[e] = cpu_to_le64(atomic64_read(&ca->errors[e]));
	}
	rcu_read_unlock();
}

void bch2_dev_io_errors_to_text(struct printbuf *out, struct bch_dev *ca)
{
	struct bch_fs *c = ca->fs;
	struct bch_member m;

	mutex_lock(&ca->fs->sb_lock);
	m = bch2_sb_member_get(c->disk_sb.sb, ca->dev_idx);
	mutex_unlock(&ca->fs->sb_lock);

	printbuf_tabstop_push(out, 12);

	prt_str(out, "IO errors since filesystem creation");
	prt_newline(out);

	printbuf_indent_add(out, 2);
	for (unsigned i = 0; i < BCH_MEMBER_ERROR_NR; i++) {
		prt_printf(out, "%s:", bch2_member_error_strs[i]);
		prt_tab(out);
		prt_u64(out, atomic64_read(&ca->errors[i]));
		prt_newline(out);
	}
	printbuf_indent_sub(out, 2);

	prt_str(out, "IO errors since ");
	bch2_pr_time_units(out, (ktime_get_real_seconds() - le64_to_cpu(m.errors_reset_time)) * NSEC_PER_SEC);
	prt_str(out, " ago");
	prt_newline(out);

	printbuf_indent_add(out, 2);
	for (unsigned i = 0; i < BCH_MEMBER_ERROR_NR; i++) {
		prt_printf(out, "%s:", bch2_member_error_strs[i]);
		prt_tab(out);
		prt_u64(out, atomic64_read(&ca->errors[i]) - le64_to_cpu(m.errors_at_reset[i]));
		prt_newline(out);
	}
	printbuf_indent_sub(out, 2);
}

void bch2_dev_errors_reset(struct bch_dev *ca)
{
	struct bch_fs *c = ca->fs;
	struct bch_member *m;

	mutex_lock(&c->sb_lock);
	m = bch2_members_v2_get_mut(c->disk_sb.sb, ca->dev_idx);
	for (unsigned i = 0; i < ARRAY_SIZE(m->errors_at_reset); i++)
		m->errors_at_reset[i] = cpu_to_le64(atomic64_read(&ca->errors[i]));
	m->errors_reset_time = ktime_get_real_seconds();

	bch2_write_super(c);
	mutex_unlock(&c->sb_lock);
}
