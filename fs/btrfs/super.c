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
#include <linux/cleancache.h>
#include <linux/ratelimit.h>
#include <linux/crc32c.h>
#include <linux/btrfs.h>
#include "delayed-inode.h"
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "btrfs_inode.h"
#include "print-tree.h"
#include "props.h"
#include "xattr.h"
#include "volumes.h"
#include "export.h"
#include "compression.h"
#include "rcu-string.h"
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
#define CREATE_TRACE_POINTS
#include <trace/events/btrfs.h>

static const struct super_operations btrfs_super_ops;

/*
 * Types for mounting the default subvolume and a subvolume explicitly
 * requested by subvol=/path. That way the callchain is straightforward and we
 * don't have to play tricks with the mount options and recursive calls to
 * btrfs_mount.
 *
 * The new btrfs_root_fs_type also servers as a tag for the bdev_holder.
 */
static struct file_system_type btrfs_fs_type;
static struct file_system_type btrfs_root_fs_type;

static int btrfs_remount(struct super_block *sb, int *flags, char *data);

/*
 * Generally the error codes correspond to their respective errors, but there
 * are a few special cases.
 *
 * EUCLEAN: Any sort of corruption that we encounter.  The tree-checker for
 *          instance will return EUCLEAN if any of the blocks are corrupted in
 *          a way that is problematic.  We want to reserve EUCLEAN for these
 *          sort of corruptions.
 *
 * EROFS: If we check BTRFS_FS_STATE_ERROR and fail out with a return error, we
 *        need to use EROFS for this case.  We will have no idea of the
 *        original failure, that will have been reported at the time we tripped
 *        over the error.  Each subsequent error that doesn't have any context
 *        of the original error should use EROFS when handling BTRFS_FS_STATE_ERROR.
 */
const char * __attribute_const__ btrfs_decode_error(int errno)
{
	char *errstr = "unknown";

	switch (errno) {
	case -ENOENT:		/* -2 */
		errstr = "No such entry";
		break;
	case -EIO:		/* -5 */
		errstr = "IO failure";
		break;
	case -ENOMEM:		/* -12*/
		errstr = "Out of memory";
		break;
	case -EEXIST:		/* -17 */
		errstr = "Object already exists";
		break;
	case -ENOSPC:		/* -28 */
		errstr = "No space left";
		break;
	case -EROFS:		/* -30 */
		errstr = "Readonly filesystem";
		break;
	case -EOPNOTSUPP:	/* -95 */
		errstr = "Operation not supported";
		break;
	case -EUCLEAN:		/* -117 */
		errstr = "Filesystem corrupted";
		break;
	case -EDQUOT:		/* -122 */
		errstr = "Quota exceeded";
		break;
	}

	return errstr;
}

/*
 * __btrfs_handle_fs_error decodes expected errors from the caller and
 * invokes the appropriate error response.
 */
__cold
void __btrfs_handle_fs_error(struct btrfs_fs_info *fs_info, const char *function,
		       unsigned int line, int errno, const char *fmt, ...)
{
	struct super_block *sb = fs_info->sb;
#ifdef CONFIG_PRINTK
	const char *errstr;
#endif

	/*
	 * Special case: if the error is EROFS, and we're already
	 * under SB_RDONLY, then it is safe here.
	 */
	if (errno == -EROFS && sb_rdonly(sb))
  		return;

#ifdef CONFIG_PRINTK
	errstr = btrfs_decode_error(errno);
	if (fmt) {
		struct va_format vaf;
		va_list args;

		va_start(args, fmt);
		vaf.fmt = fmt;
		vaf.va = &args;

		pr_crit("BTRFS: error (device %s) in %s:%d: errno=%d %s (%pV)\n",
			sb->s_id, function, line, errno, errstr, &vaf);
		va_end(args);
	} else {
		pr_crit("BTRFS: error (device %s) in %s:%d: errno=%d %s\n",
			sb->s_id, function, line, errno, errstr);
	}
#endif

	/*
	 * Today we only save the error info to memory.  Long term we'll
	 * also send it down to the disk
	 */
	set_bit(BTRFS_FS_STATE_ERROR, &fs_info->fs_state);

	/* Don't go through full error handling during mount */
	if (!(sb->s_flags & SB_BORN))
		return;

	if (sb_rdonly(sb))
		return;

	btrfs_discard_stop(fs_info);

	/* btrfs handle error by forcing the filesystem readonly */
	btrfs_set_sb_rdonly(sb);
	btrfs_info(fs_info, "forced readonly");
	/*
	 * Note that a running device replace operation is not canceled here
	 * although there is no way to update the progress. It would add the
	 * risk of a deadlock, therefore the canceling is omitted. The only
	 * penalty is that some I/O remains active until the procedure
	 * completes. The next time when the filesystem is mounted writable
	 * again, the device replace operation continues.
	 */
}

#ifdef CONFIG_PRINTK
static const char * const logtypes[] = {
	"emergency",
	"alert",
	"critical",
	"error",
	"warning",
	"notice",
	"info",
	"debug",
};


/*
 * Use one ratelimit state per log level so that a flood of less important
 * messages doesn't cause more important ones to be dropped.
 */
static struct ratelimit_state printk_limits[] = {
	RATELIMIT_STATE_INIT(printk_limits[0], DEFAULT_RATELIMIT_INTERVAL, 100),
	RATELIMIT_STATE_INIT(printk_limits[1], DEFAULT_RATELIMIT_INTERVAL, 100),
	RATELIMIT_STATE_INIT(printk_limits[2], DEFAULT_RATELIMIT_INTERVAL, 100),
	RATELIMIT_STATE_INIT(printk_limits[3], DEFAULT_RATELIMIT_INTERVAL, 100),
	RATELIMIT_STATE_INIT(printk_limits[4], DEFAULT_RATELIMIT_INTERVAL, 100),
	RATELIMIT_STATE_INIT(printk_limits[5], DEFAULT_RATELIMIT_INTERVAL, 100),
	RATELIMIT_STATE_INIT(printk_limits[6], DEFAULT_RATELIMIT_INTERVAL, 100),
	RATELIMIT_STATE_INIT(printk_limits[7], DEFAULT_RATELIMIT_INTERVAL, 100),
};

void __cold btrfs_printk(const struct btrfs_fs_info *fs_info, const char *fmt, ...)
{
	char lvl[PRINTK_MAX_SINGLE_HEADER_LEN + 1] = "\0";
	struct va_format vaf;
	va_list args;
	int kern_level;
	const char *type = logtypes[4];
	struct ratelimit_state *ratelimit = &printk_limits[4];

	va_start(args, fmt);

	while ((kern_level = printk_get_level(fmt)) != 0) {
		size_t size = printk_skip_level(fmt) - fmt;

		if (kern_level >= '0' && kern_level <= '7') {
			memcpy(lvl, fmt,  size);
			lvl[size] = '\0';
			type = logtypes[kern_level - '0'];
			ratelimit = &printk_limits[kern_level - '0'];
		}
		fmt += size;
	}

	vaf.fmt = fmt;
	vaf.va = &args;

	if (__ratelimit(ratelimit)) {
		if (fs_info)
			printk("%sBTRFS %s (device %s): %pV\n", lvl, type,
				fs_info->sb->s_id, &vaf);
		else
			printk("%sBTRFS %s: %pV\n", lvl, type, &vaf);
	}

	va_end(args);
}
#endif

#if BITS_PER_LONG == 32
void __cold btrfs_warn_32bit_limit(struct btrfs_fs_info *fs_info)
{
	if (!test_and_set_bit(BTRFS_FS_32BIT_WARN, &fs_info->flags)) {
		btrfs_warn(fs_info, "reaching 32bit limit for logical addresses");
		btrfs_warn(fs_info,
"due to page cache limit on 32bit systems, btrfs can't access metadata at or beyond %lluT",
			   BTRFS_32BIT_MAX_FILE_SIZE >> 40);
		btrfs_warn(fs_info,
			   "please consider upgrading to 64bit kernel/hardware");
	}
}

void __cold btrfs_err_32bit_limit(struct btrfs_fs_info *fs_info)
{
	if (!test_and_set_bit(BTRFS_FS_32BIT_ERROR, &fs_info->flags)) {
		btrfs_err(fs_info, "reached 32bit limit for logical addresses");
		btrfs_err(fs_info,
"due to page cache limit on 32bit systems, metadata beyond %lluT can't be accessed",
			  BTRFS_32BIT_MAX_FILE_SIZE >> 40);
		btrfs_err(fs_info,
			   "please consider upgrading to 64bit kernel/hardware");
	}
}
#endif

