/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993
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
 *	@(#)kvm.h	8.1 (Berkeley) 6/2/93
 * $FreeBSD$
 */

#ifndef _KVM_H_
#define	_KVM_H_

#include <sys/cdefs.h>
#include <sys/types.h>
#include <nlist.h>

/*
 * Including vm/vm.h causes namespace pollution issues.  For the
 * most part, only things using kvm_walk_pages() need to #include it.
 */
#ifndef VM_H
typedef u_char vm_prot_t;
#endif

/* Default version symbol. */
#define	VRS_SYM		"_version"
#define	VRS_KEY		"VERSION"

#ifndef _SIZE_T_DECLARED
typedef	__size_t	size_t;
#define	_SIZE_T_DECLARED
#endif

#ifndef _SSIZE_T_DECLARED
typedef	__ssize_t	ssize_t;
#define	_SSIZE_T_DECLARED
#endif

struct kvm_nlist {
	const char *n_name;
	unsigned char n_type;
	kvaddr_t n_value;
};

typedef struct __kvm kvm_t;

struct kinfo_proc;
struct proc;

struct kvm_swap {
	char	ksw_devname[32];
	u_int	ksw_used;
	u_int	ksw_total;
	int	ksw_flags;
	u_int	ksw_reserved1;
	u_int	ksw_reserved2;
};

struct kvm_page {
	unsigned int version;
	u_long paddr;
	u_long kmap_vaddr;
	u_long dmap_vaddr;
	vm_prot_t prot;
	u_long offset;
	size_t len;
	/* end of version 1 */
};

#define SWIF_DEV_PREFIX	0x0002
#define	LIBKVM_WALK_PAGES_VERSION	1

__BEGIN_DECLS
int	  kvm_close(kvm_t *);
int	  kvm_dpcpu_setcpu(kvm_t *, unsigned int);
char	**kvm_getargv(kvm_t *, const struct kinfo_proc *, int);
int	  kvm_getcptime(kvm_t *, long *);
char	**kvm_getenvv(kvm_t *, const struct kinfo_proc *, int);
char	 *kvm_geterr(kvm_t *);
int	  kvm_getloadavg(kvm_t *, double [], int);
int	  kvm_getmaxcpu(kvm_t *);
int	  kvm_getncpus(kvm_t *);
void	 *kvm_getpcpu(kvm_t *, int);
uint64_t  kvm_counter_u64_fetch(kvm_t *, u_long);
struct kinfo_proc *
	  kvm_getprocs(kvm_t *, int, int, int *);
int	  kvm_getswapinfo(kvm_t *, struct kvm_swap *, int, int);
int	  kvm_native(kvm_t *);
int	  kvm_nlist(kvm_t *, struct nlist *);
int	  kvm_nlist2(kvm_t *, struct kvm_nlist *);
kvm_t	 *kvm_open
	    (const char *, const char *, const char *, int, const char *);
kvm_t	 *kvm_openfiles
	    (const char *, const char *, const char *, int, char *);
kvm_t	 *kvm_open2
	    (const char *, const char *, int, char *,
	    int (*)(const char *, kvaddr_t *));
ssize_t	  kvm_read(kvm_t *, unsigned long, void *, size_t);
ssize_t	  kvm_read_zpcpu(kvm_t *, unsigned long, void *, size_t, int);
ssize_t	  kvm_read2(kvm_t *, kvaddr_t, void *, size_t);
ssize_t	  kvm_write(kvm_t *, unsigned long, const void *, size_t);

typedef int kvm_walk_pages_cb_t(struct kvm_page *, void *);
int kvm_walk_pages(kvm_t *, kvm_walk_pages_cb_t *, void *);
__END_DECLS

#endif /* !_KVM_H_ */
