/*-
 * Copyright (c) 1982, 1986, 1989, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_capsicum.h"
#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_sctp.h"
#include "opt_ktrace.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/capsicum.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sysproto.h>
#include <sys/malloc.h>
#include <sys/filedesc.h>
#include <sys/event.h>
#include <sys/proc.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/filio.h>
#include <sys/jail.h>
#include <sys/mount.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/sf_buf.h>
#include <sys/sysent.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/signalvar.h>
#include <sys/syscall.h>
#include <sys/syscallsubr.h>
#include <sys/sysctl.h>
#include <sys/uio.h>
#include <sys/vnode.h>
#ifdef KTRACE
#include <sys/ktrace.h>
#endif
#ifdef COMPAT_FREEBSD32
#include <compat/freebsd32/freebsd32_util.h>
#endif

#include <net/vnet.h>

#include <security/audit/audit.h>
#include <security/mac/mac_framework.h>

#include <netinet/sctp.h>
#include <netinet/sctp_peeloff.h>

static struct syscall_helper_data sctp_syscalls[] = {
	SYSCALL_INIT_HELPER_F(sctp_peeloff, SYF_CAPENABLED),
	SYSCALL_INIT_HELPER_F(sctp_generic_sendmsg, SYF_CAPENABLED),
	SYSCALL_INIT_HELPER_F(sctp_generic_sendmsg_iov, SYF_CAPENABLED),
	SYSCALL_INIT_HELPER_F(sctp_generic_recvmsg, SYF_CAPENABLED),
	SYSCALL_INIT_LAST
};

static void
sctp_syscalls_init(void *unused __unused)
{
	int error __unused;

	error = syscall_helper_register(sctp_syscalls, SY_THR_STATIC);
	KASSERT((error == 0),
	    ("%s: syscall_helper_register failed for sctp syscalls", __func__));
#ifdef COMPAT_FREEBSD32
	error = syscall32_helper_register(sctp_syscalls, SY_THR_STATIC);
	KASSERT((error == 0),
	    ("%s: syscall32_helper_register failed for sctp syscalls",
	    __func__));
#endif
}
SYSINIT(sctp_syscalls, SI_SUB_SYSCALLS, SI_ORDER_ANY, sctp_syscalls_init, NULL);

/*
 * SCTP syscalls.
 * Functionality only compiled in if SCTP is defined in the kernel Makefile,
 * otherwise all return EOPNOTSUPP.
 * XXX: We should make this loadable one day.
 */
int
sys_sctp_peeloff(td, uap)
	struct thread *td;
	struct sctp_peeloff_args /* {
		int	sd;
		caddr_t	name;
	} */ *uap;
{
#if (defined(INET) || defined(INET6)) && defined(SCTP)
	struct file *headfp, *nfp = NULL;
	struct socket *head, *so;
	cap_rights_t rights;
	u_int fflag;
	int error, fd;

	AUDIT_ARG_FD(uap->sd);
	error = getsock_cap(td, uap->sd, cap_rights_init(&rights, CAP_PEELOFF),
	    &headfp, &fflag, NULL);
	if (error != 0)
		goto done2;
	head = headfp->f_data;
	if (head->so_proto->pr_protocol != IPPROTO_SCTP) {
		error = EOPNOTSUPP;
		goto done;
	}
	error = sctp_can_peel_off(head, (sctp_assoc_t)uap->name);
	if (error != 0)
		goto done;
	/*
	 * At this point we know we do have a assoc to pull
	 * we proceed to get the fd setup. This may block
	 * but that is ok.
	 */

	error = falloc(td, &nfp, &fd, 0);
	if (error != 0)
		goto done;
	td->td_retval[0] = fd;

	CURVNET_SET(head->so_vnet);
	so = sopeeloff(head);
	if (so == NULL) {
		error = ENOMEM;
		goto noconnection;
	}
	finit(nfp, fflag, DTYPE_SOCKET, so, &socketops);
	error = sctp_do_peeloff(head, so, (sctp_assoc_t)uap->name);
	if (error != 0)
		goto noconnection;
	if (head->so_sigio != NULL)
		fsetown(fgetown(&head->so_sigio), &so->so_sigio);

noconnection:
	/*
	 * close the new descriptor, assuming someone hasn't ripped it
	 * out from under us.
	 */
	if (error != 0)
		fdclose(td, nfp, fd);

	/*
	 * Release explicitly held references before returning.
	 */
	CURVNET_RESTORE();
done:
	if (nfp != NULL)
		fdrop(nfp, td);
	fdrop(headfp, td);
done2:
	return (error);
#else  /* SCTP */
	return (EOPNOTSUPP);
#endif /* SCTP */
}

