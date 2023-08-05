// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "checksum.h"
#include "counters.h"
#include "disk_groups.h"
#include "ec.h"
#include "error.h"
#include "io.h"
#include "journal.h"
#include "journal_sb.h"
#include "journal_seq_blacklist.h"
#include "recovery.h"
#include "replicas.h"
#include "quota.h"
#include "sb-clean.h"
#include "sb-members.h"
#include "super-io.h"
#include "super.h"
#include "trace.h"
#include "vstructs.h"

#include <linux/backing-dev.h>
#include <linux/sort.h>

static const struct blk_holder_ops bch2_sb_handle_bdev_ops = {
};

struct bch2_metadata_version {
	u16		version;
	const char	*name;
	u64		recovery_passes;
};

static const struct bch2_metadata_version bch2_metadata_versions[] = {
#define x(n, v, _recovery_passes) {		\
	.version = v,				\
	.name = #n,				\
	.recovery_passes = _recovery_passes,	\
},
	BCH_METADATA_VERSIONS()
#undef x
};

void bch2_version_to_text(struct printbuf *out, unsigned v)
{
	const char *str = "(unknown version)";

	for (unsigned i = 0; i < ARRAY_SIZE(bch2_metadata_versions); i++)
		if (bch2_metadata_versions[i].version == v) {
			str = bch2_metadata_versions[i].name;
			break;
		}

	prt_printf(out, "%u.%u: %s", BCH_VERSION_MAJOR(v), BCH_VERSION_MINOR(v), str);
}

unsigned bch2_latest_compatible_version(unsigned v)
{
	if (!BCH_VERSION_MAJOR(v))
		return v;

	for (unsigned i = 0; i < ARRAY_SIZE(bch2_metadata_versions); i++)
		if (bch2_metadata_versions[i].version > v &&
		    BCH_VERSION_MAJOR(bch2_metadata_versions[i].version) ==
		    BCH_VERSION_MAJOR(v))
			v = bch2_metadata_versions[i].version;

	return v;
}

u64 bch2_upgrade_recovery_passes(struct bch_fs *c,
				 unsigned old_version,
				 unsigned new_version)
{
	u64 ret = 0;

	for (const struct bch2_metadata_version *i = bch2_metadata_versions;
	     i < bch2_metadata_versions + ARRAY_SIZE(bch2_metadata_versions);
	     i++)
		if (i->version > old_version && i->version <= new_version) {
			if (i->recovery_passes & RECOVERY_PASS_ALL_FSCK)
				ret |= bch2_fsck_recovery_passes();
			ret |= i->recovery_passes;
		}

	return ret &= ~RECOVERY_PASS_ALL_FSCK;
}

const char * const bch2_sb_fields[] = {
#define x(name, nr)	#name,
	BCH_SB_FIELDS()
#undef x
	NULL
};

static int bch2_sb_field_validate(struct bch_sb *, struct bch_sb_field *,
				  struct printbuf *);

struct bch_sb_field *bch2_sb_field_get(struct bch_sb *sb,
				      enum bch_sb_field_type type)
{
	struct bch_sb_field *f;

	/* XXX: need locking around superblock to access optional fields */

	vstruct_for_each(sb, f)
		if (le32_to_cpu(f->type) == type)
			return f;
	return NULL;
}

static struct bch_sb_field *__bch2_sb_field_resize(struct bch_sb_handle *sb,
						   struct bch_sb_field *f,
						   unsigned u64s)
{
	unsigned old_u64s = f ? le32_to_cpu(f->u64s) : 0;
	unsigned sb_u64s = le32_to_cpu(sb->sb->u64s) + u64s - old_u64s;

	BUG_ON(__vstruct_bytes(struct bch_sb, sb_u64s) > sb->buffer_size);

	if (!f && !u64s) {
		/* nothing to do: */
	} else if (!f) {
		f = vstruct_last(sb->sb);
		memset(f, 0, sizeof(u64) * u64s);
		f->u64s = cpu_to_le32(u64s);
		f->type = 0;
	} else {
		void *src, *dst;

		src = vstruct_end(f);

		if (u64s) {
			f->u64s = cpu_to_le32(u64s);
			dst = vstruct_end(f);
		} else {
			dst = f;
		}

		memmove(dst, src, vstruct_end(sb->sb) - src);

		if (dst > src)
			memset(src, 0, dst - src);
	}

	sb->sb->u64s = cpu_to_le32(sb_u64s);

	return u64s ? f : NULL;
}

void bch2_sb_field_delete(struct bch_sb_handle *sb,
			  enum bch_sb_field_type type)
{
	struct bch_sb_field *f = bch2_sb_field_get(sb->sb, type);

	if (f)
		__bch2_sb_field_resize(sb, f, 0);
}

/* Superblock realloc/free: */

void bch2_free_super(struct bch_sb_handle *sb)
{
	kfree(sb->bio);
	if (!IS_ERR_OR_NULL(sb->bdev))
		blkdev_put(sb->bdev, sb->holder);
	kfree(sb->holder);

	kfree(sb->sb);
	memset(sb, 0, sizeof(*sb));
}

