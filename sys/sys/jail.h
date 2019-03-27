/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Poul-Henning Kamp.
 * Copyright (c) 2009 James Gritton.
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

#ifndef _SYS_JAIL_H_
#define _SYS_JAIL_H_

#ifdef _KERNEL
struct jail_v0 {
	u_int32_t	version;
	char		*path;
	char		*hostname;
	u_int32_t	ip_number;
};
#endif

struct jail {
	uint32_t	version;
	char		*path;
	char		*hostname;
	char		*jailname;
	uint32_t	ip4s;
	uint32_t	ip6s;
	struct in_addr	*ip4;
	struct in6_addr	*ip6;
};
#define	JAIL_API_VERSION	2

/*
 * For all xprison structs, always keep the pr_version an int and
 * the first variable so userspace can easily distinguish them.
 */
#ifndef _KERNEL
struct xprison_v1 {
	int		 pr_version;
	int		 pr_id;
	char		 pr_path[MAXPATHLEN];
	char		 pr_host[MAXHOSTNAMELEN];
	u_int32_t	 pr_ip;
};
#endif

struct xprison {
	int		 pr_version;
	int		 pr_id;
	int		 pr_state;
	cpusetid_t	 pr_cpusetid;
	char		 pr_path[MAXPATHLEN];
	char		 pr_host[MAXHOSTNAMELEN];
	char		 pr_name[MAXHOSTNAMELEN];
	uint32_t	 pr_ip4s;
	uint32_t	 pr_ip6s;
#if 0
	/*
	 * sizeof(xprison) will be malloced + size needed for all
	 * IPv4 and IPv6 addesses. Offsets are based numbers of addresses.
	 */
	struct in_addr	 pr_ip4[];
	struct in6_addr	 pr_ip6[];
#endif
};
#define	XPRISON_VERSION		3

#define	PRISON_STATE_INVALID	0
#define	PRISON_STATE_ALIVE	1
#define	PRISON_STATE_DYING	2

/*
 * Flags for jail_set and jail_get.
 */
#define	JAIL_CREATE	0x01	/* Create jail if it doesn't exist */
#define	JAIL_UPDATE	0x02	/* Update parameters of existing jail */
#define	JAIL_ATTACH	0x04	/* Attach to jail upon creation */
#define	JAIL_DYING	0x08	/* Allow getting a dying jail */
#define	JAIL_SET_MASK	0x0f
#define	JAIL_GET_MASK	0x08

#define	JAIL_SYS_DISABLE	0
#define	JAIL_SYS_NEW		1
#define	JAIL_SYS_INHERIT	2

#ifndef _KERNEL

struct iovec;

int jail(struct jail *);
int jail_set(struct iovec *, unsigned int, int);
int jail_get(struct iovec *, unsigned int, int);
int jail_attach(int);
int jail_remove(int);

#else /* _KERNEL */

#include <sys/queue.h>
#include <sys/sysctl.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/_task.h>

#define JAIL_MAX	999999

#ifdef MALLOC_DECLARE
MALLOC_DECLARE(M_PRISON);
#endif
#endif /* _KERNEL */

#if defined(_KERNEL) || defined(_WANT_PRISON)

#include <sys/osd.h>

#define	HOSTUUIDLEN	64
#define	OSRELEASELEN	32

struct racct;
struct prison_racct;

/*
 * This structure describes a prison.  It is pointed to by all struct
 * ucreds's of the inmates.  pr_ref keeps track of them and is used to
 * delete the struture when the last inmate is dead.
 *
 * Lock key:
 *   (a) allprison_lock
 *   (p) locked by pr_mtx
 *   (c) set only during creation before the structure is shared, no mutex
 *       required to read
 */
