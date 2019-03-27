/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1995 Søren Schmidt
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

/* XXX we use functions that might not exist. */
#include "opt_compat.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/capsicum.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/syscallsubr.h>
#include <sys/uio.h>
#include <sys/syslog.h>
#include <sys/un.h>

#include <net/if.h>
#include <net/vnet.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#ifdef INET6
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#endif

#ifdef COMPAT_LINUX32
#include <machine/../linux32/linux.h>
#include <machine/../linux32/linux32_proto.h>
#else
#include <machine/../linux/linux.h>
#include <machine/../linux/linux_proto.h>
#endif
#include <compat/linux/linux_file.h>
#include <compat/linux/linux_socket.h>
#include <compat/linux/linux_timer.h>
#include <compat/linux/linux_util.h>

static int linux_to_bsd_domain(int);
static int linux_sendmsg_common(struct thread *, l_int, struct l_msghdr *,
					l_uint);
static int linux_recvmsg_common(struct thread *, l_int, struct l_msghdr *,
					l_uint, struct msghdr *);
static int linux_set_socket_flags(int, int *);

/*
 * Reads a Linux sockaddr and does any necessary translation.
 * Linux sockaddrs don't have a length field, only a family.
 * Copy the osockaddr structure pointed to by osa to kernel, adjust
 * family and convert to sockaddr.
 */
static int
linux_getsockaddr(struct sockaddr **sap, const struct osockaddr *osa, int salen)
{
	struct sockaddr *sa;
	struct osockaddr *kosa;
#ifdef INET6
	struct sockaddr_in6 *sin6;
	int oldv6size;
#endif
	char *name;
	int bdom, error, hdrlen, namelen;

	if (salen < 2 || salen > UCHAR_MAX || !osa)
		return (EINVAL);

#ifdef INET6
	oldv6size = 0;
	/*
	 * Check for old (pre-RFC2553) sockaddr_in6. We may accept it
	 * if it's a v4-mapped address, so reserve the proper space
	 * for it.
	 */
	if (salen == sizeof(struct sockaddr_in6) - sizeof(uint32_t)) {
		salen += sizeof(uint32_t);
		oldv6size = 1;
	}
#endif

	kosa = malloc(salen, M_SONAME, M_WAITOK);

	if ((error = copyin(osa, kosa, salen)))
		goto out;

	bdom = linux_to_bsd_domain(kosa->sa_family);
	if (bdom == -1) {
		error = EAFNOSUPPORT;
		goto out;
	}

#ifdef INET6
	/*
	 * Older Linux IPv6 code uses obsolete RFC2133 struct sockaddr_in6,
	 * which lacks the scope id compared with RFC2553 one. If we detect
	 * the situation, reject the address and write a message to system log.
	 *
	 * Still accept addresses for which the scope id is not used.
	 */
	if (oldv6size) {
		if (bdom == AF_INET6) {
			sin6 = (struct sockaddr_in6 *)kosa;
			if (IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr) ||
			    (!IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr) &&
			     !IN6_IS_ADDR_SITELOCAL(&sin6->sin6_addr) &&
			     !IN6_IS_ADDR_V4COMPAT(&sin6->sin6_addr) &&
			     !IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr) &&
			     !IN6_IS_ADDR_MULTICAST(&sin6->sin6_addr))) {
				sin6->sin6_scope_id = 0;
			} else {
				log(LOG_DEBUG,
				    "obsolete pre-RFC2553 sockaddr_in6 rejected\n");
				error = EINVAL;
				goto out;
			}
		} else
			salen -= sizeof(uint32_t);
	}
#endif
	if (bdom == AF_INET) {
		if (salen < sizeof(struct sockaddr_in)) {
			error = EINVAL;
			goto out;
		}
		salen = sizeof(struct sockaddr_in);
	}

	if (bdom == AF_LOCAL && salen > sizeof(struct sockaddr_un)) {
		hdrlen = offsetof(struct sockaddr_un, sun_path);
		name = ((struct sockaddr_un *)kosa)->sun_path;
		if (*name == '\0') {
			/*
			 * Linux abstract namespace starts with a NULL byte.
			 * XXX We do not support abstract namespace yet.
			 */
			namelen = strnlen(name + 1, salen - hdrlen - 1) + 1;
		} else
			namelen = strnlen(name, salen - hdrlen);
		salen = hdrlen + namelen;
		if (salen > sizeof(struct sockaddr_un)) {
			error = ENAMETOOLONG;
			goto out;
		}
	}

	sa = (struct sockaddr *)kosa;
	sa->sa_family = bdom;
	sa->sa_len = salen;

	*sap = sa;
	return (0);

out:
	free(kosa, M_SONAME);
	return (error);
}

static int
linux_to_bsd_domain(int domain)
{

	switch (domain) {
	case LINUX_AF_UNSPEC:
		return (AF_UNSPEC);
	case LINUX_AF_UNIX:
		return (AF_LOCAL);
	case LINUX_AF_INET:
		return (AF_INET);
	case LINUX_AF_INET6:
		return (AF_INET6);
	case LINUX_AF_AX25:
		return (AF_CCITT);
	case LINUX_AF_IPX:
		return (AF_IPX);
	case LINUX_AF_APPLETALK:
		return (AF_APPLETALK);
	}
	return (-1);
}

static int
bsd_to_linux_domain(int domain)
{

	switch (domain) {
	case AF_UNSPEC:
		return (LINUX_AF_UNSPEC);
	case AF_LOCAL:
		return (LINUX_AF_UNIX);
	case AF_INET:
		return (LINUX_AF_INET);
	case AF_INET6:
		return (LINUX_AF_INET6);
	case AF_CCITT:
		return (LINUX_AF_AX25);
	case AF_IPX:
		return (LINUX_AF_IPX);
	case AF_APPLETALK:
		return (LINUX_AF_APPLETALK);
	}
	return (-1);
}

static int
linux_to_bsd_sockopt_level(int level)
{

	switch (level) {
	case LINUX_SOL_SOCKET:
		return (SOL_SOCKET);
	}
	return (level);
}

static int
bsd_to_linux_sockopt_level(int level)
{

	switch (level) {
	case SOL_SOCKET:
		return (LINUX_SOL_SOCKET);
	}
	return (level);
}

static int
linux_to_bsd_ip_sockopt(int opt)
{

	switch (opt) {
	case LINUX_IP_TOS:
		return (IP_TOS);
	case LINUX_IP_TTL:
		return (IP_TTL);
	case LINUX_IP_OPTIONS:
		return (IP_OPTIONS);
	case LINUX_IP_MULTICAST_IF:
		return (IP_MULTICAST_IF);
	case LINUX_IP_MULTICAST_TTL:
		return (IP_MULTICAST_TTL);
	case LINUX_IP_MULTICAST_LOOP:
		return (IP_MULTICAST_LOOP);
	case LINUX_IP_ADD_MEMBERSHIP:
		return (IP_ADD_MEMBERSHIP);
	case LINUX_IP_DROP_MEMBERSHIP:
		return (IP_DROP_MEMBERSHIP);
	case LINUX_IP_HDRINCL:
		return (IP_HDRINCL);
	}
	return (-1);
}

