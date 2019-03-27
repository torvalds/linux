/*-
 * Copyright (c) 2007 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/kthread.h>
#include <sys/namei.h>
#include <sys/proc.h>
#include <sys/filedesc.h>
#include <sys/fcntl.h>
#include <sys/linker.h>
#include <sys/kobj.h>

void
kobj_free(void *address, size_t size)
{

	kmem_free(address, size);
}

void *
kobj_alloc(size_t size, int flag)
{

	return (kmem_alloc(size, (flag & KM_NOWAIT) ? KM_NOSLEEP : KM_SLEEP));
}

void *
kobj_zalloc(size_t size, int flag)
{
	void *p;

	if ((p = kobj_alloc(size, flag)) != NULL)
		bzero(p, size);
	return (p);
}

static void *
kobj_open_file_vnode(const char *file)
{
	struct thread *td = curthread;
	struct nameidata nd;
	int error, flags;

	pwd_ensure_dirs();

	flags = FREAD | O_NOFOLLOW;
	NDINIT(&nd, LOOKUP, 0, UIO_SYSSPACE, file, td);
	error = vn_open_cred(&nd, &flags, 0, 0, curthread->td_ucred, NULL);
	if (error != 0)
		return (NULL);
	NDFREE(&nd, NDF_ONLY_PNBUF);
	/* We just unlock so we hold a reference. */
	VOP_UNLOCK(nd.ni_vp, 0);
	return (nd.ni_vp);
}

static void *
kobj_open_file_loader(const char *file)
{

	return (preload_search_by_name(file));
}

struct _buf *
kobj_open_file(const char *file)
{
	struct _buf *out;

	out = kmem_alloc(sizeof(*out), KM_SLEEP);
	out->mounted = root_mounted();
	/*
	 * If root is already mounted we read file using file system,
	 * if not, we use loader.
	 */
	if (out->mounted)
		out->ptr = kobj_open_file_vnode(file);
	else
		out->ptr = kobj_open_file_loader(file);
	if (out->ptr == NULL) {
		kmem_free(out, sizeof(*out));
		return ((struct _buf *)-1);
	}
	return (out);
}

static int
kobj_get_filesize_vnode(struct _buf *file, uint64_t *size)
{
	struct vnode *vp = file->ptr;
	struct vattr va;
	int error;

	vn_lock(vp, LK_SHARED | LK_RETRY);
	error = VOP_GETATTR(vp, &va, curthread->td_ucred);
	VOP_UNLOCK(vp, 0);
	if (error == 0)
		*size = (uint64_t)va.va_size;
	return (error);
}

static int
kobj_get_filesize_loader(struct _buf *file, uint64_t *size)
{
	void *ptr;

	ptr = preload_search_info(file->ptr, MODINFO_SIZE);
	if (ptr == NULL)
		return (ENOENT);
	*size = (uint64_t)*(size_t *)ptr;
	return (0);
}

int
kobj_get_filesize(struct _buf *file, uint64_t *size)
{

	if (file->mounted)
		return (kobj_get_filesize_vnode(file, size));
	else
		return (kobj_get_filesize_loader(file, size));
}

int
kobj_read_file_vnode(struct _buf *file, char *buf, unsigned size, unsigned off)
{
	struct vnode *vp = file->ptr;
	struct thread *td = curthread;
	struct uio auio;
	struct iovec aiov;
	int error;

	bzero(&aiov, sizeof(aiov));
	bzero(&auio, sizeof(auio));

	aiov.iov_base = buf;
	aiov.iov_len = size;

	auio.uio_iov = &aiov;
	auio.uio_offset = (off_t)off;
	auio.uio_segflg = UIO_SYSSPACE;
	auio.uio_rw = UIO_READ;
	auio.uio_iovcnt = 1;
	auio.uio_resid = size;
	auio.uio_td = td;

	vn_lock(vp, LK_SHARED | LK_RETRY);
	error = VOP_READ(vp, &auio, IO_UNIT | IO_SYNC, td->td_ucred);
	VOP_UNLOCK(vp, 0);
	return (error != 0 ? -1 : size - auio.uio_resid);
}

int
kobj_read_file_loader(struct _buf *file, char *buf, unsigned size, unsigned off)
{
	char *ptr;

	ptr = preload_fetch_addr(file->ptr);
	if (ptr == NULL)
		return (ENOENT);
	bcopy(ptr + off, buf, size);
	return (0);
}

int
kobj_read_file(struct _buf *file, char *buf, unsigned size, unsigned off)
{

	if (file->mounted)
		return (kobj_read_file_vnode(file, buf, size, off));
	else
		return (kobj_read_file_loader(file, buf, size, off));
}

void
kobj_close_file(struct _buf *file)
{

	if (file->mounted)
		vn_close(file->ptr, FREAD, curthread->td_ucred, curthread);
	kmem_free(file, sizeof(*file));
}
