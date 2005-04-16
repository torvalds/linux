/*
 * Copyright (c) 2000-2001 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */

#include "xfs.h"

#include "xfs_macros.h"
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_sb.h"
#include "xfs_trans.h"
#include "xfs_dir.h"
#include "xfs_dir2.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_attr_sf.h"
#include "xfs_dir_sf.h"
#include "xfs_dir2_sf.h"
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
		cmn_err(CE_NOTE, "xfs_error_trap: error %d", e);
		debug_stop_all_cpus((void *)-1LL);
		BUG();
		break;
	}
	return e;
}
#endif

#if (defined(DEBUG) || defined(INDUCE_IO_ERROR))

int	xfs_etest[XFS_NUM_INJECT_ERROR];
int64_t	xfs_etest_fsid[XFS_NUM_INJECT_ERROR];
char *	xfs_etest_fsname[XFS_NUM_INJECT_ERROR];

void
xfs_error_test_init(void)
{
	memset(xfs_etest, 0, sizeof(xfs_etest));
	memset(xfs_etest_fsid, 0, sizeof(xfs_etest_fsid));
	memset(xfs_etest_fsname, 0, sizeof(xfs_etest_fsname));
}

int
xfs_error_test(int error_tag, int *fsidp, char *expression,
	       int line, char *file, unsigned long randfactor)
{
	int i;
	int64_t fsid;

	if (random() % randfactor)
		return 0;

	memcpy(&fsid, fsidp, sizeof(xfs_fsid_t));

	for (i = 0; i < XFS_NUM_INJECT_ERROR; i++)  {
		if (xfs_etest[i] == error_tag && xfs_etest_fsid[i] == fsid) {
			cmn_err(CE_WARN,
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
			cmn_err(CE_WARN, "XFS error tag #%d on", error_tag);
			return 0;
		}
	}

	for (i = 0; i < XFS_NUM_INJECT_ERROR; i++)  {
		if (xfs_etest[i] == 0) {
			cmn_err(CE_WARN, "Turned on XFS error tag #%d",
				error_tag);
			xfs_etest[i] = error_tag;
			xfs_etest_fsid[i] = fsid;
			len = strlen(mp->m_fsname);
			xfs_etest_fsname[i] = kmem_alloc(len + 1, KM_SLEEP);
			strcpy(xfs_etest_fsname[i], mp->m_fsname);
			return 0;
		}
	}

	cmn_err(CE_WARN, "error tag overflow, too many turned on");

	return 1;
}

int
xfs_errortag_clear(int error_tag, xfs_mount_t *mp)
{
	int i;
	int64_t fsid;

	memcpy(&fsid, mp->m_fixedfsid, sizeof(xfs_fsid_t));

	for (i = 0; i < XFS_NUM_INJECT_ERROR; i++) {
		if (xfs_etest_fsid[i] == fsid && xfs_etest[i] == error_tag) {
			xfs_etest[i] = 0;
			xfs_etest_fsid[i] = 0LL;
			kmem_free(xfs_etest_fsname[i],
				  strlen(xfs_etest_fsname[i]) + 1);
			xfs_etest_fsname[i] = NULL;
			cmn_err(CE_WARN, "Cleared XFS error tag #%d",
				error_tag);
			return 0;
		}
	}

	cmn_err(CE_WARN, "XFS error tag %d not on", error_tag);

	return 1;
}

int
xfs_errortag_clearall_umount(int64_t fsid, char *fsname, int loud)
{
	int i;
	int cleared = 0;

	for (i = 0; i < XFS_NUM_INJECT_ERROR; i++) {
		if ((fsid == 0LL || xfs_etest_fsid[i] == fsid) &&
		     xfs_etest[i] != 0) {
			cleared = 1;
			cmn_err(CE_WARN, "Clearing XFS error tag #%d",
				xfs_etest[i]);
			xfs_etest[i] = 0;
			xfs_etest_fsid[i] = 0LL;
			kmem_free(xfs_etest_fsname[i],
				  strlen(xfs_etest_fsname[i]) + 1);
			xfs_etest_fsname[i] = NULL;
		}
	}

	if (loud || cleared)
		cmn_err(CE_WARN,
			"Cleared all XFS error tags for filesystem \"%s\"",
			fsname);

	return 0;
}

int
xfs_errortag_clearall(xfs_mount_t *mp)
{
	int64_t fsid;

	memcpy(&fsid, mp->m_fixedfsid, sizeof(xfs_fsid_t));

	return xfs_errortag_clearall_umount(fsid, mp->m_fsname, 1);
}
#endif /* DEBUG || INDUCE_IO_ERROR */

static void
xfs_fs_vcmn_err(int level, xfs_mount_t *mp, char *fmt, va_list ap)
{
	if (mp != NULL) {
		char	*newfmt;
		int	len = 16 + mp->m_fsname_len + strlen(fmt);

		newfmt = kmem_alloc(len, KM_SLEEP);
		sprintf(newfmt, "Filesystem \"%s\": %s", mp->m_fsname, fmt);
		icmn_err(level, newfmt, ap);
		kmem_free(newfmt, len);
	} else {
		icmn_err(level, fmt, ap);
	}
}

void
xfs_fs_cmn_err(int level, xfs_mount_t *mp, char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	xfs_fs_vcmn_err(level, mp, fmt, ap);
	va_end(ap);
}

void
xfs_cmn_err(int panic_tag, int level, xfs_mount_t *mp, char *fmt, ...)
{
	va_list ap;

#ifdef DEBUG
	xfs_panic_mask |= XFS_PTAG_SHUTDOWN_CORRUPT;
#endif

	if (xfs_panic_mask && (xfs_panic_mask & panic_tag)
	    && (level & CE_ALERT)) {
		level &= ~CE_ALERT;
		level |= CE_PANIC;
		cmn_err(CE_ALERT, "XFS: Transforming an alert into a BUG.");
	}
	va_start(ap, fmt);
	xfs_fs_vcmn_err(level, mp, fmt, ap);
	va_end(ap);
}

void
xfs_error_report(
	char		*tag,
	int		level,
	xfs_mount_t	*mp,
	char		*fname,
	int		linenum,
	inst_t		*ra)
{
	if (level <= xfs_error_level) {
		xfs_cmn_err(XFS_PTAG_ERROR_REPORT,
			    CE_ALERT, mp,
		"XFS internal error %s at line %d of file %s.  Caller 0x%p\n",
			    tag, linenum, fname, ra);

		xfs_stack_trace();
	}
}

void
xfs_hex_dump(void *p, int length)
{
	__uint8_t *uip = (__uint8_t*)p;
	int	i;
	char	sbuf[128], *s;

	s = sbuf;
	*s = '\0';
	for (i=0; i<length; i++, uip++) {
		if ((i % 16) == 0) {
			if (*s != '\0')
				cmn_err(CE_ALERT, "%s\n", sbuf);
			s = sbuf;
			sprintf(s, "0x%x: ", i);
			while( *s != '\0')
				s++;
		}
		sprintf(s, "%02x ", *uip);

		/*
		 * the kernel sprintf is a void; user sprintf returns
		 * the sprintf'ed string's length.  Find the new end-
		 * of-string
		 */
		while( *s != '\0')
			s++;
	}
	cmn_err(CE_ALERT, "%s\n", sbuf);
}

void
xfs_corruption_error(
	char		*tag,
	int		level,
	xfs_mount_t	*mp,
	void		*p,
	char		*fname,
	int		linenum,
	inst_t		*ra)
{
	if (level <= xfs_error_level)
		xfs_hex_dump(p, 16);
	xfs_error_report(tag, level, mp, fname, linenum, ra);
}
