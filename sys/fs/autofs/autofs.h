/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2014 The FreeBSD Foundation
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

#ifndef AUTOFS_H
#define	AUTOFS_H

#define VFSTOAUTOFS(mp)    ((struct autofs_mount *)((mp)->mnt_data))

MALLOC_DECLARE(M_AUTOFS);

extern uma_zone_t autofs_request_zone;
extern uma_zone_t autofs_node_zone;

extern int autofs_debug;
extern int autofs_mount_on_stat;

#define	AUTOFS_DEBUG(X, ...)						\
	do {								\
		if (autofs_debug > 1)					\
			printf("%s: " X "\n", __func__, ## __VA_ARGS__);\
	} while (0)

#define	AUTOFS_WARN(X, ...)						\
	do {								\
		if (autofs_debug > 0) {					\
			printf("WARNING: %s: " X "\n",			\
		    	    __func__, ## __VA_ARGS__);			\
		}							\
	} while (0)

#define AUTOFS_SLOCK(X)		sx_slock(&X->am_lock)
#define AUTOFS_XLOCK(X)		sx_xlock(&X->am_lock)
#define AUTOFS_SUNLOCK(X)	sx_sunlock(&X->am_lock)
#define AUTOFS_XUNLOCK(X)	sx_xunlock(&X->am_lock)
#define AUTOFS_ASSERT_LOCKED(X)		sx_assert(&X->am_lock, SA_LOCKED)
#define AUTOFS_ASSERT_XLOCKED(X)	sx_assert(&X->am_lock, SA_XLOCKED)
#define AUTOFS_ASSERT_UNLOCKED(X)	sx_assert(&X->am_lock, SA_UNLOCKED)

struct autofs_node {
	RB_ENTRY(autofs_node)		an_link;
	char				*an_name;
	int				an_fileno;
	struct autofs_node		*an_parent;
	RB_HEAD(autofs_node_tree,
	    autofs_node)		an_children;
	struct autofs_mount		*an_mount;
	struct vnode			*an_vnode;
	struct sx			an_vnode_lock;
	bool				an_cached;
	bool				an_wildcards;
	struct callout			an_callout;
	int				an_retries;
	struct timespec			an_ctime;
};

struct autofs_mount {
	TAILQ_ENTRY(autofs_mount)	am_next;
	struct autofs_node		*am_root;
	struct mount			*am_mp;
	struct sx			am_lock;
	char				am_from[MAXPATHLEN];
	char				am_mountpoint[MAXPATHLEN];
	char				am_options[MAXPATHLEN];
	char				am_prefix[MAXPATHLEN];
	int				am_last_fileno;
};

struct autofs_request {
	TAILQ_ENTRY(autofs_request)	ar_next;
	struct autofs_mount		*ar_mount;
	int				ar_id;
	bool				ar_done;
	int				ar_error;
	bool				ar_wildcards;
	bool				ar_in_progress;
	char				ar_from[MAXPATHLEN];
	char				ar_path[MAXPATHLEN];
	char				ar_prefix[MAXPATHLEN];
	char				ar_key[MAXPATHLEN];
	char				ar_options[MAXPATHLEN];
	struct timeout_task		ar_task;
	volatile u_int			ar_refcount;
};

struct autofs_softc {
	device_t			sc_dev;
	struct cdev			*sc_cdev;
	struct cv			sc_cv;
	struct sx			sc_lock;
	TAILQ_HEAD(, autofs_request)	sc_requests;
	bool				sc_dev_opened;
	pid_t				sc_dev_sid;
	int				sc_last_request_id;
};

int	autofs_init(struct vfsconf *vfsp);
int	autofs_uninit(struct vfsconf *vfsp);
int	autofs_trigger(struct autofs_node *anp, const char *component,
	    int componentlen);
bool	autofs_cached(struct autofs_node *anp, const char *component,
	    int componentlen);
void	autofs_flush(struct autofs_mount *amp);
bool	autofs_ignore_thread(const struct thread *td);
int	autofs_node_new(struct autofs_node *parent, struct autofs_mount *amp,
	    const char *name, int namelen, struct autofs_node **anpp);
int	autofs_node_find(struct autofs_node *parent,
	    const char *name, int namelen, struct autofs_node **anpp);
void	autofs_node_delete(struct autofs_node *anp);
int	autofs_node_vn(struct autofs_node *anp, struct mount *mp,
	    int flags, struct vnode **vpp);

RB_PROTOTYPE(autofs_node_tree, autofs_node, an_link, autofs_node_cmp);

#endif /* !AUTOFS_H */
