/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Poul-Henning Kamp.
 * Copyright (c) 2008 Bjoern A. Zeeb.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"
#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/types.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/sysproto.h>
#include <sys/malloc.h>
#include <sys/osd.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/taskqueue.h>
#include <sys/fcntl.h>
#include <sys/jail.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/racct.h>
#include <sys/rctl.h>
#include <sys/refcount.h>
#include <sys/sx.h>
#include <sys/sysent.h>
#include <sys/namei.h>
#include <sys/mount.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/syscallsubr.h>
#include <sys/sysctl.h>
#include <sys/vnode.h>

#include <net/if.h>
#include <net/vnet.h>

#include <netinet/in.h>

#ifdef DDB
#include <ddb/ddb.h>
#endif /* DDB */

#include <security/mac/mac_framework.h>

#define	DEFAULT_HOSTUUID	"00000000-0000-0000-0000-000000000000"

MALLOC_DEFINE(M_PRISON, "prison", "Prison structures");
static MALLOC_DEFINE(M_PRISON_RACCT, "prison_racct", "Prison racct structures");

/* Keep struct prison prison0 and some code in kern_jail_set() readable. */
#ifdef INET
#ifdef INET6
#define	_PR_IP_SADDRSEL	PR_IP4_SADDRSEL|PR_IP6_SADDRSEL
#else
#define	_PR_IP_SADDRSEL	PR_IP4_SADDRSEL
#endif
#else /* !INET */
#ifdef INET6
#define	_PR_IP_SADDRSEL	PR_IP6_SADDRSEL
#else
#define	_PR_IP_SADDRSEL	0
#endif
#endif

/* prison0 describes what is "real" about the system. */
struct prison prison0 = {
	.pr_id		= 0,
	.pr_name	= "0",
	.pr_ref		= 1,
	.pr_uref	= 1,
	.pr_path	= "/",
	.pr_securelevel	= -1,
	.pr_devfs_rsnum = 0,
	.pr_childmax	= JAIL_MAX,
	.pr_hostuuid	= DEFAULT_HOSTUUID,
	.pr_children	= LIST_HEAD_INITIALIZER(prison0.pr_children),
#ifdef VIMAGE
	.pr_flags	= PR_HOST|PR_VNET|_PR_IP_SADDRSEL,
#else
	.pr_flags	= PR_HOST|_PR_IP_SADDRSEL,
#endif
	.pr_allow	= PR_ALLOW_ALL_STATIC,
};
MTX_SYSINIT(prison0, &prison0.pr_mtx, "jail mutex", MTX_DEF);

struct bool_flags {
	const char	*name;
	const char	*noname;
	unsigned	 flag;
};
struct jailsys_flags {
	const char	*name;
	unsigned	 disable;
	unsigned	 new;
};

/* allprison, allprison_racct and lastprid are protected by allprison_lock. */
struct	sx allprison_lock;
SX_SYSINIT(allprison_lock, &allprison_lock, "allprison");
struct	prisonlist allprison = TAILQ_HEAD_INITIALIZER(allprison);
LIST_HEAD(, prison_racct) allprison_racct;
int	lastprid = 0;

static int do_jail_attach(struct thread *td, struct prison *pr);
static void prison_complete(void *context, int pending);
static void prison_deref(struct prison *pr, int flags);
static char *prison_path(struct prison *pr1, struct prison *pr2);
static void prison_remove_one(struct prison *pr);
#ifdef RACCT
static void prison_racct_attach(struct prison *pr);
static void prison_racct_modify(struct prison *pr);
static void prison_racct_detach(struct prison *pr);
#endif

/* Flags for prison_deref */
#define	PD_DEREF	0x01
#define	PD_DEUREF	0x02
#define	PD_LOCKED	0x04
#define	PD_LIST_SLOCKED	0x08
#define	PD_LIST_XLOCKED	0x10

/*
 * Parameter names corresponding to PR_* flag values.  Size values are for kvm
 * as we cannot figure out the size of a sparse array, or an array without a
 * terminating entry.
 */
static struct bool_flags pr_flag_bool[] = {
	{"persist", "nopersist", PR_PERSIST},
#ifdef INET
	{"ip4.saddrsel", "ip4.nosaddrsel", PR_IP4_SADDRSEL},
#endif
#ifdef INET6
	{"ip6.saddrsel", "ip6.nosaddrsel", PR_IP6_SADDRSEL},
#endif
};
const size_t pr_flag_bool_size = sizeof(pr_flag_bool);

static struct jailsys_flags pr_flag_jailsys[] = {
	{"host", 0, PR_HOST},
#ifdef VIMAGE
	{"vnet", 0, PR_VNET},
#endif
#ifdef INET
	{"ip4", PR_IP4_USER, PR_IP4_USER},
#endif
#ifdef INET6
	{"ip6", PR_IP6_USER, PR_IP6_USER},
#endif
};
const size_t pr_flag_jailsys_size = sizeof(pr_flag_jailsys);

/* Make this array full-size so dynamic parameters can be added. */
static struct bool_flags pr_flag_allow[NBBY * NBPW] = {
	{"allow.set_hostname", "allow.noset_hostname", PR_ALLOW_SET_HOSTNAME},
	{"allow.sysvipc", "allow.nosysvipc", PR_ALLOW_SYSVIPC},
	{"allow.raw_sockets", "allow.noraw_sockets", PR_ALLOW_RAW_SOCKETS},
	{"allow.chflags", "allow.nochflags", PR_ALLOW_CHFLAGS},
	{"allow.mount", "allow.nomount", PR_ALLOW_MOUNT},
	{"allow.quotas", "allow.noquotas", PR_ALLOW_QUOTAS},
	{"allow.socket_af", "allow.nosocket_af", PR_ALLOW_SOCKET_AF},
	{"allow.mlock", "allow.nomlock", PR_ALLOW_MLOCK},
	{"allow.reserved_ports", "allow.noreserved_ports",
	 PR_ALLOW_RESERVED_PORTS},
	{"allow.read_msgbuf", "allow.noread_msgbuf", PR_ALLOW_READ_MSGBUF},
	{"allow.unprivileged_proc_debug", "allow.nounprivileged_proc_debug",
	 PR_ALLOW_UNPRIV_DEBUG},
};
const size_t pr_flag_allow_size = sizeof(pr_flag_allow);

#define	JAIL_DEFAULT_ALLOW		(PR_ALLOW_SET_HOSTNAME | \
					 PR_ALLOW_RESERVED_PORTS | \
					 PR_ALLOW_UNPRIV_DEBUG)
#define	JAIL_DEFAULT_ENFORCE_STATFS	2
#define	JAIL_DEFAULT_DEVFS_RSNUM	0
static unsigned jail_default_allow = JAIL_DEFAULT_ALLOW;
static int jail_default_enforce_statfs = JAIL_DEFAULT_ENFORCE_STATFS;
static int jail_default_devfs_rsnum = JAIL_DEFAULT_DEVFS_RSNUM;
#if defined(INET) || defined(INET6)
static unsigned jail_max_af_ips = 255;
#endif

/*
 * Initialize the parts of prison0 that can't be static-initialized with
 * constants.  This is called from proc0_init() after creating thread0 cpuset.
 */
void
prison0_init(void)
{

	prison0.pr_cpuset = cpuset_ref(thread0.td_cpuset);
	prison0.pr_osreldate = osreldate;
	strlcpy(prison0.pr_osrelease, osrelease, sizeof(prison0.pr_osrelease));
}

/*
 * struct jail_args {
 *	struct jail *jail;
 * };
 */
int
sys_jail(struct thread *td, struct jail_args *uap)
{
	uint32_t version;
	int error;
	struct jail j;

	error = copyin(uap->jail, &version, sizeof(uint32_t));
	if (error)
		return (error);

	switch (version) {
	case 0:
	{
		struct jail_v0 j0;

		/* FreeBSD single IPv4 jails. */
		bzero(&j, sizeof(struct jail));
		error = copyin(uap->jail, &j0, sizeof(struct jail_v0));
		if (error)
			return (error);
		j.version = j0.version;
		j.path = j0.path;
		j.hostname = j0.hostname;
		j.ip4s = htonl(j0.ip_number);	/* jail_v0 is host order */
		break;
	}

	case 1:
		/*
		 * Version 1 was used by multi-IPv4 jail implementations
		 * that never made it into the official kernel.
		 */
		return (EINVAL);

	case 2:	/* JAIL_API_VERSION */
		/* FreeBSD multi-IPv4/IPv6,noIP jails. */
		error = copyin(uap->jail, &j, sizeof(struct jail));
		if (error)
			return (error);
		break;

	default:
		/* Sci-Fi jails are not supported, sorry. */
		return (EINVAL);
	}
	return (kern_jail(td, &j));
}

int
kern_jail(struct thread *td, struct jail *j)
{
	struct iovec optiov[2 * (4 + nitems(pr_flag_allow)
#ifdef INET
			    + 1
#endif
#ifdef INET6
			    + 1
#endif
			    )];
	struct uio opt;
	char *u_path, *u_hostname, *u_name;
	struct bool_flags *bf;
#ifdef INET
	uint32_t ip4s;
	struct in_addr *u_ip4;
#endif
#ifdef INET6
	struct in6_addr *u_ip6;
#endif
	size_t tmplen;
	int error, enforce_statfs;

	bzero(&optiov, sizeof(optiov));
	opt.uio_iov = optiov;
	opt.uio_iovcnt = 0;
	opt.uio_offset = -1;
	opt.uio_resid = -1;
	opt.uio_segflg = UIO_SYSSPACE;
	opt.uio_rw = UIO_READ;
	opt.uio_td = td;

	/* Set permissions for top-level jails from sysctls. */
	if (!jailed(td->td_ucred)) {
		for (bf = pr_flag_allow;
		     bf < pr_flag_allow + nitems(pr_flag_allow) &&
			bf->flag != 0;
		     bf++) {
			optiov[opt.uio_iovcnt].iov_base = __DECONST(char *,
			    (jail_default_allow & bf->flag)
			    ? bf->name : bf->noname);
			optiov[opt.uio_iovcnt].iov_len =
			    strlen(optiov[opt.uio_iovcnt].iov_base) + 1;
			opt.uio_iovcnt += 2;
		}
		optiov[opt.uio_iovcnt].iov_base = "enforce_statfs";
		optiov[opt.uio_iovcnt].iov_len = sizeof("enforce_statfs");
		opt.uio_iovcnt++;
		enforce_statfs = jail_default_enforce_statfs;
		optiov[opt.uio_iovcnt].iov_base = &enforce_statfs;
		optiov[opt.uio_iovcnt].iov_len = sizeof(enforce_statfs);
		opt.uio_iovcnt++;
	}

	tmplen = MAXPATHLEN + MAXHOSTNAMELEN + MAXHOSTNAMELEN;
#ifdef INET
	ip4s = (j->version == 0) ? 1 : j->ip4s;
	if (ip4s > jail_max_af_ips)
		return (EINVAL);
	tmplen += ip4s * sizeof(struct in_addr);
#else
	if (j->ip4s > 0)
		return (EINVAL);
#endif
#ifdef INET6
	if (j->ip6s > jail_max_af_ips)
		return (EINVAL);
	tmplen += j->ip6s * sizeof(struct in6_addr);
#else
	if (j->ip6s > 0)
		return (EINVAL);
#endif
	u_path = malloc(tmplen, M_TEMP, M_WAITOK);
	u_hostname = u_path + MAXPATHLEN;
	u_name = u_hostname + MAXHOSTNAMELEN;
#ifdef INET
	u_ip4 = (struct in_addr *)(u_name + MAXHOSTNAMELEN);
#endif
#ifdef INET6
#ifdef INET
	u_ip6 = (struct in6_addr *)(u_ip4 + ip4s);
#else
	u_ip6 = (struct in6_addr *)(u_name + MAXHOSTNAMELEN);
#endif
#endif
	optiov[opt.uio_iovcnt].iov_base = "path";
	optiov[opt.uio_iovcnt].iov_len = sizeof("path");
	opt.uio_iovcnt++;
	optiov[opt.uio_iovcnt].iov_base = u_path;
	error = copyinstr(j->path, u_path, MAXPATHLEN,
	    &optiov[opt.uio_iovcnt].iov_len);
	if (error) {
		free(u_path, M_TEMP);
		return (error);
	}
	opt.uio_iovcnt++;
	optiov[opt.uio_iovcnt].iov_base = "host.hostname";
	optiov[opt.uio_iovcnt].iov_len = sizeof("host.hostname");
	opt.uio_iovcnt++;
	optiov[opt.uio_iovcnt].iov_base = u_hostname;
	error = copyinstr(j->hostname, u_hostname, MAXHOSTNAMELEN,
	    &optiov[opt.uio_iovcnt].iov_len);
	if (error) {
		free(u_path, M_TEMP);
		return (error);
	}
	opt.uio_iovcnt++;
	if (j->jailname != NULL) {
		optiov[opt.uio_iovcnt].iov_base = "name";
		optiov[opt.uio_iovcnt].iov_len = sizeof("name");
		opt.uio_iovcnt++;
		optiov[opt.uio_iovcnt].iov_base = u_name;
		error = copyinstr(j->jailname, u_name, MAXHOSTNAMELEN,
		    &optiov[opt.uio_iovcnt].iov_len);
		if (error) {
			free(u_path, M_TEMP);
			return (error);
		}
		opt.uio_iovcnt++;
	}
#ifdef INET
	optiov[opt.uio_iovcnt].iov_base = "ip4.addr";
	optiov[opt.uio_iovcnt].iov_len = sizeof("ip4.addr");
	opt.uio_iovcnt++;
	optiov[opt.uio_iovcnt].iov_base = u_ip4;
	optiov[opt.uio_iovcnt].iov_len = ip4s * sizeof(struct in_addr);
	if (j->version == 0)
		u_ip4->s_addr = j->ip4s;
	else {
		error = copyin(j->ip4, u_ip4, optiov[opt.uio_iovcnt].iov_len);
		if (error) {
			free(u_path, M_TEMP);
			return (error);
		}
	}
	opt.uio_iovcnt++;
#endif
#ifdef INET6
	optiov[opt.uio_iovcnt].iov_base = "ip6.addr";
	optiov[opt.uio_iovcnt].iov_len = sizeof("ip6.addr");
	opt.uio_iovcnt++;
	optiov[opt.uio_iovcnt].iov_base = u_ip6;
	optiov[opt.uio_iovcnt].iov_len = j->ip6s * sizeof(struct in6_addr);
	error = copyin(j->ip6, u_ip6, optiov[opt.uio_iovcnt].iov_len);
	if (error) {
		free(u_path, M_TEMP);
		return (error);
	}
	opt.uio_iovcnt++;
#endif
	KASSERT(opt.uio_iovcnt <= nitems(optiov),
		("kern_jail: too many iovecs (%d)", opt.uio_iovcnt));
	error = kern_jail_set(td, &opt, JAIL_CREATE | JAIL_ATTACH);
	free(u_path, M_TEMP);
	return (error);
}


/*
 * struct jail_set_args {
 *	struct iovec *iovp;
 *	unsigned int iovcnt;
 *	int flags;
 * };
 */
int
sys_jail_set(struct thread *td, struct jail_set_args *uap)
{
	struct uio *auio;
	int error;

	/* Check that we have an even number of iovecs. */
	if (uap->iovcnt & 1)
		return (EINVAL);

	error = copyinuio(uap->iovp, uap->iovcnt, &auio);
	if (error)
		return (error);
	error = kern_jail_set(td, auio, uap->flags);
	free(auio, M_IOV);
	return (error);
}

