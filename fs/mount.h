#include <linux/mount.h>

struct mnt_pcp {
	int mnt_count;
	int mnt_writers;
};

struct mount {
	struct list_head mnt_hash;
	struct mount *mnt_parent;
	struct dentry *mnt_mountpoint;
	struct vfsmount mnt;
#ifdef CONFIG_SMP
	struct mnt_pcp __percpu *mnt_pcp;
	atomic_t mnt_longterm;		/* how many of the refs are longterm */
#else
	int mnt_count;
	int mnt_writers;
#endif
};

static inline struct mount *real_mount(struct vfsmount *mnt)
{
	return container_of(mnt, struct mount, mnt);
}

static inline int mnt_has_parent(struct mount *mnt)
{
	return mnt != mnt->mnt_parent;
}

extern struct mount *__lookup_mnt(struct vfsmount *, struct dentry *, int);