/*
 * We only mark the transaction aborted and then set the file system read-only.
 * This will prevent new transactions from starting or trying to join this
 * one.
 *
 * This means that error recovery at the call site is limited to freeing
 * any local memory allocations and passing the error code up without
 * further cleanup. The transaction should complete as it normally would
 * in the call path but will return -EIO.
 *
 * We'll complete the cleanup in btrfs_end_transaction and
 * btrfs_commit_transaction.
 */
__cold
void __btrfs_abort_transaction(struct btrfs_trans_handle *trans,
			       const char *function,
			       unsigned int line, int errno)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;

	WRITE_ONCE(trans->aborted, errno);
	WRITE_ONCE(trans->transaction->aborted, errno);
	/* Wake up anybody who may be waiting on this transaction */
	wake_up(&fs_info->transaction_wait);
	wake_up(&fs_info->transaction_blocked_wait);
	__btrfs_handle_fs_error(fs_info, function, line, errno, NULL);
}
/*
 * __btrfs_panic decodes unexpected, fatal errors from the caller,
 * issues an alert, and either panics or BUGs, depending on mount options.
 */
__cold
void __btrfs_panic(struct btrfs_fs_info *fs_info, const char *function,
		   unsigned int line, int errno, const char *fmt, ...)
{
	char *s_id = "<unknown>";
	const char *errstr;
	struct va_format vaf = { .fmt = fmt };
	va_list args;

	if (fs_info)
		s_id = fs_info->sb->s_id;

	va_start(args, fmt);
	vaf.va = &args;

	errstr = btrfs_decode_error(errno);
	if (fs_info && (btrfs_test_opt(fs_info, PANIC_ON_FATAL_ERROR)))
		panic(KERN_CRIT "BTRFS panic (device %s) in %s:%d: %pV (errno=%d %s)\n",
			s_id, function, line, &vaf, errno, errstr);

	btrfs_crit(fs_info, "panic in %s:%d: %pV (errno=%d %s)",
		   function, line, &vaf, errno, errstr);
	va_end(args);
	/* Caller calls BUG() */
}

static void btrfs_put_super(struct super_block *sb)
{
	close_ctree(btrfs_sb(sb));
}

enum {
	Opt_acl, Opt_noacl,
	Opt_clear_cache,
	Opt_commit_interval,
	Opt_compress,
	Opt_compress_force,
	Opt_compress_force_type,
	Opt_compress_type,
	Opt_degraded,
	Opt_device,
	Opt_fatal_errors,
	Opt_flushoncommit, Opt_noflushoncommit,
	Opt_max_inline,
	Opt_barrier, Opt_nobarrier,
	Opt_datacow, Opt_nodatacow,
	Opt_datasum, Opt_nodatasum,
	Opt_defrag, Opt_nodefrag,
	Opt_discard, Opt_nodiscard,
	Opt_discard_mode,
	Opt_norecovery,
	Opt_ratio,
	Opt_rescan_uuid_tree,
	Opt_skip_balance,
	Opt_space_cache, Opt_no_space_cache,
	Opt_space_cache_version,
	Opt_ssd, Opt_nossd,
	Opt_ssd_spread, Opt_nossd_spread,
	Opt_subvol,
	Opt_subvol_empty,
	Opt_subvolid,
	Opt_thread_pool,
	Opt_treelog, Opt_notreelog,
	Opt_user_subvol_rm_allowed,

	/* Rescue options */
	Opt_rescue,
	Opt_usebackuproot,
	Opt_nologreplay,
	Opt_ignorebadroots,
	Opt_ignoredatacsums,
	Opt_rescue_all,

	/* Deprecated options */
	Opt_recovery,
	Opt_inode_cache, Opt_noinode_cache,

	/* Debugging options */
	Opt_check_integrity,
	Opt_check_integrity_including_extent_data,
	Opt_check_integrity_print_mask,
	Opt_enospc_debug, Opt_noenospc_debug,
#ifdef CONFIG_BTRFS_DEBUG
	Opt_fragment_data, Opt_fragment_metadata, Opt_fragment_all,
#endif
#ifdef CONFIG_BTRFS_FS_REF_VERIFY
	Opt_ref_verify,
#endif
	Opt_err,
};

static const match_table_t tokens = {
	{Opt_acl, "acl"},
	{Opt_noacl, "noacl"},
	{Opt_clear_cache, "clear_cache"},
	{Opt_commit_interval, "commit=%u"},
	{Opt_compress, "compress"},
	{Opt_compress_type, "compress=%s"},
	{Opt_compress_force, "compress-force"},
	{Opt_compress_force_type, "compress-force=%s"},
	{Opt_degraded, "degraded"},
	{Opt_device, "device=%s"},
	{Opt_fatal_errors, "fatal_errors=%s"},
	{Opt_flushoncommit, "flushoncommit"},
	{Opt_noflushoncommit, "noflushoncommit"},
	{Opt_inode_cache, "inode_cache"},
	{Opt_noinode_cache, "noinode_cache"},
	{Opt_max_inline, "max_inline=%s"},
	{Opt_barrier, "barrier"},
	{Opt_nobarrier, "nobarrier"},
	{Opt_datacow, "datacow"},
	{Opt_nodatacow, "nodatacow"},
	{Opt_datasum, "datasum"},
	{Opt_nodatasum, "nodatasum"},
	{Opt_defrag, "autodefrag"},
	{Opt_nodefrag, "noautodefrag"},
	{Opt_discard, "discard"},
	{Opt_discard_mode, "discard=%s"},
	{Opt_nodiscard, "nodiscard"},
	{Opt_norecovery, "norecovery"},
	{Opt_ratio, "metadata_ratio=%u"},
	{Opt_rescan_uuid_tree, "rescan_uuid_tree"},
	{Opt_skip_balance, "skip_balance"},
	{Opt_space_cache, "space_cache"},
	{Opt_no_space_cache, "nospace_cache"},
	{Opt_space_cache_version, "space_cache=%s"},
	{Opt_ssd, "ssd"},
	{Opt_nossd, "nossd"},
	{Opt_ssd_spread, "ssd_spread"},
	{Opt_nossd_spread, "nossd_spread"},
	{Opt_subvol, "subvol=%s"},
	{Opt_subvol_empty, "subvol="},
	{Opt_subvolid, "subvolid=%s"},
	{Opt_thread_pool, "thread_pool=%u"},
	{Opt_treelog, "treelog"},
	{Opt_notreelog, "notreelog"},
	{Opt_user_subvol_rm_allowed, "user_subvol_rm_allowed"},

	/* Rescue options */
	{Opt_rescue, "rescue=%s"},
	/* Deprecated, with alias rescue=nologreplay */
	{Opt_nologreplay, "nologreplay"},
	/* Deprecated, with alias rescue=usebackuproot */
	{Opt_usebackuproot, "usebackuproot"},

	/* Deprecated options */
	{Opt_recovery, "recovery"},

	/* Debugging options */
	{Opt_check_integrity, "check_int"},
	{Opt_check_integrity_including_extent_data, "check_int_data"},
	{Opt_check_integrity_print_mask, "check_int_print_mask=%u"},
	{Opt_enospc_debug, "enospc_debug"},
	{Opt_noenospc_debug, "noenospc_debug"},
#ifdef CONFIG_BTRFS_DEBUG
	{Opt_fragment_data, "fragment=data"},
	{Opt_fragment_metadata, "fragment=metadata"},
	{Opt_fragment_all, "fragment=all"},
#endif
#ifdef CONFIG_BTRFS_FS_REF_VERIFY
	{Opt_ref_verify, "ref_verify"},
#endif
	{Opt_err, NULL},
};

static const match_table_t rescue_tokens = {
	{Opt_usebackuproot, "usebackuproot"},
	{Opt_nologreplay, "nologreplay"},
	{Opt_ignorebadroots, "ignorebadroots"},
	{Opt_ignorebadroots, "ibadroots"},
	{Opt_ignoredatacsums, "ignoredatacsums"},
	{Opt_ignoredatacsums, "idatacsums"},
	{Opt_rescue_all, "all"},
	{Opt_err, NULL},
};