static int
linux_to_bsd_ip6_sockopt(int opt)
{

	switch (opt) {
	case LINUX_IPV6_NEXTHOP:
		return (IPV6_NEXTHOP);
	case LINUX_IPV6_UNICAST_HOPS:
		return (IPV6_UNICAST_HOPS);
	case LINUX_IPV6_MULTICAST_IF:
		return (IPV6_MULTICAST_IF);
	case LINUX_IPV6_MULTICAST_HOPS:
		return (IPV6_MULTICAST_HOPS);
	case LINUX_IPV6_MULTICAST_LOOP:
		return (IPV6_MULTICAST_LOOP);
	case LINUX_IPV6_ADD_MEMBERSHIP:
		return (IPV6_JOIN_GROUP);
	case LINUX_IPV6_DROP_MEMBERSHIP:
		return (IPV6_LEAVE_GROUP);
	case LINUX_IPV6_V6ONLY:
		return (IPV6_V6ONLY);
	case LINUX_IPV6_DONTFRAG:
		return (IPV6_DONTFRAG);
#if 0
	case LINUX_IPV6_CHECKSUM:
		return (IPV6_CHECKSUM);
	case LINUX_IPV6_RECVPKTINFO:
		return (IPV6_RECVPKTINFO);
	case LINUX_IPV6_PKTINFO:
		return (IPV6_PKTINFO);
	case LINUX_IPV6_RECVHOPLIMIT:
		return (IPV6_RECVHOPLIMIT);
	case LINUX_IPV6_HOPLIMIT:
		return (IPV6_HOPLIMIT);
	case LINUX_IPV6_RECVHOPOPTS:
		return (IPV6_RECVHOPOPTS);
	case LINUX_IPV6_HOPOPTS:
		return (IPV6_HOPOPTS);
	case LINUX_IPV6_RTHDRDSTOPTS:
		return (IPV6_RTHDRDSTOPTS);
	case LINUX_IPV6_RECVRTHDR:
		return (IPV6_RECVRTHDR);
	case LINUX_IPV6_RTHDR:
		return (IPV6_RTHDR);
	case LINUX_IPV6_RECVDSTOPTS:
		return (IPV6_RECVDSTOPTS);
	case LINUX_IPV6_DSTOPTS:
		return (IPV6_DSTOPTS);
	case LINUX_IPV6_RECVPATHMTU:
		return (IPV6_RECVPATHMTU);
	case LINUX_IPV6_PATHMTU:
		return (IPV6_PATHMTU);
#endif
	}
	return (-1);
}

static int
linux_to_bsd_so_sockopt(int opt)
{

	switch (opt) {
	case LINUX_SO_DEBUG:
		return (SO_DEBUG);
	case LINUX_SO_REUSEADDR:
		return (SO_REUSEADDR);
	case LINUX_SO_TYPE:
		return (SO_TYPE);
	case LINUX_SO_ERROR:
		return (SO_ERROR);
	case LINUX_SO_DONTROUTE:
		return (SO_DONTROUTE);
	case LINUX_SO_BROADCAST:
		return (SO_BROADCAST);
	case LINUX_SO_SNDBUF:
		return (SO_SNDBUF);
	case LINUX_SO_RCVBUF:
		return (SO_RCVBUF);
	case LINUX_SO_KEEPALIVE:
		return (SO_KEEPALIVE);
	case LINUX_SO_OOBINLINE:
		return (SO_OOBINLINE);
	case LINUX_SO_LINGER:
		return (SO_LINGER);
	case LINUX_SO_PEERCRED:
		return (LOCAL_PEERCRED);
	case LINUX_SO_RCVLOWAT:
		return (SO_RCVLOWAT);
	case LINUX_SO_SNDLOWAT:
		return (SO_SNDLOWAT);
	case LINUX_SO_RCVTIMEO:
		return (SO_RCVTIMEO);
	case LINUX_SO_SNDTIMEO:
		return (SO_SNDTIMEO);
	case LINUX_SO_TIMESTAMP:
		return (SO_TIMESTAMP);
	case LINUX_SO_ACCEPTCONN:
		return (SO_ACCEPTCONN);
	}
	return (-1);
}

static int
linux_to_bsd_tcp_sockopt(int opt)
{

	switch (opt) {
	case LINUX_TCP_NODELAY:
		return (TCP_NODELAY);
	case LINUX_TCP_MAXSEG:
		return (TCP_MAXSEG);
	case LINUX_TCP_KEEPIDLE:
		return (TCP_KEEPIDLE);
	case LINUX_TCP_KEEPINTVL:
		return (TCP_KEEPINTVL);
	case LINUX_TCP_KEEPCNT:
		return (TCP_KEEPCNT);
	case LINUX_TCP_MD5SIG:
		return (TCP_MD5SIG);
	}
	return (-1);
}

static int
linux_to_bsd_msg_flags(int flags)
{
	int ret_flags = 0;

	if (flags & LINUX_MSG_OOB)
		ret_flags |= MSG_OOB;
	if (flags & LINUX_MSG_PEEK)
		ret_flags |= MSG_PEEK;
	if (flags & LINUX_MSG_DONTROUTE)
		ret_flags |= MSG_DONTROUTE;
	if (flags & LINUX_MSG_CTRUNC)
		ret_flags |= MSG_CTRUNC;
	if (flags & LINUX_MSG_TRUNC)
		ret_flags |= MSG_TRUNC;
	if (flags & LINUX_MSG_DONTWAIT)
		ret_flags |= MSG_DONTWAIT;
	if (flags & LINUX_MSG_EOR)
		ret_flags |= MSG_EOR;
	if (flags & LINUX_MSG_WAITALL)
		ret_flags |= MSG_WAITALL;
	if (flags & LINUX_MSG_NOSIGNAL)
		ret_flags |= MSG_NOSIGNAL;
#if 0 /* not handled */
	if (flags & LINUX_MSG_PROXY)
		;
	if (flags & LINUX_MSG_FIN)
		;
	if (flags & LINUX_MSG_SYN)
		;
	if (flags & LINUX_MSG_CONFIRM)
		;
	if (flags & LINUX_MSG_RST)
		;
	if (flags & LINUX_MSG_ERRQUEUE)
		;
#endif
	return (ret_flags);
}

/*
* If bsd_to_linux_sockaddr() or linux_to_bsd_sockaddr() faults, then the
* native syscall will fault.  Thus, we don't really need to check the
* return values for these functions.
*/

static int
bsd_to_linux_sockaddr(struct sockaddr *arg)
{
	struct sockaddr sa;
	size_t sa_len = sizeof(struct sockaddr);
	int error, bdom;

	if ((error = copyin(arg, &sa, sa_len)))
		return (error);

	bdom = bsd_to_linux_domain(sa.sa_family);
	if (bdom == -1)
		return (EAFNOSUPPORT);

	*(u_short *)&sa = bdom;
	return (copyout(&sa, arg, sa_len));
}