int
kern_jail_set(struct thread *td, struct uio *optuio, int flags)
{
	struct nameidata nd;
#ifdef INET
	struct in_addr *ip4;
#endif
#ifdef INET6
	struct in6_addr *ip6;
#endif
	struct vfsopt *opt;
	struct vfsoptlist *opts;
	struct prison *pr, *deadpr, *mypr, *ppr, *tpr;
	struct vnode *root;
	char *domain, *errmsg, *host, *name, *namelc, *p, *path, *uuid;
	char *g_path, *osrelstr;
	struct bool_flags *bf;
	struct jailsys_flags *jsf;
#if defined(INET) || defined(INET6)
	struct prison *tppr;
	void *op;
#endif
	unsigned long hid;
	size_t namelen, onamelen, pnamelen;
	int born, created, cuflags, descend, enforce;
	int error, errmsg_len, errmsg_pos;
	int gotchildmax, gotenforce, gothid, gotrsnum, gotslevel;
	int jid, jsys, len, level;
	int childmax, osreldt, rsnum, slevel;
	int fullpath_disabled;
#if defined(INET) || defined(INET6)
	int ii, ij;
#endif
#ifdef INET
	int ip4s, redo_ip4;
#endif
#ifdef INET6
	int ip6s, redo_ip6;
#endif
	uint64_t pr_allow, ch_allow, pr_flags, ch_flags;
	uint64_t pr_allow_diff;
	unsigned tallow;
	char numbuf[12];

	error = priv_check(td, PRIV_JAIL_SET);
	if (!error && (flags & JAIL_ATTACH))
		error = priv_check(td, PRIV_JAIL_ATTACH);
	if (error)
		return (error);
	mypr = td->td_ucred->cr_prison;
	if ((flags & JAIL_CREATE) && mypr->pr_childmax == 0)
		return (EPERM);
	if (flags & ~JAIL_SET_MASK)
		return (EINVAL);

	/*
	 * Check all the parameters before committing to anything.  Not all
	 * errors can be caught early, but we may as well try.  Also, this
	 * takes care of some expensive stuff (path lookup) before getting
	 * the allprison lock.
	 *
	 * XXX Jails are not filesystems, and jail parameters are not mount
	 *     options.  But it makes more sense to re-use the vfsopt code
	 *     than duplicate it under a different name.
	 */
	error = vfs_buildopts(optuio, &opts);
	if (error)
		return (error);
#ifdef INET
	ip4 = NULL;
#endif
#ifdef INET6
	ip6 = NULL;
#endif
	g_path = NULL;

	cuflags = flags & (JAIL_CREATE | JAIL_UPDATE);
	if (!cuflags) {
		error = EINVAL;
		vfs_opterror(opts, "no valid operation (create or update)");
		goto done_errmsg;
	}

	error = vfs_copyopt(opts, "jid", &jid, sizeof(jid));
	if (error == ENOENT)
		jid = 0;
	else if (error != 0)
		goto done_free;

	error = vfs_copyopt(opts, "securelevel", &slevel, sizeof(slevel));
	if (error == ENOENT)
		gotslevel = 0;
	else if (error != 0)
		goto done_free;
	else
		gotslevel = 1;

	error =
	    vfs_copyopt(opts, "children.max", &childmax, sizeof(childmax));
	if (error == ENOENT)
		gotchildmax = 0;
	else if (error != 0)
		goto done_free;
	else
		gotchildmax = 1;

	error = vfs_copyopt(opts, "enforce_statfs", &enforce, sizeof(enforce));
	if (error == ENOENT)
		gotenforce = 0;
	else if (error != 0)
		goto done_free;
	else if (enforce < 0 || enforce > 2) {
		error = EINVAL;
		goto done_free;
	} else
		gotenforce = 1;

	error = vfs_copyopt(opts, "devfs_ruleset", &rsnum, sizeof(rsnum));
	if (error == ENOENT)
		gotrsnum = 0;
	else if (error != 0)
		goto done_free;
	else
		gotrsnum = 1;

	pr_flags = ch_flags = 0;
	for (bf = pr_flag_bool;
	     bf < pr_flag_bool + nitems(pr_flag_bool);
	     bf++) {
		vfs_flagopt(opts, bf->name, &pr_flags, bf->flag);
		vfs_flagopt(opts, bf->noname, &ch_flags, bf->flag);
	}
	ch_flags |= pr_flags;
	for (jsf = pr_flag_jailsys;
	     jsf < pr_flag_jailsys + nitems(pr_flag_jailsys);
	     jsf++) {
		error = vfs_copyopt(opts, jsf->name, &jsys, sizeof(jsys));
		if (error == ENOENT)
			continue;
		if (error != 0)
			goto done_free;
		switch (jsys) {
		case JAIL_SYS_DISABLE:
			if (!jsf->disable) {
				error = EINVAL;
				goto done_free;
			}
			pr_flags |= jsf->disable;
			break;
		case JAIL_SYS_NEW:
			pr_flags |= jsf->new;
			break;
		case JAIL_SYS_INHERIT:
			break;
		default:
			error = EINVAL;
			goto done_free;
		}
		ch_flags |= jsf->new | jsf->disable;
	}
	if ((flags & (JAIL_CREATE | JAIL_UPDATE | JAIL_ATTACH)) == JAIL_CREATE
	    && !(pr_flags & PR_PERSIST)) {
		error = EINVAL;
		vfs_opterror(opts, "new jail must persist or attach");
		goto done_errmsg;
	}
#ifdef VIMAGE
	if ((flags & JAIL_UPDATE) && (ch_flags & PR_VNET)) {
		error = EINVAL;
		vfs_opterror(opts, "vnet cannot be changed after creation");
		goto done_errmsg;
	}
#endif
#ifdef INET
	if ((flags & JAIL_UPDATE) && (ch_flags & PR_IP4_USER)) {
		error = EINVAL;
		vfs_opterror(opts, "ip4 cannot be changed after creation");
		goto done_errmsg;
	}
#endif
#ifdef INET6
	if ((flags & JAIL_UPDATE) && (ch_flags & PR_IP6_USER)) {
		error = EINVAL;
		vfs_opterror(opts, "ip6 cannot be changed after creation");
		goto done_errmsg;
	}
#endif

	pr_allow = ch_allow = 0;
	for (bf = pr_flag_allow;
	     bf < pr_flag_allow + nitems(pr_flag_allow) && bf->flag != 0;
	     bf++) {
		vfs_flagopt(opts, bf->name, &pr_allow, bf->flag);
		vfs_flagopt(opts, bf->noname, &ch_allow, bf->flag);
	}
	ch_allow |= pr_allow;

	error = vfs_getopt(opts, "name", (void **)&name, &len);
	if (error == ENOENT)
		name = NULL;
	else if (error != 0)
		goto done_free;
	else {
		if (len == 0 || name[len - 1] != '\0') {
			error = EINVAL;
			goto done_free;
		}
		if (len > MAXHOSTNAMELEN) {
			error = ENAMETOOLONG;
			goto done_free;
		}
	}

	error = vfs_getopt(opts, "host.hostname", (void **)&host, &len);
	if (error == ENOENT)
		host = NULL;
	else if (error != 0)
		goto done_free;
	else {
		ch_flags |= PR_HOST;
		pr_flags |= PR_HOST;
		if (len == 0 || host[len - 1] != '\0') {
			error = EINVAL;
			goto done_free;
		}
		if (len > MAXHOSTNAMELEN) {
			error = ENAMETOOLONG;
			goto done_free;
		}
	}

	error = vfs_getopt(opts, "host.domainname", (void **)&domain, &len);
	if (error == ENOENT)
		domain = NULL;
	else if (error != 0)
		goto done_free;
	else {
		ch_flags |= PR_HOST;
		pr_flags |= PR_HOST;
		if (len == 0 || domain[len - 1] != '\0') {
			error = EINVAL;
			goto done_free;
		}
		if (len > MAXHOSTNAMELEN) {
			error = ENAMETOOLONG;
			goto done_free;
		}
	}

	error = vfs_getopt(opts, "host.hostuuid", (void **)&uuid, &len);
	if (error == ENOENT)
		uuid = NULL;
	else if (error != 0)
		goto done_free;
	else {
		ch_flags |= PR_HOST;
		pr_flags |= PR_HOST;
		if (len == 0 || uuid[len - 1] != '\0') {
			error = EINVAL;
			goto done_free;
		}
		if (len > HOSTUUIDLEN) {
			error = ENAMETOOLONG;
			goto done_free;
		}
	}

#ifdef COMPAT_FREEBSD32
	if (SV_PROC_FLAG(td->td_proc, SV_ILP32)) {
		uint32_t hid32;

		error = vfs_copyopt(opts, "host.hostid", &hid32, sizeof(hid32));
		hid = hid32;
	} else
#endif
		error = vfs_copyopt(opts, "host.hostid", &hid, sizeof(hid));
	if (error == ENOENT)
		gothid = 0;
	else if (error != 0)
		goto done_free;
	else {
		gothid = 1;
		ch_flags |= PR_HOST;
		pr_flags |= PR_HOST;
	}

#ifdef INET
	error = vfs_getopt(opts, "ip4.addr", &op, &ip4s);
	if (error == ENOENT)
		ip4s = 0;
	else if (error != 0)
		goto done_free;
	else if (ip4s & (sizeof(*ip4) - 1)) {
		error = EINVAL;
		goto done_free;
	} else {
		ch_flags |= PR_IP4_USER;
		pr_flags |= PR_IP4_USER;
		if (ip4s > 0) {
			ip4s /= sizeof(*ip4);
			if (ip4s > jail_max_af_ips) {
				error = EINVAL;
				vfs_opterror(opts, "too many IPv4 addresses");
				goto done_errmsg;
			}
			ip4 = malloc(ip4s * sizeof(*ip4), M_PRISON, M_WAITOK);
			bcopy(op, ip4, ip4s * sizeof(*ip4));
			/*
			 * IP addresses are all sorted but ip[0] to preserve
			 * the primary IP address as given from userland.
			 * This special IP is used for unbound outgoing
			 * connections as well for "loopback" traffic in case
			 * source address selection cannot find any more fitting
			 * address to connect from.
			 */
			if (ip4s > 1)
				qsort(ip4 + 1, ip4s - 1, sizeof(*ip4),
				    prison_qcmp_v4);
			/*
			 * Check for duplicate addresses and do some simple
			 * zero and broadcast checks. If users give other bogus
			 * addresses it is their problem.
			 *
			 * We do not have to care about byte order for these
			 * checks so we will do them in NBO.
			 */
			for (ii = 0; ii < ip4s; ii++) {
				if (ip4[ii].s_addr == INADDR_ANY ||
				    ip4[ii].s_addr == INADDR_BROADCAST) {
					error = EINVAL;
					goto done_free;
				}
				if ((ii+1) < ip4s &&
				    (ip4[0].s_addr == ip4[ii+1].s_addr ||
				     ip4[ii].s_addr == ip4[ii+1].s_addr)) {
					error = EINVAL;
					goto done_free;
				}
			}
		}
	}
#endif

#ifdef INET6
	error = vfs_getopt(opts, "ip6.addr", &op, &ip6s);
	if (error == ENOENT)
		ip6s = 0;
	else if (error != 0)
		goto done_free;
	else if (ip6s & (sizeof(*ip6) - 1)) {
		error = EINVAL;
		goto done_free;
	} else {
		ch_flags |= PR_IP6_USER;
		pr_flags |= PR_IP6_USER;
		if (ip6s > 0) {
			ip6s /= sizeof(*ip6);
			if (ip6s > jail_max_af_ips) {
				error = EINVAL;
				vfs_opterror(opts, "too many IPv6 addresses");
				goto done_errmsg;
			}
			ip6 = malloc(ip6s * sizeof(*ip6), M_PRISON, M_WAITOK);
			bcopy(op, ip6, ip6s * sizeof(*ip6));
			if (ip6s > 1)
				qsort(ip6 + 1, ip6s - 1, sizeof(*ip6),
				    prison_qcmp_v6);
			for (ii = 0; ii < ip6s; ii++) {
				if (IN6_IS_ADDR_UNSPECIFIED(&ip6[ii])) {
					error = EINVAL;
					goto done_free;
				}
				if ((ii+1) < ip6s &&
				    (IN6_ARE_ADDR_EQUAL(&ip6[0], &ip6[ii+1]) ||
				     IN6_ARE_ADDR_EQUAL(&ip6[ii], &ip6[ii+1])))
				{
					error = EINVAL;
					goto done_free;
				}
			}
		}
	}
#endif

#if defined(VIMAGE) && (defined(INET) || defined(INET6))
	if ((ch_flags & PR_VNET) && (ch_flags & (PR_IP4_USER | PR_IP6_USER))) {
		error = EINVAL;
		vfs_opterror(opts,
		    "vnet jails cannot have IP address restrictions");
		goto done_errmsg;
	}
#endif

	error = vfs_getopt(opts, "osrelease", (void **)&osrelstr, &len);
	if (error == ENOENT)
		osrelstr = NULL;
	else if (error != 0)
		goto done_free;
	else {
		if (flags & JAIL_UPDATE) {
			error = EINVAL;
			vfs_opterror(opts,
			    "osrelease cannot be changed after creation");
			goto done_errmsg;
		}
		if (len == 0 || len >= OSRELEASELEN) {
			error = EINVAL;
			vfs_opterror(opts,
			    "osrelease string must be 1-%d bytes long",
			    OSRELEASELEN - 1);
			goto done_errmsg;
		}
	}

	error = vfs_copyopt(opts, "osreldate", &osreldt, sizeof(osreldt));
	if (error == ENOENT)
		osreldt = 0;
	else if (error != 0)
		goto done_free;
	else {
		if (flags & JAIL_UPDATE) {
			error = EINVAL;
			vfs_opterror(opts,
			    "osreldate cannot be changed after creation");
			goto done_errmsg;
		}
		if (osreldt == 0) {
			error = EINVAL;
			vfs_opterror(opts, "osreldate cannot be 0");
			goto done_errmsg;
		}
	}

	fullpath_disabled = 0;
	root = NULL;
	error = vfs_getopt(opts, "path", (void **)&path, &len);
	if (error == ENOENT)
		path = NULL;
	else if (error != 0)
		goto done_free;
	else {
		if (flags & JAIL_UPDATE) {
			error = EINVAL;
			vfs_opterror(opts,
			    "path cannot be changed after creation");
			goto done_errmsg;
		}
		if (len == 0 || path[len - 1] != '\0') {
			error = EINVAL;
			goto done_free;
		}
		NDINIT(&nd, LOOKUP, FOLLOW | LOCKLEAF, UIO_SYSSPACE,
		    path, td);
		error = namei(&nd);
		if (error)
			goto done_free;
		root = nd.ni_vp;
		NDFREE(&nd, NDF_ONLY_PNBUF);
		g_path = malloc(MAXPATHLEN, M_TEMP, M_WAITOK);
		strlcpy(g_path, path, MAXPATHLEN);
		error = vn_path_to_global_path(td, root, g_path, MAXPATHLEN);
		if (error == 0)
			path = g_path;
		else if (error == ENODEV) {
			/* proceed if sysctl debug.disablefullpath == 1 */
			fullpath_disabled = 1;
			if (len < 2 || (len == 2 && path[0] == '/'))
				path = NULL;
		} else {
			/* exit on other errors */
			goto done_free;
		}
		if (root->v_type != VDIR) {
			error = ENOTDIR;
			vput(root);
			goto done_free;
		}
		VOP_UNLOCK(root, 0);
		if (fullpath_disabled) {
			/* Leave room for a real-root full pathname. */
			if (len + (path[0] == '/' && strcmp(mypr->pr_path, "/")
			    ? strlen(mypr->pr_path) : 0) > MAXPATHLEN) {
				error = ENAMETOOLONG;
				vrele(root);
				goto done_free;
			}
		}
	}

	/*
	 * Find the specified jail, or at least its parent.
	 * This abuses the file error codes ENOENT and EEXIST.
	 */
	pr = NULL;
	ppr = mypr;
	if (cuflags == JAIL_CREATE && jid == 0 && name != NULL) {
		namelc = strrchr(name, '.');
		jid = strtoul(namelc != NULL ? namelc + 1 : name, &p, 10);
		if (*p != '\0')
			jid = 0;
	}
	sx_xlock(&allprison_lock);
	if (jid != 0) {
		/*
		 * See if a requested jid already exists.  There is an
		 * information leak here if the jid exists but is not within
		 * the caller's jail hierarchy.  Jail creators will get EEXIST
		 * even though they cannot see the jail, and CREATE | UPDATE
		 * will return ENOENT which is not normally a valid error.
		 */
		if (jid < 0) {
			error = EINVAL;
			vfs_opterror(opts, "negative jid");
			goto done_unlock_list;
		}
		pr = prison_find(jid);
		if (pr != NULL) {
			ppr = pr->pr_parent;
			/* Create: jid must not exist. */
			if (cuflags == JAIL_CREATE) {
				mtx_unlock(&pr->pr_mtx);
				error = EEXIST;
				vfs_opterror(opts, "jail %d already exists",
				    jid);
				goto done_unlock_list;
			}
			if (!prison_ischild(mypr, pr)) {
				mtx_unlock(&pr->pr_mtx);
				pr = NULL;
			} else if (pr->pr_uref == 0) {
				if (!(flags & JAIL_DYING)) {
					mtx_unlock(&pr->pr_mtx);
					error = ENOENT;
					vfs_opterror(opts, "jail %d is dying",
					    jid);
					goto done_unlock_list;
				} else if ((flags & JAIL_ATTACH) ||
				    (pr_flags & PR_PERSIST)) {
					/*
					 * A dying jail might be resurrected
					 * (via attach or persist), but first
					 * it must determine if another jail
					 * has claimed its name.  Accomplish
					 * this by implicitly re-setting the
					 * name.
					 */
					if (name == NULL)
						name = prison_name(mypr, pr);
				}
			}
		}
		if (pr == NULL) {
			/* Update: jid must exist. */
			if (cuflags == JAIL_UPDATE) {
				error = ENOENT;
				vfs_opterror(opts, "jail %d not found", jid);
				goto done_unlock_list;
			}
		}
	}
	/*
	 * If the caller provided a name, look for a jail by that name.
	 * This has different semantics for creates and updates keyed by jid
	 * (where the name must not already exist in a different jail),
	 * and updates keyed by the name itself (where the name must exist
	 * because that is the jail being updated).
	 */
	namelc = NULL;
	if (name != NULL) {
		namelc = strrchr(name, '.');
		if (namelc == NULL)
			namelc = name;
		else {
			/*
			 * This is a hierarchical name.  Split it into the
			 * parent and child names, and make sure the parent
			 * exists or matches an already found jail.
			 */
			if (pr != NULL) {
				if (strncmp(name, ppr->pr_name, namelc - name)
				    || ppr->pr_name[namelc - name] != '\0') {
					mtx_unlock(&pr->pr_mtx);
					error = EINVAL;
					vfs_opterror(opts,
					    "cannot change jail's parent");
					goto done_unlock_list;
				}
			} else {
				*namelc = '\0';
				ppr = prison_find_name(mypr, name);
				if (ppr == NULL) {
					error = ENOENT;
					vfs_opterror(opts,
					    "jail \"%s\" not found", name);
					goto done_unlock_list;
				}
				mtx_unlock(&ppr->pr_mtx);
				*namelc = '.';
			}
			namelc++;
		}
		if (namelc[0] != '\0') {
			pnamelen =
			    (ppr == &prison0) ? 0 : strlen(ppr->pr_name) + 1;
 name_again:
			deadpr = NULL;
			FOREACH_PRISON_CHILD(ppr, tpr) {
				if (tpr != pr && tpr->pr_ref > 0 &&
				    !strcmp(tpr->pr_name + pnamelen, namelc)) {
					if (pr == NULL &&
					    cuflags != JAIL_CREATE) {
						mtx_lock(&tpr->pr_mtx);
						if (tpr->pr_ref > 0) {
							/*
							 * Use this jail
							 * for updates.
							 */
							if (tpr->pr_uref > 0) {
								pr = tpr;
								break;
							}
							deadpr = tpr;
						}
						mtx_unlock(&tpr->pr_mtx);
					} else if (tpr->pr_uref > 0) {
						/*
						 * Create, or update(jid):
						 * name must not exist in an
						 * active sibling jail.
						 */
						error = EEXIST;
						if (pr != NULL)
							mtx_unlock(&pr->pr_mtx);
						vfs_opterror(opts,
						   "jail \"%s\" already exists",
						   name);
						goto done_unlock_list;
					}
				}
			}
			/* If no active jail is found, use a dying one. */
			if (deadpr != NULL && pr == NULL) {
				if (flags & JAIL_DYING) {
					mtx_lock(&deadpr->pr_mtx);
					if (deadpr->pr_ref == 0) {
						mtx_unlock(&deadpr->pr_mtx);
						goto name_again;
					}
					pr = deadpr;
				} else if (cuflags == JAIL_UPDATE) {
					error = ENOENT;
					vfs_opterror(opts,
					    "jail \"%s\" is dying", name);
					goto done_unlock_list;
				}
			}
			/* Update: name must exist if no jid. */
			else if (cuflags == JAIL_UPDATE && pr == NULL) {
				error = ENOENT;
				vfs_opterror(opts, "jail \"%s\" not found",
				    name);
				goto done_unlock_list;
			}
		}
	}
	/* Update: must provide a jid or name. */
	else if (cuflags == JAIL_UPDATE && pr == NULL) {
		error = ENOENT;
		vfs_opterror(opts, "update specified no jail");
		goto done_unlock_list;
	}

	/* If there's no prison to update, create a new one and link it in. */
	if (pr == NULL) {
		for (tpr = mypr; tpr != NULL; tpr = tpr->pr_parent)
			if (tpr->pr_childcount >= tpr->pr_childmax) {
				error = EPERM;
				vfs_opterror(opts, "prison limit exceeded");
				goto done_unlock_list;
			}
		created = 1;
		mtx_lock(&ppr->pr_mtx);
		if (ppr->pr_ref == 0) {
			mtx_unlock(&ppr->pr_mtx);
			error = ENOENT;
			vfs_opterror(opts, "jail \"%s\" not found",
			    prison_name(mypr, ppr));
			goto done_unlock_list;
		}
		ppr->pr_ref++;
		ppr->pr_uref++;
		mtx_unlock(&ppr->pr_mtx);
		pr = malloc(sizeof(*pr), M_PRISON, M_WAITOK | M_ZERO);
		if (jid == 0) {
			/* Find the next free jid. */
			jid = lastprid + 1;
 findnext:
			if (jid == JAIL_MAX)
				jid = 1;
			TAILQ_FOREACH(tpr, &allprison, pr_list) {
				if (tpr->pr_id < jid)
					continue;
				if (tpr->pr_id > jid || tpr->pr_ref == 0) {
					TAILQ_INSERT_BEFORE(tpr, pr, pr_list);
					break;
				}
				if (jid == lastprid) {
					error = EAGAIN;
					vfs_opterror(opts,
					    "no available jail IDs");
					free(pr, M_PRISON);
					prison_deref(ppr, PD_DEREF |
					    PD_DEUREF | PD_LIST_XLOCKED);
					goto done_releroot;
				}
				jid++;
				goto findnext;
			}
			lastprid = jid;
		} else {
			/*
			 * The jail already has a jid (that did not yet exist),
			 * so just find where to insert it.
			 */
			TAILQ_FOREACH(tpr, &allprison, pr_list)
				if (tpr->pr_id >= jid) {
					TAILQ_INSERT_BEFORE(tpr, pr, pr_list);
					break;
				}
		}
		if (tpr == NULL)
			TAILQ_INSERT_TAIL(&allprison, pr, pr_list);
		LIST_INSERT_HEAD(&ppr->pr_children, pr, pr_sibling);
		for (tpr = ppr; tpr != NULL; tpr = tpr->pr_parent)
			tpr->pr_childcount++;

		pr->pr_parent = ppr;
		pr->pr_id = jid;

		/* Set some default values, and inherit some from the parent. */
		if (namelc == NULL)
			namelc = "";
		if (path == NULL) {
			path = "/";
			root = mypr->pr_root;
			vref(root);
		}
		strlcpy(pr->pr_hostuuid, DEFAULT_HOSTUUID, HOSTUUIDLEN);
		pr->pr_flags |= PR_HOST;
#if defined(INET) || defined(INET6)
#ifdef VIMAGE
		if (!(pr_flags & PR_VNET))
#endif
		{
#ifdef INET
			if (!(ch_flags & PR_IP4_USER))
				pr->pr_flags |= PR_IP4 | PR_IP4_USER;
			else if (!(pr_flags & PR_IP4_USER)) {
				pr->pr_flags |= ppr->pr_flags & PR_IP4;
				if (ppr->pr_ip4 != NULL) {
					pr->pr_ip4s = ppr->pr_ip4s;
					pr->pr_ip4 = malloc(pr->pr_ip4s *
					    sizeof(struct in_addr), M_PRISON,
					    M_WAITOK);
					bcopy(ppr->pr_ip4, pr->pr_ip4,
					    pr->pr_ip4s * sizeof(*pr->pr_ip4));
				}
			}
#endif
#ifdef INET6
			if (!(ch_flags & PR_IP6_USER))
				pr->pr_flags |= PR_IP6 | PR_IP6_USER;
			else if (!(pr_flags & PR_IP6_USER)) {
				pr->pr_flags |= ppr->pr_flags & PR_IP6;
				if (ppr->pr_ip6 != NULL) {
					pr->pr_ip6s = ppr->pr_ip6s;
					pr->pr_ip6 = malloc(pr->pr_ip6s *
					    sizeof(struct in6_addr), M_PRISON,
					    M_WAITOK);
					bcopy(ppr->pr_ip6, pr->pr_ip6,
					    pr->pr_ip6s * sizeof(*pr->pr_ip6));
				}
			}
#endif
		}
#endif
		/* Source address selection is always on by default. */
		pr->pr_flags |= _PR_IP_SADDRSEL;

		pr->pr_securelevel = ppr->pr_securelevel;
		pr->pr_allow = JAIL_DEFAULT_ALLOW & ppr->pr_allow;
		pr->pr_enforce_statfs = jail_default_enforce_statfs;
		pr->pr_devfs_rsnum = ppr->pr_devfs_rsnum;

		pr->pr_osreldate = osreldt ? osreldt : ppr->pr_osreldate;
		if (osrelstr == NULL)
		    strcpy(pr->pr_osrelease, ppr->pr_osrelease);
		else
		    strcpy(pr->pr_osrelease, osrelstr);

		LIST_INIT(&pr->pr_children);
		mtx_init(&pr->pr_mtx, "jail mutex", NULL, MTX_DEF | MTX_DUPOK);
		TASK_INIT(&pr->pr_task, 0, prison_complete, pr);

#ifdef VIMAGE
		/* Allocate a new vnet if specified. */
		pr->pr_vnet = (pr_flags & PR_VNET)
		    ? vnet_alloc() : ppr->pr_vnet;
#endif
		/*
		 * Allocate a dedicated cpuset for each jail.
		 * Unlike other initial settings, this may return an erorr.
		 */
		error = cpuset_create_root(ppr, &pr->pr_cpuset);
		if (error) {
			prison_deref(pr, PD_LIST_XLOCKED);
			goto done_releroot;
		}

		mtx_lock(&pr->pr_mtx);
		/*
		 * New prisons do not yet have a reference, because we do not
		 * want others to see the incomplete prison once the
		 * allprison_lock is downgraded.
		 */
	} else {
		created = 0;
		/*
		 * Grab a reference for existing prisons, to ensure they
		 * continue to exist for the duration of the call.
		 */
		pr->pr_ref++;
#if defined(VIMAGE) && (defined(INET) || defined(INET6))
		if ((pr->pr_flags & PR_VNET) &&
		    (ch_flags & (PR_IP4_USER | PR_IP6_USER))) {
			error = EINVAL;
			vfs_opterror(opts,
			    "vnet jails cannot have IP address restrictions");
			goto done_deref_locked;
		}
#endif
#ifdef INET
		if (PR_IP4_USER & ch_flags & (pr_flags ^ pr->pr_flags)) {
			error = EINVAL;
			vfs_opterror(opts,
			    "ip4 cannot be changed after creation");
			goto done_deref_locked;
		}
#endif
#ifdef INET6
		if (PR_IP6_USER & ch_flags & (pr_flags ^ pr->pr_flags)) {
			error = EINVAL;
			vfs_opterror(opts,
			    "ip6 cannot be changed after creation");
			goto done_deref_locked;
		}
#endif
	}

	/* Do final error checking before setting anything. */
	if (gotslevel) {
		if (slevel < ppr->pr_securelevel) {
			error = EPERM;
			goto done_deref_locked;
		}
	}
	if (gotchildmax) {
		if (childmax >= ppr->pr_childmax) {
			error = EPERM;
			goto done_deref_locked;
		}
	}
	if (gotenforce) {
		if (enforce < ppr->pr_enforce_statfs) {
			error = EPERM;
			goto done_deref_locked;
		}
	}
	if (gotrsnum) {
		/*
		 * devfs_rsnum is a uint16_t
		 */
		if (rsnum < 0 || rsnum > 65535) {
			error = EINVAL;
			goto done_deref_locked;
		}
		/*
		 * Nested jails always inherit parent's devfs ruleset
		 */
		if (jailed(td->td_ucred)) {
			if (rsnum > 0 && rsnum != ppr->pr_devfs_rsnum) {
				error = EPERM;
				goto done_deref_locked;
			} else
				rsnum = ppr->pr_devfs_rsnum;
		}
	}
#ifdef INET
	if (ip4s > 0) {
		if (ppr->pr_flags & PR_IP4) {
			/*
			 * Make sure the new set of IP addresses is a
			 * subset of the parent's list.  Don't worry
			 * about the parent being unlocked, as any
			 * setting is done with allprison_lock held.
			 */
			for (ij = 0; ij < ppr->pr_ip4s; ij++)
				if (ip4[0].s_addr == ppr->pr_ip4[ij].s_addr)
					break;
			if (ij == ppr->pr_ip4s) {
				error = EPERM;
				goto done_deref_locked;
			}
			if (ip4s > 1) {
				for (ii = ij = 1; ii < ip4s; ii++) {
					if (ip4[ii].s_addr ==
					    ppr->pr_ip4[0].s_addr)
						continue;
					for (; ij < ppr->pr_ip4s; ij++)
						if (ip4[ii].s_addr ==
						    ppr->pr_ip4[ij].s_addr)
							break;
					if (ij == ppr->pr_ip4s)
						break;
				}
				if (ij == ppr->pr_ip4s) {
					error = EPERM;
					goto done_deref_locked;
				}
			}
		}
		/*
		 * Check for conflicting IP addresses.  We permit them
		 * if there is no more than one IP on each jail.  If
		 * there is a duplicate on a jail with more than one
		 * IP stop checking and return error.
		 */
#ifdef VIMAGE
		for (tppr = ppr; tppr != &prison0; tppr = tppr->pr_parent)
			if (tppr->pr_flags & PR_VNET)
				break;
#else
		tppr = &prison0;
#endif
		FOREACH_PRISON_DESCENDANT(tppr, tpr, descend) {
			if (tpr == pr ||
#ifdef VIMAGE
			    (tpr != tppr && (tpr->pr_flags & PR_VNET)) ||
#endif
			    tpr->pr_uref == 0) {
				descend = 0;
				continue;
			}
			if (!(tpr->pr_flags & PR_IP4_USER))
				continue;
			descend = 0;
			if (tpr->pr_ip4 == NULL ||
			    (ip4s == 1 && tpr->pr_ip4s == 1))
				continue;
			for (ii = 0; ii < ip4s; ii++) {
				if (prison_check_ip4_locked(tpr, &ip4[ii]) ==
				    0) {
					error = EADDRINUSE;
					vfs_opterror(opts,
					    "IPv4 addresses clash");
					goto done_deref_locked;
				}
			}
		}
	}
#endif
#ifdef INET6
	if (ip6s > 0) {
		if (ppr->pr_flags & PR_IP6) {
			/*
			 * Make sure the new set of IP addresses is a
			 * subset of the parent's list.
			 */
			for (ij = 0; ij < ppr->pr_ip6s; ij++)
				if (IN6_ARE_ADDR_EQUAL(&ip6[0],
				    &ppr->pr_ip6[ij]))
					break;
			if (ij == ppr->pr_ip6s) {
				error = EPERM;
				goto done_deref_locked;
			}
			if (ip6s > 1) {
				for (ii = ij = 1; ii < ip6s; ii++) {
					if (IN6_ARE_ADDR_EQUAL(&ip6[ii],
					     &ppr->pr_ip6[0]))
						continue;
					for (; ij < ppr->pr_ip6s; ij++)
						if (IN6_ARE_ADDR_EQUAL(
						    &ip6[ii], &ppr->pr_ip6[ij]))
							break;
					if (ij == ppr->pr_ip6s)
						break;
				}
				if (ij == ppr->pr_ip6s) {
					error = EPERM;
					goto done_deref_locked;
				}
			}
		}
		/* Check for conflicting IP addresses. */
#ifdef VIMAGE
		for (tppr = ppr; tppr != &prison0; tppr = tppr->pr_parent)
			if (tppr->pr_flags & PR_VNET)
				break;
#else
		tppr = &prison0;
#endif
		FOREACH_PRISON_DESCENDANT(tppr, tpr, descend) {
			if (tpr == pr ||
#ifdef VIMAGE
			    (tpr != tppr && (tpr->pr_flags & PR_VNET)) ||
#endif
			    tpr->pr_uref == 0) {
				descend = 0;
				continue;
			}
			if (!(tpr->pr_flags & PR_IP6_USER))
				continue;
			descend = 0;
			if (tpr->pr_ip6 == NULL ||
			    (ip6s == 1 && tpr->pr_ip6s == 1))
				continue;
			for (ii = 0; ii < ip6s; ii++) {
				if (prison_check_ip6_locked(tpr, &ip6[ii]) ==
				    0) {
					error = EADDRINUSE;
					vfs_opterror(opts,
					    "IPv6 addresses clash");
					goto done_deref_locked;
				}
			}
		}
	}
#endif
	onamelen = namelen = 0;
	if (namelc != NULL) {
		/* Give a default name of the jid.  Also allow the name to be
		 * explicitly the jid - but not any other number, and only in
		 * normal form (no leading zero/etc).
		 */
		if (namelc[0] == '\0')
			snprintf(namelc = numbuf, sizeof(numbuf), "%d", jid);
		else if ((strtoul(namelc, &p, 10) != jid ||
			  namelc[0] < '1' || namelc[0] > '9') && *p == '\0') {
			error = EINVAL;
			vfs_opterror(opts,
			    "name cannot be numeric (unless it is the jid)");
			goto done_deref_locked;
		}
		/*
		 * Make sure the name isn't too long for the prison or its
		 * children.
		 */
		pnamelen = (ppr == &prison0) ? 0 : strlen(ppr->pr_name) + 1;
		onamelen = strlen(pr->pr_name + pnamelen);
		namelen = strlen(namelc);
		if (pnamelen + namelen + 1 > sizeof(pr->pr_name)) {
			error = ENAMETOOLONG;
			goto done_deref_locked;
		}
		FOREACH_PRISON_DESCENDANT(pr, tpr, descend) {
			if (strlen(tpr->pr_name) + (namelen - onamelen) >=
			    sizeof(pr->pr_name)) {
				error = ENAMETOOLONG;
				goto done_deref_locked;
			}
		}
	}
	pr_allow_diff = pr_allow & ~ppr->pr_allow;
	if (pr_allow_diff & ~PR_ALLOW_DIFFERENCES) {
		error = EPERM;
		goto done_deref_locked;
	}

	/*
	 * Let modules check their parameters.  This requires unlocking and
	 * then re-locking the prison, but this is still a valid state as long
	 * as allprison_lock remains xlocked.
	 */
	mtx_unlock(&pr->pr_mtx);
	error = osd_jail_call(pr, PR_METHOD_CHECK, opts);
	if (error != 0) {
		prison_deref(pr, created
		    ? PD_LIST_XLOCKED
		    : PD_DEREF | PD_LIST_XLOCKED);
		goto done_releroot;
	}
	mtx_lock(&pr->pr_mtx);

	/* At this point, all valid parameters should have been noted. */
	TAILQ_FOREACH(opt, opts, link) {
		if (!opt->seen && strcmp(opt->name, "errmsg")) {
			error = EINVAL;
			vfs_opterror(opts, "unknown parameter: %s", opt->name);
			goto done_deref_locked;
		}
	}

	/* Set the parameters of the prison. */
#ifdef INET
	redo_ip4 = 0;
	if (pr_flags & PR_IP4_USER) {
		pr->pr_flags |= PR_IP4;
		free(pr->pr_ip4, M_PRISON);
		pr->pr_ip4s = ip4s;
		pr->pr_ip4 = ip4;
		ip4 = NULL;
		FOREACH_PRISON_DESCENDANT_LOCKED(pr, tpr, descend) {
#ifdef VIMAGE
			if (tpr->pr_flags & PR_VNET) {
				descend = 0;
				continue;
			}
#endif
			if (prison_restrict_ip4(tpr, NULL)) {
				redo_ip4 = 1;
				descend = 0;
			}
		}
	}
#endif
#ifdef INET6
	redo_ip6 = 0;
	if (pr_flags & PR_IP6_USER) {
		pr->pr_flags |= PR_IP6;
		free(pr->pr_ip6, M_PRISON);
		pr->pr_ip6s = ip6s;
		pr->pr_ip6 = ip6;
		ip6 = NULL;
		FOREACH_PRISON_DESCENDANT_LOCKED(pr, tpr, descend) {
#ifdef VIMAGE
			if (tpr->pr_flags & PR_VNET) {
				descend = 0;
				continue;
			}
#endif
			if (prison_restrict_ip6(tpr, NULL)) {
				redo_ip6 = 1;
				descend = 0;
			}
		}
	}
#endif
	if (gotslevel) {
		pr->pr_securelevel = slevel;
		/* Set all child jails to be at least this level. */
		FOREACH_PRISON_DESCENDANT_LOCKED(pr, tpr, descend)
			if (tpr->pr_securelevel < slevel)
				tpr->pr_securelevel = slevel;
	}
	if (gotchildmax) {
		pr->pr_childmax = childmax;
		/* Set all child jails to under this limit. */
		FOREACH_PRISON_DESCENDANT_LOCKED_LEVEL(pr, tpr, descend, level)
			if (tpr->pr_childmax > childmax - level)
				tpr->pr_childmax = childmax > level
				    ? childmax - level : 0;
	}
	if (gotenforce) {
		pr->pr_enforce_statfs = enforce;
		/* Pass this restriction on to the children. */
		FOREACH_PRISON_DESCENDANT_LOCKED(pr, tpr, descend)
			if (tpr->pr_enforce_statfs < enforce)
				tpr->pr_enforce_statfs = enforce;
	}
	if (gotrsnum) {
		pr->pr_devfs_rsnum = rsnum;
		/* Pass this restriction on to the children. */
		FOREACH_PRISON_DESCENDANT_LOCKED(pr, tpr, descend)
			tpr->pr_devfs_rsnum = rsnum;
	}
	if (namelc != NULL) {
		if (ppr == &prison0)
			strlcpy(pr->pr_name, namelc, sizeof(pr->pr_name));
		else
			snprintf(pr->pr_name, sizeof(pr->pr_name), "%s.%s",
			    ppr->pr_name, namelc);
		/* Change this component of child names. */
		FOREACH_PRISON_DESCENDANT_LOCKED(pr, tpr, descend) {
			bcopy(tpr->pr_name + onamelen, tpr->pr_name + namelen,
			    strlen(tpr->pr_name + onamelen) + 1);
			bcopy(pr->pr_name, tpr->pr_name, namelen);
		}
	}
	if (path != NULL) {
		/* Try to keep a real-rooted full pathname. */
		if (fullpath_disabled && path[0] == '/' &&
		    strcmp(mypr->pr_path, "/"))
			snprintf(pr->pr_path, sizeof(pr->pr_path), "%s%s",
			    mypr->pr_path, path);
		else
			strlcpy(pr->pr_path, path, sizeof(pr->pr_path));
		pr->pr_root = root;
	}
	if (PR_HOST & ch_flags & ~pr_flags) {
		if (pr->pr_flags & PR_HOST) {
			/*
			 * Copy the parent's host info.  As with pr_ip4 above,
			 * the lack of a lock on the parent is not a problem;
			 * it is always set with allprison_lock at least
			 * shared, and is held exclusively here.
			 */
			strlcpy(pr->pr_hostname, pr->pr_parent->pr_hostname,
			    sizeof(pr->pr_hostname));
			strlcpy(pr->pr_domainname, pr->pr_parent->pr_domainname,
			    sizeof(pr->pr_domainname));
			strlcpy(pr->pr_hostuuid, pr->pr_parent->pr_hostuuid,
			    sizeof(pr->pr_hostuuid));
			pr->pr_hostid = pr->pr_parent->pr_hostid;
		}
	} else if (host != NULL || domain != NULL || uuid != NULL || gothid) {
		/* Set this prison, and any descendants without PR_HOST. */
		if (host != NULL)
			strlcpy(pr->pr_hostname, host, sizeof(pr->pr_hostname));
		if (domain != NULL)
			strlcpy(pr->pr_domainname, domain, 
			    sizeof(pr->pr_domainname));
		if (uuid != NULL)
			strlcpy(pr->pr_hostuuid, uuid, sizeof(pr->pr_hostuuid));
		if (gothid)
			pr->pr_hostid = hid;
		FOREACH_PRISON_DESCENDANT_LOCKED(pr, tpr, descend) {
			if (tpr->pr_flags & PR_HOST)
				descend = 0;
			else {
				if (host != NULL)
					strlcpy(tpr->pr_hostname,
					    pr->pr_hostname,
					    sizeof(tpr->pr_hostname));
				if (domain != NULL)
					strlcpy(tpr->pr_domainname, 
					    pr->pr_domainname,
					    sizeof(tpr->pr_domainname));
				if (uuid != NULL)
					strlcpy(tpr->pr_hostuuid,
					    pr->pr_hostuuid,
					    sizeof(tpr->pr_hostuuid));
				if (gothid)
					tpr->pr_hostid = hid;
			}
		}
	}
	if ((tallow = ch_allow & ~pr_allow)) {
		/* Clear allow bits in all children. */
		FOREACH_PRISON_DESCENDANT_LOCKED(pr, tpr, descend)
			tpr->pr_allow &= ~tallow;
	}
	pr->pr_allow = (pr->pr_allow & ~ch_allow) | pr_allow;
	/*
	 * Persistent prisons get an extra reference, and prisons losing their
	 * persist flag lose that reference.  Only do this for existing prisons
	 * for now, so new ones will remain unseen until after the module
	 * handlers have completed.
	 */
	born = pr->pr_uref == 0;
	if (!created && (ch_flags & PR_PERSIST & (pr_flags ^ pr->pr_flags))) {
		if (pr_flags & PR_PERSIST) {
			pr->pr_ref++;
			pr->pr_uref++;
		} else {
			pr->pr_ref--;
			pr->pr_uref--;
		}
	}
	pr->pr_flags = (pr->pr_flags & ~ch_flags) | pr_flags;
	mtx_unlock(&pr->pr_mtx);

#ifdef RACCT
	if (racct_enable && created)
		prison_racct_attach(pr);
#endif

	/* Locks may have prevented a complete restriction of child IP
	 * addresses.  If so, allocate some more memory and try again.
	 */
#ifdef INET
	while (redo_ip4) {
		ip4s = pr->pr_ip4s;
		ip4 = malloc(ip4s * sizeof(*ip4), M_PRISON, M_WAITOK);
		mtx_lock(&pr->pr_mtx);
		redo_ip4 = 0;
		FOREACH_PRISON_DESCENDANT_LOCKED(pr, tpr, descend) {
#ifdef VIMAGE
			if (tpr->pr_flags & PR_VNET) {
				descend = 0;
				continue;
			}
#endif
			if (prison_restrict_ip4(tpr, ip4)) {
				if (ip4 != NULL)
					ip4 = NULL;
				else
					redo_ip4 = 1;
			}
		}
		mtx_unlock(&pr->pr_mtx);
	}
#endif
#ifdef INET6
	while (redo_ip6) {
		ip6s = pr->pr_ip6s;
		ip6 = malloc(ip6s * sizeof(*ip6), M_PRISON, M_WAITOK);
		mtx_lock(&pr->pr_mtx);
		redo_ip6 = 0;
		FOREACH_PRISON_DESCENDANT_LOCKED(pr, tpr, descend) {
#ifdef VIMAGE
			if (tpr->pr_flags & PR_VNET) {
				descend = 0;
				continue;
			}
#endif
			if (prison_restrict_ip6(tpr, ip6)) {
				if (ip6 != NULL)
					ip6 = NULL;
				else
					redo_ip6 = 1;
			}
		}
		mtx_unlock(&pr->pr_mtx);
	}
#endif

	/* Let the modules do their work. */
	sx_downgrade(&allprison_lock);
	if (born) {
		error = osd_jail_call(pr, PR_METHOD_CREATE, opts);
		if (error) {
			(void)osd_jail_call(pr, PR_METHOD_REMOVE, NULL);
			prison_deref(pr, created
			    ? PD_LIST_SLOCKED
			    : PD_DEREF | PD_LIST_SLOCKED);
			goto done_errmsg;
		}
	}
	error = osd_jail_call(pr, PR_METHOD_SET, opts);
	if (error) {
		if (born)
			(void)osd_jail_call(pr, PR_METHOD_REMOVE, NULL);
		prison_deref(pr, created
		    ? PD_LIST_SLOCKED
		    : PD_DEREF | PD_LIST_SLOCKED);
		goto done_errmsg;
	}

	/* Attach this process to the prison if requested. */
	if (flags & JAIL_ATTACH) {
		mtx_lock(&pr->pr_mtx);
		error = do_jail_attach(td, pr);
		if (error) {
			vfs_opterror(opts, "attach failed");
			if (!created)
				prison_deref(pr, PD_DEREF);
			goto done_errmsg;
		}
	}

#ifdef RACCT
	if (racct_enable && !created) {
		if (!(flags & JAIL_ATTACH))
			sx_sunlock(&allprison_lock);
		prison_racct_modify(pr);
		if (!(flags & JAIL_ATTACH))
			sx_slock(&allprison_lock);
	}
#endif

	td->td_retval[0] = pr->pr_id;

	/*
	 * Now that it is all there, drop the temporary reference from existing
	 * prisons.  Or add a reference to newly created persistent prisons
	 * (which was not done earlier so that the prison would not be publicly
	 * visible).
	 */
	if (!created) {
		prison_deref(pr, (flags & JAIL_ATTACH)
		    ? PD_DEREF
		    : PD_DEREF | PD_LIST_SLOCKED);
	} else {
		if (pr_flags & PR_PERSIST) {
			mtx_lock(&pr->pr_mtx);
			pr->pr_ref++;
			pr->pr_uref++;
			mtx_unlock(&pr->pr_mtx);
		}
		if (!(flags & JAIL_ATTACH))
			sx_sunlock(&allprison_lock);
	}

	goto done_free;

 done_deref_locked:
	prison_deref(pr, created
	    ? PD_LOCKED | PD_LIST_XLOCKED
	    : PD_DEREF | PD_LOCKED | PD_LIST_XLOCKED);
	goto done_releroot;
 done_unlock_list:
	sx_xunlock(&allprison_lock);
 done_releroot:
	if (root != NULL)
		vrele(root);
 done_errmsg:
	if (error) {
		if (vfs_getopt(opts, "errmsg", (void **)&errmsg,
		    &errmsg_len) == 0 && errmsg_len > 0) {
			errmsg_pos = 2 * vfs_getopt_pos(opts, "errmsg") + 1;
			if (optuio->uio_segflg == UIO_SYSSPACE)
				bcopy(errmsg,
				    optuio->uio_iov[errmsg_pos].iov_base,
				    errmsg_len);
			else
				copyout(errmsg,
				    optuio->uio_iov[errmsg_pos].iov_base,
				    errmsg_len);
		}
	}
 done_free:
#ifdef INET
	free(ip4, M_PRISON);
#endif
#ifdef INET6
	free(ip6, M_PRISON);
#endif
	if (g_path != NULL)
		free(g_path, M_TEMP);
	vfs_freeopts(opts);
	return (error);
}


