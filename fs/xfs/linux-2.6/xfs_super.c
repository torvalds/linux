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
#include "xfs_clnt.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir2.h"
#include "xfs_alloc.h"
#include "xfs_dmapi.h"
#include "xfs_quota.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_bmap.h"
#include "xfs_rtalloc.h"
#include "xfs_error.h"
#include "xfs_itable.h"
#include "xfs_fsops.h"
#include "xfs_rw.h"
#include "xfs_acl.h"
#include "xfs_attr.h"
#include "xfs_buf_item.h"
#include "xfs_utils.h"
#include "xfs_vnodeops.h"
#include "xfs_vfsops.h"
#include "xfs_version.h"
#include "xfs_log_priv.h"
#include "xfs_trans_priv.h"

#include <linux/namei.h>
#include <linux/init.h>
#include <linux/mount.h>
#include <linux/mempool.h>
#include <linux/writeback.h>
#include <linux/kthread.h>
#include <linux/freezer.h>

static struct quotactl_ops xfs_quotactl_operations;
static struct super_operations xfs_super_operations;
static kmem_zone_t *xfs_vnode_zone;
static kmem_zone_t *xfs_ioend_zone;
mempool_t *xfs_ioend_pool;

STATIC struct xfs_mount_args *
xfs_args_allocate(
	struct super_block	*sb,
	int			silent)
{
	struct xfs_mount_args	*args;

	args = kmem_zalloc(sizeof(struct xfs_mount_args), KM_SLEEP);
	args->logbufs = args->logbufsize = -1;
	strncpy(args->fsname, sb->s_id, MAXNAMELEN);

	/* Copy the already-parsed mount(2) flags we're interested in */
	if (sb->s_flags & MS_DIRSYNC)
		args->flags |= XFSMNT_DIRSYNC;
	if (sb->s_flags & MS_SYNCHRONOUS)
		args->flags |= XFSMNT_WSYNC;
	if (silent)
		args->flags |= XFSMNT_QUIET;
	args->flags |= XFSMNT_32BITINODES;

	return args;
}

#define MNTOPT_LOGBUFS	"logbufs"	/* number of XFS log buffers */
#define MNTOPT_LOGBSIZE	"logbsize"	/* size of XFS log buffers */
#define MNTOPT_LOGDEV	"logdev"	/* log device */
#define MNTOPT_RTDEV	"rtdev"		/* realtime I/O device */
#define MNTOPT_BIOSIZE	"biosize"	/* log2 of preferred buffered io size */
#define MNTOPT_WSYNC	"wsync"		/* safe-mode nfs compatible mount */
#define MNTOPT_INO64	"ino64"		/* force inodes into 64-bit range */
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
#define MNTOPT_OSYNCISOSYNC "osyncisosync" /* o_sync is REALLY o_sync */
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
#define MNTOPT_DMAPI	"dmapi"		/* DMI enabled (DMAPI / XDSM) */
#define MNTOPT_XDSM	"xdsm"		/* DMI enabled (DMAPI / XDSM) */
#define MNTOPT_DMI	"dmi"		/* DMI enabled (DMAPI / XDSM) */

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