int bch2_sb_realloc(struct bch_sb_handle *sb, unsigned u64s)
{
	size_t new_bytes = __vstruct_bytes(struct bch_sb, u64s);
	size_t new_buffer_size;
	struct bch_sb *new_sb;
	struct bio *bio;

	if (sb->bdev)
		new_bytes = max_t(size_t, new_bytes, bdev_logical_block_size(sb->bdev));

	new_buffer_size = roundup_pow_of_two(new_bytes);

	if (sb->sb && sb->buffer_size >= new_buffer_size)
		return 0;

	if (sb->have_layout) {
		u64 max_bytes = 512 << sb->sb->layout.sb_max_size_bits;

		if (new_bytes > max_bytes) {
			pr_err("%pg: superblock too big: want %zu but have %llu",
			       sb->bdev, new_bytes, max_bytes);
			return -BCH_ERR_ENOSPC_sb;
		}
	}

	if (sb->buffer_size >= new_buffer_size && sb->sb)
		return 0;

	if (dynamic_fault("bcachefs:add:super_realloc"))
		return -BCH_ERR_ENOMEM_sb_realloc_injected;

	new_sb = krealloc(sb->sb, new_buffer_size, GFP_NOFS|__GFP_ZERO);
	if (!new_sb)
		return -BCH_ERR_ENOMEM_sb_buf_realloc;

	sb->sb = new_sb;

	if (sb->have_bio) {
		unsigned nr_bvecs = buf_pages(sb->sb, new_buffer_size);

		bio = bio_kmalloc(nr_bvecs, GFP_KERNEL);
		if (!bio)
			return -BCH_ERR_ENOMEM_sb_bio_realloc;

		bio_init(bio, NULL, bio->bi_inline_vecs, nr_bvecs, 0);

		kfree(sb->bio);
		sb->bio = bio;
	}

	sb->buffer_size = new_buffer_size;

	return 0;
}

struct bch_sb_field *bch2_sb_field_resize(struct bch_sb_handle *sb,
					  enum bch_sb_field_type type,
					  unsigned u64s)
{
	struct bch_sb_field *f = bch2_sb_field_get(sb->sb, type);
	ssize_t old_u64s = f ? le32_to_cpu(f->u64s) : 0;
	ssize_t d = -old_u64s + u64s;

	if (bch2_sb_realloc(sb, le32_to_cpu(sb->sb->u64s) + d))
		return NULL;

	if (sb->fs_sb) {
		struct bch_fs *c = container_of(sb, struct bch_fs, disk_sb);
		struct bch_dev *ca;
		unsigned i;

		lockdep_assert_held(&c->sb_lock);

		/* XXX: we're not checking that offline device have enough space */

		for_each_online_member(ca, c, i) {
			struct bch_sb_handle *sb = &ca->disk_sb;

			if (bch2_sb_realloc(sb, le32_to_cpu(sb->sb->u64s) + d)) {
				percpu_ref_put(&ca->ref);
				return NULL;
			}
		}
	}

	f = bch2_sb_field_get(sb->sb, type);
	f = __bch2_sb_field_resize(sb, f, u64s);
	if (f)
		f->type = cpu_to_le32(type);
	return f;
}

/* Superblock validate: */

static int validate_sb_layout(struct bch_sb_layout *layout, struct printbuf *out)
{
	u64 offset, prev_offset, max_sectors;
	unsigned i;

	BUILD_BUG_ON(sizeof(struct bch_sb_layout) != 512);

	if (!uuid_equal(&layout->magic, &BCACHE_MAGIC) &&
	    !uuid_equal(&layout->magic, &BCHFS_MAGIC)) {
		prt_printf(out, "Not a bcachefs superblock layout");
		return -BCH_ERR_invalid_sb_layout;
	}

	if (layout->layout_type != 0) {
		prt_printf(out, "Invalid superblock layout type %u",
		       layout->layout_type);
		return -BCH_ERR_invalid_sb_layout_type;
	}

	if (!layout->nr_superblocks) {
		prt_printf(out, "Invalid superblock layout: no superblocks");
		return -BCH_ERR_invalid_sb_layout_nr_superblocks;
	}

	if (layout->nr_superblocks > ARRAY_SIZE(layout->sb_offset)) {
		prt_printf(out, "Invalid superblock layout: too many superblocks");
		return -BCH_ERR_invalid_sb_layout_nr_superblocks;
	}

	max_sectors = 1 << layout->sb_max_size_bits;

	prev_offset = le64_to_cpu(layout->sb_offset[0]);

	for (i = 1; i < layout->nr_superblocks; i++) {
		offset = le64_to_cpu(layout->sb_offset[i]);

		if (offset < prev_offset + max_sectors) {
			prt_printf(out, "Invalid superblock layout: superblocks overlap\n"
			       "  (sb %u ends at %llu next starts at %llu",
			       i - 1, prev_offset + max_sectors, offset);
			return -BCH_ERR_invalid_sb_layout_superblocks_overlap;
		}
		prev_offset = offset;
	}

	return 0;
}

static int bch2_sb_compatible(struct bch_sb *sb, struct printbuf *out)
{
	u16 version		= le16_to_cpu(sb->version);
	u16 version_min		= le16_to_cpu(sb->version_min);

	if (!bch2_version_compatible(version)) {
		prt_str(out, "Unsupported superblock version ");
		bch2_version_to_text(out, version);
		prt_str(out, " (min ");
		bch2_version_to_text(out, bcachefs_metadata_version_min);
		prt_str(out, ", max ");
		bch2_version_to_text(out, bcachefs_metadata_version_current);
		prt_str(out, ")");
		return -BCH_ERR_invalid_sb_version;
	}

	if (!bch2_version_compatible(version_min)) {
		prt_str(out, "Unsupported superblock version_min ");
		bch2_version_to_text(out, version_min);
		prt_str(out, " (min ");
		bch2_version_to_text(out, bcachefs_metadata_version_min);
		prt_str(out, ", max ");
		bch2_version_to_text(out, bcachefs_metadata_version_current);
		prt_str(out, ")");
		return -BCH_ERR_invalid_sb_version;
	}

	if (version_min > version) {
		prt_str(out, "Bad minimum version ");
		bch2_version_to_text(out, version_min);
		prt_str(out, ", greater than version field ");
		bch2_version_to_text(out, version);
		return -BCH_ERR_invalid_sb_version;
	}

	return 0;
}