/*
 * struct jail_get_args {
 *	struct iovec *iovp;
 *	unsigned int iovcnt;
 *	int flags;
 * };
 */
int
sys_jail_get(struct thread *td, struct jail_get_args *uap)
{
	struct uio *auio;
	int error;

	/* Check that we have an even number of iovecs. */
	if (uap->iovcnt & 1)
		return (EINVAL);

	error = copyinuio(uap->iovp, uap->iovcnt, &auio);
	if (error)
		return (error);
	error = kern_jail_get(td, auio, uap->flags);
	if (error == 0)
		error = copyout(auio->uio_iov, uap->iovp,
		    uap->iovcnt * sizeof (struct iovec));
	free(auio, M_IOV);
	return (error);
}

int
kern_jail_get(struct thread *td, struct uio *optuio, int flags)
{
	struct bool_flags *bf;
	struct jailsys_flags *jsf;
	struct prison *pr, *mypr;
	struct vfsopt *opt;
	struct vfsoptlist *opts;
	char *errmsg, *name;
	int error, errmsg_len, errmsg_pos, i, jid, len, locked, pos;
	unsigned f;

	if (flags & ~JAIL_GET_MASK)
		return (EINVAL);

	/* Get the parameter list. */
	error = vfs_buildopts(optuio, &opts);
	if (error)
		return (error);
	errmsg_pos = vfs_getopt_pos(opts, "errmsg");
	mypr = td->td_ucred->cr_prison;

	/*
	 * Find the prison specified by one of: lastjid, jid, name.
	 */
	sx_slock(&allprison_lock);
	error = vfs_copyopt(opts, "lastjid", &jid, sizeof(jid));
	if (error == 0) {
		TAILQ_FOREACH(pr, &allprison, pr_list) {
			if (pr->pr_id > jid && prison_ischild(mypr, pr)) {
				mtx_lock(&pr->pr_mtx);
				if (pr->pr_ref > 0 &&
				    (pr->pr_uref > 0 || (flags & JAIL_DYING)))
					break;
				mtx_unlock(&pr->pr_mtx);
			}
		}
		if (pr != NULL)
			goto found_prison;
		error = ENOENT;
		vfs_opterror(opts, "no jail after %d", jid);
		goto done_unlock_list;
	} else if (error != ENOENT)
		goto done_unlock_list;

	error = vfs_copyopt(opts, "jid", &jid, sizeof(jid));
	if (error == 0) {
		if (jid != 0) {
			pr = prison_find_child(mypr, jid);
			if (pr != NULL) {
				if (pr->pr_uref == 0 && !(flags & JAIL_DYING)) {
					mtx_unlock(&pr->pr_mtx);
					error = ENOENT;
					vfs_opterror(opts, "jail %d is dying",
					    jid);
					goto done_unlock_list;
				}
				goto found_prison;
			}
			error = ENOENT;
			vfs_opterror(opts, "jail %d not found", jid);
			goto done_unlock_list;
		}
	} else if (error != ENOENT)
		goto done_unlock_list;

	error = vfs_getopt(opts, "name", (void **)&name, &len);
	if (error == 0) {
		if (len == 0 || name[len - 1] != '\0') {
			error = EINVAL;
			goto done_unlock_list;
		}
		pr = prison_find_name(mypr, name);
		if (pr != NULL) {
			if (pr->pr_uref == 0 && !(flags & JAIL_DYING)) {
				mtx_unlock(&pr->pr_mtx);
				error = ENOENT;
				vfs_opterror(opts, "jail \"%s\" is dying",
				    name);
				goto done_unlock_list;
			}
			goto found_prison;
		}
		error = ENOENT;
		vfs_opterror(opts, "jail \"%s\" not found", name);
		goto done_unlock_list;
	} else if (error != ENOENT)
		goto done_unlock_list;

	vfs_opterror(opts, "no jail specified");
	error = ENOENT;
	goto done_unlock_list;

 found_prison:
	/* Get the parameters of the prison. */
	pr->pr_ref++;
	locked = PD_LOCKED;
	td->td_retval[0] = pr->pr_id;
	error = vfs_setopt(opts, "jid", &pr->pr_id, sizeof(pr->pr_id));
	if (error != 0 && error != ENOENT)
		goto done_deref;
	i = (pr->pr_parent == mypr) ? 0 : pr->pr_parent->pr_id;
	error = vfs_setopt(opts, "parent", &i, sizeof(i));
	if (error != 0 && error != ENOENT)
		goto done_deref;
	error = vfs_setopts(opts, "name", prison_name(mypr, pr));
	if (error != 0 && error != ENOENT)
		goto done_deref;
	error = vfs_setopt(opts, "cpuset.id", &pr->pr_cpuset->cs_id,
	    sizeof(pr->pr_cpuset->cs_id));
	if (error != 0 && error != ENOENT)
		goto done_deref;
	error = vfs_setopts(opts, "path", prison_path(mypr, pr));
	if (error != 0 && error != ENOENT)
		goto done_deref;
#ifdef INET
	error = vfs_setopt_part(opts, "ip4.addr", pr->pr_ip4,
	    pr->pr_ip4s * sizeof(*pr->pr_ip4));
	if (error != 0 && error != ENOENT)
		goto done_deref;
#endif
#ifdef INET6
	error = vfs_setopt_part(opts, "ip6.addr", pr->pr_ip6,
	    pr->pr_ip6s * sizeof(*pr->pr_ip6));
	if (error != 0 && error != ENOENT)
		goto done_deref;
#endif
	error = vfs_setopt(opts, "securelevel", &pr->pr_securelevel,
	    sizeof(pr->pr_securelevel));
	if (error != 0 && error != ENOENT)
		goto done_deref;
	error = vfs_setopt(opts, "children.cur", &pr->pr_childcount,
	    sizeof(pr->pr_childcount));
	if (error != 0 && error != ENOENT)
		goto done_deref;
	error = vfs_setopt(opts, "children.max", &pr->pr_childmax,
	    sizeof(pr->pr_childmax));
	if (error != 0 && error != ENOENT)
		goto done_deref;
	error = vfs_setopts(opts, "host.hostname", pr->pr_hostname);
	if (error != 0 && error != ENOENT)
		goto done_deref;
	error = vfs_setopts(opts, "host.domainname", pr->pr_domainname);
	if (error != 0 && error != ENOENT)
		goto done_deref;
	error = vfs_setopts(opts, "host.hostuuid", pr->pr_hostuuid);
	if (error != 0 && error != ENOENT)
		goto done_deref;
#ifdef COMPAT_FREEBSD32
	if (SV_PROC_FLAG(td->td_proc, SV_ILP32)) {
		uint32_t hid32 = pr->pr_hostid;

		error = vfs_setopt(opts, "host.hostid", &hid32, sizeof(hid32));
	} else
#endif
	error = vfs_setopt(opts, "host.hostid", &pr->pr_hostid,
	    sizeof(pr->pr_hostid));
	if (error != 0 && error != ENOENT)
		goto done_deref;
	error = vfs_setopt(opts, "enforce_statfs", &pr->pr_enforce_statfs,
	    sizeof(pr->pr_enforce_statfs));
	if (error != 0 && error != ENOENT)
		goto done_deref;
	error = vfs_setopt(opts, "devfs_ruleset", &pr->pr_devfs_rsnum,
	    sizeof(pr->pr_devfs_rsnum));
	if (error != 0 && error != ENOENT)
		goto done_deref;
	for (bf = pr_flag_bool;
	     bf < pr_flag_bool + nitems(pr_flag_bool);
	     bf++) {
		i = (pr->pr_flags & bf->flag) ? 1 : 0;
		error = vfs_setopt(opts, bf->name, &i, sizeof(i));
		if (error != 0 && error != ENOENT)
			goto done_deref;
		i = !i;
		error = vfs_setopt(opts, bf->noname, &i, sizeof(i));
		if (error != 0 && error != ENOENT)
			goto done_deref;
	}
	for (jsf = pr_flag_jailsys;
	     jsf < pr_flag_jailsys + nitems(pr_flag_jailsys);
	     jsf++) {
		f = pr->pr_flags & (jsf->disable | jsf->new);
		i = (f != 0 && f == jsf->disable) ? JAIL_SYS_DISABLE
		    : (f == jsf->new) ? JAIL_SYS_NEW
		    : JAIL_SYS_INHERIT;
		error = vfs_setopt(opts, jsf->name, &i, sizeof(i));
		if (error != 0 && error != ENOENT)
			goto done_deref;
	}
	for (bf = pr_flag_allow;
	     bf < pr_flag_allow + nitems(pr_flag_allow) && bf->flag != 0;
	     bf++) {
		i = (pr->pr_allow & bf->flag) ? 1 : 0;
		error = vfs_setopt(opts, bf->name, &i, sizeof(i));
		if (error != 0 && error != ENOENT)
			goto done_deref;
		i = !i;
		error = vfs_setopt(opts, bf->noname, &i, sizeof(i));
		if (error != 0 && error != ENOENT)
			goto done_deref;
	}
	i = (pr->pr_uref == 0);
	error = vfs_setopt(opts, "dying", &i, sizeof(i));
	if (error != 0 && error != ENOENT)
		goto done_deref;
	i = !i;
	error = vfs_setopt(opts, "nodying", &i, sizeof(i));
	if (error != 0 && error != ENOENT)
		goto done_deref;
	error = vfs_setopt(opts, "osreldate", &pr->pr_osreldate,
	    sizeof(pr->pr_osreldate));
	if (error != 0 && error != ENOENT)
		goto done_deref;
	error = vfs_setopts(opts, "osrelease", pr->pr_osrelease);
	if (error != 0 && error != ENOENT)
		goto done_deref;

	/* Get the module parameters. */
	mtx_unlock(&pr->pr_mtx);
	locked = 0;
	error = osd_jail_call(pr, PR_METHOD_GET, opts);
	if (error)
		goto done_deref;
	prison_deref(pr, PD_DEREF | PD_LIST_SLOCKED);

	/* By now, all parameters should have been noted. */
	TAILQ_FOREACH(opt, opts, link) {
		if (!opt->seen && strcmp(opt->name, "errmsg")) {
			error = EINVAL;
			vfs_opterror(opts, "unknown parameter: %s", opt->name);
			goto done_errmsg;
		}
	}

	/* Write the fetched parameters back to userspace. */
	error = 0;
	TAILQ_FOREACH(opt, opts, link) {
		if (opt->pos >= 0 && opt->pos != errmsg_pos) {
			pos = 2 * opt->pos + 1;
			optuio->uio_iov[pos].iov_len = opt->len;
			if (opt->value != NULL) {
				if (optuio->uio_segflg == UIO_SYSSPACE) {
					bcopy(opt->value,
					    optuio->uio_iov[pos].iov_base,
					    opt->len);
				} else {
					error = copyout(opt->value,
					    optuio->uio_iov[pos].iov_base,
					    opt->len);
					if (error)
						break;
				}
			}
		}
	}
	goto done_errmsg;

 done_deref:
	prison_deref(pr, locked | PD_DEREF | PD_LIST_SLOCKED);
	goto done_errmsg;

 done_unlock_list:
	sx_sunlock(&allprison_lock);
 done_errmsg:
	if (error && errmsg_pos >= 0) {
		vfs_getopt(opts, "errmsg", (void **)&errmsg, &errmsg_len);
		errmsg_pos = 2 * errmsg_pos + 1;
		if (errmsg_len > 0) {
			if (optuio->uio_segflg == UIO_SYSSPACE)
				bcopy(errmsg,
				    optuio->uio_iov[errmsg_pos].iov_base,
				    errmsg_len);
			else
				copyout(errmsg,
				    optuio->uio_iov[errmsg_pos].iov_base,
				    errmsg_len);
		}
	}
	vfs_freeopts(opts);
	return (error);
}


