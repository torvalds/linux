/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
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
#ifndef __XFS_DMAPI_H__
#define __XFS_DMAPI_H__

#include <linux/version.h>
/*	Values used to define the on-disk version of dm_attrname_t. All
 *	on-disk attribute names start with the 8-byte string "SGI_DMI_".
 *
 *      In the on-disk inode, DMAPI attribute names consist of the user-provided
 *      name with the DMATTR_PREFIXSTRING pre-pended.  This string must NEVER be
 *      changed.
 */

#define DMATTR_PREFIXLEN	8
#define DMATTR_PREFIXSTRING	"SGI_DMI_"

typedef enum {
	DM_EVENT_INVALID	= -1,
	DM_EVENT_CANCEL		= 0,		/* not supported */
	DM_EVENT_MOUNT		= 1,
	DM_EVENT_PREUNMOUNT	= 2,
	DM_EVENT_UNMOUNT	= 3,
	DM_EVENT_DEBUT		= 4,		/* not supported */
	DM_EVENT_CREATE		= 5,
	DM_EVENT_CLOSE		= 6,		/* not supported */
	DM_EVENT_POSTCREATE	= 7,
	DM_EVENT_REMOVE		= 8,
	DM_EVENT_POSTREMOVE	= 9,
	DM_EVENT_RENAME		= 10,
	DM_EVENT_POSTRENAME	= 11,
	DM_EVENT_LINK		= 12,
	DM_EVENT_POSTLINK	= 13,
	DM_EVENT_SYMLINK	= 14,
	DM_EVENT_POSTSYMLINK	= 15,
	DM_EVENT_READ		= 16,
	DM_EVENT_WRITE		= 17,
	DM_EVENT_TRUNCATE	= 18,
	DM_EVENT_ATTRIBUTE	= 19,
	DM_EVENT_DESTROY	= 20,
	DM_EVENT_NOSPACE	= 21,
	DM_EVENT_USER		= 22,
	DM_EVENT_MAX		= 23
} dm_eventtype_t;
#define HAVE_DM_EVENTTYPE_T

typedef enum {
	DM_RIGHT_NULL,
	DM_RIGHT_SHARED,
	DM_RIGHT_EXCL
} dm_right_t;
#define HAVE_DM_RIGHT_T

/* Defines for determining if an event message should be sent. */
#define	DM_EVENT_ENABLED(vfsp, ip, event) ( \
	unlikely ((vfsp)->vfs_flag & VFS_DMI) && \
		( ((ip)->i_d.di_dmevmask & (1 << event)) || \
		  ((ip)->i_mount->m_dmevmask & (1 << event)) ) \
	)

#define	DM_EVENT_ENABLED_IO(vfsp, io, event) ( \
	unlikely ((vfsp)->vfs_flag & VFS_DMI) && \
		( ((io)->io_dmevmask & (1 << event)) || \
		  ((io)->io_mount->m_dmevmask & (1 << event)) ) \
	)

#define DM_XFS_VALID_FS_EVENTS		( \
	(1 << DM_EVENT_PREUNMOUNT)	| \
	(1 << DM_EVENT_UNMOUNT)		| \
	(1 << DM_EVENT_NOSPACE)		| \
	(1 << DM_EVENT_DEBUT)		| \
	(1 << DM_EVENT_CREATE)		| \
	(1 << DM_EVENT_POSTCREATE)	| \
	(1 << DM_EVENT_REMOVE)		| \
	(1 << DM_EVENT_POSTREMOVE)	| \
	(1 << DM_EVENT_RENAME)		| \
	(1 << DM_EVENT_POSTRENAME)	| \
	(1 << DM_EVENT_LINK)		| \
	(1 << DM_EVENT_POSTLINK)	| \
	(1 << DM_EVENT_SYMLINK)		| \
	(1 << DM_EVENT_POSTSYMLINK)	| \
	(1 << DM_EVENT_ATTRIBUTE)	| \
	(1 << DM_EVENT_DESTROY)		)

/* Events valid in dm_set_eventlist() when called with a file handle for
   a regular file or a symlink.  These events are persistent.
*/

#define	DM_XFS_VALID_FILE_EVENTS	( \
	(1 << DM_EVENT_ATTRIBUTE)	| \
	(1 << DM_EVENT_DESTROY)		)

