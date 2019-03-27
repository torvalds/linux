/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Edward Tomasz Napierala under sponsorship
 * from the FreeBSD Foundation.
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

/*
 * Resource accounting.
 */

#ifndef _RACCT_H_
#define	_RACCT_H_

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/stdint.h>
#include <sys/sysctl.h>

struct buf;
struct proc;
struct rctl_rule_link;
struct ucred;

/*
 * Resources.
 */
#define	RACCT_UNDEFINED		-1
#define	RACCT_CPU		0
#define	RACCT_DATA		1
#define	RACCT_STACK		2
#define	RACCT_CORE		3
#define	RACCT_RSS		4
#define	RACCT_MEMLOCK		5
#define	RACCT_NPROC		6
#define	RACCT_NOFILE		7
#define	RACCT_VMEM		8
#define	RACCT_NPTS		9
#define	RACCT_SWAP		10
#define	RACCT_NTHR		11
#define	RACCT_MSGQQUEUED	12
#define	RACCT_MSGQSIZE		13
#define	RACCT_NMSGQ		14
#define	RACCT_NSEM		15
#define	RACCT_NSEMOP		16
#define	RACCT_NSHM		17
#define	RACCT_SHMSIZE		18
#define	RACCT_WALLCLOCK		19
#define	RACCT_PCTCPU		20
#define	RACCT_READBPS		21
#define	RACCT_WRITEBPS		22
#define	RACCT_READIOPS		23
#define	RACCT_WRITEIOPS		24
#define	RACCT_MAX		RACCT_WRITEIOPS

/*
 * Resource properties.
 */
#define	RACCT_IN_MILLIONS	0x01
#define	RACCT_RECLAIMABLE	0x02
#define	RACCT_INHERITABLE	0x04
#define	RACCT_DENIABLE		0x08
#define	RACCT_SLOPPY		0x10
#define	RACCT_DECAYING		0x20

extern int racct_types[];
extern bool racct_enable;

#define ASSERT_RACCT_ENABLED()	KASSERT(racct_enable, \
				    ("%s called with !racct_enable", __func__))

/*
 * Amount stored in c_resources[] is 10**6 times bigger than what's
 * visible to the userland.  It gets fixed up when retrieving resource
 * usage or adding rules.
 */
#define	RACCT_IS_IN_MILLIONS(X)	\
    ((X) != RACCT_UNDEFINED && (racct_types[(X)] & RACCT_IN_MILLIONS) != 0)

/*
 * Resource usage can drop, as opposed to only grow.  When the process
 * terminates, its resource usage is subtracted from the respective
 * per-credential racct containers.
 */
#define	RACCT_IS_RECLAIMABLE(X)	(racct_types[X] & RACCT_RECLAIMABLE)

/*
 * Children inherit resource usage.
 */
#define	RACCT_IS_INHERITABLE(X)	(racct_types[X] & RACCT_INHERITABLE)

/*
 * racct_{add,set}(9) can actually return an error and not update resource
 * usage counters.  Note that even when resource is not deniable, allocating
 * resource might cause signals to be sent by RCTL code.
 */
#define	RACCT_IS_DENIABLE(X)		(racct_types[X] & RACCT_DENIABLE)

/*
 * Per-process resource usage information makes no sense, but per-credential
 * one does.  This kind of resources are usually allocated for process, but
 * freed using credentials.
 */
#define	RACCT_IS_SLOPPY(X)		(racct_types[X] & RACCT_SLOPPY)

/*
 * When a process terminates, its resource usage is not automatically
 * subtracted from per-credential racct containers.  Instead, the resource
 * usage of per-credential racct containers decays in time.
 * Resource usage can also drop for such resource.
 */
#define RACCT_IS_DECAYING(X)		(racct_types[X] & RACCT_DECAYING)

/*
 * Resource usage can drop, as opposed to only grow.
 */