/*
 * struct jail_remove_args {
 *	int jid;
 * };
 */
int
sys_jail_remove(struct thread *td, struct jail_remove_args *uap)
{
	struct prison *pr, *cpr, *lpr, *tpr;
	int descend, error;

	error = priv_check(td, PRIV_JAIL_REMOVE);
	if (error)
		return (error);

	sx_xlock(&allprison_lock);
	pr = prison_find_child(td->td_ucred->cr_prison, uap->jid);
	if (pr == NULL) {
		sx_xunlock(&allprison_lock);
		return (EINVAL);
	}

	/* Remove all descendants of this prison, then remove this prison. */
	pr->pr_ref++;
	if (!LIST_EMPTY(&pr->pr_children)) {
		mtx_unlock(&pr->pr_mtx);
		lpr = NULL;
		FOREACH_PRISON_DESCENDANT(pr, cpr, descend) {
			mtx_lock(&cpr->pr_mtx);
			if (cpr->pr_ref > 0) {
				tpr = cpr;
				cpr->pr_ref++;
			} else {
				/* Already removed - do not do it again. */
				tpr = NULL;
			}
			mtx_unlock(&cpr->pr_mtx);
			if (lpr != NULL) {
				mtx_lock(&lpr->pr_mtx);
				prison_remove_one(lpr);
				sx_xlock(&allprison_lock);
			}
			lpr = tpr;
		}
		if (lpr != NULL) {
			mtx_lock(&lpr->pr_mtx);
			prison_remove_one(lpr);
			sx_xlock(&allprison_lock);
		}
		mtx_lock(&pr->pr_mtx);
	}
	prison_remove_one(pr);
	return (0);
}

