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
#include "xfs_errortag.h"
#include "xfs_error.h"
#include "xfs_sysfs.h"
#include "xfs_inode.h"

#ifdef DEBUG

static unsigned int xfs_errortag_random_default[] = {
	XFS_RANDOM_DEFAULT,
	XFS_RANDOM_IFLUSH_1,
	XFS_RANDOM_IFLUSH_2,
	XFS_RANDOM_IFLUSH_3,
	XFS_RANDOM_IFLUSH_4,
	XFS_RANDOM_IFLUSH_5,
	XFS_RANDOM_IFLUSH_6,
	XFS_RANDOM_DA_READ_BUF,
	XFS_RANDOM_BTREE_CHECK_LBLOCK,
	XFS_RANDOM_BTREE_CHECK_SBLOCK,
	XFS_RANDOM_ALLOC_READ_AGF,
	XFS_RANDOM_IALLOC_READ_AGI,
	XFS_RANDOM_ITOBP_INOTOBP,
	XFS_RANDOM_IUNLINK,
	XFS_RANDOM_IUNLINK_REMOVE,
	XFS_RANDOM_DIR_INO_VALIDATE,
	XFS_RANDOM_BULKSTAT_READ_CHUNK,
	XFS_RANDOM_IODONE_IOERR,
	XFS_RANDOM_STRATREAD_IOERR,
	XFS_RANDOM_STRATCMPL_IOERR,
	XFS_RANDOM_DIOWRITE_IOERR,
	XFS_RANDOM_BMAPIFORMAT,
	XFS_RANDOM_FREE_EXTENT,
	XFS_RANDOM_RMAP_FINISH_ONE,
	XFS_RANDOM_REFCOUNT_CONTINUE_UPDATE,
	XFS_RANDOM_REFCOUNT_FINISH_ONE,
	XFS_RANDOM_BMAP_FINISH_ONE,
	XFS_RANDOM_AG_RESV_CRITICAL,
	XFS_RANDOM_DROP_WRITES,
	XFS_RANDOM_LOG_BAD_CRC,
	XFS_RANDOM_LOG_ITEM_PIN,
	XFS_RANDOM_BUF_LRU_REF,
	XFS_RANDOM_FORCE_SCRUB_REPAIR,
	XFS_RANDOM_FORCE_SUMMARY_RECALC,
	XFS_RANDOM_IUNLINK_FALLBACK,
	XFS_RANDOM_BUF_IOERROR,
	XFS_RANDOM_REDUCE_MAX_IEXTENTS,
	XFS_RANDOM_BMAP_ALLOC_MINLEN_EXTENT,
	XFS_RANDOM_AG_RESV_FAIL,
	XFS_RANDOM_LARP,
	XFS_RANDOM_DA_LEAF_SPLIT,
	XFS_RANDOM_ATTR_LEAF_TO_NODE,
};

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
	struct xfs_errortag_attr *xfs_attr = to_attr(attr);
	int			ret;
	unsigned int		val;

	if (strcmp(buf, "default") == 0) {
		val = xfs_errortag_random_default[xfs_attr->tag];
	} else {
		ret = kstrtouint(buf, 0, &val);
		if (ret)
			return ret;
	}

	ret = xfs_errortag_set(mp, xfs_attr->tag, val);
	if (ret)
		return ret;
	return count;
}

STATIC ssize_t
xfs_errortag_attr_show(
	struct kobject		*kobject,
	struct attribute	*attr,
	char			*buf)
{
	struct xfs_mount	*mp = to_mp(kobject);
	struct xfs_errortag_attr *xfs_attr = to_attr(attr);

	return snprintf(buf, PAGE_SIZE, "%u\n",
			xfs_errortag_get(mp, xfs_attr->tag));
}

static const struct sysfs_ops xfs_errortag_sysfs_ops = {
	.show = xfs_errortag_attr_show,
	.store = xfs_errortag_attr_store,
};

