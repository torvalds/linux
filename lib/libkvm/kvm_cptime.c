/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2008 Yahoo!, Inc.
 * All rights reserved.
 * Written by: John Baldwin <jhb@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
#include <sys/pcpu.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <errno.h>
#include <kvm.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "kvm_private.h"

static struct nlist kvm_cp_time_nl[] = {
	{ .n_name = "_cp_time" },		/* (deprecated) */
	{ .n_name = NULL },
};

#define	NL_CP_TIME		0

static int kvm_cp_time_cached;

static int
_kvm_cp_time_init(kvm_t *kd)
{

	if (kvm_nlist(kd, kvm_cp_time_nl) < 0)
		return (-1);
	kvm_cp_time_cached = 1;
	return (0);
}

static int
getsysctl(kvm_t *kd, const char *name, void *buf, size_t len)
{
	size_t nlen;

	nlen = len;
	if (sysctlbyname(name, buf, &nlen, NULL, 0) < 0) {
		_kvm_err(kd, kd->program, "cannot read sysctl %s:%s", name,
		    strerror(errno));
		return (-1);
	}
	if (nlen != len) {
		_kvm_err(kd, kd->program, "sysctl %s has unexpected size",
		    name);
		return (-1);
	}
	return (0);
}

int
kvm_getcptime(kvm_t *kd, long *cp_time)
{
	struct pcpu *pc;
	int i, j, maxcpu;

	if (kd == NULL) {
		kvm_cp_time_cached = 0;
		return (0);
	}

	if (ISALIVE(kd))
		return (getsysctl(kd, "kern.cp_time", cp_time, sizeof(long) *
		    CPUSTATES));

	if (!kd->arch->ka_native(kd)) {
		_kvm_err(kd, kd->program,
		    "cannot read cp_time from non-native core");
		return (-1);
	}

	if (kvm_cp_time_cached == 0) {
		if (_kvm_cp_time_init(kd) < 0)
			return (-1);
	}

	/* If this kernel has a "cp_time[]" symbol, then just read that. */
	if (kvm_cp_time_nl[NL_CP_TIME].n_value != 0) {
		if (kvm_read(kd, kvm_cp_time_nl[NL_CP_TIME].n_value, cp_time,
		    sizeof(long) * CPUSTATES) != sizeof(long) * CPUSTATES) {
			_kvm_err(kd, kd->program, "cannot read cp_time array");
			return (-1);
		}
		return (0);
	}

	/*
	 * If we don't have that symbol, then we have to simulate
	 * "cp_time[]" by adding up the individual times for each CPU.
	 */
	maxcpu = kvm_getmaxcpu(kd);
	if (maxcpu < 0)
		return (-1);
	for (i = 0; i < CPUSTATES; i++)
		cp_time[i] = 0;
	for (i = 0; i < maxcpu; i++) {
		pc = kvm_getpcpu(kd, i);
		if (pc == NULL)
			continue;
		if (pc == (void *)-1)
			return (-1);
		for (j = 0; j < CPUSTATES; j++)
			cp_time[j] += pc->pc_cp_time[j];
		free(pc);
	}
	return (0);
}