static void
prison_remove_one(struct prison *pr)
{
	struct proc *p;
	int deuref;

	/* If the prison was persistent, it is not anymore. */
	deuref = 0;
	if (pr->pr_flags & PR_PERSIST) {
		pr->pr_ref--;
		deuref = PD_DEUREF;
		pr->pr_flags &= ~PR_PERSIST;
	}

	/*
	 * jail_remove added a reference.  If that's the only one, remove
	 * the prison now.
	 */
	KASSERT(pr->pr_ref > 0,
	    ("prison_remove_one removing a dead prison (jid=%d)", pr->pr_id));
	if (pr->pr_ref == 1) {
		prison_deref(pr,
		    deuref | PD_DEREF | PD_LOCKED | PD_LIST_XLOCKED);
		return;
	}

	mtx_unlock(&pr->pr_mtx);
	sx_xunlock(&allprison_lock);
	/*
	 * Kill all processes unfortunate enough to be attached to this prison.
	 */
	sx_slock(&allproc_lock);
	FOREACH_PROC_IN_SYSTEM(p) {
		PROC_LOCK(p);
		if (p->p_state != PRS_NEW && p->p_ucred &&
		    p->p_ucred->cr_prison == pr)
			kern_psignal(p, SIGKILL);
		PROC_UNLOCK(p);
	}
	sx_sunlock(&allproc_lock);
	/* Remove the temporary reference added by jail_remove. */
	prison_deref(pr, deuref | PD_DEREF);
}


/*
 * struct jail_attach_args {
 *	int jid;
 * };
 */
int
sys_jail_attach(struct thread *td, struct jail_attach_args *uap)
{
	struct prison *pr;
	int error;

	error = priv_check(td, PRIV_JAIL_ATTACH);
	if (error)
		return (error);

	/*
	 * Start with exclusive hold on allprison_lock to ensure that a possible
	 * PR_METHOD_REMOVE call isn't concurrent with jail_set or jail_remove.
	 * But then immediately downgrade it since we don't need to stop
	 * readers.
	 */
	sx_xlock(&allprison_lock);
	sx_downgrade(&allprison_lock);
	pr = prison_find_child(td->td_ucred->cr_prison, uap->jid);
	if (pr == NULL) {
		sx_sunlock(&allprison_lock);
		return (EINVAL);
	}

	/*
	 * Do not allow a process to attach to a prison that is not
	 * considered to be "alive".
	 */
	if (pr->pr_uref == 0) {
		mtx_unlock(&pr->pr_mtx);
		sx_sunlock(&allprison_lock);
		return (EINVAL);
	}

	return (do_jail_attach(td, pr));
}

static int
do_jail_attach(struct thread *td, struct prison *pr)
{
	struct proc *p;
	struct ucred *newcred, *oldcred;
	int error;

	/*
	 * XXX: Note that there is a slight race here if two threads
	 * in the same privileged process attempt to attach to two
	 * different jails at the same time.  It is important for
	 * user processes not to do this, or they might end up with
	 * a process root from one prison, but attached to the jail
	 * of another.
	 */
	pr->pr_ref++;
	pr->pr_uref++;
	mtx_unlock(&pr->pr_mtx);

	/* Let modules do whatever they need to prepare for attaching. */
	error = osd_jail_call(pr, PR_METHOD_ATTACH, td);
	if (error) {
		prison_deref(pr, PD_DEREF | PD_DEUREF | PD_LIST_SLOCKED);
		return (error);
	}
	sx_sunlock(&allprison_lock);

	/*
	 * Reparent the newly attached process to this jail.
	 */
	p = td->td_proc;
	error = cpuset_setproc_update_set(p, pr->pr_cpuset);
	if (error)
		goto e_revert_osd;

	vn_lock(pr->pr_root, LK_EXCLUSIVE | LK_RETRY);
	if ((error = change_dir(pr->pr_root, td)) != 0)
		goto e_unlock;
#ifdef MAC
	if ((error = mac_vnode_check_chroot(td->td_ucred, pr->pr_root)))
		goto e_unlock;
#endif
	VOP_UNLOCK(pr->pr_root, 0);
	if ((error = pwd_chroot(td, pr->pr_root)))
		goto e_revert_osd;

	newcred = crget();
	PROC_LOCK(p);
	oldcred = crcopysafe(p, newcred);
	newcred->cr_prison = pr;
	proc_set_cred(p, newcred);
	setsugid(p);
#ifdef RACCT
	racct_proc_ucred_changed(p, oldcred, newcred);
	crhold(newcred);
#endif
	PROC_UNLOCK(p);
#ifdef RCTL
	rctl_proc_ucred_changed(p, newcred);
	crfree(newcred);
#endif
	prison_deref(oldcred->cr_prison, PD_DEREF | PD_DEUREF);
	crfree(oldcred);
	return (0);

 e_unlock:
	VOP_UNLOCK(pr->pr_root, 0);
 e_revert_osd:
	/* Tell modules this thread is still in its old jail after all. */
	(void)osd_jail_call(td->td_ucred->cr_prison, PR_METHOD_ATTACH, td);
	prison_deref(pr, PD_DEREF | PD_DEUREF);
	return (error);
}


/*
 * Returns a locked prison instance, or NULL on failure.
 */
struct prison *
prison_find(int prid)
{
	struct prison *pr;

	sx_assert(&allprison_lock, SX_LOCKED);
	TAILQ_FOREACH(pr, &allprison, pr_list) {
		if (pr->pr_id == prid) {
			mtx_lock(&pr->pr_mtx);
			if (pr->pr_ref > 0)
				return (pr);
			mtx_unlock(&pr->pr_mtx);
		}
	}
	return (NULL);
}

/*
 * Find a prison that is a descendant of mypr.  Returns a locked prison or NULL.
 */
struct prison *
prison_find_child(struct prison *mypr, int prid)
{
	struct prison *pr;
	int descend;

	sx_assert(&allprison_lock, SX_LOCKED);
	FOREACH_PRISON_DESCENDANT(mypr, pr, descend) {
		if (pr->pr_id == prid) {
			mtx_lock(&pr->pr_mtx);
			if (pr->pr_ref > 0)
				return (pr);
			mtx_unlock(&pr->pr_mtx);
		}
	}
	return (NULL);
}

/*
 * Look for the name relative to mypr.  Returns a locked prison or NULL.
 */
struct prison *
prison_find_name(struct prison *mypr, const char *name)
{
	struct prison *pr, *deadpr;
	size_t mylen;
	int descend;

	sx_assert(&allprison_lock, SX_LOCKED);
	mylen = (mypr == &prison0) ? 0 : strlen(mypr->pr_name) + 1;
 again:
	deadpr = NULL;
	FOREACH_PRISON_DESCENDANT(mypr, pr, descend) {
		if (!strcmp(pr->pr_name + mylen, name)) {
			mtx_lock(&pr->pr_mtx);
			if (pr->pr_ref > 0) {
				if (pr->pr_uref > 0)
					return (pr);
				deadpr = pr;
			}
			mtx_unlock(&pr->pr_mtx);
		}
	}
	/* There was no valid prison - perhaps there was a dying one. */
	if (deadpr != NULL) {
		mtx_lock(&deadpr->pr_mtx);
		if (deadpr->pr_ref == 0) {
			mtx_unlock(&deadpr->pr_mtx);
			goto again;
		}
	}
	return (deadpr);
}

/*
 * See if a prison has the specific flag set.
 */
int
prison_flag(struct ucred *cred, unsigned flag)
{

	/* This is an atomic read, so no locking is necessary. */
	return (cred->cr_prison->pr_flags & flag);
}

int
prison_allow(struct ucred *cred, unsigned flag)
{

	/* This is an atomic read, so no locking is necessary. */
	return (cred->cr_prison->pr_allow & flag);
}

/*
 * Remove a prison reference.  If that was the last reference, remove the
 * prison itself - but not in this context in case there are locks held.
 */
void
prison_free_locked(struct prison *pr)
{
	int ref;

	mtx_assert(&pr->pr_mtx, MA_OWNED);
	ref = --pr->pr_ref;
	mtx_unlock(&pr->pr_mtx);
	if (ref == 0)
		taskqueue_enqueue(taskqueue_thread, &pr->pr_task);
}

void
prison_free(struct prison *pr)
{

	mtx_lock(&pr->pr_mtx);
	prison_free_locked(pr);
}

/*
 * Complete a call to either prison_free or prison_proc_free.
 */
static void
prison_complete(void *context, int pending)
{
	struct prison *pr = context;

	sx_xlock(&allprison_lock);
	mtx_lock(&pr->pr_mtx);
	prison_deref(pr, pr->pr_uref
	    ? PD_DEREF | PD_DEUREF | PD_LOCKED | PD_LIST_XLOCKED
	    : PD_LOCKED | PD_LIST_XLOCKED);
}

/*
 * Remove a prison reference (usually).  This internal version assumes no
 * mutexes are held, except perhaps the prison itself.  If there are no more
 * references, release and delist the prison.  On completion, the prison lock
 * and the allprison lock are both unlocked.
 */
static void
prison_deref(struct prison *pr, int flags)
{
	struct prison *ppr, *tpr;
	int ref, lasturef;

	if (!(flags & PD_LOCKED))
		mtx_lock(&pr->pr_mtx);
	for (;;) {
		if (flags & PD_DEUREF) {
			KASSERT(pr->pr_uref > 0,
			    ("prison_deref PD_DEUREF on a dead prison (jid=%d)",
			     pr->pr_id));
			pr->pr_uref--;
			lasturef = pr->pr_uref == 0;
			if (lasturef)
				pr->pr_ref++;
			KASSERT(prison0.pr_uref != 0, ("prison0 pr_uref=0"));
		} else
			lasturef = 0;
		if (flags & PD_DEREF) {
			KASSERT(pr->pr_ref > 0,
			    ("prison_deref PD_DEREF on a dead prison (jid=%d)",
			     pr->pr_id));
			pr->pr_ref--;
		}
		ref = pr->pr_ref;
		mtx_unlock(&pr->pr_mtx);

		/*
		 * Tell the modules if the last user reference was removed
		 * (even it sticks around in dying state).
		 */
		if (lasturef) {
			if (!(flags & (PD_LIST_SLOCKED | PD_LIST_XLOCKED))) {
				sx_xlock(&allprison_lock);
				flags |= PD_LIST_XLOCKED;
			}
			(void)osd_jail_call(pr, PR_METHOD_REMOVE, NULL);
			mtx_lock(&pr->pr_mtx);
			ref = --pr->pr_ref;
			mtx_unlock(&pr->pr_mtx);
		}

		/* If the prison still has references, nothing else to do. */
		if (ref > 0) {
			if (flags & PD_LIST_SLOCKED)
				sx_sunlock(&allprison_lock);
			else if (flags & PD_LIST_XLOCKED)
				sx_xunlock(&allprison_lock);
			return;
		}

		if (flags & PD_LIST_SLOCKED) {
			if (!sx_try_upgrade(&allprison_lock)) {
				sx_sunlock(&allprison_lock);
				sx_xlock(&allprison_lock);
			}
		} else if (!(flags & PD_LIST_XLOCKED))
			sx_xlock(&allprison_lock);

		TAILQ_REMOVE(&allprison, pr, pr_list);
		LIST_REMOVE(pr, pr_sibling);
		ppr = pr->pr_parent;
		for (tpr = ppr; tpr != NULL; tpr = tpr->pr_parent)
			tpr->pr_childcount--;
		sx_xunlock(&allprison_lock);

#ifdef VIMAGE
		if (pr->pr_vnet != ppr->pr_vnet)
			vnet_destroy(pr->pr_vnet);
#endif
		if (pr->pr_root != NULL)
			vrele(pr->pr_root);
		mtx_destroy(&pr->pr_mtx);
#ifdef INET
		free(pr->pr_ip4, M_PRISON);
#endif
#ifdef INET6
		free(pr->pr_ip6, M_PRISON);
#endif
		if (pr->pr_cpuset != NULL)
			cpuset_rel(pr->pr_cpuset);
		osd_jail_exit(pr);
#ifdef RACCT
		if (racct_enable)
			prison_racct_detach(pr);
#endif
		free(pr, M_PRISON);

		/* Removing a prison frees a reference on its parent. */
		pr = ppr;
		mtx_lock(&pr->pr_mtx);
		flags = PD_DEREF | PD_DEUREF;
	}
}

