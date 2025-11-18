// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2001,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_fs.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_error.h"
#include "xfs_sysfs.h"
#include "xfs_inode.h"

#ifdef DEBUG

#define XFS_ERRTAG(_tag, _name, _default) \
	[XFS_ERRTAG_##_tag]	= (_default),
#include "xfs_errortag.h"
static const unsigned int xfs_errortag_random_default[] = { XFS_ERRTAGS };
#undef XFS_ERRTAG

struct xfs_errortag_attr {
	struct attribute	attr;
	unsigned int		tag;
};

static inline struct xfs_errortag_attr *
to_attr(struct attribute *attr)
{
	return container_of(attr, struct xfs_errortag_attr, attr);
}

static inline struct xfs_mount *
to_mp(struct kobject *kobject)
{
	struct xfs_kobj *kobj = to_kobj(kobject);

	return container_of(kobj, struct xfs_mount, m_errortag_kobj);
}

STATIC ssize_t
xfs_errortag_attr_store(
	struct kobject		*kobject,
	struct attribute	*attr,
	const char		*buf,
	size_t			count)
{
	struct xfs_mount	*mp = to_mp(kobject);
	unsigned int		error_tag = to_attr(attr)->tag;
	int			ret;

	if (strcmp(buf, "default") == 0) {
		mp->m_errortag[error_tag] =
			xfs_errortag_random_default[error_tag];
	} else {
		ret = kstrtouint(buf, 0, &mp->m_errortag[error_tag]);
		if (ret)
			return ret;
	}

	return count;
}

STATIC ssize_t
xfs_errortag_attr_show(
	struct kobject		*kobject,
	struct attribute	*attr,
	char			*buf)
{
	struct xfs_mount	*mp = to_mp(kobject);
	unsigned int		error_tag = to_attr(attr)->tag;

	return snprintf(buf, PAGE_SIZE, "%u\n", mp->m_errortag[error_tag]);
}

static const struct sysfs_ops xfs_errortag_sysfs_ops = {
	.show = xfs_errortag_attr_show,
	.store = xfs_errortag_attr_store,
};

#define XFS_ERRTAG(_tag, _name, _default)				\
static struct xfs_errortag_attr xfs_errortag_attr_##_name = {		\
	.attr = {.name = __stringify(_name),				\
		 .mode = VERIFY_OCTAL_PERMISSIONS(S_IWUSR | S_IRUGO) },	\
	.tag	= XFS_ERRTAG_##_tag,					\
};
#include "xfs_errortag.h"
XFS_ERRTAGS
#undef XFS_ERRTAG

#define XFS_ERRTAG(_tag, _name, _default) \
	&xfs_errortag_attr_##_name.attr,
#include "xfs_errortag.h"
static struct attribute *xfs_errortag_attrs[] = {
	XFS_ERRTAGS
	NULL
};
ATTRIBUTE_GROUPS(xfs_errortag);
#undef XFS_ERRTAG

/* -1 because XFS_ERRTAG_DROP_WRITES got removed, + 1 for NULL termination */
static_assert(ARRAY_SIZE(xfs_errortag_attrs) == XFS_ERRTAG_MAX);

static const struct kobj_type xfs_errortag_ktype = {
	.release = xfs_sysfs_release,
	.sysfs_ops = &xfs_errortag_sysfs_ops,
	.default_groups = xfs_errortag_groups,
};

int
xfs_errortag_init(
	struct xfs_mount	*mp)
{
	int ret;

	mp->m_errortag = kzalloc(sizeof(unsigned int) * XFS_ERRTAG_MAX,
				GFP_KERNEL | __GFP_RETRY_MAYFAIL);
	if (!mp->m_errortag)
		return -ENOMEM;

	ret = xfs_sysfs_init(&mp->m_errortag_kobj, &xfs_errortag_ktype,
				&mp->m_kobj, "errortag");
	if (ret)
		kfree(mp->m_errortag);
	return ret;
}

void
xfs_errortag_del(
	struct xfs_mount	*mp)
{
	xfs_sysfs_del(&mp->m_errortag_kobj);
	kfree(mp->m_errortag);
}

static bool
xfs_errortag_valid(
	unsigned int		error_tag)
{
	if (error_tag >= XFS_ERRTAG_MAX)
		return false;

	/* Error out removed injection types */
	if (error_tag == XFS_ERRTAG_DROP_WRITES)
		return false;
	return true;
}

bool
xfs_errortag_enabled(
	struct xfs_mount	*mp,
	unsigned int		tag)
{
	if (!mp->m_errortag)
		return false;
	if (!xfs_errortag_valid(tag))
		return false;

	return mp->m_errortag[tag] != 0;
}

bool
xfs_errortag_test(
	struct xfs_mount	*mp,
	const char		*file,
	int			line,
	unsigned int		error_tag)
{
	unsigned int		randfactor;

	/*
	 * To be able to use error injection anywhere, we need to ensure error
	 * injection mechanism is already initialized.
	 *
	 * Code paths like I/O completion can be called before the
	 * initialization is complete, but be able to inject errors in such
	 * places is still useful.
	 */
	if (!mp->m_errortag)
		return false;

	if (!xfs_errortag_valid(error_tag))
		return false;

	randfactor = mp->m_errortag[error_tag];
	if (!randfactor || get_random_u32_below(randfactor))
		return false;

	xfs_warn_ratelimited(mp,
"Injecting error at file %s, line %d, on filesystem \"%s\"",
			file, line, mp->m_super->s_id);
	return true;
}

