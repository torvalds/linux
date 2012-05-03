/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
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
#include "xfs_bit.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir2.h"
#include "xfs_alloc.h"
#include "xfs_quota.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_bmap.h"
#include "xfs_rtalloc.h"
#include "xfs_error.h"
#include "xfs_itable.h"
#include "xfs_fsops.h"
#include "xfs_attr.h"
#include "xfs_buf_item.h"
#include "xfs_utils.h"
#include "xfs_vnodeops.h"
#include "xfs_log_priv.h"
#include "xfs_trans_priv.h"
#include "xfs_filestream.h"
#include "xfs_da_btree.h"
#include "xfs_extfree_item.h"
#include "xfs_mru_cache.h"
#include "xfs_inode_item.h"
#include "xfs_sync.h"
#include "xfs_trace.h"

#include <linux/namei.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/mount.h>
#include <linux/mempool.h>
#include <linux/writeback.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/parser.h>

static const struct super_operations xfs_super_operations;
static kmem_zone_t *xfs_ioend_zone;
mempool_t *xfs_ioend_pool;

#define MNTOPT_LOGBUFS	"logbufs"	/* number of XFS log buffers */
#define MNTOPT_LOGBSIZE	"logbsize"	/* size of XFS log buffers */
#define MNTOPT_LOGDEV	"logdev"	/* log device */
#define MNTOPT_RTDEV	"rtdev"		/* realtime I/O device */
#define MNTOPT_BIOSIZE	"biosize"	/* log2 of preferred buffered io size */
#define MNTOPT_WSYNC	"wsync"		/* safe-mode nfs compatible mount */
#define MNTOPT_NOALIGN	"noalign"	/* turn off stripe alignment */
#define MNTOPT_SWALLOC	"swalloc"	/* turn on stripe width allocation */
#define MNTOPT_SUNIT	"sunit"		/* data volume stripe unit */
#define MNTOPT_SWIDTH	"swidth"	/* data volume stripe width */
#define MNTOPT_NOUUID	"nouuid"	/* ignore filesystem UUID */
#define MNTOPT_MTPT	"mtpt"		/* filesystem mount point */
#define MNTOPT_GRPID	"grpid"		/* group-ID from parent directory */
#define MNTOPT_NOGRPID	"nogrpid"	/* group-ID from current process */
#define MNTOPT_BSDGROUPS    "bsdgroups"    /* group-ID from parent directory */
#define MNTOPT_SYSVGROUPS   "sysvgroups"   /* group-ID from current process */
#define MNTOPT_ALLOCSIZE    "allocsize"    /* preferred allocation size */
#define MNTOPT_NORECOVERY   "norecovery"   /* don't run XFS recovery */
#define MNTOPT_BARRIER	"barrier"	/* use writer barriers for log write and
					 * unwritten extent conversion */
#define MNTOPT_NOBARRIER "nobarrier"	/* .. disable */
#define MNTOPT_64BITINODE   "inode64"	/* inodes can be allocated anywhere */
#define MNTOPT_IKEEP	"ikeep"		/* do not free empty inode clusters */
#define MNTOPT_NOIKEEP	"noikeep"	/* free empty inode clusters */
#define MNTOPT_LARGEIO	   "largeio"	/* report large I/O sizes in stat() */
#define MNTOPT_NOLARGEIO   "nolargeio"	/* do not report large I/O sizes
					 * in stat(). */
#define MNTOPT_ATTR2	"attr2"		/* do use attr2 attribute format */
#define MNTOPT_NOATTR2	"noattr2"	/* do not use attr2 attribute format */
#define MNTOPT_FILESTREAM  "filestreams" /* use filestreams allocator */
#define MNTOPT_QUOTA	"quota"		/* disk quotas (user) */
#define MNTOPT_NOQUOTA	"noquota"	/* no quotas */
#define MNTOPT_USRQUOTA	"usrquota"	/* user quota enabled */
#define MNTOPT_GRPQUOTA	"grpquota"	/* group quota enabled */
#define MNTOPT_PRJQUOTA	"prjquota"	/* project quota enabled */
#define MNTOPT_UQUOTA	"uquota"	/* user quota (IRIX variant) */
#define MNTOPT_GQUOTA	"gquota"	/* group quota (IRIX variant) */
#define MNTOPT_PQUOTA	"pquota"	/* project quota (IRIX variant) */
#define MNTOPT_UQUOTANOENF "uqnoenforce"/* user quota limit enforcement */
#define MNTOPT_GQUOTANOENF "gqnoenforce"/* group quota limit enforcement */
#define MNTOPT_PQUOTANOENF "pqnoenforce"/* project quota limit enforcement */
#define MNTOPT_QUOTANOENF  "qnoenforce"	/* same as uqnoenforce */
#define MNTOPT_DELAYLOG    "delaylog"	/* Delayed logging enabled */
#define MNTOPT_NODELAYLOG  "nodelaylog"	/* Delayed logging disabled */
#define MNTOPT_DISCARD	   "discard"	/* Discard unused blocks */
#define MNTOPT_NODISCARD   "nodiscard"	/* Do not discard unused blocks */

/*
 * Table driven mount option parser.
 *
 * Currently only used for remount, but it will be used for mount
 * in the future, too.
 */
enum {
	Opt_barrier, Opt_nobarrier, Opt_err
};

static const match_table_t tokens = {
	{Opt_barrier, "barrier"},
	{Opt_nobarrier, "nobarrier"},
	{Opt_err, NULL}
};


STATIC unsigned long
suffix_strtoul(char *s, char **endp, unsigned int base)
{
	int	last, shift_left_factor = 0;
	char	*value = s;

	last = strlen(value) - 1;
	if (value[last] == 'K' || value[last] == 'k') {
		shift_left_factor = 10;
		value[last] = '\0';
	}
	if (value[last] == 'M' || value[last] == 'm') {
		shift_left_factor = 20;
		value[last] = '\0';
	}
	if (value[last] == 'G' || value[last] == 'g') {
		shift_left_factor = 30;
		value[last] = '\0';
	}

	return simple_strtoul((const char *)s, endp, base) << shift_left_factor;
}

/*
 * This function fills in xfs_mount_t fields based on mount args.
 * Note: the superblock has _not_ yet been read in.
 *
 * Note that this function leaks the various device name allocations on
 * failure.  The caller takes care of them.
 */
