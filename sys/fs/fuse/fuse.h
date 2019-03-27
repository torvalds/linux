/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2007-2009 Google Inc.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above
 *   copyright notice, this list of conditions and the following disclaimer
 *   in the documentation and/or other materials provided with the
 *   distribution.
 * * Neither the name of Google Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * Copyright (C) 2005 Csaba Henk.
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
 *
 * $FreeBSD$
 */

#include "fuse_kernel.h"

#define FUSE_DEFAULT_DAEMON_TIMEOUT                60     /* s */
#define FUSE_MIN_DAEMON_TIMEOUT                    0      /* s */
#define FUSE_MAX_DAEMON_TIMEOUT                    600    /* s */

#ifndef FUSE_FREEBSD_VERSION
#define	FUSE_FREEBSD_VERSION	"0.4.4"
#endif

/* Mapping versions to features */

#define FUSE_KERNELABI_GEQ(maj, min)	\
(FUSE_KERNEL_VERSION > (maj) || (FUSE_KERNEL_VERSION == (maj) && FUSE_KERNEL_MINOR_VERSION >= (min)))

/*
 * Appearance of new FUSE operations is not always in par with version
 * numbering... At least, 7.3 is a sufficient condition for having
 * FUSE_{ACCESS,CREATE}.
 */
#if FUSE_KERNELABI_GEQ(7, 3)
#ifndef FUSE_HAS_ACCESS
#define FUSE_HAS_ACCESS 1
#endif
#ifndef FUSE_HAS_CREATE
#define FUSE_HAS_CREATE 1
#endif
#else /* FUSE_KERNELABI_GEQ(7, 3) */
#ifndef FUSE_HAS_ACCESS
#define FUSE_HAS_ACCESS 0
#endif
#ifndef FUSE_HAS_CREATE
#define FUSE_HAS_CREATE 0
#endif
#endif

#if FUSE_KERNELABI_GEQ(7, 7)
#ifndef FUSE_HAS_GETLK
#define FUSE_HAS_GETLK 1
#endif
#ifndef FUSE_HAS_SETLK
#define FUSE_HAS_SETLK 1
#endif
#ifndef FUSE_HAS_SETLKW
#define FUSE_HAS_SETLKW 1
#endif
#ifndef FUSE_HAS_INTERRUPT
#define FUSE_HAS_INTERRUPT 1
#endif
#else /* FUSE_KERNELABI_GEQ(7, 7) */
#ifndef FUSE_HAS_GETLK
#define FUSE_HAS_GETLK 0
#endif
#ifndef FUSE_HAS_SETLK
#define FUSE_HAS_SETLK 0
#endif
#ifndef FUSE_HAS_SETLKW
#define FUSE_HAS_SETLKW 0
#endif
#ifndef FUSE_HAS_INTERRUPT
#define FUSE_HAS_INTERRUPT 0
#endif
#endif

#if FUSE_KERNELABI_GEQ(7, 8)
#ifndef FUSE_HAS_FLUSH_RELEASE
#define FUSE_HAS_FLUSH_RELEASE 1
/*
 * "DESTROY" came in the middle of the 7.8 era,
 * so this is not completely exact...
 */
#ifndef FUSE_HAS_DESTROY
#define FUSE_HAS_DESTROY 1
#endif
#endif
#else /* FUSE_KERNELABI_GEQ(7, 8) */
#ifndef FUSE_HAS_FLUSH_RELEASE
#define FUSE_HAS_FLUSH_RELEASE 0
#ifndef FUSE_HAS_DESTROY
#define FUSE_HAS_DESTROY 0
#endif
#endif
#endif

/* misc */

SYSCTL_DECL(_vfs_fusefs);

/* Fuse locking */

extern struct mtx fuse_mtx;
#define FUSE_LOCK() fuse_lck_mtx_lock(fuse_mtx)
#define FUSE_UNLOCK() fuse_lck_mtx_unlock(fuse_mtx)

#define RECTIFY_TDCR(td, cred)			\
do {						\
	if (! (td))				\
		(td) = curthread;		\
	if (! (cred))				\
		(cred) = (td)->td_ucred;	\
} while (0)

/* Debug related stuff */

#ifndef FUSE_DEBUG_DEVICE
#define FUSE_DEBUG_DEVICE               0
#endif

#ifndef FUSE_DEBUG_FILE
#define FUSE_DEBUG_FILE                 0
#endif

#ifndef FUSE_DEBUG_INTERNAL
#define FUSE_DEBUG_INTERNAL             0
#endif

#ifndef FUSE_DEBUG_IO
#define FUSE_DEBUG_IO                   0
#endif

#ifndef FUSE_DEBUG_IPC
#define FUSE_DEBUG_IPC                  0
#endif

#ifndef FUSE_DEBUG_LOCK
#define FUSE_DEBUG_LOCK                 0
#endif

#ifndef FUSE_DEBUG_VFSOPS
#define FUSE_DEBUG_VFSOPS               0
#endif

#ifndef FUSE_DEBUG_VNOPS
#define FUSE_DEBUG_VNOPS                0
#endif

#ifndef FUSE_TRACE
#define FUSE_TRACE                      0
#endif

#define DEBUGX(cond, fmt, ...) do {					\
	if (((cond))) {							\
		printf("%s: " fmt, __func__, ## __VA_ARGS__);		\
	}								\
} while (0)

#define fuse_lck_mtx_lock(mtx) do {						\
	DEBUGX(FUSE_DEBUG_LOCK, "0:   lock(%s): %s@%d by %d\n",			\
	    __STRING(mtx), __func__, __LINE__, curthread->td_proc->p_pid);	\
	mtx_lock(&(mtx));							\
	DEBUGX(FUSE_DEBUG_LOCK, "1:   lock(%s): %s@%d by %d\n",			\
	    __STRING(mtx), __func__, __LINE__, curthread->td_proc->p_pid);	\
} while (0)

#define fuse_lck_mtx_unlock(mtx) do {						\
	DEBUGX(FUSE_DEBUG_LOCK, "0: unlock(%s): %s@%d by %d\n",			\
	    __STRING(mtx), __func__, __LINE__, curthread->td_proc->p_pid);	\
	mtx_unlock(&(mtx));							\
	DEBUGX(FUSE_DEBUG_LOCK, "1: unlock(%s): %s@%d by %d\n",			\
	    __STRING(mtx), __func__, __LINE__, curthread->td_proc->p_pid);	\
} while (0)

void fuse_ipc_init(void);
void fuse_ipc_destroy(void);

int fuse_device_init(void);
void fuse_device_destroy(void);
