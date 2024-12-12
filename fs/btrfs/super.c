// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#include <linux/blkdev.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/backing-dev.h>
#include <linux/mount.h>
#include <linux/writeback.h>
#include <linux/statfs.h>
#include <linux/compat.h>
#include <linux/parser.h>
#include <linux/ctype.h>
#include <linux/namei.h>
#include <linux/miscdevice.h>
#include <linux/magic.h>
#include <linux/slab.h>
#include <linux/ratelimit.h>
#include <linux/crc32c.h>
#include <linux/btrfs.h>
#include <linux/security.h>
#include <linux/fs_parser.h>
#include "messages.h"
#include "delayed-inode.h"
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "btrfs_inode.h"
#include "direct-io.h"
#include "props.h"
#include "xattr.h"
#include "bio.h"
#include "export.h"
#include "compression.h"
#include "dev-replace.h"
#include "free-space-cache.h"
#include "backref.h"
#include "space-info.h"
#include "sysfs.h"
#include "zoned.h"
#include "tests/btrfs-tests.h"
#include "block-group.h"
#include "discard.h"
#include "qgroup.h"
#include "raid56.h"
#include "fs.h"
#include "accessors.h"
#include "defrag.h"
#include "dir-item.h"
#include "ioctl.h"
#include "scrub.h"
#include "verity.h"
#include "super.h"
#include "extent-tree.h"
#define CREATE_TRACE_POINTS
#include <trace/events/btrfs.h>

static const struct super_operations btrfs_super_ops;
static struct file_system_type btrfs_fs_type;

static void btrfs_put_super(struct super_block *sb)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(sb);

	btrfs_info(fs_info, "last unmount of filesystem %pU", fs_info->fs_devices->fsid);
	close_ctree(fs_info);
}

/* Store the mount options related information. */
struct btrfs_fs_context {
	char *subvol_name;
	u64 subvol_objectid;
	u64 max_inline;
	u32 commit_interval;
	u32 metadata_ratio;
	u32 thread_pool_size;
	unsigned long long mount_opt;
	unsigned long compress_type:4;
	unsigned int compress_level;
	refcount_t refs;
};

enum {
	Opt_acl,
	Opt_clear_cache,
	Opt_commit_interval,
	Opt_compress,
	Opt_compress_force,
	Opt_compress_force_type,
	Opt_compress_type,
	Opt_degraded,
	Opt_device,
	Opt_fatal_errors,
	Opt_flushoncommit,
	Opt_max_inline,
	Opt_barrier,
	Opt_datacow,
	Opt_datasum,
	Opt_defrag,
	Opt_discard,
	Opt_discard_mode,
	Opt_ratio,
	Opt_rescan_uuid_tree,
	Opt_skip_balance,
	Opt_space_cache,
	Opt_space_cache_version,
	Opt_ssd,
	Opt_ssd_spread,
	Opt_subvol,
	Opt_subvol_empty,
	Opt_subvolid,
	Opt_thread_pool,
	Opt_treelog,
	Opt_user_subvol_rm_allowed,
	Opt_norecovery,

	/* Rescue options */
	Opt_rescue,
	Opt_usebackuproot,
	Opt_nologreplay,

	/* Debugging options */
	Opt_enospc_debug,
#ifdef CONFIG_BTRFS_DEBUG
	Opt_fragment, Opt_fragment_data, Opt_fragment_metadata, Opt_fragment_all,
#endif
#ifdef CONFIG_BTRFS_FS_REF_VERIFY
	Opt_ref_verify,
#endif
	Opt_err,
};

enum {
	Opt_fatal_errors_panic,
	Opt_fatal_errors_bug,
};

static const struct constant_table btrfs_parameter_fatal_errors[] = {
	{ "panic", Opt_fatal_errors_panic },
	{ "bug", Opt_fatal_errors_bug },
	{}
};

enum {
	Opt_discard_sync,
	Opt_discard_async,
};

static const struct constant_table btrfs_parameter_discard[] = {
	{ "sync", Opt_discard_sync },
	{ "async", Opt_discard_async },
	{}
};

enum {
	Opt_space_cache_v1,
	Opt_space_cache_v2,
};

static const struct constant_table btrfs_parameter_space_cache[] = {
	{ "v1", Opt_space_cache_v1 },
	{ "v2", Opt_space_cache_v2 },
	{}
};

enum {
	Opt_rescue_usebackuproot,
	Opt_rescue_nologreplay,
	Opt_rescue_ignorebadroots,
	Opt_rescue_ignoredatacsums,
	Opt_rescue_ignoremetacsums,
	Opt_rescue_ignoresuperflags,
	Opt_rescue_parameter_all,
};

static const struct constant_table btrfs_parameter_rescue[] = {
	{ "usebackuproot", Opt_rescue_usebackuproot },
	{ "nologreplay", Opt_rescue_nologreplay },
	{ "ignorebadroots", Opt_rescue_ignorebadroots },
	{ "ibadroots", Opt_rescue_ignorebadroots },
	{ "ignoredatacsums", Opt_rescue_ignoredatacsums },
	{ "ignoremetacsums", Opt_rescue_ignoremetacsums},
	{ "ignoresuperflags", Opt_rescue_ignoresuperflags},
	{ "idatacsums", Opt_rescue_ignoredatacsums },
	{ "imetacsums", Opt_rescue_ignoremetacsums},
	{ "isuperflags", Opt_rescue_ignoresuperflags},
	{ "all", Opt_rescue_parameter_all },
	{}
};

#ifdef CONFIG_BTRFS_DEBUG
enum {
	Opt_fragment_parameter_data,
	Opt_fragment_parameter_metadata,
	Opt_fragment_parameter_all,
};

static const struct constant_table btrfs_parameter_fragment[] = {
	{ "data", Opt_fragment_parameter_data },
	{ "metadata", Opt_fragment_parameter_metadata },
	{ "all", Opt_fragment_parameter_all },
	{}
};
#endif

static const struct fs_parameter_spec btrfs_fs_parameters[] = {
	fsparam_flag_no("acl", Opt_acl),
	fsparam_flag_no("autodefrag", Opt_defrag),
	fsparam_flag_no("barrier", Opt_barrier),
	fsparam_flag("clear_cache", Opt_clear_cache),
	fsparam_u32("commit", Opt_commit_interval),
	fsparam_flag("compress", Opt_compress),
	fsparam_string("compress", Opt_compress_type),
	fsparam_flag("compress-force", Opt_compress_force),
	fsparam_string("compress-force", Opt_compress_force_type),
	fsparam_flag_no("datacow", Opt_datacow),
	fsparam_flag_no("datasum", Opt_datasum),
	fsparam_flag("degraded", Opt_degraded),
	fsparam_string("device", Opt_device),
	fsparam_flag_no("discard", Opt_discard),
	fsparam_enum("discard", Opt_discard_mode, btrfs_parameter_discard),
	fsparam_enum("fatal_errors", Opt_fatal_errors, btrfs_parameter_fatal_errors),
	fsparam_flag_no("flushoncommit", Opt_flushoncommit),
	fsparam_string("max_inline", Opt_max_inline),
	fsparam_u32("metadata_ratio", Opt_ratio),
	fsparam_flag("rescan_uuid_tree", Opt_rescan_uuid_tree),
	fsparam_flag("skip_balance", Opt_skip_balance),
	fsparam_flag_no("space_cache", Opt_space_cache),
	fsparam_enum("space_cache", Opt_space_cache_version, btrfs_parameter_space_cache),
	fsparam_flag_no("ssd", Opt_ssd),
	fsparam_flag_no("ssd_spread", Opt_ssd_spread),
	fsparam_string("subvol", Opt_subvol),
	fsparam_flag("subvol=", Opt_subvol_empty),
	fsparam_u64("subvolid", Opt_subvolid),
	fsparam_u32("thread_pool", Opt_thread_pool),
	fsparam_flag_no("treelog", Opt_treelog),
	fsparam_flag("user_subvol_rm_allowed", Opt_user_subvol_rm_allowed),

	/* Rescue options. */
	fsparam_enum("rescue", Opt_rescue, btrfs_parameter_rescue),
	/* Deprecated, with alias rescue=nologreplay */
	__fsparam(NULL, "nologreplay", Opt_nologreplay, fs_param_deprecated, NULL),
	/* Deprecated, with alias rescue=usebackuproot */
	__fsparam(NULL, "usebackuproot", Opt_usebackuproot, fs_param_deprecated, NULL),
	/* For compatibility only, alias for "rescue=nologreplay". */
	fsparam_flag("norecovery", Opt_norecovery),

	/* Debugging options. */
	fsparam_flag_no("enospc_debug", Opt_enospc_debug),
#ifdef CONFIG_BTRFS_DEBUG
	fsparam_enum("fragment", Opt_fragment, btrfs_parameter_fragment),
#endif
#ifdef CONFIG_BTRFS_FS_REF_VERIFY
	fsparam_flag("ref_verify", Opt_ref_verify),
#endif
	{}
};

/* No support for restricting writes to btrfs devices yet... */
static inline blk_mode_t btrfs_open_mode(struct fs_context *fc)
{
	return sb_open_mode(fc->sb_flags) & ~BLK_OPEN_RESTRICT_WRITES;
}