static int
linux_to_bsd_sockaddr(struct sockaddr *arg, int len)
{
	struct sockaddr sa;
	size_t sa_len = sizeof(struct sockaddr);
	int error, bdom;

	if ((error = copyin(arg, &sa, sa_len)))
		return (error);

	bdom = linux_to_bsd_domain(*(sa_family_t *)&sa);
	if (bdom == -1)
		return (EAFNOSUPPORT);

	sa.sa_family = bdom;
	sa.sa_len = len;
	return (copyout(&sa, arg, sa_len));
}

static int
linux_sa_put(struct osockaddr *osa)
{
	struct osockaddr sa;
	int error, bdom;

	/*
	 * Only read/write the osockaddr family part, the rest is
	 * not changed.
	 */
	error = copyin(osa, &sa, sizeof(sa.sa_family));
	if (error != 0)
		return (error);

	bdom = bsd_to_linux_domain(sa.sa_family);
	if (bdom == -1)
		return (EINVAL);

	sa.sa_family = bdom;
	return (copyout(&sa, osa, sizeof(sa.sa_family)));
}

static int
linux_to_bsd_cmsg_type(int cmsg_type)
{

	switch (cmsg_type) {
	case LINUX_SCM_RIGHTS:
		return (SCM_RIGHTS);
	case LINUX_SCM_CREDENTIALS:
		return (SCM_CREDS);
	}
	return (-1);
}

static int
bsd_to_linux_cmsg_type(int cmsg_type)
{

	switch (cmsg_type) {
	case SCM_RIGHTS:
		return (LINUX_SCM_RIGHTS);
	case SCM_CREDS:
		return (LINUX_SCM_CREDENTIALS);
	case SCM_TIMESTAMP:
		return (LINUX_SCM_TIMESTAMP);
	}
	return (-1);
}

static int
linux_to_bsd_msghdr(struct msghdr *bhdr, const struct l_msghdr *lhdr)
{
	if (lhdr->msg_controllen > INT_MAX)
		return (ENOBUFS);

	bhdr->msg_name		= PTRIN(lhdr->msg_name);
	bhdr->msg_namelen	= lhdr->msg_namelen;
	bhdr->msg_iov		= PTRIN(lhdr->msg_iov);
	bhdr->msg_iovlen	= lhdr->msg_iovlen;
	bhdr->msg_control	= PTRIN(lhdr->msg_control);

	/*
	 * msg_controllen is skipped since BSD and LINUX control messages
	 * are potentially different sizes (e.g. the cred structure used
	 * by SCM_CREDS is different between the two operating system).
	 *
	 * The caller can set it (if necessary) after converting all the
	 * control messages.
	 */

	bhdr->msg_flags		= linux_to_bsd_msg_flags(lhdr->msg_flags);
	return (0);
}

static int
bsd_to_linux_msghdr(const struct msghdr *bhdr, struct l_msghdr *lhdr)
{
	lhdr->msg_name		= PTROUT(bhdr->msg_name);
	lhdr->msg_namelen	= bhdr->msg_namelen;
	lhdr->msg_iov		= PTROUT(bhdr->msg_iov);
	lhdr->msg_iovlen	= bhdr->msg_iovlen;
	lhdr->msg_control	= PTROUT(bhdr->msg_control);

	/*
	 * msg_controllen is skipped since BSD and LINUX control messages
	 * are potentially different sizes (e.g. the cred structure used
	 * by SCM_CREDS is different between the two operating system).
	 *
	 * The caller can set it (if necessary) after converting all the
	 * control messages.
	 */

	/* msg_flags skipped */
	return (0);
}

static int
linux_set_socket_flags(int lflags, int *flags)
{

	if (lflags & ~(LINUX_SOCK_CLOEXEC | LINUX_SOCK_NONBLOCK))
		return (EINVAL);
	if (lflags & LINUX_SOCK_NONBLOCK)
		*flags |= SOCK_NONBLOCK;
	if (lflags & LINUX_SOCK_CLOEXEC)
		*flags |= SOCK_CLOEXEC;
	return (0);
}

static int
linux_sendit(struct thread *td, int s, struct msghdr *mp, int flags,
    struct mbuf *control, enum uio_seg segflg)
{
	struct sockaddr *to;
	int error;

	if (mp->msg_name != NULL) {
		error = linux_getsockaddr(&to, mp->msg_name, mp->msg_namelen);
		if (error != 0)
			return (error);
		mp->msg_name = to;
	} else
		to = NULL;

	error = kern_sendit(td, s, mp, linux_to_bsd_msg_flags(flags), control,
	    segflg);

	if (to)
		free(to, M_SONAME);
	return (error);
}

/* Return 0 if IP_HDRINCL is set for the given socket. */
static int
linux_check_hdrincl(struct thread *td, int s)
{
	int error, optval;
	socklen_t size_val;

	size_val = sizeof(optval);
	error = kern_getsockopt(td, s, IPPROTO_IP, IP_HDRINCL,
	    &optval, UIO_SYSSPACE, &size_val);
	if (error != 0)
		return (error);

	return (optval == 0);
}

/*
 * Updated sendto() when IP_HDRINCL is set:
 * tweak endian-dependent fields in the IP packet.
 */
static int
linux_sendto_hdrincl(struct thread *td, struct linux_sendto_args *linux_args)
{
/*
 * linux_ip_copysize defines how many bytes we should copy
 * from the beginning of the IP packet before we customize it for BSD.
 * It should include all the fields we modify (ip_len and ip_off).
 */
#define linux_ip_copysize	8

	struct ip *packet;
	struct msghdr msg;
	struct iovec aiov[1];
	int error;

	/* Check that the packet isn't too big or too small. */
	if (linux_args->len < linux_ip_copysize ||
	    linux_args->len > IP_MAXPACKET)
		return (EINVAL);

	packet = (struct ip *)malloc(linux_args->len, M_LINUX, M_WAITOK);

	/* Make kernel copy of the packet to be sent */
	if ((error = copyin(PTRIN(linux_args->msg), packet,
	    linux_args->len)))
		goto goout;

	/* Convert fields from Linux to BSD raw IP socket format */
	packet->ip_len = linux_args->len;
	packet->ip_off = ntohs(packet->ip_off);

	/* Prepare the msghdr and iovec structures describing the new packet */
	msg.msg_name = PTRIN(linux_args->to);
	msg.msg_namelen = linux_args->tolen;
	msg.msg_iov = aiov;
	msg.msg_iovlen = 1;
	msg.msg_control = NULL;
	msg.msg_flags = 0;
	aiov[0].iov_base = (char *)packet;
	aiov[0].iov_len = linux_args->len;
	error = linux_sendit(td, linux_args->s, &msg, linux_args->flags,
	    NULL, UIO_SYSSPACE);
goout:
	free(packet, M_LINUX);
	return (error);
}

