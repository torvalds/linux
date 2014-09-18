#ifndef FS_CEPH_IOCTL_H
#define FS_CEPH_IOCTL_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define CEPH_IOCTL_MAGIC 0x97

/*
 * CEPH_IOC_GET_LAYOUT - get file layout or dir layout policy
 * CEPH_IOC_SET_LAYOUT - set file layout
 * CEPH_IOC_SET_LAYOUT_POLICY - set dir layout policy
 *
 * The file layout specifies how file data is striped over objects in
 * the distributed object store, which object pool they belong to (if
 * it differs from the default), and an optional 'preferred osd' to
 * store them on.
 *
 * Files get a new layout based on the policy set on the containing
 * directory or one of its ancestors.  The GET_LAYOUT ioctl will let
 * you examine the layout for a file or the policy on a directory.
 *
 * SET_LAYOUT will let you set a layout on a newly created file.  This
 * only works immediately after the file is created and before any
 * data is written to it.
 *
 * SET_LAYOUT_POLICY will let you set a layout policy (default layout)
 * on a directory that will apply to any new files created in that
 * directory (or any child directory that doesn't specify a layout of
 * its own).
 */

/* use u64 to align sanely on all archs */
struct ceph_ioctl_layout {
	__u64 stripe_unit, stripe_count, object_size;
	__u64 data_pool;

	/* obsolete.  new values ignored, always return -1 */
	__s64 preferred_osd;
};

#define CEPH_IOC_GET_LAYOUT _IOR(CEPH_IOCTL_MAGIC, 1,		\
				   struct ceph_ioctl_layout)
#define CEPH_IOC_SET_LAYOUT _IOW(CEPH_IOCTL_MAGIC, 2,		\
				   struct ceph_ioctl_layout)
#define CEPH_IOC_SET_LAYOUT_POLICY _IOW(CEPH_IOCTL_MAGIC, 5,	\
				   struct ceph_ioctl_layout)

/*
 * CEPH_IOC_GET_DATALOC - get location of file data in the cluster
 *
 * Extract identity, address of the OSD and object storing a given
 * file offset.
 */
struct ceph_ioctl_dataloc {
	__u64 file_offset;           /* in+out: file offset */
	__u64 object_offset;         /* out: offset in object */
	__u64 object_no;             /* out: object # */
	__u64 object_size;           /* out: object size */
	char object_name[64];        /* out: object name */
	__u64 block_offset;          /* out: offset in block */
	__u64 block_size;            /* out: block length */
	__s64 osd;                   /* out: osd # */
	struct sockaddr_storage osd_addr; /* out: osd address */
};

#define CEPH_IOC_GET_DATALOC _IOWR(CEPH_IOCTL_MAGIC, 3,	\
				   struct ceph_ioctl_dataloc)

/*
 * CEPH_IOC_LAZYIO - relax consistency
 *
 * Normally Ceph switches to synchronous IO when multiple clients have
 * the file open (and or more for write).  Reads and writes bypass the
 * page cache and go directly to the OSD.  Setting this flag on a file
 * descriptor will allow buffered IO for this file in cases where the
 * application knows it won't interfere with other nodes (or doesn't
 * care).
 */
#define CEPH_IOC_LAZYIO _IO(CEPH_IOCTL_MAGIC, 4)

/*
 * CEPH_IOC_SYNCIO - force synchronous IO
 *
 * This ioctl sets a file flag that forces the synchronous IO that
 * bypasses the page cache, even if it is not necessary.  This is
 * essentially the opposite behavior of IOC_LAZYIO.  This forces the
 * same read/write path as a file opened by multiple clients when one
 * or more of those clients is opened for write.
 *
 * Note that this type of sync IO takes a different path than a file
 * opened with O_SYNC/D_SYNC (writes hit the page cache and are
 * immediately flushed on page boundaries).  It is very similar to
 * O_DIRECT (writes bypass the page cache) excep that O_DIRECT writes
 * are not copied (user page must remain stable) and O_DIRECT writes
 * have alignment restrictions (on the buffer and file offset).
 */
#define CEPH_IOC_SYNCIO _IO(CEPH_IOCTL_MAGIC, 5)

#endif