#define RACCT_CAN_DROP(X)		(RACCT_IS_RECLAIMABLE(X) | RACCT_IS_DECAYING(X))

/*
 * The 'racct' structure defines resource consumption for a particular
 * subject, such as process or jail.
 *
 * This structure must be filled with zeroes initially.
 */
struct racct {
	int64_t				r_resources[RACCT_MAX + 1];
	LIST_HEAD(, rctl_rule_link)	r_rule_links;
};

SYSCTL_DECL(_kern_racct);

#ifdef RACCT

extern struct mtx racct_lock;

#define RACCT_LOCK()		mtx_lock(&racct_lock)
#define RACCT_UNLOCK()		mtx_unlock(&racct_lock)
#define RACCT_LOCK_ASSERT()	mtx_assert(&racct_lock, MA_OWNED)

#define RACCT_ENABLED()		__predict_false(racct_enable)

#define	RACCT_PROC_LOCK(p)	do {		\
	if (RACCT_ENABLED())			\
		PROC_LOCK(p);			\
} while (0)
#define	RACCT_PROC_UNLOCK(p)	do {		\
	if (RACCT_ENABLED())			\
		PROC_UNLOCK(p);			\
} while (0)

int	racct_add(struct proc *p, int resource, uint64_t amount);
void	racct_add_cred(struct ucred *cred, int resource, uint64_t amount);
void	racct_add_force(struct proc *p, int resource, uint64_t amount);
void	racct_add_buf(struct proc *p, const struct buf *bufp, int is_write);
int	racct_set(struct proc *p, int resource, uint64_t amount);
int	racct_set_unlocked(struct proc *p, int resource, uint64_t amount);
void	racct_set_force(struct proc *p, int resource, uint64_t amount);
void	racct_sub(struct proc *p, int resource, uint64_t amount);
void	racct_sub_cred(struct ucred *cred, int resource, uint64_t amount);
uint64_t	racct_get_limit(struct proc *p, int resource);
uint64_t	racct_get_available(struct proc *p, int resource);

void	racct_create(struct racct **racctp);
void	racct_destroy(struct racct **racctp);

int	racct_proc_fork(struct proc *parent, struct proc *child);
void	racct_proc_fork_done(struct proc *child);
void	racct_proc_exit(struct proc *p);

void	racct_proc_ucred_changed(struct proc *p, struct ucred *oldcred,
	    struct ucred *newcred);
void	racct_move(struct racct *dest, struct racct *src);
void	racct_proc_throttled(struct proc *p);
void	racct_proc_throttle(struct proc *p, int timeout);

#else

#define	RACCT_PROC_LOCK(p)	do { } while (0)
#define	RACCT_PROC_UNLOCK(p)	do { } while (0)

static inline int
racct_add(struct proc *p, int resource, uint64_t amount)
{

	return (0);
}

static inline void
racct_add_cred(struct ucred *cred, int resource, uint64_t amount)
{
}

static inline void
racct_add_force(struct proc *p, int resource, uint64_t amount)
{
}

static inline int
racct_set(struct proc *p, int resource, uint64_t amount)
{

	return (0);
}

static inline void
racct_set_force(struct proc *p, int resource, uint64_t amount)
{
}

static inline void
racct_sub(struct proc *p, int resource, uint64_t amount)
{
}

static inline void
racct_sub_cred(struct ucred *cred, int resource, uint64_t amount)
{
}

static inline uint64_t
racct_get_limit(struct proc *p, int resource)
{

	return (UINT64_MAX);
}

static inline uint64_t
racct_get_available(struct proc *p, int resource)
{

	return (UINT64_MAX);
}

#define	racct_create(x)
#define	racct_destroy(x)

static inline int
racct_proc_fork(struct proc *parent, struct proc *child)
{

	return (0);
}

static inline void
racct_proc_fork_done(struct proc *child)
{
}

static inline void
racct_proc_exit(struct proc *p)
{
}

#endif

#endif /* !_RACCT_H_ */