int
linux_socket(struct thread *td, struct linux_socket_args *args)
{
	int domain, retval_socket, type;

	type = args->type & LINUX_SOCK_TYPE_MASK;
	if (type < 0 || type > LINUX_SOCK_MAX)
		return (EINVAL);
	retval_socket = linux_set_socket_flags(args->type & ~LINUX_SOCK_TYPE_MASK,
		&type);
	if (retval_socket != 0)
		return (retval_socket);
	domain = linux_to_bsd_domain(args->domain);
	if (domain == -1)
		return (EAFNOSUPPORT);

	retval_socket = kern_socket(td, domain, type, args->protocol);
	if (retval_socket)
		return (retval_socket);

	if (type == SOCK_RAW
	    && (args->protocol == IPPROTO_RAW || args->protocol == 0)
	    && domain == PF_INET) {
		/* It's a raw IP socket: set the IP_HDRINCL option. */
		int hdrincl;

		hdrincl = 1;
		/* We ignore any error returned by kern_setsockopt() */
		kern_setsockopt(td, td->td_retval[0], IPPROTO_IP, IP_HDRINCL,
		    &hdrincl, UIO_SYSSPACE, sizeof(hdrincl));
	}
#ifdef INET6
	/*
	 * Linux AF_INET6 socket has IPV6_V6ONLY setsockopt set to 0 by default
	 * and some apps depend on this. So, set V6ONLY to 0 for Linux apps.
	 * For simplicity we do this unconditionally of the net.inet6.ip6.v6only
	 * sysctl value.
	 */
	if (domain == PF_INET6) {
		int v6only;

		v6only = 0;
		/* We ignore any error returned by setsockopt() */
		kern_setsockopt(td, td->td_retval[0], IPPROTO_IPV6, IPV6_V6ONLY,
		    &v6only, UIO_SYSSPACE, sizeof(v6only));
	}
#endif

	return (retval_socket);
}

int
linux_bind(struct thread *td, struct linux_bind_args *args)
{
	struct sockaddr *sa;
	int error;

	error = linux_getsockaddr(&sa, PTRIN(args->name),
	    args->namelen);
	if (error != 0)
		return (error);

	error = kern_bindat(td, AT_FDCWD, args->s, sa);
	free(sa, M_SONAME);
	if (error == EADDRNOTAVAIL && args->namelen != sizeof(struct sockaddr_in))
		return (EINVAL);
	return (error);
}

int
linux_connect(struct thread *td, struct linux_connect_args *args)
{
	struct socket *so;
	struct sockaddr *sa;
	struct file *fp;
	u_int fflag;
	int error;

	error = linux_getsockaddr(&sa, (struct osockaddr *)PTRIN(args->name),
	    args->namelen);
	if (error != 0)
		return (error);

	error = kern_connectat(td, AT_FDCWD, args->s, sa);
	free(sa, M_SONAME);
	if (error != EISCONN)
		return (error);

	/*
	 * Linux doesn't return EISCONN the first time it occurs,
	 * when on a non-blocking socket. Instead it returns the
	 * error getsockopt(SOL_SOCKET, SO_ERROR) would return on BSD.
	 */
	error = getsock_cap(td, args->s, &cap_connect_rights,
	    &fp, &fflag, NULL);
	if (error != 0)
		return (error);

	error = EISCONN;
	so = fp->f_data;
	if (fflag & FNONBLOCK) {
		SOCK_LOCK(so);
		if (so->so_emuldata == 0)
			error = so->so_error;
		so->so_emuldata = (void *)1;
		SOCK_UNLOCK(so);
	}
	fdrop(fp, td);

	return (error);
}

int
linux_listen(struct thread *td, struct linux_listen_args *args)
{

	return (kern_listen(td, args->s, args->backlog));
}

static int
linux_accept_common(struct thread *td, int s, l_uintptr_t addr,
    l_uintptr_t namelen, int flags)
{
	struct accept4_args /* {
		int	s;
		struct sockaddr * __restrict name;
		socklen_t * __restrict anamelen;
		int	flags;
	} */ bsd_args;
	struct socket *so;
	struct file *fp;
	int error, error1;

	bsd_args.s = s;
	bsd_args.name = (struct sockaddr * __restrict)PTRIN(addr);
	bsd_args.anamelen = PTRIN(namelen);
	bsd_args.flags = 0;
	error = linux_set_socket_flags(flags, &bsd_args.flags);
	if (error != 0)
		return (error);
	error = sys_accept4(td, &bsd_args);
	bsd_to_linux_sockaddr((struct sockaddr *)bsd_args.name);
	if (error != 0) {
		if (error == EFAULT && namelen != sizeof(struct sockaddr_in))
			return (EINVAL);
		if (error == EINVAL) {
			error1 = getsock_cap(td, s, &cap_accept_rights, &fp, NULL, NULL);
			if (error1 != 0)
				return (error1);
			so = fp->f_data;
			if (so->so_type == SOCK_DGRAM) {
				fdrop(fp, td);
				return (EOPNOTSUPP);
			}
			fdrop(fp, td);
		}
		return (error);
	}
	if (addr)
		error = linux_sa_put(PTRIN(addr));
	if (error != 0) {
		(void)kern_close(td, td->td_retval[0]);
		td->td_retval[0] = 0;
	}
	return (error);
}

int
linux_accept(struct thread *td, struct linux_accept_args *args)
{

	return (linux_accept_common(td, args->s, args->addr,
	    args->namelen, 0));
}

int
linux_accept4(struct thread *td, struct linux_accept4_args *args)
{

	return (linux_accept_common(td, args->s, args->addr,
	    args->namelen, args->flags));
}

int
linux_getsockname(struct thread *td, struct linux_getsockname_args *args)
{
	struct getsockname_args /* {
		int	fdes;
		struct sockaddr * __restrict asa;
		socklen_t * __restrict alen;
	} */ bsd_args;
	int error;

	bsd_args.fdes = args->s;
	bsd_args.asa = (struct sockaddr * __restrict)PTRIN(args->addr);
	bsd_args.alen = PTRIN(args->namelen);
	error = sys_getsockname(td, &bsd_args);
	bsd_to_linux_sockaddr((struct sockaddr *)bsd_args.asa);
	if (error != 0)
		return (error);
	return (linux_sa_put(PTRIN(args->addr)));
}

int
linux_getpeername(struct thread *td, struct linux_getpeername_args *args)
{
	struct getpeername_args /* {
		int fdes;
		caddr_t asa;
		int *alen;
	} */ bsd_args;
	int error;

	bsd_args.fdes = args->s;
	bsd_args.asa = (struct sockaddr *)PTRIN(args->addr);
	bsd_args.alen = (socklen_t *)PTRIN(args->namelen);
	error = sys_getpeername(td, &bsd_args);
	bsd_to_linux_sockaddr((struct sockaddr *)bsd_args.asa);
	if (error != 0)
		return (error);
	return (linux_sa_put(PTRIN(args->addr)));
}