struct prison {
	TAILQ_ENTRY(prison) pr_list;			/* (a) all prisons */
	int		 pr_id;				/* (c) prison id */
	int		 pr_ref;			/* (p) refcount */
	int		 pr_uref;			/* (p) user (alive) refcount */
	unsigned	 pr_flags;			/* (p) PR_* flags */
	LIST_HEAD(, prison) pr_children;		/* (a) list of child jails */
	LIST_ENTRY(prison) pr_sibling;			/* (a) next in parent's list */
	struct prison	*pr_parent;			/* (c) containing jail */
	struct mtx	 pr_mtx;
	struct task	 pr_task;			/* (c) destroy task */
	struct osd	 pr_osd;			/* (p) additional data */
	struct cpuset	*pr_cpuset;			/* (p) cpuset */
	struct vnet	*pr_vnet;			/* (c) network stack */
	struct vnode	*pr_root;			/* (c) vnode to rdir */
	int		 pr_ip4s;			/* (p) number of v4 IPs */
	int		 pr_ip6s;			/* (p) number of v6 IPs */
	struct in_addr	*pr_ip4;			/* (p) v4 IPs of jail */
	struct in6_addr	*pr_ip6;			/* (p) v6 IPs of jail */
	struct prison_racct *pr_prison_racct;		/* (c) racct jail proxy */
	void		*pr_sparep[3];
	int		 pr_childcount;			/* (a) number of child jails */
	int		 pr_childmax;			/* (p) maximum child jails */
	unsigned	 pr_allow;			/* (p) PR_ALLOW_* flags */
	int		 pr_securelevel;		/* (p) securelevel */
	int		 pr_enforce_statfs;		/* (p) statfs permission */
	int		 pr_devfs_rsnum;		/* (p) devfs ruleset */
	int		 pr_spare[3];
	int		 pr_osreldate;			/* (c) kern.osreldate value */
	unsigned long	 pr_hostid;			/* (p) jail hostid */
	char		 pr_name[MAXHOSTNAMELEN];	/* (p) admin jail name */
	char		 pr_path[MAXPATHLEN];		/* (c) chroot path */
	char		 pr_hostname[MAXHOSTNAMELEN];	/* (p) jail hostname */
	char		 pr_domainname[MAXHOSTNAMELEN];	/* (p) jail domainname */
	char		 pr_hostuuid[HOSTUUIDLEN];	/* (p) jail hostuuid */
	char		 pr_osrelease[OSRELEASELEN];	/* (c) kern.osrelease value */
};

struct prison_racct {
	LIST_ENTRY(prison_racct) prr_next;
	char		prr_name[MAXHOSTNAMELEN];
	u_int		prr_refcount;
	struct racct	*prr_racct;
};
#endif /* _KERNEL || _WANT_PRISON */

#ifdef _KERNEL
/* Flag bits set via options */
#define	PR_PERSIST	0x00000001	/* Can exist without processes */
#define	PR_HOST		0x00000002	/* Virtualize hostname et al */
#define	PR_IP4_USER	0x00000004	/* Restrict IPv4 addresses */
#define	PR_IP6_USER	0x00000008	/* Restrict IPv6 addresses */
#define	PR_VNET		0x00000010	/* Virtual network stack */
#define	PR_IP4_SADDRSEL	0x00000080	/* Do IPv4 src addr sel. or use the */
					/* primary jail address. */
#define	PR_IP6_SADDRSEL	0x00000100	/* Do IPv6 src addr sel. or use the */
					/* primary jail address. */

/* Internal flag bits */
#define	PR_IP4		0x02000000	/* IPv4 restricted or disabled */
					/* by this jail or an ancestor */
#define	PR_IP6		0x04000000	/* IPv6 restricted or disabled */
					/* by this jail or an ancestor */

/*
 * Flags for pr_allow
 * Bits not noted here may be used for dynamic allow.mount.xxxfs.
 */
#define	PR_ALLOW_SET_HOSTNAME		0x00000001
#define	PR_ALLOW_SYSVIPC		0x00000002
#define	PR_ALLOW_RAW_SOCKETS		0x00000004
#define	PR_ALLOW_CHFLAGS		0x00000008
#define	PR_ALLOW_MOUNT			0x00000010
#define	PR_ALLOW_QUOTAS			0x00000020
#define	PR_ALLOW_SOCKET_AF		0x00000040
#define	PR_ALLOW_MLOCK			0x00000080
#define	PR_ALLOW_READ_MSGBUF		0x00000100
#define	PR_ALLOW_UNPRIV_DEBUG		0x00000200
#define	PR_ALLOW_RESERVED_PORTS		0x00008000
#define	PR_ALLOW_KMEM_ACCESS		0x00010000	/* reserved, not used yet */
#define	PR_ALLOW_ALL_STATIC		0x000183ff

/*
 * PR_ALLOW_DIFFERENCES determines which flags are able to be
 * different between the parent and child jail upon creation.
 */
#define	PR_ALLOW_DIFFERENCES		(PR_ALLOW_UNPRIV_DEBUG)

/*
 * OSD methods
 */