static int btrfs_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	struct btrfs_fs_context *ctx = fc->fs_private;
	struct fs_parse_result result;
	int opt;

	opt = fs_parse(fc, btrfs_fs_parameters, param, &result);
	if (opt < 0)
		return opt;

	switch (opt) {
	case Opt_degraded:
		btrfs_set_opt(ctx->mount_opt, DEGRADED);
		break;
	case Opt_subvol_empty:
		/*
		 * This exists because we used to allow it on accident, so we're
		 * keeping it to maintain ABI.  See 37becec95ac3 ("Btrfs: allow
		 * empty subvol= again").
		 */
		break;
	case Opt_subvol:
		kfree(ctx->subvol_name);
		ctx->subvol_name = kstrdup(param->string, GFP_KERNEL);
		if (!ctx->subvol_name)
			return -ENOMEM;
		break;
	case Opt_subvolid:
		ctx->subvol_objectid = result.uint_64;

		/* subvolid=0 means give me the original fs_tree. */
		if (!ctx->subvol_objectid)
			ctx->subvol_objectid = BTRFS_FS_TREE_OBJECTID;
		break;
	case Opt_device: {
		struct btrfs_device *device;
		blk_mode_t mode = btrfs_open_mode(fc);

		mutex_lock(&uuid_mutex);
		device = btrfs_scan_one_device(param->string, mode, false);
		mutex_unlock(&uuid_mutex);
		if (IS_ERR(device))
			return PTR_ERR(device);
		break;
	}
	case Opt_datasum:
		if (result.negated) {
			btrfs_set_opt(ctx->mount_opt, NODATASUM);
		} else {
			btrfs_clear_opt(ctx->mount_opt, NODATACOW);
			btrfs_clear_opt(ctx->mount_opt, NODATASUM);
		}
		break;
	case Opt_datacow:
		if (result.negated) {
			btrfs_clear_opt(ctx->mount_opt, COMPRESS);
			btrfs_clear_opt(ctx->mount_opt, FORCE_COMPRESS);
			btrfs_set_opt(ctx->mount_opt, NODATACOW);
			btrfs_set_opt(ctx->mount_opt, NODATASUM);
		} else {
			btrfs_clear_opt(ctx->mount_opt, NODATACOW);
		}
		break;
	case Opt_compress_force:
	case Opt_compress_force_type:
		btrfs_set_opt(ctx->mount_opt, FORCE_COMPRESS);
		fallthrough;
	case Opt_compress:
	case Opt_compress_type:
		/*
		 * Provide the same semantics as older kernels that don't use fs
		 * context, specifying the "compress" option clears
		 * "force-compress" without the need to pass
		 * "compress-force=[no|none]" before specifying "compress".
		 */
		if (opt != Opt_compress_force && opt != Opt_compress_force_type)
			btrfs_clear_opt(ctx->mount_opt, FORCE_COMPRESS);

		if (opt == Opt_compress || opt == Opt_compress_force) {
			ctx->compress_type = BTRFS_COMPRESS_ZLIB;
			ctx->compress_level = BTRFS_ZLIB_DEFAULT_LEVEL;
			btrfs_set_opt(ctx->mount_opt, COMPRESS);
			btrfs_clear_opt(ctx->mount_opt, NODATACOW);
			btrfs_clear_opt(ctx->mount_opt, NODATASUM);
		} else if (strncmp(param->string, "zlib", 4) == 0) {
			ctx->compress_type = BTRFS_COMPRESS_ZLIB;
			ctx->compress_level =
				btrfs_compress_str2level(BTRFS_COMPRESS_ZLIB,
							 param->string + 4);
			btrfs_set_opt(ctx->mount_opt, COMPRESS);
			btrfs_clear_opt(ctx->mount_opt, NODATACOW);
			btrfs_clear_opt(ctx->mount_opt, NODATASUM);
		} else if (strncmp(param->string, "lzo", 3) == 0) {
			ctx->compress_type = BTRFS_COMPRESS_LZO;
			ctx->compress_level = 0;
			btrfs_set_opt(ctx->mount_opt, COMPRESS);
			btrfs_clear_opt(ctx->mount_opt, NODATACOW);
			btrfs_clear_opt(ctx->mount_opt, NODATASUM);
		} else if (strncmp(param->string, "zstd", 4) == 0) {
			ctx->compress_type = BTRFS_COMPRESS_ZSTD;
			ctx->compress_level =
				btrfs_compress_str2level(BTRFS_COMPRESS_ZSTD,
							 param->string + 4);
			btrfs_set_opt(ctx->mount_opt, COMPRESS);
			btrfs_clear_opt(ctx->mount_opt, NODATACOW);
			btrfs_clear_opt(ctx->mount_opt, NODATASUM);
		} else if (strncmp(param->string, "no", 2) == 0) {
			ctx->compress_level = 0;
			ctx->compress_type = 0;
			btrfs_clear_opt(ctx->mount_opt, COMPRESS);
			btrfs_clear_opt(ctx->mount_opt, FORCE_COMPRESS);
		} else {
			btrfs_err(NULL, "unrecognized compression value %s",
				  param->string);
			return -EINVAL;
		}
		break;
	case Opt_ssd:
		if (result.negated) {
			btrfs_set_opt(ctx->mount_opt, NOSSD);
			btrfs_clear_opt(ctx->mount_opt, SSD);
			btrfs_clear_opt(ctx->mount_opt, SSD_SPREAD);
		} else {
			btrfs_set_opt(ctx->mount_opt, SSD);
			btrfs_clear_opt(ctx->mount_opt, NOSSD);
		}
		break;
	case Opt_ssd_spread:
		if (result.negated) {
			btrfs_clear_opt(ctx->mount_opt, SSD_SPREAD);
		} else {
			btrfs_set_opt(ctx->mount_opt, SSD);
			btrfs_set_opt(ctx->mount_opt, SSD_SPREAD);
			btrfs_clear_opt(ctx->mount_opt, NOSSD);
		}
		break;
	case Opt_barrier:
		if (result.negated)
			btrfs_set_opt(ctx->mount_opt, NOBARRIER);
		else
			btrfs_clear_opt(ctx->mount_opt, NOBARRIER);
		break;
	case Opt_thread_pool:
		if (result.uint_32 == 0) {
			btrfs_err(NULL, "invalid value 0 for thread_pool");
			return -EINVAL;
		}
		ctx->thread_pool_size = result.uint_32;
		break;
	case Opt_max_inline:
		ctx->max_inline = memparse(param->string, NULL);
		break;
	case Opt_acl:
		if (result.negated) {
			fc->sb_flags &= ~SB_POSIXACL;
		} else {
#ifdef CONFIG_BTRFS_FS_POSIX_ACL
			fc->sb_flags |= SB_POSIXACL;
#else
			btrfs_err(NULL, "support for ACL not compiled in");
			return -EINVAL;
#endif
		}
		/*
		 * VFS limits the ability to toggle ACL on and off via remount,
		 * despite every file system allowing this.  This seems to be
		 * an oversight since we all do, but it'll fail if we're
		 * remounting.  So don't set the mask here, we'll check it in
		 * btrfs_reconfigure and do the toggling ourselves.
		 */
		if (fc->purpose != FS_CONTEXT_FOR_RECONFIGURE)
			fc->sb_flags_mask |= SB_POSIXACL;
		break;
	case Opt_treelog:
		if (result.negated)
			btrfs_set_opt(ctx->mount_opt, NOTREELOG);
		else
			btrfs_clear_opt(ctx->mount_opt, NOTREELOG);
		break;
	case Opt_nologreplay:
		btrfs_warn(NULL,
		"'nologreplay' is deprecated, use 'rescue=nologreplay' instead");
		btrfs_set_opt(ctx->mount_opt, NOLOGREPLAY);
		break;
	case Opt_norecovery:
		btrfs_info(NULL,
"'norecovery' is for compatibility only, recommended to use 'rescue=nologreplay'");
		btrfs_set_opt(ctx->mount_opt, NOLOGREPLAY);
		break;
	case Opt_flushoncommit:
		if (result.negated)
			btrfs_clear_opt(ctx->mount_opt, FLUSHONCOMMIT);
		else
			btrfs_set_opt(ctx->mount_opt, FLUSHONCOMMIT);
		break;
	case Opt_ratio:
		ctx->metadata_ratio = result.uint_32;
		break;
	case Opt_discard:
		if (result.negated) {
			btrfs_clear_opt(ctx->mount_opt, DISCARD_SYNC);
			btrfs_clear_opt(ctx->mount_opt, DISCARD_ASYNC);
			btrfs_set_opt(ctx->mount_opt, NODISCARD);
		} else {
			btrfs_set_opt(ctx->mount_opt, DISCARD_SYNC);
			btrfs_clear_opt(ctx->mount_opt, DISCARD_ASYNC);
		}
		break;
	case Opt_discard_mode:
		switch (result.uint_32) {
		case Opt_discard_sync:
			btrfs_clear_opt(ctx->mount_opt, DISCARD_ASYNC);
			btrfs_set_opt(ctx->mount_opt, DISCARD_SYNC);
			break;
		case Opt_discard_async:
			btrfs_clear_opt(ctx->mount_opt, DISCARD_SYNC);
			btrfs_set_opt(ctx->mount_opt, DISCARD_ASYNC);
			break;
		default:
			btrfs_err(NULL, "unrecognized discard mode value %s",
				  param->key);
			return -EINVAL;
		}
		btrfs_clear_opt(ctx->mount_opt, NODISCARD);
		break;
	case Opt_space_cache:
		if (result.negated) {
			btrfs_set_opt(ctx->mount_opt, NOSPACECACHE);
			btrfs_clear_opt(ctx->mount_opt, SPACE_CACHE);
			btrfs_clear_opt(ctx->mount_opt, FREE_SPACE_TREE);
		} else {
			btrfs_clear_opt(ctx->mount_opt, FREE_SPACE_TREE);
			btrfs_set_opt(ctx->mount_opt, SPACE_CACHE);
		}
		break;
	case Opt_space_cache_version:
		switch (result.uint_32) {
		case Opt_space_cache_v1:
			btrfs_set_opt(ctx->mount_opt, SPACE_CACHE);
			btrfs_clear_opt(ctx->mount_opt, FREE_SPACE_TREE);
			break;
		case Opt_space_cache_v2:
			btrfs_clear_opt(ctx->mount_opt, SPACE_CACHE);
			btrfs_set_opt(ctx->mount_opt, FREE_SPACE_TREE);
			break;
		default:
			btrfs_err(NULL, "unrecognized space_cache value %s",
				  param->key);
			return -EINVAL;
		}
		break;
	case Opt_rescan_uuid_tree:
		btrfs_set_opt(ctx->mount_opt, RESCAN_UUID_TREE);
		break;
	case Opt_clear_cache:
		btrfs_set_opt(ctx->mount_opt, CLEAR_CACHE);
		break;
	case Opt_user_subvol_rm_allowed:
		btrfs_set_opt(ctx->mount_opt, USER_SUBVOL_RM_ALLOWED);
		break;
	case Opt_enospc_debug:
		if (result.negated)
			btrfs_clear_opt(ctx->mount_opt, ENOSPC_DEBUG);
		else
			btrfs_set_opt(ctx->mount_opt, ENOSPC_DEBUG);
		break;
	case Opt_defrag:
		if (result.negated)
			btrfs_clear_opt(ctx->mount_opt, AUTO_DEFRAG);
		else
			btrfs_set_opt(ctx->mount_opt, AUTO_DEFRAG);
		break;
	case Opt_usebackuproot:
		btrfs_warn(NULL,
			   "'usebackuproot' is deprecated, use 'rescue=usebackuproot' instead");
		btrfs_set_opt(ctx->mount_opt, USEBACKUPROOT);

		/* If we're loading the backup roots we can't trust the space cache. */
		btrfs_set_opt(ctx->mount_opt, CLEAR_CACHE);
		break;
	case Opt_skip_balance:
		btrfs_set_opt(ctx->mount_opt, SKIP_BALANCE);
		break;
	case Opt_fatal_errors:
		switch (result.uint_32) {
		case Opt_fatal_errors_panic:
			btrfs_set_opt(ctx->mount_opt, PANIC_ON_FATAL_ERROR);
			break;
		case Opt_fatal_errors_bug:
			btrfs_clear_opt(ctx->mount_opt, PANIC_ON_FATAL_ERROR);
			break;
		default:
			btrfs_err(NULL, "unrecognized fatal_errors value %s",
				  param->key);
			return -EINVAL;
		}
		break;
	case Opt_commit_interval:
		ctx->commit_interval = result.uint_32;
		if (ctx->commit_interval == 0)
			ctx->commit_interval = BTRFS_DEFAULT_COMMIT_INTERVAL;
		break;
	case Opt_rescue:
		switch (result.uint_32) {
		case Opt_rescue_usebackuproot:
			btrfs_set_opt(ctx->mount_opt, USEBACKUPROOT);
			break;
		case Opt_rescue_nologreplay:
			btrfs_set_opt(ctx->mount_opt, NOLOGREPLAY);
			break;
		case Opt_rescue_ignorebadroots:
			btrfs_set_opt(ctx->mount_opt, IGNOREBADROOTS);
			break;
		case Opt_rescue_ignoredatacsums:
			btrfs_set_opt(ctx->mount_opt, IGNOREDATACSUMS);
			break;
		case Opt_rescue_ignoremetacsums:
			btrfs_set_opt(ctx->mount_opt, IGNOREMETACSUMS);
			break;
		case Opt_rescue_ignoresuperflags:
			btrfs_set_opt(ctx->mount_opt, IGNORESUPERFLAGS);
			break;
		case Opt_rescue_parameter_all:
			btrfs_set_opt(ctx->mount_opt, IGNOREDATACSUMS);
			btrfs_set_opt(ctx->mount_opt, IGNOREMETACSUMS);
			btrfs_set_opt(ctx->mount_opt, IGNORESUPERFLAGS);
			btrfs_set_opt(ctx->mount_opt, IGNOREBADROOTS);
			btrfs_set_opt(ctx->mount_opt, NOLOGREPLAY);
			break;
		default:
			btrfs_info(NULL, "unrecognized rescue option '%s'",
				   param->key);
			return -EINVAL;
		}
		break;
#ifdef CONFIG_BTRFS_DEBUG
	case Opt_fragment:
		switch (result.uint_32) {
		case Opt_fragment_parameter_all:
			btrfs_set_opt(ctx->mount_opt, FRAGMENT_DATA);
			btrfs_set_opt(ctx->mount_opt, FRAGMENT_METADATA);
			break;
		case Opt_fragment_parameter_metadata:
			btrfs_set_opt(ctx->mount_opt, FRAGMENT_METADATA);
			break;
		case Opt_fragment_parameter_data:
			btrfs_set_opt(ctx->mount_opt, FRAGMENT_DATA);
			break;
		default:
			btrfs_info(NULL, "unrecognized fragment option '%s'",
				   param->key);
			return -EINVAL;
		}
		break;
#endif
#ifdef CONFIG_BTRFS_FS_REF_VERIFY
	case Opt_ref_verify:
		btrfs_set_opt(ctx->mount_opt, REF_VERIFY);
		break;
#endif
	default:
		btrfs_err(NULL, "unrecognized mount option '%s'", param->key);
		return -EINVAL;
	}

	return 0;
}

