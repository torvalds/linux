/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2001 Dag-Erling Coïdan Smørgrav
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 *
 *      $FreeBSD$
 */

#ifndef _PSEUDOFS_INTERNAL_H_INCLUDED
#define _PSEUDOFS_INTERNAL_H_INCLUDED

/*
 * Sysctl subtree
 */
SYSCTL_DECL(_vfs_pfs);

/*
 * Vnode data
 */
struct pfs_vdata {
	struct pfs_node	*pvd_pn;
	pid_t		 pvd_pid;
	struct vnode	*pvd_vnode;
	struct pfs_vdata*pvd_prev, *pvd_next;
	int		 pvd_dead:1;
};

/*
 * Vnode cache
 */
void	 pfs_vncache_load	(void);
void	 pfs_vncache_unload	(void);
int	 pfs_vncache_alloc	(struct mount *, struct vnode **,
				 struct pfs_node *, pid_t pid);
int	 pfs_vncache_free	(struct vnode *);

/*
 * File number bitmap
 */
void	 pfs_fileno_init	(struct pfs_info *);
void	 pfs_fileno_uninit	(struct pfs_info *);
void	 pfs_fileno_alloc	(struct pfs_node *);
void	 pfs_fileno_free	(struct pfs_node *);

/*
 * Debugging
 */
#ifdef PSEUDOFS_TRACE
extern int pfs_trace;

#define PFS_TRACE(foo) \
	do { \
		if (pfs_trace) { \
			printf("%s(): line %d: ", __func__, __LINE__); \
			printf foo ; \
			printf("\n"); \
		} \
	} while (0)
#define PFS_RETURN(err) \
	do { \
		if (pfs_trace) { \
			printf("%s(): line %d: returning %d\n", \
			    __func__, __LINE__, err); \
		} \
		return (err); \
	} while (0)
#else
#define PFS_TRACE(foo) \
	do { /* nothing */ } while (0)
#define PFS_RETURN(err) \
	return (err)
#endif

/*
 * Inline helpers for locking
 */
static inline void
pfs_lock(struct pfs_node *pn)
{

	mtx_lock(&pn->pn_mutex);
}

static inline void
pfs_unlock(struct pfs_node *pn)
{

	mtx_unlock(&pn->pn_mutex);
}

static inline void
pfs_assert_owned(struct pfs_node *pn)
{

	mtx_assert(&pn->pn_mutex, MA_OWNED);
}

static inline void
pfs_assert_not_owned(struct pfs_node *pn)
{

	mtx_assert(&pn->pn_mutex, MA_NOTOWNED);
}

static inline int
pn_fill(PFS_FILL_ARGS)
{

	PFS_TRACE(("%s", pn->pn_name));
	KASSERT(pn->pn_fill != NULL, ("%s(): no callback", __func__));
	if (p != NULL) {
		PROC_LOCK_ASSERT(p, MA_NOTOWNED);
		PROC_ASSERT_HELD(p);
	}
	pfs_assert_not_owned(pn);
	return ((pn->pn_fill)(PFS_FILL_ARGNAMES));
}

static inline int
pn_attr(PFS_ATTR_ARGS)
{

	PFS_TRACE(("%s", pn->pn_name));
	KASSERT(pn->pn_attr != NULL, ("%s(): no callback", __func__));
	if (p != NULL)
		PROC_LOCK_ASSERT(p, MA_OWNED);
	pfs_assert_not_owned(pn);
	return ((pn->pn_attr)(PFS_ATTR_ARGNAMES));
}

static inline int
pn_vis(PFS_VIS_ARGS)
{

	PFS_TRACE(("%s", pn->pn_name));
	KASSERT(pn->pn_vis != NULL, ("%s(): no callback", __func__));
	KASSERT(p != NULL, ("%s(): no process", __func__));
	PROC_LOCK_ASSERT(p, MA_OWNED);
	pfs_assert_not_owned(pn);
	return ((pn->pn_vis)(PFS_VIS_ARGNAMES));
}

static inline int
pn_ioctl(PFS_IOCTL_ARGS)
{

	PFS_TRACE(("%s", pn->pn_name));
	KASSERT(pn->pn_ioctl != NULL, ("%s(): no callback", __func__));
	if (p != NULL)
		PROC_LOCK_ASSERT(p, MA_OWNED);
	pfs_assert_not_owned(pn);
	return ((pn->pn_ioctl)(PFS_IOCTL_ARGNAMES));
}

static inline int
pn_getextattr(PFS_GETEXTATTR_ARGS)
{

	PFS_TRACE(("%s", pn->pn_name));
	KASSERT(pn->pn_getextattr != NULL, ("%s(): no callback", __func__));
	if (p != NULL)
		PROC_LOCK_ASSERT(p, MA_OWNED);
	pfs_assert_not_owned(pn);
	return ((pn->pn_getextattr)(PFS_GETEXTATTR_ARGNAMES));
}

static inline int
pn_close(PFS_CLOSE_ARGS)
{

	PFS_TRACE(("%s", pn->pn_name));
	KASSERT(pn->pn_close != NULL, ("%s(): no callback", __func__));
	if (p != NULL)
		PROC_LOCK_ASSERT(p, MA_OWNED);
	pfs_assert_not_owned(pn);
	return ((pn->pn_close)(PFS_CLOSE_ARGNAMES));
}

static inline int
pn_destroy(PFS_DESTROY_ARGS)
{

	PFS_TRACE(("%s", pn->pn_name));
	KASSERT(pn->pn_destroy != NULL, ("%s(): no callback", __func__));
	pfs_assert_not_owned(pn);
	return ((pn->pn_destroy)(PFS_DESTROY_ARGNAMES));
}

#endif