static int bch2_sb_validate(struct bch_sb_handle *disk_sb, struct printbuf *out,
			    int rw)
{
	struct bch_sb *sb = disk_sb->sb;
	struct bch_sb_field *f;
	struct bch_sb_field_members *mi;
	enum bch_opt_id opt_id;
	u16 block_size;
	int ret;

	ret = bch2_sb_compatible(sb, out);
	if (ret)
		return ret;

	if (sb->features[1] ||
	    (le64_to_cpu(sb->features[0]) & (~0ULL << BCH_FEATURE_NR))) {
		prt_printf(out, "Filesystem has incompatible features");
		return -BCH_ERR_invalid_sb_features;
	}

	block_size = le16_to_cpu(sb->block_size);

	if (block_size > PAGE_SECTORS) {
		prt_printf(out, "Block size too big (got %u, max %u)",
		       block_size, PAGE_SECTORS);
		return -BCH_ERR_invalid_sb_block_size;
	}

	if (bch2_is_zero(sb->user_uuid.b, sizeof(sb->user_uuid))) {
		prt_printf(out, "Bad user UUID (got zeroes)");
		return -BCH_ERR_invalid_sb_uuid;
	}

	if (bch2_is_zero(sb->uuid.b, sizeof(sb->uuid))) {
		prt_printf(out, "Bad intenal UUID (got zeroes)");
		return -BCH_ERR_invalid_sb_uuid;
	}

	if (!sb->nr_devices ||
	    sb->nr_devices > BCH_SB_MEMBERS_MAX) {
		prt_printf(out, "Bad number of member devices %u (max %u)",
		       sb->nr_devices, BCH_SB_MEMBERS_MAX);
		return -BCH_ERR_invalid_sb_too_many_members;
	}

	if (sb->dev_idx >= sb->nr_devices) {
		prt_printf(out, "Bad dev_idx (got %u, nr_devices %u)",
		       sb->dev_idx, sb->nr_devices);
		return -BCH_ERR_invalid_sb_dev_idx;
	}

	if (!sb->time_precision ||
	    le32_to_cpu(sb->time_precision) > NSEC_PER_SEC) {
		prt_printf(out, "Invalid time precision: %u (min 1, max %lu)",
		       le32_to_cpu(sb->time_precision), NSEC_PER_SEC);
		return -BCH_ERR_invalid_sb_time_precision;
	}

	if (rw == READ) {
		/*
		 * Been seeing a bug where these are getting inexplicably
		 * zeroed, so we're now validating them, but we have to be
		 * careful not to preven people's filesystems from mounting:
		 */
		if (!BCH_SB_JOURNAL_FLUSH_DELAY(sb))
			SET_BCH_SB_JOURNAL_FLUSH_DELAY(sb, 1000);
		if (!BCH_SB_JOURNAL_RECLAIM_DELAY(sb))
			SET_BCH_SB_JOURNAL_RECLAIM_DELAY(sb, 1000);

		if (!BCH_SB_VERSION_UPGRADE_COMPLETE(sb))
			SET_BCH_SB_VERSION_UPGRADE_COMPLETE(sb, le16_to_cpu(sb->version));
	}

	for (opt_id = 0; opt_id < bch2_opts_nr; opt_id++) {
		const struct bch_option *opt = bch2_opt_table + opt_id;

		if (opt->get_sb != BCH2_NO_SB_OPT) {
			u64 v = bch2_opt_from_sb(sb, opt_id);

			prt_printf(out, "Invalid option ");
			ret = bch2_opt_validate(opt, v, out);
			if (ret)
				return ret;

			printbuf_reset(out);
		}
	}

	/* validate layout */
	ret = validate_sb_layout(&sb->layout, out);
	if (ret)
		return ret;

	vstruct_for_each(sb, f) {
		if (!f->u64s) {
			prt_printf(out, "Invalid superblock: optional field with size 0 (type %u)",
			       le32_to_cpu(f->type));
			return -BCH_ERR_invalid_sb_field_size;
		}

		if (vstruct_next(f) > vstruct_last(sb)) {
			prt_printf(out, "Invalid superblock: optional field extends past end of superblock (type %u)",
			       le32_to_cpu(f->type));
			return -BCH_ERR_invalid_sb_field_size;
		}
	}

	/* members must be validated first: */
	mi = bch2_sb_get_members(sb);
	if (!mi) {
		prt_printf(out, "Invalid superblock: member info area missing");
		return -BCH_ERR_invalid_sb_members_missing;
	}

	ret = bch2_sb_field_validate(sb, &mi->field, out);
	if (ret)
		return ret;

	vstruct_for_each(sb, f) {
		if (le32_to_cpu(f->type) == BCH_SB_FIELD_members)
			continue;

		ret = bch2_sb_field_validate(sb, f, out);
		if (ret)
			return ret;
	}

	return 0;
}

/* device open: */