STATIC int
xfs_parseargs(
	struct xfs_mount	*mp,
	char			*options,
	struct xfs_mount_args	*args,
	int			update)
{
	char			*this_char, *value, *eov;
	int			dsunit, dswidth, vol_dsunit, vol_dswidth;
	int			iosize;
	int			dmapi_implies_ikeep = 1;

	args->flags |= XFSMNT_BARRIER;
	args->flags2 |= XFSMNT2_COMPAT_IOSIZE;

	if (!options)
		goto done;

	iosize = dsunit = dswidth = vol_dsunit = vol_dswidth = 0;

	while ((this_char = strsep(&options, ",")) != NULL) {
		if (!*this_char)
			continue;
		if ((value = strchr(this_char, '=')) != NULL)
			*value++ = 0;

		if (!strcmp(this_char, MNTOPT_LOGBUFS)) {
			if (!value || !*value) {
				cmn_err(CE_WARN,
					"XFS: %s option requires an argument",
					this_char);
				return EINVAL;
			}
			args->logbufs = simple_strtoul(value, &eov, 10);
		} else if (!strcmp(this_char, MNTOPT_LOGBSIZE)) {
			if (!value || !*value) {
				cmn_err(CE_WARN,
					"XFS: %s option requires an argument",
					this_char);
				return EINVAL;
			}
			args->logbufsize = suffix_strtoul(value, &eov, 10);
		} else if (!strcmp(this_char, MNTOPT_LOGDEV)) {
			if (!value || !*value) {
				cmn_err(CE_WARN,
					"XFS: %s option requires an argument",
					this_char);
				return EINVAL;
			}
			strncpy(args->logname, value, MAXNAMELEN);
		} else if (!strcmp(this_char, MNTOPT_MTPT)) {
			if (!value || !*value) {
				cmn_err(CE_WARN,
					"XFS: %s option requires an argument",
					this_char);
				return EINVAL;
			}
			strncpy(args->mtpt, value, MAXNAMELEN);
		} else if (!strcmp(this_char, MNTOPT_RTDEV)) {
			if (!value || !*value) {
				cmn_err(CE_WARN,
					"XFS: %s option requires an argument",
					this_char);
				return EINVAL;
			}
			strncpy(args->rtname, value, MAXNAMELEN);
		} else if (!strcmp(this_char, MNTOPT_BIOSIZE)) {
			if (!value || !*value) {
				cmn_err(CE_WARN,
					"XFS: %s option requires an argument",
					this_char);
				return EINVAL;
			}
			iosize = simple_strtoul(value, &eov, 10);
			args->flags |= XFSMNT_IOSIZE;
			args->iosizelog = (uint8_t) iosize;
		} else if (!strcmp(this_char, MNTOPT_ALLOCSIZE)) {
			if (!value || !*value) {
				cmn_err(CE_WARN,
					"XFS: %s option requires an argument",
					this_char);
				return EINVAL;
			}
			iosize = suffix_strtoul(value, &eov, 10);
			args->flags |= XFSMNT_IOSIZE;
			args->iosizelog = ffs(iosize) - 1;
		} else if (!strcmp(this_char, MNTOPT_GRPID) ||
			   !strcmp(this_char, MNTOPT_BSDGROUPS)) {
			mp->m_flags |= XFS_MOUNT_GRPID;
		} else if (!strcmp(this_char, MNTOPT_NOGRPID) ||
			   !strcmp(this_char, MNTOPT_SYSVGROUPS)) {
			mp->m_flags &= ~XFS_MOUNT_GRPID;
		} else if (!strcmp(this_char, MNTOPT_WSYNC)) {
			args->flags |= XFSMNT_WSYNC;
		} else if (!strcmp(this_char, MNTOPT_OSYNCISOSYNC)) {
			args->flags |= XFSMNT_OSYNCISOSYNC;
		} else if (!strcmp(this_char, MNTOPT_NORECOVERY)) {
			args->flags |= XFSMNT_NORECOVERY;
		} else if (!strcmp(this_char, MNTOPT_INO64)) {
			args->flags |= XFSMNT_INO64;
#if !XFS_BIG_INUMS
			cmn_err(CE_WARN,
				"XFS: %s option not allowed on this system",
				this_char);
			return EINVAL;
#endif
		} else if (!strcmp(this_char, MNTOPT_NOALIGN)) {
			args->flags |= XFSMNT_NOALIGN;
		} else if (!strcmp(this_char, MNTOPT_SWALLOC)) {
			args->flags |= XFSMNT_SWALLOC;
		} else if (!strcmp(this_char, MNTOPT_SUNIT)) {
			if (!value || !*value) {
				cmn_err(CE_WARN,
					"XFS: %s option requires an argument",
					this_char);
				return EINVAL;
			}
			dsunit = simple_strtoul(value, &eov, 10);
		} else if (!strcmp(this_char, MNTOPT_SWIDTH)) {
			if (!value || !*value) {
				cmn_err(CE_WARN,
					"XFS: %s option requires an argument",
					this_char);
				return EINVAL;
			}
			dswidth = simple_strtoul(value, &eov, 10);
		} else if (!strcmp(this_char, MNTOPT_64BITINODE)) {
			args->flags &= ~XFSMNT_32BITINODES;
#if !XFS_BIG_INUMS
			cmn_err(CE_WARN,
				"XFS: %s option not allowed on this system",
				this_char);
			return EINVAL;
#endif
		} else if (!strcmp(this_char, MNTOPT_NOUUID)) {
			args->flags |= XFSMNT_NOUUID;
		} else if (!strcmp(this_char, MNTOPT_BARRIER)) {
			args->flags |= XFSMNT_BARRIER;
		} else if (!strcmp(this_char, MNTOPT_NOBARRIER)) {
			args->flags &= ~XFSMNT_BARRIER;
		} else if (!strcmp(this_char, MNTOPT_IKEEP)) {
			args->flags |= XFSMNT_IKEEP;
		} else if (!strcmp(this_char, MNTOPT_NOIKEEP)) {
			dmapi_implies_ikeep = 0;
			args->flags &= ~XFSMNT_IKEEP;
		} else if (!strcmp(this_char, MNTOPT_LARGEIO)) {
			args->flags2 &= ~XFSMNT2_COMPAT_IOSIZE;
		} else if (!strcmp(this_char, MNTOPT_NOLARGEIO)) {
			args->flags2 |= XFSMNT2_COMPAT_IOSIZE;
		} else if (!strcmp(this_char, MNTOPT_ATTR2)) {
			args->flags |= XFSMNT_ATTR2;
		} else if (!strcmp(this_char, MNTOPT_NOATTR2)) {
			args->flags &= ~XFSMNT_ATTR2;
		} else if (!strcmp(this_char, MNTOPT_FILESTREAM)) {
			args->flags2 |= XFSMNT2_FILESTREAMS;
		} else if (!strcmp(this_char, MNTOPT_NOQUOTA)) {
			args->flags &= ~(XFSMNT_UQUOTAENF|XFSMNT_UQUOTA);
			args->flags &= ~(XFSMNT_GQUOTAENF|XFSMNT_GQUOTA);
		} else if (!strcmp(this_char, MNTOPT_QUOTA) ||
			   !strcmp(this_char, MNTOPT_UQUOTA) ||
			   !strcmp(this_char, MNTOPT_USRQUOTA)) {
			args->flags |= XFSMNT_UQUOTA | XFSMNT_UQUOTAENF;
		} else if (!strcmp(this_char, MNTOPT_QUOTANOENF) ||
			   !strcmp(this_char, MNTOPT_UQUOTANOENF)) {
			args->flags |= XFSMNT_UQUOTA;
			args->flags &= ~XFSMNT_UQUOTAENF;
		} else if (!strcmp(this_char, MNTOPT_PQUOTA) ||
			   !strcmp(this_char, MNTOPT_PRJQUOTA)) {
			args->flags |= XFSMNT_PQUOTA | XFSMNT_PQUOTAENF;
		} else if (!strcmp(this_char, MNTOPT_PQUOTANOENF)) {
			args->flags |= XFSMNT_PQUOTA;
			args->flags &= ~XFSMNT_PQUOTAENF;
		} else if (!strcmp(this_char, MNTOPT_GQUOTA) ||
			   !strcmp(this_char, MNTOPT_GRPQUOTA)) {
			args->flags |= XFSMNT_GQUOTA | XFSMNT_GQUOTAENF;
		} else if (!strcmp(this_char, MNTOPT_GQUOTANOENF)) {
			args->flags |= XFSMNT_GQUOTA;
			args->flags &= ~XFSMNT_GQUOTAENF;
		} else if (!strcmp(this_char, MNTOPT_DMAPI)) {
			args->flags |= XFSMNT_DMAPI;
		} else if (!strcmp(this_char, MNTOPT_XDSM)) {
			args->flags |= XFSMNT_DMAPI;
		} else if (!strcmp(this_char, MNTOPT_DMI)) {
			args->flags |= XFSMNT_DMAPI;
		} else if (!strcmp(this_char, "ihashsize")) {
			cmn_err(CE_WARN,
	"XFS: ihashsize no longer used, option is deprecated.");
		} else if (!strcmp(this_char, "osyncisdsync")) {
			/* no-op, this is now the default */
			cmn_err(CE_WARN,
	"XFS: osyncisdsync is now the default, option is deprecated.");
		} else if (!strcmp(this_char, "irixsgid")) {
			cmn_err(CE_WARN,
	"XFS: irixsgid is now a sysctl(2) variable, option is deprecated.");
		} else {
			cmn_err(CE_WARN,
				"XFS: unknown mount option [%s].", this_char);
			return EINVAL;
		}
	}

	if (args->flags & XFSMNT_NORECOVERY) {
		if ((mp->m_flags & XFS_MOUNT_RDONLY) == 0) {
			cmn_err(CE_WARN,
				"XFS: no-recovery mounts must be read-only.");
			return EINVAL;
		}
	}

	if ((args->flags & XFSMNT_NOALIGN) && (dsunit || dswidth)) {
		cmn_err(CE_WARN,
	"XFS: sunit and swidth options incompatible with the noalign option");
		return EINVAL;
	}

	if ((args->flags & XFSMNT_GQUOTA) && (args->flags & XFSMNT_PQUOTA)) {
		cmn_err(CE_WARN,
			"XFS: cannot mount with both project and group quota");
		return EINVAL;
	}

	if ((args->flags & XFSMNT_DMAPI) && *args->mtpt == '\0') {
		printk("XFS: %s option needs the mount point option as well\n",
			MNTOPT_DMAPI);
		return EINVAL;
	}

	if ((dsunit && !dswidth) || (!dsunit && dswidth)) {
		cmn_err(CE_WARN,
			"XFS: sunit and swidth must be specified together");
		return EINVAL;
	}

	if (dsunit && (dswidth % dsunit != 0)) {
		cmn_err(CE_WARN,
	"XFS: stripe width (%d) must be a multiple of the stripe unit (%d)",
			dswidth, dsunit);
		return EINVAL;
	}

	/*
	 * Applications using DMI filesystems often expect the
	 * inode generation number to be monotonically increasing.
	 * If we delete inode chunks we break this assumption, so
	 * keep unused inode chunks on disk for DMI filesystems
	 * until we come up with a better solution.
	 * Note that if "ikeep" or "noikeep" mount options are
	 * supplied, then they are honored.
	 */
	if ((args->flags & XFSMNT_DMAPI) && dmapi_implies_ikeep)
		args->flags |= XFSMNT_IKEEP;

	if ((args->flags & XFSMNT_NOALIGN) != XFSMNT_NOALIGN) {
		if (dsunit) {
			args->sunit = dsunit;
			args->flags |= XFSMNT_RETERR;
		} else {
			args->sunit = vol_dsunit;
		}
		dswidth ? (args->swidth = dswidth) :
			  (args->swidth = vol_dswidth);
	} else {
		args->sunit = args->swidth = 0;
	}

done:
	if (args->flags & XFSMNT_32BITINODES)
		mp->m_flags |= XFS_MOUNT_SMALL_INUMS;
	if (args->flags2)
		args->flags |= XFSMNT_FLAGS2;
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
		{ XFS_MOUNT_INO64,		"," MNTOPT_INO64 },
		{ XFS_MOUNT_NOALIGN,		"," MNTOPT_NOALIGN },
		{ XFS_MOUNT_SWALLOC,		"," MNTOPT_SWALLOC },
		{ XFS_MOUNT_NOUUID,		"," MNTOPT_NOUUID },
		{ XFS_MOUNT_NORECOVERY,		"," MNTOPT_NORECOVERY },
		{ XFS_MOUNT_OSYNCISOSYNC,	"," MNTOPT_OSYNCISOSYNC },
		{ XFS_MOUNT_ATTR2,		"," MNTOPT_ATTR2 },
		{ XFS_MOUNT_FILESTREAMS,	"," MNTOPT_FILESTREAM },
		{ XFS_MOUNT_DMAPI,		"," MNTOPT_DMAPI },
		{ XFS_MOUNT_GRPID,		"," MNTOPT_GRPID },
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

	if (mp->m_qflags & (XFS_PQUOTA_ACCT|XFS_OQUOTA_ENFD))
		seq_puts(m, "," MNTOPT_PRJQUOTA);
	else if (mp->m_qflags & XFS_PQUOTA_ACCT)
		seq_puts(m, "," MNTOPT_PQUOTANOENF);

	if (mp->m_qflags & (XFS_GQUOTA_ACCT|XFS_OQUOTA_ENFD))
		seq_puts(m, "," MNTOPT_GRPQUOTA);
	else if (mp->m_qflags & XFS_GQUOTA_ACCT)
		seq_puts(m, "," MNTOPT_GQUOTANOENF);

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
	 * __block_prepare_write does this in an [unsigned] long...
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
# if defined(CONFIG_LBD)
	ASSERT(sizeof(sector_t) == 8);
	pagefactor = PAGE_CACHE_SIZE;
	bitshift = BITS_PER_LONG;
# else
	pagefactor = PAGE_CACHE_SIZE >> (PAGE_CACHE_SHIFT - blockshift);
# endif
#endif

	return (((__uint64_t)pagefactor) << bitshift) - 1;
}

