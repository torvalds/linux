#ifndef FS_CEPH_IOCTL_H
#define FS_CEPH_IOCTL_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define CEPH_IOCTL_MAGIC 0x97

/* just use u64 to align sanely on all archs */
struct ceph_ioctl_layout {
	__u64 stripe_unit, stripe_count, object_size;
	__u64 data_pool;
	__s64 preferred_osd;
};

#define CEPH_IOC_GET_LAYOUT _IOR(CEPH_IOCTL_MAGIC, 1,		\
				   struct ceph_ioctl_layout)
#define CEPH_IOC_SET_LAYOUT _IOW(CEPH_IOCTL_MAGIC, 2,		\
				   struct ceph_ioctl_layout)
#define CEPH_IOC_SET_LAYOUT_POLICY _IOW(CEPH_IOCTL_MAGIC, 5,	\
				   struct ceph_ioctl_layout)

/*
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

#define CEPH_IOC_LAZYIO _IO(CEPH_IOCTL_MAGIC, 4)
#define CEPH_IOC_SYNCIO _IO(CEPH_IOCTL_MAGIC, 5)

#endif