/*
 * Some options only have meaning at mount time and shouldn't persist across
 * remounts, or be displayed. Clear these at the end of mount and remount code
 * paths.
 */
static void btrfs_clear_oneshot_options(struct btrfs_fs_info *fs_info)
{
	btrfs_clear_opt(fs_info->mount_opt, USEBACKUPROOT);
	btrfs_clear_opt(fs_info->mount_opt, CLEAR_CACHE);
	btrfs_clear_opt(fs_info->mount_opt, NOSPACECACHE);
}

static bool check_ro_option(const struct btrfs_fs_info *fs_info,
			    unsigned long long mount_opt, unsigned long long opt,
			    const char *opt_name)
{
	if (mount_opt & opt) {
		btrfs_err(fs_info, "%s must be used with ro mount option",
			  opt_name);
		return true;
	}
	return false;
}

bool btrfs_check_options(const struct btrfs_fs_info *info,
			 unsigned long long *mount_opt,
			 unsigned long flags)
{
	bool ret = true;

	if (!(flags & SB_RDONLY) &&
	    (check_ro_option(info, *mount_opt, BTRFS_MOUNT_NOLOGREPLAY, "nologreplay") ||
	     check_ro_option(info, *mount_opt, BTRFS_MOUNT_IGNOREBADROOTS, "ignorebadroots") ||
	     check_ro_option(info, *mount_opt, BTRFS_MOUNT_IGNOREDATACSUMS, "ignoredatacsums") ||
	     check_ro_option(info, *mount_opt, BTRFS_MOUNT_IGNOREMETACSUMS, "ignoremetacsums") ||
	     check_ro_option(info, *mount_opt, BTRFS_MOUNT_IGNORESUPERFLAGS, "ignoresuperflags")))
		ret = false;

	if (btrfs_fs_compat_ro(info, FREE_SPACE_TREE) &&
	    !btrfs_raw_test_opt(*mount_opt, FREE_SPACE_TREE) &&
	    !btrfs_raw_test_opt(*mount_opt, CLEAR_CACHE)) {
		btrfs_err(info, "cannot disable free-space-tree");
		ret = false;
	}
	if (btrfs_fs_compat_ro(info, BLOCK_GROUP_TREE) &&
	     !btrfs_raw_test_opt(*mount_opt, FREE_SPACE_TREE)) {
		btrfs_err(info, "cannot disable free-space-tree with block-group-tree feature");
		ret = false;
	}

	if (btrfs_check_mountopts_zoned(info, mount_opt))
		ret = false;

	if (!test_bit(BTRFS_FS_STATE_REMOUNTING, &info->fs_state)) {
		if (btrfs_raw_test_opt(*mount_opt, SPACE_CACHE)) {
			btrfs_info(info, "disk space caching is enabled");
			btrfs_warn(info,
"space cache v1 is being deprecated and will be removed in a future release, please use -o space_cache=v2");
		}
		if (btrfs_raw_test_opt(*mount_opt, FREE_SPACE_TREE))
			btrfs_info(info, "using free-space-tree");
	}

	return ret;
}

/*
 * This is subtle, we only call this during open_ctree().  We need to pre-load
 * the mount options with the on-disk settings.  Before the new mount API took
 * effect we would do this on mount and remount.  With the new mount API we'll
 * only do this on the initial mount.
 *
 * This isn't a change in behavior, because we're using the current state of the
 * file system to set the current mount options.  If you mounted with special
 * options to disable these features and then remounted we wouldn't revert the
 * settings, because mounting without these features cleared the on-disk
 * settings, so this being called on re-mount is not needed.
 */
void btrfs_set_free_space_cache_settings(struct btrfs_fs_info *fs_info)
{
	if (fs_info->sectorsize < PAGE_SIZE) {
		btrfs_clear_opt(fs_info->mount_opt, SPACE_CACHE);
		if (!btrfs_test_opt(fs_info, FREE_SPACE_TREE)) {
			btrfs_info(fs_info,
				   "forcing free space tree for sector size %u with page size %lu",
				   fs_info->sectorsize, PAGE_SIZE);
			btrfs_set_opt(fs_info->mount_opt, FREE_SPACE_TREE);
		}
	}

	/*
	 * At this point our mount options are populated, so we only mess with
	 * these settings if we don't have any settings already.
	 */
	if (btrfs_test_opt(fs_info, FREE_SPACE_TREE))
		return;

	if (btrfs_is_zoned(fs_info) &&
	    btrfs_free_space_cache_v1_active(fs_info)) {
		btrfs_info(fs_info, "zoned: clearing existing space cache");
		btrfs_set_super_cache_generation(fs_info->super_copy, 0);
		return;
	}

	if (btrfs_test_opt(fs_info, SPACE_CACHE))
		return;

	if (btrfs_test_opt(fs_info, NOSPACECACHE))
		return;

	/*
	 * At this point we don't have explicit options set by the user, set
	 * them ourselves based on the state of the file system.
	 */
	if (btrfs_fs_compat_ro(fs_info, FREE_SPACE_TREE))
		btrfs_set_opt(fs_info->mount_opt, FREE_SPACE_TREE);
	else if (btrfs_free_space_cache_v1_active(fs_info))
		btrfs_set_opt(fs_info->mount_opt, SPACE_CACHE);
}

static void set_device_specific_options(struct btrfs_fs_info *fs_info)
{
	if (!btrfs_test_opt(fs_info, NOSSD) &&
	    !fs_info->fs_devices->rotating)
		btrfs_set_opt(fs_info->mount_opt, SSD);

	/*
	 * For devices supporting discard turn on discard=async automatically,
	 * unless it's already set or disabled. This could be turned off by
	 * nodiscard for the same mount.
	 *
	 * The zoned mode piggy backs on the discard functionality for
	 * resetting a zone. There is no reason to delay the zone reset as it is
	 * fast enough. So, do not enable async discard for zoned mode.
	 */
	if (!(btrfs_test_opt(fs_info, DISCARD_SYNC) ||
	      btrfs_test_opt(fs_info, DISCARD_ASYNC) ||
	      btrfs_test_opt(fs_info, NODISCARD)) &&
	    fs_info->fs_devices->discardable &&
	    !btrfs_is_zoned(fs_info))
		btrfs_set_opt(fs_info->mount_opt, DISCARD_ASYNC);
}

char *btrfs_get_subvol_name_from_objectid(struct btrfs_fs_info *fs_info,
					  u64 subvol_objectid)
{
	struct btrfs_root *root = fs_info->tree_root;
	struct btrfs_root *fs_root = NULL;
	struct btrfs_root_ref *root_ref;
	struct btrfs_inode_ref *inode_ref;
	struct btrfs_key key;
	struct btrfs_path *path = NULL;
	char *name = NULL, *ptr;
	u64 dirid;
	int len;
	int ret;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto err;
	}

	name = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!name) {
		ret = -ENOMEM;
		goto err;
	}
	ptr = name + PATH_MAX - 1;
	ptr[0] = '\0';

	/*
	 * Walk up the subvolume trees in the tree of tree roots by root
	 * backrefs until we hit the top-level subvolume.
	 */
	while (subvol_objectid != BTRFS_FS_TREE_OBJECTID) {
		key.objectid = subvol_objectid;
		key.type = BTRFS_ROOT_BACKREF_KEY;
		key.offset = (u64)-1;

		ret = btrfs_search_backwards(root, &key, path);
		if (ret < 0) {
			goto err;
		} else if (ret > 0) {
			ret = -ENOENT;
			goto err;
		}

		subvol_objectid = key.offset;

		root_ref = btrfs_item_ptr(path->nodes[0], path->slots[0],
					  struct btrfs_root_ref);
		len = btrfs_root_ref_name_len(path->nodes[0], root_ref);
		ptr -= len + 1;
		if (ptr < name) {
			ret = -ENAMETOOLONG;
			goto err;
		}
		read_extent_buffer(path->nodes[0], ptr + 1,
				   (unsigned long)(root_ref + 1), len);
		ptr[0] = '/';
		dirid = btrfs_root_ref_dirid(path->nodes[0], root_ref);
		btrfs_release_path(path);

		fs_root = btrfs_get_fs_root(fs_info, subvol_objectid, true);
		if (IS_ERR(fs_root)) {
			ret = PTR_ERR(fs_root);
			fs_root = NULL;
			goto err;
		}

		/*
		 * Walk up the filesystem tree by inode refs until we hit the
		 * root directory.
		 */
		while (dirid != BTRFS_FIRST_FREE_OBJECTID) {
			key.objectid = dirid;
			key.type = BTRFS_INODE_REF_KEY;
			key.offset = (u64)-1;

			ret = btrfs_search_backwards(fs_root, &key, path);
			if (ret < 0) {
				goto err;
			} else if (ret > 0) {
				ret = -ENOENT;
				goto err;
			}

			dirid = key.offset;

			inode_ref = btrfs_item_ptr(path->nodes[0],
						   path->slots[0],
						   struct btrfs_inode_ref);
			len = btrfs_inode_ref_name_len(path->nodes[0],
						       inode_ref);
			ptr -= len + 1;
			if (ptr < name) {
				ret = -ENAMETOOLONG;
				goto err;
			}
			read_extent_buffer(path->nodes[0], ptr + 1,
					   (unsigned long)(inode_ref + 1), len);
			ptr[0] = '/';
			btrfs_release_path(path);
		}
		btrfs_put_root(fs_root);
		fs_root = NULL;
	}

	btrfs_free_path(path);
	if (ptr == name + PATH_MAX - 1) {
		name[0] = '/';
		name[1] = '\0';
	} else {
		memmove(name, ptr, name + PATH_MAX - ptr);
	}
	return name;

err:
	btrfs_put_root(fs_root);
	btrfs_free_path(path);
	kfree(name);
	return ERR_PTR(ret);
}

static int get_default_subvol_objectid(struct btrfs_fs_info *fs_info, u64 *objectid)
{
	struct btrfs_root *root = fs_info->tree_root;
	struct btrfs_dir_item *di;
	struct btrfs_path *path;
	struct btrfs_key location;
	struct fscrypt_str name = FSTR_INIT("default", 7);
	u64 dir_id;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	/*
	 * Find the "default" dir item which points to the root item that we
	 * will mount by default if we haven't been given a specific subvolume
	 * to mount.
	 */
	dir_id = btrfs_super_root_dir(fs_info->super_copy);
	di = btrfs_lookup_dir_item(NULL, root, path, dir_id, &name, 0);
	if (IS_ERR(di)) {
		btrfs_free_path(path);
		return PTR_ERR(di);
	}
	if (!di) {
		/*
		 * Ok the default dir item isn't there.  This is weird since
		 * it's always been there, but don't freak out, just try and
		 * mount the top-level subvolume.
		 */
		btrfs_free_path(path);
		*objectid = BTRFS_FS_TREE_OBJECTID;
		return 0;
	}

	btrfs_dir_item_key_to_cpu(path->nodes[0], di, &location);
	btrfs_free_path(path);
	*objectid = location.objectid;
	return 0;
}

static int btrfs_fill_super(struct super_block *sb,
			    struct btrfs_fs_devices *fs_devices)
{
	struct inode *inode;
	struct btrfs_fs_info *fs_info = btrfs_sb(sb);
	int err;

	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_magic = BTRFS_SUPER_MAGIC;
	sb->s_op = &btrfs_super_ops;
	sb->s_d_op = &btrfs_dentry_operations;
	sb->s_export_op = &btrfs_export_ops;
#ifdef CONFIG_FS_VERITY
	sb->s_vop = &btrfs_verityops;
#endif
	sb->s_xattr = btrfs_xattr_handlers;
	sb->s_time_gran = 1;
	sb->s_iflags |= SB_I_CGROUPWB;

	err = super_setup_bdi(sb);
	if (err) {
		btrfs_err(fs_info, "super_setup_bdi failed");
		return err;
	}

	err = open_ctree(sb, fs_devices);
	if (err) {
		btrfs_err(fs_info, "open_ctree failed");
		return err;
	}

	inode = btrfs_iget(BTRFS_FIRST_FREE_OBJECTID, fs_info->fs_root);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		btrfs_handle_fs_error(fs_info, err, NULL);
		goto fail_close;
	}

	sb->s_root = d_make_root(inode);
	if (!sb->s_root) {
		err = -ENOMEM;
		goto fail_close;
	}

	sb->s_flags |= SB_ACTIVE;
	return 0;

