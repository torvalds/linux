// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_LINUX__
#define __XFS_LINUX__

#include <linux/types.h>
#include <linux/uuid.h>

/*
 * Kernel specific type declarations for XFS
 */

typedef __s64			xfs_off_t;	/* <file offset> type */
typedef unsigned long long	xfs_ino_t;	/* <inode> type */
typedef __s64			xfs_daddr_t;	/* <disk address> type */
typedef __u32			xfs_dev_t;
typedef __u32			xfs_nlink_t;

#include "xfs_types.h"

#include "kmem.h"
#include "mrlock.h"

#include <linux/semaphore.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/crc32c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/file.h>
#include <linux/swap.h>
#include <linux/errno.h>
#include <linux/sched/signal.h>
#include <linux/bitops.h>
#include <linux/major.h>
#include <linux/pagemap.h>
#include <linux/vfs.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/proc_fs.h>
#include <linux/sort.h>
#include <linux/cpu.h>
#include <linux/notifier.h>
#include <linux/delay.h>
#include <linux/log2.h>
#include <linux/spinlock.h>
#include <linux/random.h>
#include <linux/ctype.h>
#include <linux/writeback.h>
#include <linux/capability.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/list_sort.h>
#include <linux/ratelimit.h>
#include <linux/rhashtable.h>

#include <asm/page.h>
#include <asm/div64.h>
#include <asm/param.h>
#include <linux/uaccess.h>
#include <asm/byteorder.h>
#include <asm/unaligned.h>

#include "xfs_fs.h"
#include "xfs_stats.h"
#include "xfs_sysctl.h"
#include "xfs_iops.h"
#include "xfs_aops.h"
#include "xfs_super.h"
#include "xfs_cksum.h"
#include "xfs_buf.h"
#include "xfs_message.h"

#ifdef __BIG_ENDIAN
#define XFS_NATIVE_HOST 1
#else
#undef XFS_NATIVE_HOST
#endif

#define irix_sgid_inherit	xfs_params.sgid_inherit.val
#define irix_symlink_mode	xfs_params.symlink_mode.val
#define xfs_panic_mask		xfs_params.panic_mask.val
#define xfs_error_level		xfs_params.error_level.val
#define xfs_syncd_centisecs	xfs_params.syncd_timer.val
#define xfs_stats_clear		xfs_params.stats_clear.val
#define xfs_inherit_sync	xfs_params.inherit_sync.val
#define xfs_inherit_nodump	xfs_params.inherit_nodump.val
#define xfs_inherit_noatime	xfs_params.inherit_noatim.val
#define xfs_inherit_nosymlinks	xfs_params.inherit_nosym.val
#define xfs_rotorstep		xfs_params.rotorstep.val
#define xfs_inherit_nodefrag	xfs_params.inherit_nodfrg.val
#define xfs_fstrm_centisecs	xfs_params.fstrm_timer.val
#define xfs_eofb_secs		xfs_params.eofb_timer.val
#define xfs_cowb_secs		xfs_params.cowb_timer.val

#define current_cpu()		(raw_smp_processor_id())
#define current_pid()		(current->pid)
#define current_test_flags(f)	(current->flags & (f))
#define current_set_flags_nested(sp, f)		\
		(*(sp) = current->flags, current->flags |= (f))
#define current_clear_flags_nested(sp, f)	\
		(*(sp) = current->flags, current->flags &= ~(f))
#define current_restore_flags_nested(sp, f)	\
		(current->flags = ((current->flags & ~(f)) | (*(sp) & (f))))

#define NBBY		8		/* number of bits per byte */

/*
 * Size of block device i/o is parameterized here.
 * Currently the system supports page-sized i/o.
 */
#define	BLKDEV_IOSHIFT		PAGE_SHIFT
#define	BLKDEV_IOSIZE		(1<<BLKDEV_IOSHIFT)
/* number of BB's per block device block */
#define	BLKDEV_BB		BTOBB(BLKDEV_IOSIZE)

#define ENOATTR		ENODATA		/* Attribute not found */
#define EWRONGFS	EINVAL		/* Mount with wrong filesystem type */
#define EFSCORRUPTED	EUCLEAN		/* Filesystem is corrupted */
#define EFSBADCRC	EBADMSG		/* Bad CRC detected */

#define SYNCHRONIZE()	barrier()
#define __return_address __builtin_return_address(0)

/*
 * Return the address of a label.  Use barrier() so that the optimizer
 * won't reorder code to refactor the error jumpouts into a single
 * return, which throws off the reported address.
 */
#define __this_address	({ __label__ __here; __here: barrier(); &&__here; })