void
prison_hold_locked(struct prison *pr)
{

	mtx_assert(&pr->pr_mtx, MA_OWNED);
	KASSERT(pr->pr_ref > 0,
	    ("Trying to hold dead prison %p (jid=%d).", pr, pr->pr_id));
	pr->pr_ref++;
}

void
prison_hold(struct prison *pr)
{

	mtx_lock(&pr->pr_mtx);
	prison_hold_locked(pr);
	mtx_unlock(&pr->pr_mtx);
}

void
prison_proc_hold(struct prison *pr)
{

	mtx_lock(&pr->pr_mtx);
	KASSERT(pr->pr_uref > 0,
	    ("Cannot add a process to a non-alive prison (jid=%d)", pr->pr_id));
	pr->pr_uref++;
	mtx_unlock(&pr->pr_mtx);
}

void
prison_proc_free(struct prison *pr)
{

	mtx_lock(&pr->pr_mtx);
	KASSERT(pr->pr_uref > 0,
	    ("Trying to kill a process in a dead prison (jid=%d)", pr->pr_id));
	if (pr->pr_uref > 1)
		pr->pr_uref--;
	else {
		/*
		 * Don't remove the last user reference in this context, which
		 * is expected to be a process that is not only locked, but
		 * also half dead.
		 */
		pr->pr_ref++;
		mtx_unlock(&pr->pr_mtx);
		taskqueue_enqueue(taskqueue_thread, &pr->pr_task);
		return;
	}
	mtx_unlock(&pr->pr_mtx);
}

/*
 * Check if a jail supports the given address family.
 *
 * Returns 0 if not jailed or the address family is supported, EAFNOSUPPORT
 * if not.
 */
int
prison_check_af(struct ucred *cred, int af)
{
	struct prison *pr;
	int error;

	KASSERT(cred != NULL, ("%s: cred is NULL", __func__));

	pr = cred->cr_prison;
#ifdef VIMAGE
	/* Prisons with their own network stack are not limited. */
	if (prison_owns_vnet(cred))
		return (0);
#endif

	error = 0;
	switch (af)
	{
#ifdef INET
	case AF_INET:
		if (pr->pr_flags & PR_IP4)
		{
			mtx_lock(&pr->pr_mtx);
			if ((pr->pr_flags & PR_IP4) && pr->pr_ip4 == NULL)
				error = EAFNOSUPPORT;
			mtx_unlock(&pr->pr_mtx);
		}
		break;
#endif
#ifdef INET6
	case AF_INET6:
		if (pr->pr_flags & PR_IP6)
		{
			mtx_lock(&pr->pr_mtx);
			if ((pr->pr_flags & PR_IP6) && pr->pr_ip6 == NULL)
				error = EAFNOSUPPORT;
			mtx_unlock(&pr->pr_mtx);
		}
		break;
#endif
	case AF_LOCAL:
	case AF_ROUTE:
		break;
	default:
		if (!(pr->pr_allow & PR_ALLOW_SOCKET_AF))
			error = EAFNOSUPPORT;
	}
	return (error);
}

/*
 * Check if given address belongs to the jail referenced by cred (wrapper to
 * prison_check_ip[46]).
 *
 * Returns 0 if jail doesn't restrict the address family or if address belongs
 * to jail, EADDRNOTAVAIL if the address doesn't belong, or EAFNOSUPPORT if
 * the jail doesn't allow the address family.  IPv4 Address passed in in NBO.
 */
int
prison_if(struct ucred *cred, struct sockaddr *sa)
{
#ifdef INET
	struct sockaddr_in *sai;
#endif
#ifdef INET6
	struct sockaddr_in6 *sai6;
#endif
	int error;

	KASSERT(cred != NULL, ("%s: cred is NULL", __func__));
	KASSERT(sa != NULL, ("%s: sa is NULL", __func__));

#ifdef VIMAGE
	if (prison_owns_vnet(cred))
		return (0);
#endif

	error = 0;
	switch (sa->sa_family)
	{
#ifdef INET
	case AF_INET:
		sai = (struct sockaddr_in *)sa;
		error = prison_check_ip4(cred, &sai->sin_addr);
		break;
#endif
#ifdef INET6
	case AF_INET6:
		sai6 = (struct sockaddr_in6 *)sa;
		error = prison_check_ip6(cred, &sai6->sin6_addr);
		break;
#endif
	default:
		if (!(cred->cr_prison->pr_allow & PR_ALLOW_SOCKET_AF))
			error = EAFNOSUPPORT;
	}
	return (error);
}

/*
 * Return 0 if jails permit p1 to frob p2, otherwise ESRCH.
 */
int
prison_check(struct ucred *cred1, struct ucred *cred2)
{

	return ((cred1->cr_prison == cred2->cr_prison ||
	    prison_ischild(cred1->cr_prison, cred2->cr_prison)) ? 0 : ESRCH);
}

/*
 * Return 1 if p2 is a child of p1, otherwise 0.
 */
int
prison_ischild(struct prison *pr1, struct prison *pr2)
{

	for (pr2 = pr2->pr_parent; pr2 != NULL; pr2 = pr2->pr_parent)
		if (pr1 == pr2)
			return (1);
	return (0);
}

/*
 * Return 1 if the passed credential is in a jail, otherwise 0.
 */
int
jailed(struct ucred *cred)
{

	return (cred->cr_prison != &prison0);
}

/*
 * Return 1 if the passed credential is in a jail and that jail does not
 * have its own virtual network stack, otherwise 0.
 */
int
jailed_without_vnet(struct ucred *cred)
{

	if (!jailed(cred))
		return (0);
#ifdef VIMAGE
	if (prison_owns_vnet(cred))
		return (0);
#endif

	return (1);
}

/*
 * Return the correct hostname (domainname, et al) for the passed credential.
 */
void
getcredhostname(struct ucred *cred, char *buf, size_t size)
{
	struct prison *pr;

	/*
	 * A NULL credential can be used to shortcut to the physical
	 * system's hostname.
	 */
	pr = (cred != NULL) ? cred->cr_prison : &prison0;
	mtx_lock(&pr->pr_mtx);
	strlcpy(buf, pr->pr_hostname, size);
	mtx_unlock(&pr->pr_mtx);
}

void
getcreddomainname(struct ucred *cred, char *buf, size_t size)
{

	mtx_lock(&cred->cr_prison->pr_mtx);
	strlcpy(buf, cred->cr_prison->pr_domainname, size);
	mtx_unlock(&cred->cr_prison->pr_mtx);
}

void
getcredhostuuid(struct ucred *cred, char *buf, size_t size)
{

	mtx_lock(&cred->cr_prison->pr_mtx);
	strlcpy(buf, cred->cr_prison->pr_hostuuid, size);
	mtx_unlock(&cred->cr_prison->pr_mtx);
}

void
getcredhostid(struct ucred *cred, unsigned long *hostid)
{

	mtx_lock(&cred->cr_prison->pr_mtx);
	*hostid = cred->cr_prison->pr_hostid;
	mtx_unlock(&cred->cr_prison->pr_mtx);
}

#ifdef VIMAGE
/*
 * Determine whether the prison represented by cred owns
 * its vnet rather than having it inherited.
 *
 * Returns 1 in case the prison owns the vnet, 0 otherwise.
 */
int
prison_owns_vnet(struct ucred *cred)
{

	/*
	 * vnets cannot be added/removed after jail creation,
	 * so no need to lock here.
	 */
	return (cred->cr_prison->pr_flags & PR_VNET ? 1 : 0);
}
#endif

/*
 * Determine whether the subject represented by cred can "see"
 * status of a mount point.
 * Returns: 0 for permitted, ENOENT otherwise.
 * XXX: This function should be called cr_canseemount() and should be
 *      placed in kern_prot.c.
 */
int
prison_canseemount(struct ucred *cred, struct mount *mp)
{
	struct prison *pr;
	struct statfs *sp;
	size_t len;

	pr = cred->cr_prison;
	if (pr->pr_enforce_statfs == 0)
		return (0);
	if (pr->pr_root->v_mount == mp)
		return (0);
	if (pr->pr_enforce_statfs == 2)
		return (ENOENT);
	/*
	 * If jail's chroot directory is set to "/" we should be able to see
	 * all mount-points from inside a jail.
	 * This is ugly check, but this is the only situation when jail's
	 * directory ends with '/'.
	 */
	if (strcmp(pr->pr_path, "/") == 0)
		return (0);
	len = strlen(pr->pr_path);
	sp = &mp->mnt_stat;
	if (strncmp(pr->pr_path, sp->f_mntonname, len) != 0)
		return (ENOENT);
	/*
	 * Be sure that we don't have situation where jail's root directory
	 * is "/some/path" and mount point is "/some/pathpath".
	 */
	if (sp->f_mntonname[len] != '\0' && sp->f_mntonname[len] != '/')
		return (ENOENT);
	return (0);
}

void
prison_enforce_statfs(struct ucred *cred, struct mount *mp, struct statfs *sp)
{
	char jpath[MAXPATHLEN];
	struct prison *pr;
	size_t len;

	pr = cred->cr_prison;
	if (pr->pr_enforce_statfs == 0)
		return;
	if (prison_canseemount(cred, mp) != 0) {
		bzero(sp->f_mntonname, sizeof(sp->f_mntonname));
		strlcpy(sp->f_mntonname, "[restricted]",
		    sizeof(sp->f_mntonname));
		return;
	}
	if (pr->pr_root->v_mount == mp) {
		/*
		 * Clear current buffer data, so we are sure nothing from
		 * the valid path left there.
		 */
		bzero(sp->f_mntonname, sizeof(sp->f_mntonname));
		*sp->f_mntonname = '/';
		return;
	}
	/*
	 * If jail's chroot directory is set to "/" we should be able to see
	 * all mount-points from inside a jail.
	 */
	if (strcmp(pr->pr_path, "/") == 0)
		return;
	len = strlen(pr->pr_path);
	strlcpy(jpath, sp->f_mntonname + len, sizeof(jpath));
	/*
	 * Clear current buffer data, so we are sure nothing from
	 * the valid path left there.
	 */
	bzero(sp->f_mntonname, sizeof(sp->f_mntonname));
	if (*jpath == '\0') {
		/* Should never happen. */
		*sp->f_mntonname = '/';
	} else {
		strlcpy(sp->f_mntonname, jpath, sizeof(sp->f_mntonname));
	}
}

/*
 * Check with permission for a specific privilege is granted within jail.  We
 * have a specific list of accepted privileges; the rest are denied.
 */
int
prison_priv_check(struct ucred *cred, int priv)
{

	if (!jailed(cred))
		return (0);

#ifdef VIMAGE
	/*
	 * Privileges specific to prisons with a virtual network stack.
	 * There might be a duplicate entry here in case the privilege
	 * is only granted conditionally in the legacy jail case.
	 */
	switch (priv) {
#ifdef notyet
		/*
		 * NFS-specific privileges.
		 */
	case PRIV_NFS_DAEMON:
	case PRIV_NFS_LOCKD:
#endif
		/*
		 * Network stack privileges.
		 */
	case PRIV_NET_BRIDGE:
	case PRIV_NET_GRE:
	case PRIV_NET_BPF:
	case PRIV_NET_RAW:		/* Dup, cond. in legacy jail case. */
	case PRIV_NET_ROUTE:
	case PRIV_NET_TAP:
	case PRIV_NET_SETIFMTU:
	case PRIV_NET_SETIFFLAGS:
	case PRIV_NET_SETIFCAP:
	case PRIV_NET_SETIFDESCR:
	case PRIV_NET_SETIFNAME	:
	case PRIV_NET_SETIFMETRIC:
	case PRIV_NET_SETIFPHYS:
	case PRIV_NET_SETIFMAC:
	case PRIV_NET_SETLANPCP:
	case PRIV_NET_ADDMULTI:
	case PRIV_NET_DELMULTI:
	case PRIV_NET_HWIOCTL:
	case PRIV_NET_SETLLADDR:
	case PRIV_NET_ADDIFGROUP:
	case PRIV_NET_DELIFGROUP:
	case PRIV_NET_IFCREATE:
	case PRIV_NET_IFDESTROY:
	case PRIV_NET_ADDIFADDR:
	case PRIV_NET_DELIFADDR:
	case PRIV_NET_LAGG:
	case PRIV_NET_GIF:
	case PRIV_NET_SETIFVNET:
	case PRIV_NET_SETIFFIB:

		/*
		 * 802.11-related privileges.
		 */
	case PRIV_NET80211_GETKEY:
#ifdef notyet
	case PRIV_NET80211_MANAGE:		/* XXX-BZ discuss with sam@ */
#endif

#ifdef notyet
		/*
		 * ATM privileges.
		 */
	case PRIV_NETATM_CFG:
	case PRIV_NETATM_ADD:
	case PRIV_NETATM_DEL:
	case PRIV_NETATM_SET:

		/*
		 * Bluetooth privileges.
		 */
	case PRIV_NETBLUETOOTH_RAW:
#endif

		/*
		 * Netgraph and netgraph module privileges.
		 */
	case PRIV_NETGRAPH_CONTROL:
#ifdef notyet
	case PRIV_NETGRAPH_TTY:
#endif

		/*
		 * IPv4 and IPv6 privileges.
		 */
	case PRIV_NETINET_IPFW:
	case PRIV_NETINET_DIVERT:
	case PRIV_NETINET_PF:
	case PRIV_NETINET_DUMMYNET:
	case PRIV_NETINET_CARP:
	case PRIV_NETINET_MROUTE:
	case PRIV_NETINET_RAW:
	case PRIV_NETINET_ADDRCTRL6:
	case PRIV_NETINET_ND6:
	case PRIV_NETINET_SCOPE6:
	case PRIV_NETINET_ALIFETIME6:
	case PRIV_NETINET_IPSEC:
	case PRIV_NETINET_BINDANY:

#ifdef notyet
		/*
		 * NCP privileges.
		 */
	case PRIV_NETNCP:

		/*
		 * SMB privileges.
		 */
	case PRIV_NETSMB:
#endif

	/*
	 * No default: or deny here.
	 * In case of no permit fall through to next switch().
	 */
		if (cred->cr_prison->pr_flags & PR_VNET)
			return (0);
	}
#endif /* VIMAGE */

	switch (priv) {

		/*
		 * Allow ktrace privileges for root in jail.
		 */
	case PRIV_KTRACE:

#if 0
		/*
		 * Allow jailed processes to configure audit identity and
		 * submit audit records (login, etc).  In the future we may
		 * want to further refine the relationship between audit and
		 * jail.
		 */
	case PRIV_AUDIT_GETAUDIT:
	case PRIV_AUDIT_SETAUDIT:
	case PRIV_AUDIT_SUBMIT:
#endif

		/*
		 * Allow jailed processes to manipulate process UNIX
		 * credentials in any way they see fit.
		 */
	case PRIV_CRED_SETUID:
	case PRIV_CRED_SETEUID:
	case PRIV_CRED_SETGID:
	case PRIV_CRED_SETEGID:
	case PRIV_CRED_SETGROUPS:
	case PRIV_CRED_SETREUID:
	case PRIV_CRED_SETREGID:
	case PRIV_CRED_SETRESUID:
	case PRIV_CRED_SETRESGID:

		/*
		 * Jail implements visibility constraints already, so allow
		 * jailed root to override uid/gid-based constraints.
		 */
	case PRIV_SEEOTHERGIDS:
	case PRIV_SEEOTHERUIDS:

		/*
		 * Jail implements inter-process debugging limits already, so
		 * allow jailed root various debugging privileges.
		 */
	case PRIV_DEBUG_DIFFCRED:
	case PRIV_DEBUG_SUGID:
	case PRIV_DEBUG_UNPRIV:

		/*
		 * Allow jail to set various resource limits and login
		 * properties, and for now, exceed process resource limits.
		 */
	case PRIV_PROC_LIMIT:
	case PRIV_PROC_SETLOGIN:
	case PRIV_PROC_SETRLIMIT:

		/*
		 * System V and POSIX IPC privileges are granted in jail.
		 */
	case PRIV_IPC_READ:
	case PRIV_IPC_WRITE:
	case PRIV_IPC_ADMIN:
	case PRIV_IPC_MSGSIZE:
	case PRIV_MQ_ADMIN:

		/*
		 * Jail operations within a jail work on child jails.
		 */
	case PRIV_JAIL_ATTACH:
	case PRIV_JAIL_SET:
	case PRIV_JAIL_REMOVE:

		/*
		 * Jail implements its own inter-process limits, so allow
		 * root processes in jail to change scheduling on other
		 * processes in the same jail.  Likewise for signalling.
		 */
	case PRIV_SCHED_DIFFCRED:
	case PRIV_SCHED_CPUSET:
	case PRIV_SIGNAL_DIFFCRED:
	case PRIV_SIGNAL_SUGID:

		/*
		 * Allow jailed processes to write to sysctls marked as jail
		 * writable.
		 */
	case PRIV_SYSCTL_WRITEJAIL:

		/*
		 * Allow root in jail to manage a variety of quota
		 * properties.  These should likely be conditional on a
		 * configuration option.
		 */
	case PRIV_VFS_GETQUOTA:
	case PRIV_VFS_SETQUOTA:

		/*
		 * Since Jail relies on chroot() to implement file system
		 * protections, grant many VFS privileges to root in jail.
		 * Be careful to exclude mount-related and NFS-related
		 * privileges.
		 */
	case PRIV_VFS_READ:
	case PRIV_VFS_WRITE:
	case PRIV_VFS_ADMIN:
	case PRIV_VFS_EXEC:
	case PRIV_VFS_LOOKUP:
	case PRIV_VFS_BLOCKRESERVE:	/* XXXRW: Slightly surprising. */
	case PRIV_VFS_CHFLAGS_DEV:
	case PRIV_VFS_CHOWN:
	case PRIV_VFS_CHROOT:
	case PRIV_VFS_RETAINSUGID:
	case PRIV_VFS_FCHROOT:
	case PRIV_VFS_LINK:
	case PRIV_VFS_SETGID:
	case PRIV_VFS_STAT:
	case PRIV_VFS_STICKYFILE:

		/*
		 * As in the non-jail case, non-root users are expected to be
		 * able to read kernel/phyiscal memory (provided /dev/[k]mem
		 * exists in the jail and they have permission to access it).
		 */
	case PRIV_KMEM_READ:
		return (0);

		/*
		 * Depending on the global setting, allow privilege of
		 * setting system flags.
		 */
	case PRIV_VFS_SYSFLAGS:
		if (cred->cr_prison->pr_allow & PR_ALLOW_CHFLAGS)
			return (0);
		else
			return (EPERM);

		/*
		 * Depending on the global setting, allow privilege of
		 * mounting/unmounting file systems.
		 */
	case PRIV_VFS_MOUNT:
	case PRIV_VFS_UNMOUNT:
	case PRIV_VFS_MOUNT_NONUSER:
	case PRIV_VFS_MOUNT_OWNER:
		if (cred->cr_prison->pr_allow & PR_ALLOW_MOUNT &&
		    cred->cr_prison->pr_enforce_statfs < 2)
			return (0);
		else
			return (EPERM);

		/*
		 * Conditionnaly allow locking (unlocking) physical pages
		 * in memory.
		 */
	case PRIV_VM_MLOCK:
	case PRIV_VM_MUNLOCK:
		if (cred->cr_prison->pr_allow & PR_ALLOW_MLOCK)
			return (0);
		else
			return (EPERM);

		/*
		 * Conditionally allow jailed root to bind reserved ports.
		 */
	case PRIV_NETINET_RESERVEDPORT:
		if (cred->cr_prison->pr_allow & PR_ALLOW_RESERVED_PORTS)
			return (0);
		else
			return (EPERM);

		/*
		 * Allow jailed root to reuse in-use ports.
		 */
	case PRIV_NETINET_REUSEPORT:
		return (0);

		/*
		 * Allow jailed root to set certain IPv4/6 (option) headers.
		 */
	case PRIV_NETINET_SETHDROPTS:
		return (0);

		/*
		 * Conditionally allow creating raw sockets in jail.
		 */
	case PRIV_NETINET_RAW:
		if (cred->cr_prison->pr_allow & PR_ALLOW_RAW_SOCKETS)
			return (0);
		else
			return (EPERM);

		/*
		 * Since jail implements its own visibility limits on netstat
		 * sysctls, allow getcred.  This allows identd to work in
		 * jail.
		 */
	case PRIV_NETINET_GETCRED:
		return (0);

		/*
		 * Allow jailed root to set loginclass.
		 */
	case PRIV_PROC_SETLOGINCLASS:
		return (0);

		/*
		 * Do not allow a process inside a jail to read the kernel
		 * message buffer unless explicitly permitted.
		 */
	case PRIV_MSGBUF:
		if (cred->cr_prison->pr_allow & PR_ALLOW_READ_MSGBUF)
			return (0);
		return (EPERM);

	default:
		/*
		 * In all remaining cases, deny the privilege request.  This
		 * includes almost all network privileges, many system
		 * configuration privileges.
		 */
		return (EPERM);
	}
}