fail_close:
	close_ctree(fs_info);
	return err;
}

int btrfs_sync_fs(struct super_block *sb, int wait)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_fs_info *fs_info = btrfs_sb(sb);
	struct btrfs_root *root = fs_info->tree_root;

	trace_btrfs_sync_fs(fs_info, wait);

	if (!wait) {
		filemap_flush(fs_info->btree_inode->i_mapping);
		return 0;
	}

	btrfs_wait_ordered_roots(fs_info, U64_MAX, NULL);

	trans = btrfs_attach_transaction_barrier(root);
	if (IS_ERR(trans)) {
		/* no transaction, don't bother */
		if (PTR_ERR(trans) == -ENOENT) {
			/*
			 * Exit unless we have some pending changes
			 * that need to go through commit
			 */
			if (!test_bit(BTRFS_FS_NEED_TRANS_COMMIT,
				      &fs_info->flags))
				return 0;
			/*
			 * A non-blocking test if the fs is frozen. We must not
			 * start a new transaction here otherwise a deadlock
			 * happens. The pending operations are delayed to the
			 * next commit after thawing.
			 */
			if (sb_start_write_trylock(sb))
				sb_end_write(sb);
			else
				return 0;
			trans = btrfs_start_transaction(root, 0);
		}
		if (IS_ERR(trans))
			return PTR_ERR(trans);
	}
	return btrfs_commit_transaction(trans);
}

static void print_rescue_option(struct seq_file *seq, const char *s, bool *printed)
{
	seq_printf(seq, "%s%s", (*printed) ? ":" : ",rescue=", s);
	*printed = true;
}

static int btrfs_show_options(struct seq_file *seq, struct dentry *dentry)
{
	struct btrfs_fs_info *info = btrfs_sb(dentry->d_sb);
	const char *compress_type;
	const char *subvol_name;
	bool printed = false;

	if (btrfs_test_opt(info, DEGRADED))
		seq_puts(seq, ",degraded");
	if (btrfs_test_opt(info, NODATASUM))
		seq_puts(seq, ",nodatasum");
	if (btrfs_test_opt(info, NODATACOW))
		seq_puts(seq, ",nodatacow");
	if (btrfs_test_opt(info, NOBARRIER))
		seq_puts(seq, ",nobarrier");
	if (info->max_inline != BTRFS_DEFAULT_MAX_INLINE)
		seq_printf(seq, ",max_inline=%llu", info->max_inline);
	if (info->thread_pool_size !=  min_t(unsigned long,
					     num_online_cpus() + 2, 8))
		seq_printf(seq, ",thread_pool=%u", info->thread_pool_size);
	if (btrfs_test_opt(info, COMPRESS)) {
		compress_type = btrfs_compress_type2str(info->compress_type);
		if (btrfs_test_opt(info, FORCE_COMPRESS))
			seq_printf(seq, ",compress-force=%s", compress_type);
		else
			seq_printf(seq, ",compress=%s", compress_type);
		if (info->compress_level)
			seq_printf(seq, ":%d", info->compress_level);
	}
	if (btrfs_test_opt(info, NOSSD))
		seq_puts(seq, ",nossd");
	if (btrfs_test_opt(info, SSD_SPREAD))
		seq_puts(seq, ",ssd_spread");
	else if (btrfs_test_opt(info, SSD))
		seq_puts(seq, ",ssd");
	if (btrfs_test_opt(info, NOTREELOG))
		seq_puts(seq, ",notreelog");
	if (btrfs_test_opt(info, NOLOGREPLAY))
		print_rescue_option(seq, "nologreplay", &printed);
	if (btrfs_test_opt(info, USEBACKUPROOT))
		print_rescue_option(seq, "usebackuproot", &printed);
	if (btrfs_test_opt(info, IGNOREBADROOTS))
		print_rescue_option(seq, "ignorebadroots", &printed);
	if (btrfs_test_opt(info, IGNOREDATACSUMS))
		print_rescue_option(seq, "ignoredatacsums", &printed);
	if (btrfs_test_opt(info, IGNOREMETACSUMS))
		print_rescue_option(seq, "ignoremetacsums", &printed);
	if (btrfs_test_opt(info, IGNORESUPERFLAGS))
		print_rescue_option(seq, "ignoresuperflags", &printed);
	if (btrfs_test_opt(info, FLUSHONCOMMIT))
		seq_puts(seq, ",flushoncommit");
	if (btrfs_test_opt(info, DISCARD_SYNC))
		seq_puts(seq, ",discard");
	if (btrfs_test_opt(info, DISCARD_ASYNC))
		seq_puts(seq, ",discard=async");
	if (!(info->sb->s_flags & SB_POSIXACL))
		seq_puts(seq, ",noacl");
	if (btrfs_free_space_cache_v1_active(info))
		seq_puts(seq, ",space_cache");
	else if (btrfs_fs_compat_ro(info, FREE_SPACE_TREE))
		seq_puts(seq, ",space_cache=v2");
	else
		seq_puts(seq, ",nospace_cache");
	if (btrfs_test_opt(info, RESCAN_UUID_TREE))
		seq_puts(seq, ",rescan_uuid_tree");
	if (btrfs_test_opt(info, CLEAR_CACHE))
		seq_puts(seq, ",clear_cache");
	if (btrfs_test_opt(info, USER_SUBVOL_RM_ALLOWED))
		seq_puts(seq, ",user_subvol_rm_allowed");
	if (btrfs_test_opt(info, ENOSPC_DEBUG))
		seq_puts(seq, ",enospc_debug");
	if (btrfs_test_opt(info, AUTO_DEFRAG))
		seq_puts(seq, ",autodefrag");
	if (btrfs_test_opt(info, SKIP_BALANCE))
		seq_puts(seq, ",skip_balance");
	if (info->metadata_ratio)
		seq_printf(seq, ",metadata_ratio=%u", info->metadata_ratio);
	if (btrfs_test_opt(info, PANIC_ON_FATAL_ERROR))
		seq_puts(seq, ",fatal_errors=panic");
	if (info->commit_interval != BTRFS_DEFAULT_COMMIT_INTERVAL)
		seq_printf(seq, ",commit=%u", info->commit_interval);
#ifdef CONFIG_BTRFS_DEBUG
	if (btrfs_test_opt(info, FRAGMENT_DATA))
		seq_puts(seq, ",fragment=data");
	if (btrfs_test_opt(info, FRAGMENT_METADATA))
		seq_puts(seq, ",fragment=metadata");
#endif
	if (btrfs_test_opt(info, REF_VERIFY))
		seq_puts(seq, ",ref_verify");
	seq_printf(seq, ",subvolid=%llu", btrfs_root_id(BTRFS_I(d_inode(dentry))->root));
	subvol_name = btrfs_get_subvol_name_from_objectid(info,
			btrfs_root_id(BTRFS_I(d_inode(dentry))->root));
	if (!IS_ERR(subvol_name)) {
		seq_puts(seq, ",subvol=");
		seq_escape(seq, subvol_name, " \t\n\\");
		kfree(subvol_name);
	}
	return 0;
}

/*
 * subvolumes are identified by ino 256
 */
static inline int is_subvolume_inode(struct inode *inode)
{
	if (inode && inode->i_ino == BTRFS_FIRST_FREE_OBJECTID)
		return 1;
	return 0;
}

static struct dentry *mount_subvol(const char *subvol_name, u64 subvol_objectid,
				   struct vfsmount *mnt)
{
	struct dentry *root;
	int ret;

	if (!subvol_name) {
		if (!subvol_objectid) {
			ret = get_default_subvol_objectid(btrfs_sb(mnt->mnt_sb),
							  &subvol_objectid);
			if (ret) {
				root = ERR_PTR(ret);
				goto out;
			}
		}
		subvol_name = btrfs_get_subvol_name_from_objectid(
					btrfs_sb(mnt->mnt_sb), subvol_objectid);
		if (IS_ERR(subvol_name)) {
			root = ERR_CAST(subvol_name);
			subvol_name = NULL;
			goto out;
		}

	}

	root = mount_subtree(mnt, subvol_name);
	/* mount_subtree() drops our reference on the vfsmount. */
	mnt = NULL;

	if (!IS_ERR(root)) {
		struct super_block *s = root->d_sb;
		struct btrfs_fs_info *fs_info = btrfs_sb(s);
		struct inode *root_inode = d_inode(root);
		u64 root_objectid = btrfs_root_id(BTRFS_I(root_inode)->root);

		ret = 0;
		if (!is_subvolume_inode(root_inode)) {
			btrfs_err(fs_info, "'%s' is not a valid subvolume",
			       subvol_name);
			ret = -EINVAL;
		}
		if (subvol_objectid && root_objectid != subvol_objectid) {
			/*
			 * This will also catch a race condition where a
			 * subvolume which was passed by ID is renamed and
			 * another subvolume is renamed over the old location.
			 */
			btrfs_err(fs_info,
				  "subvol '%s' does not match subvolid %llu",
				  subvol_name, subvol_objectid);
			ret = -EINVAL;
		}
		if (ret) {
			dput(root);
			root = ERR_PTR(ret);
			deactivate_locked_super(s);
		}
	}

out:
	mntput(mnt);
	kfree(subvol_name);
	return root;
}

static void btrfs_resize_thread_pool(struct btrfs_fs_info *fs_info,
				     u32 new_pool_size, u32 old_pool_size)
{
	if (new_pool_size == old_pool_size)
		return;

	fs_info->thread_pool_size = new_pool_size;

	btrfs_info(fs_info, "resize thread pool %d -> %d",
	       old_pool_size, new_pool_size);

	btrfs_workqueue_set_max(fs_info->workers, new_pool_size);
	btrfs_workqueue_set_max(fs_info->delalloc_workers, new_pool_size);
	btrfs_workqueue_set_max(fs_info->caching_workers, new_pool_size);
	workqueue_set_max_active(fs_info->endio_workers, new_pool_size);
	workqueue_set_max_active(fs_info->endio_meta_workers, new_pool_size);
	btrfs_workqueue_set_max(fs_info->endio_write_workers, new_pool_size);
	btrfs_workqueue_set_max(fs_info->endio_freespace_worker, new_pool_size);
	btrfs_workqueue_set_max(fs_info->delayed_workers, new_pool_size);
}

static inline void btrfs_remount_begin(struct btrfs_fs_info *fs_info,
				       unsigned long long old_opts, int flags)
{
	if (btrfs_raw_test_opt(old_opts, AUTO_DEFRAG) &&
	    (!btrfs_raw_test_opt(fs_info->mount_opt, AUTO_DEFRAG) ||
	     (flags & SB_RDONLY))) {
		/* wait for any defraggers to finish */
		wait_event(fs_info->transaction_wait,
			   (atomic_read(&fs_info->defrag_running) == 0));
		if (flags & SB_RDONLY)
			sync_filesystem(fs_info->sb);
	}
}

static inline void btrfs_remount_cleanup(struct btrfs_fs_info *fs_info,
					 unsigned long long old_opts)
{
	const bool cache_opt = btrfs_test_opt(fs_info, SPACE_CACHE);

	/*
	 * We need to cleanup all defragable inodes if the autodefragment is
	 * close or the filesystem is read only.
	 */
	if (btrfs_raw_test_opt(old_opts, AUTO_DEFRAG) &&
	    (!btrfs_raw_test_opt(fs_info->mount_opt, AUTO_DEFRAG) || sb_rdonly(fs_info->sb))) {
		btrfs_cleanup_defrag_inodes(fs_info);
	}

	/* If we toggled discard async */
	if (!btrfs_raw_test_opt(old_opts, DISCARD_ASYNC) &&
	    btrfs_test_opt(fs_info, DISCARD_ASYNC))
		btrfs_discard_resume(fs_info);
	else if (btrfs_raw_test_opt(old_opts, DISCARD_ASYNC) &&
		 !btrfs_test_opt(fs_info, DISCARD_ASYNC))
		btrfs_discard_cleanup(fs_info);

	/* If we toggled space cache */
	if (cache_opt != btrfs_free_space_cache_v1_active(fs_info))
		btrfs_set_free_space_cache_v1_active(fs_info, cache_opt);
}