STATIC int
xfs_parseargs(
	struct xfs_mount	*mp,
	char			*options)
{
	struct super_block	*sb = mp->m_super;
	char			*this_char, *value, *eov;
	int			dsunit = 0;
	int			dswidth = 0;
	int			iosize = 0;
	__uint8_t		iosizelog = 0;

	/*
	 * set up the mount name first so all the errors will refer to the
	 * correct device.
	 */
	mp->m_fsname = kstrndup(sb->s_id, MAXNAMELEN, GFP_KERNEL);
	if (!mp->m_fsname)
		return ENOMEM;
	mp->m_fsname_len = strlen(mp->m_fsname) + 1;

	/*
	 * Copy binary VFS mount flags we are interested in.
	 */
	if (sb->s_flags & MS_RDONLY)
		mp->m_flags |= XFS_MOUNT_RDONLY;
	if (sb->s_flags & MS_DIRSYNC)
		mp->m_flags |= XFS_MOUNT_DIRSYNC;
	if (sb->s_flags & MS_SYNCHRONOUS)
		mp->m_flags |= XFS_MOUNT_WSYNC;

	/*
	 * Set some default flags that could be cleared by the mount option
	 * parsing.
	 */
	mp->m_flags |= XFS_MOUNT_BARRIER;
	mp->m_flags |= XFS_MOUNT_COMPAT_IOSIZE;
	mp->m_flags |= XFS_MOUNT_SMALL_INUMS;

	/*
	 * These can be overridden by the mount option parsing.
	 */
	mp->m_logbufs = -1;
	mp->m_logbsize = -1;

	if (!options)
		goto done;

	while ((this_char = strsep(&options, ",")) != NULL) {
		if (!*this_char)
			continue;
		if ((value = strchr(this_char, '=')) != NULL)
			*value++ = 0;

		if (!strcmp(this_char, MNTOPT_LOGBUFS)) {
			if (!value || !*value) {
				xfs_warn(mp, "%s option requires an argument",
					this_char);
				return EINVAL;
			}
			mp->m_logbufs = simple_strtoul(value, &eov, 10);
		} else if (!strcmp(this_char, MNTOPT_LOGBSIZE)) {
			if (!value || !*value) {
				xfs_warn(mp, "%s option requires an argument",
					this_char);
				return EINVAL;
			}
			mp->m_logbsize = suffix_strtoul(value, &eov, 10);
		} else if (!strcmp(this_char, MNTOPT_LOGDEV)) {
			if (!value || !*value) {
				xfs_warn(mp, "%s option requires an argument",
					this_char);
				return EINVAL;
			}
			mp->m_logname = kstrndup(value, MAXNAMELEN, GFP_KERNEL);
			if (!mp->m_logname)
				return ENOMEM;
		} else if (!strcmp(this_char, MNTOPT_MTPT)) {
			xfs_warn(mp, "%s option not allowed on this system",
				this_char);
			return EINVAL;
		} else if (!strcmp(this_char, MNTOPT_RTDEV)) {
			if (!value || !*value) {
				xfs_warn(mp, "%s option requires an argument",
					this_char);
				return EINVAL;
			}
			mp->m_rtname = kstrndup(value, MAXNAMELEN, GFP_KERNEL);
			if (!mp->m_rtname)
				return ENOMEM;
		} else if (!strcmp(this_char, MNTOPT_BIOSIZE)) {
			if (!value || !*value) {
				xfs_warn(mp, "%s option requires an argument",
					this_char);
				return EINVAL;
			}
			iosize = simple_strtoul(value, &eov, 10);
			iosizelog = ffs(iosize) - 1;
		} else if (!strcmp(this_char, MNTOPT_ALLOCSIZE)) {
			if (!value || !*value) {
				xfs_warn(mp, "%s option requires an argument",
					this_char);
				return EINVAL;
			}
			iosize = suffix_strtoul(value, &eov, 10);
			iosizelog = ffs(iosize) - 1;
		} else if (!strcmp(this_char, MNTOPT_GRPID) ||
			   !strcmp(this_char, MNTOPT_BSDGROUPS)) {
			mp->m_flags |= XFS_MOUNT_GRPID;
		} else if (!strcmp(this_char, MNTOPT_NOGRPID) ||
			   !strcmp(this_char, MNTOPT_SYSVGROUPS)) {
			mp->m_flags &= ~XFS_MOUNT_GRPID;
		} else if (!strcmp(this_char, MNTOPT_WSYNC)) {
			mp->m_flags |= XFS_MOUNT_WSYNC;
		} else if (!strcmp(this_char, MNTOPT_NORECOVERY)) {
			mp->m_flags |= XFS_MOUNT_NORECOVERY;
		} else if (!strcmp(this_char, MNTOPT_NOALIGN)) {
			mp->m_flags |= XFS_MOUNT_NOALIGN;
		} else if (!strcmp(this_char, MNTOPT_SWALLOC)) {
			mp->m_flags |= XFS_MOUNT_SWALLOC;
		} else if (!strcmp(this_char, MNTOPT_SUNIT)) {
			if (!value || !*value) {
				xfs_warn(mp, "%s option requires an argument",
					this_char);
				return EINVAL;
			}
			dsunit = simple_strtoul(value, &eov, 10);
		} else if (!strcmp(this_char, MNTOPT_SWIDTH)) {
			if (!value || !*value) {
				xfs_warn(mp, "%s option requires an argument",
					this_char);
				return EINVAL;
			}
			dswidth = simple_strtoul(value, &eov, 10);
		} else if (!strcmp(this_char, MNTOPT_64BITINODE)) {
			mp->m_flags &= ~XFS_MOUNT_SMALL_INUMS;
#if !XFS_BIG_INUMS
			xfs_warn(mp, "%s option not allowed on this system",
				this_char);
			return EINVAL;
#endif
		} else if (!strcmp(this_char, MNTOPT_NOUUID)) {
			mp->m_flags |= XFS_MOUNT_NOUUID;
		} else if (!strcmp(this_char, MNTOPT_BARRIER)) {
			mp->m_flags |= XFS_MOUNT_BARRIER;
		} else if (!strcmp(this_char, MNTOPT_NOBARRIER)) {
			mp->m_flags &= ~XFS_MOUNT_BARRIER;
		} else if (!strcmp(this_char, MNTOPT_IKEEP)) {
			mp->m_flags |= XFS_MOUNT_IKEEP;
		} else if (!strcmp(this_char, MNTOPT_NOIKEEP)) {
			mp->m_flags &= ~XFS_MOUNT_IKEEP;
		} else if (!strcmp(this_char, MNTOPT_LARGEIO)) {
			mp->m_flags &= ~XFS_MOUNT_COMPAT_IOSIZE;
		} else if (!strcmp(this_char, MNTOPT_NOLARGEIO)) {
			mp->m_flags |= XFS_MOUNT_COMPAT_IOSIZE;
		} else if (!strcmp(this_char, MNTOPT_ATTR2)) {
			mp->m_flags |= XFS_MOUNT_ATTR2;
		} else if (!strcmp(this_char, MNTOPT_NOATTR2)) {
			mp->m_flags &= ~XFS_MOUNT_ATTR2;
			mp->m_flags |= XFS_MOUNT_NOATTR2;
		} else if (!strcmp(this_char, MNTOPT_FILESTREAM)) {
			mp->m_flags |= XFS_MOUNT_FILESTREAMS;
		} else if (!strcmp(this_char, MNTOPT_NOQUOTA)) {
			mp->m_qflags &= ~XFS_ALL_QUOTA_ACCT;
			mp->m_qflags &= ~XFS_ALL_QUOTA_ENFD;
			mp->m_qflags &= ~XFS_ALL_QUOTA_ACTIVE;
		} else if (!strcmp(this_char, MNTOPT_QUOTA) ||
			   !strcmp(this_char, MNTOPT_UQUOTA) ||
			   !strcmp(this_char, MNTOPT_USRQUOTA)) {
			mp->m_qflags |= (XFS_UQUOTA_ACCT | XFS_UQUOTA_ACTIVE |
					 XFS_UQUOTA_ENFD);
		} else if (!strcmp(this_char, MNTOPT_QUOTANOENF) ||
			   !strcmp(this_char, MNTOPT_UQUOTANOENF)) {
			mp->m_qflags |= (XFS_UQUOTA_ACCT | XFS_UQUOTA_ACTIVE);
			mp->m_qflags &= ~XFS_UQUOTA_ENFD;
		} else if (!strcmp(this_char, MNTOPT_PQUOTA) ||
			   !strcmp(this_char, MNTOPT_PRJQUOTA)) {
			mp->m_qflags |= (XFS_PQUOTA_ACCT | XFS_PQUOTA_ACTIVE |
					 XFS_OQUOTA_ENFD);
		} else if (!strcmp(this_char, MNTOPT_PQUOTANOENF)) {
			mp->m_qflags |= (XFS_PQUOTA_ACCT | XFS_PQUOTA_ACTIVE);
			mp->m_qflags &= ~XFS_OQUOTA_ENFD;
		} else if (!strcmp(this_char, MNTOPT_GQUOTA) ||
			   !strcmp(this_char, MNTOPT_GRPQUOTA)) {
			mp->m_qflags |= (XFS_GQUOTA_ACCT | XFS_GQUOTA_ACTIVE |
					 XFS_OQUOTA_ENFD);
		} else if (!strcmp(this_char, MNTOPT_GQUOTANOENF)) {
			mp->m_qflags |= (XFS_GQUOTA_ACCT | XFS_GQUOTA_ACTIVE);
			mp->m_qflags &= ~XFS_OQUOTA_ENFD;
		} else if (!strcmp(this_char, MNTOPT_DELAYLOG)) {
			xfs_warn(mp,
	"delaylog is the default now, option is deprecated.");
		} else if (!strcmp(this_char, MNTOPT_NODELAYLOG)) {
			xfs_warn(mp,
	"nodelaylog support has been removed, option is deprecated.");
		} else if (!strcmp(this_char, MNTOPT_DISCARD)) {
			mp->m_flags |= XFS_MOUNT_DISCARD;
		} else if (!strcmp(this_char, MNTOPT_NODISCARD)) {
			mp->m_flags &= ~XFS_MOUNT_DISCARD;
		} else if (!strcmp(this_char, "ihashsize")) {
			xfs_warn(mp,
	"ihashsize no longer used, option is deprecated.");
		} else if (!strcmp(this_char, "osyncisdsync")) {
			xfs_warn(mp,
	"osyncisdsync has no effect, option is deprecated.");
		} else if (!strcmp(this_char, "osyncisosync")) {
			xfs_warn(mp,
	"osyncisosync has no effect, option is deprecated.");
		} else if (!strcmp(this_char, "irixsgid")) {
			xfs_warn(mp,
	"irixsgid is now a sysctl(2) variable, option is deprecated.");
		} else {
			xfs_warn(mp, "unknown mount option [%s].", this_char);
			return EINVAL;
		}
	}

	/*
	 * no recovery flag requires a read-only mount
	 */
	if ((mp->m_flags & XFS_MOUNT_NORECOVERY) &&
	    !(mp->m_flags & XFS_MOUNT_RDONLY)) {
		xfs_warn(mp, "no-recovery mounts must be read-only.");
		return EINVAL;
	}

	if ((mp->m_flags & XFS_MOUNT_NOALIGN) && (dsunit || dswidth)) {
		xfs_warn(mp,
	"sunit and swidth options incompatible with the noalign option");
		return EINVAL;
	}

#ifndef CONFIG_XFS_QUOTA
	if (XFS_IS_QUOTA_RUNNING(mp)) {
		xfs_warn(mp, "quota support not available in this kernel.");
		return EINVAL;
	}
#endif

	if ((mp->m_qflags & (XFS_GQUOTA_ACCT | XFS_GQUOTA_ACTIVE)) &&
	    (mp->m_qflags & (XFS_PQUOTA_ACCT | XFS_PQUOTA_ACTIVE))) {
		xfs_warn(mp, "cannot mount with both project and group quota");
		return EINVAL;
	}

	if ((dsunit && !dswidth) || (!dsunit && dswidth)) {
		xfs_warn(mp, "sunit and swidth must be specified together");
		return EINVAL;
	}

	if (dsunit && (dswidth % dsunit != 0)) {
		xfs_warn(mp,
	"stripe width (%d) must be a multiple of the stripe unit (%d)",
			dswidth, dsunit);
		return EINVAL;
	}

done:
	if (!(mp->m_flags & XFS_MOUNT_NOALIGN)) {
		/*
		 * At this point the superblock has not been read
		 * in, therefore we do not know the block size.
		 * Before the mount call ends we will convert
		 * these to FSBs.
		 */
		if (dsunit) {
			mp->m_dalign = dsunit;
			mp->m_flags |= XFS_MOUNT_RETERR;
		}

		if (dswidth)
			mp->m_swidth = dswidth;
	}

	if (mp->m_logbufs != -1 &&
	    mp->m_logbufs != 0 &&
	    (mp->m_logbufs < XLOG_MIN_ICLOGS ||
	     mp->m_logbufs > XLOG_MAX_ICLOGS)) {
		xfs_warn(mp, "invalid logbufs value: %d [not %d-%d]",
			mp->m_logbufs, XLOG_MIN_ICLOGS, XLOG_MAX_ICLOGS);
		return XFS_ERROR(EINVAL);
	}
	if (mp->m_logbsize != -1 &&
	    mp->m_logbsize !=  0 &&
	    (mp->m_logbsize < XLOG_MIN_RECORD_BSIZE ||
	     mp->m_logbsize > XLOG_MAX_RECORD_BSIZE ||
	     !is_power_of_2(mp->m_logbsize))) {
		xfs_warn(mp,
			"invalid logbufsize: %d [not 16k,32k,64k,128k or 256k]",
			mp->m_logbsize);
		return XFS_ERROR(EINVAL);
	}

	if (iosizelog) {
		if (iosizelog > XFS_MAX_IO_LOG ||
		    iosizelog < XFS_MIN_IO_LOG) {
			xfs_warn(mp, "invalid log iosize: %d [not %d-%d]",
				iosizelog, XFS_MIN_IO_LOG,
				XFS_MAX_IO_LOG);
			return XFS_ERROR(EINVAL);
		}

		mp->m_flags |= XFS_MOUNT_DFLT_IOSIZE;
		mp->m_readio_log = iosizelog;
		mp->m_writeio_log = iosizelog;
	}

	return 0;
}