int
linux_socketpair(struct thread *td, struct linux_socketpair_args *args)
{
	struct socketpair_args /* {
		int domain;
		int type;
		int protocol;
		int *rsv;
	} */ bsd_args;
	int error;

	bsd_args.domain = linux_to_bsd_domain(args->domain);
	if (bsd_args.domain != PF_LOCAL)
		return (EAFNOSUPPORT);
	bsd_args.type = args->type & LINUX_SOCK_TYPE_MASK;
	if (bsd_args.type < 0 || bsd_args.type > LINUX_SOCK_MAX)
		return (EINVAL);
	error = linux_set_socket_flags(args->type & ~LINUX_SOCK_TYPE_MASK,
		&bsd_args.type);
	if (error != 0)
		return (error);
	if (args->protocol != 0 && args->protocol != PF_UNIX)

		/*
		 * Use of PF_UNIX as protocol argument is not right,
		 * but Linux does it.
		 * Do not map PF_UNIX as its Linux value is identical
		 * to FreeBSD one.
		 */
		return (EPROTONOSUPPORT);
	else
		bsd_args.protocol = 0;
	bsd_args.rsv = (int *)PTRIN(args->rsv);
	return (sys_socketpair(td, &bsd_args));
}

#if defined(__i386__) || (defined(__amd64__) && defined(COMPAT_LINUX32))
struct linux_send_args {
	register_t s;
	register_t msg;
	register_t len;
	register_t flags;
};

static int
linux_send(struct thread *td, struct linux_send_args *args)
{
	struct sendto_args /* {
		int s;
		caddr_t buf;
		int len;
		int flags;
		caddr_t to;
		int tolen;
	} */ bsd_args;

	bsd_args.s = args->s;
	bsd_args.buf = (caddr_t)PTRIN(args->msg);
	bsd_args.len = args->len;
	bsd_args.flags = args->flags;
	bsd_args.to = NULL;
	bsd_args.tolen = 0;
	return (sys_sendto(td, &bsd_args));
}

struct linux_recv_args {
	register_t s;
	register_t msg;
	register_t len;
	register_t flags;
};

static int
linux_recv(struct thread *td, struct linux_recv_args *args)
{
	struct recvfrom_args /* {
		int s;
		caddr_t buf;
		int len;
		int flags;
		struct sockaddr *from;
		socklen_t fromlenaddr;
	} */ bsd_args;

	bsd_args.s = args->s;
	bsd_args.buf = (caddr_t)PTRIN(args->msg);
	bsd_args.len = args->len;
	bsd_args.flags = linux_to_bsd_msg_flags(args->flags);
	bsd_args.from = NULL;
	bsd_args.fromlenaddr = 0;
	return (sys_recvfrom(td, &bsd_args));
}
#endif /* __i386__ || (__amd64__ && COMPAT_LINUX32) */

int
linux_sendto(struct thread *td, struct linux_sendto_args *args)
{
	struct msghdr msg;
	struct iovec aiov;

	if (linux_check_hdrincl(td, args->s) == 0)
		/* IP_HDRINCL set, tweak the packet before sending */
		return (linux_sendto_hdrincl(td, args));

	msg.msg_name = PTRIN(args->to);
	msg.msg_namelen = args->tolen;
	msg.msg_iov = &aiov;
	msg.msg_iovlen = 1;
	msg.msg_control = NULL;
	msg.msg_flags = 0;
	aiov.iov_base = PTRIN(args->msg);
	aiov.iov_len = args->len;
	return (linux_sendit(td, args->s, &msg, args->flags, NULL,
	    UIO_USERSPACE));
}

int
linux_recvfrom(struct thread *td, struct linux_recvfrom_args *args)
{
	struct msghdr msg;
	struct iovec aiov;
	int error, fromlen;

	if (PTRIN(args->fromlen) != NULL) {
		error = copyin(PTRIN(args->fromlen), &fromlen,
		    sizeof(fromlen));
		if (error != 0)
			return (error);
		if (fromlen < 0)
			return (EINVAL);
		msg.msg_namelen = fromlen;
	} else
		msg.msg_namelen = 0;

	msg.msg_name = (struct sockaddr * __restrict)PTRIN(args->from);
	msg.msg_iov = &aiov;
	msg.msg_iovlen = 1;
	aiov.iov_base = PTRIN(args->buf);
	aiov.iov_len = args->len;
	msg.msg_control = 0;
	msg.msg_flags = linux_to_bsd_msg_flags(args->flags);

	error = kern_recvit(td, args->s, &msg, UIO_USERSPACE, NULL);
	if (error != 0)
		return (error);

	if (PTRIN(args->from) != NULL) {
		error = bsd_to_linux_sockaddr((struct sockaddr *)
		    PTRIN(args->from));
		if (error != 0)
			return (error);

		error = linux_sa_put((struct osockaddr *)
		    PTRIN(args->from));
	}

	if (PTRIN(args->fromlen) != NULL)
		error = copyout(&msg.msg_namelen, PTRIN(args->fromlen),
		    sizeof(msg.msg_namelen));

	return (error);
}

