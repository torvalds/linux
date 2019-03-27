/*-
 * Copyright (c) 2015-2017 Nuxi, https://nuxi.nl/
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
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/uio.h>

#include <contrib/cloudabi/cloudabi32_types.h>

#include <compat/cloudabi/cloudabi_util.h>

#include <compat/cloudabi32/cloudabi32_proto.h>
#include <compat/cloudabi32/cloudabi32_util.h>

static MALLOC_DEFINE(M_SOCKET, "socket", "CloudABI socket");

int
cloudabi32_sys_sock_recv(struct thread *td,
    struct cloudabi32_sys_sock_recv_args *uap)
{
	cloudabi32_recv_in_t ri;
	cloudabi32_recv_out_t ro = {};
	cloudabi32_iovec_t iovobj;
	struct iovec *iov;
	const cloudabi32_iovec_t *user_iov;
	size_t i, rdatalen, rfdslen;
	int error;

	error = copyin(uap->in, &ri, sizeof(ri));
	if (error != 0)
		return (error);

	/* Convert iovecs to native format. */
	if (ri.ri_data_len > UIO_MAXIOV)
		return (EINVAL);
	iov = mallocarray(ri.ri_data_len, sizeof(struct iovec),
	    M_SOCKET, M_WAITOK);
	user_iov = TO_PTR(ri.ri_data);
	for (i = 0; i < ri.ri_data_len; i++) {
		error = copyin(&user_iov[i], &iovobj, sizeof(iovobj));
		if (error != 0) {
			free(iov, M_SOCKET);
			return (error);
		}
		iov[i].iov_base = TO_PTR(iovobj.buf);
		iov[i].iov_len = iovobj.buf_len;
	}

	error = cloudabi_sock_recv(td, uap->sock, iov, ri.ri_data_len,
	    TO_PTR(ri.ri_fds), ri.ri_fds_len, ri.ri_flags, &rdatalen,
	    &rfdslen, &ro.ro_flags);
	free(iov, M_SOCKET);
	if (error != 0)
		return (error);

	ro.ro_datalen = rdatalen;
	ro.ro_fdslen = rfdslen;
	return (copyout(&ro, uap->out, sizeof(ro)));
}

int
cloudabi32_sys_sock_send(struct thread *td,
    struct cloudabi32_sys_sock_send_args *uap)
{
	cloudabi32_send_in_t si;
	cloudabi32_send_out_t so = {};
	cloudabi32_ciovec_t iovobj;
	struct iovec *iov;
	const cloudabi32_ciovec_t *user_iov;
	size_t datalen, i;
	int error;

	error = copyin(uap->in, &si, sizeof(si));
	if (error != 0)
		return (error);

	/* Convert iovecs to native format. */
	if (si.si_data_len > UIO_MAXIOV)
		return (EINVAL);
	iov = mallocarray(si.si_data_len, sizeof(struct iovec),
	    M_SOCKET, M_WAITOK);
	user_iov = TO_PTR(si.si_data);
	for (i = 0; i < si.si_data_len; i++) {
		error = copyin(&user_iov[i], &iovobj, sizeof(iovobj));
		if (error != 0) {
			free(iov, M_SOCKET);
			return (error);
		}
		iov[i].iov_base = TO_PTR(iovobj.buf);
		iov[i].iov_len = iovobj.buf_len;
	}

	error = cloudabi_sock_send(td, uap->sock, iov, si.si_data_len,
	    TO_PTR(si.si_fds), si.si_fds_len, &datalen);
	free(iov, M_SOCKET);
	if (error != 0)
		return (error);

	so.so_datalen = datalen;
	return (copyout(&so, uap->out, sizeof(so)));
}