struct proc_xfs_info {
	int	flag;
	char	*str;
};

STATIC int
xfs_showargs(
	struct xfs_mount	*mp,
	struct seq_file		*m)
{
	static struct proc_xfs_info xfs_info_set[] = {
		/* the few simple ones we can get from the mount struct */
		{ XFS_MOUNT_IKEEP,		"," MNTOPT_IKEEP },
		{ XFS_MOUNT_WSYNC,		"," MNTOPT_WSYNC },
		{ XFS_MOUNT_NOALIGN,		"," MNTOPT_NOALIGN },
		{ XFS_MOUNT_SWALLOC,		"," MNTOPT_SWALLOC },
		{ XFS_MOUNT_NOUUID,		"," MNTOPT_NOUUID },
		{ XFS_MOUNT_NORECOVERY,		"," MNTOPT_NORECOVERY },
		{ XFS_MOUNT_ATTR2,		"," MNTOPT_ATTR2 },
		{ XFS_MOUNT_FILESTREAMS,	"," MNTOPT_FILESTREAM },
		{ XFS_MOUNT_GRPID,		"," MNTOPT_GRPID },
		{ XFS_MOUNT_DISCARD,		"," MNTOPT_DISCARD },
		{ 0, NULL }
	};
	static struct proc_xfs_info xfs_info_unset[] = {
		/* the few simple ones we can get from the mount struct */
		{ XFS_MOUNT_COMPAT_IOSIZE,	"," MNTOPT_LARGEIO },
		{ XFS_MOUNT_BARRIER,		"," MNTOPT_NOBARRIER },
		{ XFS_MOUNT_SMALL_INUMS,	"," MNTOPT_64BITINODE },
		{ 0, NULL }
	};
	struct proc_xfs_info	*xfs_infop;

	for (xfs_infop = xfs_info_set; xfs_infop->flag; xfs_infop++) {
		if (mp->m_flags & xfs_infop->flag)
			seq_puts(m, xfs_infop->str);
	}
	for (xfs_infop = xfs_info_unset; xfs_infop->flag; xfs_infop++) {
		if (!(mp->m_flags & xfs_infop->flag))
			seq_puts(m, xfs_infop->str);
	}

	if (mp->m_flags & XFS_MOUNT_DFLT_IOSIZE)
		seq_printf(m, "," MNTOPT_ALLOCSIZE "=%dk",
				(int)(1 << mp->m_writeio_log) >> 10);

	if (mp->m_logbufs > 0)
		seq_printf(m, "," MNTOPT_LOGBUFS "=%d", mp->m_logbufs);
	if (mp->m_logbsize > 0)
		seq_printf(m, "," MNTOPT_LOGBSIZE "=%dk", mp->m_logbsize >> 10);

	if (mp->m_logname)
		seq_printf(m, "," MNTOPT_LOGDEV "=%s", mp->m_logname);
	if (mp->m_rtname)
		seq_printf(m, "," MNTOPT_RTDEV "=%s", mp->m_rtname);

	if (mp->m_dalign > 0)
		seq_printf(m, "," MNTOPT_SUNIT "=%d",
				(int)XFS_FSB_TO_BB(mp, mp->m_dalign));
	if (mp->m_swidth > 0)
		seq_printf(m, "," MNTOPT_SWIDTH "=%d",
				(int)XFS_FSB_TO_BB(mp, mp->m_swidth));

