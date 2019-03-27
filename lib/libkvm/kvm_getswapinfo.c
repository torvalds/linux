/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999, Matthew Dillon.  All Rights Reserved.
 * Copyright (c) 2001, Thomas Moestl.  All Rights Reserved.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
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
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/blist.h>
#include <sys/queue.h>
#include <sys/sysctl.h>

#include <vm/swap_pager.h>
#include <vm/vm_param.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <kvm.h>
#include <nlist.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "kvm_private.h"

static struct nlist kvm_swap_nl[] = {
	{ .n_name = "_swtailq" },	/* list of swap devices and sizes */
	{ .n_name = "_dmmax" },		/* maximum size of a swap block */
	{ .n_name = NULL }
};

#define NL_SWTAILQ	0
#define NL_DMMAX	1

static int kvm_swap_nl_cached = 0;
static int unswdev;  /* number of found swap dev's */
static int dmmax;

static int  kvm_getswapinfo_kvm(kvm_t *, struct kvm_swap *, int, int);
static int  kvm_getswapinfo_sysctl(kvm_t *, struct kvm_swap *, int, int);
static int  nlist_init(kvm_t *);
static int  getsysctl(kvm_t *, const char *, void *, size_t);

#define KREAD(kd, addr, obj) \
	(kvm_read(kd, addr, (char *)(obj), sizeof(*obj)) != sizeof(*obj))
#define	KGET(idx, var)							\
	KGET2(kvm_swap_nl[(idx)].n_value, var, kvm_swap_nl[(idx)].n_name)
#define KGET2(addr, var, msg)						\
	if (KREAD(kd, (u_long)(addr), (var))) {				\
		_kvm_err(kd, kd->program, "cannot read %s", msg);	\
		return (-1);						\
	}

#define GETSWDEVNAME(dev, str, flags)					\
	if (dev == NODEV) {						\
		strlcpy(str, "[NFS swap]", sizeof(str));		\
	} else {							\
		snprintf(						\
		    str, sizeof(str),"%s%s",				\
		    ((flags & SWIF_DEV_PREFIX) ? _PATH_DEV : ""),	\
		    devname(dev, S_IFCHR)				\
		);							\
	}

int
kvm_getswapinfo(kvm_t *kd, struct kvm_swap *swap_ary, int swap_max, int flags)
{

	/*
	 * clear cache
	 */
	if (kd == NULL) {
		kvm_swap_nl_cached = 0;
		return(0);
	}

	if (ISALIVE(kd)) {
		return kvm_getswapinfo_sysctl(kd, swap_ary, swap_max, flags);
	} else {
		return kvm_getswapinfo_kvm(kd, swap_ary, swap_max, flags);
	}
}

int
kvm_getswapinfo_kvm(kvm_t *kd, struct kvm_swap *swap_ary, int swap_max,
    int flags)
{
	int i;
	swblk_t ttl;
	TAILQ_HEAD(, swdevt) swtailq;
	struct swdevt *sp, swinfo;
	struct kvm_swap tot;

	if (!kd->arch->ka_native(kd)) {
		_kvm_err(kd, kd->program,
		    "cannot read swapinfo from non-native core");
		return (-1);
	}

	if (!nlist_init(kd))
		return (-1);

	bzero(&tot, sizeof(tot));
	KGET(NL_SWTAILQ, &swtailq);
	sp = TAILQ_FIRST(&swtailq);
	for (i = 0; sp != NULL; i++) {
		KGET2(sp, &swinfo, "swinfo");
		ttl = swinfo.sw_nblks - dmmax;
		if (i < swap_max - 1) {
			bzero(&swap_ary[i], sizeof(swap_ary[i]));
			swap_ary[i].ksw_total = ttl;
			swap_ary[i].ksw_used = swinfo.sw_used;
			swap_ary[i].ksw_flags = swinfo.sw_flags;
			GETSWDEVNAME(swinfo.sw_dev, swap_ary[i].ksw_devname,
			     flags);
		}
		tot.ksw_total += ttl;
		tot.ksw_used += swinfo.sw_used;
		sp = TAILQ_NEXT(&swinfo, sw_list);
	}

	if (i >= swap_max)
		i = swap_max - 1;
	if (i >= 0)
		swap_ary[i] = tot;

        return(i);
}