int
sys_sctp_generic_sendmsg (td, uap)
	struct thread *td;
	struct sctp_generic_sendmsg_args /* {
		int sd,
		caddr_t msg,
		int mlen,
		caddr_t to,
		__socklen_t tolen,
		struct sctp_sndrcvinfo *sinfo,
		int flags
	} */ *uap;
{
#if (defined(INET) || defined(INET6)) && defined(SCTP)
	struct sctp_sndrcvinfo sinfo, *u_sinfo = NULL;
	struct socket *so;
	struct file *fp = NULL;
	struct sockaddr *to = NULL;
#ifdef KTRACE
	struct uio *ktruio = NULL;
#endif
	struct uio auio;
	struct iovec iov[1];
	cap_rights_t rights;
	int error = 0, len;

	if (uap->sinfo != NULL) {
		error = copyin(uap->sinfo, &sinfo, sizeof (sinfo));
		if (error != 0)
			return (error);
		u_sinfo = &sinfo;
	}

	cap_rights_init(&rights, CAP_SEND);
	if (uap->tolen != 0) {
		error = getsockaddr(&to, uap->to, uap->tolen);
		if (error != 0) {
			to = NULL;
			goto sctp_bad2;
		}
		cap_rights_set(&rights, CAP_CONNECT);
	}

	AUDIT_ARG_FD(uap->sd);
	error = getsock_cap(td, uap->sd, &rights, &fp, NULL, NULL);
	if (error != 0)
		goto sctp_bad;
#ifdef KTRACE
	if (to && (KTRPOINT(td, KTR_STRUCT)))
		ktrsockaddr(to);
#endif

	iov[0].iov_base = uap->msg;
	iov[0].iov_len = uap->mlen;

	so = (struct socket *)fp->f_data;
	if (so->so_proto->pr_protocol != IPPROTO_SCTP) {
		error = EOPNOTSUPP;
		goto sctp_bad;
	}
#ifdef MAC
	error = mac_socket_check_send(td->td_ucred, so);
	if (error != 0)
		goto sctp_bad;
#endif /* MAC */

	auio.uio_iov =  iov;
	auio.uio_iovcnt = 1;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_rw = UIO_WRITE;
	auio.uio_td = td;
	auio.uio_offset = 0;			/* XXX */
	auio.uio_resid = 0;
#ifdef KTRACE
	if (KTRPOINT(td, KTR_GENIO))
		ktruio = cloneuio(&auio);
#endif /* KTRACE */
	len = auio.uio_resid = uap->mlen;
	CURVNET_SET(so->so_vnet);
	error = sctp_lower_sosend(so, to, &auio, (struct mbuf *)NULL,
	    (struct mbuf *)NULL, uap->flags, u_sinfo, td);
	CURVNET_RESTORE();
	if (error != 0) {
		if (auio.uio_resid != len && (error == ERESTART ||
		    error == EINTR || error == EWOULDBLOCK))
			error = 0;
		/* Generation of SIGPIPE can be controlled per socket. */
		if (error == EPIPE && !(so->so_options & SO_NOSIGPIPE) &&
		    !(uap->flags & MSG_NOSIGNAL)) {
			PROC_LOCK(td->td_proc);
			tdsignal(td, SIGPIPE);
			PROC_UNLOCK(td->td_proc);
		}
	}
	if (error == 0)
		td->td_retval[0] = len - auio.uio_resid;
#ifdef KTRACE
	if (ktruio != NULL) {
		ktruio->uio_resid = td->td_retval[0];
		ktrgenio(uap->sd, UIO_WRITE, ktruio, error);
	}
#endif /* KTRACE */
sctp_bad:
	if (fp != NULL)
		fdrop(fp, td);
sctp_bad2:
	free(to, M_SONAME);
	return (error);
#else  /* SCTP */
	return (EOPNOTSUPP);
#endif /* SCTP */
}