	if (mp->m_qflags & (XFS_UQUOTA_ACCT|XFS_UQUOTA_ENFD))
		seq_puts(m, "," MNTOPT_USRQUOTA);
	else if (mp->m_qflags & XFS_UQUOTA_ACCT)
		seq_puts(m, "," MNTOPT_UQUOTANOENF);

	/* Either project or group quotas can be active, not both */

	if (mp->m_qflags & XFS_PQUOTA_ACCT) {
		if (mp->m_qflags & XFS_OQUOTA_ENFD)
			seq_puts(m, "," MNTOPT_PRJQUOTA);
		else
			seq_puts(m, "," MNTOPT_PQUOTANOENF);
	} else if (mp->m_qflags & XFS_GQUOTA_ACCT) {
		if (mp->m_qflags & XFS_OQUOTA_ENFD)
			seq_puts(m, "," MNTOPT_GRPQUOTA);
		else
			seq_puts(m, "," MNTOPT_GQUOTANOENF);
	}

	if (!(mp->m_qflags & XFS_ALL_QUOTA_ACCT))
		seq_puts(m, "," MNTOPT_NOQUOTA);

	return 0;
}
__uint64_t
xfs_max_file_offset(
	unsigned int		blockshift)
{
	unsigned int		pagefactor = 1;
	unsigned int		bitshift = BITS_PER_LONG - 1;

	/* Figure out maximum filesize, on Linux this can depend on
	 * the filesystem blocksize (on 32 bit platforms).
	 * __block_write_begin does this in an [unsigned] long...
	 *      page->index << (PAGE_CACHE_SHIFT - bbits)
	 * So, for page sized blocks (4K on 32 bit platforms),
	 * this wraps at around 8Tb (hence MAX_LFS_FILESIZE which is
	 *      (((u64)PAGE_CACHE_SIZE << (BITS_PER_LONG-1))-1)
	 * but for smaller blocksizes it is less (bbits = log2 bsize).
	 * Note1: get_block_t takes a long (implicit cast from above)
	 * Note2: The Large Block Device (LBD and HAVE_SECTOR_T) patch
	 * can optionally convert the [unsigned] long from above into
	 * an [unsigned] long long.
	 */

#if BITS_PER_LONG == 32
# if defined(CONFIG_LBDAF)
	ASSERT(sizeof(sector_t) == 8);
	pagefactor = PAGE_CACHE_SIZE;
	bitshift = BITS_PER_LONG;
# else
	pagefactor = PAGE_CACHE_SIZE >> (PAGE_CACHE_SHIFT - blockshift);
# endif
#endif

	return (((__uint64_t)pagefactor) << bitshift) - 1;
}

STATIC int
xfs_blkdev_get(
	xfs_mount_t		*mp,
	const char		*name,
	struct block_device	**bdevp)
{
	int			error = 0;

	*bdevp = blkdev_get_by_path(name, FMODE_READ|FMODE_WRITE|FMODE_EXCL,
				    mp);
	if (IS_ERR(*bdevp)) {
		error = PTR_ERR(*bdevp);
		xfs_warn(mp, "Invalid device [%s], error=%d\n", name, error);
	}

	return -error;
}

STATIC void
xfs_blkdev_put(
	struct block_device	*bdev)
{
	if (bdev)
		blkdev_put(bdev, FMODE_READ|FMODE_WRITE|FMODE_EXCL);
}

void
xfs_blkdev_issue_flush(
	xfs_buftarg_t		*buftarg)
{
	blkdev_issue_flush(buftarg->bt_bdev, GFP_KERNEL, NULL);
}

STATIC void
xfs_close_devices(
	struct xfs_mount	*mp)
{
	if (mp->m_logdev_targp && mp->m_logdev_targp != mp->m_ddev_targp) {
		struct block_device *logdev = mp->m_logdev_targp->bt_bdev;
		xfs_free_buftarg(mp, mp->m_logdev_targp);
		xfs_blkdev_put(logdev);
	}
	if (mp->m_rtdev_targp) {
		struct block_device *rtdev = mp->m_rtdev_targp->bt_bdev;
		xfs_free_buftarg(mp, mp->m_rtdev_targp);
		xfs_blkdev_put(rtdev);
	}
	xfs_free_buftarg(mp, mp->m_ddev_targp);
}

/*
 * The file system configurations are:
 *	(1) device (partition) with data and internal log
 *	(2) logical volume with data and log subvolumes.
 *	(3) logical volume with data, log, and realtime subvolumes.
 *
 * We only have to handle opening the log and realtime volumes here if
 * they are present.  The data subvolume has already been opened by
 * get_sb_bdev() and is stored in sb->s_bdev.
 */
STATIC int
xfs_open_devices(
	struct xfs_mount	*mp)
{
	struct block_device	*ddev = mp->m_super->s_bdev;
	struct block_device	*logdev = NULL, *rtdev = NULL;
	int			error;

	/*
	 * Open real time and log devices - order is important.
	 */
	if (mp->m_logname) {
		error = xfs_blkdev_get(mp, mp->m_logname, &logdev);
		if (error)
			goto out;
	}

	if (mp->m_rtname) {
		error = xfs_blkdev_get(mp, mp->m_rtname, &rtdev);
		if (error)
			goto out_close_logdev;

		if (rtdev == ddev || rtdev == logdev) {
			xfs_warn(mp,
	"Cannot mount filesystem with identical rtdev and ddev/logdev.");
			error = EINVAL;
			goto out_close_rtdev;
		}
	}

	/*
	 * Setup xfs_mount buffer target pointers
	 */
	error = ENOMEM;
	mp->m_ddev_targp = xfs_alloc_buftarg(mp, ddev, 0, mp->m_fsname);
	if (!mp->m_ddev_targp)
		goto out_close_rtdev;

	if (rtdev) {
		mp->m_rtdev_targp = xfs_alloc_buftarg(mp, rtdev, 1,
							mp->m_fsname);
		if (!mp->m_rtdev_targp)
			goto out_free_ddev_targ;
	}

	if (logdev && logdev != ddev) {
		mp->m_logdev_targp = xfs_alloc_buftarg(mp, logdev, 1,
							mp->m_fsname);
		if (!mp->m_logdev_targp)
			goto out_free_rtdev_targ;
	} else {
		mp->m_logdev_targp = mp->m_ddev_targp;
	}

	return 0;

 out_free_rtdev_targ:
	if (mp->m_rtdev_targp)
		xfs_free_buftarg(mp, mp->m_rtdev_targp);
 out_free_ddev_targ:
	xfs_free_buftarg(mp, mp->m_ddev_targp);
 out_close_rtdev:
	if (rtdev)
		xfs_blkdev_put(rtdev);
 out_close_logdev:
	if (logdev && logdev != ddev)
		xfs_blkdev_put(logdev);
 out:
	return error;
}

/*
 * Setup xfs_mount buffer target pointers based on superblock
 */
STATIC int
xfs_setup_devices(
	struct xfs_mount	*mp)
{
	int			error;

	error = xfs_setsize_buftarg(mp->m_ddev_targp, mp->m_sb.sb_blocksize,
				    mp->m_sb.sb_sectsize);
	if (error)
		return error;

	if (mp->m_logdev_targp && mp->m_logdev_targp != mp->m_ddev_targp) {
		unsigned int	log_sector_size = BBSIZE;

		if (xfs_sb_version_hassector(&mp->m_sb))
			log_sector_size = mp->m_sb.sb_logsectsize;
		error = xfs_setsize_buftarg(mp->m_logdev_targp,
					    mp->m_sb.sb_blocksize,
					    log_sector_size);
		if (error)
			return error;
	}
	if (mp->m_rtdev_targp) {
		error = xfs_setsize_buftarg(mp->m_rtdev_targp,
					    mp->m_sb.sb_blocksize,
					    mp->m_sb.sb_sectsize);
		if (error)
			return error;
	}

	return 0;
}

STATIC int
xfs_init_mount_workqueues(
	struct xfs_mount	*mp)
{
	mp->m_data_workqueue = alloc_workqueue("xfs-data/%s",
			WQ_MEM_RECLAIM, 0, mp->m_fsname);
	if (!mp->m_data_workqueue)
		goto out;