static bool check_ro_option(struct btrfs_fs_info *fs_info, unsigned long opt,
			    const char *opt_name)
{
	if (fs_info->mount_opt & opt) {
		btrfs_err(fs_info, "%s must be used with ro mount option",
			  opt_name);
		return true;
	}
	return false;
}

static int parse_rescue_options(struct btrfs_fs_info *info, const char *options)
{
	char *opts;
	char *orig;
	char *p;
	substring_t args[MAX_OPT_ARGS];
	int ret = 0;

	opts = kstrdup(options, GFP_KERNEL);
	if (!opts)
		return -ENOMEM;
	orig = opts;

	while ((p = strsep(&opts, ":")) != NULL) {
		int token;

		if (!*p)
			continue;
		token = match_token(p, rescue_tokens, args);
		switch (token){
		case Opt_usebackuproot:
			btrfs_info(info,
				   "trying to use backup root at mount time");
			btrfs_set_opt(info->mount_opt, USEBACKUPROOT);
			break;
		case Opt_nologreplay:
			btrfs_set_and_info(info, NOLOGREPLAY,
					   "disabling log replay at mount time");
			break;
		case Opt_ignorebadroots:
			btrfs_set_and_info(info, IGNOREBADROOTS,
					   "ignoring bad roots");
			break;
		case Opt_ignoredatacsums:
			btrfs_set_and_info(info, IGNOREDATACSUMS,
					   "ignoring data csums");
			break;
		case Opt_rescue_all:
			btrfs_info(info, "enabling all of the rescue options");
			btrfs_set_and_info(info, IGNOREDATACSUMS,
					   "ignoring data csums");
			btrfs_set_and_info(info, IGNOREBADROOTS,
					   "ignoring bad roots");
			btrfs_set_and_info(info, NOLOGREPLAY,
					   "disabling log replay at mount time");
			break;
		case Opt_err:
			btrfs_info(info, "unrecognized rescue option '%s'", p);
			ret = -EINVAL;
			goto out;
		default:
			break;
		}

	}
out:
	kfree(orig);
	return ret;
}

/*
 * Regular mount options parser.  Everything that is needed only when
 * reading in a new superblock is parsed here.
 * XXX JDM: This needs to be cleaned up for remount.
 */
