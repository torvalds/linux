#include <linux/mount.h>

struct mount {
	struct list_head mnt_hash;
	struct vfsmount *mnt_parent;
	struct vfsmount mnt;
};

static inline struct mount *real_mount(struct vfsmount *mnt)
{
	return container_of(mnt, struct mount, mnt);
}

static inline int mnt_has_parent(struct mount *mnt)
{
	return &mnt->mnt != mnt->mnt_parent;
}

extern struct mount *__lookup_mnt(struct vfsmount *, struct dentry *, int);
