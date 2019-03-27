/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Stanislav Sedov <stas@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_COMMON_KVM_H_
#define	_COMMON_KVM_H_

dev_t	dev2udev(kvm_t *kd, struct cdev *dev);
int	kdevtoname(kvm_t *kd, struct cdev *dev, char *);
int	kvm_read_all(kvm_t *kd, unsigned long addr, void *buf,
    size_t nbytes);

/*
 * Filesystems specific access routines.
 */
int	devfs_filestat(kvm_t *kd, struct vnode *vp, struct vnstat *vn);
int	isofs_filestat(kvm_t *kd, struct vnode *vp, struct vnstat *vn);
int	msdosfs_filestat(kvm_t *kd, struct vnode *vp, struct vnstat *vn);
int	nfs_filestat(kvm_t *kd, struct vnode *vp, struct vnstat *vn);
int	smbfs_filestat(kvm_t *kd, struct vnode *vp, struct vnstat *vn);
int	udf_filestat(kvm_t *kd, struct vnode *vp, struct vnstat *vn);
int	ufs_filestat(kvm_t *kd, struct vnode *vp, struct vnstat *vn);
int	zfs_filestat(kvm_t *kd, struct vnode *vp, struct vnstat *vn);
void	*getvnodedata(struct vnode *vp);
struct mount	*getvnodemount(struct vnode *vp);

#endif	/* _COMMON_KVM_H_ */