static int
linux_sendmsg_common(struct thread *td, l_int s, struct l_msghdr *msghdr,
    l_uint flags)
{
	struct cmsghdr *cmsg;
	struct mbuf *control;
	struct msghdr msg;
	struct l_cmsghdr linux_cmsg;
	struct l_cmsghdr *ptr_cmsg;
	struct l_msghdr linux_msg;
	struct iovec *iov;
	socklen_t datalen;
	struct sockaddr *sa;
	sa_family_t sa_family;
	void *data;
	l_size_t len;
	l_size_t clen;
	int error;

	error = copyin(msghdr, &linux_msg, sizeof(linux_msg));
	if (error != 0)
		return (error);

	/*
	 * Some Linux applications (ping) define a non-NULL control data
	 * pointer, but a msg_controllen of 0, which is not allowed in the
	 * FreeBSD system call interface.  NULL the msg_control pointer in
	 * order to handle this case.  This should be checked, but allows the
	 * Linux ping to work.
	 */
	if (PTRIN(linux_msg.msg_control) != NULL && linux_msg.msg_controllen == 0)
		linux_msg.msg_control = PTROUT(NULL);

	error = linux_to_bsd_msghdr(&msg, &linux_msg);
	if (error != 0)
		return (error);

#ifdef COMPAT_LINUX32
	error = linux32_copyiniov(PTRIN(msg.msg_iov), msg.msg_iovlen,
	    &iov, EMSGSIZE);
#else
	error = copyiniov(msg.msg_iov, msg.msg_iovlen, &iov, EMSGSIZE);
#endif
	if (error != 0)
		return (error);

	control = NULL;

	if (linux_msg.msg_controllen >= sizeof(struct l_cmsghdr)) {
		error = kern_getsockname(td, s, &sa, &datalen);
		if (error != 0)
			goto bad;
		sa_family = sa->sa_family;
		free(sa, M_SONAME);

		error = ENOBUFS;
		control = m_get(M_WAITOK, MT_CONTROL);
		MCLGET(control, M_WAITOK);
		data = mtod(control, void *);
		datalen = 0;

		ptr_cmsg = PTRIN(linux_msg.msg_control);
		clen = linux_msg.msg_controllen;
		do {
			error = copyin(ptr_cmsg, &linux_cmsg,
			    sizeof(struct l_cmsghdr));
			if (error != 0)
				goto bad;

			error = EINVAL;
			if (linux_cmsg.cmsg_len < sizeof(struct l_cmsghdr) ||
			    linux_cmsg.cmsg_len > clen)
				goto bad;

			if (datalen + CMSG_HDRSZ > MCLBYTES)
				goto bad;

			/*
			 * Now we support only SCM_RIGHTS and SCM_CRED,
			 * so return EINVAL in any other cmsg_type
			 */
			cmsg = data;
			cmsg->cmsg_type =
			    linux_to_bsd_cmsg_type(linux_cmsg.cmsg_type);
			cmsg->cmsg_level =
			    linux_to_bsd_sockopt_level(linux_cmsg.cmsg_level);
			if (cmsg->cmsg_type == -1
			    || cmsg->cmsg_level != SOL_SOCKET)
				goto bad;

			/*
			 * Some applications (e.g. pulseaudio) attempt to
			 * send ancillary data even if the underlying protocol
			 * doesn't support it which is not allowed in the
			 * FreeBSD system call interface.
			 */
			if (sa_family != AF_UNIX)
				continue;

			if (cmsg->cmsg_type == SCM_CREDS) {
				len = sizeof(struct cmsgcred);
				if (datalen + CMSG_SPACE(len) > MCLBYTES)
					goto bad;

				/*
				 * The lower levels will fill in the structure
				 */
				memset(CMSG_DATA(data), 0, len);
			} else {
				len = linux_cmsg.cmsg_len - L_CMSG_HDRSZ;
				if (datalen + CMSG_SPACE(len) < datalen ||
				    datalen + CMSG_SPACE(len) > MCLBYTES)
					goto bad;

				error = copyin(LINUX_CMSG_DATA(ptr_cmsg),
				    CMSG_DATA(data), len);
				if (error != 0)
					goto bad;
			}

			cmsg->cmsg_len = CMSG_LEN(len);
			data = (char *)data + CMSG_SPACE(len);
			datalen += CMSG_SPACE(len);

			if (clen <= LINUX_CMSG_ALIGN(linux_cmsg.cmsg_len))
				break;

			clen -= LINUX_CMSG_ALIGN(linux_cmsg.cmsg_len);
			ptr_cmsg = (struct l_cmsghdr *)((char *)ptr_cmsg +
			    LINUX_CMSG_ALIGN(linux_cmsg.cmsg_len));
		} while(clen >= sizeof(struct l_cmsghdr));

		control->m_len = datalen;
		if (datalen == 0) {
			m_freem(control);
			control = NULL;
		}
	}

	msg.msg_iov = iov;
	msg.msg_flags = 0;
	error = linux_sendit(td, s, &msg, flags, control, UIO_USERSPACE);
	control = NULL;

bad:
	m_freem(control);
	free(iov, M_IOV);
	return (error);
}

int
linux_sendmsg(struct thread *td, struct linux_sendmsg_args *args)
{

	return (linux_sendmsg_common(td, args->s, PTRIN(args->msg),
	    args->flags));
}

int
linux_sendmmsg(struct thread *td, struct linux_sendmmsg_args *args)
{
	struct l_mmsghdr *msg;
	l_uint retval;
	int error, datagrams;

	if (args->vlen > UIO_MAXIOV)
		args->vlen = UIO_MAXIOV;

	msg = PTRIN(args->msg);
	datagrams = 0;
	while (datagrams < args->vlen) {
		error = linux_sendmsg_common(td, args->s, &msg->msg_hdr,
		    args->flags);
		if (error != 0)
			break;

		retval = td->td_retval[0];
		error = copyout(&retval, &msg->msg_len, sizeof(msg->msg_len));
		if (error != 0)
			break;
		++msg;
		++datagrams;
	}
	if (error == 0)
		td->td_retval[0] = datagrams;
	return (error);
}

static int
linux_recvmsg_common(struct thread *td, l_int s, struct l_msghdr *msghdr,
    l_uint flags, struct msghdr *msg)
{
	struct cmsghdr *cm;
	struct cmsgcred *cmcred;
	struct l_cmsghdr *linux_cmsg = NULL;
	struct l_ucred linux_ucred;
	socklen_t datalen, maxlen, outlen;
	struct l_msghdr linux_msg;
	struct iovec *iov, *uiov;
	struct mbuf *control = NULL;
	struct mbuf **controlp;
	struct timeval *ftmvl;
	l_timeval ltmvl;
	caddr_t outbuf;
	void *data;
	int error, i, fd, fds, *fdp;

	error = copyin(msghdr, &linux_msg, sizeof(linux_msg));
	if (error != 0)
		return (error);

	error = linux_to_bsd_msghdr(msg, &linux_msg);
	if (error != 0)
		return (error);

#ifdef COMPAT_LINUX32
	error = linux32_copyiniov(PTRIN(msg->msg_iov), msg->msg_iovlen,
	    &iov, EMSGSIZE);
#else
	error = copyiniov(msg->msg_iov, msg->msg_iovlen, &iov, EMSGSIZE);
#endif
	if (error != 0)
		return (error);

	if (msg->msg_name) {
		error = linux_to_bsd_sockaddr((struct sockaddr *)msg->msg_name,
		    msg->msg_namelen);
		if (error != 0)
			goto bad;
	}

	uiov = msg->msg_iov;
	msg->msg_iov = iov;
	controlp = (msg->msg_control != NULL) ? &control : NULL;
	error = kern_recvit(td, s, msg, UIO_USERSPACE, controlp);
	msg->msg_iov = uiov;
	if (error != 0)
		goto bad;

	error = bsd_to_linux_msghdr(msg, &linux_msg);
	if (error != 0)
		goto bad;

	if (linux_msg.msg_name) {
		error = bsd_to_linux_sockaddr((struct sockaddr *)
		    PTRIN(linux_msg.msg_name));
		if (error != 0)
			goto bad;
	}
	if (linux_msg.msg_name && linux_msg.msg_namelen > 2) {
		error = linux_sa_put(PTRIN(linux_msg.msg_name));
		if (error != 0)
			goto bad;
	}

	maxlen = linux_msg.msg_controllen;
	linux_msg.msg_controllen = 0;
	if (control) {
		linux_cmsg = malloc(L_CMSG_HDRSZ, M_LINUX, M_WAITOK | M_ZERO);

		msg->msg_control = mtod(control, struct cmsghdr *);
		msg->msg_controllen = control->m_len;

		cm = CMSG_FIRSTHDR(msg);
		outbuf = PTRIN(linux_msg.msg_control);
		outlen = 0;
		while (cm != NULL) {
			linux_cmsg->cmsg_type =
			    bsd_to_linux_cmsg_type(cm->cmsg_type);
			linux_cmsg->cmsg_level =
			    bsd_to_linux_sockopt_level(cm->cmsg_level);
			if (linux_cmsg->cmsg_type == -1 ||
			    cm->cmsg_level != SOL_SOCKET) {
				error = EINVAL;
				goto bad;
			}

			data = CMSG_DATA(cm);
			datalen = (caddr_t)cm + cm->cmsg_len - (caddr_t)data;

			switch (cm->cmsg_type) {
			case SCM_RIGHTS:
				if (flags & LINUX_MSG_CMSG_CLOEXEC) {
					fds = datalen / sizeof(int);
					fdp = data;
					for (i = 0; i < fds; i++) {
						fd = *fdp++;
						(void)kern_fcntl(td, fd,
						    F_SETFD, FD_CLOEXEC);
					}
				}
				break;

			case SCM_CREDS:
				/*
				 * Currently LOCAL_CREDS is never in
				 * effect for Linux so no need to worry
				 * about sockcred
				 */
				if (datalen != sizeof(*cmcred)) {
					error = EMSGSIZE;
					goto bad;
				}
				cmcred = (struct cmsgcred *)data;
				bzero(&linux_ucred, sizeof(linux_ucred));
				linux_ucred.pid = cmcred->cmcred_pid;
				linux_ucred.uid = cmcred->cmcred_uid;
				linux_ucred.gid = cmcred->cmcred_gid;
				data = &linux_ucred;
				datalen = sizeof(linux_ucred);
				break;

			case SCM_TIMESTAMP:
				if (datalen != sizeof(struct timeval)) {
					error = EMSGSIZE;
					goto bad;
				}
				ftmvl = (struct timeval *)data;
				ltmvl.tv_sec = ftmvl->tv_sec;
				ltmvl.tv_usec = ftmvl->tv_usec;
				data = &ltmvl;
				datalen = sizeof(ltmvl);
				break;
			}

			if (outlen + LINUX_CMSG_LEN(datalen) > maxlen) {
				if (outlen == 0) {
					error = EMSGSIZE;
					goto bad;
				} else {
					linux_msg.msg_flags |= LINUX_MSG_CTRUNC;
					m_dispose_extcontrolm(control);
					goto out;
				}
			}

			linux_cmsg->cmsg_len = LINUX_CMSG_LEN(datalen);

			error = copyout(linux_cmsg, outbuf, L_CMSG_HDRSZ);
			if (error != 0)
				goto bad;
			outbuf += L_CMSG_HDRSZ;

			error = copyout(data, outbuf, datalen);
			if (error != 0)
				goto bad;

			outbuf += LINUX_CMSG_ALIGN(datalen);
			outlen += LINUX_CMSG_LEN(datalen);

			cm = CMSG_NXTHDR(msg, cm);
		}
		linux_msg.msg_controllen = outlen;
	}

out:
	error = copyout(&linux_msg, msghdr, sizeof(linux_msg));

bad:
	if (control != NULL) {
		if (error != 0)
			m_dispose_extcontrolm(control);
		m_freem(control);
	}
	free(iov, M_IOV);
	free(linux_cmsg, M_LINUX);

	return (error);
}

