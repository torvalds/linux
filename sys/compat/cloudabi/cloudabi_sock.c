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
#include <sys/capsicum.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/syscallsubr.h>
#include <sys/systm.h>

#include <net/vnet.h>

#include <netinet/in.h>

#include <contrib/cloudabi/cloudabi_types_common.h>

#include <compat/cloudabi/cloudabi_proto.h>
#include <compat/cloudabi/cloudabi_util.h>

int
cloudabi_sys_sock_shutdown(struct thread *td,
    struct cloudabi_sys_sock_shutdown_args *uap)
{
	int how;

	switch (uap->how) {
	case CLOUDABI_SHUT_RD:
		how = SHUT_RD;
		break;
	case CLOUDABI_SHUT_WR:
		how = SHUT_WR;
		break;
	case CLOUDABI_SHUT_RD | CLOUDABI_SHUT_WR:
		how = SHUT_RDWR;
		break;
	default:
		return (EINVAL);
	}

	return (kern_shutdown(td, uap->sock, how));
}

int
cloudabi_sock_recv(struct thread *td, cloudabi_fd_t fd, struct iovec *data,
    size_t datalen, cloudabi_fd_t *fds, size_t fdslen,
    cloudabi_riflags_t flags, size_t *rdatalen, size_t *rfdslen,
    cloudabi_roflags_t *rflags)
{
	struct msghdr hdr = {
		.msg_iov = data,
		.msg_iovlen = datalen,
	};
	struct mbuf *control;
	int error;

	/* Convert flags. */
	if (flags & CLOUDABI_SOCK_RECV_PEEK)
		hdr.msg_flags |= MSG_PEEK;
	if (flags & CLOUDABI_SOCK_RECV_WAITALL)
		hdr.msg_flags |= MSG_WAITALL;

	control = NULL;
	error = kern_recvit(td, fd, &hdr, UIO_SYSSPACE,
	    fdslen > 0 ? &control : NULL);
	if (error != 0)
		return (error);

	/* Convert return values. */
	*rdatalen = td->td_retval[0];
	td->td_retval[0] = 0;
	*rfdslen = 0;
	*rflags = 0;
	if (hdr.msg_flags & MSG_TRUNC)
		*rflags |= CLOUDABI_SOCK_RECV_DATA_TRUNCATED;

	/* Extract file descriptors from SCM_RIGHTS messages. */
	if (control != NULL) {
		struct cmsghdr *chdr;

		hdr.msg_control = mtod(control, void *);
		hdr.msg_controllen = control->m_len;
		for (chdr = CMSG_FIRSTHDR(&hdr); chdr != NULL;
		    chdr = CMSG_NXTHDR(&hdr, chdr)) {
			if (chdr->cmsg_level == SOL_SOCKET &&
			    chdr->cmsg_type == SCM_RIGHTS) {
				size_t nfds;

				nfds = (chdr->cmsg_len - CMSG_LEN(0)) /
				    sizeof(int);
				if (nfds > fdslen) {
					/* Unable to store file descriptors. */
					*rflags |=
					    CLOUDABI_SOCK_RECV_FDS_TRUNCATED;
					m_dispose_extcontrolm(control);
					break;
				}
				error = copyout(CMSG_DATA(chdr), fds,
				    nfds * sizeof(int));
				if (error != 0)
					break;
				fds += nfds;
				fdslen -= nfds;
				*rfdslen += nfds;
			}
		}
		if (control != NULL) {
			if (error != 0)
				m_dispose_extcontrolm(control);
			m_free(control);
		}
	}
	return (error);
}

int
cloudabi_sock_send(struct thread *td, cloudabi_fd_t fd, struct iovec *data,
    size_t datalen, const cloudabi_fd_t *fds, size_t fdslen, size_t *rdatalen)
{
	struct msghdr hdr = {
		.msg_iov = data,
		.msg_iovlen = datalen,
	};
	struct mbuf *control;
	int error;

	/* Convert file descriptor array to an SCM_RIGHTS message. */
	if (fdslen > MCLBYTES || CMSG_SPACE(fdslen * sizeof(int)) > MCLBYTES) {
		return (EINVAL);
	} else if (fdslen > 0) {
		struct cmsghdr *chdr;

		control = m_get2(CMSG_SPACE(fdslen * sizeof(int)),
		    M_WAITOK, MT_CONTROL, 0);
		control->m_len = CMSG_SPACE(fdslen * sizeof(int));

		chdr = mtod(control, struct cmsghdr *);
		chdr->cmsg_len = CMSG_LEN(fdslen * sizeof(int));
		chdr->cmsg_level = SOL_SOCKET;
		chdr->cmsg_type = SCM_RIGHTS;
		error = copyin(fds, CMSG_DATA(chdr), fdslen * sizeof(int));
		if (error != 0) {
			m_free(control);
			return (error);
		}
	} else {
		control = NULL;
	}

	error = kern_sendit(td, fd, &hdr, MSG_NOSIGNAL, control, UIO_USERSPACE);
	if (error != 0)
		return (error);
	*rdatalen = td->td_retval[0];
	td->td_retval[0] = 0;
	return (0);
}