/*
 * Return the part of pr2's name that is relative to pr1, or the whole name
 * if it does not directly follow.
 */

char *
prison_name(struct prison *pr1, struct prison *pr2)
{
	char *name;

	/* Jails see themselves as "0" (if they see themselves at all). */
	if (pr1 == pr2)
		return "0";
	name = pr2->pr_name;
	if (prison_ischild(pr1, pr2)) {
		/*
		 * pr1 isn't locked (and allprison_lock may not be either)
		 * so its length can't be counted on.  But the number of dots
		 * can be counted on - and counted.
		 */
		for (; pr1 != &prison0; pr1 = pr1->pr_parent)
			name = strchr(name, '.') + 1;
	}
	return (name);
}

/*
 * Return the part of pr2's path that is relative to pr1, or the whole path
 * if it does not directly follow.
 */
static char *
prison_path(struct prison *pr1, struct prison *pr2)
{
	char *path1, *path2;
	int len1;

	path1 = pr1->pr_path;
	path2 = pr2->pr_path;
	if (!strcmp(path1, "/"))
		return (path2);
	len1 = strlen(path1);
	if (strncmp(path1, path2, len1))
		return (path2);
	if (path2[len1] == '\0')
		return "/";
	if (path2[len1] == '/')
		return (path2 + len1);
	return (path2);
}


/*
 * Jail-related sysctls.
 */
static SYSCTL_NODE(_security, OID_AUTO, jail, CTLFLAG_RW, 0,
    "Jails");

static int
sysctl_jail_list(SYSCTL_HANDLER_ARGS)
{
	struct xprison *xp;
	struct prison *pr, *cpr;
#ifdef INET
	struct in_addr *ip4 = NULL;
	int ip4s = 0;
#endif
#ifdef INET6
	struct in6_addr *ip6 = NULL;
	int ip6s = 0;
#endif
	int descend, error;

	xp = malloc(sizeof(*xp), M_TEMP, M_WAITOK);
	pr = req->td->td_ucred->cr_prison;
	error = 0;
	sx_slock(&allprison_lock);
	FOREACH_PRISON_DESCENDANT(pr, cpr, descend) {
#if defined(INET) || defined(INET6)
 again:
#endif
		mtx_lock(&cpr->pr_mtx);
#ifdef INET
		if (cpr->pr_ip4s > 0) {
			if (ip4s < cpr->pr_ip4s) {
				ip4s = cpr->pr_ip4s;
				mtx_unlock(&cpr->pr_mtx);
				ip4 = realloc(ip4, ip4s *
				    sizeof(struct in_addr), M_TEMP, M_WAITOK);
				goto again;
			}
			bcopy(cpr->pr_ip4, ip4,
			    cpr->pr_ip4s * sizeof(struct in_addr));
		}
#endif
#ifdef INET6
		if (cpr->pr_ip6s > 0) {
			if (ip6s < cpr->pr_ip6s) {
				ip6s = cpr->pr_ip6s;
				mtx_unlock(&cpr->pr_mtx);
				ip6 = realloc(ip6, ip6s *
				    sizeof(struct in6_addr), M_TEMP, M_WAITOK);
				goto again;
			}
			bcopy(cpr->pr_ip6, ip6,
			    cpr->pr_ip6s * sizeof(struct in6_addr));
		}
#endif
		if (cpr->pr_ref == 0) {
			mtx_unlock(&cpr->pr_mtx);
			continue;
		}
		bzero(xp, sizeof(*xp));
		xp->pr_version = XPRISON_VERSION;
		xp->pr_id = cpr->pr_id;
		xp->pr_state = cpr->pr_uref > 0
		    ? PRISON_STATE_ALIVE : PRISON_STATE_DYING;
		strlcpy(xp->pr_path, prison_path(pr, cpr), sizeof(xp->pr_path));
		strlcpy(xp->pr_host, cpr->pr_hostname, sizeof(xp->pr_host));
		strlcpy(xp->pr_name, prison_name(pr, cpr), sizeof(xp->pr_name));
#ifdef INET
		xp->pr_ip4s = cpr->pr_ip4s;
#endif
#ifdef INET6
		xp->pr_ip6s = cpr->pr_ip6s;
#endif
		mtx_unlock(&cpr->pr_mtx);
		error = SYSCTL_OUT(req, xp, sizeof(*xp));
		if (error)
			break;
#ifdef INET
		if (xp->pr_ip4s > 0) {
			error = SYSCTL_OUT(req, ip4,
			    xp->pr_ip4s * sizeof(struct in_addr));
			if (error)
				break;
		}
#endif
#ifdef INET6
		if (xp->pr_ip6s > 0) {
			error = SYSCTL_OUT(req, ip6,
			    xp->pr_ip6s * sizeof(struct in6_addr));
			if (error)
				break;
		}
#endif
	}
	sx_sunlock(&allprison_lock);
	free(xp, M_TEMP);
#ifdef INET
	free(ip4, M_TEMP);
#endif
#ifdef INET6
	free(ip6, M_TEMP);
#endif
	return (error);
}

SYSCTL_OID(_security_jail, OID_AUTO, list,
    CTLTYPE_STRUCT | CTLFLAG_RD | CTLFLAG_MPSAFE, NULL, 0,
    sysctl_jail_list, "S", "List of active jails");

static int
sysctl_jail_jailed(SYSCTL_HANDLER_ARGS)
{
	int error, injail;

	injail = jailed(req->td->td_ucred);
	error = SYSCTL_OUT(req, &injail, sizeof(injail));

	return (error);
}

SYSCTL_PROC(_security_jail, OID_AUTO, jailed,
    CTLTYPE_INT | CTLFLAG_RD | CTLFLAG_MPSAFE, NULL, 0,
    sysctl_jail_jailed, "I", "Process in jail?");

static int
sysctl_jail_vnet(SYSCTL_HANDLER_ARGS)
{
	int error, havevnet;
#ifdef VIMAGE
	struct ucred *cred = req->td->td_ucred;

	havevnet = jailed(cred) && prison_owns_vnet(cred);
#else
	havevnet = 0;
#endif
	error = SYSCTL_OUT(req, &havevnet, sizeof(havevnet));

	return (error);
}

SYSCTL_PROC(_security_jail, OID_AUTO, vnet,
    CTLTYPE_INT | CTLFLAG_RD | CTLFLAG_MPSAFE, NULL, 0,
    sysctl_jail_vnet, "I", "Jail owns vnet?");

#if defined(INET) || defined(INET6)
SYSCTL_UINT(_security_jail, OID_AUTO, jail_max_af_ips, CTLFLAG_RW,
    &jail_max_af_ips, 0,
    "Number of IP addresses a jail may have at most per address family (deprecated)");
#endif

/*
 * Default parameters for jail(2) compatibility.  For historical reasons,
 * the sysctl names have varying similarity to the parameter names.  Prisons
 * just see their own parameters, and can't change them.
 */
static int
sysctl_jail_default_allow(SYSCTL_HANDLER_ARGS)
{
	struct prison *pr;
	int allow, error, i;

	pr = req->td->td_ucred->cr_prison;
	allow = (pr == &prison0) ? jail_default_allow : pr->pr_allow;

	/* Get the current flag value, and convert it to a boolean. */
	i = (allow & arg2) ? 1 : 0;
	if (arg1 != NULL)
		i = !i;
	error = sysctl_handle_int(oidp, &i, 0, req);
	if (error || !req->newptr)
		return (error);
	i = i ? arg2 : 0;
	if (arg1 != NULL)
		i ^= arg2;
	/*
	 * The sysctls don't have CTLFLAGS_PRISON, so assume prison0
	 * for writing.
	 */
	mtx_lock(&prison0.pr_mtx);
	jail_default_allow = (jail_default_allow & ~arg2) | i;
	mtx_unlock(&prison0.pr_mtx);
	return (0);
}

SYSCTL_PROC(_security_jail, OID_AUTO, set_hostname_allowed,
    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE,
    NULL, PR_ALLOW_SET_HOSTNAME, sysctl_jail_default_allow, "I",
    "Processes in jail can set their hostnames (deprecated)");
SYSCTL_PROC(_security_jail, OID_AUTO, socket_unixiproute_only,
    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE,
    (void *)1, PR_ALLOW_SOCKET_AF, sysctl_jail_default_allow, "I",
    "Processes in jail are limited to creating UNIX/IP/route sockets only (deprecated)");
SYSCTL_PROC(_security_jail, OID_AUTO, sysvipc_allowed,
    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE,
    NULL, PR_ALLOW_SYSVIPC, sysctl_jail_default_allow, "I",
    "Processes in jail can use System V IPC primitives (deprecated)");
SYSCTL_PROC(_security_jail, OID_AUTO, allow_raw_sockets,
    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE,
    NULL, PR_ALLOW_RAW_SOCKETS, sysctl_jail_default_allow, "I",
    "Prison root can create raw sockets (deprecated)");
SYSCTL_PROC(_security_jail, OID_AUTO, chflags_allowed,
    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE,
    NULL, PR_ALLOW_CHFLAGS, sysctl_jail_default_allow, "I",
    "Processes in jail can alter system file flags (deprecated)");
SYSCTL_PROC(_security_jail, OID_AUTO, mount_allowed,
    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE,
    NULL, PR_ALLOW_MOUNT, sysctl_jail_default_allow, "I",
    "Processes in jail can mount/unmount jail-friendly file systems (deprecated)");

static int
sysctl_jail_default_level(SYSCTL_HANDLER_ARGS)
{
	struct prison *pr;
	int level, error;

	pr = req->td->td_ucred->cr_prison;
	level = (pr == &prison0) ? *(int *)arg1 : *(int *)((char *)pr + arg2);
	error = sysctl_handle_int(oidp, &level, 0, req);
	if (error || !req->newptr)
		return (error);
	*(int *)arg1 = level;
	return (0);
}

SYSCTL_PROC(_security_jail, OID_AUTO, enforce_statfs,
    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE,
    &jail_default_enforce_statfs, offsetof(struct prison, pr_enforce_statfs),
    sysctl_jail_default_level, "I",
    "Processes in jail cannot see all mounted file systems (deprecated)");

SYSCTL_PROC(_security_jail, OID_AUTO, devfs_ruleset,
    CTLTYPE_INT | CTLFLAG_RD | CTLFLAG_MPSAFE,
    &jail_default_devfs_rsnum, offsetof(struct prison, pr_devfs_rsnum),
    sysctl_jail_default_level, "I",
    "Ruleset for the devfs filesystem in jail (deprecated)");

/*
 * Nodes to describe jail parameters.  Maximum length of string parameters
 * is returned in the string itself, and the other parameters exist merely
 * to make themselves and their types known.
 */
SYSCTL_NODE(_security_jail, OID_AUTO, param, CTLFLAG_RW, 0,
    "Jail parameters");

int
sysctl_jail_param(SYSCTL_HANDLER_ARGS)
{
	int i;
	long l;
	size_t s;
	char numbuf[12];

	switch (oidp->oid_kind & CTLTYPE)
	{
	case CTLTYPE_LONG:
	case CTLTYPE_ULONG:
		l = 0;
#ifdef SCTL_MASK32
		if (!(req->flags & SCTL_MASK32))
#endif
			return (SYSCTL_OUT(req, &l, sizeof(l)));
	case CTLTYPE_INT:
	case CTLTYPE_UINT:
		i = 0;
		return (SYSCTL_OUT(req, &i, sizeof(i)));
	case CTLTYPE_STRING:
		snprintf(numbuf, sizeof(numbuf), "%jd", (intmax_t)arg2);
		return
		    (sysctl_handle_string(oidp, numbuf, sizeof(numbuf), req));
	case CTLTYPE_STRUCT:
		s = (size_t)arg2;
		return (SYSCTL_OUT(req, &s, sizeof(s)));
	}
	return (0);
}

/*
 * CTLFLAG_RDTUN in the following indicates jail parameters that can be set at
 * jail creation time but cannot be changed in an existing jail.
 */
SYSCTL_JAIL_PARAM(, jid, CTLTYPE_INT | CTLFLAG_RDTUN, "I", "Jail ID");
SYSCTL_JAIL_PARAM(, parent, CTLTYPE_INT | CTLFLAG_RD, "I", "Jail parent ID");
SYSCTL_JAIL_PARAM_STRING(, name, CTLFLAG_RW, MAXHOSTNAMELEN, "Jail name");
SYSCTL_JAIL_PARAM_STRING(, path, CTLFLAG_RDTUN, MAXPATHLEN, "Jail root path");
SYSCTL_JAIL_PARAM(, securelevel, CTLTYPE_INT | CTLFLAG_RW,
    "I", "Jail secure level");
SYSCTL_JAIL_PARAM(, osreldate, CTLTYPE_INT | CTLFLAG_RDTUN, "I", 
    "Jail value for kern.osreldate and uname -K");
SYSCTL_JAIL_PARAM_STRING(, osrelease, CTLFLAG_RDTUN, OSRELEASELEN, 
    "Jail value for kern.osrelease and uname -r");
SYSCTL_JAIL_PARAM(, enforce_statfs, CTLTYPE_INT | CTLFLAG_RW,
    "I", "Jail cannot see all mounted file systems");
SYSCTL_JAIL_PARAM(, devfs_ruleset, CTLTYPE_INT | CTLFLAG_RW,
    "I", "Ruleset for in-jail devfs mounts");
SYSCTL_JAIL_PARAM(, persist, CTLTYPE_INT | CTLFLAG_RW,
    "B", "Jail persistence");
#ifdef VIMAGE
SYSCTL_JAIL_PARAM(, vnet, CTLTYPE_INT | CTLFLAG_RDTUN,
    "E,jailsys", "Virtual network stack");
#endif
SYSCTL_JAIL_PARAM(, dying, CTLTYPE_INT | CTLFLAG_RD,
    "B", "Jail is in the process of shutting down");

SYSCTL_JAIL_PARAM_NODE(children, "Number of child jails");
SYSCTL_JAIL_PARAM(_children, cur, CTLTYPE_INT | CTLFLAG_RD,
    "I", "Current number of child jails");
SYSCTL_JAIL_PARAM(_children, max, CTLTYPE_INT | CTLFLAG_RW,
    "I", "Maximum number of child jails");

SYSCTL_JAIL_PARAM_SYS_NODE(host, CTLFLAG_RW, "Jail host info");
SYSCTL_JAIL_PARAM_STRING(_host, hostname, CTLFLAG_RW, MAXHOSTNAMELEN,
    "Jail hostname");
SYSCTL_JAIL_PARAM_STRING(_host, domainname, CTLFLAG_RW, MAXHOSTNAMELEN,
    "Jail NIS domainname");
SYSCTL_JAIL_PARAM_STRING(_host, hostuuid, CTLFLAG_RW, HOSTUUIDLEN,
    "Jail host UUID");
SYSCTL_JAIL_PARAM(_host, hostid, CTLTYPE_ULONG | CTLFLAG_RW,
    "LU", "Jail host ID");

SYSCTL_JAIL_PARAM_NODE(cpuset, "Jail cpuset");
SYSCTL_JAIL_PARAM(_cpuset, id, CTLTYPE_INT | CTLFLAG_RD, "I", "Jail cpuset ID");

