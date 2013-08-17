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
#include "xfs_fs.h"
#include "xfs_types.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_utils.h"
#include "xfs_error.h"

#ifdef DEBUG

int	xfs_etrap[XFS_ERROR_NTRAP] = {
	0,
};

int
xfs_error_trap(int e)
{
	int i;

	if (!e)
		return 0;
	for (i = 0; i < XFS_ERROR_NTRAP; i++) {
		if (xfs_etrap[i] == 0)
			break;
		if (e != xfs_etrap[i])
			continue;
		xfs_notice(NULL, "%s: error %d", __func__, e);
		BUG();
		break;
	}
	return e;
}

int	xfs_etest[XFS_NUM_INJECT_ERROR];
int64_t	xfs_etest_fsid[XFS_NUM_INJECT_ERROR];
char *	xfs_etest_fsname[XFS_NUM_INJECT_ERROR];
int	xfs_error_test_active;

int
xfs_error_test(int error_tag, int *fsidp, char *expression,
	       int line, char *file, unsigned long randfactor)
{
	int i;
	int64_t fsid;

	if (random32() % randfactor)
		return 0;

	memcpy(&fsid, fsidp, sizeof(xfs_fsid_t));

	for (i = 0; i < XFS_NUM_INJECT_ERROR; i++)  {
		if (xfs_etest[i] == error_tag && xfs_etest_fsid[i] == fsid) {
			xfs_warn(NULL,
	"Injecting error (%s) at file %s, line %d, on filesystem \"%s\"",
				expression, file, line, xfs_etest_fsname[i]);
			return 1;
		}
	}

	return 0;
}

int
xfs_errortag_add(int error_tag, xfs_mount_t *mp)
{
	int i;
	int len;
	int64_t fsid;

	memcpy(&fsid, mp->m_fixedfsid, sizeof(xfs_fsid_t));

	for (i = 0; i < XFS_NUM_INJECT_ERROR; i++)  {
		if (xfs_etest_fsid[i] == fsid && xfs_etest[i] == error_tag) {
			xfs_warn(mp, "error tag #%d on", error_tag);
			return 0;
		}
	}

	for (i = 0; i < XFS_NUM_INJECT_ERROR; i++)  {
		if (xfs_etest[i] == 0) {
			xfs_warn(mp, "Turned on XFS error tag #%d",
				error_tag);
			xfs_etest[i] = error_tag;
			xfs_etest_fsid[i] = fsid;
			len = strlen(mp->m_fsname);
			xfs_etest_fsname[i] = kmem_alloc(len + 1, KM_SLEEP);
			strcpy(xfs_etest_fsname[i], mp->m_fsname);
			xfs_error_test_active++;
			return 0;
		}
	}

	xfs_warn(mp, "error tag overflow, too many turned on");

	return 1;
}

int
xfs_errortag_clearall(xfs_mount_t *mp, int loud)
{
	int64_t fsid;
	int cleared = 0;
	int i;

	memcpy(&fsid, mp->m_fixedfsid, sizeof(xfs_fsid_t));


	for (i = 0; i < XFS_NUM_INJECT_ERROR; i++) {
		if ((fsid == 0LL || xfs_etest_fsid[i] == fsid) &&
		     xfs_etest[i] != 0) {
			cleared = 1;
			xfs_warn(mp, "Clearing XFS error tag #%d",
				xfs_etest[i]);
			xfs_etest[i] = 0;
			xfs_etest_fsid[i] = 0LL;
			kmem_free(xfs_etest_fsname[i]);
			xfs_etest_fsname[i] = NULL;
			xfs_error_test_active--;
		}
	}

	if (loud || cleared)
		xfs_warn(mp, "Cleared all XFS error tags for filesystem");

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
	inst_t			*ra)
{
	if (level <= xfs_error_level) {
		xfs_alert_tag(mp, XFS_PTAG_ERROR_REPORT,
		"Internal error %s at line %d of file %s.  Caller 0x%p\n",
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
	inst_t			*ra)
{
	if (level <= xfs_error_level)
		xfs_hex_dump(p, 16);
	xfs_error_report(tag, level, mp, filename, linenum, ra);
	xfs_alert(mp, "Corruption detected. Unmount and run xfs_repair");
}