STATIC_INLINE void
xfs_set_inodeops(
	struct inode		*inode)
{
	switch (inode->i_mode & S_IFMT) {
	case S_IFREG:
		inode->i_op = &xfs_inode_operations;
		inode->i_fop = &xfs_file_operations;
		inode->i_mapping->a_ops = &xfs_address_space_operations;
		break;
	case S_IFDIR:
		inode->i_op = &xfs_dir_inode_operations;
		inode->i_fop = &xfs_dir_file_operations;
		break;
	case S_IFLNK:
		inode->i_op = &xfs_symlink_inode_operations;
		if (!(XFS_I(inode)->i_df.if_flags & XFS_IFINLINE))
			inode->i_mapping->a_ops = &xfs_address_space_operations;
		break;
	default:
		inode->i_op = &xfs_inode_operations;
		init_special_inode(inode, inode->i_mode, inode->i_rdev);
		break;
	}
}

STATIC_INLINE void
xfs_revalidate_inode(
	xfs_mount_t		*mp,
	bhv_vnode_t		*vp,
	xfs_inode_t		*ip)
{
	struct inode		*inode = vn_to_inode(vp);

	inode->i_mode	= ip->i_d.di_mode;
	inode->i_nlink	= ip->i_d.di_nlink;
	inode->i_uid	= ip->i_d.di_uid;
	inode->i_gid	= ip->i_d.di_gid;

	switch (inode->i_mode & S_IFMT) {
	case S_IFBLK:
	case S_IFCHR:
		inode->i_rdev =
			MKDEV(sysv_major(ip->i_df.if_u2.if_rdev) & 0x1ff,
			      sysv_minor(ip->i_df.if_u2.if_rdev));
		break;
	default:
		inode->i_rdev = 0;
		break;
	}

	inode->i_generation = ip->i_d.di_gen;
	i_size_write(inode, ip->i_d.di_size);
	inode->i_atime.tv_sec	= ip->i_d.di_atime.t_sec;
	inode->i_atime.tv_nsec	= ip->i_d.di_atime.t_nsec;
	inode->i_mtime.tv_sec	= ip->i_d.di_mtime.t_sec;
	inode->i_mtime.tv_nsec	= ip->i_d.di_mtime.t_nsec;
	inode->i_ctime.tv_sec	= ip->i_d.di_ctime.t_sec;
	inode->i_ctime.tv_nsec	= ip->i_d.di_ctime.t_nsec;
	if (ip->i_d.di_flags & XFS_DIFLAG_IMMUTABLE)
		inode->i_flags |= S_IMMUTABLE;
	else
		inode->i_flags &= ~S_IMMUTABLE;
	if (ip->i_d.di_flags & XFS_DIFLAG_APPEND)
		inode->i_flags |= S_APPEND;
	else
		inode->i_flags &= ~S_APPEND;
	if (ip->i_d.di_flags & XFS_DIFLAG_SYNC)
		inode->i_flags |= S_SYNC;
	else
		inode->i_flags &= ~S_SYNC;
	if (ip->i_d.di_flags & XFS_DIFLAG_NOATIME)
		inode->i_flags |= S_NOATIME;
	else
		inode->i_flags &= ~S_NOATIME;
	xfs_iflags_clear(ip, XFS_IMODIFIED);
}

