/*
 * Copyright (c) 2000-2001,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include "xfs.h"
#include "xfs_format.h"
#include "xfs_fs.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_error.h"

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
};

int
xfs_errortag_init(
	struct xfs_mount	*mp)
{
	mp->m_errortag = kmem_zalloc(sizeof(unsigned int) * XFS_ERRTAG_MAX,
			KM_SLEEP | KM_MAYFAIL);
	if (!mp->m_errortag)
		return -ENOMEM;
	return 0;
}

void
xfs_errortag_del(
	struct xfs_mount	*mp)
{
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

	ASSERT(error_tag < XFS_ERRTAG_MAX);
	randfactor = mp->m_errortag[error_tag];
	if (!randfactor || prandom_u32() % randfactor)
		return false;

	xfs_warn_ratelimited(mp,
"Injecting error (%s) at file %s, line %d, on filesystem \"%s\"",
			expression, file, line, mp->m_fsname);
	return true;
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
	void			*ra)
{
	if (level <= xfs_error_level) {
		xfs_alert_tag(mp, XFS_PTAG_ERROR_REPORT,
		"Internal error %s at line %d of file %s.  Caller %pS",
			    tag, linenum, filename, ra);

		xfs_stack_trace();
	}
}

void
xfs_corruption_error(
	const char		*tag,
	int			level,
	struct xfs_mount	*mp,
	void			*p,
	const char		*filename,
	int			linenum,
	void			*ra)
{
	if (level <= xfs_error_level)
		xfs_hex_dump(p, 64);
	xfs_error_report(tag, level, mp, filename, linenum, ra);
	xfs_alert(mp, "Corruption detected. Unmount and run xfs_repair");
}

/*
 * Warnings specifically for verifier errors.  Differentiate CRC vs. invalid
 * values, and omit the stack trace unless the error level is tuned high.
 */
void
xfs_verifier_error(
	struct xfs_buf		*bp)
{
	struct xfs_mount *mp = bp->b_target->bt_mount;

	xfs_alert(mp, "Metadata %s detected at %pF, %s block 0x%llx",
		  bp->b_error == -EFSBADCRC ? "CRC error" : "corruption",
		  __return_address, bp->b_ops->name, bp->b_bn);

	xfs_alert(mp, "Unmount and run xfs_repair");

	if (xfs_error_level >= XFS_ERRLEVEL_LOW) {
		xfs_alert(mp, "First 64 bytes of corrupted metadata buffer:");
		xfs_hex_dump(xfs_buf_offset(bp, 0), 64);
	}

	if (xfs_error_level >= XFS_ERRLEVEL_HIGH)
		xfs_stack_trace();
}