int btrfs_parse_options(struct btrfs_fs_info *info, char *options,
			unsigned long new_flags)
{
	substring_t args[MAX_OPT_ARGS];
	char *p, *num;
	int intarg;
	int ret = 0;
	char *compress_type;
	bool compress_force = false;
	enum btrfs_compression_type saved_compress_type;
	int saved_compress_level;
	bool saved_compress_force;
	int no_compress = 0;

	if (btrfs_fs_compat_ro(info, FREE_SPACE_TREE))
		btrfs_set_opt(info->mount_opt, FREE_SPACE_TREE);
	else if (btrfs_free_space_cache_v1_active(info)) {
		if (btrfs_is_zoned(info)) {
			btrfs_info(info,
			"zoned: clearing existing space cache");
			btrfs_set_super_cache_generation(info->super_copy, 0);
		} else {
			btrfs_set_opt(info->mount_opt, SPACE_CACHE);
		}
	}

	/*
	 * Even the options are empty, we still need to do extra check
	 * against new flags
	 */
	if (!options)
		goto check;

	while ((p = strsep(&options, ",")) != NULL) {
		int token;
		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_degraded:
			btrfs_info(info, "allowing degraded mounts");
			btrfs_set_opt(info->mount_opt, DEGRADED);
			break;
		case Opt_subvol:
		case Opt_subvol_empty:
		case Opt_subvolid:
		case Opt_device:
			/*
			 * These are parsed by btrfs_parse_subvol_options or
			 * btrfs_parse_device_options and can be ignored here.
			 */
			break;
		case Opt_nodatasum:
			btrfs_set_and_info(info, NODATASUM,
					   "setting nodatasum");
			break;
		case Opt_datasum:
			if (btrfs_test_opt(info, NODATASUM)) {
				if (btrfs_test_opt(info, NODATACOW))
					btrfs_info(info,
						   "setting datasum, datacow enabled");
				else
					btrfs_info(info, "setting datasum");
			}
			btrfs_clear_opt(info->mount_opt, NODATACOW);
			btrfs_clear_opt(info->mount_opt, NODATASUM);
			break;
		case Opt_nodatacow:
			if (!btrfs_test_opt(info, NODATACOW)) {
				if (!btrfs_test_opt(info, COMPRESS) ||
				    !btrfs_test_opt(info, FORCE_COMPRESS)) {
					btrfs_info(info,
						   "setting nodatacow, compression disabled");
				} else {
					btrfs_info(info, "setting nodatacow");
				}
			}
			btrfs_clear_opt(info->mount_opt, COMPRESS);
			btrfs_clear_opt(info->mount_opt, FORCE_COMPRESS);
			btrfs_set_opt(info->mount_opt, NODATACOW);
			btrfs_set_opt(info->mount_opt, NODATASUM);
			break;
		case Opt_datacow:
			btrfs_clear_and_info(info, NODATACOW,
					     "setting datacow");
			break;
		case Opt_compress_force:
		case Opt_compress_force_type:
			compress_force = true;
			fallthrough;
		case Opt_compress:
		case Opt_compress_type:
			saved_compress_type = btrfs_test_opt(info,
							     COMPRESS) ?
				info->compress_type : BTRFS_COMPRESS_NONE;
			saved_compress_force =
				btrfs_test_opt(info, FORCE_COMPRESS);
			saved_compress_level = info->compress_level;
			if (token == Opt_compress ||
			    token == Opt_compress_force ||
			    strncmp(args[0].from, "zlib", 4) == 0) {
				compress_type = "zlib";

				info->compress_type = BTRFS_COMPRESS_ZLIB;
				info->compress_level = BTRFS_ZLIB_DEFAULT_LEVEL;
				/*
				 * args[0] contains uninitialized data since
				 * for these tokens we don't expect any
				 * parameter.
				 */
				if (token != Opt_compress &&
				    token != Opt_compress_force)
					info->compress_level =
					  btrfs_compress_str2level(
							BTRFS_COMPRESS_ZLIB,
							args[0].from + 4);
				btrfs_set_opt(info->mount_opt, COMPRESS);
				btrfs_clear_opt(info->mount_opt, NODATACOW);
				btrfs_clear_opt(info->mount_opt, NODATASUM);
				no_compress = 0;
			} else if (strncmp(args[0].from, "lzo", 3) == 0) {
				compress_type = "lzo";
				info->compress_type = BTRFS_COMPRESS_LZO;
				info->compress_level = 0;
				btrfs_set_opt(info->mount_opt, COMPRESS);
				btrfs_clear_opt(info->mount_opt, NODATACOW);
				btrfs_clear_opt(info->mount_opt, NODATASUM);
				btrfs_set_fs_incompat(info, COMPRESS_LZO);
				no_compress = 0;
			} else if (strncmp(args[0].from, "zstd", 4) == 0) {
				compress_type = "zstd";
				info->compress_type = BTRFS_COMPRESS_ZSTD;
				info->compress_level =
					btrfs_compress_str2level(
							 BTRFS_COMPRESS_ZSTD,
							 args[0].from + 4);
				btrfs_set_opt(info->mount_opt, COMPRESS);
				btrfs_clear_opt(info->mount_opt, NODATACOW);
				btrfs_clear_opt(info->mount_opt, NODATASUM);
				btrfs_set_fs_incompat(info, COMPRESS_ZSTD);
				no_compress = 0;
			} else if (strncmp(args[0].from, "no", 2) == 0) {
				compress_type = "no";
				info->compress_level = 0;
				info->compress_type = 0;
				btrfs_clear_opt(info->mount_opt, COMPRESS);
				btrfs_clear_opt(info->mount_opt, FORCE_COMPRESS);
				compress_force = false;
				no_compress++;
			} else {
				btrfs_err(info, "unrecognized compression value %s",
					  args[0].from);
				ret = -EINVAL;
				goto out;
			}

			if (compress_force) {
				btrfs_set_opt(info->mount_opt, FORCE_COMPRESS);
			} else {
				/*
				 * If we remount from compress-force=xxx to
				 * compress=xxx, we need clear FORCE_COMPRESS
				 * flag, otherwise, there is no way for users
				 * to disable forcible compression separately.
				 */
				btrfs_clear_opt(info->mount_opt, FORCE_COMPRESS);
			}
			if (no_compress == 1) {
				btrfs_info(info, "use no compression");
			} else if ((info->compress_type != saved_compress_type) ||
				   (compress_force != saved_compress_force) ||
				   (info->compress_level != saved_compress_level)) {
				btrfs_info(info, "%s %s compression, level %d",
					   (compress_force) ? "force" : "use",
					   compress_type, info->compress_level);
			}
			compress_force = false;
			break;
		case Opt_ssd:
			btrfs_set_and_info(info, SSD,
					   "enabling ssd optimizations");
			btrfs_clear_opt(info->mount_opt, NOSSD);
			break;
		case Opt_ssd_spread:
			btrfs_set_and_info(info, SSD,
					   "enabling ssd optimizations");
			btrfs_set_and_info(info, SSD_SPREAD,
					   "using spread ssd allocation scheme");
			btrfs_clear_opt(info->mount_opt, NOSSD);
			break;
		case Opt_nossd:
			btrfs_set_opt(info->mount_opt, NOSSD);
			btrfs_clear_and_info(info, SSD,
					     "not using ssd optimizations");
			fallthrough;
		case Opt_nossd_spread:
			btrfs_clear_and_info(info, SSD_SPREAD,
					     "not using spread ssd allocation scheme");
			break;
		case Opt_barrier:
			btrfs_clear_and_info(info, NOBARRIER,
					     "turning on barriers");
			break;
		case Opt_nobarrier:
			btrfs_set_and_info(info, NOBARRIER,
					   "turning off barriers");
			break;
		case Opt_thread_pool:
			ret = match_int(&args[0], &intarg);
			if (ret) {
				btrfs_err(info, "unrecognized thread_pool value %s",
					  args[0].from);
				goto out;
			} else if (intarg == 0) {
				btrfs_err(info, "invalid value 0 for thread_pool");
				ret = -EINVAL;
				goto out;
			}
			info->thread_pool_size = intarg;
			break;
		case Opt_max_inline:
			num = match_strdup(&args[0]);
			if (num) {
				info->max_inline = memparse(num, NULL);
				kfree(num);

				if (info->max_inline) {
					info->max_inline = min_t(u64,
						info->max_inline,
						info->sectorsize);
				}
				btrfs_info(info, "max_inline at %llu",
					   info->max_inline);
			} else {
				ret = -ENOMEM;
				goto out;
			}
			break;
		case Opt_acl:
#ifdef CONFIG_BTRFS_FS_POSIX_ACL
			info->sb->s_flags |= SB_POSIXACL;
			break;
#else
			btrfs_err(info, "support for ACL not compiled in!");
			ret = -EINVAL;
			goto out;
#endif
		case Opt_noacl:
			info->sb->s_flags &= ~SB_POSIXACL;
			break;
		case Opt_notreelog:
			btrfs_set_and_info(info, NOTREELOG,
					   "disabling tree log");
			break;
		case Opt_treelog:
			btrfs_clear_and_info(info, NOTREELOG,
					     "enabling tree log");
			break;
		case Opt_norecovery:
		case Opt_nologreplay:
			btrfs_warn(info,
		"'nologreplay' is deprecated, use 'rescue=nologreplay' instead");
			btrfs_set_and_info(info, NOLOGREPLAY,
					   "disabling log replay at mount time");
			break;
		case Opt_flushoncommit:
			btrfs_set_and_info(info, FLUSHONCOMMIT,
					   "turning on flush-on-commit");
			break;
		case Opt_noflushoncommit:
			btrfs_clear_and_info(info, FLUSHONCOMMIT,
					     "turning off flush-on-commit");
			break;
		case Opt_ratio:
			ret = match_int(&args[0], &intarg);
			if (ret) {
				btrfs_err(info, "unrecognized metadata_ratio value %s",
					  args[0].from);
				goto out;
			}
			info->metadata_ratio = intarg;
			btrfs_info(info, "metadata ratio %u",
				   info->metadata_ratio);
			break;
		case Opt_discard:
		case Opt_discard_mode:
			if (token == Opt_discard ||
			    strcmp(args[0].from, "sync") == 0) {
				btrfs_clear_opt(info->mount_opt, DISCARD_ASYNC);
				btrfs_set_and_info(info, DISCARD_SYNC,
						   "turning on sync discard");
			} else if (strcmp(args[0].from, "async") == 0) {
				btrfs_clear_opt(info->mount_opt, DISCARD_SYNC);
				btrfs_set_and_info(info, DISCARD_ASYNC,
						   "turning on async discard");
			} else {
				btrfs_err(info, "unrecognized discard mode value %s",
					  args[0].from);
				ret = -EINVAL;
				goto out;
			}
			break;
		case Opt_nodiscard:
			btrfs_clear_and_info(info, DISCARD_SYNC,
					     "turning off discard");
			btrfs_clear_and_info(info, DISCARD_ASYNC,
					     "turning off async discard");
			break;
		case Opt_space_cache:
		case Opt_space_cache_version:
			if (token == Opt_space_cache ||
			    strcmp(args[0].from, "v1") == 0) {
				btrfs_clear_opt(info->mount_opt,
						FREE_SPACE_TREE);
				btrfs_set_and_info(info, SPACE_CACHE,
					   "enabling disk space caching");
			} else if (strcmp(args[0].from, "v2") == 0) {
				btrfs_clear_opt(info->mount_opt,
						SPACE_CACHE);
				btrfs_set_and_info(info, FREE_SPACE_TREE,
						   "enabling free space tree");
			} else {
				btrfs_err(info, "unrecognized space_cache value %s",
					  args[0].from);
				ret = -EINVAL;
				goto out;
			}
			break;
		case Opt_rescan_uuid_tree:
			btrfs_set_opt(info->mount_opt, RESCAN_UUID_TREE);
			break;
		case Opt_no_space_cache:
			if (btrfs_test_opt(info, SPACE_CACHE)) {
				btrfs_clear_and_info(info, SPACE_CACHE,
					     "disabling disk space caching");
			}
			if (btrfs_test_opt(info, FREE_SPACE_TREE)) {
				btrfs_clear_and_info(info, FREE_SPACE_TREE,
					     "disabling free space tree");
			}
			break;
		case Opt_inode_cache:
		case Opt_noinode_cache:
			btrfs_warn(info,
	"the 'inode_cache' option is deprecated and has no effect since 5.11");
			break;
		case Opt_clear_cache:
			btrfs_set_and_info(info, CLEAR_CACHE,
					   "force clearing of disk cache");
			break;
		case Opt_user_subvol_rm_allowed:
			btrfs_set_opt(info->mount_opt, USER_SUBVOL_RM_ALLOWED);
			break;
		case Opt_enospc_debug:
			btrfs_set_opt(info->mount_opt, ENOSPC_DEBUG);
			break;
		case Opt_noenospc_debug:
			btrfs_clear_opt(info->mount_opt, ENOSPC_DEBUG);
			break;
		case Opt_defrag:
			btrfs_set_and_info(info, AUTO_DEFRAG,
					   "enabling auto defrag");
			break;
		case Opt_nodefrag:
			btrfs_clear_and_info(info, AUTO_DEFRAG,
					     "disabling auto defrag");
			break;
		case Opt_recovery:
		case Opt_usebackuproot:
			btrfs_warn(info,
			"'%s' is deprecated, use 'rescue=usebackuproot' instead",
				   token == Opt_recovery ? "recovery" :
				   "usebackuproot");
			btrfs_info(info,
				   "trying to use backup root at mount time");
			btrfs_set_opt(info->mount_opt, USEBACKUPROOT);
			break;
		case Opt_skip_balance:
			btrfs_set_opt(info->mount_opt, SKIP_BALANCE);
			break;
#ifdef CONFIG_BTRFS_FS_CHECK_INTEGRITY
		case Opt_check_integrity_including_extent_data:
			btrfs_info(info,
				   "enabling check integrity including extent data");
			btrfs_set_opt(info->mount_opt, CHECK_INTEGRITY_DATA);
			btrfs_set_opt(info->mount_opt, CHECK_INTEGRITY);
			break;
		case Opt_check_integrity:
			btrfs_info(info, "enabling check integrity");
			btrfs_set_opt(info->mount_opt, CHECK_INTEGRITY);
			break;
		case Opt_check_integrity_print_mask:
			ret = match_int(&args[0], &intarg);
			if (ret) {
				btrfs_err(info,
				"unrecognized check_integrity_print_mask value %s",
					args[0].from);
				goto out;
			}
			info->check_integrity_print_mask = intarg;
			btrfs_info(info, "check_integrity_print_mask 0x%x",
				   info->check_integrity_print_mask);
			break;
#else
		case Opt_check_integrity_including_extent_data:
		case Opt_check_integrity:
		case Opt_check_integrity_print_mask:
			btrfs_err(info,
				  "support for check_integrity* not compiled in!");
			ret = -EINVAL;
			goto out;
#endif
		case Opt_fatal_errors:
			if (strcmp(args[0].from, "panic") == 0) {
				btrfs_set_opt(info->mount_opt,
					      PANIC_ON_FATAL_ERROR);
			} else if (strcmp(args[0].from, "bug") == 0) {
				btrfs_clear_opt(info->mount_opt,
					      PANIC_ON_FATAL_ERROR);
			} else {
				btrfs_err(info, "unrecognized fatal_errors value %s",
					  args[0].from);
				ret = -EINVAL;
				goto out;
			}
			break;
		case Opt_commit_interval:
			intarg = 0;
			ret = match_int(&args[0], &intarg);
			if (ret) {
				btrfs_err(info, "unrecognized commit_interval value %s",
					  args[0].from);
				ret = -EINVAL;
				goto out;
			}
			if (intarg == 0) {
				btrfs_info(info,
					   "using default commit interval %us",
					   BTRFS_DEFAULT_COMMIT_INTERVAL);
				intarg = BTRFS_DEFAULT_COMMIT_INTERVAL;
			} else if (intarg > 300) {
				btrfs_warn(info, "excessive commit interval %d",
					   intarg);
			}
			info->commit_interval = intarg;
			break;
		case Opt_rescue:
			ret = parse_rescue_options(info, args[0].from);
			if (ret < 0) {
				btrfs_err(info, "unrecognized rescue value %s",
					  args[0].from);
				goto out;
			}
			break;
#ifdef CONFIG_BTRFS_DEBUG
		case Opt_fragment_all:
			btrfs_info(info, "fragmenting all space");
			btrfs_set_opt(info->mount_opt, FRAGMENT_DATA);
			btrfs_set_opt(info->mount_opt, FRAGMENT_METADATA);
			break;
		case Opt_fragment_metadata:
			btrfs_info(info, "fragmenting metadata");
			btrfs_set_opt(info->mount_opt,
				      FRAGMENT_METADATA);
			break;
		case Opt_fragment_data:
			btrfs_info(info, "fragmenting data");
			btrfs_set_opt(info->mount_opt, FRAGMENT_DATA);
			break;
#endif
#ifdef CONFIG_BTRFS_FS_REF_VERIFY
		case Opt_ref_verify:
			btrfs_info(info, "doing ref verification");
			btrfs_set_opt(info->mount_opt, REF_VERIFY);
			break;
#endif
		case Opt_err:
			btrfs_err(info, "unrecognized mount option '%s'", p);
			ret = -EINVAL;
			goto out;
		default:
			break;
		}
	}