#define XFS_PROJID_DEFAULT	0

#define howmany(x, y)	(((x)+((y)-1))/(y))

static inline void delay(long ticks)
{
	schedule_timeout_uninterruptible(ticks);
}

/*
 * XFS wrapper structure for sysfs support. It depends on external data
 * structures and is embedded in various internal data structures to implement
 * the XFS sysfs object heirarchy. Define it here for broad access throughout
 * the codebase.
 */
struct xfs_kobj {
	struct kobject		kobject;
	struct completion	complete;
};

struct xstats {
	struct xfsstats __percpu	*xs_stats;
	struct xfs_kobj			xs_kobj;
};

extern struct xstats xfsstats;

/* Kernel uid/gid conversion. These are used to convert to/from the on disk
 * uid_t/gid_t types to the kuid_t/kgid_t types that the kernel uses internally.
 * The conversion here is type only, the value will remain the same since we
 * are converting to the init_user_ns. The uid is later mapped to a particular
 * user namespace value when crossing the kernel/user boundary.
 */
static inline uint32_t xfs_kuid_to_uid(kuid_t uid)
{
	return from_kuid(&init_user_ns, uid);
}

static inline kuid_t xfs_uid_to_kuid(uint32_t uid)
{
	return make_kuid(&init_user_ns, uid);
}

static inline uint32_t xfs_kgid_to_gid(kgid_t gid)
{
	return from_kgid(&init_user_ns, gid);
}

static inline kgid_t xfs_gid_to_kgid(uint32_t gid)
{
	return make_kgid(&init_user_ns, gid);
}

static inline dev_t xfs_to_linux_dev_t(xfs_dev_t dev)
{
	return MKDEV(sysv_major(dev) & 0x1ff, sysv_minor(dev));
}

static inline xfs_dev_t linux_to_xfs_dev_t(dev_t dev)
{
	return sysv_encode_dev(dev);
}

/*
 * Various platform dependent calls that don't fit anywhere else
 */
#define xfs_sort(a,n,s,fn)	sort(a,n,s,fn,NULL)
#define xfs_stack_trace()	dump_stack()

static inline uint64_t roundup_64(uint64_t x, uint32_t y)
{
	x += y - 1;
	do_div(x, y);
	return x * y;
}

static inline uint64_t howmany_64(uint64_t x, uint32_t y)
{
	x += y - 1;
	do_div(x, y);
	return x;
}

int xfs_rw_bdev(struct block_device *bdev, sector_t sector, unsigned int count,
		char *data, unsigned int op);

#define ASSERT_ALWAYS(expr)	\
	(likely(expr) ? (void)0 : assfail(NULL, #expr, __FILE__, __LINE__))

#ifdef DEBUG
#define ASSERT(expr)	\
	(likely(expr) ? (void)0 : assfail(NULL, #expr, __FILE__, __LINE__))

#else	/* !DEBUG */

#ifdef XFS_WARN

#define ASSERT(expr)	\
	(likely(expr) ? (void)0 : asswarn(NULL, #expr, __FILE__, __LINE__))

#else	/* !DEBUG && !XFS_WARN */

#define ASSERT(expr)		((void)0)

#endif /* XFS_WARN */
#endif /* DEBUG */

#define XFS_IS_CORRUPT(mp, expr)	\
	(unlikely(expr) ? xfs_corruption_error(#expr, XFS_ERRLEVEL_LOW, (mp), \
					       NULL, 0, __FILE__, __LINE__, \
					       __this_address), \
			  true : false)

#define STATIC static noinline

#ifdef CONFIG_XFS_RT

/*
 * make sure we ignore the inode flag if the filesystem doesn't have a
 * configured realtime device.
 */
#define XFS_IS_REALTIME_INODE(ip)			\
	(((ip)->i_d.di_flags & XFS_DIFLAG_REALTIME) &&	\
	 (ip)->i_mount->m_rtdev_targp)
#define XFS_IS_REALTIME_MOUNT(mp) ((mp)->m_rtdev_targp ? 1 : 0)
#else
#define XFS_IS_REALTIME_INODE(ip) (0)
#define XFS_IS_REALTIME_MOUNT(mp) (0)
#endif

/*
 * Starting in Linux 4.15, the %p (raw pointer value) printk modifier
 * prints a hashed version of the pointer to avoid leaking kernel
 * pointers into dmesg.  If we're trying to debug the kernel we want the
 * raw values, so override this behavior as best we can.
 */
#ifdef DEBUG
# define PTR_FMT "%px"
#else
# define PTR_FMT "%p"
#endif

#endif /* __XFS_LINUX__ */