#define	GETSYSCTL(kd, name, var)					\
	    getsysctl(kd, name, &(var), sizeof(var))

/* The maximum MIB length for vm.swap_info and an additional device number */
#define	SWI_MAXMIB	3

int
kvm_getswapinfo_sysctl(kvm_t *kd, struct kvm_swap *swap_ary, int swap_max,
    int flags)
{
	int ti;
	swblk_t ttl;
	size_t mibi, len;
	int soid[SWI_MAXMIB];
	struct xswdev xsd;
	struct kvm_swap tot;

	if (!GETSYSCTL(kd, "vm.dmmax", dmmax))
		return -1;

	mibi = SWI_MAXMIB - 1;
	if (sysctlnametomib("vm.swap_info", soid, &mibi) == -1) {
		_kvm_err(kd, kd->program, "sysctlnametomib failed: %s",
		    strerror(errno));
		return -1;
	}
	bzero(&tot, sizeof(tot));
	for (unswdev = 0;; unswdev++) {
		soid[mibi] = unswdev;
		len = sizeof(xsd);
		if (sysctl(soid, mibi + 1, &xsd, &len, NULL, 0) == -1) {
			if (errno == ENOENT)
				break;
			_kvm_err(kd, kd->program, "cannot read sysctl: %s.",
			    strerror(errno));
			return -1;
		}
		if (len != sizeof(xsd)) {
			_kvm_err(kd, kd->program, "struct xswdev has unexpected "
			    "size;  kernel and libkvm out of sync?");
			return -1;
		}
		if (xsd.xsw_version != XSWDEV_VERSION) {
			_kvm_err(kd, kd->program, "struct xswdev version "
			    "mismatch; kernel and libkvm out of sync?");
			return -1;
		}

		ttl = xsd.xsw_nblks - dmmax;
		if (unswdev < swap_max - 1) {
			bzero(&swap_ary[unswdev], sizeof(swap_ary[unswdev]));
			swap_ary[unswdev].ksw_total = ttl;
			swap_ary[unswdev].ksw_used = xsd.xsw_used;
			swap_ary[unswdev].ksw_flags = xsd.xsw_flags;
			GETSWDEVNAME(xsd.xsw_dev, swap_ary[unswdev].ksw_devname,
			     flags);
		}
		tot.ksw_total += ttl;
		tot.ksw_used += xsd.xsw_used;
	}

	ti = unswdev;
	if (ti >= swap_max)
		ti = swap_max - 1;
	if (ti >= 0)
		swap_ary[ti] = tot;

        return(ti);
}

static int
nlist_init(kvm_t *kd)
{

	if (kvm_swap_nl_cached)
		return (1);

	if (kvm_nlist(kd, kvm_swap_nl) < 0)
		return (0);

	/* Required entries */
	if (kvm_swap_nl[NL_SWTAILQ].n_value == 0) {
		_kvm_err(kd, kd->program, "unable to find swtailq");
		return (0);
	}

	if (kvm_swap_nl[NL_DMMAX].n_value == 0) {
		_kvm_err(kd, kd->program, "unable to find dmmax");
		return (0);
	}

	/* Get globals, type of swap */
	KGET(NL_DMMAX, &dmmax);

	kvm_swap_nl_cached = 1;
	return (1);
}

static int
getsysctl(kvm_t *kd, const char *name, void *ptr, size_t len)
{
	size_t nlen = len;
	if (sysctlbyname(name, ptr, &nlen, NULL, 0) == -1) {
		_kvm_err(kd, kd->program, "cannot read sysctl %s:%s", name,
		    strerror(errno));
		return (0);
	}
	if (nlen != len) {
		_kvm_err(kd, kd->program, "sysctl %s has unexpected size", name);
		return (0);
	}
	return (1);
}