static int btrfs_remount_rw(struct btrfs_fs_info *fs_info)
{
	int ret;

	if (BTRFS_FS_ERROR(fs_info)) {
		btrfs_err(fs_info,
			  "remounting read-write after error is not allowed");
		return -EINVAL;
	}

	if (fs_info->fs_devices->rw_devices == 0)
		return -EACCES;

	if (!btrfs_check_rw_degradable(fs_info, NULL)) {
		btrfs_warn(fs_info,
			   "too many missing devices, writable remount is not allowed");
		return -EACCES;
	}

	if (btrfs_super_log_root(fs_info->super_copy) != 0) {
		btrfs_warn(fs_info,
			   "mount required to replay tree-log, cannot remount read-write");
		return -EINVAL;
	}

	/*
	 * NOTE: when remounting with a change that does writes, don't put it
	 * anywhere above this point, as we are not sure to be safe to write
	 * until we pass the above checks.
	 */
	ret = btrfs_start_pre_rw_mount(fs_info);
	if (ret)
		return ret;

	btrfs_clear_sb_rdonly(fs_info->sb);

	set_bit(BTRFS_FS_OPEN, &fs_info->flags);

	/*
	 * If we've gone from readonly -> read-write, we need to get our
	 * sync/async discard lists in the right state.
	 */
	btrfs_discard_resume(fs_info);

	return 0;
}

static int btrfs_remount_ro(struct btrfs_fs_info *fs_info)
{
	/*
	 * This also happens on 'umount -rf' or on shutdown, when the
	 * filesystem is busy.
	 */
	cancel_work_sync(&fs_info->async_reclaim_work);
	cancel_work_sync(&fs_info->async_data_reclaim_work);

	btrfs_discard_cleanup(fs_info);

	/* Wait for the uuid_scan task to finish */
	down(&fs_info->uuid_tree_rescan_sem);
	/* Avoid complains from lockdep et al. */
	up(&fs_info->uuid_tree_rescan_sem);

	btrfs_set_sb_rdonly(fs_info->sb);

	/*
	 * Setting SB_RDONLY will put the cleaner thread to sleep at the next
	 * loop if it's already active.  If it's already asleep, we'll leave
	 * unused block groups on disk until we're mounted read-write again
	 * unless we clean them up here.
	 */
	btrfs_delete_unused_bgs(fs_info);

	/*
	 * The cleaner task could be already running before we set the flag
	 * BTRFS_FS_STATE_RO (and SB_RDONLY in the superblock).  We must make
	 * sure that after we finish the remount, i.e. after we call
	 * btrfs_commit_super(), the cleaner can no longer start a transaction
	 * - either because it was dropping a dead root, running delayed iputs
	 *   or deleting an unused block group (the cleaner picked a block
	 *   group from the list of unused block groups before we were able to
	 *   in the previous call to btrfs_delete_unused_bgs()).
	 */
	wait_on_bit(&fs_info->flags, BTRFS_FS_CLEANER_RUNNING, TASK_UNINTERRUPTIBLE);

	/*
	 * We've set the superblock to RO mode, so we might have made the
	 * cleaner task sleep without running all pending delayed iputs. Go
	 * through all the delayed iputs here, so that if an unmount happens
	 * without remounting RW we don't end up at finishing close_ctree()
	 * with a non-empty list of delayed iputs.
	 */
	btrfs_run_delayed_iputs(fs_info);

	btrfs_dev_replace_suspend_for_unmount(fs_info);
	btrfs_scrub_cancel(fs_info);
	btrfs_pause_balance(fs_info);

	/*
	 * Pause the qgroup rescan worker if it is running. We don't want it to
	 * be still running after we are in RO mode, as after that, by the time
	 * we unmount, it might have left a transaction open, so we would leak
	 * the transaction and/or crash.
	 */
	btrfs_qgroup_wait_for_completion(fs_info, false);

	return btrfs_commit_super(fs_info);
}

static void btrfs_ctx_to_info(struct btrfs_fs_info *fs_info, struct btrfs_fs_context *ctx)
{
	fs_info->max_inline = ctx->max_inline;
	fs_info->commit_interval = ctx->commit_interval;
	fs_info->metadata_ratio = ctx->metadata_ratio;
	fs_info->thread_pool_size = ctx->thread_pool_size;
	fs_info->mount_opt = ctx->mount_opt;
	fs_info->compress_type = ctx->compress_type;
	fs_info->compress_level = ctx->compress_level;
}

static void btrfs_info_to_ctx(struct btrfs_fs_info *fs_info, struct btrfs_fs_context *ctx)
{
	ctx->max_inline = fs_info->max_inline;
	ctx->commit_interval = fs_info->commit_interval;
	ctx->metadata_ratio = fs_info->metadata_ratio;
	ctx->thread_pool_size = fs_info->thread_pool_size;
	ctx->mount_opt = fs_info->mount_opt;
	ctx->compress_type = fs_info->compress_type;
	ctx->compress_level = fs_info->compress_level;
}