	mp->m_unwritten_workqueue = alloc_workqueue("xfs-conv/%s",
			WQ_MEM_RECLAIM, 0, mp->m_fsname);
	if (!mp->m_unwritten_workqueue)
		goto out_destroy_data_iodone_queue;

	return 0;

out_destroy_data_iodone_queue:
	destroy_workqueue(mp->m_data_workqueue);
out:
	return -ENOMEM;
}

STATIC void
xfs_destroy_mount_workqueues(
	struct xfs_mount	*mp)
{
	destroy_workqueue(mp->m_data_workqueue);
	destroy_workqueue(mp->m_unwritten_workqueue);
}

/* Catch misguided souls that try to use this interface on XFS */
STATIC struct inode *
xfs_fs_alloc_inode(
	struct super_block	*sb)
{
	BUG();
	return NULL;
}

/*
 * Now that the generic code is guaranteed not to be accessing
 * the linux inode, we can reclaim the inode.
 */
STATIC void
xfs_fs_destroy_inode(
	struct inode		*inode)
{
	struct xfs_inode	*ip = XFS_I(inode);

	trace_xfs_destroy_inode(ip);

	XFS_STATS_INC(vn_reclaim);

	/* bad inode, get out here ASAP */
	if (is_bad_inode(inode))
		goto out_reclaim;

	ASSERT(XFS_FORCED_SHUTDOWN(ip->i_mount) || ip->i_delayed_blks == 0);

	/*
	 * We should never get here with one of the reclaim flags already set.
	 */
	ASSERT_ALWAYS(!xfs_iflags_test(ip, XFS_IRECLAIMABLE));
	ASSERT_ALWAYS(!xfs_iflags_test(ip, XFS_IRECLAIM));

	/*
	 * We always use background reclaim here because even if the
	 * inode is clean, it still may be under IO and hence we have
	 * to take the flush lock. The background reclaim path handles
	 * this more efficiently than we can here, so simply let background
	 * reclaim tear down all inodes.
	 */
out_reclaim:
	xfs_inode_set_reclaim_tag(ip);
}

/*
 * Slab object creation initialisation for the XFS inode.
 * This covers only the idempotent fields in the XFS inode;
 * all other fields need to be initialised on allocation
 * from the slab. This avoids the need to repeatedly initialise
 * fields in the xfs inode that left in the initialise state
 * when freeing the inode.
 */
STATIC void
xfs_fs_inode_init_once(
	void			*inode)
{
	struct xfs_inode	*ip = inode;

	memset(ip, 0, sizeof(struct xfs_inode));

	/* vfs inode */
	inode_init_once(VFS_I(ip));

	/* xfs inode */
	atomic_set(&ip->i_pincount, 0);
	spin_lock_init(&ip->i_flags_lock);

	mrlock_init(&ip->i_lock, MRLOCK_ALLOW_EQUAL_PRI|MRLOCK_BARRIER,
		     "xfsino", ip->i_ino);
}

/*
 * This is called by the VFS when dirtying inode metadata.  This can happen
 * for a few reasons, but we only care about timestamp updates, given that
 * we handled the rest ourselves.  In theory no other calls should happen,
 * but for example generic_write_end() keeps dirtying the inode after
 * updating i_size.  Thus we check that the flags are exactly I_DIRTY_SYNC,
 * and skip this call otherwise.
 *
 * We'll hopefull get a different method just for updating timestamps soon,
 * at which point this hack can go away, and maybe we'll also get real
 * error handling here.
 */
STATIC void
xfs_fs_dirty_inode(
	struct inode		*inode,
	int			flags)
{
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_trans	*tp;
	int			error;

	if (flags != I_DIRTY_SYNC)
		return;

	trace_xfs_dirty_inode(ip);

	tp = xfs_trans_alloc(mp, XFS_TRANS_FSYNC_TS);
	error = xfs_trans_reserve(tp, 0, XFS_FSYNC_TS_LOG_RES(mp), 0, 0, 0);
	if (error) {
		xfs_trans_cancel(tp, 0);
		goto trouble;
	}
	xfs_ilock(ip, XFS_ILOCK_EXCL);
	/*
	 * Grab all the latest timestamps from the Linux inode.
	 */
	ip->i_d.di_atime.t_sec = (__int32_t)inode->i_atime.tv_sec;
	ip->i_d.di_atime.t_nsec = (__int32_t)inode->i_atime.tv_nsec;
	ip->i_d.di_ctime.t_sec = (__int32_t)inode->i_ctime.tv_sec;
	ip->i_d.di_ctime.t_nsec = (__int32_t)inode->i_ctime.tv_nsec;
	ip->i_d.di_mtime.t_sec = (__int32_t)inode->i_mtime.tv_sec;
	ip->i_d.di_mtime.t_nsec = (__int32_t)inode->i_mtime.tv_nsec;

	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
	xfs_trans_log_inode(tp, ip, XFS_ILOG_TIMESTAMP);
	error = xfs_trans_commit(tp, 0);
	if (error)
		goto trouble;
	return;

trouble:
	xfs_warn(mp, "failed to update timestamps for inode 0x%llx", ip->i_ino);
}

STATIC void
xfs_fs_evict_inode(
	struct inode		*inode)
{
	xfs_inode_t		*ip = XFS_I(inode);

	trace_xfs_evict_inode(ip);

	truncate_inode_pages(&inode->i_data, 0);
	clear_inode(inode);
	XFS_STATS_INC(vn_rele);
	XFS_STATS_INC(vn_remove);
	XFS_STATS_DEC(vn_active);

	/*
	 * The iolock is used by the file system to coordinate reads,
	 * writes, and block truncates.  Up to this point the lock
	 * protected concurrent accesses by users of the inode.  But
	 * from here forward we're doing some final processing of the
	 * inode because we're done with it, and although we reuse the
	 * iolock for protection it is really a distinct lock class
	 * (in the lockdep sense) from before.  To keep lockdep happy
	 * (and basically indicate what we are doing), we explicitly
	 * re-init the iolock here.
	 */
	ASSERT(!rwsem_is_locked(&ip->i_iolock.mr_lock));
	mrlock_init(&ip->i_iolock, MRLOCK_BARRIER, "xfsio", ip->i_ino);
	lockdep_set_class_and_name(&ip->i_iolock.mr_lock,
			&xfs_iolock_reclaimable, "xfs_iolock_reclaimable");

	xfs_inactive(ip);
}

/*
 * We do an unlocked check for XFS_IDONTCACHE here because we are already
 * serialised against cache hits here via the inode->i_lock and igrab() in
 * xfs_iget_cache_hit(). Hence a lookup that might clear this flag will not be
 * racing with us, and it avoids needing to grab a spinlock here for every inode
 * we drop the final reference on.
 */
STATIC int
xfs_fs_drop_inode(
	struct inode		*inode)
{
	struct xfs_inode	*ip = XFS_I(inode);

	return generic_drop_inode(inode) || (ip->i_flags & XFS_IDONTCACHE);
}

STATIC void
xfs_free_fsname(
	struct xfs_mount	*mp)
{
	kfree(mp->m_fsname);
	kfree(mp->m_rtname);
	kfree(mp->m_logname);
}

STATIC void
xfs_fs_put_super(
	struct super_block	*sb)
{
	struct xfs_mount	*mp = XFS_M(sb);

	xfs_syncd_stop(mp);

	/*
	 * Blow away any referenced inode in the filestreams cache.
	 * This can and will cause log traffic as inodes go inactive
	 * here.
	 */
	xfs_filestream_unmount(mp);

	xfs_flush_buftarg(mp->m_ddev_targp, 1);

	xfs_unmountfs(mp);
	xfs_freesb(mp);
	xfs_icsb_destroy_counters(mp);
	xfs_destroy_mount_workqueues(mp);
	xfs_close_devices(mp);
	xfs_free_fsname(mp);
	kfree(mp);
}