static void bch2_sb_update(struct bch_fs *c)
{
	struct bch_sb *src = c->disk_sb.sb;
	struct bch_sb_field_members *mi = bch2_sb_get_members(src);
	struct bch_dev *ca;
	unsigned i;

	lockdep_assert_held(&c->sb_lock);

	c->sb.uuid		= src->uuid;
	c->sb.user_uuid		= src->user_uuid;
	c->sb.version		= le16_to_cpu(src->version);
	c->sb.version_min	= le16_to_cpu(src->version_min);
	c->sb.version_upgrade_complete = BCH_SB_VERSION_UPGRADE_COMPLETE(src);
	c->sb.nr_devices	= src->nr_devices;
	c->sb.clean		= BCH_SB_CLEAN(src);
	c->sb.encryption_type	= BCH_SB_ENCRYPTION_TYPE(src);

	c->sb.nsec_per_time_unit = le32_to_cpu(src->time_precision);
	c->sb.time_units_per_sec = NSEC_PER_SEC / c->sb.nsec_per_time_unit;

	/* XXX this is wrong, we need a 96 or 128 bit integer type */
	c->sb.time_base_lo	= div_u64(le64_to_cpu(src->time_base_lo),
					  c->sb.nsec_per_time_unit);
	c->sb.time_base_hi	= le32_to_cpu(src->time_base_hi);

	c->sb.features		= le64_to_cpu(src->features[0]);
	c->sb.compat		= le64_to_cpu(src->compat[0]);

	for_each_member_device(ca, c, i)
		ca->mi = bch2_mi_to_cpu(mi->members + i);
}

static int __copy_super(struct bch_sb_handle *dst_handle, struct bch_sb *src)
{
	struct bch_sb_field *src_f, *dst_f;
	struct bch_sb *dst = dst_handle->sb;
	unsigned i;

	dst->version		= src->version;
	dst->version_min	= src->version_min;
	dst->seq		= src->seq;
	dst->uuid		= src->uuid;
	dst->user_uuid		= src->user_uuid;
	memcpy(dst->label,	src->label, sizeof(dst->label));

	dst->block_size		= src->block_size;
	dst->nr_devices		= src->nr_devices;

	dst->time_base_lo	= src->time_base_lo;
	dst->time_base_hi	= src->time_base_hi;
	dst->time_precision	= src->time_precision;

	memcpy(dst->flags,	src->flags,	sizeof(dst->flags));
	memcpy(dst->features,	src->features,	sizeof(dst->features));
	memcpy(dst->compat,	src->compat,	sizeof(dst->compat));

	for (i = 0; i < BCH_SB_FIELD_NR; i++) {
		int d;

		if ((1U << i) & BCH_SINGLE_DEVICE_SB_FIELDS)
			continue;

		src_f = bch2_sb_field_get(src, i);
		dst_f = bch2_sb_field_get(dst, i);

		d = (src_f ? le32_to_cpu(src_f->u64s) : 0) -
		    (dst_f ? le32_to_cpu(dst_f->u64s) : 0);
		if (d > 0) {
			int ret = bch2_sb_realloc(dst_handle,
					le32_to_cpu(dst_handle->sb->u64s) + d);

			if (ret)
				return ret;

			dst = dst_handle->sb;
			dst_f = bch2_sb_field_get(dst, i);
		}

		dst_f = __bch2_sb_field_resize(dst_handle, dst_f,
				src_f ? le32_to_cpu(src_f->u64s) : 0);

		if (src_f)
			memcpy(dst_f, src_f, vstruct_bytes(src_f));
	}

	return 0;
}

int bch2_sb_to_fs(struct bch_fs *c, struct bch_sb *src)
{
	int ret;

	lockdep_assert_held(&c->sb_lock);

	ret =   bch2_sb_realloc(&c->disk_sb, 0) ?:
		__copy_super(&c->disk_sb, src) ?:
		bch2_sb_replicas_to_cpu_replicas(c) ?:
		bch2_sb_disk_groups_to_cpu(c);
	if (ret)
		return ret;

	bch2_sb_update(c);
	return 0;
}

int bch2_sb_from_fs(struct bch_fs *c, struct bch_dev *ca)
{
	return __copy_super(&ca->disk_sb, c->disk_sb.sb);
}

/* read superblock: */

static int read_one_super(struct bch_sb_handle *sb, u64 offset, struct printbuf *err)
{
	struct bch_csum csum;
	size_t bytes;
	int ret;
reread:
	bio_reset(sb->bio, sb->bdev, REQ_OP_READ|REQ_SYNC|REQ_META);
	sb->bio->bi_iter.bi_sector = offset;
	bch2_bio_map(sb->bio, sb->sb, sb->buffer_size);

	ret = submit_bio_wait(sb->bio);
	if (ret) {
		prt_printf(err, "IO error: %i", ret);
		return ret;
	}

	if (!uuid_equal(&sb->sb->magic, &BCACHE_MAGIC) &&
	    !uuid_equal(&sb->sb->magic, &BCHFS_MAGIC)) {
		prt_printf(err, "Not a bcachefs superblock");
		return -BCH_ERR_invalid_sb_magic;
	}

	ret = bch2_sb_compatible(sb->sb, err);
	if (ret)
		return ret;

	bytes = vstruct_bytes(sb->sb);

	if (bytes > 512 << sb->sb->layout.sb_max_size_bits) {
		prt_printf(err, "Invalid superblock: too big (got %zu bytes, layout max %lu)",
		       bytes, 512UL << sb->sb->layout.sb_max_size_bits);
		return -BCH_ERR_invalid_sb_too_big;
	}

	if (bytes > sb->buffer_size) {
		ret = bch2_sb_realloc(sb, le32_to_cpu(sb->sb->u64s));
		if (ret)
			return ret;
		goto reread;
	}

	if (BCH_SB_CSUM_TYPE(sb->sb) >= BCH_CSUM_NR) {
		prt_printf(err, "unknown checksum type %llu", BCH_SB_CSUM_TYPE(sb->sb));
		return -BCH_ERR_invalid_sb_csum_type;
	}

	/* XXX: verify MACs */
	csum = csum_vstruct(NULL, BCH_SB_CSUM_TYPE(sb->sb),
			    null_nonce(), sb->sb);

	if (bch2_crc_cmp(csum, sb->sb->csum)) {
		prt_printf(err, "bad checksum");
		return -BCH_ERR_invalid_sb_csum;
	}

	sb->seq = le64_to_cpu(sb->sb->seq);

	return 0;
}