#define btrfs_info_if_set(fs_info, old_ctx, opt, fmt, args...)			\
do {										\
	if ((!old_ctx || !btrfs_raw_test_opt(old_ctx->mount_opt, opt)) &&	\
	    btrfs_raw_test_opt(fs_info->mount_opt, opt))			\
		btrfs_info(fs_info, fmt, ##args);				\
} while (0)

#define btrfs_info_if_unset(fs_info, old_ctx, opt, fmt, args...)	\
do {									\
	if ((old_ctx && btrfs_raw_test_opt(old_ctx->mount_opt, opt)) &&	\
	    !btrfs_raw_test_opt(fs_info->mount_opt, opt))		\
		btrfs_info(fs_info, fmt, ##args);			\
} while (0)

static void btrfs_emit_options(struct btrfs_fs_info *info,
			       struct btrfs_fs_context *old)
{
	btrfs_info_if_set(info, old, NODATASUM, "setting nodatasum");
	btrfs_info_if_set(info, old, DEGRADED, "allowing degraded mounts");
	btrfs_info_if_set(info, old, NODATASUM, "setting nodatasum");
	btrfs_info_if_set(info, old, SSD, "enabling ssd optimizations");
	btrfs_info_if_set(info, old, SSD_SPREAD, "using spread ssd allocation scheme");
	btrfs_info_if_set(info, old, NOBARRIER, "turning off barriers");
	btrfs_info_if_set(info, old, NOTREELOG, "disabling tree log");
	btrfs_info_if_set(info, old, NOLOGREPLAY, "disabling log replay at mount time");
	btrfs_info_if_set(info, old, FLUSHONCOMMIT, "turning on flush-on-commit");
	btrfs_info_if_set(info, old, DISCARD_SYNC, "turning on sync discard");
	btrfs_info_if_set(info, old, DISCARD_ASYNC, "turning on async discard");
	btrfs_info_if_set(info, old, FREE_SPACE_TREE, "enabling free space tree");
	btrfs_info_if_set(info, old, SPACE_CACHE, "enabling disk space caching");
	btrfs_info_if_set(info, old, CLEAR_CACHE, "force clearing of disk cache");
	btrfs_info_if_set(info, old, AUTO_DEFRAG, "enabling auto defrag");
	btrfs_info_if_set(info, old, FRAGMENT_DATA, "fragmenting data");
	btrfs_info_if_set(info, old, FRAGMENT_METADATA, "fragmenting metadata");
	btrfs_info_if_set(info, old, REF_VERIFY, "doing ref verification");
	btrfs_info_if_set(info, old, USEBACKUPROOT, "trying to use backup root at mount time");
	btrfs_info_if_set(info, old, IGNOREBADROOTS, "ignoring bad roots");
	btrfs_info_if_set(info, old, IGNOREDATACSUMS, "ignoring data csums");
	btrfs_info_if_set(info, old, IGNOREMETACSUMS, "ignoring meta csums");
	btrfs_info_if_set(info, old, IGNORESUPERFLAGS, "ignoring unknown super block flags");

	btrfs_info_if_unset(info, old, NODATACOW, "setting datacow");
	btrfs_info_if_unset(info, old, SSD, "not using ssd optimizations");
	btrfs_info_if_unset(info, old, SSD_SPREAD, "not using spread ssd allocation scheme");
	btrfs_info_if_unset(info, old, NOBARRIER, "turning off barriers");
	btrfs_info_if_unset(info, old, NOTREELOG, "enabling tree log");
	btrfs_info_if_unset(info, old, SPACE_CACHE, "disabling disk space caching");
	btrfs_info_if_unset(info, old, FREE_SPACE_TREE, "disabling free space tree");
	btrfs_info_if_unset(info, old, AUTO_DEFRAG, "disabling auto defrag");
	btrfs_info_if_unset(info, old, COMPRESS, "use no compression");

	/* Did the compression settings change? */
	if (btrfs_test_opt(info, COMPRESS) &&
	    (!old ||
	     old->compress_type != info->compress_type ||
	     old->compress_level != info->compress_level ||
	     (!btrfs_raw_test_opt(old->mount_opt, FORCE_COMPRESS) &&
	      btrfs_raw_test_opt(info->mount_opt, FORCE_COMPRESS)))) {
		const char *compress_type = btrfs_compress_type2str(info->compress_type);

		btrfs_info(info, "%s %s compression, level %d",
			   btrfs_test_opt(info, FORCE_COMPRESS) ? "force" : "use",
			   compress_type, info->compress_level);
	}

	if (info->max_inline != BTRFS_DEFAULT_MAX_INLINE)
		btrfs_info(info, "max_inline set to %llu", info->max_inline);
}

static int btrfs_reconfigure(struct fs_context *fc)
{
	struct super_block *sb = fc->root->d_sb;
	struct btrfs_fs_info *fs_info = btrfs_sb(sb);
	struct btrfs_fs_context *ctx = fc->fs_private;
	struct btrfs_fs_context old_ctx;
	int ret = 0;
	bool mount_reconfigure = (fc->s_fs_info != NULL);

	btrfs_info_to_ctx(fs_info, &old_ctx);

	/*
	 * This is our "bind mount" trick, we don't want to allow the user to do
	 * anything other than mount a different ro/rw and a different subvol,
	 * all of the mount options should be maintained.
	 */
	if (mount_reconfigure)
		ctx->mount_opt = old_ctx.mount_opt;

	sync_filesystem(sb);
	set_bit(BTRFS_FS_STATE_REMOUNTING, &fs_info->fs_state);

	if (!btrfs_check_options(fs_info, &ctx->mount_opt, fc->sb_flags))
		return -EINVAL;

	ret = btrfs_check_features(fs_info, !(fc->sb_flags & SB_RDONLY));
	if (ret < 0)
		return ret;

	btrfs_ctx_to_info(fs_info, ctx);
	btrfs_remount_begin(fs_info, old_ctx.mount_opt, fc->sb_flags);
	btrfs_resize_thread_pool(fs_info, fs_info->thread_pool_size,
				 old_ctx.thread_pool_size);

	if ((bool)btrfs_test_opt(fs_info, FREE_SPACE_TREE) !=
	    (bool)btrfs_fs_compat_ro(fs_info, FREE_SPACE_TREE) &&
	    (!sb_rdonly(sb) || (fc->sb_flags & SB_RDONLY))) {
		btrfs_warn(fs_info,
		"remount supports changing free space tree only from RO to RW");
		/* Make sure free space cache options match the state on disk. */
		if (btrfs_fs_compat_ro(fs_info, FREE_SPACE_TREE)) {
			btrfs_set_opt(fs_info->mount_opt, FREE_SPACE_TREE);
			btrfs_clear_opt(fs_info->mount_opt, SPACE_CACHE);
		}
		if (btrfs_free_space_cache_v1_active(fs_info)) {
			btrfs_clear_opt(fs_info->mount_opt, FREE_SPACE_TREE);
			btrfs_set_opt(fs_info->mount_opt, SPACE_CACHE);
		}
	}

	ret = 0;
	if (!sb_rdonly(sb) && (fc->sb_flags & SB_RDONLY))
		ret = btrfs_remount_ro(fs_info);
	else if (sb_rdonly(sb) && !(fc->sb_flags & SB_RDONLY))
		ret = btrfs_remount_rw(fs_info);
	if (ret)
		goto restore;

	/*
	 * If we set the mask during the parameter parsing VFS would reject the
	 * remount.  Here we can set the mask and the value will be updated
	 * appropriately.
	 */
	if ((fc->sb_flags & SB_POSIXACL) != (sb->s_flags & SB_POSIXACL))
		fc->sb_flags_mask |= SB_POSIXACL;

	btrfs_emit_options(fs_info, &old_ctx);
	wake_up_process(fs_info->transaction_kthread);
	btrfs_remount_cleanup(fs_info, old_ctx.mount_opt);
	btrfs_clear_oneshot_options(fs_info);
	clear_bit(BTRFS_FS_STATE_REMOUNTING, &fs_info->fs_state);

	return 0;
restore:
	btrfs_ctx_to_info(fs_info, &old_ctx);
	btrfs_remount_cleanup(fs_info, old_ctx.mount_opt);
	clear_bit(BTRFS_FS_STATE_REMOUNTING, &fs_info->fs_state);
	return ret;
}

/* Used to sort the devices by max_avail(descending sort) */
static int btrfs_cmp_device_free_bytes(const void *a, const void *b)
{
	const struct btrfs_device_info *dev_info1 = a;
	const struct btrfs_device_info *dev_info2 = b;

	if (dev_info1->max_avail > dev_info2->max_avail)
		return -1;
	else if (dev_info1->max_avail < dev_info2->max_avail)
		return 1;
	return 0;
}

/*
 * sort the devices by max_avail, in which max free extent size of each device
 * is stored.(Descending Sort)
 */
static inline void btrfs_descending_sort_devices(
					struct btrfs_device_info *devices,
					size_t nr_devices)
{
	sort(devices, nr_devices, sizeof(struct btrfs_device_info),
	     btrfs_cmp_device_free_bytes, NULL);
}

/*
 * The helper to calc the free space on the devices that can be used to store
 * file data.
 */
static inline int btrfs_calc_avail_data_space(struct btrfs_fs_info *fs_info,
					      u64 *free_bytes)
{
	struct btrfs_device_info *devices_info;
	struct btrfs_fs_devices *fs_devices = fs_info->fs_devices;
	struct btrfs_device *device;
	u64 type;
	u64 avail_space;
	u64 min_stripe_size;
	int num_stripes = 1;
	int i = 0, nr_devices;
	const struct btrfs_raid_attr *rattr;

	/*
	 * We aren't under the device list lock, so this is racy-ish, but good
	 * enough for our purposes.
	 */
	nr_devices = fs_info->fs_devices->open_devices;
	if (!nr_devices) {
		smp_mb();
		nr_devices = fs_info->fs_devices->open_devices;
		ASSERT(nr_devices);
		if (!nr_devices) {
			*free_bytes = 0;
			return 0;
		}
	}

	devices_info = kmalloc_array(nr_devices, sizeof(*devices_info),
			       GFP_KERNEL);
	if (!devices_info)
		return -ENOMEM;

	/* calc min stripe number for data space allocation */
	type = btrfs_data_alloc_profile(fs_info);
	rattr = &btrfs_raid_array[btrfs_bg_flags_to_raid_index(type)];

	if (type & BTRFS_BLOCK_GROUP_RAID0)
		num_stripes = nr_devices;
	else if (type & BTRFS_BLOCK_GROUP_RAID1_MASK)
		num_stripes = rattr->ncopies;
	else if (type & BTRFS_BLOCK_GROUP_RAID10)
		num_stripes = 4;

	/* Adjust for more than 1 stripe per device */
	min_stripe_size = rattr->dev_stripes * BTRFS_STRIPE_LEN;

	rcu_read_lock();
	list_for_each_entry_rcu(device, &fs_devices->devices, dev_list) {
		if (!test_bit(BTRFS_DEV_STATE_IN_FS_METADATA,
						&device->dev_state) ||
		    !device->bdev ||
		    test_bit(BTRFS_DEV_STATE_REPLACE_TGT, &device->dev_state))
			continue;

		if (i >= nr_devices)
			break;

		avail_space = device->total_bytes - device->bytes_used;

		/* align with stripe_len */
		avail_space = rounddown(avail_space, BTRFS_STRIPE_LEN);

		/*
		 * Ensure we have at least min_stripe_size on top of the
		 * reserved space on the device.
		 */
		if (avail_space <= BTRFS_DEVICE_RANGE_RESERVED + min_stripe_size)
			continue;

		avail_space -= BTRFS_DEVICE_RANGE_RESERVED;

		devices_info[i].dev = device;
		devices_info[i].max_avail = avail_space;

		i++;
	}
	rcu_read_unlock();

	nr_devices = i;

	btrfs_descending_sort_devices(devices_info, nr_devices);

	i = nr_devices - 1;
	avail_space = 0;
	while (nr_devices >= rattr->devs_min) {
		num_stripes = min(num_stripes, nr_devices);

		if (devices_info[i].max_avail >= min_stripe_size) {
			int j;
			u64 alloc_size;

			avail_space += devices_info[i].max_avail * num_stripes;
			alloc_size = devices_info[i].max_avail;
			for (j = i + 1 - num_stripes; j <= i; j++)
				devices_info[j].max_avail -= alloc_size;
		}
		i--;
		nr_devices--;
	}

	kfree(devices_info);
	*free_bytes = avail_space;
	return 0;
}

/*
 * Calculate numbers for 'df', pessimistic in case of mixed raid profiles.
 *
 * If there's a redundant raid level at DATA block groups, use the respective
 * multiplier to scale the sizes.
 *
 * Unused device space usage is based on simulating the chunk allocator
 * algorithm that respects the device sizes and order of allocations.  This is
 * a close approximation of the actual use but there are other factors that may
 * change the result (like a new metadata chunk).
 *
 * If metadata is exhausted, f_bavail will be 0.
 */
static int btrfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(dentry->d_sb);
	struct btrfs_super_block *disk_super = fs_info->super_copy;
	struct btrfs_space_info *found;
	u64 total_used = 0;
	u64 total_free_data = 0;
	u64 total_free_meta = 0;
	u32 bits = fs_info->sectorsize_bits;
	__be32 *fsid = (__be32 *)fs_info->fs_devices->fsid;
	unsigned factor = 1;
	struct btrfs_block_rsv *block_rsv = &fs_info->global_block_rsv;
	int ret;
	u64 thresh = 0;
	int mixed = 0;

	list_for_each_entry(found, &fs_info->space_info, list) {
		if (found->flags & BTRFS_BLOCK_GROUP_DATA) {
			int i;

			total_free_data += found->disk_total - found->disk_used;
			total_free_data -=
				btrfs_account_ro_block_groups_free_space(found);

			for (i = 0; i < BTRFS_NR_RAID_TYPES; i++) {
				if (!list_empty(&found->block_groups[i]))
					factor = btrfs_bg_type_to_factor(
						btrfs_raid_array[i].bg_flag);
			}
		}

		/*
		 * Metadata in mixed block group profiles are accounted in data
		 */
		if (!mixed && found->flags & BTRFS_BLOCK_GROUP_METADATA) {
			if (found->flags & BTRFS_BLOCK_GROUP_DATA)
				mixed = 1;
			else
				total_free_meta += found->disk_total -
					found->disk_used;
		}

		total_used += found->disk_used;
	}

	buf->f_blocks = div_u64(btrfs_super_total_bytes(disk_super), factor);
	buf->f_blocks >>= bits;
	buf->f_bfree = buf->f_blocks - (div_u64(total_used, factor) >> bits);

	/* Account global block reserve as used, it's in logical size already */
	spin_lock(&block_rsv->lock);
	/* Mixed block groups accounting is not byte-accurate, avoid overflow */
	if (buf->f_bfree >= block_rsv->size >> bits)
		buf->f_bfree -= block_rsv->size >> bits;
	else
		buf->f_bfree = 0;
	spin_unlock(&block_rsv->lock);

	buf->f_bavail = div_u64(total_free_data, factor);
	ret = btrfs_calc_avail_data_space(fs_info, &total_free_data);
	if (ret)
		return ret;
	buf->f_bavail += div_u64(total_free_data, factor);
	buf->f_bavail = buf->f_bavail >> bits;

	/*
	 * We calculate the remaining metadata space minus global reserve. If
	 * this is (supposedly) smaller than zero, there's no space. But this
	 * does not hold in practice, the exhausted state happens where's still
	 * some positive delta. So we apply some guesswork and compare the
	 * delta to a 4M threshold.  (Practically observed delta was ~2M.)
	 *
	 * We probably cannot calculate the exact threshold value because this
	 * depends on the internal reservations requested by various
	 * operations, so some operations that consume a few metadata will
	 * succeed even if the Avail is zero. But this is better than the other
	 * way around.
	 */
	thresh = SZ_4M;

	/*
	 * We only want to claim there's no available space if we can no longer
	 * allocate chunks for our metadata profile and our global reserve will
	 * not fit in the free metadata space.  If we aren't ->full then we
	 * still can allocate chunks and thus are fine using the currently
	 * calculated f_bavail.
	 */
	if (!mixed && block_rsv->space_info->full &&
	    (total_free_meta < thresh || total_free_meta - thresh < block_rsv->size))
		buf->f_bavail = 0;

	buf->f_type = BTRFS_SUPER_MAGIC;
	buf->f_bsize = fs_info->sectorsize;
	buf->f_namelen = BTRFS_NAME_LEN;

	/* We treat it as constant endianness (it doesn't matter _which_)
	   because we want the fsid to come out the same whether mounted
	   on a big-endian or little-endian host */
	buf->f_fsid.val[0] = be32_to_cpu(fsid[0]) ^ be32_to_cpu(fsid[2]);
	buf->f_fsid.val[1] = be32_to_cpu(fsid[1]) ^ be32_to_cpu(fsid[3]);
	/* Mask in the root object ID too, to disambiguate subvols */
	buf->f_fsid.val[0] ^= btrfs_root_id(BTRFS_I(d_inode(dentry))->root) >> 32;
	buf->f_fsid.val[1] ^= btrfs_root_id(BTRFS_I(d_inode(dentry))->root);

	return 0;
}

static int btrfs_fc_test_super(struct super_block *sb, struct fs_context *fc)
{
	struct btrfs_fs_info *p = fc->s_fs_info;
	struct btrfs_fs_info *fs_info = btrfs_sb(sb);

	return fs_info->fs_devices == p->fs_devices;
}

static int btrfs_get_tree_super(struct fs_context *fc)
{
	struct btrfs_fs_info *fs_info = fc->s_fs_info;
	struct btrfs_fs_context *ctx = fc->fs_private;
	struct btrfs_fs_devices *fs_devices = NULL;
	struct block_device *bdev;
	struct btrfs_device *device;
	struct super_block *sb;
	blk_mode_t mode = btrfs_open_mode(fc);
	int ret;

	btrfs_ctx_to_info(fs_info, ctx);
	mutex_lock(&uuid_mutex);

	/*
	 * With 'true' passed to btrfs_scan_one_device() (mount time) we expect
	 * either a valid device or an error.
	 */
	device = btrfs_scan_one_device(fc->source, mode, true);
	ASSERT(device != NULL);
	if (IS_ERR(device)) {
		mutex_unlock(&uuid_mutex);
		return PTR_ERR(device);
	}

	fs_devices = device->fs_devices;
	fs_info->fs_devices = fs_devices;

	ret = btrfs_open_devices(fs_devices, mode, &btrfs_fs_type);
	mutex_unlock(&uuid_mutex);
	if (ret)
		return ret;

	if (!(fc->sb_flags & SB_RDONLY) && fs_devices->rw_devices == 0) {
		ret = -EACCES;
		goto error;
	}

	bdev = fs_devices->latest_dev->bdev;

	/*
	 * From now on the error handling is not straightforward.
	 *
	 * If successful, this will transfer the fs_info into the super block,
	 * and fc->s_fs_info will be NULL.  However if there's an existing
	 * super, we'll still have fc->s_fs_info populated.  If we error
	 * completely out it'll be cleaned up when we drop the fs_context,
	 * otherwise it's tied to the lifetime of the super_block.
	 */
	sb = sget_fc(fc, btrfs_fc_test_super, set_anon_super_fc);
	if (IS_ERR(sb)) {
		ret = PTR_ERR(sb);
		goto error;
	}

	set_device_specific_options(fs_info);

	if (sb->s_root) {
		btrfs_close_devices(fs_devices);
		if ((fc->sb_flags ^ sb->s_flags) & SB_RDONLY)
			ret = -EBUSY;
	} else {
		snprintf(sb->s_id, sizeof(sb->s_id), "%pg", bdev);
		shrinker_debugfs_rename(sb->s_shrink, "sb-btrfs:%s", sb->s_id);
		btrfs_sb(sb)->bdev_holder = &btrfs_fs_type;
		ret = btrfs_fill_super(sb, fs_devices);
	}

	if (ret) {
		deactivate_locked_super(sb);
		return ret;
	}

	btrfs_clear_oneshot_options(fs_info);

	fc->root = dget(sb->s_root);
	return 0;

error:
	btrfs_close_devices(fs_devices);
	return ret;
}

/*
 * Ever since commit 0723a0473fb4 ("btrfs: allow mounting btrfs subvolumes
 * with different ro/rw options") the following works:
 *
 *        (i) mount /dev/sda3 -o subvol=foo,ro /mnt/foo
 *       (ii) mount /dev/sda3 -o subvol=bar,rw /mnt/bar
 *
 * which looks nice and innocent but is actually pretty intricate and deserves
 * a long comment.
 *
 * On another filesystem a subvolume mount is close to something like:
 *
 *	(iii) # create rw superblock + initial mount
 *	      mount -t xfs /dev/sdb /opt/
 *
 *	      # create ro bind mount
 *	      mount --bind -o ro /opt/foo /mnt/foo
 *
 *	      # unmount initial mount
 *	      umount /opt
 *
 * Of course, there's some special subvolume sauce and there's the fact that the
 * sb->s_root dentry is really swapped after mount_subtree(). But conceptually
 * it's very close and will help us understand the issue.
 *
 * The old mount API didn't cleanly distinguish between a mount being made ro
 * and a superblock being made ro.  The only way to change the ro state of
 * either object was by passing ms_rdonly. If a new mount was created via
 * mount(2) such as:
 *
 *      mount("/dev/sdb", "/mnt", "xfs", ms_rdonly, null);
 *
 * the MS_RDONLY flag being specified had two effects:
 *
 * (1) MNT_READONLY was raised -> the resulting mount got
 *     @mnt->mnt_flags |= MNT_READONLY raised.
 *
 * (2) MS_RDONLY was passed to the filesystem's mount method and the filesystems
 *     made the superblock ro. Note, how SB_RDONLY has the same value as
 *     ms_rdonly and is raised whenever MS_RDONLY is passed through mount(2).
 *
 * Creating a subtree mount via (iii) ends up leaving a rw superblock with a
 * subtree mounted ro.
 *
 * But consider the effect on the old mount API on btrfs subvolume mounting
 * which combines the distinct step in (iii) into a single step.
 *
 * By issuing (i) both the mount and the superblock are turned ro. Now when (ii)
 * is issued the superblock is ro and thus even if the mount created for (ii) is
 * rw it wouldn't help. Hence, btrfs needed to transition the superblock from ro
 * to rw for (ii) which it did using an internal remount call.
 *
 * IOW, subvolume mounting was inherently complicated due to the ambiguity of
 * MS_RDONLY in mount(2). Note, this ambiguity has mount(8) always translate
 * "ro" to MS_RDONLY. IOW, in both (i) and (ii) "ro" becomes MS_RDONLY when
 * passed by mount(8) to mount(2).
 *
 * Enter the new mount API. The new mount API disambiguates making a mount ro
 * and making a superblock ro.
 *
 * (3) To turn a mount ro the MOUNT_ATTR_ONLY flag can be used with either
 *     fsmount() or mount_setattr() this is a pure VFS level change for a
 *     specific mount or mount tree that is never seen by the filesystem itself.
 *
 * (4) To turn a superblock ro the "ro" flag must be used with
 *     fsconfig(FSCONFIG_SET_FLAG, "ro"). This option is seen by the filesystem
 *     in fc->sb_flags.
 *
 * But, currently the util-linux mount command already utilizes the new mount
 * API and is still setting fsconfig(FSCONFIG_SET_FLAG, "ro") no matter if it's
 * btrfs or not, setting the whole super block RO.  To make per-subvolume mounting
 * work with different options work we need to keep backward compatibility.
 */
static struct vfsmount *btrfs_reconfigure_for_mount(struct fs_context *fc)
{
	struct vfsmount *mnt;
	int ret;
	const bool ro2rw = !(fc->sb_flags & SB_RDONLY);

	/*
	 * We got an EBUSY because our SB_RDONLY flag didn't match the existing
	 * super block, so invert our setting here and retry the mount so we
	 * can get our vfsmount.
	 */
	if (ro2rw)
		fc->sb_flags |= SB_RDONLY;
	else
		fc->sb_flags &= ~SB_RDONLY;

	mnt = fc_mount(fc);
	if (IS_ERR(mnt))
		return mnt;

	if (!ro2rw)
		return mnt;

	/* We need to convert to rw, call reconfigure. */
	fc->sb_flags &= ~SB_RDONLY;
	down_write(&mnt->mnt_sb->s_umount);
	ret = btrfs_reconfigure(fc);
	up_write(&mnt->mnt_sb->s_umount);
	if (ret) {
		mntput(mnt);
		return ERR_PTR(ret);
	}
	return mnt;
}

static int btrfs_get_tree_subvol(struct fs_context *fc)
{
	struct btrfs_fs_info *fs_info = NULL;
	struct btrfs_fs_context *ctx = fc->fs_private;
	struct fs_context *dup_fc;
	struct dentry *dentry;
	struct vfsmount *mnt;

	/*
	 * Setup a dummy root and fs_info for test/set super.  This is because
	 * we don't actually fill this stuff out until open_ctree, but we need
	 * then open_ctree will properly initialize the file system specific
	 * settings later.  btrfs_init_fs_info initializes the static elements
	 * of the fs_info (locks and such) to make cleanup easier if we find a
	 * superblock with our given fs_devices later on at sget() time.
	 */
	fs_info = kvzalloc(sizeof(struct btrfs_fs_info), GFP_KERNEL);
	if (!fs_info)
		return -ENOMEM;

	fs_info->super_copy = kzalloc(BTRFS_SUPER_INFO_SIZE, GFP_KERNEL);
	fs_info->super_for_commit = kzalloc(BTRFS_SUPER_INFO_SIZE, GFP_KERNEL);
	if (!fs_info->super_copy || !fs_info->super_for_commit) {
		btrfs_free_fs_info(fs_info);
		return -ENOMEM;
	}
	btrfs_init_fs_info(fs_info);

	dup_fc = vfs_dup_fs_context(fc);
	if (IS_ERR(dup_fc)) {
		btrfs_free_fs_info(fs_info);
		return PTR_ERR(dup_fc);
	}

	/*
	 * When we do the sget_fc this gets transferred to the sb, so we only
	 * need to set it on the dup_fc as this is what creates the super block.
	 */
	dup_fc->s_fs_info = fs_info;

	/*
	 * We'll do the security settings in our btrfs_get_tree_super() mount
	 * loop, they were duplicated into dup_fc, we can drop the originals
	 * here.
	 */
	security_free_mnt_opts(&fc->security);
	fc->security = NULL;

	mnt = fc_mount(dup_fc);
	if (PTR_ERR_OR_ZERO(mnt) == -EBUSY)
		mnt = btrfs_reconfigure_for_mount(dup_fc);
	put_fs_context(dup_fc);
	if (IS_ERR(mnt))
		return PTR_ERR(mnt);

	/*
	 * This free's ->subvol_name, because if it isn't set we have to
	 * allocate a buffer to hold the subvol_name, so we just drop our
	 * reference to it here.
	 */
	dentry = mount_subvol(ctx->subvol_name, ctx->subvol_objectid, mnt);
	ctx->subvol_name = NULL;
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);

	fc->root = dentry;
	return 0;
}