void
xfs_initialize_vnode(
	struct xfs_mount	*mp,
	bhv_vnode_t		*vp,
	struct xfs_inode	*ip)
{
	struct inode		*inode = vn_to_inode(vp);

	if (!ip->i_vnode) {
		ip->i_vnode = vp;
		inode->i_private = ip;
	}

	/*
	 * We need to set the ops vectors, and unlock the inode, but if
	 * we have been called during the new inode create process, it is
	 * too early to fill in the Linux inode.  We will get called a
	 * second time once the inode is properly set up, and then we can
	 * finish our work.
	 */
	if (ip->i_d.di_mode != 0 && (inode->i_state & I_NEW)) {
		xfs_revalidate_inode(mp, vp, ip);
		xfs_set_inodeops(inode);

		xfs_iflags_clear(ip, XFS_INEW);
		barrier();

		unlock_new_inode(inode);
	}
}

int
xfs_blkdev_get(
	xfs_mount_t		*mp,
	const char		*name,
	struct block_device	**bdevp)
{
	int			error = 0;

	*bdevp = open_bdev_excl(name, 0, mp);
	if (IS_ERR(*bdevp)) {
		error = PTR_ERR(*bdevp);
		printk("XFS: Invalid device [%s], error=%d\n", name, error);
	}

	return -error;
}

void
xfs_blkdev_put(
	struct block_device	*bdev)
{
	if (bdev)
		close_bdev_excl(bdev);
}

/*
 * Try to write out the superblock using barriers.
 */
STATIC int
xfs_barrier_test(
	xfs_mount_t	*mp)
{
	xfs_buf_t	*sbp = xfs_getsb(mp, 0);
	int		error;

	XFS_BUF_UNDONE(sbp);
	XFS_BUF_UNREAD(sbp);
	XFS_BUF_UNDELAYWRITE(sbp);
	XFS_BUF_WRITE(sbp);
	XFS_BUF_UNASYNC(sbp);
	XFS_BUF_ORDERED(sbp);

	xfsbdstrat(mp, sbp);
	error = xfs_iowait(sbp);

	/*
	 * Clear all the flags we set and possible error state in the
	 * buffer.  We only did the write to try out whether barriers
	 * worked and shouldn't leave any traces in the superblock
	 * buffer.
	 */
	XFS_BUF_DONE(sbp);
	XFS_BUF_ERROR(sbp, 0);
	XFS_BUF_UNORDERED(sbp);

	xfs_buf_relse(sbp);
	return error;
}