check:
	/* We're read-only, don't have to check. */
	if (new_flags & SB_RDONLY)
		goto out;

	if (check_ro_option(info, BTRFS_MOUNT_NOLOGREPLAY, "nologreplay") ||
	    check_ro_option(info, BTRFS_MOUNT_IGNOREBADROOTS, "ignorebadroots") ||
	    check_ro_option(info, BTRFS_MOUNT_IGNOREDATACSUMS, "ignoredatacsums"))
		ret = -EINVAL;
out:
	if (btrfs_fs_compat_ro(info, FREE_SPACE_TREE) &&
	    !btrfs_test_opt(info, FREE_SPACE_TREE) &&
	    !btrfs_test_opt(info, CLEAR_CACHE)) {
		btrfs_err(info, "cannot disable free space tree");
		ret = -EINVAL;

	}
	if (!ret)
		ret = btrfs_check_mountopts_zoned(info);
	if (!ret && btrfs_test_opt(info, SPACE_CACHE))
		btrfs_info(info, "disk space caching is enabled");
	if (!ret && btrfs_test_opt(info, FREE_SPACE_TREE))
		btrfs_info(info, "using free space tree");
	return ret;
}

/*
 * Parse mount options that are required early in the mount process.
 *
 * All other options will be parsed on much later in the mount process and
 * only when we need to allocate a new super block.
 */
static int btrfs_parse_device_options(const char *options, fmode_t flags,
				      void *holder)
{
	substring_t args[MAX_OPT_ARGS];
	char *device_name, *opts, *orig, *p;
	struct btrfs_device *device = NULL;
	int error = 0;

	lockdep_assert_held(&uuid_mutex);

	if (!options)
		return 0;

	/*
	 * strsep changes the string, duplicate it because btrfs_parse_options
	 * gets called later
	 */
	opts = kstrdup(options, GFP_KERNEL);
	if (!opts)
		return -ENOMEM;
	orig = opts;

	while ((p = strsep(&opts, ",")) != NULL) {
		int token;

		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		if (token == Opt_device) {
			device_name = match_strdup(&args[0]);
			if (!device_name) {
				error = -ENOMEM;
				goto out;
			}
			device = btrfs_scan_one_device(device_name, flags,
					holder);
			kfree(device_name);
			if (IS_ERR(device)) {
				error = PTR_ERR(device);
				goto out;
			}
		}
	}

out:
	kfree(orig);
	return error;
}

/*
 * Parse mount options that are related to subvolume id
 *
 * The value is later passed to mount_subvol()
 */
static int btrfs_parse_subvol_options(const char *options, char **subvol_name,
		u64 *subvol_objectid)
{
	substring_t args[MAX_OPT_ARGS];
	char *opts, *orig, *p;
	int error = 0;
	u64 subvolid;

	if (!options)
		return 0;

	/*
	 * strsep changes the string, duplicate it because
	 * btrfs_parse_device_options gets called later
	 */
	opts = kstrdup(options, GFP_KERNEL);
	if (!opts)
		return -ENOMEM;
	orig = opts;

	while ((p = strsep(&opts, ",")) != NULL) {
		int token;
		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_subvol:
			kfree(*subvol_name);
			*subvol_name = match_strdup(&args[0]);
			if (!*subvol_name) {
				error = -ENOMEM;
				goto out;
			}
			break;
		case Opt_subvolid:
			error = match_u64(&args[0], &subvolid);
			if (error)
				goto out;

			/* we want the original fs_tree */
			if (subvolid == 0)
				subvolid = BTRFS_FS_TREE_OBJECTID;

			*subvol_objectid = subvolid;
			break;
		default:
			break;
		}
	}

out:
	kfree(orig);
	return error;
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
	di = btrfs_lookup_dir_item(NULL, root, path, dir_id, "default", 7, 0);
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
			    struct btrfs_fs_devices *fs_devices,
			    void *data)
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
#ifdef CONFIG_BTRFS_FS_POSIX_ACL
	sb->s_flags |= SB_POSIXACL;
