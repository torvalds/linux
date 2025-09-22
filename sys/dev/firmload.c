/*	$OpenBSD: firmload.c,v 1.16 2018/08/13 23:12:39 deraadt Exp $	*/

/*
 * Copyright (c) 2004 Theo de Raadt <deraadt@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/syslimits.h>
#include <sys/namei.h>
#include <sys/vnode.h>
#include <sys/errno.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/device.h>
#include <sys/pledge.h>

int
loadfirmware(const char *name, u_char **bufp, size_t *buflen)
{
	struct proc *p = curproc;
	struct nameidata nid;
	char *path, *ptr;
	struct iovec iov;
	struct uio uio;
	struct vattr va;
	int error;

	if (!rootvp || !vcount(rootvp))
		return (EIO);

	path = malloc(MAXPATHLEN, M_TEMP, M_NOWAIT);
	if (path == NULL)
		return (ENOMEM);

	if (snprintf(path, MAXPATHLEN, "/etc/firmware/%s", name) >=
	    MAXPATHLEN) {
		error = ENAMETOOLONG;
		goto err;
	}

	NDINIT(&nid, LOOKUP, NOFOLLOW|LOCKLEAF|KERNELPATH,
	    UIO_SYSSPACE, path, p);
	nid.ni_pledge = PLEDGE_RPATH;
	error = namei(&nid);
#ifdef RAMDISK_HOOKS
	/* try again with mounted disk */
	if (error) {
		if (snprintf(path, MAXPATHLEN, "/mnt/etc/firmware/%s", name) >=
		    MAXPATHLEN) {
			error = ENAMETOOLONG;
			goto err;
		}

		NDINIT(&nid, LOOKUP, NOFOLLOW|LOCKLEAF|KERNELPATH,
		    UIO_SYSSPACE, path, p);
		nid.ni_pledge = PLEDGE_RPATH;
		error = namei(&nid);
	}
#endif
	if (error)
		goto err;
	error = VOP_GETATTR(nid.ni_vp, &va, p->p_ucred, p);
	if (error)
		goto fail;
	if (nid.ni_vp->v_type != VREG || va.va_size == 0) {
		error = EINVAL;
		goto fail;
	}
	if (va.va_size > FIRMWARE_MAX) {
		error = E2BIG;
		goto fail;
	}
	ptr = malloc(va.va_size, M_DEVBUF, M_NOWAIT);
	if (ptr == NULL) {
		error = ENOMEM;
		goto fail;
	}

	iov.iov_base = ptr;
	iov.iov_len = va.va_size;
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_offset = 0;
	uio.uio_resid = va.va_size;
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_rw = UIO_READ;
	uio.uio_procp = p;

	error = VOP_READ(nid.ni_vp, &uio, 0, p->p_ucred);

	if (error == 0) {
		*bufp = ptr;
		*buflen = va.va_size;
	} else
		free(ptr, M_DEVBUF, va.va_size);

fail:
	vput(nid.ni_vp);
err:
	free(path, M_TEMP, MAXPATHLEN);
	return (error);
}