void
xfs_mountfs_check_barriers(xfs_mount_t *mp)
{
	int error;

	if (mp->m_logdev_targp != mp->m_ddev_targp) {
		xfs_fs_cmn_err(CE_NOTE, mp,
		  "Disabling barriers, not supported with external log device");
		mp->m_flags &= ~XFS_MOUNT_BARRIER;
		return;
	}

	if (mp->m_ddev_targp->bt_bdev->bd_disk->queue->ordered ==
					QUEUE_ORDERED_NONE) {
		xfs_fs_cmn_err(CE_NOTE, mp,
		  "Disabling barriers, not supported by the underlying device");
		mp->m_flags &= ~XFS_MOUNT_BARRIER;
		return;
	}

	if (xfs_readonly_buftarg(mp->m_ddev_targp)) {
		xfs_fs_cmn_err(CE_NOTE, mp,
		  "Disabling barriers, underlying device is readonly");
		mp->m_flags &= ~XFS_MOUNT_BARRIER;
		return;
	}

	error = xfs_barrier_test(mp);
	if (error) {
		xfs_fs_cmn_err(CE_NOTE, mp,
		  "Disabling barriers, trial barrier write failed");
		mp->m_flags &= ~XFS_MOUNT_BARRIER;
		return;
	}
}

void
xfs_blkdev_issue_flush(
	xfs_buftarg_t		*buftarg)
{
	blkdev_issue_flush(buftarg->bt_bdev, NULL);
}

/*
 * XFS AIL push thread support
 */
void
xfsaild_wakeup(
	xfs_mount_t		*mp,
	xfs_lsn_t		threshold_lsn)
{
	mp->m_ail.xa_target = threshold_lsn;
	wake_up_process(mp->m_ail.xa_task);
}

int
xfsaild(
	void	*data)
{
	xfs_mount_t	*mp = (xfs_mount_t *)data;
	xfs_lsn_t	last_pushed_lsn = 0;
	long		tout = 0;

	while (!kthread_should_stop()) {
		if (tout)
			schedule_timeout_interruptible(msecs_to_jiffies(tout));
		tout = 1000;

		/* swsusp */
		try_to_freeze();

		ASSERT(mp->m_log);
		if (XFS_FORCED_SHUTDOWN(mp))
			continue;

		tout = xfsaild_push(mp, &last_pushed_lsn);
	}

	return 0;
}	/* xfsaild */

int
xfsaild_start(
	xfs_mount_t	*mp)
{
	mp->m_ail.xa_target = 0;
	mp->m_ail.xa_task = kthread_run(xfsaild, mp, "xfsaild");
	if (IS_ERR(mp->m_ail.xa_task))
		return -PTR_ERR(mp->m_ail.xa_task);
	return 0;
}

void
xfsaild_stop(
	xfs_mount_t	*mp)
{
	kthread_stop(mp->m_ail.xa_task);
}



STATIC struct inode *
xfs_fs_alloc_inode(
	struct super_block	*sb)
{
	bhv_vnode_t		*vp;

	vp = kmem_zone_alloc(xfs_vnode_zone, KM_SLEEP);
	if (unlikely(!vp))
		return NULL;
	return vn_to_inode(vp);
}

STATIC void
xfs_fs_destroy_inode(
	struct inode		*inode)
{
	kmem_zone_free(xfs_vnode_zone, vn_from_inode(inode));
}

STATIC void
xfs_fs_inode_init_once(
	kmem_zone_t		*zonep,
	void			*vnode)
{
	inode_init_once(vn_to_inode((bhv_vnode_t *)vnode));
}

STATIC int __init
xfs_init_zones(void)
{
	xfs_vnode_zone = kmem_zone_init_flags(sizeof(bhv_vnode_t), "xfs_vnode",
					KM_ZONE_HWALIGN | KM_ZONE_RECLAIM |
					KM_ZONE_SPREAD,
					xfs_fs_inode_init_once);
	if (!xfs_vnode_zone)
		goto out;

	xfs_ioend_zone = kmem_zone_init(sizeof(xfs_ioend_t), "xfs_ioend");
	if (!xfs_ioend_zone)
		goto out_destroy_vnode_zone;

	xfs_ioend_pool = mempool_create_slab_pool(4 * MAX_BUF_PER_PAGE,
						  xfs_ioend_zone);
	if (!xfs_ioend_pool)
		goto out_free_ioend_zone;
	return 0;

 out_free_ioend_zone:
	kmem_zone_destroy(xfs_ioend_zone);
 out_destroy_vnode_zone:
	kmem_zone_destroy(xfs_vnode_zone);
 out:
	return -ENOMEM;
}

STATIC void
xfs_destroy_zones(void)
{
	mempool_destroy(xfs_ioend_pool);
	kmem_zone_destroy(xfs_vnode_zone);
	kmem_zone_destroy(xfs_ioend_zone);
}

/*
 * Attempt to flush the inode, this will actually fail
 * if the inode is pinned, but we dirty the inode again
 * at the point when it is unpinned after a log write,
 * since this is when the inode itself becomes flushable.
 */
STATIC int
xfs_fs_write_inode(
	struct inode		*inode,
	int			sync)
{
	int			error = 0;
	int			flags = 0;

	xfs_itrace_entry(XFS_I(inode));
	if (sync) {
		filemap_fdatawait(inode->i_mapping);
		flags |= FLUSH_SYNC;
	}
	error = xfs_inode_flush(XFS_I(inode), flags);
	/*
	 * if we failed to write out the inode then mark
	 * it dirty again so we'll try again later.
	 */
	if (error)
		mark_inode_dirty_sync(inode);

	return -error;
}

STATIC void
xfs_fs_clear_inode(
	struct inode		*inode)
{
	xfs_inode_t		*ip = XFS_I(inode);

