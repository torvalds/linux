/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008, David Xu <davidxu@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "namespace.h"
#include <pthread_np.h>
#include <sys/param.h>
#include <sys/cpuset.h>
#include "un-namespace.h"

#include "thr_private.h"

__weak_reference(_pthread_getaffinity_np, pthread_getaffinity_np);
__weak_reference(_pthread_setaffinity_np, pthread_setaffinity_np);

int
_pthread_setaffinity_np(pthread_t td, size_t cpusetsize, const cpuset_t *cpusetp)
{
	struct pthread	*curthread = _get_curthread();
	lwpid_t		tid;
	int		error;

	if (td == curthread) {
		error = cpuset_setaffinity(CPU_LEVEL_WHICH, CPU_WHICH_TID,
			-1, cpusetsize, cpusetp);
		if (error == -1)
			error = errno;
	} else if ((error = _thr_find_thread(curthread, td, 0)) == 0) {
		tid = TID(td);
		error = cpuset_setaffinity(CPU_LEVEL_WHICH, CPU_WHICH_TID, tid,
			cpusetsize, cpusetp);
		if (error == -1)
			error = errno;
		THR_THREAD_UNLOCK(curthread, td);
	}
	return (error);
}

int
_pthread_getaffinity_np(pthread_t td, size_t cpusetsize, cpuset_t *cpusetp)
{
	struct pthread	*curthread = _get_curthread();
	lwpid_t tid;
	int error;

	if (td == curthread) {
		error = cpuset_getaffinity(CPU_LEVEL_WHICH, CPU_WHICH_TID,
			-1, cpusetsize, cpusetp);
		if (error == -1)
			error = errno;
	} else if ((error = _thr_find_thread(curthread, td, 0)) == 0) {
		tid = TID(td);
		error = cpuset_getaffinity(CPU_LEVEL_WHICH, CPU_WHICH_TID, tid,
			    cpusetsize, cpusetp);
		if (error == -1)
			error = errno;
		THR_THREAD_UNLOCK(curthread, td);
	}
	return (error);
}