#define XFS_ERRORTAG_ATTR_RW(_name, _tag) \
static struct xfs_errortag_attr xfs_errortag_attr_##_name = {		\
	.attr = {.name = __stringify(_name),				\
		 .mode = VERIFY_OCTAL_PERMISSIONS(S_IWUSR | S_IRUGO) },	\
	.tag	= (_tag),						\
}

#define XFS_ERRORTAG_ATTR_LIST(_name) &xfs_errortag_attr_##_name.attr

XFS_ERRORTAG_ATTR_RW(noerror,		XFS_ERRTAG_NOERROR);
XFS_ERRORTAG_ATTR_RW(iflush1,		XFS_ERRTAG_IFLUSH_1);
XFS_ERRORTAG_ATTR_RW(iflush2,		XFS_ERRTAG_IFLUSH_2);
XFS_ERRORTAG_ATTR_RW(iflush3,		XFS_ERRTAG_IFLUSH_3);
XFS_ERRORTAG_ATTR_RW(iflush4,		XFS_ERRTAG_IFLUSH_4);
XFS_ERRORTAG_ATTR_RW(iflush5,		XFS_ERRTAG_IFLUSH_5);
XFS_ERRORTAG_ATTR_RW(iflush6,		XFS_ERRTAG_IFLUSH_6);
XFS_ERRORTAG_ATTR_RW(dareadbuf,		XFS_ERRTAG_DA_READ_BUF);
XFS_ERRORTAG_ATTR_RW(btree_chk_lblk,	XFS_ERRTAG_BTREE_CHECK_LBLOCK);
XFS_ERRORTAG_ATTR_RW(btree_chk_sblk,	XFS_ERRTAG_BTREE_CHECK_SBLOCK);
XFS_ERRORTAG_ATTR_RW(readagf,		XFS_ERRTAG_ALLOC_READ_AGF);
XFS_ERRORTAG_ATTR_RW(readagi,		XFS_ERRTAG_IALLOC_READ_AGI);
XFS_ERRORTAG_ATTR_RW(itobp,		XFS_ERRTAG_ITOBP_INOTOBP);
XFS_ERRORTAG_ATTR_RW(iunlink,		XFS_ERRTAG_IUNLINK);
XFS_ERRORTAG_ATTR_RW(iunlinkrm,		XFS_ERRTAG_IUNLINK_REMOVE);
XFS_ERRORTAG_ATTR_RW(dirinovalid,	XFS_ERRTAG_DIR_INO_VALIDATE);
XFS_ERRORTAG_ATTR_RW(bulkstat,		XFS_ERRTAG_BULKSTAT_READ_CHUNK);
XFS_ERRORTAG_ATTR_RW(logiodone,		XFS_ERRTAG_IODONE_IOERR);
XFS_ERRORTAG_ATTR_RW(stratread,		XFS_ERRTAG_STRATREAD_IOERR);
XFS_ERRORTAG_ATTR_RW(stratcmpl,		XFS_ERRTAG_STRATCMPL_IOERR);
XFS_ERRORTAG_ATTR_RW(diowrite,		XFS_ERRTAG_DIOWRITE_IOERR);
XFS_ERRORTAG_ATTR_RW(bmapifmt,		XFS_ERRTAG_BMAPIFORMAT);
XFS_ERRORTAG_ATTR_RW(free_extent,	XFS_ERRTAG_FREE_EXTENT);
XFS_ERRORTAG_ATTR_RW(rmap_finish_one,	XFS_ERRTAG_RMAP_FINISH_ONE);
XFS_ERRORTAG_ATTR_RW(refcount_continue_update,	XFS_ERRTAG_REFCOUNT_CONTINUE_UPDATE);
XFS_ERRORTAG_ATTR_RW(refcount_finish_one,	XFS_ERRTAG_REFCOUNT_FINISH_ONE);
XFS_ERRORTAG_ATTR_RW(bmap_finish_one,	XFS_ERRTAG_BMAP_FINISH_ONE);
XFS_ERRORTAG_ATTR_RW(ag_resv_critical,	XFS_ERRTAG_AG_RESV_CRITICAL);
XFS_ERRORTAG_ATTR_RW(drop_writes,	XFS_ERRTAG_DROP_WRITES);
XFS_ERRORTAG_ATTR_RW(log_bad_crc,	XFS_ERRTAG_LOG_BAD_CRC);
XFS_ERRORTAG_ATTR_RW(log_item_pin,	XFS_ERRTAG_LOG_ITEM_PIN);
XFS_ERRORTAG_ATTR_RW(buf_lru_ref,	XFS_ERRTAG_BUF_LRU_REF);
XFS_ERRORTAG_ATTR_RW(force_repair,	XFS_ERRTAG_FORCE_SCRUB_REPAIR);
XFS_ERRORTAG_ATTR_RW(bad_summary,	XFS_ERRTAG_FORCE_SUMMARY_RECALC);
XFS_ERRORTAG_ATTR_RW(iunlink_fallback,	XFS_ERRTAG_IUNLINK_FALLBACK);
XFS_ERRORTAG_ATTR_RW(buf_ioerror,	XFS_ERRTAG_BUF_IOERROR);
XFS_ERRORTAG_ATTR_RW(reduce_max_iextents,	XFS_ERRTAG_REDUCE_MAX_IEXTENTS);
XFS_ERRORTAG_ATTR_RW(bmap_alloc_minlen_extent,	XFS_ERRTAG_BMAP_ALLOC_MINLEN_EXTENT);
XFS_ERRORTAG_ATTR_RW(ag_resv_fail, XFS_ERRTAG_AG_RESV_FAIL);
XFS_ERRORTAG_ATTR_RW(larp,		XFS_ERRTAG_LARP);
XFS_ERRORTAG_ATTR_RW(da_leaf_split,	XFS_ERRTAG_DA_LEAF_SPLIT);
XFS_ERRORTAG_ATTR_RW(attr_leaf_to_node,	XFS_ERRTAG_ATTR_LEAF_TO_NODE);

