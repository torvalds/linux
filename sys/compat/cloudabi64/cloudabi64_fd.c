/*-
 * Copyright (c) 2015 Nuxi, https://nuxi.nl/
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/malloc.h>
#include <sys/syscallsubr.h>
#include <sys/systm.h>
#include <sys/uio.h>

#include <contrib/cloudabi/cloudabi64_types.h>

#include <compat/cloudabi64/cloudabi64_proto.h>
#include <compat/cloudabi64/cloudabi64_util.h>

/* Copies in 64-bit iovec structures from userspace. */
static int
cloudabi64_copyinuio(const cloudabi64_iovec_t *iovp, size_t iovcnt,
    struct uio **uiop)
{
	cloudabi64_iovec_t iovobj;
	struct uio *uio;
	struct iovec *iov;
	size_t i;
	int error;

	/* Allocate uio and iovecs. */
	if (iovcnt > UIO_MAXIOV)
		return (EINVAL);
	uio = malloc(sizeof(struct uio) + iovcnt * sizeof(struct iovec),
	    M_IOV, M_WAITOK);
	iov = (struct iovec *)(uio + 1);

	/* Initialize uio. */
	uio->uio_iov = iov;
	uio->uio_iovcnt = iovcnt;
	uio->uio_segflg = UIO_USERSPACE;
	uio->uio_offset = -1;
	uio->uio_resid = 0;

	/* Copy in iovecs. */
	for (i = 0; i < iovcnt; i++) {
		error = copyin(&iovp[i], &iovobj, sizeof(iovobj));
		if (error != 0) {
			free(uio, M_IOV);
			return (error);
		}
		iov[i].iov_base = TO_PTR(iovobj.buf);
		iov[i].iov_len = iovobj.buf_len;
		if (iov[i].iov_len > INT64_MAX - uio->uio_resid) {
			free(uio, M_IOV);
			return (EINVAL);
		}
		uio->uio_resid += iov[i].iov_len;
	}

	*uiop = uio;
	return (0);
}

int
cloudabi64_sys_fd_pread(struct thread *td,
    struct cloudabi64_sys_fd_pread_args *uap)
{
	struct uio *uio;
	int error;

	error = cloudabi64_copyinuio(uap->iovs, uap->iovs_len, &uio);
	if (error != 0)
		return (error);
	error = kern_preadv(td, uap->fd, uio, uap->offset);
	free(uio, M_IOV);
	return (error);
}

int
cloudabi64_sys_fd_pwrite(struct thread *td,
    struct cloudabi64_sys_fd_pwrite_args *uap)
{
	struct uio *uio;
	int error;

	error = cloudabi64_copyinuio(TO_PTR(uap->iovs), uap->iovs_len, &uio);
	if (error != 0)
		return (error);
	error = kern_pwritev(td, uap->fd, uio, uap->offset);
	free(uio, M_IOV);
	return (error);
}

int
cloudabi64_sys_fd_read(struct thread *td,
    struct cloudabi64_sys_fd_read_args *uap)
{
	struct uio *uio;
	int error;

	error = cloudabi64_copyinuio(uap->iovs, uap->iovs_len, &uio);
	if (error != 0)
		return (error);
	error = kern_readv(td, uap->fd, uio);
	free(uio, M_IOV);
	return (error);
}

int
cloudabi64_sys_fd_write(struct thread *td,
    struct cloudabi64_sys_fd_write_args *uap)
{
	struct uio *uio;
	int error;

	error = cloudabi64_copyinuio(TO_PTR(uap->iovs), uap->iovs_len, &uio);
	if (error != 0)
		return (error);
	error = kern_writev(td, uap->fd, uio);
	free(uio, M_IOV);
	return (error);
}
