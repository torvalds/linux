/*	$FreeBSD$	*/
/*	$NetBSD: kttcp.c,v 1.3 2002/07/03 19:36:52 thorpej Exp $	*/

/*
 * Copyright (c) 2002 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Frank van der Linden and Jason R. Thorpe for
 * Wasabi Systems, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed for the NetBSD Project by
 *	Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * kttcp.c --
 *
 *	This module provides kernel support for testing network
 *	throughput from the perspective of the kernel.  It is
 *	similar in spirit to the classic ttcp network benchmark
 *	program, the main difference being that with kttcp, the
 *	kernel is the source and sink of the data.
 *
 *	Testing like this is useful for a few reasons:
 *
 *	1. This allows us to know what kind of performance we can
 *	   expect from network applications that run in the kernel
 *	   space, such as the NFS server or the NFS client.  These
 *	   applications don't have to move the data to/from userspace,
 *	   and so benchmark programs which run in userspace don't
 *	   give us an accurate model.
 *
 *	2. Since data received is just thrown away, the receiver
 *	   is very fast.  This can provide better exercise for the
 *	   sender at the other end.
 *
 *	3. Since the NetBSD kernel currently uses a run-to-completion
 *	   scheduling model, kttcp provides a benchmark model where
 *	   preemption of the benchmark program is not an issue.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/sysctl.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/errno.h>
#include <sys/uio.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/fcntl.h>
#include <sys/protosw.h>
#include <sys/socketvar.h>
#include <sys/socket.h>
#include <sys/mbuf.h>
#include <sys/resourcevar.h>
#include <sys/proc.h>
#include <sys/module.h>

#include "kttcpio.h"

#ifndef timersub
#define timersub(tvp, uvp, vvp)						\
	do {								\
		(vvp)->tv_sec = (tvp)->tv_sec - (uvp)->tv_sec;		\
		(vvp)->tv_usec = (tvp)->tv_usec - (uvp)->tv_usec;	\
		if ((vvp)->tv_usec < 0) {				\
			(vvp)->tv_sec--;				\
			(vvp)->tv_usec += 1000000;			\
		}							\
	} while (0)
#endif

static int kttcp_send(struct thread *p, struct kttcp_io_args *);
static int kttcp_recv(struct thread *p, struct kttcp_io_args *);

static d_open_t		kttcpopen;
static d_ioctl_t	kttcpioctl;

static struct cdevsw kttcp_cdevsw = {
	.d_open =	kttcpopen,
	.d_ioctl =	kttcpioctl,
	.d_name =	"kttcp",
	.d_maj =	MAJOR_AUTO,
	.d_version =	D_VERSION,
};

static int
kttcpopen(struct cdev *dev, int flag, int mode, struct thread *td)
{
	/* Always succeeds. */
	return (0);
}

static int
kttcpioctl(struct cdev *dev, u_long cmd, caddr_t data, int flag, struct thread *td)
{
	int error;

	if ((flag & FWRITE) == 0)
		return EPERM;

	switch (cmd) {
	case KTTCP_IO_SEND:
		error = kttcp_send(td, (struct kttcp_io_args *) data);
		break;

	case KTTCP_IO_RECV:
		error = kttcp_recv(td, (struct kttcp_io_args *) data);
		break;

	default:
		return EINVAL;
	}

	return error;
}

static int nbyte = 65536;

static int
kttcp_send(struct thread *td, struct kttcp_io_args *kio)
{
	struct file *fp;
	int error;
	struct timeval t0, t1;
	unsigned long long len = 0;
	struct uio auio;
	struct iovec aiov;

	bzero(&aiov, sizeof(aiov));
	bzero(&auio, sizeof(auio));
	auio.uio_iov = &aiov;
	auio.uio_segflg = UIO_NOCOPY;

	error = fget(td, kio->kio_socket, &fp);
	if (error != 0)
		return error;

	if ((fp->f_flag & FWRITE) == 0) {
		fdrop(fp, td);
		return EBADF;
	}
	if (fp->f_type == DTYPE_SOCKET) {
		len = kio->kio_totalsize;
		microtime(&t0);
		do {
			nbyte =  MIN(len, (unsigned long long)nbyte);
			aiov.iov_len = nbyte;
			auio.uio_resid = nbyte;
			auio.uio_offset = 0;
			error = sosend((struct socket *)fp->f_data, NULL,
				       &auio, NULL, NULL, 0, td);
			len -= auio.uio_offset;
		} while (error == 0 && len != 0);
		microtime(&t1);
	} else
		error = EFTYPE;
	fdrop(fp, td);
	if (error != 0)
		return error;
	timersub(&t1, &t0, &kio->kio_elapsed);

	kio->kio_bytesdone = kio->kio_totalsize - len;

	return 0;
}

static int
kttcp_recv(struct thread *td, struct kttcp_io_args *kio)
{
	struct file *fp;
	int error;
	struct timeval t0, t1;
	unsigned long long len = 0;
	struct uio auio;
	struct iovec aiov;

	bzero(&aiov, sizeof(aiov));
	bzero(&auio, sizeof(auio));
	auio.uio_iov = &aiov;
	auio.uio_segflg = UIO_NOCOPY;

	error = fget(td, kio->kio_socket, &fp);
	if (error != 0)
		return error;

	if ((fp->f_flag & FWRITE) == 0) {
		fdrop(fp, td);
		return EBADF;
	}
	if (fp->f_type == DTYPE_SOCKET) {
		len = kio->kio_totalsize;
		microtime(&t0);
		do {
			nbyte =  MIN(len, (unsigned long long)nbyte);
			aiov.iov_len = nbyte;
			auio.uio_resid = nbyte;
			auio.uio_offset = 0;
			error = soreceive((struct socket *)fp->f_data,
					  NULL, &auio, NULL, NULL, NULL);
			len -= auio.uio_offset;
		} while (error == 0 && len > 0 && auio.uio_offset != 0);
		microtime(&t1);
		if (error == EPIPE)
			error = 0;
	} else
		error = EFTYPE;
	fdrop(fp, td);
	if (error != 0)
		return error;
	timersub(&t1, &t0, &kio->kio_elapsed);

	kio->kio_bytesdone = kio->kio_totalsize - len;

	return 0;
}

static struct cdev *kttcp_dev;

/*
 * Initialization code, both for static and dynamic loading.
 */
static int
kttcpdev_modevent(module_t mod, int type, void *unused)
{
	switch (type) {
	case MOD_LOAD:
		kttcp_dev = make_dev(&kttcp_cdevsw, 0,
				      UID_ROOT, GID_WHEEL, 0666,
				      "kttcp");
		return 0;
	case MOD_UNLOAD:
		/*XXX disallow if active sessions */
		destroy_dev(kttcp_dev);
		return 0;
	}
	return EINVAL;
}

static moduledata_t kttcpdev_mod = {
	"kttcpdev",
	kttcpdev_modevent,
	0
};
MODULE_VERSION(kttcpdev, 1);
DECLARE_MODULE(kttcpdev, kttcpdev_mod, SI_SUB_PSEUDO, SI_ORDER_ANY);