#endif
	sb->s_flags |= SB_I_VERSION;
	sb->s_iflags |= SB_I_CGROUPWB;

	err = super_setup_bdi(sb);
	if (err) {
		btrfs_err(fs_info, "super_setup_bdi failed");
		return err;
	}

	err = open_ctree(sb, fs_devices, (char *)data);
	if (err) {
		btrfs_err(fs_info, "open_ctree failed");
		return err;
	}

	inode = btrfs_iget(sb, BTRFS_FIRST_FREE_OBJECTID, fs_info->fs_root);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto fail_close;
	}

	sb->s_root = d_make_root(inode);
	if (!sb->s_root) {
		err = -ENOMEM;
		goto fail_close;
	}

	cleancache_init_fs(sb);
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

	btrfs_wait_ordered_roots(fs_info, U64_MAX, 0, (u64)-1);

	trans = btrfs_attach_transaction_barrier(root);
	if (IS_ERR(trans)) {
		/* no transaction, don't bother */
		if (PTR_ERR(trans) == -ENOENT) {
			/*
			 * Exit unless we have some pending changes
			 * that need to go through commit
			 */
			if (fs_info->pending_changes == 0)
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
#ifdef CONFIG_BTRFS_FS_CHECK_INTEGRITY
	if (btrfs_test_opt(info, CHECK_INTEGRITY_DATA))
		seq_puts(seq, ",check_int_data");
	else if (btrfs_test_opt(info, CHECK_INTEGRITY))
		seq_puts(seq, ",check_int");
	if (info->check_integrity_print_mask)
		seq_printf(seq, ",check_int_print_mask=%d",
				info->check_integrity_print_mask);
#endif
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
	seq_printf(seq, ",subvolid=%llu",
		  BTRFS_I(d_inode(dentry))->root->root_key.objectid);
	subvol_name = btrfs_get_subvol_name_from_objectid(info,
			BTRFS_I(d_inode(dentry))->root->root_key.objectid);
	if (!IS_ERR(subvol_name)) {
		seq_puts(seq, ",subvol=");
		seq_escape(seq, subvol_name, " \t\n\\");
		kfree(subvol_name);
	}
	return 0;
}

static int btrfs_test_super(struct super_block *s, void *data)
{
	struct btrfs_fs_info *p = data;
	struct btrfs_fs_info *fs_info = btrfs_sb(s);

	return fs_info->fs_devices == p->fs_devices;
}

static int btrfs_set_super(struct super_block *s, void *data)
{
	int err = set_anon_super(s, data);
	if (!err)
		s->s_fs_info = data;
	return err;
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
		u64 root_objectid = BTRFS_I(root_inode)->root->root_key.objectid;

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

/*
 * Find a superblock for the given device / mount point.
 *
 * Note: This is based on mount_bdev from fs/super.c with a few additions
 *       for multiple device setup.  Make sure to keep it in sync.
 */
static struct dentry *btrfs_mount_root(struct file_system_type *fs_type,
		int flags, const char *device_name, void *data)
{
	struct block_device *bdev = NULL;
	struct super_block *s;
	struct btrfs_device *device = NULL;
	struct btrfs_fs_devices *fs_devices = NULL;
	struct btrfs_fs_info *fs_info = NULL;
	void *new_sec_opts = NULL;
	fmode_t mode = FMODE_READ;
	int error = 0;

	if (!(flags & SB_RDONLY))
		mode |= FMODE_WRITE;

	if (data) {
		error = security_sb_eat_lsm_opts(data, &new_sec_opts);
		if (error)
			return ERR_PTR(error);
	}

	/*
	 * Setup a dummy root and fs_info for test/set super.  This is because
	 * we don't actually fill this stuff out until open_ctree, but we need
	 * then open_ctree will properly initialize the file system specific
	 * settings later.  btrfs_init_fs_info initializes the static elements
	 * of the fs_info (locks and such) to make cleanup easier if we find a
	 * superblock with our given fs_devices later on at sget() time.
	 */
	fs_info = kvzalloc(sizeof(struct btrfs_fs_info), GFP_KERNEL);
	if (!fs_info) {
		error = -ENOMEM;
		goto error_sec_opts;
	}
	btrfs_init_fs_info(fs_info);

	fs_info->super_copy = kzalloc(BTRFS_SUPER_INFO_SIZE, GFP_KERNEL);
	fs_info->super_for_commit = kzalloc(BTRFS_SUPER_INFO_SIZE, GFP_KERNEL);
	if (!fs_info->super_copy || !fs_info->super_for_commit) {
		error = -ENOMEM;
		goto error_fs_info;
	}

	mutex_lock(&uuid_mutex);
	error = btrfs_parse_device_options(data, mode, fs_type);
	if (error) {
		mutex_unlock(&uuid_mutex);
		goto error_fs_info;
	}

	device = btrfs_scan_one_device(device_name, mode, fs_type);
	if (IS_ERR(device)) {
		mutex_unlock(&uuid_mutex);
		error = PTR_ERR(device);
		goto error_fs_info;
	}

	fs_devices = device->fs_devices;
	fs_info->fs_devices = fs_devices;

	error = btrfs_open_devices(fs_devices, mode, fs_type);
	mutex_unlock(&uuid_mutex);
	if (error)
		goto error_fs_info;

	if (!(flags & SB_RDONLY) && fs_devices->rw_devices == 0) {
		error = -EACCES;
		goto error_close_devices;
	}

	bdev = fs_devices->latest_dev->bdev;
	s = sget(fs_type, btrfs_test_super, btrfs_set_super, flags | SB_NOSEC,
		 fs_info);
	if (IS_ERR(s)) {
		error = PTR_ERR(s);
		goto error_close_devices;
	}

	if (s->s_root) {
		btrfs_close_devices(fs_devices);
		btrfs_free_fs_info(fs_info);
		if ((flags ^ s->s_flags) & SB_RDONLY)
			error = -EBUSY;
	} else {
		snprintf(s->s_id, sizeof(s->s_id), "%pg", bdev);
		btrfs_sb(s)->bdev_holder = fs_type;
		if (!strstr(crc32c_impl(), "generic"))
			set_bit(BTRFS_FS_CSUM_IMPL_FAST, &fs_info->flags);
		error = btrfs_fill_super(s, fs_devices, data);
	}
	if (!error)
		error = security_sb_set_mnt_opts(s, new_sec_opts, 0, NULL);
	security_free_mnt_opts(&new_sec_opts);
	if (error) {
		deactivate_locked_super(s);
		return ERR_PTR(error);
	}

	return dget(s->s_root);

error_close_devices:
	btrfs_close_devices(fs_devices);
error_fs_info:
	btrfs_free_fs_info(fs_info);
error_sec_opts:
	security_free_mnt_opts(&new_sec_opts);
	return ERR_PTR(error);
}

/*
 * Mount function which is called by VFS layer.
 *
 * In order to allow mounting a subvolume directly, btrfs uses mount_subtree()
 * which needs vfsmount* of device's root (/).  This means device's root has to
 * be mounted internally in any case.
 *
 * Operation flow:
 *   1. Parse subvol id related options for later use in mount_subvol().
 *
 *   2. Mount device's root (/) by calling vfs_kern_mount().
 *
 *      NOTE: vfs_kern_mount() is used by VFS to call btrfs_mount() in the
 *      first place. In order to avoid calling btrfs_mount() again, we use
 *      different file_system_type which is not registered to VFS by
 *      register_filesystem() (btrfs_root_fs_type). As a result,
 *      btrfs_mount_root() is called. The return value will be used by
 *      mount_subtree() in mount_subvol().
 *
 *   3. Call mount_subvol() to get the dentry of subvolume. Since there is
 *      "btrfs subvolume set-default", mount_subvol() is called always.
 */
static struct dentry *btrfs_mount(struct file_system_type *fs_type, int flags,
		const char *device_name, void *data)
{
	struct vfsmount *mnt_root;
	struct dentry *root;
	char *subvol_name = NULL;
	u64 subvol_objectid = 0;
	int error = 0;

	error = btrfs_parse_subvol_options(data, &subvol_name,
					&subvol_objectid);
	if (error) {
		kfree(subvol_name);
		return ERR_PTR(error);
	}

	/* mount device's root (/) */
	mnt_root = vfs_kern_mount(&btrfs_root_fs_type, flags, device_name, data);
	if (PTR_ERR_OR_ZERO(mnt_root) == -EBUSY) {
		if (flags & SB_RDONLY) {
			mnt_root = vfs_kern_mount(&btrfs_root_fs_type,
				flags & ~SB_RDONLY, device_name, data);
		} else {
			mnt_root = vfs_kern_mount(&btrfs_root_fs_type,
				flags | SB_RDONLY, device_name, data);
			if (IS_ERR(mnt_root)) {
				root = ERR_CAST(mnt_root);
				kfree(subvol_name);
				goto out;
			}

			down_write(&mnt_root->mnt_sb->s_umount);
			error = btrfs_remount(mnt_root->mnt_sb, &flags, NULL);
			up_write(&mnt_root->mnt_sb->s_umount);
			if (error < 0) {
				root = ERR_PTR(error);
				mntput(mnt_root);
				kfree(subvol_name);
				goto out;
			}
		}
	}
	if (IS_ERR(mnt_root)) {
		root = ERR_CAST(mnt_root);
		kfree(subvol_name);
		goto out;
	}

	/* mount_subvol() will free subvol_name and mnt_root */
	root = mount_subvol(subvol_name, subvol_objectid, mnt_root);

out:
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
	btrfs_workqueue_set_max(fs_info->endio_workers, new_pool_size);
	btrfs_workqueue_set_max(fs_info->endio_meta_workers, new_pool_size);
	btrfs_workqueue_set_max(fs_info->endio_meta_write_workers,
				new_pool_size);
	btrfs_workqueue_set_max(fs_info->endio_write_workers, new_pool_size);
	btrfs_workqueue_set_max(fs_info->endio_freespace_worker, new_pool_size);
	btrfs_workqueue_set_max(fs_info->delayed_workers, new_pool_size);
	btrfs_workqueue_set_max(fs_info->readahead_workers, new_pool_size);
	btrfs_workqueue_set_max(fs_info->scrub_wr_completion_workers,
				new_pool_size);
}

static inline void btrfs_remount_begin(struct btrfs_fs_info *fs_info,
				       unsigned long old_opts, int flags)
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
					 unsigned long old_opts)
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

static int btrfs_remount(struct super_block *sb, int *flags, char *data)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(sb);
	unsigned old_flags = sb->s_flags;
	unsigned long old_opts = fs_info->mount_opt;
	unsigned long old_compress_type = fs_info->compress_type;
	u64 old_max_inline = fs_info->max_inline;
	u32 old_thread_pool_size = fs_info->thread_pool_size;
	u32 old_metadata_ratio = fs_info->metadata_ratio;
	int ret;

	sync_filesystem(sb);
	set_bit(BTRFS_FS_STATE_REMOUNTING, &fs_info->fs_state);

	if (data) {
		void *new_sec_opts = NULL;

		ret = security_sb_eat_lsm_opts(data, &new_sec_opts);
		if (!ret)
			ret = security_sb_remount(sb, new_sec_opts);
		security_free_mnt_opts(&new_sec_opts);
		if (ret)
			goto restore;
	}

	ret = btrfs_parse_options(fs_info, data, *flags);
	if (ret)
		goto restore;

	/* V1 cache is not supported for subpage mount. */
	if (fs_info->sectorsize < PAGE_SIZE && btrfs_test_opt(fs_info, SPACE_CACHE)) {
		btrfs_warn(fs_info,
	"v1 space cache is not supported for page size %lu with sectorsize %u",
			   PAGE_SIZE, fs_info->sectorsize);
		ret = -EINVAL;
		goto restore;
	}
	btrfs_remount_begin(fs_info, old_opts, *flags);
	btrfs_resize_thread_pool(fs_info,
		fs_info->thread_pool_size, old_thread_pool_size);

	if ((bool)btrfs_test_opt(fs_info, FREE_SPACE_TREE) !=
	    (bool)btrfs_fs_compat_ro(fs_info, FREE_SPACE_TREE) &&
	    (!sb_rdonly(sb) || (*flags & SB_RDONLY))) {
		btrfs_warn(fs_info,
		"remount supports changing free space tree only from ro to rw");
		/* Make sure free space cache options match the state on disk */
		if (btrfs_fs_compat_ro(fs_info, FREE_SPACE_TREE)) {
			btrfs_set_opt(fs_info->mount_opt, FREE_SPACE_TREE);
			btrfs_clear_opt(fs_info->mount_opt, SPACE_CACHE);
		}
		if (btrfs_free_space_cache_v1_active(fs_info)) {
			btrfs_clear_opt(fs_info->mount_opt, FREE_SPACE_TREE);
			btrfs_set_opt(fs_info->mount_opt, SPACE_CACHE);
		}
	}

	if ((bool)(*flags & SB_RDONLY) == sb_rdonly(sb))
		goto out;

	if (*flags & SB_RDONLY) {
		/*
		 * this also happens on 'umount -rf' or on shutdown, when
		 * the filesystem is busy.
		 */
		cancel_work_sync(&fs_info->async_reclaim_work);
		cancel_work_sync(&fs_info->async_data_reclaim_work);

		btrfs_discard_cleanup(fs_info);

		/* wait for the uuid_scan task to finish */
		down(&fs_info->uuid_tree_rescan_sem);
		/* avoid complains from lockdep et al. */
		up(&fs_info->uuid_tree_rescan_sem);

		btrfs_set_sb_rdonly(sb);

		/*
		 * Setting SB_RDONLY will put the cleaner thread to
		 * sleep at the next loop if it's already active.
		 * If it's already asleep, we'll leave unused block
		 * groups on disk until we're mounted read-write again
		 * unless we clean them up here.
		 */
		btrfs_delete_unused_bgs(fs_info);

		/*
		 * The cleaner task could be already running before we set the
		 * flag BTRFS_FS_STATE_RO (and SB_RDONLY in the superblock).
		 * We must make sure that after we finish the remount, i.e. after
		 * we call btrfs_commit_super(), the cleaner can no longer start
		 * a transaction - either because it was dropping a dead root,
		 * running delayed iputs or deleting an unused block group (the
		 * cleaner picked a block group from the list of unused block
		 * groups before we were able to in the previous call to
		 * btrfs_delete_unused_bgs()).
		 */
		wait_on_bit(&fs_info->flags, BTRFS_FS_CLEANER_RUNNING,
			    TASK_UNINTERRUPTIBLE);

		/*
		 * We've set the superblock to RO mode, so we might have made
		 * the cleaner task sleep without running all pending delayed
		 * iputs. Go through all the delayed iputs here, so that if an
		 * unmount happens without remounting RW we don't end up at
		 * finishing close_ctree() with a non-empty list of delayed
		 * iputs.
		 */
		btrfs_run_delayed_iputs(fs_info);

		btrfs_dev_replace_suspend_for_unmount(fs_info);
		btrfs_scrub_cancel(fs_info);
		btrfs_pause_balance(fs_info);

		/*
		 * Pause the qgroup rescan worker if it is running. We don't want
		 * it to be still running after we are in RO mode, as after that,
		 * by the time we unmount, it might have left a transaction open,
		 * so we would leak the transaction and/or crash.
		 */
		btrfs_qgroup_wait_for_completion(fs_info, false);

		ret = btrfs_commit_super(fs_info);
		if (ret)
			goto restore;
	} else {
		if (test_bit(BTRFS_FS_STATE_ERROR, &fs_info->fs_state)) {
			btrfs_err(fs_info,
				"Remounting read-write after error is not allowed");
			ret = -EINVAL;
			goto restore;
		}
		if (fs_info->fs_devices->rw_devices == 0) {
			ret = -EACCES;
			goto restore;
		}

		if (!btrfs_check_rw_degradable(fs_info, NULL)) {
			btrfs_warn(fs_info,
		"too many missing devices, writable remount is not allowed");
			ret = -EACCES;
			goto restore;
		}

		if (btrfs_super_log_root(fs_info->super_copy) != 0) {
			btrfs_warn(fs_info,
		"mount required to replay tree-log, cannot remount read-write");
			ret = -EINVAL;
			goto restore;
		}

		/*
		 * NOTE: when remounting with a change that does writes, don't
		 * put it anywhere above this point, as we are not sure to be
		 * safe to write until we pass the above checks.
		 */
		ret = btrfs_start_pre_rw_mount(fs_info);
		if (ret)
			goto restore;

		btrfs_clear_sb_rdonly(sb);

		set_bit(BTRFS_FS_OPEN, &fs_info->flags);
	}
out:
	/*
	 * We need to set SB_I_VERSION here otherwise it'll get cleared by VFS,
	 * since the absence of the flag means it can be toggled off by remount.
	 */
	*flags |= SB_I_VERSION;

	wake_up_process(fs_info->transaction_kthread);
	btrfs_remount_cleanup(fs_info, old_opts);
	btrfs_clear_oneshot_options(fs_info);
	clear_bit(BTRFS_FS_STATE_REMOUNTING, &fs_info->fs_state);

	return 0;

restore:
	/* We've hit an error - don't reset SB_RDONLY */
	if (sb_rdonly(sb))
		old_flags |= SB_RDONLY;
	if (!(old_flags & SB_RDONLY))
		clear_bit(BTRFS_FS_STATE_RO, &fs_info->fs_state);
	sb->s_flags = old_flags;
	fs_info->mount_opt = old_opts;
	fs_info->compress_type = old_compress_type;
	fs_info->max_inline = old_max_inline;
	btrfs_resize_thread_pool(fs_info,
		old_thread_pool_size, fs_info->thread_pool_size);
	fs_info->metadata_ratio = old_metadata_ratio;
	btrfs_remount_cleanup(fs_info, old_opts);
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
	else if (type & BTRFS_BLOCK_GROUP_RAID1)
		num_stripes = 2;
	else if (type & BTRFS_BLOCK_GROUP_RAID1C3)
		num_stripes = 3;
	else if (type & BTRFS_BLOCK_GROUP_RAID1C4)
		num_stripes = 4;
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
		 * In order to avoid overwriting the superblock on the drive,
		 * btrfs starts at an offset of at least 1MB when doing chunk
		 * allocation.
		 *
		 * This ensures we have at least min_stripe_size free space
		 * after excluding 1MB.
		 */
		if (avail_space <= SZ_1M + min_stripe_size)
			continue;

		avail_space -= SZ_1M;

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
		 * Metadata in mixed block goup profiles are accounted in data
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
	    total_free_meta - thresh < block_rsv->size)
		buf->f_bavail = 0;

	buf->f_type = BTRFS_SUPER_MAGIC;
	buf->f_bsize = dentry->d_sb->s_blocksize;
	buf->f_namelen = BTRFS_NAME_LEN;

	/* We treat it as constant endianness (it doesn't matter _which_)
	   because we want the fsid to come out the same whether mounted
	   on a big-endian or little-endian host */
	buf->f_fsid.val[0] = be32_to_cpu(fsid[0]) ^ be32_to_cpu(fsid[2]);
	buf->f_fsid.val[1] = be32_to_cpu(fsid[1]) ^ be32_to_cpu(fsid[3]);
	/* Mask in the root object ID too, to disambiguate subvols */
	buf->f_fsid.val[0] ^=
		BTRFS_I(d_inode(dentry))->root->root_key.objectid >> 32;
	buf->f_fsid.val[1] ^=
		BTRFS_I(d_inode(dentry))->root->root_key.objectid;

	return 0;
}

