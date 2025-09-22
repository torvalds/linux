/*	$OpenBSD: udf_extern.h,v 1.16 2025/07/07 00:55:15 jsg Exp $	*/

/*
 * Written by Pedro Martelletto <pedro@ambientworks.net> in February 2005.
 * Public domain.
 */

#ifdef _KERNEL

/*
 * udf_subr.c
 */
int udf_rawnametounicode(u_int len, char *, unicode_t *);
int udf_vat_get(struct umount *, uint32_t);
int udf_vat_map(struct umount *, uint32_t *);

/*
 * udf_vfsops.c
 */
int udf_init(struct vfsconf *);
int udf_mount(struct mount *, const char *, void *, struct nameidata *,
    struct proc *);
int udf_unmount(struct mount *, int, struct proc *);
int udf_start(struct mount *, int, struct proc *);
int udf_root(struct mount *, struct vnode **);
int udf_quotactl(struct mount *, int, uid_t, caddr_t, struct proc *);
int udf_statfs(struct mount *, struct statfs *, struct proc *);
int udf_vget(struct mount *, ino_t, struct vnode **);
int udf_sync(struct mount *, int, int, struct ucred *, struct proc *);
int udf_checkexp(struct mount *, struct mbuf *, int *, struct ucred **);
int udf_fhtovp(struct mount *, struct fid *, struct vnode **);
int udf_vptofh(struct vnode *, struct fid *);

/*
 * udf_vnops.c
 */
int udf_access(void *v);
int udf_getattr(void *v);
int udf_open(void *v);
int udf_close(void *v);
int udf_ioctl(void *v);
int udf_read(void *v);
int udf_readdir(void *v);
int udf_readlink(void *v);
int udf_strategy(void *v);
int udf_bmap(void *v);
int udf_lookup(void *v);
int udf_inactive(void *v);
int udf_reclaim(void *v);
int udf_lock(void *v);
int udf_unlock(void *v);
int udf_pathconf(void *);
int udf_islocked(void *v);
int udf_print(void *v);
int udf_transname(char *, char *, int, struct umount *);
int udf_readatoffset(struct unode *, int *, off_t, struct buf **,
    uint8_t **);

/*
 * Memory pools.
 */
extern struct pool udf_trans_pool;
extern struct pool unode_pool;
extern struct pool udf_ds_pool;

/* Set of UDF vnode operations.*/
extern const struct vops udf_vops;

#endif /* _KERNEL */