int
sys_sctp_generic_sendmsg_iov(td, uap)
	struct thread *td;
	struct sctp_generic_sendmsg_iov_args /* {
		int sd,
		struct iovec *iov,
		int iovlen,
		caddr_t to,
		__socklen_t tolen,
		struct sctp_sndrcvinfo *sinfo,
		int flags
	} */ *uap;
{
#if (defined(INET) || defined(INET6)) && defined(SCTP)
	struct sctp_sndrcvinfo sinfo, *u_sinfo = NULL;
	struct socket *so;
	struct file *fp = NULL;
	struct sockaddr *to = NULL;
#ifdef KTRACE
	struct uio *ktruio = NULL;
#endif
	struct uio auio;
	struct iovec *iov, *tiov;
	cap_rights_t rights;
	ssize_t len;
	int error, i;

	if (uap->sinfo != NULL) {
		error = copyin(uap->sinfo, &sinfo, sizeof (sinfo));
		if (error != 0)
			return (error);
		u_sinfo = &sinfo;
	}
	cap_rights_init(&rights, CAP_SEND);
	if (uap->tolen != 0) {
		error = getsockaddr(&to, uap->to, uap->tolen);
		if (error != 0) {
			to = NULL;
			goto sctp_bad2;
		}
		cap_rights_set(&rights, CAP_CONNECT);
	}

	AUDIT_ARG_FD(uap->sd);
	error = getsock_cap(td, uap->sd, &rights, &fp, NULL, NULL);
	if (error != 0)
		goto sctp_bad1;

#ifdef COMPAT_FREEBSD32
	if (SV_CURPROC_FLAG(SV_ILP32))
		error = freebsd32_copyiniov((struct iovec32 *)uap->iov,
		    uap->iovlen, &iov, EMSGSIZE);
	else
#endif
		error = copyiniov(uap->iov, uap->iovlen, &iov, EMSGSIZE);
	if (error != 0)
		goto sctp_bad1;
#ifdef KTRACE
	if (to && (KTRPOINT(td, KTR_STRUCT)))
		ktrsockaddr(to);
#endif

	so = (struct socket *)fp->f_data;
	if (so->so_proto->pr_protocol != IPPROTO_SCTP) {
		error = EOPNOTSUPP;
		goto sctp_bad;
	}
#ifdef MAC
	error = mac_socket_check_send(td->td_ucred, so);
	if (error != 0)
		goto sctp_bad;
#endif /* MAC */

	auio.uio_iov = iov;
	auio.uio_iovcnt = uap->iovlen;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_rw = UIO_WRITE;
	auio.uio_td = td;
	auio.uio_offset = 0;			/* XXX */
	auio.uio_resid = 0;
	tiov = iov;
	for (i = 0; i <uap->iovlen; i++, tiov++) {
		if ((auio.uio_resid += tiov->iov_len) < 0) {
			error = EINVAL;
			goto sctp_bad;
		}
	}
#ifdef KTRACE
	if (KTRPOINT(td, KTR_GENIO))
		ktruio = cloneuio(&auio);
#endif /* KTRACE */
	len = auio.uio_resid;
	CURVNET_SET(so->so_vnet);
	error = sctp_lower_sosend(so, to, &auio,
		    (struct mbuf *)NULL, (struct mbuf *)NULL,
		    uap->flags, u_sinfo, td);
	CURVNET_RESTORE();
	if (error != 0) {
		if (auio.uio_resid != len && (error == ERESTART ||
		    error == EINTR || error == EWOULDBLOCK))
			error = 0;
		/* Generation of SIGPIPE can be controlled per socket */
		if (error == EPIPE && !(so->so_options & SO_NOSIGPIPE) &&
		    !(uap->flags & MSG_NOSIGNAL)) {
			PROC_LOCK(td->td_proc);
			tdsignal(td, SIGPIPE);
			PROC_UNLOCK(td->td_proc);
		}
	}
	if (error == 0)
		td->td_retval[0] = len - auio.uio_resid;
#ifdef KTRACE
	if (ktruio != NULL) {
		ktruio->uio_resid = td->td_retval[0];
		ktrgenio(uap->sd, UIO_WRITE, ktruio, error);
	}
#endif /* KTRACE */
sctp_bad:
	free(iov, M_IOV);
sctp_bad1:
	if (fp != NULL)
		fdrop(fp, td);
sctp_bad2:
	free(to, M_SONAME);
	return (error);
#else  /* SCTP */
	return (EOPNOTSUPP);
#endif /* SCTP */
}

