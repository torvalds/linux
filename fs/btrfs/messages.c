// SPDX-License-Identifier: GPL-2.0

#include "fs.h"
#include "messages.h"
#include "discard.h"
#include "super.h"

#ifdef CONFIG_PRINTK

#define STATE_STRING_PREFACE	" state "
#define STATE_STRING_BUF_LEN	(sizeof(STATE_STRING_PREFACE) + BTRFS_FS_STATE_COUNT + 1)

/*
 * Characters to print to indicate error conditions or uncommon filesystem state.
 * RO is not an error.
 */
static const char fs_state_chars[] = {
	[BTRFS_FS_STATE_REMOUNTING]		= 'M',
	[BTRFS_FS_STATE_RO]			= 0,
	[BTRFS_FS_STATE_TRANS_ABORTED]		= 'A',
	[BTRFS_FS_STATE_DEV_REPLACING]		= 'R',
	[BTRFS_FS_STATE_DUMMY_FS_INFO]		= 0,
	[BTRFS_FS_STATE_NO_CSUMS]		= 'C',
	[BTRFS_FS_STATE_LOG_CLEANUP_ERROR]	= 'L',
};

static void btrfs_state_to_string(const struct btrfs_fs_info *info, char *buf)
{
	unsigned int bit;
	bool states_printed = false;
	unsigned long fs_state = READ_ONCE(info->fs_state);
	char *curr = buf;

	memcpy(curr, STATE_STRING_PREFACE, sizeof(STATE_STRING_PREFACE));
	curr += sizeof(STATE_STRING_PREFACE) - 1;

	if (BTRFS_FS_ERROR(info)) {
		*curr++ = 'E';
		states_printed = true;
	}

	for_each_set_bit(bit, &fs_state, sizeof(fs_state)) {
		WARN_ON_ONCE(bit >= BTRFS_FS_STATE_COUNT);
		if ((bit < BTRFS_FS_STATE_COUNT) && fs_state_chars[bit]) {
			*curr++ = fs_state_chars[bit];
			states_printed = true;
		}
	}

	/* If no states were printed, reset the buffer */
	if (!states_printed)
		curr = buf;

	*curr++ = 0;
}
#endif

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
const char * __attribute_const__ btrfs_decode_error(int error)
{
	char *errstr = "unknown";

	switch (error) {
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
 * Decodes expected errors from the caller and invokes the appropriate error
 * response.
 */
__cold
void __btrfs_handle_fs_error(struct btrfs_fs_info *fs_info, const char *function,
		       unsigned int line, int error, const char *fmt, ...)
{
	struct super_block *sb = fs_info->sb;
#ifdef CONFIG_PRINTK
	char statestr[STATE_STRING_BUF_LEN];
	const char *errstr;
#endif

#ifdef CONFIG_PRINTK_INDEX
	printk_index_subsys_emit(
		"BTRFS: error (device %s%s) in %s:%d: errno=%d %s", KERN_CRIT, fmt);
#endif

	/*
	 * Special case: if the error is EROFS, and we're already under
	 * SB_RDONLY, then it is safe here.
	 */
	if (error == -EROFS && sb_rdonly(sb))
		return;

#ifdef CONFIG_PRINTK
	errstr = btrfs_decode_error(error);
	btrfs_state_to_string(fs_info, statestr);
	if (fmt) {
		struct va_format vaf;
		va_list args;

		va_start(args, fmt);
		vaf.fmt = fmt;
		vaf.va = &args;

		pr_crit("BTRFS: error (device %s%s) in %s:%d: errno=%d %s (%pV)\n",
			sb->s_id, statestr, function, line, error, errstr, &vaf);
		va_end(args);
	} else {
		pr_crit("BTRFS: error (device %s%s) in %s:%d: errno=%d %s\n",
			sb->s_id, statestr, function, line, error, errstr);
	}
#endif

	/*
	 * Today we only save the error info to memory.  Long term we'll also
	 * send it down to the disk.
	 */
	WRITE_ONCE(fs_info->fs_error, error);

	/* Don't go through full error handling during mount. */
	if (!(sb->s_flags & SB_BORN))
		return;

	if (sb_rdonly(sb))
		return;

	btrfs_discard_stop(fs_info);

	/* Handle error by forcing the filesystem readonly. */
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

void __cold _btrfs_printk(const struct btrfs_fs_info *fs_info, const char *fmt, ...)
{
	char lvl[PRINTK_MAX_SINGLE_HEADER_LEN + 1] = "\0";
	struct va_format vaf;
	va_list args;
	int kern_level;
	const char *type = logtypes[4];
	struct ratelimit_state *ratelimit = &printk_limits[4];

#ifdef CONFIG_PRINTK_INDEX
	printk_index_subsys_emit("%sBTRFS %s (device %s): ", NULL, fmt);
#endif

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
		if (fs_info) {
			char statestr[STATE_STRING_BUF_LEN];

			btrfs_state_to_string(fs_info, statestr);
			_printk("%sBTRFS %s (device %s%s): %pV\n", lvl, type,
				fs_info->sb->s_id, statestr, &vaf);
		} else {
			_printk("%sBTRFS %s: %pV\n", lvl, type, &vaf);
		}
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
 * Decode unexpected, fatal errors from the caller, issue an alert, and either
 * panic or BUGs, depending on mount options.
 */
__cold
void __btrfs_panic(const struct btrfs_fs_info *fs_info, const char *function,
		   unsigned int line, int error, const char *fmt, ...)
{
	char *s_id = "<unknown>";
	const char *errstr;
	struct va_format vaf = { .fmt = fmt };
	va_list args;

	if (fs_info)
		s_id = fs_info->sb->s_id;

	va_start(args, fmt);
	vaf.va = &args;

	errstr = btrfs_decode_error(error);
	if (fs_info && (btrfs_test_opt(fs_info, PANIC_ON_FATAL_ERROR)))
		panic(KERN_CRIT "BTRFS panic (device %s) in %s:%d: %pV (errno=%d %s)\n",
			s_id, function, line, &vaf, error, errstr);

	btrfs_crit(fs_info, "panic in %s:%d: %pV (errno=%d %s)",
		   function, line, &vaf, error, errstr);
	va_end(args);
	/* Caller calls BUG() */
}
