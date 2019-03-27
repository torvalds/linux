/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007 Ariff Abdullah <ariff@FreeBSD.org>
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
 *
 * $FreeBSD$
 */

#ifndef _SND_CLONE_H_
#define _SND_CLONE_H_

struct snd_clone_entry;
struct snd_clone;

/*
 * 750 milisecond default deadline. Short enough to not cause excessive
 * garbage collection, long enough to indicate stalled VFS.
 */
#define SND_CLONE_DEADLINE_DEFAULT	750

/*
 * Fit within 24bit MAXMINOR.
 */
#define SND_CLONE_MAXUNIT		0xffffff

/*
 * Creation flags, mostly related to the behaviour of garbage collector.
 *
 * SND_CLONE_ENABLE     - Enable clone allocation.
 * SND_CLONE_GC_ENABLE  - Enable garbage collector operation, automatically
 *                        or if explicitly called upon.
 * SND_CLONE_GC_UNREF   - Garbage collect during unref operation.
 * SND_CLONE_GC_LASTREF - Garbage collect during last reference
 *                        (refcount = 0)
 * SND_CLONE_GC_EXPIRED - Don't garbage collect unless the global clone
 *                        handler has been expired.
 * SND_CLONE_GC_REVOKE  - Revoke clone invocation status which has been
 *                        expired instead of removing and freeing it.
 * SND_CLONE_WAITOK     - malloc() is allowed to sleep while allocating
 *                        clone entry.
 */
#define SND_CLONE_ENABLE	0x00000001
#define SND_CLONE_GC_ENABLE	0x00000002
#define SND_CLONE_GC_UNREF	0x00000004
#define SND_CLONE_GC_LASTREF	0x00000008
#define SND_CLONE_GC_EXPIRED	0x00000010
#define SND_CLONE_GC_REVOKE	0x00000020
#define SND_CLONE_WAITOK	0x80000000

#define SND_CLONE_GC_MASK	(SND_CLONE_GC_ENABLE  |			\
				 SND_CLONE_GC_UNREF   |			\
				 SND_CLONE_GC_LASTREF |			\
				 SND_CLONE_GC_EXPIRED |			\
				 SND_CLONE_GC_REVOKE)

#define SND_CLONE_MASK		(SND_CLONE_ENABLE | SND_CLONE_GC_MASK |	\
				 SND_CLONE_WAITOK)

/*
 * Runtime clone device flags
 *
 * These are mostly private to the clone manager operation:
 *
 * SND_CLONE_NEW    - New clone allocation in progress.
 * SND_CLONE_INVOKE - Cloning being invoked, waiting for next VFS operation.
 * SND_CLONE_BUSY   - In progress, being referenced by living thread/proc.
 */
#define SND_CLONE_NEW		0x00000001
#define SND_CLONE_INVOKE	0x00000002
#define SND_CLONE_BUSY		0x00000004

/*
 * Nothing important, just for convenience.
 */
#define SND_CLONE_ALLOC		(SND_CLONE_NEW | SND_CLONE_INVOKE |	\
				 SND_CLONE_BUSY)

#define SND_CLONE_DEVMASK	SND_CLONE_ALLOC


void snd_timestamp(struct timespec *);

struct snd_clone *snd_clone_create(int, int, int, uint32_t);
int snd_clone_busy(struct snd_clone *);
int snd_clone_enable(struct snd_clone *);
int snd_clone_disable(struct snd_clone *);
int snd_clone_getsize(struct snd_clone *);
int snd_clone_getmaxunit(struct snd_clone *);
int snd_clone_setmaxunit(struct snd_clone *, int);
int snd_clone_getdeadline(struct snd_clone *);
int snd_clone_setdeadline(struct snd_clone *, int);
int snd_clone_gettime(struct snd_clone *, struct timespec *);
uint32_t snd_clone_getflags(struct snd_clone *);
uint32_t snd_clone_setflags(struct snd_clone *, uint32_t);
int snd_clone_getdevtime(struct cdev *, struct timespec *);
uint32_t snd_clone_getdevflags(struct cdev *);
uint32_t snd_clone_setdevflags(struct cdev *, uint32_t);
int snd_clone_gc(struct snd_clone *);
void snd_clone_destroy(struct snd_clone *);
int snd_clone_acquire(struct cdev *);
int snd_clone_release(struct cdev *);
int snd_clone_ref(struct cdev *);
int snd_clone_unref(struct cdev *);
void snd_clone_register(struct snd_clone_entry *, struct cdev *);
struct snd_clone_entry *snd_clone_alloc(struct snd_clone *, struct cdev **,
    int *, int);

#define snd_clone_enabled(x)	((x) != NULL && 			\
				(snd_clone_getflags(x) & SND_CLONE_ENABLE))
#define snd_clone_disabled(x)	(!snd_clone_enabled(x))

#endif /* !_SND_CLONE_H */
