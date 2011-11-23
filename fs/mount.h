#include <linux/mount.h>

struct mount {
	struct vfsmount mnt;
};

static inline struct mount *real_mount(struct vfsmount *mnt)
{
	return container_of(mnt, struct mount, mnt);
}

static inline int mnt_has_parent(struct vfsmount *mnt)
{
	return mnt != mnt->mnt_parent;
}