	/*
	 * ip can be null when xfs_iget_core calls xfs_idestroy if we
	 * find an inode with di_mode == 0 but without IGET_CREATE set.
	 */
	if (ip) {
		xfs_itrace_entry(ip);
		XFS_STATS_INC(vn_rele);
		XFS_STATS_INC(vn_remove);
		XFS_STATS_INC(vn_reclaim);
		XFS_STATS_DEC(vn_active);

		xfs_inactive(ip);
		xfs_iflags_clear(ip, XFS_IMODIFIED);
		if (xfs_reclaim(ip))
			panic("%s: cannot reclaim 0x%p\n", __func__, inode);
	}

	ASSERT(XFS_I(inode) == NULL);
}

/*
 * Enqueue a work item to be picked up by the vfs xfssyncd thread.
 * Doing this has two advantages:
 * - It saves on stack space, which is tight in certain situations
 * - It can be used (with care) as a mechanism to avoid deadlocks.
 * Flushing while allocating in a full filesystem requires both.
 */
STATIC void
xfs_syncd_queue_work(
	struct xfs_mount *mp,
	void		*data,
	void		(*syncer)(struct xfs_mount *, void *))
{
	struct bhv_vfs_sync_work *work;

	work = kmem_alloc(sizeof(struct bhv_vfs_sync_work), KM_SLEEP);
	INIT_LIST_HEAD(&work->w_list);
	work->w_syncer = syncer;
	work->w_data = data;
	work->w_mount = mp;
	spin_lock(&mp->m_sync_lock);
	list_add_tail(&work->w_list, &mp->m_sync_list);
	spin_unlock(&mp->m_sync_lock);
	wake_up_process(mp->m_sync_task);
}

/*
 * Flush delayed allocate data, attempting to free up reserved space
 * from existing allocations.  At this point a new allocation attempt
 * has failed with ENOSPC and we are in the process of scratching our
 * heads, looking about for more room...
 */
STATIC void
xfs_flush_inode_work(
	struct xfs_mount *mp,
	void		*arg)
{
	struct inode	*inode = arg;
	filemap_flush(inode->i_mapping);
	iput(inode);
}

void
xfs_flush_inode(
	xfs_inode_t	*ip)
{
	struct inode	*inode = ip->i_vnode;

	igrab(inode);
	xfs_syncd_queue_work(ip->i_mount, inode, xfs_flush_inode_work);
	delay(msecs_to_jiffies(500));
}

/*
 * This is the "bigger hammer" version of xfs_flush_inode_work...
 * (IOW, "If at first you don't succeed, use a Bigger Hammer").
 */
STATIC void
xfs_flush_device_work(
	struct xfs_mount *mp,
	void		*arg)
{
	struct inode	*inode = arg;
	sync_blockdev(mp->m_super->s_bdev);
	iput(inode);
}

void
xfs_flush_device(
	xfs_inode_t	*ip)
{
	struct inode	*inode = vn_to_inode(XFS_ITOV(ip));

	igrab(inode);
	xfs_syncd_queue_work(ip->i_mount, inode, xfs_flush_device_work);
	delay(msecs_to_jiffies(500));
	xfs_log_force(ip->i_mount, (xfs_lsn_t)0, XFS_LOG_FORCE|XFS_LOG_SYNC);
}

STATIC void
xfs_sync_worker(
	struct xfs_mount *mp,
	void		*unused)
{
	int		error;

	if (!(mp->m_flags & XFS_MOUNT_RDONLY))
		error = xfs_sync(mp, SYNC_FSDATA | SYNC_BDFLUSH | SYNC_ATTR);
	mp->m_sync_seq++;
	wake_up(&mp->m_wait_single_sync_task);
}

STATIC int
xfssyncd(
	void			*arg)
{
	struct xfs_mount	*mp = arg;
	long			timeleft;
	bhv_vfs_sync_work_t	*work, *n;
	LIST_HEAD		(tmp);

	set_freezable();
	timeleft = xfs_syncd_centisecs * msecs_to_jiffies(10);
	for (;;) {
		timeleft = schedule_timeout_interruptible(timeleft);
		/* swsusp */
		try_to_freeze();
		if (kthread_should_stop() && list_empty(&mp->m_sync_list))
			break;

		spin_lock(&mp->m_sync_lock);
		/*
		 * We can get woken by laptop mode, to do a sync -
		 * that's the (only!) case where the list would be
		 * empty with time remaining.
		 */
		if (!timeleft || list_empty(&mp->m_sync_list)) {
			if (!timeleft)
				timeleft = xfs_syncd_centisecs *
							msecs_to_jiffies(10);
			INIT_LIST_HEAD(&mp->m_sync_work.w_list);
			list_add_tail(&mp->m_sync_work.w_list,
					&mp->m_sync_list);
		}
		list_for_each_entry_safe(work, n, &mp->m_sync_list, w_list)
			list_move(&work->w_list, &tmp);
		spin_unlock(&mp->m_sync_lock);

		list_for_each_entry_safe(work, n, &tmp, w_list) {
			(*work->w_syncer)(mp, work->w_data);
			list_del(&work->w_list);
			if (work == &mp->m_sync_work)
				continue;
			kmem_free(work, sizeof(struct bhv_vfs_sync_work));
		}
	}

	return 0;
}

STATIC void
xfs_fs_put_super(
	struct super_block	*sb)
{
	struct xfs_mount	*mp = XFS_M(sb);
	int			error;

	kthread_stop(mp->m_sync_task);

	xfs_sync(mp, SYNC_ATTR | SYNC_DELWRI);
	error = xfs_unmount(mp, 0, NULL);
	if (error)
		printk("XFS: unmount got error=%d\n", error);
}