STATIC int
xfs_fs_sync_fs(
	struct super_block	*sb,
	int			wait)
{
	struct xfs_mount	*mp = XFS_M(sb);
	int			error;

	/*
	 * Doing anything during the async pass would be counterproductive.
	 */
	if (!wait)
		return 0;

	error = xfs_quiesce_data(mp);
	if (error)
		return -error;

	if (laptop_mode) {
		/*
		 * The disk must be active because we're syncing.
		 * We schedule xfssyncd now (now that the disk is
		 * active) instead of later (when it might not be).
		 */
		flush_delayed_work_sync(&mp->m_sync_work);
	}

	return 0;
}

STATIC int
xfs_fs_statfs(
	struct dentry		*dentry,
	struct kstatfs		*statp)
{
	struct xfs_mount	*mp = XFS_M(dentry->d_sb);
	xfs_sb_t		*sbp = &mp->m_sb;
	struct xfs_inode	*ip = XFS_I(dentry->d_inode);
	__uint64_t		fakeinos, id;
	xfs_extlen_t		lsize;
	__int64_t		ffree;

	statp->f_type = XFS_SB_MAGIC;
	statp->f_namelen = MAXNAMELEN - 1;

	id = huge_encode_dev(mp->m_ddev_targp->bt_dev);
	statp->f_fsid.val[0] = (u32)id;
	statp->f_fsid.val[1] = (u32)(id >> 32);

	xfs_icsb_sync_counters(mp, XFS_ICSB_LAZY_COUNT);

	spin_lock(&mp->m_sb_lock);
	statp->f_bsize = sbp->sb_blocksize;
	lsize = sbp->sb_logstart ? sbp->sb_logblocks : 0;
	statp->f_blocks = sbp->sb_dblocks - lsize;
	statp->f_bfree = statp->f_bavail =
				sbp->sb_fdblocks - XFS_ALLOC_SET_ASIDE(mp);
	fakeinos = statp->f_bfree << sbp->sb_inopblog;
	statp->f_files =
	    MIN(sbp->sb_icount + fakeinos, (__uint64_t)XFS_MAXINUMBER);
	if (mp->m_maxicount)
		statp->f_files = min_t(typeof(statp->f_files),
					statp->f_files,
					mp->m_maxicount);

	/* make sure statp->f_ffree does not underflow */
	ffree = statp->f_files - (sbp->sb_icount - sbp->sb_ifree);
	statp->f_ffree = max_t(__int64_t, ffree, 0);

	spin_unlock(&mp->m_sb_lock);

	if ((ip->i_d.di_flags & XFS_DIFLAG_PROJINHERIT) ||
	    ((mp->m_qflags & (XFS_PQUOTA_ACCT|XFS_OQUOTA_ENFD))) ==
			      (XFS_PQUOTA_ACCT|XFS_OQUOTA_ENFD))
		xfs_qm_statvfs(ip, statp);
	return 0;
}

STATIC void
xfs_save_resvblks(struct xfs_mount *mp)
{
	__uint64_t resblks = 0;

	mp->m_resblks_save = mp->m_resblks;
	xfs_reserve_blocks(mp, &resblks, NULL);
}

STATIC void
xfs_restore_resvblks(struct xfs_mount *mp)
{
	__uint64_t resblks;

	if (mp->m_resblks_save) {
		resblks = mp->m_resblks_save;
		mp->m_resblks_save = 0;
	} else
		resblks = xfs_default_resblks(mp);

	xfs_reserve_blocks(mp, &resblks, NULL);
}

STATIC int
xfs_fs_remount(
	struct super_block	*sb,
	int			*flags,
	char			*options)
{
	struct xfs_mount	*mp = XFS_M(sb);
	substring_t		args[MAX_OPT_ARGS];
	char			*p;
	int			error;

	while ((p = strsep(&options, ",")) != NULL) {
		int token;

		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_barrier:
			mp->m_flags |= XFS_MOUNT_BARRIER;
			break;
		case Opt_nobarrier:
			mp->m_flags &= ~XFS_MOUNT_BARRIER;
			break;
		default:
			/*
			 * Logically we would return an error here to prevent
			 * users from believing they might have changed
			 * mount options using remount which can't be changed.
			 *
			 * But unfortunately mount(8) adds all options from
			 * mtab and fstab to the mount arguments in some cases
			 * so we can't blindly reject options, but have to
			 * check for each specified option if it actually
			 * differs from the currently set option and only
			 * reject it if that's the case.
			 *
			 * Until that is implemented we return success for
			 * every remount request, and silently ignore all
			 * options that we can't actually change.
			 */
#if 0
			xfs_info(mp,
		"mount option \"%s\" not supported for remount\n", p);
			return -EINVAL;
#else
			break;
#endif
		}
	}

	/* ro -> rw */
	if ((mp->m_flags & XFS_MOUNT_RDONLY) && !(*flags & MS_RDONLY)) {
		mp->m_flags &= ~XFS_MOUNT_RDONLY;

		/*
		 * If this is the first remount to writeable state we
		 * might have some superblock changes to update.
		 */
		if (mp->m_update_flags) {
			error = xfs_mount_log_sb(mp, mp->m_update_flags);
			if (error) {
				xfs_warn(mp, "failed to write sb changes");
				return error;
			}
			mp->m_update_flags = 0;
		}

		/*
		 * Fill out the reserve pool if it is empty. Use the stashed
		 * value if it is non-zero, otherwise go with the default.
		 */
		xfs_restore_resvblks(mp);
	}

	/* rw -> ro */
	if (!(mp->m_flags & XFS_MOUNT_RDONLY) && (*flags & MS_RDONLY)) {
		/*
		 * After we have synced the data but before we sync the
		 * metadata, we need to free up the reserve block pool so that
		 * the used block count in the superblock on disk is correct at
		 * the end of the remount. Stash the current reserve pool size
		 * so that if we get remounted rw, we can return it to the same
		 * size.
		 */

		xfs_quiesce_data(mp);
		xfs_save_resvblks(mp);
		xfs_quiesce_attr(mp);
		mp->m_flags |= XFS_MOUNT_RDONLY;
	}

	return 0;
}

/*
 * Second stage of a freeze. The data is already frozen so we only
 * need to take care of the metadata. Once that's done write a dummy
 * record to dirty the log in case of a crash while frozen.
 */
STATIC int
xfs_fs_freeze(
	struct super_block	*sb)
{
	struct xfs_mount	*mp = XFS_M(sb);

	xfs_save_resvblks(mp);
	xfs_quiesce_attr(mp);
	return -xfs_fs_log_dummy(mp);
}

STATIC int
xfs_fs_unfreeze(
	struct super_block	*sb)
{
	struct xfs_mount	*mp = XFS_M(sb);

	xfs_restore_resvblks(mp);
	return 0;
}

STATIC int
xfs_fs_show_options(
	struct seq_file		*m,
	struct dentry		*root)
{
	return -xfs_showargs(XFS_M(root->d_sb), m);
}

/*
 * This function fills in xfs_mount_t fields based on mount args.
 * Note: the superblock _has_ now been read in.
 */
STATIC int
xfs_finish_flags(
	struct xfs_mount	*mp)
{
	int			ronly = (mp->m_flags & XFS_MOUNT_RDONLY);

	/* Fail a mount where the logbuf is smaller than the log stripe */
	if (xfs_sb_version_haslogv2(&mp->m_sb)) {
		if (mp->m_logbsize <= 0 &&
		    mp->m_sb.sb_logsunit > XLOG_BIG_RECORD_BSIZE) {
			mp->m_logbsize = mp->m_sb.sb_logsunit;
		} else if (mp->m_logbsize > 0 &&
			   mp->m_logbsize < mp->m_sb.sb_logsunit) {
			xfs_warn(mp,
		"logbuf size must be greater than or equal to log stripe size");
			return XFS_ERROR(EINVAL);
		}
	} else {
		/* Fail a mount if the logbuf is larger than 32K */
		if (mp->m_logbsize > XLOG_BIG_RECORD_BSIZE) {
			xfs_warn(mp,
		"logbuf size for version 1 logs must be 16K or 32K");
			return XFS_ERROR(EINVAL);
		}
	}

