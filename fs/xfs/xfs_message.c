// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2011 Red Hat, Inc.  All Rights Reserved.
 */

#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_error.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"

/*
 * XFS logging functions
 */
static void
__xfs_printk(
	const char		*level,
	const struct xfs_mount	*mp,
	struct va_format	*vaf)
{
	if (mp && mp->m_super) {
		printk("%sXFS (%s): %pV\n", level, mp->m_super->s_id, vaf);
		return;
	}
	printk("%sXFS: %pV\n", level, vaf);
}

void
xfs_printk_level(
	const char *kern_level,
	const struct xfs_mount *mp,
	const char *fmt, ...)
{
	struct va_format	vaf;
	va_list			args;
	int			level;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;

	__xfs_printk(kern_level, mp, &vaf);

	va_end(args);

	if (!kstrtoint(kern_level, 0, &level) &&
	    level <= LOGLEVEL_ERR &&
	    xfs_error_level >= XFS_ERRLEVEL_HIGH)
		xfs_stack_trace();
}

void
_xfs_alert_tag(
	const struct xfs_mount	*mp,
	uint32_t		panic_tag,
	const char		*fmt, ...)
{
	struct va_format	vaf;
	va_list			args;
	int			do_panic = 0;

	if (xfs_panic_mask && (xfs_panic_mask & panic_tag)) {
		xfs_alert(mp, "Transforming an alert into a BUG.");
		do_panic = 1;
	}

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	__xfs_printk(KERN_ALERT, mp, &vaf);
	va_end(args);

	BUG_ON(do_panic);
}

void
asswarn(
	struct xfs_mount	*mp,
	char			*expr,
	char			*file,
	int			line)
{
	xfs_warn(mp, "Assertion failed: %s, file: %s, line: %d",
		expr, file, line);
	WARN_ON(1);
}

void
assfail(
	struct xfs_mount	*mp,
	char			*expr,
	char			*file,
	int			line)
{
	xfs_emerg(mp, "Assertion failed: %s, file: %s, line: %d",
		expr, file, line);
	if (xfs_globals.bug_on_assert)
		BUG();
	else
		WARN_ON(1);
}

void
xfs_hex_dump(const void *p, int length)
{
	print_hex_dump(KERN_ALERT, "", DUMP_PREFIX_OFFSET, 16, 1, p, length, 1);
}

void
xfs_buf_alert_ratelimited(
	struct xfs_buf		*bp,
	const char		*rlmsg,
	const char		*fmt,
	...)
{
	struct xfs_mount	*mp = bp->b_mount;
	struct va_format	vaf;
	va_list			args;

	/* use the more aggressive per-target rate limit for buffers */
	if (!___ratelimit(&bp->b_target->bt_ioerror_rl, rlmsg))
		return;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	__xfs_printk(KERN_ALERT, mp, &vaf);
	va_end(args);
}

void
xfs_warn_experimental(
	struct xfs_mount		*mp,
	enum xfs_experimental_feat	feat)
{
	static const struct {
		const char		*name;
		long			opstate;
	} features[] = {
		[XFS_EXPERIMENTAL_SHRINK] = {
			.opstate	= XFS_OPSTATE_WARNED_SHRINK,
			.name		= "online shrink",
		},
		[XFS_EXPERIMENTAL_LARP] = {
			.opstate	= XFS_OPSTATE_WARNED_LARP,
			.name		= "logged extended attributes",
		},
		[XFS_EXPERIMENTAL_LBS] = {
			.opstate	= XFS_OPSTATE_WARNED_LBS,
			.name		= "large block size",
		},
		[XFS_EXPERIMENTAL_METADIR] = {
			.opstate	= XFS_OPSTATE_WARNED_METADIR,
			.name		= "metadata directory tree",
		},
		[XFS_EXPERIMENTAL_ZONED] = {
			.opstate	= XFS_OPSTATE_WARNED_ZONED,
			.name		= "zoned RT device",
		},
	};
	ASSERT(feat >= 0 && feat < XFS_EXPERIMENTAL_MAX);
	BUILD_BUG_ON(ARRAY_SIZE(features) != XFS_EXPERIMENTAL_MAX);

	if (xfs_should_warn(mp, features[feat].opstate))
		xfs_warn(mp,
 "EXPERIMENTAL %s feature enabled.  Use at your own risk!",
				features[feat].name);
}