int bch2_read_super(const char *path, struct bch_opts *opts,
		    struct bch_sb_handle *sb)
{
	u64 offset = opt_get(*opts, sb);
	struct bch_sb_layout layout;
	struct printbuf err = PRINTBUF;
	__le64 *i;
	int ret;
#ifndef __KERNEL__
retry:
#endif
	memset(sb, 0, sizeof(*sb));
	sb->mode	= BLK_OPEN_READ;
	sb->have_bio	= true;
	sb->holder	= kmalloc(1, GFP_KERNEL);
	if (!sb->holder)
		return -ENOMEM;

#ifndef __KERNEL__
	if (opt_get(*opts, direct_io) == false)
		sb->mode |= FMODE_BUFFERED;
#endif

	if (!opt_get(*opts, noexcl))
		sb->mode |= BLK_OPEN_EXCL;

	if (!opt_get(*opts, nochanges))
		sb->mode |= BLK_OPEN_WRITE;

	sb->bdev = blkdev_get_by_path(path, sb->mode, sb->holder, &bch2_sb_handle_bdev_ops);
	if (IS_ERR(sb->bdev) &&
	    PTR_ERR(sb->bdev) == -EACCES &&
	    opt_get(*opts, read_only)) {
		sb->mode &= ~BLK_OPEN_WRITE;

		sb->bdev = blkdev_get_by_path(path, sb->mode, sb->holder, &bch2_sb_handle_bdev_ops);
		if (!IS_ERR(sb->bdev))
			opt_set(*opts, nochanges, true);
	}

	if (IS_ERR(sb->bdev)) {
		ret = PTR_ERR(sb->bdev);
		goto out;
	}

	ret = bch2_sb_realloc(sb, 0);
	if (ret) {
		prt_printf(&err, "error allocating memory for superblock");
		goto err;
	}

	if (bch2_fs_init_fault("read_super")) {
		prt_printf(&err, "dynamic fault");
		ret = -EFAULT;
		goto err;
	}

	ret = read_one_super(sb, offset, &err);
	if (!ret)
		goto got_super;

	if (opt_defined(*opts, sb))
		goto err;

	printk(KERN_ERR "bcachefs (%s): error reading default superblock: %s",
	       path, err.buf);
	printbuf_reset(&err);

	/*
	 * Error reading primary superblock - read location of backup
	 * superblocks:
	 */
	bio_reset(sb->bio, sb->bdev, REQ_OP_READ|REQ_SYNC|REQ_META);
	sb->bio->bi_iter.bi_sector = BCH_SB_LAYOUT_SECTOR;
	/*
	 * use sb buffer to read layout, since sb buffer is page aligned but
	 * layout won't be:
	 */
	bch2_bio_map(sb->bio, sb->sb, sizeof(struct bch_sb_layout));

	ret = submit_bio_wait(sb->bio);
	if (ret) {
		prt_printf(&err, "IO error: %i", ret);
		goto err;
	}

	memcpy(&layout, sb->sb, sizeof(layout));
	ret = validate_sb_layout(&layout, &err);
	if (ret)
		goto err;

	for (i = layout.sb_offset;
	     i < layout.sb_offset + layout.nr_superblocks; i++) {
		offset = le64_to_cpu(*i);

		if (offset == opt_get(*opts, sb))
			continue;

		ret = read_one_super(sb, offset, &err);
		if (!ret)
			goto got_super;
	}

	goto err;

got_super:
	if (le16_to_cpu(sb->sb->block_size) << 9 <
	    bdev_logical_block_size(sb->bdev) &&
	    opt_get(*opts, direct_io)) {
#ifndef __KERNEL__
		opt_set(*opts, direct_io, false);
		bch2_free_super(sb);
		goto retry;
#endif
		prt_printf(&err, "block size (%u) smaller than device block size (%u)",
		       le16_to_cpu(sb->sb->block_size) << 9,
		       bdev_logical_block_size(sb->bdev));
		ret = -BCH_ERR_block_size_too_small;
		goto err;
	}

	ret = 0;
	sb->have_layout = true;

	ret = bch2_sb_validate(sb, &err, READ);
	if (ret) {
		printk(KERN_ERR "bcachefs (%s): error validating superblock: %s",
		       path, err.buf);
		goto err_no_print;
	}
out:
	printbuf_exit(&err);
	return ret;
err:
	printk(KERN_ERR "bcachefs (%s): error reading superblock: %s",
	       path, err.buf);
err_no_print:
	bch2_free_super(sb);
	goto out;
}

/* write superblock: */

static void write_super_endio(struct bio *bio)
{
	struct bch_dev *ca = bio->bi_private;

	/* XXX: return errors directly */

	if (bch2_dev_io_err_on(bio->bi_status, ca, "superblock write error: %s",
			       bch2_blk_status_to_str(bio->bi_status)))
		ca->sb_write_error = 1;

	closure_put(&ca->fs->sb_write);
	percpu_ref_put(&ca->io_ref);
}