	/*
	 * mkfs'ed attr2 will turn on attr2 mount unless explicitly
	 * told by noattr2 to turn it off
	 */
	if (xfs_sb_version_hasattr2(&mp->m_sb) &&
	    !(mp->m_flags & XFS_MOUNT_NOATTR2))
		mp->m_flags |= XFS_MOUNT_ATTR2;

	/*
	 * prohibit r/w mounts of read-only filesystems
	 */
	if ((mp->m_sb.sb_flags & XFS_SBF_READONLY) && !ronly) {
		xfs_warn(mp,
			"cannot mount a read-only filesystem as read-write");
		return XFS_ERROR(EROFS);
	}

	return 0;
}

STATIC int
xfs_fs_fill_super(
	struct super_block	*sb,
	void			*data,
	int			silent)
{
	struct inode		*root;
	struct xfs_mount	*mp = NULL;
	int			flags = 0, error = ENOMEM;

	mp = kzalloc(sizeof(struct xfs_mount), GFP_KERNEL);
	if (!mp)
		goto out;

	spin_lock_init(&mp->m_sb_lock);
	mutex_init(&mp->m_growlock);
	atomic_set(&mp->m_active_trans, 0);

	mp->m_super = sb;
	sb->s_fs_info = mp;

	error = xfs_parseargs(mp, (char *)data);
	if (error)
		goto out_free_fsname;

	sb_min_blocksize(sb, BBSIZE);
	sb->s_xattr = xfs_xattr_handlers;
	sb->s_export_op = &xfs_export_operations;
#ifdef CONFIG_XFS_QUOTA
	sb->s_qcop = &xfs_quotactl_operations;
#endif
	sb->s_op = &xfs_super_operations;

	if (silent)
		flags |= XFS_MFSI_QUIET;

	error = xfs_open_devices(mp);
	if (error)
		goto out_free_fsname;

	error = xfs_init_mount_workqueues(mp);
	if (error)
		goto out_close_devices;

	error = xfs_icsb_init_counters(mp);
	if (error)
		goto out_destroy_workqueues;

	error = xfs_readsb(mp, flags);
	if (error)
		goto out_destroy_counters;

	error = xfs_finish_flags(mp);
	if (error)
		goto out_free_sb;

	error = xfs_setup_devices(mp);
	if (error)
		goto out_free_sb;

	error = xfs_filestream_mount(mp);
	if (error)
		goto out_free_sb;

	/*
	 * we must configure the block size in the superblock before we run the
	 * full mount process as the mount process can lookup and cache inodes.
	 * For the same reason we must also initialise the syncd and register
	 * the inode cache shrinker so that inodes can be reclaimed during
	 * operations like a quotacheck that iterate all inodes in the
	 * filesystem.
	 */
	sb->s_magic = XFS_SB_MAGIC;
	sb->s_blocksize = mp->m_sb.sb_blocksize;
	sb->s_blocksize_bits = ffs(sb->s_blocksize) - 1;
	sb->s_maxbytes = xfs_max_file_offset(sb->s_blocksize_bits);
	sb->s_max_links = XFS_MAXLINK;
	sb->s_time_gran = 1;
	set_posix_acl_flag(sb);

	error = xfs_mountfs(mp);
	if (error)
		goto out_filestream_unmount;

	error = xfs_syncd_init(mp);
	if (error)
		goto out_unmount;

	root = igrab(VFS_I(mp->m_rootip));
	if (!root) {
		error = ENOENT;
		goto out_syncd_stop;
	}
	if (is_bad_inode(root)) {
		error = EINVAL;
		goto out_syncd_stop;
	}
	sb->s_root = d_make_root(root);
	if (!sb->s_root) {
		error = ENOMEM;
		goto out_syncd_stop;
	}

	return 0;

 out_filestream_unmount:
	xfs_filestream_unmount(mp);
 out_free_sb:
	xfs_freesb(mp);
 out_destroy_counters:
	xfs_icsb_destroy_counters(mp);
out_destroy_workqueues:
	xfs_destroy_mount_workqueues(mp);
 out_close_devices:
	xfs_close_devices(mp);
 out_free_fsname:
	xfs_free_fsname(mp);
	kfree(mp);
 out:
	return -error;

 out_syncd_stop:
	xfs_syncd_stop(mp);
 out_unmount:
	/*
	 * Blow away any referenced inode in the filestreams cache.
	 * This can and will cause log traffic as inodes go inactive
	 * here.
	 */
	xfs_filestream_unmount(mp);

	xfs_flush_buftarg(mp->m_ddev_targp, 1);

	xfs_unmountfs(mp);
	goto out_free_sb;
}

STATIC struct dentry *
xfs_fs_mount(
	struct file_system_type	*fs_type,
	int			flags,
	const char		*dev_name,
	void			*data)
{
	return mount_bdev(fs_type, flags, dev_name, data, xfs_fs_fill_super);
}

static int
xfs_fs_nr_cached_objects(
	struct super_block	*sb)
{
	return xfs_reclaim_inodes_count(XFS_M(sb));
}

static void
xfs_fs_free_cached_objects(
	struct super_block	*sb,
	int			nr_to_scan)
{
	xfs_reclaim_inodes_nr(XFS_M(sb), nr_to_scan);
}

static const struct super_operations xfs_super_operations = {
	.alloc_inode		= xfs_fs_alloc_inode,
	.destroy_inode		= xfs_fs_destroy_inode,
	.dirty_inode		= xfs_fs_dirty_inode,
	.evict_inode		= xfs_fs_evict_inode,
	.drop_inode		= xfs_fs_drop_inode,
	.put_super		= xfs_fs_put_super,
	.sync_fs		= xfs_fs_sync_fs,
	.freeze_fs		= xfs_fs_freeze,
	.unfreeze_fs		= xfs_fs_unfreeze,
	.statfs			= xfs_fs_statfs,
	.remount_fs		= xfs_fs_remount,
	.show_options		= xfs_fs_show_options,
	.nr_cached_objects	= xfs_fs_nr_cached_objects,
	.free_cached_objects	= xfs_fs_free_cached_objects,
};

static struct file_system_type xfs_fs_type = {
	.owner			= THIS_MODULE,
	.name			= "xfs",
	.mount			= xfs_fs_mount,
	.kill_sb		= kill_block_super,
	.fs_flags		= FS_REQUIRES_DEV,
};