int
sys_sctp_generic_recvmsg(td, uap)
	struct thread *td;
	struct sctp_generic_recvmsg_args /* {
		int sd,
		struct iovec *iov,
		int iovlen,
		struct sockaddr *from,
		__socklen_t *fromlenaddr,
		struct sctp_sndrcvinfo *sinfo,
		int *msg_flags
	} */ *uap;
{
#if (defined(INET) || defined(INET6)) && defined(SCTP)
	uint8_t sockbufstore[256];
	struct uio auio;
	struct iovec *iov, *tiov;
	struct sctp_sndrcvinfo sinfo;
	struct socket *so;
	struct file *fp = NULL;
	struct sockaddr *fromsa;
	cap_rights_t rights;
#ifdef KTRACE
	struct uio *ktruio = NULL;
#endif
	ssize_t len;
	int error, fromlen, i, msg_flags;

	AUDIT_ARG_FD(uap->sd);
	error = getsock_cap(td, uap->sd, cap_rights_init(&rights, CAP_RECV),
	    &fp, NULL, NULL);
	if (error != 0)
		return (error);
#ifdef COMPAT_FREEBSD32
	if (SV_CURPROC_FLAG(SV_ILP32))
		error = freebsd32_copyiniov((struct iovec32 *)uap->iov,
		    uap->iovlen, &iov, EMSGSIZE);
	else
#endif
		error = copyiniov(uap->iov, uap->iovlen, &iov, EMSGSIZE);
	if (error != 0)
		goto out1;

	so = fp->f_data;
	if (so->so_proto->pr_protocol != IPPROTO_SCTP) {
		error = EOPNOTSUPP;
		goto out;
	}
#ifdef MAC
	error = mac_socket_check_receive(td->td_ucred, so);
	if (error != 0)
		goto out;
#endif /* MAC */

	if (uap->fromlenaddr != NULL) {
		error = copyin(uap->fromlenaddr, &fromlen, sizeof (fromlen));
		if (error != 0)
			goto out;
	} else {
		fromlen = 0;
	}
	if (uap->msg_flags) {
		error = copyin(uap->msg_flags, &msg_flags, sizeof (int));
		if (error != 0)
			goto out;
	} else {
		msg_flags = 0;
	}
	auio.uio_iov = iov;
	auio.uio_iovcnt = uap->iovlen;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_rw = UIO_READ;
	auio.uio_td = td;
	auio.uio_offset = 0;			/* XXX */
	auio.uio_resid = 0;
	tiov = iov;
	for (i = 0; i <uap->iovlen; i++, tiov++) {
		if ((auio.uio_resid += tiov->iov_len) < 0) {
			error = EINVAL;
			goto out;
		}
	}
	len = auio.uio_resid;
	fromsa = (struct sockaddr *)sockbufstore;

#ifdef KTRACE
	if (KTRPOINT(td, KTR_GENIO))
		ktruio = cloneuio(&auio);
#endif /* KTRACE */
	memset(&sinfo, 0, sizeof(struct sctp_sndrcvinfo));
	CURVNET_SET(so->so_vnet);
	error = sctp_sorecvmsg(so, &auio, (struct mbuf **)NULL,
		    fromsa, fromlen, &msg_flags,
		    (struct sctp_sndrcvinfo *)&sinfo, 1);
	CURVNET_RESTORE();
	if (error != 0) {
		if (auio.uio_resid != len && (error == ERESTART ||
		    error == EINTR || error == EWOULDBLOCK))
			error = 0;
	} else {
		if (uap->sinfo)
			error = copyout(&sinfo, uap->sinfo, sizeof (sinfo));
	}
#ifdef KTRACE
	if (ktruio != NULL) {
		ktruio->uio_resid = len - auio.uio_resid;
		ktrgenio(uap->sd, UIO_READ, ktruio, error);
	}
#endif /* KTRACE */
	if (error != 0)
		goto out;
	td->td_retval[0] = len - auio.uio_resid;

	if (fromlen && uap->from) {
		len = fromlen;
		if (len <= 0 || fromsa == NULL)
			len = 0;
		else {
			len = MIN(len, fromsa->sa_len);
			error = copyout(fromsa, uap->from, (size_t)len);
			if (error != 0)
				goto out;
		}
		error = copyout(&len, uap->fromlenaddr, sizeof (socklen_t));
		if (error != 0)
			goto out;
	}
#ifdef KTRACE
	if (KTRPOINT(td, KTR_STRUCT))
		ktrsockaddr(fromsa);
#endif
	if (uap->msg_flags) {
		error = copyout(&msg_flags, uap->msg_flags, sizeof (int));
		if (error != 0)
			goto out;
	}
out:
	free(iov, M_IOV);
out1:
	if (fp != NULL)
		fdrop(fp, td);

	return (error);
#else  /* SCTP */
	return (EOPNOTSUPP);
#endif /* SCTP */
}