static void read_back_super(struct bch_fs *c, struct bch_dev *ca)
{
	struct bch_sb *sb = ca->disk_sb.sb;
	struct bio *bio = ca->disk_sb.bio;

	bio_reset(bio, ca->disk_sb.bdev, REQ_OP_READ|REQ_SYNC|REQ_META);
	bio->bi_iter.bi_sector	= le64_to_cpu(sb->layout.sb_offset[0]);
	bio->bi_end_io		= write_super_endio;
	bio->bi_private		= ca;
	bch2_bio_map(bio, ca->sb_read_scratch, PAGE_SIZE);

	this_cpu_add(ca->io_done->sectors[READ][BCH_DATA_sb],
		     bio_sectors(bio));

	percpu_ref_get(&ca->io_ref);
	closure_bio_submit(bio, &c->sb_write);
}

static void write_one_super(struct bch_fs *c, struct bch_dev *ca, unsigned idx)
{
	struct bch_sb *sb = ca->disk_sb.sb;
	struct bio *bio = ca->disk_sb.bio;

	sb->offset = sb->layout.sb_offset[idx];

	SET_BCH_SB_CSUM_TYPE(sb, bch2_csum_opt_to_type(c->opts.metadata_checksum, false));
	sb->csum = csum_vstruct(c, BCH_SB_CSUM_TYPE(sb),
				null_nonce(), sb);

	bio_reset(bio, ca->disk_sb.bdev, REQ_OP_WRITE|REQ_SYNC|REQ_META);
	bio->bi_iter.bi_sector	= le64_to_cpu(sb->offset);
	bio->bi_end_io		= write_super_endio;
	bio->bi_private		= ca;
	bch2_bio_map(bio, sb,
		     roundup((size_t) vstruct_bytes(sb),
			     bdev_logical_block_size(ca->disk_sb.bdev)));

	this_cpu_add(ca->io_done->sectors[WRITE][BCH_DATA_sb],
		     bio_sectors(bio));

	percpu_ref_get(&ca->io_ref);
	closure_bio_submit(bio, &c->sb_write);
}

int bch2_write_super(struct bch_fs *c)
{
	struct closure *cl = &c->sb_write;
	struct bch_dev *ca;
	struct printbuf err = PRINTBUF;
	unsigned i, sb = 0, nr_wrote;
	struct bch_devs_mask sb_written;
	bool wrote, can_mount_without_written, can_mount_with_written;
	unsigned degraded_flags = BCH_FORCE_IF_DEGRADED;
	int ret = 0;

	trace_and_count(c, write_super, c, _RET_IP_);

	if (c->opts.very_degraded)
		degraded_flags |= BCH_FORCE_IF_LOST;

	lockdep_assert_held(&c->sb_lock);

	closure_init_stack(cl);
	memset(&sb_written, 0, sizeof(sb_written));

	/* Make sure we're using the new magic numbers: */
	c->disk_sb.sb->magic = BCHFS_MAGIC;
	c->disk_sb.sb->layout.magic = BCHFS_MAGIC;

	le64_add_cpu(&c->disk_sb.sb->seq, 1);

	if (test_bit(BCH_FS_ERROR, &c->flags))
		SET_BCH_SB_HAS_ERRORS(c->disk_sb.sb, 1);
	if (test_bit(BCH_FS_TOPOLOGY_ERROR, &c->flags))
		SET_BCH_SB_HAS_TOPOLOGY_ERRORS(c->disk_sb.sb, 1);

	SET_BCH_SB_BIG_ENDIAN(c->disk_sb.sb, CPU_BIG_ENDIAN);

	bch2_sb_counters_from_cpu(c);

	for_each_online_member(ca, c, i)
		bch2_sb_from_fs(c, ca);

	for_each_online_member(ca, c, i) {
		printbuf_reset(&err);

		ret = bch2_sb_validate(&ca->disk_sb, &err, WRITE);
		if (ret) {
			bch2_fs_inconsistent(c, "sb invalid before write: %s", err.buf);
			percpu_ref_put(&ca->io_ref);
			goto out;
		}
	}

	if (c->opts.nochanges)
		goto out;

	/*
	 * Defer writing the superblock until filesystem initialization is
	 * complete - don't write out a partly initialized superblock:
	 */
	if (!BCH_SB_INITIALIZED(c->disk_sb.sb))
		goto out;

	for_each_online_member(ca, c, i) {
		__set_bit(ca->dev_idx, sb_written.d);
		ca->sb_write_error = 0;
	}

	for_each_online_member(ca, c, i)
		read_back_super(c, ca);
	closure_sync(cl);

	for_each_online_member(ca, c, i) {
		if (ca->sb_write_error)
			continue;

		if (le64_to_cpu(ca->sb_read_scratch->seq) < ca->disk_sb.seq) {
			bch2_fs_fatal_error(c,
				"Superblock write was silently dropped! (seq %llu expected %llu)",
				le64_to_cpu(ca->sb_read_scratch->seq),
				ca->disk_sb.seq);
			percpu_ref_put(&ca->io_ref);
			ret = -BCH_ERR_erofs_sb_err;
			goto out;
		}

		if (le64_to_cpu(ca->sb_read_scratch->seq) > ca->disk_sb.seq) {
			bch2_fs_fatal_error(c,
				"Superblock modified by another process (seq %llu expected %llu)",
				le64_to_cpu(ca->sb_read_scratch->seq),
				ca->disk_sb.seq);
			percpu_ref_put(&ca->io_ref);
			ret = -BCH_ERR_erofs_sb_err;
			goto out;
		}
	}

	do {
		wrote = false;
		for_each_online_member(ca, c, i)
			if (!ca->sb_write_error &&
			    sb < ca->disk_sb.sb->layout.nr_superblocks) {
				write_one_super(c, ca, sb);
				wrote = true;
			}
		closure_sync(cl);
		sb++;
	} while (wrote);

	for_each_online_member(ca, c, i) {
		if (ca->sb_write_error)
			__clear_bit(ca->dev_idx, sb_written.d);
		else
			ca->disk_sb.seq = le64_to_cpu(ca->disk_sb.sb->seq);
	}

	nr_wrote = dev_mask_nr(&sb_written);

	can_mount_with_written =
		bch2_have_enough_devs(c, sb_written, degraded_flags, false);

	for (i = 0; i < ARRAY_SIZE(sb_written.d); i++)
		sb_written.d[i] = ~sb_written.d[i];

	can_mount_without_written =
		bch2_have_enough_devs(c, sb_written, degraded_flags, false);

	/*
	 * If we would be able to mount _without_ the devices we successfully
	 * wrote superblocks to, we weren't able to write to enough devices:
	 *
	 * Exception: if we can mount without the successes because we haven't
	 * written anything (new filesystem), we continue if we'd be able to
	 * mount with the devices we did successfully write to:
	 */
	if (bch2_fs_fatal_err_on(!nr_wrote ||
				 !can_mount_with_written ||
				 (can_mount_without_written &&
				  !can_mount_with_written), c,
		"Unable to write superblock to sufficient devices (from %ps)",
		(void *) _RET_IP_))
		ret = -1;
out:
	/* Make new options visible after they're persistent: */
	bch2_sb_update(c);
	printbuf_exit(&err);
	return ret;
}

