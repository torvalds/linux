// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "disk_groups.h"
#include "replicas.h"
#include "sb-members.h"
#include "super-io.h"

/* Code for bch_sb_field_members: */
static struct bch_member *members_v1_get_mut(struct bch_sb_field_members *mi, int i)
{
	return mi->members + i;
}

static struct bch_member members_v1_get(struct bch_sb_field_members *mi, int i)
{
	struct bch_member ret, *p = members_v1_get_mut(mi, i);
	memset(&ret, 0, sizeof(ret));
	memcpy(&ret, p, min_t(size_t, sizeof(struct bch_member), sizeof(ret))); return ret;
}

struct bch_member bch2_sb_member_get(struct bch_sb *sb, int i)
{
	struct bch_sb_field_members *mi1 = bch2_sb_get_members(sb);
	return members_v1_get(mi1, i);
}

static int bch2_sb_members_validate(struct bch_sb *sb,
				    struct bch_sb_field *f,
				    struct printbuf *err)
{
	struct bch_sb_field_members *mi = field_to_type(f, members);
	unsigned i;

	if ((void *) (mi->members + sb->nr_devices) >
	    vstruct_end(&mi->field)) {
		prt_printf(err, "too many devices for section size");
		return -BCH_ERR_invalid_sb_members;
	}

	for (i = 0; i < sb->nr_devices; i++) {
		struct bch_member *m = mi->members + i;

		if (!bch2_member_exists(m))
			continue;

		if (le64_to_cpu(m->nbuckets) > LONG_MAX) {
			prt_printf(err, "device %u: too many buckets (got %llu, max %lu)",
			       i, le64_to_cpu(m->nbuckets), LONG_MAX);
			return -BCH_ERR_invalid_sb_members;
		}

		if (le64_to_cpu(m->nbuckets) -
		    le16_to_cpu(m->first_bucket) < BCH_MIN_NR_NBUCKETS) {
			prt_printf(err, "device %u: not enough buckets (got %llu, max %u)",
			       i, le64_to_cpu(m->nbuckets), BCH_MIN_NR_NBUCKETS);
			return -BCH_ERR_invalid_sb_members;
		}

		if (le16_to_cpu(m->bucket_size) <
		    le16_to_cpu(sb->block_size)) {
			prt_printf(err, "device %u: bucket size %u smaller than block size %u",
			       i, le16_to_cpu(m->bucket_size), le16_to_cpu(sb->block_size));
			return -BCH_ERR_invalid_sb_members;
		}

		if (le16_to_cpu(m->bucket_size) <
		    BCH_SB_BTREE_NODE_SIZE(sb)) {
			prt_printf(err, "device %u: bucket size %u smaller than btree node size %llu",
			       i, le16_to_cpu(m->bucket_size), BCH_SB_BTREE_NODE_SIZE(sb));
			return -BCH_ERR_invalid_sb_members;
		}
	}

	return 0;
}

static void bch2_sb_members_to_text(struct printbuf *out, struct bch_sb *sb,
				    struct bch_sb_field *f)
{
	struct bch_sb_field_members *mi = field_to_type(f, members);
	struct bch_sb_field_disk_groups *gi = bch2_sb_get_disk_groups(sb);
	unsigned i;

	for (i = 0; i < sb->nr_devices; i++) {
		struct bch_member *m = mi->members + i;
		unsigned data_have = bch2_sb_dev_has_data(sb, i);
		u64 bucket_size = le16_to_cpu(m->bucket_size);
		u64 device_size = le64_to_cpu(m->nbuckets) * bucket_size;

		if (!bch2_member_exists(m))
			continue;

		prt_printf(out, "Device:");
		prt_tab(out);
		prt_printf(out, "%u", i);
		prt_newline(out);

		printbuf_indent_add(out, 2);

		prt_printf(out, "UUID:");
		prt_tab(out);
		pr_uuid(out, m->uuid.b);
		prt_newline(out);

		prt_printf(out, "Size:");
		prt_tab(out);
		prt_units_u64(out, device_size << 9);
		prt_newline(out);

		prt_printf(out, "Bucket size:");
		prt_tab(out);
		prt_units_u64(out, bucket_size << 9);
		prt_newline(out);

		prt_printf(out, "First bucket:");
		prt_tab(out);
		prt_printf(out, "%u", le16_to_cpu(m->first_bucket));
		prt_newline(out);

		prt_printf(out, "Buckets:");
		prt_tab(out);
		prt_printf(out, "%llu", le64_to_cpu(m->nbuckets));
		prt_newline(out);

		prt_printf(out, "Last mount:");
		prt_tab(out);
		if (m->last_mount)
			pr_time(out, le64_to_cpu(m->last_mount));
		else
			prt_printf(out, "(never)");
		prt_newline(out);

		prt_printf(out, "State:");
		prt_tab(out);
		prt_printf(out, "%s",
		       BCH_MEMBER_STATE(m) < BCH_MEMBER_STATE_NR
		       ? bch2_member_states[BCH_MEMBER_STATE(m)]
		       : "unknown");
		prt_newline(out);

		prt_printf(out, "Label:");
		prt_tab(out);
		if (BCH_MEMBER_GROUP(m)) {
			unsigned idx = BCH_MEMBER_GROUP(m) - 1;

			if (idx < disk_groups_nr(gi))
				prt_printf(out, "%s (%u)",
				       gi->entries[idx].label, idx);
			else
				prt_printf(out, "(bad disk labels section)");
		} else {
			prt_printf(out, "(none)");
		}
		prt_newline(out);

		prt_printf(out, "Data allowed:");
		prt_tab(out);
		if (BCH_MEMBER_DATA_ALLOWED(m))
			prt_bitflags(out, bch2_data_types, BCH_MEMBER_DATA_ALLOWED(m));
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

		prt_printf(out, "Discard:");
		prt_tab(out);
		prt_printf(out, "%llu", BCH_MEMBER_DISCARD(m));
		prt_newline(out);

		prt_printf(out, "Freespace initialized:");
		prt_tab(out);
		prt_printf(out, "%llu", BCH_MEMBER_FREESPACE_INITIALIZED(m));
		prt_newline(out);

		printbuf_indent_sub(out, 2);
	}
}

const struct bch_sb_field_ops bch_sb_field_ops_members = {
	.validate	= bch2_sb_members_validate,
	.to_text	= bch2_sb_members_to_text,
};