STATIC void
xfs_fs_write_super(
	struct super_block	*sb)
{
	if (!(sb->s_flags & MS_RDONLY))
		xfs_sync(XFS_M(sb), SYNC_FSDATA);
	sb->s_dirt = 0;
}

STATIC int
xfs_fs_sync_super(
	struct super_block	*sb,
	int			wait)
{
	struct xfs_mount	*mp = XFS_M(sb);
	int			error;
	int			flags;

	/*
	 * Treat a sync operation like a freeze.  This is to work
	 * around a race in sync_inodes() which works in two phases
	 * - an asynchronous flush, which can write out an inode
	 * without waiting for file size updates to complete, and a
	 * synchronous flush, which wont do anything because the
	 * async flush removed the inode's dirty flag.  Also
	 * sync_inodes() will not see any files that just have
	 * outstanding transactions to be flushed because we don't
	 * dirty the Linux inode until after the transaction I/O
	 * completes.
	 */
	if (wait || unlikely(sb->s_frozen == SB_FREEZE_WRITE)) {
		/*
		 * First stage of freeze - no more writers will make progress
		 * now we are here, so we flush delwri and delalloc buffers
		 * here, then wait for all I/O to complete.  Data is frozen at
		 * that point. Metadata is not frozen, transactions can still
		 * occur here so don't bother flushing the buftarg (i.e
		 * SYNC_QUIESCE) because it'll just get dirty again.
		 */
		flags = SYNC_DATA_QUIESCE;
	} else
		flags = SYNC_FSDATA;

	error = xfs_sync(mp, flags);
	sb->s_dirt = 0;

	if (unlikely(laptop_mode)) {
		int	prev_sync_seq = mp->m_sync_seq;

		/*
		 * The disk must be active because we're syncing.
		 * We schedule xfssyncd now (now that the disk is
		 * active) instead of later (when it might not be).
		 */
		wake_up_process(mp->m_sync_task);
		/*
		 * We have to wait for the sync iteration to complete.
		 * If we don't, the disk activity caused by the sync
		 * will come after the sync is completed, and that
		 * triggers another sync from laptop mode.
		 */
		wait_event(mp->m_wait_single_sync_task,
				mp->m_sync_seq != prev_sync_seq);
	}

	return -error;
}

STATIC int
xfs_fs_statfs(
	struct dentry		*dentry,
	struct kstatfs		*statp)
{
	struct xfs_mount	*mp = XFS_M(dentry->d_sb);
	xfs_sb_t		*sbp = &mp->m_sb;
	__uint64_t		fakeinos, id;
	xfs_extlen_t		lsize;

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
#if XFS_BIG_INUMS
	fakeinos += mp->m_inoadd;
#endif
	statp->f_files =
	    MIN(sbp->sb_icount + fakeinos, (__uint64_t)XFS_MAXINUMBER);
	if (mp->m_maxicount)
#if XFS_BIG_INUMS
		if (!mp->m_inoadd)
#endif
			statp->f_files = min_t(typeof(statp->f_files),
						statp->f_files,
						mp->m_maxicount);
	statp->f_ffree = statp->f_files - (sbp->sb_icount - sbp->sb_ifree);
	spin_unlock(&mp->m_sb_lock);

	XFS_QM_DQSTATVFS(XFS_I(dentry->d_inode), statp);
	return 0;
}

STATIC int
xfs_fs_remount(
	struct super_block	*sb,
	int			*flags,
	char			*options)
{
	struct xfs_mount	*mp = XFS_M(sb);
	struct xfs_mount_args	*args = xfs_args_allocate(sb, 0);
	int			error;

	error = xfs_parseargs(mp, options, args, 1);
	if (!error)
		error = xfs_mntupdate(mp, flags, args);
	kmem_free(args, sizeof(*args));
	return -error;
}

/*
 * Second stage of a freeze. The data is already frozen so we only
 * need to take care of themetadata. Once that's done write a dummy
 * record to dirty the log in case of a crash while frozen.
 */
STATIC void
xfs_fs_lockfs(
	struct super_block	*sb)
{
	struct xfs_mount	*mp = XFS_M(sb);

	xfs_attr_quiesce(mp);
	xfs_fs_log_dummy(mp);
}

STATIC int
xfs_fs_show_options(
	struct seq_file		*m,
	struct vfsmount		*mnt)
{
	return -xfs_showargs(XFS_M(mnt->mnt_sb), m);
}

STATIC int
xfs_fs_quotasync(
	struct super_block	*sb,
	int			type)
{
	return -XFS_QM_QUOTACTL(XFS_M(sb), Q_XQUOTASYNC, 0, NULL);
}

STATIC int
xfs_fs_getxstate(
	struct super_block	*sb,
	struct fs_quota_stat	*fqs)
{
	return -XFS_QM_QUOTACTL(XFS_M(sb), Q_XGETQSTAT, 0, (caddr_t)fqs);
}

STATIC int
xfs_fs_setxstate(
	struct super_block	*sb,
	unsigned int		flags,
	int			op)
{
	return -XFS_QM_QUOTACTL(XFS_M(sb), op, 0, (caddr_t)&flags);
}

STATIC int
xfs_fs_getxquota(
	struct super_block	*sb,
	int			type,
	qid_t			id,
	struct fs_disk_quota	*fdq)
{
	return -XFS_QM_QUOTACTL(XFS_M(sb),
				 (type == USRQUOTA) ? Q_XGETQUOTA :
				  ((type == GRPQUOTA) ? Q_XGETGQUOTA :
				   Q_XGETPQUOTA), id, (caddr_t)fdq);
}

STATIC int
xfs_fs_setxquota(
	struct super_block	*sb,
	int			type,
	qid_t			id,
	struct fs_disk_quota	*fdq)
{
	return -XFS_QM_QUOTACTL(XFS_M(sb),
				 (type == USRQUOTA) ? Q_XSETQLIM :
				  ((type == GRPQUOTA) ? Q_XSETGQLIM :
				   Q_XSETPQLIM), id, (caddr_t)fdq);
}