void __bch2_check_set_feature(struct bch_fs *c, unsigned feat)
{
	mutex_lock(&c->sb_lock);
	if (!(c->sb.features & (1ULL << feat))) {
		c->disk_sb.sb->features[0] |= cpu_to_le64(1ULL << feat);

		bch2_write_super(c);
	}
	mutex_unlock(&c->sb_lock);
}

/* Downgrade if superblock is at a higher version than currently supported: */
void bch2_sb_maybe_downgrade(struct bch_fs *c)
{
	lockdep_assert_held(&c->sb_lock);

	/*
	 * Downgrade, if superblock is at a higher version than currently
	 * supported:
	 */
	if (BCH_SB_VERSION_UPGRADE_COMPLETE(c->disk_sb.sb) > bcachefs_metadata_version_current)
		SET_BCH_SB_VERSION_UPGRADE_COMPLETE(c->disk_sb.sb, bcachefs_metadata_version_current);
	if (c->sb.version > bcachefs_metadata_version_current)
		c->disk_sb.sb->version = cpu_to_le16(bcachefs_metadata_version_current);
	if (c->sb.version_min > bcachefs_metadata_version_current)
		c->disk_sb.sb->version_min = cpu_to_le16(bcachefs_metadata_version_current);
	c->disk_sb.sb->compat[0] &= cpu_to_le64((1ULL << BCH_COMPAT_NR) - 1);
}

void bch2_sb_upgrade(struct bch_fs *c, unsigned new_version)
{
	lockdep_assert_held(&c->sb_lock);

	c->disk_sb.sb->version = cpu_to_le16(new_version);
	c->disk_sb.sb->features[0] |= cpu_to_le64(BCH_SB_FEATURES_ALL);
}