static struct attribute *xfs_errortag_attrs[] = {
	XFS_ERRORTAG_ATTR_LIST(noerror),
	XFS_ERRORTAG_ATTR_LIST(iflush1),
	XFS_ERRORTAG_ATTR_LIST(iflush2),
	XFS_ERRORTAG_ATTR_LIST(iflush3),
	XFS_ERRORTAG_ATTR_LIST(iflush4),
	XFS_ERRORTAG_ATTR_LIST(iflush5),
	XFS_ERRORTAG_ATTR_LIST(iflush6),
	XFS_ERRORTAG_ATTR_LIST(dareadbuf),
	XFS_ERRORTAG_ATTR_LIST(btree_chk_lblk),
	XFS_ERRORTAG_ATTR_LIST(btree_chk_sblk),
	XFS_ERRORTAG_ATTR_LIST(readagf),
	XFS_ERRORTAG_ATTR_LIST(readagi),
	XFS_ERRORTAG_ATTR_LIST(itobp),
	XFS_ERRORTAG_ATTR_LIST(iunlink),
	XFS_ERRORTAG_ATTR_LIST(iunlinkrm),
	XFS_ERRORTAG_ATTR_LIST(dirinovalid),
	XFS_ERRORTAG_ATTR_LIST(bulkstat),
	XFS_ERRORTAG_ATTR_LIST(logiodone),
	XFS_ERRORTAG_ATTR_LIST(stratread),
	XFS_ERRORTAG_ATTR_LIST(stratcmpl),
	XFS_ERRORTAG_ATTR_LIST(diowrite),
	XFS_ERRORTAG_ATTR_LIST(bmapifmt),
	XFS_ERRORTAG_ATTR_LIST(free_extent),
	XFS_ERRORTAG_ATTR_LIST(rmap_finish_one),
	XFS_ERRORTAG_ATTR_LIST(refcount_continue_update),
	XFS_ERRORTAG_ATTR_LIST(refcount_finish_one),
	XFS_ERRORTAG_ATTR_LIST(bmap_finish_one),
	XFS_ERRORTAG_ATTR_LIST(ag_resv_critical),
	XFS_ERRORTAG_ATTR_LIST(drop_writes),
	XFS_ERRORTAG_ATTR_LIST(log_bad_crc),
	XFS_ERRORTAG_ATTR_LIST(log_item_pin),
	XFS_ERRORTAG_ATTR_LIST(buf_lru_ref),
	XFS_ERRORTAG_ATTR_LIST(force_repair),
	XFS_ERRORTAG_ATTR_LIST(bad_summary),
	XFS_ERRORTAG_ATTR_LIST(iunlink_fallback),
	XFS_ERRORTAG_ATTR_LIST(buf_ioerror),
	XFS_ERRORTAG_ATTR_LIST(reduce_max_iextents),
	XFS_ERRORTAG_ATTR_LIST(bmap_alloc_minlen_extent),
	XFS_ERRORTAG_ATTR_LIST(ag_resv_fail),
	XFS_ERRORTAG_ATTR_LIST(larp),
	XFS_ERRORTAG_ATTR_LIST(da_leaf_split),
	XFS_ERRORTAG_ATTR_LIST(attr_leaf_to_node),
	NULL,
};
ATTRIBUTE_GROUPS(xfs_errortag);