#define	PR_METHOD_CREATE	0
#define	PR_METHOD_GET		1
#define	PR_METHOD_SET		2
#define	PR_METHOD_CHECK		3
#define	PR_METHOD_ATTACH	4
#define	PR_METHOD_REMOVE	5
#define	PR_MAXMETHOD		6

/*
 * Lock/unlock a prison.
 * XXX These exist not so much for general convenience, but to be useable in
 *     the FOREACH_PRISON_DESCENDANT_LOCKED macro which can't handle them in
 *     non-function form as currently defined.
 */
static __inline void
prison_lock(struct prison *pr)
{

	mtx_lock(&pr->pr_mtx);
}

static __inline void
prison_unlock(struct prison *pr)
{

	mtx_unlock(&pr->pr_mtx);
}

/* Traverse a prison's immediate children. */
#define	FOREACH_PRISON_CHILD(ppr, cpr)					\
	LIST_FOREACH(cpr, &(ppr)->pr_children, pr_sibling)

/*
 * Preorder traversal of all of a prison's descendants.
 * This ugly loop allows the macro to be followed by a single block
 * as expected in a looping primitive.
 */
#define	FOREACH_PRISON_DESCENDANT(ppr, cpr, descend)			\
	for ((cpr) = (ppr), (descend) = 1;				\
	    ((cpr) = (((descend) && !LIST_EMPTY(&(cpr)->pr_children))	\
	      ? LIST_FIRST(&(cpr)->pr_children)				\
	      : ((cpr) == (ppr)						\
		 ? NULL							\
		 : (((descend) = LIST_NEXT(cpr, pr_sibling) != NULL)	\
		    ? LIST_NEXT(cpr, pr_sibling)			\
		    : (cpr)->pr_parent))));)				\
		if (!(descend))						\
			;						\
		else

/*
 * As above, but lock descendants on the way down and unlock on the way up.
 */
#define	FOREACH_PRISON_DESCENDANT_LOCKED(ppr, cpr, descend)		\
	for ((cpr) = (ppr), (descend) = 1;				\
	    ((cpr) = (((descend) && !LIST_EMPTY(&(cpr)->pr_children))	\
	      ? LIST_FIRST(&(cpr)->pr_children)				\
	      : ((cpr) == (ppr)						\
		 ? NULL							\
		 : ((prison_unlock(cpr),				\
		    (descend) = LIST_NEXT(cpr, pr_sibling) != NULL)	\
		    ? LIST_NEXT(cpr, pr_sibling)			\
		    : (cpr)->pr_parent))));)				\
		if ((descend) ? (prison_lock(cpr), 0) : 1)		\
			;						\
		else

/*
 * As above, but also keep track of the level descended to.
 */
#define	FOREACH_PRISON_DESCENDANT_LOCKED_LEVEL(ppr, cpr, descend, level)\
	for ((cpr) = (ppr), (descend) = 1, (level) = 0;			\
	    ((cpr) = (((descend) && !LIST_EMPTY(&(cpr)->pr_children))	\
	      ? (level++, LIST_FIRST(&(cpr)->pr_children))		\
	      : ((cpr) == (ppr)						\
		 ? NULL							\
		 : ((prison_unlock(cpr),				\
		    (descend) = LIST_NEXT(cpr, pr_sibling) != NULL)	\
		    ? LIST_NEXT(cpr, pr_sibling)			\
		    : (level--, (cpr)->pr_parent)))));)			\
		if ((descend) ? (prison_lock(cpr), 0) : 1)		\
			;						\
		else

/*
 * Attributes of the physical system, and the root of the jail tree.
 */
extern struct	prison prison0;

TAILQ_HEAD(prisonlist, prison);
extern struct	prisonlist allprison;
extern struct	sx allprison_lock;

/*
 * Sysctls to describe jail parameters.
 */
SYSCTL_DECL(_security_jail_param);

#define	SYSCTL_JAIL_PARAM(module, param, type, fmt, descr)		\
    SYSCTL_PROC(_security_jail_param ## module, OID_AUTO, param,	\
	(type) | CTLFLAG_MPSAFE, NULL, 0, sysctl_jail_param, fmt, descr)
#define	SYSCTL_JAIL_PARAM_STRING(module, param, access, len, descr)	\
    SYSCTL_PROC(_security_jail_param ## module, OID_AUTO, param,	\
	CTLTYPE_STRING | CTLFLAG_MPSAFE | (access), NULL, len,		\
	sysctl_jail_param, "A", descr)