int
linux_recvmsg(struct thread *td, struct linux_recvmsg_args *args)
{
	struct msghdr bsd_msg;

	return (linux_recvmsg_common(td, args->s, PTRIN(args->msg),
	    args->flags, &bsd_msg));
}

int
linux_recvmmsg(struct thread *td, struct linux_recvmmsg_args *args)
{
	struct l_mmsghdr *msg;
	struct msghdr bsd_msg;
	struct l_timespec lts;
	struct timespec ts, tts;
	l_uint retval;
	int error, datagrams;

	if (args->timeout) {
		error = copyin(args->timeout, &lts, sizeof(struct l_timespec));
		if (error != 0)
			return (error);
		error = linux_to_native_timespec(&ts, &lts);
		if (error != 0)
			return (error);
		getnanotime(&tts);
		timespecadd(&tts, &ts, &tts);
	}

	msg = PTRIN(args->msg);
	datagrams = 0;
	while (datagrams < args->vlen) {
		error = linux_recvmsg_common(td, args->s, &msg->msg_hdr,
		    args->flags & ~LINUX_MSG_WAITFORONE, &bsd_msg);
		if (error != 0)
			break;

		retval = td->td_retval[0];
		error = copyout(&retval, &msg->msg_len, sizeof(msg->msg_len));
		if (error != 0)
			break;
		++msg;
		++datagrams;

		/*
		 * MSG_WAITFORONE turns on MSG_DONTWAIT after one packet.
		 */
		if (args->flags & LINUX_MSG_WAITFORONE)
			args->flags |= LINUX_MSG_DONTWAIT;

		/*
		 * See BUGS section of recvmmsg(2).
		 */
		if (args->timeout) {
			getnanotime(&ts);
			timespecsub(&ts, &tts, &ts);
			if (!timespecisset(&ts) || ts.tv_sec > 0)
				break;
		}
		/* Out of band data, return right away. */
		if (bsd_msg.msg_flags & MSG_OOB)
			break;
	}
	if (error == 0)
		td->td_retval[0] = datagrams;
	return (error);
}

int
linux_shutdown(struct thread *td, struct linux_shutdown_args *args)
{

	return (kern_shutdown(td, args->s, args->how));
}

int
linux_setsockopt(struct thread *td, struct linux_setsockopt_args *args)
{
	struct setsockopt_args /* {
		int s;
		int level;
		int name;
		const void *val;
		int valsize;
	} */ bsd_args;
	l_timeval linux_tv;
	struct timeval tv;
	int error, name;

	bsd_args.s = args->s;
	bsd_args.level = linux_to_bsd_sockopt_level(args->level);
	switch (bsd_args.level) {
	case SOL_SOCKET:
		name = linux_to_bsd_so_sockopt(args->optname);
		switch (name) {
		case SO_RCVTIMEO:
			/* FALLTHROUGH */
		case SO_SNDTIMEO:
			error = copyin(PTRIN(args->optval), &linux_tv,
			    sizeof(linux_tv));
			if (error != 0)
				return (error);
			tv.tv_sec = linux_tv.tv_sec;
			tv.tv_usec = linux_tv.tv_usec;
			return (kern_setsockopt(td, args->s, bsd_args.level,
			    name, &tv, UIO_SYSSPACE, sizeof(tv)));
			/* NOTREACHED */
		default:
			break;
		}
		break;
	case IPPROTO_IP:
		name = linux_to_bsd_ip_sockopt(args->optname);
		break;
	case IPPROTO_IPV6:
		name = linux_to_bsd_ip6_sockopt(args->optname);
		break;
	case IPPROTO_TCP:
		name = linux_to_bsd_tcp_sockopt(args->optname);
		break;
	default:
		name = -1;
		break;
	}
	if (name == -1)
		return (ENOPROTOOPT);

	bsd_args.name = name;
	bsd_args.val = PTRIN(args->optval);
	bsd_args.valsize = args->optlen;

	if (name == IPV6_NEXTHOP) {
		linux_to_bsd_sockaddr(__DECONST(struct sockaddr *,
		    bsd_args.val), bsd_args.valsize);
		error = sys_setsockopt(td, &bsd_args);
		bsd_to_linux_sockaddr(__DECONST(struct sockaddr *,
		    bsd_args.val));
	} else
		error = sys_setsockopt(td, &bsd_args);

	return (error);
}