STATIC int __init
xfs_init_zones(void)
{

	xfs_ioend_zone = kmem_zone_init(sizeof(xfs_ioend_t), "xfs_ioend");
	if (!xfs_ioend_zone)
		goto out;

	xfs_ioend_pool = mempool_create_slab_pool(4 * MAX_BUF_PER_PAGE,
						  xfs_ioend_zone);
	if (!xfs_ioend_pool)
		goto out_destroy_ioend_zone;

	xfs_log_ticket_zone = kmem_zone_init(sizeof(xlog_ticket_t),
						"xfs_log_ticket");
	if (!xfs_log_ticket_zone)
		goto out_destroy_ioend_pool;

	xfs_bmap_free_item_zone = kmem_zone_init(sizeof(xfs_bmap_free_item_t),
						"xfs_bmap_free_item");
	if (!xfs_bmap_free_item_zone)
		goto out_destroy_log_ticket_zone;

	xfs_btree_cur_zone = kmem_zone_init(sizeof(xfs_btree_cur_t),
						"xfs_btree_cur");
	if (!xfs_btree_cur_zone)
		goto out_destroy_bmap_free_item_zone;

	xfs_da_state_zone = kmem_zone_init(sizeof(xfs_da_state_t),
						"xfs_da_state");
	if (!xfs_da_state_zone)
		goto out_destroy_btree_cur_zone;

	xfs_dabuf_zone = kmem_zone_init(sizeof(xfs_dabuf_t), "xfs_dabuf");
	if (!xfs_dabuf_zone)
		goto out_destroy_da_state_zone;

	xfs_ifork_zone = kmem_zone_init(sizeof(xfs_ifork_t), "xfs_ifork");
	if (!xfs_ifork_zone)
		goto out_destroy_dabuf_zone;

	xfs_trans_zone = kmem_zone_init(sizeof(xfs_trans_t), "xfs_trans");
	if (!xfs_trans_zone)
		goto out_destroy_ifork_zone;

	xfs_log_item_desc_zone =
		kmem_zone_init(sizeof(struct xfs_log_item_desc),
			       "xfs_log_item_desc");
	if (!xfs_log_item_desc_zone)
		goto out_destroy_trans_zone;

	/*
	 * The size of the zone allocated buf log item is the maximum
	 * size possible under XFS.  This wastes a little bit of memory,
	 * but it is much faster.
	 */
	xfs_buf_item_zone = kmem_zone_init((sizeof(xfs_buf_log_item_t) +
				(((XFS_MAX_BLOCKSIZE / XFS_BLF_CHUNK) /
				  NBWORD) * sizeof(int))), "xfs_buf_item");
	if (!xfs_buf_item_zone)
		goto out_destroy_log_item_desc_zone;

	xfs_efd_zone = kmem_zone_init((sizeof(xfs_efd_log_item_t) +
			((XFS_EFD_MAX_FAST_EXTENTS - 1) *
				 sizeof(xfs_extent_t))), "xfs_efd_item");
	if (!xfs_efd_zone)
		goto out_destroy_buf_item_zone;

	xfs_efi_zone = kmem_zone_init((sizeof(xfs_efi_log_item_t) +
			((XFS_EFI_MAX_FAST_EXTENTS - 1) *
				sizeof(xfs_extent_t))), "xfs_efi_item");
	if (!xfs_efi_zone)
		goto out_destroy_efd_zone;

	xfs_inode_zone =
		kmem_zone_init_flags(sizeof(xfs_inode_t), "xfs_inode",
			KM_ZONE_HWALIGN | KM_ZONE_RECLAIM | KM_ZONE_SPREAD,
			xfs_fs_inode_init_once);
	if (!xfs_inode_zone)
		goto out_destroy_efi_zone;

	xfs_ili_zone =
		kmem_zone_init_flags(sizeof(xfs_inode_log_item_t), "xfs_ili",
					KM_ZONE_SPREAD, NULL);
	if (!xfs_ili_zone)
		goto out_destroy_inode_zone;

	return 0;

 out_destroy_inode_zone:
	kmem_zone_destroy(xfs_inode_zone);
 out_destroy_efi_zone:
	kmem_zone_destroy(xfs_efi_zone);
 out_destroy_efd_zone:
	kmem_zone_destroy(xfs_efd_zone);
 out_destroy_buf_item_zone:
	kmem_zone_destroy(xfs_buf_item_zone);
 out_destroy_log_item_desc_zone:
	kmem_zone_destroy(xfs_log_item_desc_zone);
 out_destroy_trans_zone:
	kmem_zone_destroy(xfs_trans_zone);
 out_destroy_ifork_zone:
	kmem_zone_destroy(xfs_ifork_zone);
 out_destroy_dabuf_zone:
	kmem_zone_destroy(xfs_dabuf_zone);
 out_destroy_da_state_zone:
	kmem_zone_destroy(xfs_da_state_zone);
 out_destroy_btree_cur_zone:
	kmem_zone_destroy(xfs_btree_cur_zone);
 out_destroy_bmap_free_item_zone:
	kmem_zone_destroy(xfs_bmap_free_item_zone);
 out_destroy_log_ticket_zone:
	kmem_zone_destroy(xfs_log_ticket_zone);
 out_destroy_ioend_pool:
	mempool_destroy(xfs_ioend_pool);
 out_destroy_ioend_zone:
	kmem_zone_destroy(xfs_ioend_zone);
 out:
	return -ENOMEM;
}

STATIC void
xfs_destroy_zones(void)
{
	kmem_zone_destroy(xfs_ili_zone);
	kmem_zone_destroy(xfs_inode_zone);
	kmem_zone_destroy(xfs_efi_zone);
	kmem_zone_destroy(xfs_efd_zone);
	kmem_zone_destroy(xfs_buf_item_zone);
	kmem_zone_destroy(xfs_log_item_desc_zone);
	kmem_zone_destroy(xfs_trans_zone);
	kmem_zone_destroy(xfs_ifork_zone);
	kmem_zone_destroy(xfs_dabuf_zone);
	kmem_zone_destroy(xfs_da_state_zone);
	kmem_zone_destroy(xfs_btree_cur_zone);
	kmem_zone_destroy(xfs_bmap_free_item_zone);
	kmem_zone_destroy(xfs_log_ticket_zone);
	mempool_destroy(xfs_ioend_pool);
	kmem_zone_destroy(xfs_ioend_zone);

}

STATIC int __init
xfs_init_workqueues(void)
{
	/*
	 * We never want to the same work item to run twice, reclaiming inodes
	 * or idling the log is not going to get any faster by multiple CPUs
	 * competing for ressources.  Use the default large max_active value
	 * so that even lots of filesystems can perform these task in parallel.
	 */
	xfs_syncd_wq = alloc_workqueue("xfssyncd", WQ_NON_REENTRANT, 0);
	if (!xfs_syncd_wq)
		return -ENOMEM;

	/*
	 * The allocation workqueue can be used in memory reclaim situations
	 * (writepage path), and parallelism is only limited by the number of
	 * AGs in all the filesystems mounted. Hence use the default large
	 * max_active value for this workqueue.
	 */
	xfs_alloc_wq = alloc_workqueue("xfsalloc", WQ_MEM_RECLAIM, 0);
	if (!xfs_alloc_wq)
		goto out_destroy_syncd;

	return 0;

out_destroy_syncd:
	destroy_workqueue(xfs_syncd_wq);
	return -ENOMEM;
}

STATIC void
xfs_destroy_workqueues(void)
{
	destroy_workqueue(xfs_alloc_wq);
	destroy_workqueue(xfs_syncd_wq);
}

STATIC int __init
init_xfs_fs(void)
{
	int			error;

	printk(KERN_INFO XFS_VERSION_STRING " with "
			 XFS_BUILD_OPTIONS " enabled\n");

	xfs_dir_startup();

	error = xfs_init_zones();
	if (error)
		goto out;

	error = xfs_init_workqueues();
	if (error)
		goto out_destroy_zones;

	error = xfs_mru_cache_init();
	if (error)
		goto out_destroy_wq;

	error = xfs_filestream_init();
	if (error)
		goto out_mru_cache_uninit;

	error = xfs_buf_init();
	if (error)
		goto out_filestream_uninit;

	error = xfs_init_procfs();
	if (error)
		goto out_buf_terminate;

	error = xfs_sysctl_register();
	if (error)
		goto out_cleanup_procfs;

	error = xfs_qm_init();
	if (error)
		goto out_sysctl_unregister;

	error = register_filesystem(&xfs_fs_type);
	if (error)
		goto out_qm_exit;
	return 0;

 out_qm_exit:
	xfs_qm_exit();
 out_sysctl_unregister:
	xfs_sysctl_unregister();
 out_cleanup_procfs:
	xfs_cleanup_procfs();
 out_buf_terminate:
	xfs_buf_terminate();
 out_filestream_uninit:
	xfs_filestream_uninit();
 out_mru_cache_uninit:
	xfs_mru_cache_uninit();
 out_destroy_wq:
	xfs_destroy_workqueues();
 out_destroy_zones:
	xfs_destroy_zones();
 out:
	return error;
}

STATIC void __exit
exit_xfs_fs(void)
{
	xfs_qm_exit();
	unregister_filesystem(&xfs_fs_type);
	xfs_sysctl_unregister();
	xfs_cleanup_procfs();
	xfs_buf_terminate();
	xfs_filestream_uninit();
	xfs_mru_cache_uninit();
	xfs_destroy_workqueues();
	xfs_destroy_zones();
}

module_init(init_xfs_fs);
module_exit(exit_xfs_fs);

MODULE_AUTHOR("Silicon Graphics, Inc.");
MODULE_DESCRIPTION(XFS_VERSION_STRING " with " XFS_BUILD_OPTIONS " enabled");
MODULE_LICENSE("GPL");