static int btrfs_get_tree(struct fs_context *fc)
{
	/*
	 * Since we use mount_subtree to mount the default/specified subvol, we
	 * have to do mounts in two steps.
	 *
	 * First pass through we call btrfs_get_tree_subvol(), this is just a
	 * wrapper around fc_mount() to call back into here again, and this time
	 * we'll call btrfs_get_tree_super().  This will do the open_ctree() and
	 * everything to open the devices and file system.  Then we return back
	 * with a fully constructed vfsmount in btrfs_get_tree_subvol(), and
	 * from there we can do our mount_subvol() call, which will lookup
	 * whichever subvol we're mounting and setup this fc with the
	 * appropriate dentry for the subvol.
	 */
	if (fc->s_fs_info)
		return btrfs_get_tree_super(fc);
	return btrfs_get_tree_subvol(fc);
}

static void btrfs_kill_super(struct super_block *sb)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(sb);
	kill_anon_super(sb);
	btrfs_free_fs_info(fs_info);
}

static void btrfs_free_fs_context(struct fs_context *fc)
{
	struct btrfs_fs_context *ctx = fc->fs_private;
	struct btrfs_fs_info *fs_info = fc->s_fs_info;

	if (fs_info)
		btrfs_free_fs_info(fs_info);

	if (ctx && refcount_dec_and_test(&ctx->refs)) {
		kfree(ctx->subvol_name);
		kfree(ctx);
	}
}

static int btrfs_dup_fs_context(struct fs_context *fc, struct fs_context *src_fc)
{
	struct btrfs_fs_context *ctx = src_fc->fs_private;

	/*
	 * Give a ref to our ctx to this dup, as we want to keep it around for
	 * our original fc so we can have the subvolume name or objectid.
	 *
	 * We unset ->source in the original fc because the dup needs it for
	 * mounting, and then once we free the dup it'll free ->source, so we
	 * need to make sure we're only pointing to it in one fc.
	 */
	refcount_inc(&ctx->refs);
	fc->fs_private = ctx;
	fc->source = src_fc->source;
	src_fc->source = NULL;
	return 0;
}

static const struct fs_context_operations btrfs_fs_context_ops = {
	.parse_param	= btrfs_parse_param,
	.reconfigure	= btrfs_reconfigure,
	.get_tree	= btrfs_get_tree,
	.dup		= btrfs_dup_fs_context,
	.free		= btrfs_free_fs_context,
};

