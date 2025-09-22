/*	$OpenBSD: ufs_quota_stub.c,v 1.8 2015/03/14 03:38:53 jsg Exp $	*/

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/namei.h>
#include <sys/malloc.h>
#include <sys/vnode.h>
#include <sys/mount.h>

#include <ufs/ufs/quota.h>
#include <ufs/ufs/inode.h>
#include <ufs/ufs/ufsmount.h>
#include <ufs/ufs/ufs_extern.h>

#ifndef QUOTA

int
getinoquota(struct inode *ip) {
	return (0);
}

int
ufs_quota_alloc_blocks2(struct inode *ip, daddr_t change, 
    struct ucred *cred, enum ufs_quota_flags flags) {
	return (0);
}

int
ufs_quota_free_blocks2(struct inode *ip, daddr_t change, 
    struct ucred *cred, enum ufs_quota_flags flags) {
	return (0);
}

int
ufs_quota_alloc_inode2(struct inode *ip, struct ucred *cred,
    enum ufs_quota_flags flags) {
	return (0);
}

int
ufs_quota_free_inode2(struct inode *ip, struct ucred *cred,
    enum ufs_quota_flags flags) {
	return (0);
}

int
quotaoff(struct proc *p, struct mount *mp, int flags) {
	return (0);
}

int
qsync(struct mount *mp) {
	return (0);
}

int
ufs_quotactl(struct mount *mp, int a, uid_t u, caddr_t addr, struct proc *p) {
	return (EOPNOTSUPP);
}

void
ufs_quota_init(void) {
}

int
ufs_quota_delete(struct inode *ip) {
	return (0);
}

#endif