#ifdef INET
SYSCTL_JAIL_PARAM_SYS_NODE(ip4, CTLFLAG_RDTUN,
    "Jail IPv4 address virtualization");
SYSCTL_JAIL_PARAM_STRUCT(_ip4, addr, CTLFLAG_RW, sizeof(struct in_addr),
    "S,in_addr,a", "Jail IPv4 addresses");
SYSCTL_JAIL_PARAM(_ip4, saddrsel, CTLTYPE_INT | CTLFLAG_RW,
    "B", "Do (not) use IPv4 source address selection rather than the "
    "primary jail IPv4 address.");
#endif
#ifdef INET6
SYSCTL_JAIL_PARAM_SYS_NODE(ip6, CTLFLAG_RDTUN,
    "Jail IPv6 address virtualization");
SYSCTL_JAIL_PARAM_STRUCT(_ip6, addr, CTLFLAG_RW, sizeof(struct in6_addr),
    "S,in6_addr,a", "Jail IPv6 addresses");
SYSCTL_JAIL_PARAM(_ip6, saddrsel, CTLTYPE_INT | CTLFLAG_RW,
    "B", "Do (not) use IPv6 source address selection rather than the "
    "primary jail IPv6 address.");
#endif

SYSCTL_JAIL_PARAM_NODE(allow, "Jail permission flags");
SYSCTL_JAIL_PARAM(_allow, set_hostname, CTLTYPE_INT | CTLFLAG_RW,
    "B", "Jail may set hostname");
SYSCTL_JAIL_PARAM(_allow, sysvipc, CTLTYPE_INT | CTLFLAG_RW,
    "B", "Jail may use SYSV IPC");
SYSCTL_JAIL_PARAM(_allow, raw_sockets, CTLTYPE_INT | CTLFLAG_RW,
    "B", "Jail may create raw sockets");
SYSCTL_JAIL_PARAM(_allow, chflags, CTLTYPE_INT | CTLFLAG_RW,
    "B", "Jail may alter system file flags");
SYSCTL_JAIL_PARAM(_allow, quotas, CTLTYPE_INT | CTLFLAG_RW,
    "B", "Jail may set file quotas");
SYSCTL_JAIL_PARAM(_allow, socket_af, CTLTYPE_INT | CTLFLAG_RW,
    "B", "Jail may create sockets other than just UNIX/IPv4/IPv6/route");
SYSCTL_JAIL_PARAM(_allow, mlock, CTLTYPE_INT | CTLFLAG_RW,
    "B", "Jail may lock (unlock) physical pages in memory");
SYSCTL_JAIL_PARAM(_allow, reserved_ports, CTLTYPE_INT | CTLFLAG_RW,
    "B", "Jail may bind sockets to reserved ports");
SYSCTL_JAIL_PARAM(_allow, read_msgbuf, CTLTYPE_INT | CTLFLAG_RW,
    "B", "Jail may read the kernel message buffer");
SYSCTL_JAIL_PARAM(_allow, unprivileged_proc_debug, CTLTYPE_INT | CTLFLAG_RW,
    "B", "Unprivileged processes may use process debugging facilities");

SYSCTL_JAIL_PARAM_SUBNODE(allow, mount, "Jail mount/unmount permission flags");
SYSCTL_JAIL_PARAM(_allow_mount, , CTLTYPE_INT | CTLFLAG_RW,
    "B", "Jail may mount/unmount jail-friendly file systems in general");

/*
 * Add a dynamic parameter allow.<name>, or allow.<prefix>.<name>.  Return
 * its associated bit in the pr_allow bitmask, or zero if the parameter was
 * not created.
 */
unsigned
prison_add_allow(const char *prefix, const char *name, const char *prefix_descr,
    const char *descr)
{
	struct bool_flags *bf;
	struct sysctl_oid *parent;
	char *allow_name, *allow_noname, *allowed;
#ifndef NO_SYSCTL_DESCR
	char *descr_deprecated;
#endif
	unsigned allow_flag;

	if (prefix
	    ? asprintf(&allow_name, M_PRISON, "allow.%s.%s", prefix, name)
		< 0 ||
	      asprintf(&allow_noname, M_PRISON, "allow.%s.no%s", prefix, name)
		< 0
	    : asprintf(&allow_name, M_PRISON, "allow.%s", name) < 0 ||
	      asprintf(&allow_noname, M_PRISON, "allow.no%s", name) < 0) {
		free(allow_name, M_PRISON);
		return 0;
	}

	/*
	 * See if this parameter has already beed added, i.e. a module was
	 * previously loaded/unloaded.
	 */
	mtx_lock(&prison0.pr_mtx);
	for (bf = pr_flag_allow;
	     bf < pr_flag_allow + nitems(pr_flag_allow) && bf->flag != 0;
	     bf++) {
		if (strcmp(bf->name, allow_name) == 0) {
			allow_flag = bf->flag;
			goto no_add;
		}
	}

	/*
	 * Find a free bit in prison0's pr_allow, failing if there are none
	 * (which shouldn't happen as long as we keep track of how many
	 * potential dynamic flags exist).
	 *
	 * Due to per-jail unprivileged process debugging support
	 * using pr_allow, also verify against PR_ALLOW_ALL_STATIC.
	 * prison0 may have unprivileged process debugging unset.
	 */
	for (allow_flag = 1;; allow_flag <<= 1) {
		if (allow_flag == 0)
			goto no_add;
		if (allow_flag & PR_ALLOW_ALL_STATIC)
			continue;
		if ((prison0.pr_allow & allow_flag) == 0)
			break;
	}

	/*
	 * Note the parameter in the next open slot in pr_flag_allow.
	 * Set the flag last so code that checks pr_flag_allow can do so
	 * without locking.
	 */
	for (bf = pr_flag_allow; bf->flag != 0; bf++)
		if (bf == pr_flag_allow + nitems(pr_flag_allow)) {
			/* This should never happen, but is not fatal. */
			allow_flag = 0;
			goto no_add;
		}
	prison0.pr_allow |= allow_flag;
	bf->name = allow_name;
	bf->noname = allow_noname;
	bf->flag = allow_flag;
	mtx_unlock(&prison0.pr_mtx);

	/*
	 * Create sysctls for the paramter, and the back-compat global
	 * permission.
	 */
	parent = prefix
	    ? SYSCTL_ADD_NODE(NULL,
		  SYSCTL_CHILDREN(&sysctl___security_jail_param_allow),
		  OID_AUTO, prefix, 0, 0, prefix_descr)
	    : &sysctl___security_jail_param_allow;
	(void)SYSCTL_ADD_PROC(NULL, SYSCTL_CHILDREN(parent), OID_AUTO,
	    name, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE,
	    NULL, 0, sysctl_jail_param, "B", descr);
	if ((prefix
	     ? asprintf(&allowed, M_TEMP, "%s_%s_allowed", prefix, name)
	     : asprintf(&allowed, M_TEMP, "%s_allowed", name)) >= 0) {
#ifndef NO_SYSCTL_DESCR
		(void)asprintf(&descr_deprecated, M_TEMP, "%s (deprecated)",
		    descr);
#endif
		(void)SYSCTL_ADD_PROC(NULL,
		    SYSCTL_CHILDREN(&sysctl___security_jail), OID_AUTO, allowed,
		    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE, NULL, allow_flag,
		    sysctl_jail_default_allow, "I", descr_deprecated);
#ifndef NO_SYSCTL_DESCR
		free(descr_deprecated, M_TEMP);
#endif
		free(allowed, M_TEMP);
	}
	return allow_flag;

 no_add:
	mtx_unlock(&prison0.pr_mtx);
	free(allow_name, M_PRISON);
	free(allow_noname, M_PRISON);
	return allow_flag;
}

/*
 * The VFS system will register jail-aware filesystems here.  They each get
 * a parameter allow.mount.xxxfs and a flag to check when a jailed user
 * attempts to mount.
 */
void
prison_add_vfs(struct vfsconf *vfsp)
{
#ifdef NO_SYSCTL_DESCR

	vfsp->vfc_prison_flag = prison_add_allow("mount", vfsp->vfc_name,
	    NULL, NULL);
#else
	char *descr;

	(void)asprintf(&descr, M_TEMP, "Jail may mount the %s file system",
	    vfsp->vfc_name);
	vfsp->vfc_prison_flag = prison_add_allow("mount", vfsp->vfc_name,
	    NULL, descr);
	free(descr, M_TEMP);
#endif
}

#ifdef RACCT
void
prison_racct_foreach(void (*callback)(struct racct *racct,
    void *arg2, void *arg3), void (*pre)(void), void (*post)(void),
    void *arg2, void *arg3)
{
	struct prison_racct *prr;

	ASSERT_RACCT_ENABLED();

	sx_slock(&allprison_lock);
	if (pre != NULL)
		(pre)();
	LIST_FOREACH(prr, &allprison_racct, prr_next)
		(callback)(prr->prr_racct, arg2, arg3);
	if (post != NULL)
		(post)();
	sx_sunlock(&allprison_lock);
}

static struct prison_racct *
prison_racct_find_locked(const char *name)
{
	struct prison_racct *prr;

	ASSERT_RACCT_ENABLED();
	sx_assert(&allprison_lock, SA_XLOCKED);

	if (name[0] == '\0' || strlen(name) >= MAXHOSTNAMELEN)
		return (NULL);

	LIST_FOREACH(prr, &allprison_racct, prr_next) {
		if (strcmp(name, prr->prr_name) != 0)
			continue;

		/* Found prison_racct with a matching name? */
		prison_racct_hold(prr);
		return (prr);
	}

	/* Add new prison_racct. */
	prr = malloc(sizeof(*prr), M_PRISON_RACCT, M_ZERO | M_WAITOK);
	racct_create(&prr->prr_racct);

	strcpy(prr->prr_name, name);
	refcount_init(&prr->prr_refcount, 1);
	LIST_INSERT_HEAD(&allprison_racct, prr, prr_next);

	return (prr);
}

struct prison_racct *
prison_racct_find(const char *name)
{
	struct prison_racct *prr;

	ASSERT_RACCT_ENABLED();

	sx_xlock(&allprison_lock);
	prr = prison_racct_find_locked(name);
	sx_xunlock(&allprison_lock);
	return (prr);
}

void
prison_racct_hold(struct prison_racct *prr)
{

	ASSERT_RACCT_ENABLED();

	refcount_acquire(&prr->prr_refcount);
}

static void
prison_racct_free_locked(struct prison_racct *prr)
{

	ASSERT_RACCT_ENABLED();
	sx_assert(&allprison_lock, SA_XLOCKED);

	if (refcount_release(&prr->prr_refcount)) {
		racct_destroy(&prr->prr_racct);
		LIST_REMOVE(prr, prr_next);
		free(prr, M_PRISON_RACCT);
	}
}

void
prison_racct_free(struct prison_racct *prr)
{

	ASSERT_RACCT_ENABLED();
	sx_assert(&allprison_lock, SA_UNLOCKED);

	if (refcount_release_if_not_last(&prr->prr_refcount))
		return;

	sx_xlock(&allprison_lock);
	prison_racct_free_locked(prr);
	sx_xunlock(&allprison_lock);
}

static void
prison_racct_attach(struct prison *pr)
{
	struct prison_racct *prr;

	ASSERT_RACCT_ENABLED();
	sx_assert(&allprison_lock, SA_XLOCKED);

	prr = prison_racct_find_locked(pr->pr_name);
	KASSERT(prr != NULL, ("cannot find prison_racct"));

	pr->pr_prison_racct = prr;
}

/*
 * Handle jail renaming.  From the racct point of view, renaming means
 * moving from one prison_racct to another.
 */
static void
prison_racct_modify(struct prison *pr)
{
#ifdef RCTL
	struct proc *p;
	struct ucred *cred;
#endif
	struct prison_racct *oldprr;

	ASSERT_RACCT_ENABLED();

	sx_slock(&allproc_lock);
	sx_xlock(&allprison_lock);

	if (strcmp(pr->pr_name, pr->pr_prison_racct->prr_name) == 0) {
		sx_xunlock(&allprison_lock);
		sx_sunlock(&allproc_lock);
		return;
	}

	oldprr = pr->pr_prison_racct;
	pr->pr_prison_racct = NULL;

	prison_racct_attach(pr);

	/*
	 * Move resource utilisation records.
	 */
	racct_move(pr->pr_prison_racct->prr_racct, oldprr->prr_racct);

#ifdef RCTL
	/*
	 * Force rctl to reattach rules to processes.
	 */
	FOREACH_PROC_IN_SYSTEM(p) {
		PROC_LOCK(p);
		cred = crhold(p->p_ucred);
		PROC_UNLOCK(p);
		rctl_proc_ucred_changed(p, cred);
		crfree(cred);
	}
#endif

	sx_sunlock(&allproc_lock);
	prison_racct_free_locked(oldprr);
	sx_xunlock(&allprison_lock);
}

static void
prison_racct_detach(struct prison *pr)
{

	ASSERT_RACCT_ENABLED();
	sx_assert(&allprison_lock, SA_UNLOCKED);

	if (pr->pr_prison_racct == NULL)
		return;
	prison_racct_free(pr->pr_prison_racct);
	pr->pr_prison_racct = NULL;
}
#endif /* RACCT */

#ifdef DDB

static void
db_show_prison(struct prison *pr)
{
	struct bool_flags *bf;
	struct jailsys_flags *jsf;
#if defined(INET) || defined(INET6)
	int ii;
#endif
	unsigned f;
#ifdef INET
	char ip4buf[INET_ADDRSTRLEN];
#endif
#ifdef INET6
	char ip6buf[INET6_ADDRSTRLEN];
#endif

	db_printf("prison %p:\n", pr);
	db_printf(" jid             = %d\n", pr->pr_id);
	db_printf(" name            = %s\n", pr->pr_name);
	db_printf(" parent          = %p\n", pr->pr_parent);
	db_printf(" ref             = %d\n", pr->pr_ref);
	db_printf(" uref            = %d\n", pr->pr_uref);
	db_printf(" path            = %s\n", pr->pr_path);
	db_printf(" cpuset          = %d\n", pr->pr_cpuset
	    ? pr->pr_cpuset->cs_id : -1);
#ifdef VIMAGE
	db_printf(" vnet            = %p\n", pr->pr_vnet);
#endif
	db_printf(" root            = %p\n", pr->pr_root);
	db_printf(" securelevel     = %d\n", pr->pr_securelevel);
	db_printf(" devfs_rsnum     = %d\n", pr->pr_devfs_rsnum);
	db_printf(" children.max    = %d\n", pr->pr_childmax);
	db_printf(" children.cur    = %d\n", pr->pr_childcount);
	db_printf(" child           = %p\n", LIST_FIRST(&pr->pr_children));
	db_printf(" sibling         = %p\n", LIST_NEXT(pr, pr_sibling));
	db_printf(" flags           = 0x%x", pr->pr_flags);
	for (bf = pr_flag_bool; bf < pr_flag_bool + nitems(pr_flag_bool); bf++)
		if (pr->pr_flags & bf->flag)
			db_printf(" %s", bf->name);
	for (jsf = pr_flag_jailsys;
	     jsf < pr_flag_jailsys + nitems(pr_flag_jailsys);
	     jsf++) {
		f = pr->pr_flags & (jsf->disable | jsf->new);
		db_printf(" %-16s= %s\n", jsf->name,
		    (f != 0 && f == jsf->disable) ? "disable"
		    : (f == jsf->new) ? "new"
		    : "inherit");
	}
	db_printf(" allow           = 0x%x", pr->pr_allow);
	for (bf = pr_flag_allow;
	     bf < pr_flag_allow + nitems(pr_flag_allow) && bf->flag != 0;
	     bf++)
		if (pr->pr_allow & bf->flag)
			db_printf(" %s", bf->name);
	db_printf("\n");
	db_printf(" enforce_statfs  = %d\n", pr->pr_enforce_statfs);
	db_printf(" host.hostname   = %s\n", pr->pr_hostname);
	db_printf(" host.domainname = %s\n", pr->pr_domainname);
	db_printf(" host.hostuuid   = %s\n", pr->pr_hostuuid);
	db_printf(" host.hostid     = %lu\n", pr->pr_hostid);
#ifdef INET
	db_printf(" ip4s            = %d\n", pr->pr_ip4s);
	for (ii = 0; ii < pr->pr_ip4s; ii++)
		db_printf(" %s %s\n",
		    ii == 0 ? "ip4.addr        =" : "                 ",
		    inet_ntoa_r(pr->pr_ip4[ii], ip4buf));
#endif
#ifdef INET6
	db_printf(" ip6s            = %d\n", pr->pr_ip6s);
	for (ii = 0; ii < pr->pr_ip6s; ii++)
		db_printf(" %s %s\n",
		    ii == 0 ? "ip6.addr        =" : "                 ",
		    ip6_sprintf(ip6buf, &pr->pr_ip6[ii]));
#endif
}

DB_SHOW_COMMAND(prison, db_show_prison_command)
{
	struct prison *pr;

	if (!have_addr) {
		/*
		 * Show all prisons in the list, and prison0 which is not
		 * listed.
		 */
		db_show_prison(&prison0);
		if (!db_pager_quit) {
			TAILQ_FOREACH(pr, &allprison, pr_list) {
				db_show_prison(pr);
				if (db_pager_quit)
					break;
			}
		}
		return;
	}

	if (addr == 0)
		pr = &prison0;
	else {
		/* Look for a prison with the ID and with references. */
		TAILQ_FOREACH(pr, &allprison, pr_list)
			if (pr->pr_id == addr && pr->pr_ref > 0)
				break;
		if (pr == NULL)
			/* Look again, without requiring a reference. */
			TAILQ_FOREACH(pr, &allprison, pr_list)
				if (pr->pr_id == addr)
					break;
		if (pr == NULL)
			/* Assume address points to a valid prison. */
			pr = (struct prison *)addr;
	}
	db_show_prison(pr);
}

#endif /* DDB */
