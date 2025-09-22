/*	$OpenBSD: sysarch.h,v 1.14 2018/01/07 18:54:44 guenther Exp $	*/
/*	$NetBSD: sysarch.h,v 1.1 2003/04/26 18:39:48 fvdl Exp $	*/

#ifndef _MACHINE_SYSARCH_H_
#define _MACHINE_SYSARCH_H_

/*
 * Architecture specific syscalls (amd64)
 */
#define	AMD64_IOPL		2

struct amd64_iopl_args {
	int iopl;
};

#ifdef _KERNEL
int amd64_iopl(struct proc *, void *, register_t *);
#else

#include <sys/cdefs.h>

__BEGIN_DECLS
int amd64_iopl(int);
int sysarch(int, void *);
__END_DECLS
#endif

#endif /* !_MACHINE_SYSARCH_H_ */
