#ifndef _LINUX_DEVFS_FS_H
#define _LINUX_DEVFS_FS_H

#include <linux/ioctl.h>

#define DEVFSD_PROTOCOL_REVISION_KERNEL  5

#define	DEVFSD_IOCTL_BASE	'd'

/*  These are the various ioctls  */
#define DEVFSDIOC_GET_PROTO_REV         _IOR(DEVFSD_IOCTL_BASE, 0, int)
#define DEVFSDIOC_SET_EVENT_MASK        _IOW(DEVFSD_IOCTL_BASE, 2, int)
#define DEVFSDIOC_RELEASE_EVENT_QUEUE   _IOW(DEVFSD_IOCTL_BASE, 3, int)
#define DEVFSDIOC_SET_DEBUG_MASK        _IOW(DEVFSD_IOCTL_BASE, 4, int)

#define DEVFSD_NOTIFY_REGISTERED    0
#define DEVFSD_NOTIFY_UNREGISTERED  1
#define DEVFSD_NOTIFY_ASYNC_OPEN    2
#define DEVFSD_NOTIFY_CLOSE         3
#define DEVFSD_NOTIFY_LOOKUP        4
#define DEVFSD_NOTIFY_CHANGE        5
#define DEVFSD_NOTIFY_CREATE        6
#define DEVFSD_NOTIFY_DELETE        7

#define DEVFS_PATHLEN               1024	/*  Never change this otherwise the
						   binary interface will change   */

struct devfsd_notify_struct {	/*  Use native C types to ensure same types in kernel and user space     */
	unsigned int type;	/*  DEVFSD_NOTIFY_* value                   */
	unsigned int mode;	/*  Mode of the inode or device entry       */
	unsigned int major;	/*  Major number of device entry            */
	unsigned int minor;	/*  Minor number of device entry            */
	unsigned int uid;	/*  Uid of process, inode or device entry   */
	unsigned int gid;	/*  Gid of process, inode or device entry   */
	unsigned int overrun_count;	/*  Number of lost events                   */
	unsigned int namelen;	/*  Number of characters not including '\0' */
	/*  The device name MUST come last                                       */
	char devname[DEVFS_PATHLEN];	/*  This will be '\0' terminated            */
};

#endif				/*  _LINUX_DEVFS_FS_H  */
