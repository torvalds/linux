/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1989, 1993
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
 *	@(#)kern_xxx.c	8.2 (Berkeley) 11/14/93
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/kernel.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>

#include <vm/vm_param.h>

#if defined(COMPAT_43)

int
ogethostname(struct thread *td, struct ogethostname_args *uap)
{
	int name[2];
	size_t len = uap->len;

	name[0] = CTL_KERN;
	name[1] = KERN_HOSTNAME;
	return (userland_sysctl(td, name, 2, uap->hostname, &len,
	    1, 0, 0, 0, 0));
}

int
osethostname(struct thread *td, struct osethostname_args *uap)
{
	int name[2];

	name[0] = CTL_KERN;
	name[1] = KERN_HOSTNAME;
	return (userland_sysctl(td, name, 2, 0, 0, 0, uap->hostname,
	    uap->len, 0, 0));
}

#ifndef _SYS_SYSPROTO_H_
struct ogethostid_args {
	int	dummy;
};
#endif
/* ARGSUSED */
int
ogethostid(struct thread *td, struct ogethostid_args *uap)
{
	size_t len = sizeof(long);
	int name[2];

	name[0] = CTL_KERN;
	name[1] = KERN_HOSTID;
	return (kernel_sysctl(td, name, 2, (long *)td->td_retval, &len,
	    NULL, 0, NULL, 0));
}

int
osethostid(struct thread *td, struct osethostid_args *uap)
{
	int name[2];

	name[0] = CTL_KERN;
	name[1] = KERN_HOSTID;
	return (kernel_sysctl(td, name, 2, NULL, NULL, &uap->hostid,
	    sizeof(uap->hostid), NULL, 0));
}

int
oquota(struct thread *td, struct oquota_args *uap)
{

	return (ENOSYS);
}

#define	KINFO_PROC		(0<<8)
#define	KINFO_RT		(1<<8)
#define	KINFO_VNODE		(2<<8)
#define	KINFO_FILE		(3<<8)
#define	KINFO_METER		(4<<8)
#define	KINFO_LOADAVG		(5<<8)
#define	KINFO_CLOCKRATE		(6<<8)

/* Non-standard BSDI extension - only present on their 4.3 net-2 releases */
#define	KINFO_BSDI_SYSINFO	(101<<8)

/*
 * XXX this is bloat, but I hope it's better here than on the potentially
 * limited kernel stack...  -Peter
 */

static struct {
	int	bsdi_machine;		/* "i386" on BSD/386 */
/*      ^^^ this is an offset to the string, relative to the struct start */
	char	*pad0;
	long	pad1;
	long	pad2;
	long	pad3;
	u_long	pad4;
	u_long	pad5;
	u_long	pad6;

	int	bsdi_ostype;		/* "BSD/386" on BSD/386 */
	int	bsdi_osrelease;		/* "1.1" on BSD/386 */
	long	pad7;
	long	pad8;
	char	*pad9;

	long	pad10;
	long	pad11;
	int	pad12;
	long	pad13;
	quad_t	pad14;
	long	pad15;

	struct	timeval pad16;
	/* we dont set this, because BSDI's uname used gethostname() instead */
	int	bsdi_hostname;		/* hostname on BSD/386 */

	/* the actual string data is appended here */

} bsdi_si;

/*
 * this data is appended to the end of the bsdi_si structure during copyout.
 * The "char *" offsets are relative to the base of the bsdi_si struct.
 * This contains "FreeBSD\02.0-BUILT-nnnnnn\0i386\0", and these strings
 * should not exceed the length of the buffer here... (or else!! :-)
 */
static char bsdi_strings[80];	/* It had better be less than this! */

int
ogetkerninfo(struct thread *td, struct ogetkerninfo_args *uap)
{
	int error, name[6];
	size_t size;
	u_int needed = 0;

	switch (uap->op & 0xff00) {

	case KINFO_RT:
		name[0] = CTL_NET;
		name[1] = PF_ROUTE;
		name[2] = 0;
		name[3] = (uap->op & 0xff0000) >> 16;
		name[4] = uap->op & 0xff;
		name[5] = uap->arg;
		error = userland_sysctl(td, name, 6, uap->where, uap->size,
			0, 0, 0, &size, 0);
		break;

	case KINFO_VNODE:
		name[0] = CTL_KERN;
		name[1] = KERN_VNODE;
		error = userland_sysctl(td, name, 2, uap->where, uap->size,
			0, 0, 0, &size, 0);
		break;

	case KINFO_PROC:
		name[0] = CTL_KERN;
		name[1] = KERN_PROC;
		name[2] = uap->op & 0xff;
		name[3] = uap->arg;
		error = userland_sysctl(td, name, 4, uap->where, uap->size,
			0, 0, 0, &size, 0);
		break;

	case KINFO_FILE:
		name[0] = CTL_KERN;
		name[1] = KERN_FILE;
		error = userland_sysctl(td, name, 2, uap->where, uap->size,
			0, 0, 0, &size, 0);
		break;

	case KINFO_METER:
		name[0] = CTL_VM;
		name[1] = VM_TOTAL;
		error = userland_sysctl(td, name, 2, uap->where, uap->size,
			0, 0, 0, &size, 0);
		break;

	case KINFO_LOADAVG:
		name[0] = CTL_VM;
		name[1] = VM_LOADAVG;
		error = userland_sysctl(td, name, 2, uap->where, uap->size,
			0, 0, 0, &size, 0);
		break;

	case KINFO_CLOCKRATE:
		name[0] = CTL_KERN;
		name[1] = KERN_CLOCKRATE;
		error = userland_sysctl(td, name, 2, uap->where, uap->size,
			0, 0, 0, &size, 0);
		break;

	case KINFO_BSDI_SYSINFO: {
		/*
		 * this is pretty crude, but it's just enough for uname()
		 * from BSDI's 1.x libc to work.
		 *
		 * *size gives the size of the buffer before the call, and
		 * the amount of data copied after a successful call.
		 * If successful, the return value is the amount of data
		 * available, which can be larger than *size.
		 *
		 * BSDI's 2.x product apparently fails with ENOMEM if *size
		 * is too small.
		 */

		u_int left;
		char *s;

		bzero((char *)&bsdi_si, sizeof(bsdi_si));
		bzero(bsdi_strings, sizeof(bsdi_strings));

		s = bsdi_strings;

		bsdi_si.bsdi_ostype = (s - bsdi_strings) + sizeof(bsdi_si);
		strcpy(s, ostype);
		s += strlen(s) + 1;

		bsdi_si.bsdi_osrelease = (s - bsdi_strings) + sizeof(bsdi_si);
		strcpy(s, osrelease);
		s += strlen(s) + 1;

		bsdi_si.bsdi_machine = (s - bsdi_strings) + sizeof(bsdi_si);
		strcpy(s, machine);
		s += strlen(s) + 1;

		needed = sizeof(bsdi_si) + (s - bsdi_strings);

		if ((uap->where == NULL) || (uap->size == NULL)) {
			/* process is asking how much buffer to supply.. */
			size = needed;
			error = 0;
			break;
		}

		if ((error = copyin(uap->size, &size, sizeof(size))) != 0)
			break;

		/* if too much buffer supplied, trim it down */
		if (size > needed)
			size = needed;

		/* how much of the buffer is remaining */
		left = size;

		if ((error = copyout((char *)&bsdi_si, uap->where, left)) != 0)
			break;

		/* is there any point in continuing? */
		if (left > sizeof(bsdi_si)) {
			left -= sizeof(bsdi_si);
			error = copyout(&bsdi_strings,
					uap->where + sizeof(bsdi_si), left);
		}
		break;
	}

	default:
		error = EOPNOTSUPP;
		break;
	}
	if (error == 0) {
		td->td_retval[0] = needed ? needed : size;
		if (uap->size) {
			error = copyout(&size, uap->size, sizeof(size));
		}
	}
	return (error);
}
#endif /* COMPAT_43 */

#ifdef COMPAT_FREEBSD4
/*
 * This is the FreeBSD-1.1 compatible uname(2) interface.  These days it is
 * done in libc as a wrapper around a bunch of sysctl's.  This must maintain
 * the old 1.1 binary ABI.
 */
#if SYS_NMLN != 32
#error "FreeBSD-1.1 uname syscall has been broken"
#endif
#ifndef _SYS_SYSPROTO_H_
struct uname_args {
	struct utsname  *name;
};
#endif
/* ARGSUSED */
int
freebsd4_uname(struct thread *td, struct freebsd4_uname_args *uap)
{
	int name[2], error;
	size_t len;
	char *s, *us;

	name[0] = CTL_KERN;
	name[1] = KERN_OSTYPE;
	len = sizeof (uap->name->sysname);
	error = userland_sysctl(td, name, 2, uap->name->sysname, &len, 
		1, 0, 0, 0, 0);
	if (error)
		return (error);
	subyte( uap->name->sysname + sizeof(uap->name->sysname) - 1, 0);

	name[1] = KERN_HOSTNAME;
	len = sizeof uap->name->nodename;
	error = userland_sysctl(td, name, 2, uap->name->nodename, &len, 
		1, 0, 0, 0, 0);
	if (error)
		return (error);
	subyte( uap->name->nodename + sizeof(uap->name->nodename) - 1, 0);

	name[1] = KERN_OSRELEASE;
	len = sizeof uap->name->release;
	error = userland_sysctl(td, name, 2, uap->name->release, &len, 
		1, 0, 0, 0, 0);
	if (error)
		return (error);
	subyte( uap->name->release + sizeof(uap->name->release) - 1, 0);

/*
	name = KERN_VERSION;
	len = sizeof uap->name->version;
	error = userland_sysctl(td, name, 2, uap->name->version, &len, 
		1, 0, 0, 0, 0);
	if (error)
		return (error);
	subyte( uap->name->version + sizeof(uap->name->version) - 1, 0);
*/

/*
 * this stupid hackery to make the version field look like FreeBSD 1.1
 */
	for(s = version; *s && *s != '#'; s++);

	for(us = uap->name->version; *s && *s != ':'; s++) {
		error = subyte( us++, *s);
		if (error)
			return (error);
	}
	error = subyte( us++, 0);
	if (error)
		return (error);

	name[0] = CTL_HW;
	name[1] = HW_MACHINE;
	len = sizeof uap->name->machine;
	error = userland_sysctl(td, name, 2, uap->name->machine, &len, 
		1, 0, 0, 0, 0);
	if (error)
		return (error);
	subyte( uap->name->machine + sizeof(uap->name->machine) - 1, 0);
	return (0);
}

#ifndef _SYS_SYSPROTO_H_
struct getdomainname_args {
	char    *domainname;
	int     len;
};
#endif
/* ARGSUSED */
int
freebsd4_getdomainname(struct thread *td,
    struct freebsd4_getdomainname_args *uap)
{
	int name[2];
	size_t len = uap->len;

	name[0] = CTL_KERN;
	name[1] = KERN_NISDOMAINNAME;
	return (userland_sysctl(td, name, 2, uap->domainname, &len,
	    1, 0, 0, 0, 0));
}

#ifndef _SYS_SYSPROTO_H_
struct setdomainname_args {
	char    *domainname;
	int     len;
};
#endif
/* ARGSUSED */
int
freebsd4_setdomainname(struct thread *td,
    struct freebsd4_setdomainname_args *uap)
{
	int name[2];

	name[0] = CTL_KERN;
	name[1] = KERN_NISDOMAINNAME;
	return (userland_sysctl(td, name, 2, 0, 0, 0, uap->domainname,
	    uap->len, 0, 0));
}
#endif /* COMPAT_FREEBSD4 */