static int btrfs_init_fs_context(struct fs_context *fc)
{
	struct btrfs_fs_context *ctx;

	ctx = kzalloc(sizeof(struct btrfs_fs_context), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	refcount_set(&ctx->refs, 1);
	fc->fs_private = ctx;
	fc->ops = &btrfs_fs_context_ops;

	if (fc->purpose == FS_CONTEXT_FOR_RECONFIGURE) {
		btrfs_info_to_ctx(btrfs_sb(fc->root->d_sb), ctx);
	} else {
		ctx->thread_pool_size =
			min_t(unsigned long, num_online_cpus() + 2, 8);
		ctx->max_inline = BTRFS_DEFAULT_MAX_INLINE;
		ctx->commit_interval = BTRFS_DEFAULT_COMMIT_INTERVAL;
	}

#ifdef CONFIG_BTRFS_FS_POSIX_ACL
	fc->sb_flags |= SB_POSIXACL;
#endif
	fc->sb_flags |= SB_I_VERSION;

	return 0;
}

static struct file_system_type btrfs_fs_type = {
	.owner			= THIS_MODULE,
	.name			= "btrfs",
	.init_fs_context	= btrfs_init_fs_context,
	.parameters		= btrfs_fs_parameters,
	.kill_sb		= btrfs_kill_super,
	.fs_flags		= FS_REQUIRES_DEV | FS_BINARY_MOUNTDATA |
				  FS_ALLOW_IDMAP | FS_MGTIME,
 };

MODULE_ALIAS_FS("btrfs");

static int btrfs_control_open(struct inode *inode, struct file *file)
{
	/*
	 * The control file's private_data is used to hold the
	 * transaction when it is started and is used to keep
	 * track of whether a transaction is already in progress.
	 */
	file->private_data = NULL;
	return 0;
}

/*
 * Used by /dev/btrfs-control for devices ioctls.
 */
static long btrfs_control_ioctl(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	struct btrfs_ioctl_vol_args *vol;
	struct btrfs_device *device = NULL;
	dev_t devt = 0;
	int ret = -ENOTTY;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	vol = memdup_user((void __user *)arg, sizeof(*vol));
	if (IS_ERR(vol))
		return PTR_ERR(vol);
	ret = btrfs_check_ioctl_vol_args_path(vol);
	if (ret < 0)
		goto out;

	switch (cmd) {
	case BTRFS_IOC_SCAN_DEV:
		mutex_lock(&uuid_mutex);
		/*
		 * Scanning outside of mount can return NULL which would turn
		 * into 0 error code.
		 */
		device = btrfs_scan_one_device(vol->name, BLK_OPEN_READ, false);
		ret = PTR_ERR_OR_ZERO(device);
		mutex_unlock(&uuid_mutex);
		break;
	case BTRFS_IOC_FORGET_DEV:
		if (vol->name[0] != 0) {
			ret = lookup_bdev(vol->name, &devt);
			if (ret)
				break;
		}
		ret = btrfs_forget_devices(devt);
		break;
	case BTRFS_IOC_DEVICES_READY:
		mutex_lock(&uuid_mutex);
		/*
		 * Scanning outside of mount can return NULL which would turn
		 * into 0 error code.
		 */
		device = btrfs_scan_one_device(vol->name, BLK_OPEN_READ, false);
		if (IS_ERR_OR_NULL(device)) {
			mutex_unlock(&uuid_mutex);
			if (IS_ERR(device))
				ret = PTR_ERR(device);
			else
				ret = 0;
			break;
		}
		ret = !(device->fs_devices->num_devices ==
			device->fs_devices->total_devices);
		mutex_unlock(&uuid_mutex);
		break;
	case BTRFS_IOC_GET_SUPPORTED_FEATURES:
		ret = btrfs_ioctl_get_supported_features((void __user*)arg);
		break;
	}

out:
	kfree(vol);
	return ret;
}

static int btrfs_freeze(struct super_block *sb)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(sb);

	set_bit(BTRFS_FS_FROZEN, &fs_info->flags);
	/*
	 * We don't need a barrier here, we'll wait for any transaction that
	 * could be in progress on other threads (and do delayed iputs that
	 * we want to avoid on a frozen filesystem), or do the commit
	 * ourselves.
	 */
	return btrfs_commit_current_transaction(fs_info->tree_root);
}

static int check_dev_super(struct btrfs_device *dev)
{
	struct btrfs_fs_info *fs_info = dev->fs_info;
	struct btrfs_super_block *sb;
	u64 last_trans;
	u16 csum_type;
	int ret = 0;

	/* This should be called with fs still frozen. */
	ASSERT(test_bit(BTRFS_FS_FROZEN, &fs_info->flags));

	/* Missing dev, no need to check. */
	if (!dev->bdev)
		return 0;

	/* Only need to check the primary super block. */
	sb = btrfs_read_dev_one_super(dev->bdev, 0, true);
	if (IS_ERR(sb))
		return PTR_ERR(sb);

	/* Verify the checksum. */
	csum_type = btrfs_super_csum_type(sb);
	if (csum_type != btrfs_super_csum_type(fs_info->super_copy)) {
		btrfs_err(fs_info, "csum type changed, has %u expect %u",
			  csum_type, btrfs_super_csum_type(fs_info->super_copy));
		ret = -EUCLEAN;
		goto out;
	}

	if (btrfs_check_super_csum(fs_info, sb)) {
		btrfs_err(fs_info, "csum for on-disk super block no longer matches");
		ret = -EUCLEAN;
		goto out;
	}

	/* Btrfs_validate_super() includes fsid check against super->fsid. */
	ret = btrfs_validate_super(fs_info, sb, 0);
	if (ret < 0)
		goto out;

	last_trans = btrfs_get_last_trans_committed(fs_info);
	if (btrfs_super_generation(sb) != last_trans) {
		btrfs_err(fs_info, "transid mismatch, has %llu expect %llu",
			  btrfs_super_generation(sb), last_trans);
		ret = -EUCLEAN;
		goto out;
	}
out:
	btrfs_release_disk_super(sb);
	return ret;
}

static int btrfs_unfreeze(struct super_block *sb)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(sb);
	struct btrfs_device *device;
	int ret = 0;

	/*
	 * Make sure the fs is not changed by accident (like hibernation then
	 * modified by other OS).
	 * If we found anything wrong, we mark the fs error immediately.
	 *
	 * And since the fs is frozen, no one can modify the fs yet, thus
	 * we don't need to hold device_list_mutex.
	 */
	list_for_each_entry(device, &fs_info->fs_devices->devices, dev_list) {
		ret = check_dev_super(device);
		if (ret < 0) {
			btrfs_handle_fs_error(fs_info, ret,
				"super block on devid %llu got modified unexpectedly",
				device->devid);
			break;
		}
	}
	clear_bit(BTRFS_FS_FROZEN, &fs_info->flags);

	/*
	 * We still return 0, to allow VFS layer to unfreeze the fs even the
	 * above checks failed. Since the fs is either fine or read-only, we're
	 * safe to continue, without causing further damage.
	 */
	return 0;
}

static int btrfs_show_devname(struct seq_file *m, struct dentry *root)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(root->d_sb);

	/*
	 * There should be always a valid pointer in latest_dev, it may be stale
	 * for a short moment in case it's being deleted but still valid until
	 * the end of RCU grace period.
	 */
	rcu_read_lock();
	seq_escape(m, btrfs_dev_name(fs_info->fs_devices->latest_dev), " \t\n\\");
	rcu_read_unlock();

	return 0;
}

static long btrfs_nr_cached_objects(struct super_block *sb, struct shrink_control *sc)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(sb);
	const s64 nr = percpu_counter_sum_positive(&fs_info->evictable_extent_maps);

	trace_btrfs_extent_map_shrinker_count(fs_info, nr);

	return nr;
}

static long btrfs_free_cached_objects(struct super_block *sb, struct shrink_control *sc)
{
	const long nr_to_scan = min_t(unsigned long, LONG_MAX, sc->nr_to_scan);
	struct btrfs_fs_info *fs_info = btrfs_sb(sb);

	btrfs_free_extent_maps(fs_info, nr_to_scan);

	/* The extent map shrinker runs asynchronously, so always return 0. */
	return 0;
}

static const struct super_operations btrfs_super_ops = {
	.drop_inode	= btrfs_drop_inode,
	.evict_inode	= btrfs_evict_inode,
	.put_super	= btrfs_put_super,
	.sync_fs	= btrfs_sync_fs,
	.show_options	= btrfs_show_options,
	.show_devname	= btrfs_show_devname,
	.alloc_inode	= btrfs_alloc_inode,
	.destroy_inode	= btrfs_destroy_inode,
	.free_inode	= btrfs_free_inode,
	.statfs		= btrfs_statfs,
	.freeze_fs	= btrfs_freeze,
	.unfreeze_fs	= btrfs_unfreeze,
	.nr_cached_objects = btrfs_nr_cached_objects,
	.free_cached_objects = btrfs_free_cached_objects,
};

static const struct file_operations btrfs_ctl_fops = {
	.open = btrfs_control_open,
	.unlocked_ioctl	 = btrfs_control_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
	.owner	 = THIS_MODULE,
	.llseek = noop_llseek,
};

static struct miscdevice btrfs_misc = {
	.minor		= BTRFS_MINOR,
	.name		= "btrfs-control",
	.fops		= &btrfs_ctl_fops
};

MODULE_ALIAS_MISCDEV(BTRFS_MINOR);
MODULE_ALIAS("devname:btrfs-control");

static int __init btrfs_interface_init(void)
{
	return misc_register(&btrfs_misc);
}

static __cold void btrfs_interface_exit(void)
{
	misc_deregister(&btrfs_misc);
}

static int __init btrfs_print_mod_info(void)
{
	static const char options[] = ""
#ifdef CONFIG_BTRFS_DEBUG
			", debug=on"
#endif
#ifdef CONFIG_BTRFS_ASSERT
			", assert=on"
#endif
#ifdef CONFIG_BTRFS_FS_REF_VERIFY
			", ref-verify=on"
#endif
#ifdef CONFIG_BLK_DEV_ZONED
			", zoned=yes"
#else
			", zoned=no"
#endif
#ifdef CONFIG_FS_VERITY
			", fsverity=yes"
#else
			", fsverity=no"
#endif
			;
	pr_info("Btrfs loaded%s\n", options);
	return 0;
}

static int register_btrfs(void)
{
	return register_filesystem(&btrfs_fs_type);
}

static void unregister_btrfs(void)
{
	unregister_filesystem(&btrfs_fs_type);
}

/* Helper structure for long init/exit functions. */
struct init_sequence {
	int (*init_func)(void);
	/* Can be NULL if the init_func doesn't need cleanup. */
	void (*exit_func)(void);
};

static const struct init_sequence mod_init_seq[] = {
	{
		.init_func = btrfs_props_init,
		.exit_func = NULL,
	}, {
		.init_func = btrfs_init_sysfs,
		.exit_func = btrfs_exit_sysfs,
	}, {
		.init_func = btrfs_init_compress,
		.exit_func = btrfs_exit_compress,
	}, {
		.init_func = btrfs_init_cachep,
		.exit_func = btrfs_destroy_cachep,
	}, {
		.init_func = btrfs_init_dio,
		.exit_func = btrfs_destroy_dio,
	}, {
		.init_func = btrfs_transaction_init,
		.exit_func = btrfs_transaction_exit,
	}, {
		.init_func = btrfs_ctree_init,
		.exit_func = btrfs_ctree_exit,
	}, {
		.init_func = btrfs_free_space_init,
		.exit_func = btrfs_free_space_exit,
	}, {
		.init_func = extent_state_init_cachep,
		.exit_func = extent_state_free_cachep,
	}, {
		.init_func = extent_buffer_init_cachep,
		.exit_func = extent_buffer_free_cachep,
	}, {
		.init_func = btrfs_bioset_init,
		.exit_func = btrfs_bioset_exit,
	}, {
		.init_func = extent_map_init,
		.exit_func = extent_map_exit,
	}, {
		.init_func = ordered_data_init,
		.exit_func = ordered_data_exit,
	}, {
		.init_func = btrfs_delayed_inode_init,
		.exit_func = btrfs_delayed_inode_exit,
	}, {
		.init_func = btrfs_auto_defrag_init,
		.exit_func = btrfs_auto_defrag_exit,
	}, {
		.init_func = btrfs_delayed_ref_init,
		.exit_func = btrfs_delayed_ref_exit,
	}, {
		.init_func = btrfs_prelim_ref_init,
		.exit_func = btrfs_prelim_ref_exit,
	}, {
		.init_func = btrfs_interface_init,
		.exit_func = btrfs_interface_exit,
	}, {
		.init_func = btrfs_print_mod_info,
		.exit_func = NULL,
	}, {
		.init_func = btrfs_run_sanity_tests,
		.exit_func = NULL,
	}, {
		.init_func = register_btrfs,
		.exit_func = unregister_btrfs,
	}
};

static bool mod_init_result[ARRAY_SIZE(mod_init_seq)];

static __always_inline void btrfs_exit_btrfs_fs(void)
{
	int i;

	for (i = ARRAY_SIZE(mod_init_seq) - 1; i >= 0; i--) {
		if (!mod_init_result[i])
			continue;
		if (mod_init_seq[i].exit_func)
			mod_init_seq[i].exit_func();
		mod_init_result[i] = false;
	}
}

static void __exit exit_btrfs_fs(void)
{
	btrfs_exit_btrfs_fs();
	btrfs_cleanup_fs_uuids();
}

static int __init init_btrfs_fs(void)
{
	int ret;
	int i;

	for (i = 0; i < ARRAY_SIZE(mod_init_seq); i++) {
		ASSERT(!mod_init_result[i]);
		ret = mod_init_seq[i].init_func();
		if (ret < 0) {
			btrfs_exit_btrfs_fs();
			return ret;
		}
		mod_init_result[i] = true;
	}
	return 0;
}

late_initcall(init_btrfs_fs);
module_exit(exit_btrfs_fs)

MODULE_DESCRIPTION("B-Tree File System (BTRFS)");
MODULE_LICENSE("GPL");
MODULE_SOFTDEP("pre: crc32c");
MODULE_SOFTDEP("pre: xxhash64");
MODULE_SOFTDEP("pre: sha256");
MODULE_SOFTDEP("pre: blake2b-256");