int
linux_getsockopt(struct thread *td, struct linux_getsockopt_args *args)
{
	struct getsockopt_args /* {
		int s;
		int level;
		int name;
		caddr_t val;
		int *avalsize;
	} */ bsd_args;
	l_timeval linux_tv;
	struct timeval tv;
	socklen_t tv_len, xulen, len;
	struct xucred xu;
	struct l_ucred lxu;
	int error, name, newval;

	bsd_args.s = args->s;
	bsd_args.level = linux_to_bsd_sockopt_level(args->level);
	switch (bsd_args.level) {
	case SOL_SOCKET:
		name = linux_to_bsd_so_sockopt(args->optname);
		switch (name) {
		case SO_RCVTIMEO:
			/* FALLTHROUGH */
		case SO_SNDTIMEO:
			tv_len = sizeof(tv);
			error = kern_getsockopt(td, args->s, bsd_args.level,
			    name, &tv, UIO_SYSSPACE, &tv_len);
			if (error != 0)
				return (error);
			linux_tv.tv_sec = tv.tv_sec;
			linux_tv.tv_usec = tv.tv_usec;
			return (copyout(&linux_tv, PTRIN(args->optval),
			    sizeof(linux_tv)));
			/* NOTREACHED */
		case LOCAL_PEERCRED:
			if (args->optlen < sizeof(lxu))
				return (EINVAL);
			/*
			 * LOCAL_PEERCRED is not served at the SOL_SOCKET level,
			 * but by the Unix socket's level 0.
			 */
			bsd_args.level = 0;
			xulen = sizeof(xu);
			error = kern_getsockopt(td, args->s, bsd_args.level,
			    name, &xu, UIO_SYSSPACE, &xulen);
			if (error != 0)
				return (error);
			/*
			 * XXX Use 0 for pid as the FreeBSD does not cache peer pid.
			 */
			lxu.pid = 0;
			lxu.uid = xu.cr_uid;
			lxu.gid = xu.cr_gid;
			return (copyout(&lxu, PTRIN(args->optval), sizeof(lxu)));
			/* NOTREACHED */
		case SO_ERROR:
			len = sizeof(newval);
			error = kern_getsockopt(td, args->s, bsd_args.level,
			    name, &newval, UIO_SYSSPACE, &len);
			if (error != 0)
				return (error);
			newval = -SV_ABI_ERRNO(td->td_proc, newval);
			return (copyout(&newval, PTRIN(args->optval), len));
			/* NOTREACHED */
		default:
			break;
		}
		break;
	case IPPROTO_IP:
		name = linux_to_bsd_ip_sockopt(args->optname);
		break;
	case IPPROTO_IPV6:
		name = linux_to_bsd_ip6_sockopt(args->optname);
		break;
	case IPPROTO_TCP:
		name = linux_to_bsd_tcp_sockopt(args->optname);
		break;
	default:
		name = -1;
		break;
	}
	if (name == -1)
		return (EINVAL);

	bsd_args.name = name;
	bsd_args.val = PTRIN(args->optval);
	bsd_args.avalsize = PTRIN(args->optlen);

	if (name == IPV6_NEXTHOP) {
		error = sys_getsockopt(td, &bsd_args);
		bsd_to_linux_sockaddr((struct sockaddr *)bsd_args.val);
	} else
		error = sys_getsockopt(td, &bsd_args);

	return (error);
}

#if defined(__i386__) || (defined(__amd64__) && defined(COMPAT_LINUX32))

/* Argument list sizes for linux_socketcall */
static const unsigned char lxs_args_cnt[] = {
	0 /* unused*/,		3 /* socket */,
	3 /* bind */,		3 /* connect */,
	2 /* listen */,		3 /* accept */,
	3 /* getsockname */,	3 /* getpeername */,
	4 /* socketpair */,	4 /* send */,
	4 /* recv */,		6 /* sendto */,
	6 /* recvfrom */,	2 /* shutdown */,
	5 /* setsockopt */,	5 /* getsockopt */,
	3 /* sendmsg */,	3 /* recvmsg */,
	4 /* accept4 */,	5 /* recvmmsg */,
	4 /* sendmmsg */
};
#define	LINUX_ARGS_CNT		(nitems(lxs_args_cnt) - 1)
#define	LINUX_ARG_SIZE(x)	(lxs_args_cnt[x] * sizeof(l_ulong))

int
linux_socketcall(struct thread *td, struct linux_socketcall_args *args)
{
	l_ulong a[6];
#if defined(__amd64__) && defined(COMPAT_LINUX32)
	register_t l_args[6];
#endif
	void *arg;
	int error;

	if (args->what < LINUX_SOCKET || args->what > LINUX_ARGS_CNT)
		return (EINVAL);
	error = copyin(PTRIN(args->args), a, LINUX_ARG_SIZE(args->what));
	if (error != 0)
		return (error);

#if defined(__amd64__) && defined(COMPAT_LINUX32)
	for (int i = 0; i < lxs_args_cnt[args->what]; ++i)
		l_args[i] = a[i];
	arg = l_args;
#else
	arg = a;
#endif
	switch (args->what) {
	case LINUX_SOCKET:
		return (linux_socket(td, arg));
	case LINUX_BIND:
		return (linux_bind(td, arg));
	case LINUX_CONNECT:
		return (linux_connect(td, arg));
	case LINUX_LISTEN:
		return (linux_listen(td, arg));
	case LINUX_ACCEPT:
		return (linux_accept(td, arg));
	case LINUX_GETSOCKNAME:
		return (linux_getsockname(td, arg));
	case LINUX_GETPEERNAME:
		return (linux_getpeername(td, arg));
	case LINUX_SOCKETPAIR:
		return (linux_socketpair(td, arg));
	case LINUX_SEND:
		return (linux_send(td, arg));
	case LINUX_RECV:
		return (linux_recv(td, arg));
	case LINUX_SENDTO:
		return (linux_sendto(td, arg));
	case LINUX_RECVFROM:
		return (linux_recvfrom(td, arg));
	case LINUX_SHUTDOWN:
		return (linux_shutdown(td, arg));
	case LINUX_SETSOCKOPT:
		return (linux_setsockopt(td, arg));
	case LINUX_GETSOCKOPT:
		return (linux_getsockopt(td, arg));
	case LINUX_SENDMSG:
		return (linux_sendmsg(td, arg));
	case LINUX_RECVMSG:
		return (linux_recvmsg(td, arg));
	case LINUX_ACCEPT4:
		return (linux_accept4(td, arg));
	case LINUX_RECVMMSG:
		return (linux_recvmmsg(td, arg));
	case LINUX_SENDMMSG:
		return (linux_sendmmsg(td, arg));
	}

	uprintf("LINUX: 'socket' typ=%d not implemented\n", args->what);
	return (ENOSYS);
}
#endif /* __i386__ || (__amd64__ && COMPAT_LINUX32) */