/* Events valid in dm_set_eventlist() when called with a file handle for
   a directory.  These events are persistent.
*/

#define	DM_XFS_VALID_DIRECTORY_EVENTS	( \
	(1 << DM_EVENT_CREATE)		| \
	(1 << DM_EVENT_POSTCREATE)	| \
	(1 << DM_EVENT_REMOVE)		| \
	(1 << DM_EVENT_POSTREMOVE)	| \
	(1 << DM_EVENT_RENAME)		| \
	(1 << DM_EVENT_POSTRENAME)	| \
	(1 << DM_EVENT_LINK)		| \
	(1 << DM_EVENT_POSTLINK)	| \
	(1 << DM_EVENT_SYMLINK)		| \
	(1 << DM_EVENT_POSTSYMLINK)	| \
	(1 << DM_EVENT_ATTRIBUTE)	| \
	(1 << DM_EVENT_DESTROY)		)

/* Events supported by the XFS filesystem. */
#define	DM_XFS_SUPPORTED_EVENTS		( \
	(1 << DM_EVENT_MOUNT)		| \
	(1 << DM_EVENT_PREUNMOUNT)	| \
	(1 << DM_EVENT_UNMOUNT)		| \
	(1 << DM_EVENT_NOSPACE)		| \
	(1 << DM_EVENT_CREATE)		| \
	(1 << DM_EVENT_POSTCREATE)	| \
	(1 << DM_EVENT_REMOVE)		| \
	(1 << DM_EVENT_POSTREMOVE)	| \
	(1 << DM_EVENT_RENAME)		| \
	(1 << DM_EVENT_POSTRENAME)	| \
	(1 << DM_EVENT_LINK)		| \
	(1 << DM_EVENT_POSTLINK)	| \
	(1 << DM_EVENT_SYMLINK)		| \
	(1 << DM_EVENT_POSTSYMLINK)	| \
	(1 << DM_EVENT_READ)		| \
	(1 << DM_EVENT_WRITE)		| \
	(1 << DM_EVENT_TRUNCATE)	| \
	(1 << DM_EVENT_ATTRIBUTE)	| \
	(1 << DM_EVENT_DESTROY)		)


/*
 *	Definitions used for the flags field on dm_send_*_event().
 */

#define DM_FLAGS_NDELAY		0x001	/* return EAGAIN after dm_pending() */
#define DM_FLAGS_UNWANTED	0x002	/* event not in fsys dm_eventset_t */
#define DM_FLAGS_IMUX		0x004	/* thread holds i_mutex */
#define DM_FLAGS_IALLOCSEM_RD	0x010	/* thread holds i_alloc_sem rd */
#define DM_FLAGS_IALLOCSEM_WR	0x020	/* thread holds i_alloc_sem wr */

/*
 *	Based on IO_ISDIRECT, decide which i_ flag is set.
 */
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0)
#define DM_SEM_FLAG_RD(ioflags) (((ioflags) & IO_ISDIRECT) ? \
			      DM_FLAGS_IMUX : 0)
#define DM_SEM_FLAG_WR	(DM_FLAGS_IALLOCSEM_WR | DM_FLAGS_IMUX)
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)) && \
    (LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,22))
#define DM_SEM_FLAG_RD(ioflags) (((ioflags) & IO_ISDIRECT) ? \
			      DM_FLAGS_IALLOCSEM_RD : DM_FLAGS_IMUX)
#define DM_SEM_FLAG_WR	(DM_FLAGS_IALLOCSEM_WR | DM_FLAGS_IMUX)
#endif

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,4,21)
#define DM_SEM_FLAG_RD(ioflags) (((ioflags) & IO_ISDIRECT) ? \
			      0 : DM_FLAGS_IMUX)
#define DM_SEM_FLAG_WR	(DM_FLAGS_IMUX)
#endif


/*
 *	Macros to turn caller specified delay/block flags into
 *	dm_send_xxxx_event flag DM_FLAGS_NDELAY.
 */

#define FILP_DELAY_FLAG(filp) ((filp->f_flags&(O_NDELAY|O_NONBLOCK)) ? \
			DM_FLAGS_NDELAY : 0)
#define AT_DELAY_FLAG(f) ((f&ATTR_NONBLOCK) ? DM_FLAGS_NDELAY : 0)


extern struct bhv_vfsops xfs_dmops;

#endif  /* __XFS_DMAPI_H__ */