int
xfs_errortag_add(
	struct xfs_mount	*mp,
	unsigned int		error_tag)
{
	BUILD_BUG_ON(ARRAY_SIZE(xfs_errortag_random_default) != XFS_ERRTAG_MAX);

	if (!xfs_errortag_valid(error_tag))
		return -EINVAL;
	mp->m_errortag[error_tag] = xfs_errortag_random_default[error_tag];
	return 0;
}

int
xfs_errortag_clearall(
	struct xfs_mount	*mp)
{
	memset(mp->m_errortag, 0, sizeof(unsigned int) * XFS_ERRTAG_MAX);
	return 0;
}
#endif /* DEBUG */

void
xfs_error_report(
	const char		*tag,
	int			level,
	struct xfs_mount	*mp,
	const char		*filename,
	int			linenum,
	xfs_failaddr_t		failaddr)
{
	if (level <= xfs_error_level) {
		xfs_alert_tag(mp, XFS_PTAG_ERROR_REPORT,
		"Internal error %s at line %d of file %s.  Caller %pS",
			    tag, linenum, filename, failaddr);

		xfs_stack_trace();
	}
}

void
xfs_corruption_error(
	const char		*tag,
	int			level,
	struct xfs_mount	*mp,
	const void		*buf,
	size_t			bufsize,
	const char		*filename,
	int			linenum,
	xfs_failaddr_t		failaddr)
{
	if (buf && level <= xfs_error_level)
		xfs_hex_dump(buf, bufsize);
	xfs_error_report(tag, level, mp, filename, linenum, failaddr);
	xfs_alert(mp, "Corruption detected. Unmount and run xfs_repair");
}

/*
 * Complain about the kinds of metadata corruption that we can't detect from a
 * verifier, such as incorrect inter-block relationship data.  Does not set
 * bp->b_error.
 *
 * Call xfs_buf_mark_corrupt, not this function.
 */
void
xfs_buf_corruption_error(
	struct xfs_buf		*bp,
	xfs_failaddr_t		fa)
{
	struct xfs_mount	*mp = bp->b_mount;

	xfs_alert_tag(mp, XFS_PTAG_VERIFIER_ERROR,
		  "Metadata corruption detected at %pS, %s block 0x%llx",
		  fa, bp->b_ops->name, xfs_buf_daddr(bp));

	xfs_alert(mp, "Unmount and run xfs_repair");

	if (xfs_error_level >= XFS_ERRLEVEL_HIGH)
		xfs_stack_trace();
}

/*
 * Warnings specifically for verifier errors.  Differentiate CRC vs. invalid
 * values, and omit the stack trace unless the error level is tuned high.
 */
void
xfs_buf_verifier_error(
	struct xfs_buf		*bp,
	int			error,
	const char		*name,
	const void		*buf,
	size_t			bufsz,
	xfs_failaddr_t		failaddr)
{
	struct xfs_mount	*mp = bp->b_mount;
	xfs_failaddr_t		fa;
	int			sz;

	fa = failaddr ? failaddr : __return_address;
	__xfs_buf_ioerror(bp, error, fa);

	xfs_alert_tag(mp, XFS_PTAG_VERIFIER_ERROR,
		  "Metadata %s detected at %pS, %s block 0x%llx %s",
		  bp->b_error == -EFSBADCRC ? "CRC error" : "corruption",
		  fa, bp->b_ops->name, xfs_buf_daddr(bp), name);

	xfs_alert(mp, "Unmount and run xfs_repair");

	if (xfs_error_level >= XFS_ERRLEVEL_LOW) {
		sz = min_t(size_t, XFS_CORRUPTION_DUMP_LEN, bufsz);
		xfs_alert(mp, "First %d bytes of corrupted metadata buffer:",
				sz);
		xfs_hex_dump(buf, sz);
	}

	if (xfs_error_level >= XFS_ERRLEVEL_HIGH)
		xfs_stack_trace();
}

/*
 * Warnings specifically for verifier errors.  Differentiate CRC vs. invalid
 * values, and omit the stack trace unless the error level is tuned high.
 */
void
xfs_verifier_error(
	struct xfs_buf		*bp,
	int			error,
	xfs_failaddr_t		failaddr)
{
	return xfs_buf_verifier_error(bp, error, "", xfs_buf_offset(bp, 0),
			XFS_CORRUPTION_DUMP_LEN, failaddr);
}

/*
 * Warnings for inode corruption problems.  Don't bother with the stack
 * trace unless the error level is turned up high.
 */
void
xfs_inode_verifier_error(
	struct xfs_inode	*ip,
	int			error,
	const char		*name,
	const void		*buf,
	size_t			bufsz,
	xfs_failaddr_t		failaddr)
{
	struct xfs_mount	*mp = ip->i_mount;
	xfs_failaddr_t		fa;
	int			sz;

	fa = failaddr ? failaddr : __return_address;

	xfs_alert(mp, "Metadata %s detected at %pS, inode 0x%llx %s",
		  error == -EFSBADCRC ? "CRC error" : "corruption",
		  fa, ip->i_ino, name);

	xfs_alert(mp, "Unmount and run xfs_repair");

	if (buf && xfs_error_level >= XFS_ERRLEVEL_LOW) {
		sz = min_t(size_t, XFS_CORRUPTION_DUMP_LEN, bufsz);
		xfs_alert(mp, "First %d bytes of corrupted metadata buffer:",
				sz);
		xfs_hex_dump(buf, sz);
	}

	if (xfs_error_level >= XFS_ERRLEVEL_HIGH)
		xfs_stack_trace();
}