static struct kobj_type xfs_errortag_ktype = {
	.release = xfs_sysfs_release,
	.sysfs_ops = &xfs_errortag_sysfs_ops,
	.default_groups = xfs_errortag_groups,
};

int
xfs_errortag_init(
	struct xfs_mount	*mp)
{
	int ret;

	mp->m_errortag = kmem_zalloc(sizeof(unsigned int) * XFS_ERRTAG_MAX,
			KM_MAYFAIL);
	if (!mp->m_errortag)
		return -ENOMEM;

	ret = xfs_sysfs_init(&mp->m_errortag_kobj, &xfs_errortag_ktype,
				&mp->m_kobj, "errortag");
	if (ret)
		kmem_free(mp->m_errortag);
	return ret;
}

void
xfs_errortag_del(
	struct xfs_mount	*mp)
{
	xfs_sysfs_del(&mp->m_errortag_kobj);
	kmem_free(mp->m_errortag);
}

bool
xfs_errortag_test(
	struct xfs_mount	*mp,
	const char		*expression,
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

	ASSERT(error_tag < XFS_ERRTAG_MAX);
	randfactor = mp->m_errortag[error_tag];
	if (!randfactor || prandom_u32_max(randfactor))
		return false;

	xfs_warn_ratelimited(mp,
"Injecting error (%s) at file %s, line %d, on filesystem \"%s\"",
			expression, file, line, mp->m_super->s_id);
	return true;
}

int
xfs_errortag_get(
	struct xfs_mount	*mp,
	unsigned int		error_tag)
{
	if (error_tag >= XFS_ERRTAG_MAX)
		return -EINVAL;

	return mp->m_errortag[error_tag];
}

int
xfs_errortag_set(
	struct xfs_mount	*mp,
	unsigned int		error_tag,
	unsigned int		tag_value)
{
	if (error_tag >= XFS_ERRTAG_MAX)
		return -EINVAL;

	mp->m_errortag[error_tag] = tag_value;
	return 0;
}

int
xfs_errortag_add(
	struct xfs_mount	*mp,
	unsigned int		error_tag)
{
	BUILD_BUG_ON(ARRAY_SIZE(xfs_errortag_random_default) != XFS_ERRTAG_MAX);

	if (error_tag >= XFS_ERRTAG_MAX)
		return -EINVAL;

	return xfs_errortag_set(mp, error_tag,
			xfs_errortag_random_default[error_tag]);
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