#define	SYSCTL_JAIL_PARAM_STRUCT(module, param, access, len, fmt, descr)\
    SYSCTL_PROC(_security_jail_param ## module, OID_AUTO, param,	\
	CTLTYPE_STRUCT | CTLFLAG_MPSAFE | (access), NULL, len,		\
	sysctl_jail_param, fmt, descr)
#define	SYSCTL_JAIL_PARAM_NODE(module, descr)				\
    SYSCTL_NODE(_security_jail_param, OID_AUTO, module, 0, 0, descr)
#define	SYSCTL_JAIL_PARAM_SUBNODE(parent, module, descr)		\
    SYSCTL_NODE(_security_jail_param_##parent, OID_AUTO, module, 0, 0, descr)
#define	SYSCTL_JAIL_PARAM_SYS_NODE(module, access, descr)		\
    SYSCTL_JAIL_PARAM_NODE(module, descr);				\
    SYSCTL_JAIL_PARAM(_##module, , CTLTYPE_INT | (access), "E,jailsys",	\
	descr)

/*
 * Kernel support functions for jail().
 */
struct ucred;
struct mount;
struct sockaddr;
struct statfs;
struct vfsconf;
int jailed(struct ucred *cred);
int jailed_without_vnet(struct ucred *);
void getcredhostname(struct ucred *, char *, size_t);
void getcreddomainname(struct ucred *, char *, size_t);
void getcredhostuuid(struct ucred *, char *, size_t);
void getcredhostid(struct ucred *, unsigned long *);
void prison0_init(void);
int prison_allow(struct ucred *, unsigned);
int prison_check(struct ucred *cred1, struct ucred *cred2);
int prison_owns_vnet(struct ucred *);
int prison_canseemount(struct ucred *cred, struct mount *mp);
void prison_enforce_statfs(struct ucred *cred, struct mount *mp,
    struct statfs *sp);
struct prison *prison_find(int prid);
struct prison *prison_find_child(struct prison *, int);
struct prison *prison_find_name(struct prison *, const char *);
int prison_flag(struct ucred *, unsigned);
void prison_free(struct prison *pr);
void prison_free_locked(struct prison *pr);
void prison_hold(struct prison *pr);
void prison_hold_locked(struct prison *pr);
void prison_proc_hold(struct prison *);
void prison_proc_free(struct prison *);
int prison_ischild(struct prison *, struct prison *);
int prison_equal_ip4(struct prison *, struct prison *);
int prison_get_ip4(struct ucred *cred, struct in_addr *ia);
int prison_local_ip4(struct ucred *cred, struct in_addr *ia);
int prison_remote_ip4(struct ucred *cred, struct in_addr *ia);
int prison_check_ip4(const struct ucred *, const struct in_addr *);
int prison_check_ip4_locked(const struct prison *, const struct in_addr *);
int prison_saddrsel_ip4(struct ucred *, struct in_addr *);
int prison_restrict_ip4(struct prison *, struct in_addr *);
int prison_qcmp_v4(const void *, const void *);
#ifdef INET6
int prison_equal_ip6(struct prison *, struct prison *);
int prison_get_ip6(struct ucred *, struct in6_addr *);
int prison_local_ip6(struct ucred *, struct in6_addr *, int);
int prison_remote_ip6(struct ucred *, struct in6_addr *);
int prison_check_ip6(const struct ucred *, const struct in6_addr *);
int prison_check_ip6_locked(const struct prison *, const struct in6_addr *);
int prison_saddrsel_ip6(struct ucred *, struct in6_addr *);
int prison_restrict_ip6(struct prison *, struct in6_addr *);
int prison_qcmp_v6(const void *, const void *);
#endif
int prison_check_af(struct ucred *cred, int af);
int prison_if(struct ucred *cred, struct sockaddr *sa);
char *prison_name(struct prison *, struct prison *);
int prison_priv_check(struct ucred *cred, int priv);
int sysctl_jail_param(SYSCTL_HANDLER_ARGS);
unsigned prison_add_allow(const char *prefix, const char *name,
    const char *prefix_descr, const char *descr);
void prison_add_vfs(struct vfsconf *vfsp);
void prison_racct_foreach(void (*callback)(struct racct *racct,
    void *arg2, void *arg3), void (*pre)(void), void (*post)(void),
    void *arg2, void *arg3);
struct prison_racct *prison_racct_find(const char *name);
void prison_racct_hold(struct prison_racct *prr);
void prison_racct_free(struct prison_racct *prr);

#endif /* _KERNEL */
#endif /* !_SYS_JAIL_H_ */