static void btrfs_kill_super(struct super_block *sb)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(sb);
	kill_anon_super(sb);
	btrfs_free_fs_info(fs_info);
}

static struct file_system_type btrfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "btrfs",
	.mount		= btrfs_mount,
	.kill_sb	= btrfs_kill_super,
	.fs_flags	= FS_REQUIRES_DEV | FS_BINARY_MOUNTDATA,
};

static struct file_system_type btrfs_root_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "btrfs",
	.mount		= btrfs_mount_root,
	.kill_sb	= btrfs_kill_super,
	.fs_flags	= FS_REQUIRES_DEV | FS_BINARY_MOUNTDATA | FS_ALLOW_IDMAP,
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
	int ret = -ENOTTY;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	vol = memdup_user((void __user *)arg, sizeof(*vol));
	if (IS_ERR(vol))
		return PTR_ERR(vol);
	vol->name[BTRFS_PATH_NAME_MAX] = '\0';

	switch (cmd) {
	case BTRFS_IOC_SCAN_DEV:
		mutex_lock(&uuid_mutex);
		device = btrfs_scan_one_device(vol->name, FMODE_READ,
					       &btrfs_root_fs_type);
		ret = PTR_ERR_OR_ZERO(device);
		mutex_unlock(&uuid_mutex);
		break;
	case BTRFS_IOC_FORGET_DEV:
		ret = btrfs_forget_devices(vol->name);
		break;
	case BTRFS_IOC_DEVICES_READY:
		mutex_lock(&uuid_mutex);
		device = btrfs_scan_one_device(vol->name, FMODE_READ,
					       &btrfs_root_fs_type);
		if (IS_ERR(device)) {
			mutex_unlock(&uuid_mutex);
			ret = PTR_ERR(device);
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

	kfree(vol);
	return ret;
}

static int btrfs_freeze(struct super_block *sb)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_fs_info *fs_info = btrfs_sb(sb);
	struct btrfs_root *root = fs_info->tree_root;

	set_bit(BTRFS_FS_FROZEN, &fs_info->flags);
	/*
	 * We don't need a barrier here, we'll wait for any transaction that
	 * could be in progress on other threads (and do delayed iputs that
	 * we want to avoid on a frozen filesystem), or do the commit
	 * ourselves.
	 */
	trans = btrfs_attach_transaction_barrier(root);
	if (IS_ERR(trans)) {
		/* no transaction, don't bother */
		if (PTR_ERR(trans) == -ENOENT)
			return 0;
		return PTR_ERR(trans);
	}
	return btrfs_commit_transaction(trans);
}