STATIC int
xfs_fs_fill_super(
	struct super_block	*sb,
	void			*data,
	int			silent)
{
	struct inode		*root;
	struct xfs_mount	*mp = NULL;
	struct xfs_mount_args	*args = xfs_args_allocate(sb, silent);
	int			error;

	mp = xfs_mount_init();

	INIT_LIST_HEAD(&mp->m_sync_list);
	spin_lock_init(&mp->m_sync_lock);
	init_waitqueue_head(&mp->m_wait_single_sync_task);

	mp->m_super = sb;
	sb->s_fs_info = mp;

	if (sb->s_flags & MS_RDONLY)
		mp->m_flags |= XFS_MOUNT_RDONLY;

	error = xfs_parseargs(mp, (char *)data, args, 0);
	if (error)
		goto fail_vfsop;

	sb_min_blocksize(sb, BBSIZE);
	sb->s_export_op = &xfs_export_operations;
	sb->s_qcop = &xfs_quotactl_operations;
	sb->s_op = &xfs_super_operations;

	error = xfs_mount(mp, args, NULL);
	if (error)
		goto fail_vfsop;

	sb->s_dirt = 1;
	sb->s_magic = XFS_SB_MAGIC;
	sb->s_blocksize = mp->m_sb.sb_blocksize;
	sb->s_blocksize_bits = ffs(sb->s_blocksize) - 1;
	sb->s_maxbytes = xfs_max_file_offset(sb->s_blocksize_bits);
	sb->s_time_gran = 1;
	set_posix_acl_flag(sb);

	root = igrab(mp->m_rootip->i_vnode);
	if (!root) {
		error = ENOENT;
		goto fail_unmount;
	}
	if (is_bad_inode(root)) {
		error = EINVAL;
		goto fail_vnrele;
	}
	sb->s_root = d_alloc_root(root);
	if (!sb->s_root) {
		error = ENOMEM;
		goto fail_vnrele;
	}

	mp->m_sync_work.w_syncer = xfs_sync_worker;
	mp->m_sync_work.w_mount = mp;
	mp->m_sync_task = kthread_run(xfssyncd, mp, "xfssyncd");
	if (IS_ERR(mp->m_sync_task)) {
		error = -PTR_ERR(mp->m_sync_task);
		goto fail_vnrele;
	}

	xfs_itrace_exit(XFS_I(sb->s_root->d_inode));

	kmem_free(args, sizeof(*args));
	return 0;

fail_vnrele:
	if (sb->s_root) {
		dput(sb->s_root);
		sb->s_root = NULL;
	} else {
		iput(root);
	}

fail_unmount:
	xfs_unmount(mp, 0, NULL);

fail_vfsop:
	kmem_free(args, sizeof(*args));
	return -error;
}

STATIC int
xfs_fs_get_sb(
	struct file_system_type	*fs_type,
	int			flags,
	const char		*dev_name,
	void			*data,
	struct vfsmount		*mnt)
{
	return get_sb_bdev(fs_type, flags, dev_name, data, xfs_fs_fill_super,
			   mnt);
}

static struct super_operations xfs_super_operations = {
	.alloc_inode		= xfs_fs_alloc_inode,
	.destroy_inode		= xfs_fs_destroy_inode,
	.write_inode		= xfs_fs_write_inode,
	.clear_inode		= xfs_fs_clear_inode,
	.put_super		= xfs_fs_put_super,
	.write_super		= xfs_fs_write_super,
	.sync_fs		= xfs_fs_sync_super,
	.write_super_lockfs	= xfs_fs_lockfs,
	.statfs			= xfs_fs_statfs,
	.remount_fs		= xfs_fs_remount,
	.show_options		= xfs_fs_show_options,
};

static struct quotactl_ops xfs_quotactl_operations = {
	.quota_sync		= xfs_fs_quotasync,
	.get_xstate		= xfs_fs_getxstate,
	.set_xstate		= xfs_fs_setxstate,
	.get_xquota		= xfs_fs_getxquota,
	.set_xquota		= xfs_fs_setxquota,
};

static struct file_system_type xfs_fs_type = {
	.owner			= THIS_MODULE,
	.name			= "xfs",
	.get_sb			= xfs_fs_get_sb,
	.kill_sb		= kill_block_super,
	.fs_flags		= FS_REQUIRES_DEV,
};


STATIC int __init
init_xfs_fs( void )
{
	int			error;
	static char		message[] __initdata = KERN_INFO \
		XFS_VERSION_STRING " with " XFS_BUILD_OPTIONS " enabled\n";

	printk(message);

	ktrace_init(64);

	error = xfs_init_zones();
	if (error < 0)
		goto undo_zones;

	error = xfs_buf_init();
	if (error < 0)
		goto undo_buffers;

	vn_init();
	xfs_init();
	uuid_init();
	vfs_initquota();

	error = register_filesystem(&xfs_fs_type);
	if (error)
		goto undo_register;
	return 0;

undo_register:
	xfs_buf_terminate();

undo_buffers:
	xfs_destroy_zones();

undo_zones:
	return error;
}

STATIC void __exit
exit_xfs_fs( void )
{
	vfs_exitquota();
	unregister_filesystem(&xfs_fs_type);
	xfs_cleanup();
	xfs_buf_terminate();
	xfs_destroy_zones();
	ktrace_uninit();
}

module_init(init_xfs_fs);
module_exit(exit_xfs_fs);

MODULE_AUTHOR("Silicon Graphics, Inc.");
MODULE_DESCRIPTION(XFS_VERSION_STRING " with " XFS_BUILD_OPTIONS " enabled");
MODULE_LICENSE("GPL");