static const struct bch_sb_field_ops *bch2_sb_field_ops[] = {
#define x(f, nr)					\
	[BCH_SB_FIELD_##f] = &bch_sb_field_ops_##f,
	BCH_SB_FIELDS()
#undef x
};

static const struct bch_sb_field_ops bch2_sb_field_null_ops;

static const struct bch_sb_field_ops *bch2_sb_field_type_ops(unsigned type)
{
	return likely(type < ARRAY_SIZE(bch2_sb_field_ops))
		? bch2_sb_field_ops[type]
		: &bch2_sb_field_null_ops;
}

static int bch2_sb_field_validate(struct bch_sb *sb, struct bch_sb_field *f,
				  struct printbuf *err)
{
	unsigned type = le32_to_cpu(f->type);
	struct printbuf field_err = PRINTBUF;
	const struct bch_sb_field_ops *ops = bch2_sb_field_type_ops(type);
	int ret;

	ret = ops->validate ? ops->validate(sb, f, &field_err) : 0;
	if (ret) {
		prt_printf(err, "Invalid superblock section %s: %s",
			   bch2_sb_fields[type], field_err.buf);
		prt_newline(err);
		bch2_sb_field_to_text(err, sb, f);
	}

	printbuf_exit(&field_err);
	return ret;
}

void bch2_sb_field_to_text(struct printbuf *out, struct bch_sb *sb,
			   struct bch_sb_field *f)
{
	unsigned type = le32_to_cpu(f->type);
	const struct bch_sb_field_ops *ops = bch2_sb_field_type_ops(type);

	if (!out->nr_tabstops)
		printbuf_tabstop_push(out, 32);

	if (type < BCH_SB_FIELD_NR)
		prt_printf(out, "%s", bch2_sb_fields[type]);
	else
		prt_printf(out, "(unknown field %u)", type);

	prt_printf(out, " (size %zu):", vstruct_bytes(f));
	prt_newline(out);

	if (ops->to_text) {
		printbuf_indent_add(out, 2);
		ops->to_text(out, sb, f);
		printbuf_indent_sub(out, 2);
	}
}

void bch2_sb_layout_to_text(struct printbuf *out, struct bch_sb_layout *l)
{
	unsigned i;

	prt_printf(out, "Type:                    %u", l->layout_type);
	prt_newline(out);

	prt_str(out, "Superblock max size:     ");
	prt_units_u64(out, 512 << l->sb_max_size_bits);
	prt_newline(out);

	prt_printf(out, "Nr superblocks:          %u", l->nr_superblocks);
	prt_newline(out);

	prt_str(out, "Offsets:                 ");
	for (i = 0; i < l->nr_superblocks; i++) {
		if (i)
			prt_str(out, ", ");
		prt_printf(out, "%llu", le64_to_cpu(l->sb_offset[i]));
	}
	prt_newline(out);
}

void bch2_sb_to_text(struct printbuf *out, struct bch_sb *sb,
		     bool print_layout, unsigned fields)
{
	struct bch_sb_field_members *mi;
	struct bch_sb_field *f;
	u64 fields_have = 0;
	unsigned nr_devices = 0;

	if (!out->nr_tabstops)
		printbuf_tabstop_push(out, 44);

	mi = bch2_sb_get_members(sb);
	if (mi) {
		struct bch_member *m;

		for (m = mi->members;
		     m < mi->members + sb->nr_devices;
		     m++)
			nr_devices += bch2_member_exists(m);
	}

	prt_printf(out, "External UUID:");
	prt_tab(out);
	pr_uuid(out, sb->user_uuid.b);
	prt_newline(out);

	prt_printf(out, "Internal UUID:");
	prt_tab(out);
	pr_uuid(out, sb->uuid.b);
	prt_newline(out);

	prt_str(out, "Device index:");
	prt_tab(out);
	prt_printf(out, "%u", sb->dev_idx);
	prt_newline(out);

	prt_str(out, "Label:");
	prt_tab(out);
	prt_printf(out, "%.*s", (int) sizeof(sb->label), sb->label);
	prt_newline(out);

	prt_str(out, "Version:");
	prt_tab(out);
	bch2_version_to_text(out, le16_to_cpu(sb->version));
	prt_newline(out);

	prt_str(out, "Version upgrade complete:");
	prt_tab(out);
	bch2_version_to_text(out, BCH_SB_VERSION_UPGRADE_COMPLETE(sb));
	prt_newline(out);

	prt_printf(out, "Oldest version on disk:");
	prt_tab(out);
	bch2_version_to_text(out, le16_to_cpu(sb->version_min));
	prt_newline(out);

	prt_printf(out, "Created:");
	prt_tab(out);
	if (sb->time_base_lo)
		pr_time(out, div_u64(le64_to_cpu(sb->time_base_lo), NSEC_PER_SEC));
	else
		prt_printf(out, "(not set)");
	prt_newline(out);

	prt_printf(out, "Sequence number:");
	prt_tab(out);
	prt_printf(out, "%llu", le64_to_cpu(sb->seq));
	prt_newline(out);

	prt_printf(out, "Superblock size:");
	prt_tab(out);
	prt_printf(out, "%zu", vstruct_bytes(sb));
	prt_newline(out);

	prt_printf(out, "Clean:");
	prt_tab(out);
	prt_printf(out, "%llu", BCH_SB_CLEAN(sb));
	prt_newline(out);

	prt_printf(out, "Devices:");
	prt_tab(out);
	prt_printf(out, "%u", nr_devices);
	prt_newline(out);

	prt_printf(out, "Sections:");
	vstruct_for_each(sb, f)
		fields_have |= 1 << le32_to_cpu(f->type);
	prt_tab(out);
	prt_bitflags(out, bch2_sb_fields, fields_have);
	prt_newline(out);

	prt_printf(out, "Features:");
	prt_tab(out);
	prt_bitflags(out, bch2_sb_features, le64_to_cpu(sb->features[0]));
	prt_newline(out);

	prt_printf(out, "Compat features:");
	prt_tab(out);
	prt_bitflags(out, bch2_sb_compat, le64_to_cpu(sb->compat[0]));
	prt_newline(out);

	prt_newline(out);
	prt_printf(out, "Options:");
	prt_newline(out);
	printbuf_indent_add(out, 2);
	{
		enum bch_opt_id id;

		for (id = 0; id < bch2_opts_nr; id++) {
			const struct bch_option *opt = bch2_opt_table + id;

			if (opt->get_sb != BCH2_NO_SB_OPT) {
				u64 v = bch2_opt_from_sb(sb, id);

				prt_printf(out, "%s:", opt->attr.name);
				prt_tab(out);
				bch2_opt_to_text(out, NULL, sb, opt, v,
						 OPT_HUMAN_READABLE|OPT_SHOW_FULL_LIST);
				prt_newline(out);
			}
		}
	}

	printbuf_indent_sub(out, 2);

	if (print_layout) {
		prt_newline(out);
		prt_printf(out, "layout:");
		prt_newline(out);
		printbuf_indent_add(out, 2);
		bch2_sb_layout_to_text(out, &sb->layout);
		printbuf_indent_sub(out, 2);
	}

	vstruct_for_each(sb, f)
		if (fields & (1 << le32_to_cpu(f->type))) {
			prt_newline(out);
			bch2_sb_field_to_text(out, sb, f);
		}
}
