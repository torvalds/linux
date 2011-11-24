#include <linux/mount.h>

static inline int mnt_has_parent(struct vfsmount *mnt)
{
	return mnt != mnt->mnt_parent;
}