static int btrfs_unfreeze(struct super_block *sb)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(sb);

	clear_bit(BTRFS_FS_FROZEN, &fs_info->flags);
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
	seq_escape(m, rcu_str_deref(fs_info->fs_devices->latest_dev->name), " \t\n\\");
	rcu_read_unlock();

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
	.remount_fs	= btrfs_remount,
	.freeze_fs	= btrfs_freeze,
	.unfreeze_fs	= btrfs_unfreeze,
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

static void __init btrfs_print_mod_info(void)
{
	static const char options[] = ""
#ifdef CONFIG_BTRFS_DEBUG
			", debug=on"
#endif
#ifdef CONFIG_BTRFS_ASSERT
			", assert=on"
#endif
#ifdef CONFIG_BTRFS_FS_CHECK_INTEGRITY
			", integrity-checker=on"
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
	pr_info("Btrfs loaded, crc32c=%s%s\n", crc32c_impl(), options);
}

static int __init init_btrfs_fs(void)
{
	int err;

	btrfs_props_init();

	err = btrfs_init_sysfs();
	if (err)
		return err;

	btrfs_init_compress();

	err = btrfs_init_cachep();
	if (err)
		goto free_compress;

	err = extent_io_init();
	if (err)
		goto free_cachep;

	err = extent_state_cache_init();
	if (err)
		goto free_extent_io;

	err = extent_map_init();
	if (err)
		goto free_extent_state_cache;

	err = ordered_data_init();
	if (err)
		goto free_extent_map;

	err = btrfs_delayed_inode_init();
	if (err)
		goto free_ordered_data;

	err = btrfs_auto_defrag_init();
	if (err)
		goto free_delayed_inode;

	err = btrfs_delayed_ref_init();
	if (err)
		goto free_auto_defrag;

	err = btrfs_prelim_ref_init();
	if (err)
		goto free_delayed_ref;

	err = btrfs_end_io_wq_init();
	if (err)
		goto free_prelim_ref;

	err = btrfs_interface_init();
	if (err)
		goto free_end_io_wq;

	btrfs_print_mod_info();

	err = btrfs_run_sanity_tests();
	if (err)
		goto unregister_ioctl;

	err = register_filesystem(&btrfs_fs_type);
	if (err)
		goto unregister_ioctl;

	return 0;

unregister_ioctl:
	btrfs_interface_exit();
free_end_io_wq:
	btrfs_end_io_wq_exit();
free_prelim_ref:
	btrfs_prelim_ref_exit();
free_delayed_ref:
	btrfs_delayed_ref_exit();
free_auto_defrag:
	btrfs_auto_defrag_exit();
free_delayed_inode:
	btrfs_delayed_inode_exit();
free_ordered_data:
	ordered_data_exit();
free_extent_map:
	extent_map_exit();
free_extent_state_cache:
	extent_state_cache_exit();
free_extent_io:
	extent_io_exit();
free_cachep:
	btrfs_destroy_cachep();
free_compress:
	btrfs_exit_compress();
	btrfs_exit_sysfs();

	return err;
}

static void __exit exit_btrfs_fs(void)
{
	btrfs_destroy_cachep();
	btrfs_delayed_ref_exit();
	btrfs_auto_defrag_exit();
	btrfs_delayed_inode_exit();
	btrfs_prelim_ref_exit();
	ordered_data_exit();
	extent_map_exit();
	extent_state_cache_exit();
	extent_io_exit();
	btrfs_interface_exit();
	btrfs_end_io_wq_exit();
	unregister_filesystem(&btrfs_fs_type);
	btrfs_exit_sysfs();
	btrfs_cleanup_fs_uuids();
	btrfs_exit_compress();
}

late_initcall(init_btrfs_fs);
module_exit(exit_btrfs_fs)

MODULE_LICENSE("GPL");
MODULE_SOFTDEP("pre: crc32c");
MODULE_SOFTDEP("pre: xxhash64");
MODULE_SOFTDEP("pre: sha256");
MODULE_SOFTDEP("pre: blake2b-256");
