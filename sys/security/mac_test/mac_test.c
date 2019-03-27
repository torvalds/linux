/*-
 * Copyright (c) 1999-2002, 2007-2011 Robert N. M. Watson
 * Copyright (c) 2001-2005 McAfee, Inc.
 * Copyright (c) 2006 SPARTA, Inc.
 * Copyright (c) 2008 Apple Inc.
 * All rights reserved.
 *
 * This software was developed by Robert Watson for the TrustedBSD Project.
 *
 * This software was developed for the FreeBSD Project in part by McAfee
 * Research, the Security Research Division of McAfee, Inc. under
 * DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"), as part of the DARPA
 * CHATS research program.
 *
 * This software was enhanced by SPARTA ISSO under SPAWAR contract
 * N66001-04-C-6019 ("SEFOS").
 *
 * This software was developed at the University of Cambridge Computer
 * Laboratory with support from a grant from Google, Inc.
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
 * Developed by the TrustedBSD Project.
 *
 * MAC Test policy - tests MAC Framework labeling by assigning object class
 * magic numbers to each label and validates that each time an object label
 * is passed into the policy, it has a consistent object type, catching
 * incorrectly passed labels, labels passed after free, etc.
 */

#include <sys/param.h>
#include <sys/acl.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/ksem.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mount.h>
#include <sys/msg.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sx.h>
#include <sys/sysctl.h>

#include <fs/devfs/devfs.h>

#include <net/bpfdesc.h>
#include <net/if.h>
#include <net/if_types.h>
#include <net/if_var.h>

#include <security/mac/mac_policy.h>

SYSCTL_DECL(_security_mac);

static SYSCTL_NODE(_security_mac, OID_AUTO, test, CTLFLAG_RW, 0,
    "TrustedBSD mac_test policy controls");

#define	MAGIC_BPF	0xfe1ad1b6
#define	MAGIC_DEVFS	0x9ee79c32
#define	MAGIC_IFNET	0xc218b120
#define	MAGIC_INPCB	0x4440f7bb
#define	MAGIC_IP6Q	0x0870e1b7
#define	MAGIC_IPQ	0x206188ef
#define	MAGIC_MBUF	0xbbefa5bb
#define	MAGIC_MOUNT	0xc7c46e47
#define	MAGIC_SOCKET	0x9199c6cd
#define	MAGIC_SYNCACHE	0x7fb838a8
#define	MAGIC_SYSV_MSG	0x8bbba61e
#define	MAGIC_SYSV_MSQ	0xea672391
#define	MAGIC_SYSV_SEM	0x896e8a0b
#define	MAGIC_SYSV_SHM	0x76119ab0
#define	MAGIC_PIPE	0xdc6c9919
#define	MAGIC_POSIX_SEM	0x78ae980c
#define	MAGIC_POSIX_SHM	0x4e853fc9
#define	MAGIC_PROC	0x3b4be98f
#define	MAGIC_CRED	0x9a5a4987
#define	MAGIC_VNODE	0x1a67a45c
#define	MAGIC_FREE	0x849ba1fd

#define	SLOT(x)	mac_label_get((x), test_slot)
#define	SLOT_SET(x, v)	mac_label_set((x), test_slot, (v))

static int	test_slot;
SYSCTL_INT(_security_mac_test, OID_AUTO, slot, CTLFLAG_RD,
    &test_slot, 0, "Slot allocated by framework");

static SYSCTL_NODE(_security_mac_test, OID_AUTO, counter, CTLFLAG_RW, 0,
    "TrustedBSD mac_test counters controls");

#define	COUNTER_DECL(variable)						\
	static int counter_##variable;					\
	SYSCTL_INT(_security_mac_test_counter, OID_AUTO, variable,	\
	CTLFLAG_RD, &counter_##variable, 0, #variable)

#define	COUNTER_INC(variable)	atomic_add_int(&counter_##variable, 1)

#ifdef KDB
#define	DEBUGGER(func, string)	kdb_enter(KDB_WHY_MAC, (string))
#else
#define	DEBUGGER(func, string)	printf("mac_test: %s: %s\n", (func), (string))
#endif

#define	LABEL_CHECK(label, magic) do {					\
	if (label != NULL) {						\
		KASSERT(SLOT(label) == magic ||	SLOT(label) == 0,	\
		    ("%s: bad %s label", __func__, #magic));		\
	}								\
} while (0)

#define	LABEL_DESTROY(label, magic) do {				\
	if (SLOT(label) == magic || SLOT(label) == 0) {			\
		SLOT_SET(label, MAGIC_FREE);				\
	} else if (SLOT(label) == MAGIC_FREE) {				\
		DEBUGGER("%s: dup destroy", __func__);			\
	} else {							\
		DEBUGGER("%s: corrupted label", __func__);		\
	}								\
} while (0)

#define	LABEL_INIT(label, magic) do {					\
	SLOT_SET(label, magic);						\
} while (0)

#define	LABEL_NOTFREE(label) do {					\
	KASSERT(SLOT(label) != MAGIC_FREE,				\
	    ("%s: destroyed label", __func__));				\
} while (0)

/*
 * Object-specific entry point implementations are sorted alphabetically by
 * object type name and then by operation.
 */
COUNTER_DECL(bpfdesc_check_receive);
static int
test_bpfdesc_check_receive(struct bpf_d *d, struct label *dlabel,
    struct ifnet *ifp, struct label *ifplabel)
{

	LABEL_CHECK(dlabel, MAGIC_BPF);
	LABEL_CHECK(ifplabel, MAGIC_IFNET);
	COUNTER_INC(bpfdesc_check_receive);

	return (0);
}

COUNTER_DECL(bpfdesc_create);
static void
test_bpfdesc_create(struct ucred *cred, struct bpf_d *d,
    struct label *dlabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(dlabel, MAGIC_BPF);
	COUNTER_INC(bpfdesc_create);
}

COUNTER_DECL(bpfdesc_create_mbuf);
static void
test_bpfdesc_create_mbuf(struct bpf_d *d, struct label *dlabel,
    struct mbuf *m, struct label *mlabel)
{

	LABEL_CHECK(dlabel, MAGIC_BPF);
	LABEL_CHECK(mlabel, MAGIC_MBUF);
	COUNTER_INC(bpfdesc_create_mbuf);
}

COUNTER_DECL(bpfdesc_destroy_label);
static void
test_bpfdesc_destroy_label(struct label *label)
{

	LABEL_DESTROY(label, MAGIC_BPF);
	COUNTER_INC(bpfdesc_destroy_label);
}

COUNTER_DECL(bpfdesc_init_label);
static void
test_bpfdesc_init_label(struct label *label)
{

	LABEL_INIT(label, MAGIC_BPF);
	COUNTER_INC(bpfdesc_init_label);
}

COUNTER_DECL(cred_check_relabel);
static int
test_cred_check_relabel(struct ucred *cred, struct label *newlabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(newlabel, MAGIC_CRED);
	COUNTER_INC(cred_check_relabel);

	return (0);
}

COUNTER_DECL(cred_check_setaudit);
static int
test_cred_check_setaudit(struct ucred *cred, struct auditinfo *ai)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	COUNTER_INC(cred_check_setaudit);

	return (0);
}

COUNTER_DECL(cred_check_setaudit_addr);
static int
test_cred_check_setaudit_addr(struct ucred *cred,
    struct auditinfo_addr *aia)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	COUNTER_INC(cred_check_setaudit_addr);

	return (0);
}

COUNTER_DECL(cred_check_setauid);
static int
test_cred_check_setauid(struct ucred *cred, uid_t auid)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	COUNTER_INC(cred_check_setauid);

	return (0);
}

COUNTER_DECL(cred_check_setegid);
static int
test_cred_check_setegid(struct ucred *cred, gid_t egid)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	COUNTER_INC(cred_check_setegid);

	return (0);
}

COUNTER_DECL(proc_check_euid);
static int
test_cred_check_seteuid(struct ucred *cred, uid_t euid)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	COUNTER_INC(proc_check_euid);

	return (0);
}

COUNTER_DECL(cred_check_setregid);
static int
test_cred_check_setregid(struct ucred *cred, gid_t rgid, gid_t egid)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	COUNTER_INC(cred_check_setregid);

	return (0);
}

COUNTER_DECL(cred_check_setreuid);
static int
test_cred_check_setreuid(struct ucred *cred, uid_t ruid, uid_t euid)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	COUNTER_INC(cred_check_setreuid);

	return (0);
}

COUNTER_DECL(cred_check_setgid);
static int
test_cred_check_setgid(struct ucred *cred, gid_t gid)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	COUNTER_INC(cred_check_setgid);

	return (0);
}

COUNTER_DECL(cred_check_setgroups);
static int
test_cred_check_setgroups(struct ucred *cred, int ngroups,
	gid_t *gidset)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	COUNTER_INC(cred_check_setgroups);

	return (0);
}

COUNTER_DECL(cred_check_setresgid);
static int
test_cred_check_setresgid(struct ucred *cred, gid_t rgid, gid_t egid,
	gid_t sgid)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	COUNTER_INC(cred_check_setresgid);

	return (0);
}

COUNTER_DECL(cred_check_setresuid);
static int
test_cred_check_setresuid(struct ucred *cred, uid_t ruid, uid_t euid,
	uid_t suid)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	COUNTER_INC(cred_check_setresuid);

	return (0);
}

COUNTER_DECL(cred_check_setuid);
static int
test_cred_check_setuid(struct ucred *cred, uid_t uid)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	COUNTER_INC(cred_check_setuid);

	return (0);
}

COUNTER_DECL(cred_check_visible);
static int
test_cred_check_visible(struct ucred *u1, struct ucred *u2)
{

	LABEL_CHECK(u1->cr_label, MAGIC_CRED);
	LABEL_CHECK(u2->cr_label, MAGIC_CRED);
	COUNTER_INC(cred_check_visible);

	return (0);
}

COUNTER_DECL(cred_copy_label);
static void
test_cred_copy_label(struct label *src, struct label *dest)
{

	LABEL_CHECK(src, MAGIC_CRED);
	LABEL_CHECK(dest, MAGIC_CRED);
	COUNTER_INC(cred_copy_label);
}

COUNTER_DECL(cred_create_init);
static void
test_cred_create_init(struct ucred *cred)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	COUNTER_INC(cred_create_init);
}

COUNTER_DECL(cred_create_swapper);
static void
test_cred_create_swapper(struct ucred *cred)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	COUNTER_INC(cred_create_swapper);
}

COUNTER_DECL(cred_destroy_label);
static void
test_cred_destroy_label(struct label *label)
{

	LABEL_DESTROY(label, MAGIC_CRED);
	COUNTER_INC(cred_destroy_label);
}

COUNTER_DECL(cred_externalize_label);
static int
test_cred_externalize_label(struct label *label, char *element_name,
    struct sbuf *sb, int *claimed)
{

	LABEL_CHECK(label, MAGIC_CRED);
	COUNTER_INC(cred_externalize_label);

	return (0);
}

COUNTER_DECL(cred_init_label);
static void
test_cred_init_label(struct label *label)
{

	LABEL_INIT(label, MAGIC_CRED);
	COUNTER_INC(cred_init_label);
}

COUNTER_DECL(cred_internalize_label);
static int
test_cred_internalize_label(struct label *label, char *element_name,
    char *element_data, int *claimed)
{

	LABEL_CHECK(label, MAGIC_CRED);
	COUNTER_INC(cred_internalize_label);

	return (0);
}

COUNTER_DECL(cred_relabel);
static void
test_cred_relabel(struct ucred *cred, struct label *newlabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(newlabel, MAGIC_CRED);
	COUNTER_INC(cred_relabel);
}

COUNTER_DECL(devfs_create_device);
static void
test_devfs_create_device(struct ucred *cred, struct mount *mp,
    struct cdev *dev, struct devfs_dirent *de, struct label *delabel)
{

	if (cred != NULL)
		LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(delabel, MAGIC_DEVFS);
	COUNTER_INC(devfs_create_device);
}

COUNTER_DECL(devfs_create_directory);
static void
test_devfs_create_directory(struct mount *mp, char *dirname,
    int dirnamelen, struct devfs_dirent *de, struct label *delabel)
{

	LABEL_CHECK(delabel, MAGIC_DEVFS);
	COUNTER_INC(devfs_create_directory);
}

COUNTER_DECL(devfs_create_symlink);
static void
test_devfs_create_symlink(struct ucred *cred, struct mount *mp,
    struct devfs_dirent *dd, struct label *ddlabel, struct devfs_dirent *de,
    struct label *delabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(ddlabel, MAGIC_DEVFS);
	LABEL_CHECK(delabel, MAGIC_DEVFS);
	COUNTER_INC(devfs_create_symlink);
}

COUNTER_DECL(devfs_destroy_label);
static void
test_devfs_destroy_label(struct label *label)
{

	LABEL_DESTROY(label, MAGIC_DEVFS);
	COUNTER_INC(devfs_destroy_label);
}

COUNTER_DECL(devfs_init_label);
static void
test_devfs_init_label(struct label *label)
{

	LABEL_INIT(label, MAGIC_DEVFS);
	COUNTER_INC(devfs_init_label);
}

COUNTER_DECL(devfs_update);
static void
test_devfs_update(struct mount *mp, struct devfs_dirent *devfs_dirent,
    struct label *direntlabel, struct vnode *vp, struct label *vplabel)
{

	LABEL_CHECK(direntlabel, MAGIC_DEVFS);
	LABEL_CHECK(vplabel, MAGIC_VNODE);
	COUNTER_INC(devfs_update);
}

COUNTER_DECL(devfs_vnode_associate);
static void
test_devfs_vnode_associate(struct mount *mp, struct label *mplabel,
    struct devfs_dirent *de, struct label *delabel, struct vnode *vp,
    struct label *vplabel)
{

	LABEL_CHECK(mplabel, MAGIC_MOUNT);
	LABEL_CHECK(delabel, MAGIC_DEVFS);
	LABEL_CHECK(vplabel, MAGIC_VNODE);
	COUNTER_INC(devfs_vnode_associate);
}

COUNTER_DECL(ifnet_check_relabel);
static int
test_ifnet_check_relabel(struct ucred *cred, struct ifnet *ifp,
    struct label *ifplabel, struct label *newlabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(ifplabel, MAGIC_IFNET);
	LABEL_CHECK(newlabel, MAGIC_IFNET);
	COUNTER_INC(ifnet_check_relabel);

	return (0);
}

COUNTER_DECL(ifnet_check_transmit);
static int
test_ifnet_check_transmit(struct ifnet *ifp, struct label *ifplabel,
    struct mbuf *m, struct label *mlabel)
{

	LABEL_CHECK(ifplabel, MAGIC_IFNET);
	LABEL_CHECK(mlabel, MAGIC_MBUF);
	COUNTER_INC(ifnet_check_transmit);

	return (0);
}

COUNTER_DECL(ifnet_copy_label);
static void
test_ifnet_copy_label(struct label *src, struct label *dest)
{

	LABEL_CHECK(src, MAGIC_IFNET);
	LABEL_CHECK(dest, MAGIC_IFNET);
	COUNTER_INC(ifnet_copy_label);
}

COUNTER_DECL(ifnet_create);
static void
test_ifnet_create(struct ifnet *ifp, struct label *ifplabel)
{

	LABEL_CHECK(ifplabel, MAGIC_IFNET);
	COUNTER_INC(ifnet_create);
}

COUNTER_DECL(ifnet_create_mbuf);
static void
test_ifnet_create_mbuf(struct ifnet *ifp, struct label *ifplabel,
    struct mbuf *m, struct label *mlabel)
{

	LABEL_CHECK(ifplabel, MAGIC_IFNET);
	LABEL_CHECK(mlabel, MAGIC_MBUF);
	COUNTER_INC(ifnet_create_mbuf);
}

COUNTER_DECL(ifnet_destroy_label);
static void
test_ifnet_destroy_label(struct label *label)
{

	LABEL_DESTROY(label, MAGIC_IFNET);
	COUNTER_INC(ifnet_destroy_label);
}

COUNTER_DECL(ifnet_externalize_label);
static int
test_ifnet_externalize_label(struct label *label, char *element_name,
    struct sbuf *sb, int *claimed)
{

	LABEL_CHECK(label, MAGIC_IFNET);
	COUNTER_INC(ifnet_externalize_label);

	return (0);
}

COUNTER_DECL(ifnet_init_label);
static void
test_ifnet_init_label(struct label *label)
{

	LABEL_INIT(label, MAGIC_IFNET);
	COUNTER_INC(ifnet_init_label);
}

COUNTER_DECL(ifnet_internalize_label);
static int
test_ifnet_internalize_label(struct label *label, char *element_name,
    char *element_data, int *claimed)
{

	LABEL_CHECK(label, MAGIC_IFNET);
	COUNTER_INC(ifnet_internalize_label);

	return (0);
}

COUNTER_DECL(ifnet_relabel);
static void
test_ifnet_relabel(struct ucred *cred, struct ifnet *ifp,
    struct label *ifplabel, struct label *newlabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(ifplabel, MAGIC_IFNET);
	LABEL_CHECK(newlabel, MAGIC_IFNET);
	COUNTER_INC(ifnet_relabel);
}

COUNTER_DECL(inpcb_check_deliver);
static int
test_inpcb_check_deliver(struct inpcb *inp, struct label *inplabel,
    struct mbuf *m, struct label *mlabel)
{

	LABEL_CHECK(inplabel, MAGIC_INPCB);
	LABEL_CHECK(mlabel, MAGIC_MBUF);
	COUNTER_INC(inpcb_check_deliver);

	return (0);
}

COUNTER_DECL(inpcb_check_visible);
static int
test_inpcb_check_visible(struct ucred *cred, struct inpcb *inp,
    struct label *inplabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(inplabel, MAGIC_INPCB);
	COUNTER_INC(inpcb_check_visible);

	return (0);
}

COUNTER_DECL(inpcb_create);
static void
test_inpcb_create(struct socket *so, struct label *solabel,
    struct inpcb *inp, struct label *inplabel)
{

	SOCK_LOCK(so);
	LABEL_CHECK(solabel, MAGIC_SOCKET);
	SOCK_UNLOCK(so);
	LABEL_CHECK(inplabel, MAGIC_INPCB);
	COUNTER_INC(inpcb_create);
}

COUNTER_DECL(inpcb_create_mbuf);
static void
test_inpcb_create_mbuf(struct inpcb *inp, struct label *inplabel,
    struct mbuf *m, struct label *mlabel)
{

	LABEL_CHECK(inplabel, MAGIC_INPCB);
	LABEL_CHECK(mlabel, MAGIC_MBUF);
	COUNTER_INC(inpcb_create_mbuf);
}

COUNTER_DECL(inpcb_destroy_label);
static void
test_inpcb_destroy_label(struct label *label)
{

	LABEL_DESTROY(label, MAGIC_INPCB);
	COUNTER_INC(inpcb_destroy_label);
}

COUNTER_DECL(inpcb_init_label);
static int
test_inpcb_init_label(struct label *label, int flag)
{

	if (flag & M_WAITOK)
		WITNESS_WARN(WARN_GIANTOK | WARN_SLEEPOK, NULL,
		    "test_inpcb_init_label() at %s:%d", __FILE__,
		    __LINE__);

	LABEL_INIT(label, MAGIC_INPCB);
	COUNTER_INC(inpcb_init_label);
	return (0);
}

COUNTER_DECL(inpcb_sosetlabel);
static void
test_inpcb_sosetlabel(struct socket *so, struct label *solabel,
    struct inpcb *inp, struct label *inplabel)
{

	SOCK_LOCK_ASSERT(so);

	LABEL_CHECK(solabel, MAGIC_SOCKET);
	LABEL_CHECK(inplabel, MAGIC_INPCB);
	COUNTER_INC(inpcb_sosetlabel);
}

COUNTER_DECL(ip6q_create);
static void
test_ip6q_create(struct mbuf *fragment, struct label *fragmentlabel,
    struct ip6q *q6, struct label *q6label)
{

	LABEL_CHECK(fragmentlabel, MAGIC_MBUF);
	LABEL_CHECK(q6label, MAGIC_IP6Q);
	COUNTER_INC(ip6q_create);
}

COUNTER_DECL(ip6q_destroy_label);
static void
test_ip6q_destroy_label(struct label *label)
{

	LABEL_DESTROY(label, MAGIC_IP6Q);
	COUNTER_INC(ip6q_destroy_label);
}

COUNTER_DECL(ip6q_init_label);
static int
test_ip6q_init_label(struct label *label, int flag)
{

	if (flag & M_WAITOK)
		WITNESS_WARN(WARN_GIANTOK | WARN_SLEEPOK, NULL,
		    "test_ip6q_init_label() at %s:%d", __FILE__,
		    __LINE__);

	LABEL_INIT(label, MAGIC_IP6Q);
	COUNTER_INC(ip6q_init_label);
	return (0);
}

COUNTER_DECL(ip6q_match);
static int
test_ip6q_match(struct mbuf *fragment, struct label *fragmentlabel,
    struct ip6q *q6, struct label *q6label)
{

	LABEL_CHECK(fragmentlabel, MAGIC_MBUF);
	LABEL_CHECK(q6label, MAGIC_IP6Q);
	COUNTER_INC(ip6q_match);

	return (1);
}

COUNTER_DECL(ip6q_reassemble);
static void
test_ip6q_reassemble(struct ip6q *q6, struct label *q6label, struct mbuf *m,
   struct label *mlabel)
{

	LABEL_CHECK(q6label, MAGIC_IP6Q);
	LABEL_CHECK(mlabel, MAGIC_MBUF);
	COUNTER_INC(ip6q_reassemble);
}

COUNTER_DECL(ip6q_update);
static void
test_ip6q_update(struct mbuf *m, struct label *mlabel, struct ip6q *q6,
    struct label *q6label)
{

	LABEL_CHECK(mlabel, MAGIC_MBUF);
	LABEL_CHECK(q6label, MAGIC_IP6Q);
	COUNTER_INC(ip6q_update);
}

COUNTER_DECL(ipq_create);
static void
test_ipq_create(struct mbuf *fragment, struct label *fragmentlabel,
    struct ipq *q, struct label *qlabel)
{

	LABEL_CHECK(fragmentlabel, MAGIC_MBUF);
	LABEL_CHECK(qlabel, MAGIC_IPQ);
	COUNTER_INC(ipq_create);
}

COUNTER_DECL(ipq_destroy_label);
static void
test_ipq_destroy_label(struct label *label)
{

	LABEL_DESTROY(label, MAGIC_IPQ);
	COUNTER_INC(ipq_destroy_label);
}

COUNTER_DECL(ipq_init_label);
static int
test_ipq_init_label(struct label *label, int flag)
{

	if (flag & M_WAITOK)
		WITNESS_WARN(WARN_GIANTOK | WARN_SLEEPOK, NULL,
		    "test_ipq_init_label() at %s:%d", __FILE__,
		    __LINE__);

	LABEL_INIT(label, MAGIC_IPQ);
	COUNTER_INC(ipq_init_label);
	return (0);
}

COUNTER_DECL(ipq_match);
static int
test_ipq_match(struct mbuf *fragment, struct label *fragmentlabel,
    struct ipq *q, struct label *qlabel)
{

	LABEL_CHECK(fragmentlabel, MAGIC_MBUF);
	LABEL_CHECK(qlabel, MAGIC_IPQ);
	COUNTER_INC(ipq_match);

	return (1);
}

COUNTER_DECL(ipq_reassemble);
static void
test_ipq_reassemble(struct ipq *q, struct label *qlabel, struct mbuf *m,
   struct label *mlabel)
{

	LABEL_CHECK(qlabel, MAGIC_IPQ);
	LABEL_CHECK(mlabel, MAGIC_MBUF);
	COUNTER_INC(ipq_reassemble);
}

COUNTER_DECL(ipq_update);
static void
test_ipq_update(struct mbuf *m, struct label *mlabel, struct ipq *q,
    struct label *qlabel)
{

	LABEL_CHECK(mlabel, MAGIC_MBUF);
	LABEL_CHECK(qlabel, MAGIC_IPQ);
	COUNTER_INC(ipq_update);
}

COUNTER_DECL(kenv_check_dump);
static int
test_kenv_check_dump(struct ucred *cred)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	COUNTER_INC(kenv_check_dump);

	return (0);
}

COUNTER_DECL(kenv_check_get);
static int
test_kenv_check_get(struct ucred *cred, char *name)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	COUNTER_INC(kenv_check_get);

	return (0);
}

COUNTER_DECL(kenv_check_set);
static int
test_kenv_check_set(struct ucred *cred, char *name, char *value)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	COUNTER_INC(kenv_check_set);

	return (0);
}

COUNTER_DECL(kenv_check_unset);
static int
test_kenv_check_unset(struct ucred *cred, char *name)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	COUNTER_INC(kenv_check_unset);

	return (0);
}

COUNTER_DECL(kld_check_load);
static int
test_kld_check_load(struct ucred *cred, struct vnode *vp,
    struct label *label)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(label, MAGIC_VNODE);
	COUNTER_INC(kld_check_load);

	return (0);
}

COUNTER_DECL(kld_check_stat);
static int
test_kld_check_stat(struct ucred *cred)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	COUNTER_INC(kld_check_stat);

	return (0);
}

COUNTER_DECL(mbuf_copy_label);
static void
test_mbuf_copy_label(struct label *src, struct label *dest)
{

	LABEL_CHECK(src, MAGIC_MBUF);
	LABEL_CHECK(dest, MAGIC_MBUF);
	COUNTER_INC(mbuf_copy_label);
}

COUNTER_DECL(mbuf_destroy_label);
static void
test_mbuf_destroy_label(struct label *label)
{

	/*
	 * If we're loaded dynamically, there may be mbufs in flight that
	 * didn't have label storage allocated for them.  Handle this
	 * gracefully.
	 */
	if (label == NULL)
		return;

	LABEL_DESTROY(label, MAGIC_MBUF);
	COUNTER_INC(mbuf_destroy_label);
}

COUNTER_DECL(mbuf_init_label);
static int
test_mbuf_init_label(struct label *label, int flag)
{

	if (flag & M_WAITOK)
		WITNESS_WARN(WARN_GIANTOK | WARN_SLEEPOK, NULL,
		    "test_mbuf_init_label() at %s:%d", __FILE__,
		    __LINE__);

	LABEL_INIT(label, MAGIC_MBUF);
	COUNTER_INC(mbuf_init_label);
	return (0);
}

COUNTER_DECL(mount_check_stat);
static int
test_mount_check_stat(struct ucred *cred, struct mount *mp,
    struct label *mplabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(mplabel, MAGIC_MOUNT);
	COUNTER_INC(mount_check_stat);

	return (0);
}

COUNTER_DECL(mount_create);
static void
test_mount_create(struct ucred *cred, struct mount *mp,
    struct label *mplabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(mplabel, MAGIC_MOUNT);
	COUNTER_INC(mount_create);
}

COUNTER_DECL(mount_destroy_label);
static void
test_mount_destroy_label(struct label *label)
{

	LABEL_DESTROY(label, MAGIC_MOUNT);
	COUNTER_INC(mount_destroy_label);
}

COUNTER_DECL(mount_init_label);
static void
test_mount_init_label(struct label *label)
{

	LABEL_INIT(label, MAGIC_MOUNT);
	COUNTER_INC(mount_init_label);
}

COUNTER_DECL(netinet_arp_send);
static void
test_netinet_arp_send(struct ifnet *ifp, struct label *ifplabel,
    struct mbuf *m, struct label *mlabel)
{

	LABEL_CHECK(ifplabel, MAGIC_IFNET);
	LABEL_CHECK(mlabel, MAGIC_MBUF);
	COUNTER_INC(netinet_arp_send);
}

COUNTER_DECL(netinet_fragment);
static void
test_netinet_fragment(struct mbuf *m, struct label *mlabel,
    struct mbuf *frag, struct label *fraglabel)
{

	LABEL_CHECK(mlabel, MAGIC_MBUF);
	LABEL_CHECK(fraglabel, MAGIC_MBUF);
	COUNTER_INC(netinet_fragment);
}

COUNTER_DECL(netinet_icmp_reply);
static void
test_netinet_icmp_reply(struct mbuf *mrecv, struct label *mrecvlabel,
    struct mbuf *msend, struct label *msendlabel)
{

	LABEL_CHECK(mrecvlabel, MAGIC_MBUF);
	LABEL_CHECK(msendlabel, MAGIC_MBUF);
	COUNTER_INC(netinet_icmp_reply);
}

COUNTER_DECL(netinet_icmp_replyinplace);
static void
test_netinet_icmp_replyinplace(struct mbuf *m, struct label *mlabel)
{

	LABEL_CHECK(mlabel, MAGIC_MBUF);
	COUNTER_INC(netinet_icmp_replyinplace);
}

COUNTER_DECL(netinet_igmp_send);
static void
test_netinet_igmp_send(struct ifnet *ifp, struct label *ifplabel,
    struct mbuf *m, struct label *mlabel)
{

	LABEL_CHECK(ifplabel, MAGIC_IFNET);
	LABEL_CHECK(mlabel, MAGIC_MBUF);
	COUNTER_INC(netinet_igmp_send);
}

COUNTER_DECL(netinet_tcp_reply);
static void
test_netinet_tcp_reply(struct mbuf *m, struct label *mlabel)
{

	LABEL_CHECK(mlabel, MAGIC_MBUF);
	COUNTER_INC(netinet_tcp_reply);
}

COUNTER_DECL(netinet6_nd6_send);
static void
test_netinet6_nd6_send(struct ifnet *ifp, struct label *ifplabel,
    struct mbuf *m, struct label *mlabel)
{

	LABEL_CHECK(ifplabel, MAGIC_IFNET);
	LABEL_CHECK(mlabel, MAGIC_MBUF);
	COUNTER_INC(netinet6_nd6_send);
}

COUNTER_DECL(pipe_check_ioctl);
static int
test_pipe_check_ioctl(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel, unsigned long cmd, void /* caddr_t */ *data)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(pplabel, MAGIC_PIPE);
	COUNTER_INC(pipe_check_ioctl);

	return (0);
}

COUNTER_DECL(pipe_check_poll);
static int
test_pipe_check_poll(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(pplabel, MAGIC_PIPE);
	COUNTER_INC(pipe_check_poll);

	return (0);
}

COUNTER_DECL(pipe_check_read);
static int
test_pipe_check_read(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(pplabel, MAGIC_PIPE);
	COUNTER_INC(pipe_check_read);

	return (0);
}

COUNTER_DECL(pipe_check_relabel);
static int
test_pipe_check_relabel(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel, struct label *newlabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(pplabel, MAGIC_PIPE);
	LABEL_CHECK(newlabel, MAGIC_PIPE);
	COUNTER_INC(pipe_check_relabel);

	return (0);
}

COUNTER_DECL(pipe_check_stat);
static int
test_pipe_check_stat(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(pplabel, MAGIC_PIPE);
	COUNTER_INC(pipe_check_stat);

	return (0);
}

COUNTER_DECL(pipe_check_write);
static int
test_pipe_check_write(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(pplabel, MAGIC_PIPE);
	COUNTER_INC(pipe_check_write);

	return (0);
}

COUNTER_DECL(pipe_copy_label);
static void
test_pipe_copy_label(struct label *src, struct label *dest)
{

	LABEL_CHECK(src, MAGIC_PIPE);
	LABEL_CHECK(dest, MAGIC_PIPE);
	COUNTER_INC(pipe_copy_label);
}

COUNTER_DECL(pipe_create);
static void
test_pipe_create(struct ucred *cred, struct pipepair *pp,
   struct label *pplabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(pplabel, MAGIC_PIPE);
	COUNTER_INC(pipe_create);
}

COUNTER_DECL(pipe_destroy_label);
static void
test_pipe_destroy_label(struct label *label)
{

	LABEL_DESTROY(label, MAGIC_PIPE);
	COUNTER_INC(pipe_destroy_label);
}

COUNTER_DECL(pipe_externalize_label);
static int
test_pipe_externalize_label(struct label *label, char *element_name,
    struct sbuf *sb, int *claimed)
{

	LABEL_CHECK(label, MAGIC_PIPE);
	COUNTER_INC(pipe_externalize_label);

	return (0);
}

COUNTER_DECL(pipe_init_label);
static void
test_pipe_init_label(struct label *label)
{

	LABEL_INIT(label, MAGIC_PIPE);
	COUNTER_INC(pipe_init_label);
}

COUNTER_DECL(pipe_internalize_label);
static int
test_pipe_internalize_label(struct label *label, char *element_name,
    char *element_data, int *claimed)
{

	LABEL_CHECK(label, MAGIC_PIPE);
	COUNTER_INC(pipe_internalize_label);

	return (0);
}

COUNTER_DECL(pipe_relabel);
static void
test_pipe_relabel(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel, struct label *newlabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(pplabel, MAGIC_PIPE);
	LABEL_CHECK(newlabel, MAGIC_PIPE);
	COUNTER_INC(pipe_relabel);
}

COUNTER_DECL(posixsem_check_getvalue);
static int
test_posixsem_check_getvalue(struct ucred *active_cred, struct ucred *file_cred,
    struct ksem *ks, struct label *kslabel)
{

	LABEL_CHECK(active_cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(file_cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(kslabel, MAGIC_POSIX_SEM);
	COUNTER_INC(posixsem_check_getvalue);

	return (0);
}

COUNTER_DECL(posixsem_check_open);
static int
test_posixsem_check_open(struct ucred *cred, struct ksem *ks,
    struct label *kslabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(kslabel, MAGIC_POSIX_SEM);
	COUNTER_INC(posixsem_check_open);

	return (0);
}

COUNTER_DECL(posixsem_check_post);
static int
test_posixsem_check_post(struct ucred *active_cred, struct ucred *file_cred,
    struct ksem *ks, struct label *kslabel)
{

	LABEL_CHECK(active_cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(file_cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(kslabel, MAGIC_POSIX_SEM);
	COUNTER_INC(posixsem_check_post);

	return (0);
}

COUNTER_DECL(posixsem_check_setmode);
static int
test_posixsem_check_setmode(struct ucred *cred, struct ksem *ks,
    struct label *kslabel, mode_t mode)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(kslabel, MAGIC_POSIX_SHM);
	COUNTER_INC(posixsem_check_setmode);
	return (0);
}

COUNTER_DECL(posixsem_check_setowner);
static int
test_posixsem_check_setowner(struct ucred *cred, struct ksem *ks,
    struct label *kslabel, uid_t uid, gid_t gid)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(kslabel, MAGIC_POSIX_SHM);
	COUNTER_INC(posixsem_check_setowner);
	return (0);
}

COUNTER_DECL(posixsem_check_stat);
static int
test_posixsem_check_stat(struct ucred *active_cred,
    struct ucred *file_cred, struct ksem *ks, struct label *kslabel)
{

	LABEL_CHECK(active_cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(file_cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(kslabel, MAGIC_POSIX_SEM);
	COUNTER_INC(posixsem_check_stat);
	return (0);
}

COUNTER_DECL(posixsem_check_unlink);
static int
test_posixsem_check_unlink(struct ucred *cred, struct ksem *ks,
    struct label *kslabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(kslabel, MAGIC_POSIX_SEM);
	COUNTER_INC(posixsem_check_unlink);

	return (0);
}

COUNTER_DECL(posixsem_check_wait);
static int
test_posixsem_check_wait(struct ucred *active_cred, struct ucred *file_cred,
    struct ksem *ks, struct label *kslabel)
{

	LABEL_CHECK(active_cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(file_cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(kslabel, MAGIC_POSIX_SEM);
	COUNTER_INC(posixsem_check_wait);

	return (0);
}

COUNTER_DECL(posixsem_create);
static void
test_posixsem_create(struct ucred *cred, struct ksem *ks,
   struct label *kslabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(kslabel, MAGIC_POSIX_SEM);
	COUNTER_INC(posixsem_create);
}

COUNTER_DECL(posixsem_destroy_label);
static void
test_posixsem_destroy_label(struct label *label)
{

	LABEL_DESTROY(label, MAGIC_POSIX_SEM);
	COUNTER_INC(posixsem_destroy_label);
}

COUNTER_DECL(posixsem_init_label);
static void
test_posixsem_init_label(struct label *label)
{

	LABEL_INIT(label, MAGIC_POSIX_SEM);
	COUNTER_INC(posixsem_init_label);
}

COUNTER_DECL(posixshm_check_create);
static int
test_posixshm_check_create(struct ucred *cred, const char *path)
{

	COUNTER_INC(posixshm_check_create);
	return (0);
}

COUNTER_DECL(posixshm_check_mmap);
static int
test_posixshm_check_mmap(struct ucred *cred, struct shmfd *shmfd,
    struct label *shmfdlabel, int prot, int flags)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(shmfdlabel, MAGIC_POSIX_SHM);
	COUNTER_INC(posixshm_check_mmap);
	return (0);
}

COUNTER_DECL(posixshm_check_open);
static int
test_posixshm_check_open(struct ucred *cred, struct shmfd *shmfd,
    struct label *shmfdlabel, accmode_t accmode)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(shmfdlabel, MAGIC_POSIX_SHM);
	COUNTER_INC(posixshm_check_open);
	return (0);
}

COUNTER_DECL(posixshm_check_read);
static int
test_posixshm_check_read(struct ucred *active_cred,
    struct ucred *file_cred, struct shmfd *shm, struct label *shmlabel)
{

	LABEL_CHECK(active_cred->cr_label, MAGIC_CRED);
	if (file_cred != NULL)
		LABEL_CHECK(file_cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(shmlabel, MAGIC_POSIX_SHM);
	COUNTER_INC(posixshm_check_read);

	return (0);
}

COUNTER_DECL(posixshm_check_setmode);
static int
test_posixshm_check_setmode(struct ucred *cred, struct shmfd *shmfd,
    struct label *shmfdlabel, mode_t mode)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(shmfdlabel, MAGIC_POSIX_SHM);
	COUNTER_INC(posixshm_check_setmode);
	return (0);
}

COUNTER_DECL(posixshm_check_setowner);
static int
test_posixshm_check_setowner(struct ucred *cred, struct shmfd *shmfd,
    struct label *shmfdlabel, uid_t uid, gid_t gid)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(shmfdlabel, MAGIC_POSIX_SHM);
	COUNTER_INC(posixshm_check_setowner);
	return (0);
}

COUNTER_DECL(posixshm_check_stat);
static int
test_posixshm_check_stat(struct ucred *active_cred,
    struct ucred *file_cred, struct shmfd *shmfd, struct label *shmfdlabel)
{

	LABEL_CHECK(active_cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(file_cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(shmfdlabel, MAGIC_POSIX_SHM);
	COUNTER_INC(posixshm_check_stat);
	return (0);
}

COUNTER_DECL(posixshm_check_truncate);
static int
test_posixshm_check_truncate(struct ucred *active_cred,
    struct ucred *file_cred, struct shmfd *shmfd, struct label *shmfdlabel)
{

	LABEL_CHECK(active_cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(file_cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(shmfdlabel, MAGIC_POSIX_SHM);
	COUNTER_INC(posixshm_check_truncate);
	return (0);
}

COUNTER_DECL(posixshm_check_unlink);
static int
test_posixshm_check_unlink(struct ucred *cred, struct shmfd *shmfd,
    struct label *shmfdlabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(shmfdlabel, MAGIC_POSIX_SHM);
	COUNTER_INC(posixshm_check_unlink);
	return (0);
}

COUNTER_DECL(posixshm_check_write);
static int
test_posixshm_check_write(struct ucred *active_cred,
    struct ucred *file_cred, struct shmfd *shm, struct label *shmlabel)
{

	LABEL_CHECK(active_cred->cr_label, MAGIC_CRED);
	if (file_cred != NULL)
		LABEL_CHECK(file_cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(shmlabel, MAGIC_POSIX_SHM);
	COUNTER_INC(posixshm_check_write);

	return (0);
}

COUNTER_DECL(posixshm_create);
static void
test_posixshm_create(struct ucred *cred, struct shmfd *shmfd,
   struct label *shmfdlabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(shmfdlabel, MAGIC_POSIX_SHM);
	COUNTER_INC(posixshm_create);
}

COUNTER_DECL(posixshm_destroy_label);
static void
test_posixshm_destroy_label(struct label *label)
{

	LABEL_DESTROY(label, MAGIC_POSIX_SHM);
	COUNTER_INC(posixshm_destroy_label);
}

COUNTER_DECL(posixshm_init_label);
static void
test_posixshm_init_label(struct label *label)
{

	LABEL_INIT(label, MAGIC_POSIX_SHM);
	COUNTER_INC(posixshm_init_label);
}

COUNTER_DECL(proc_check_debug);
static int
test_proc_check_debug(struct ucred *cred, struct proc *p)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(p->p_ucred->cr_label, MAGIC_CRED);
	COUNTER_INC(proc_check_debug);

	return (0);
}

COUNTER_DECL(proc_check_sched);
static int
test_proc_check_sched(struct ucred *cred, struct proc *p)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(p->p_ucred->cr_label, MAGIC_CRED);
	COUNTER_INC(proc_check_sched);

	return (0);
}

COUNTER_DECL(proc_check_signal);
static int
test_proc_check_signal(struct ucred *cred, struct proc *p, int signum)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(p->p_ucred->cr_label, MAGIC_CRED);
	COUNTER_INC(proc_check_signal);

	return (0);
}

COUNTER_DECL(proc_check_wait);
static int
test_proc_check_wait(struct ucred *cred, struct proc *p)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(p->p_ucred->cr_label, MAGIC_CRED);
	COUNTER_INC(proc_check_wait);

	return (0);
}

COUNTER_DECL(proc_destroy_label);
static void
test_proc_destroy_label(struct label *label)
{

	LABEL_DESTROY(label, MAGIC_PROC);
	COUNTER_INC(proc_destroy_label);
}

COUNTER_DECL(proc_init_label);
static void
test_proc_init_label(struct label *label)
{

	LABEL_INIT(label, MAGIC_PROC);
	COUNTER_INC(proc_init_label);
}

COUNTER_DECL(socket_check_accept);
static int
test_socket_check_accept(struct ucred *cred, struct socket *so,
    struct label *solabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	SOCK_LOCK(so);
	LABEL_CHECK(solabel, MAGIC_SOCKET);
	SOCK_UNLOCK(so);
	COUNTER_INC(socket_check_accept);

	return (0);
}

COUNTER_DECL(socket_check_bind);
static int
test_socket_check_bind(struct ucred *cred, struct socket *so,
    struct label *solabel, struct sockaddr *sa)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	SOCK_LOCK(so);
	LABEL_CHECK(solabel, MAGIC_SOCKET);
	SOCK_UNLOCK(so);
	COUNTER_INC(socket_check_bind);

	return (0);
}

COUNTER_DECL(socket_check_connect);
static int
test_socket_check_connect(struct ucred *cred, struct socket *so,
    struct label *solabel, struct sockaddr *sa)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	SOCK_LOCK(so);
	LABEL_CHECK(solabel, MAGIC_SOCKET);
	SOCK_UNLOCK(so);
	COUNTER_INC(socket_check_connect);

	return (0);
}

COUNTER_DECL(socket_check_deliver);
static int
test_socket_check_deliver(struct socket *so, struct label *solabel,
    struct mbuf *m, struct label *mlabel)
{

	SOCK_LOCK(so);
	LABEL_CHECK(solabel, MAGIC_SOCKET);
	SOCK_UNLOCK(so);
	LABEL_CHECK(mlabel, MAGIC_MBUF);
	COUNTER_INC(socket_check_deliver);

	return (0);
}

COUNTER_DECL(socket_check_listen);
static int
test_socket_check_listen(struct ucred *cred, struct socket *so,
    struct label *solabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	SOCK_LOCK(so);
	LABEL_CHECK(solabel, MAGIC_SOCKET);
	SOCK_UNLOCK(so);
	COUNTER_INC(socket_check_listen);

	return (0);
}

COUNTER_DECL(socket_check_poll);
static int
test_socket_check_poll(struct ucred *cred, struct socket *so,
    struct label *solabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	SOCK_LOCK(so);
	LABEL_CHECK(solabel, MAGIC_SOCKET);
	SOCK_UNLOCK(so);
	COUNTER_INC(socket_check_poll);

	return (0);
}

COUNTER_DECL(socket_check_receive);
static int
test_socket_check_receive(struct ucred *cred, struct socket *so,
    struct label *solabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	SOCK_LOCK(so);
	LABEL_CHECK(solabel, MAGIC_SOCKET);
	SOCK_UNLOCK(so);
	COUNTER_INC(socket_check_receive);

	return (0);
}

COUNTER_DECL(socket_check_relabel);
static int
test_socket_check_relabel(struct ucred *cred, struct socket *so,
    struct label *solabel, struct label *newlabel)
{

	SOCK_LOCK_ASSERT(so);

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(solabel, MAGIC_SOCKET);
	LABEL_CHECK(newlabel, MAGIC_SOCKET);
	COUNTER_INC(socket_check_relabel);

	return (0);
}

COUNTER_DECL(socket_check_send);
static int
test_socket_check_send(struct ucred *cred, struct socket *so,
    struct label *solabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	SOCK_LOCK(so);
	LABEL_CHECK(solabel, MAGIC_SOCKET);
	SOCK_UNLOCK(so);
	COUNTER_INC(socket_check_send);

	return (0);
}

COUNTER_DECL(socket_check_stat);
static int
test_socket_check_stat(struct ucred *cred, struct socket *so,
    struct label *solabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	SOCK_LOCK(so);
	LABEL_CHECK(solabel, MAGIC_SOCKET);
	SOCK_UNLOCK(so);
	COUNTER_INC(socket_check_stat);

	return (0);
}

COUNTER_DECL(socket_check_visible);
static int
test_socket_check_visible(struct ucred *cred, struct socket *so,
    struct label *solabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	SOCK_LOCK(so);
	LABEL_CHECK(solabel, MAGIC_SOCKET);
	SOCK_UNLOCK(so);
	COUNTER_INC(socket_check_visible);

	return (0);
}

COUNTER_DECL(socket_copy_label);
static void
test_socket_copy_label(struct label *src, struct label *dest)
{

	LABEL_CHECK(src, MAGIC_SOCKET);
	LABEL_CHECK(dest, MAGIC_SOCKET);
	COUNTER_INC(socket_copy_label);
}

COUNTER_DECL(socket_create);
static void
test_socket_create(struct ucred *cred, struct socket *so,
    struct label *solabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(solabel, MAGIC_SOCKET);
	COUNTER_INC(socket_create);
}

COUNTER_DECL(socket_create_mbuf);
static void
test_socket_create_mbuf(struct socket *so, struct label *solabel,
    struct mbuf *m, struct label *mlabel)
{

	SOCK_LOCK(so);
	LABEL_CHECK(solabel, MAGIC_SOCKET);
	SOCK_UNLOCK(so);
	LABEL_CHECK(mlabel, MAGIC_MBUF);
	COUNTER_INC(socket_create_mbuf);
}

COUNTER_DECL(socket_destroy_label);
static void
test_socket_destroy_label(struct label *label)
{

	LABEL_DESTROY(label, MAGIC_SOCKET);
	COUNTER_INC(socket_destroy_label);
}

COUNTER_DECL(socket_externalize_label);
static int
test_socket_externalize_label(struct label *label, char *element_name,
    struct sbuf *sb, int *claimed)
{

	LABEL_CHECK(label, MAGIC_SOCKET);
	COUNTER_INC(socket_externalize_label);

	return (0);
}

COUNTER_DECL(socket_init_label);
static int
test_socket_init_label(struct label *label, int flag)
{

	if (flag & M_WAITOK)
		WITNESS_WARN(WARN_GIANTOK | WARN_SLEEPOK, NULL,
		    "test_socket_init_label() at %s:%d", __FILE__,
		    __LINE__);

	LABEL_INIT(label, MAGIC_SOCKET);
	COUNTER_INC(socket_init_label);
	return (0);
}

COUNTER_DECL(socket_internalize_label);
static int
test_socket_internalize_label(struct label *label, char *element_name,
    char *element_data, int *claimed)
{

	LABEL_CHECK(label, MAGIC_SOCKET);
	COUNTER_INC(socket_internalize_label);

	return (0);
}

COUNTER_DECL(socket_newconn);
static void
test_socket_newconn(struct socket *oldso, struct label *oldsolabel,
    struct socket *newso, struct label *newsolabel)
{

	SOCK_LOCK(oldso);
	LABEL_CHECK(oldsolabel, MAGIC_SOCKET);
	SOCK_UNLOCK(oldso);
	SOCK_LOCK(newso);
	LABEL_CHECK(newsolabel, MAGIC_SOCKET);
	SOCK_UNLOCK(newso);
	COUNTER_INC(socket_newconn);
}

COUNTER_DECL(socket_relabel);
static void
test_socket_relabel(struct ucred *cred, struct socket *so,
    struct label *solabel, struct label *newlabel)
{

	SOCK_LOCK_ASSERT(so);

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(solabel, MAGIC_SOCKET);
	LABEL_CHECK(newlabel, MAGIC_SOCKET);
	COUNTER_INC(socket_relabel);
}

COUNTER_DECL(socketpeer_destroy_label);
static void
test_socketpeer_destroy_label(struct label *label)
{

	LABEL_DESTROY(label, MAGIC_SOCKET);
	COUNTER_INC(socketpeer_destroy_label);
}

COUNTER_DECL(socketpeer_externalize_label);
static int
test_socketpeer_externalize_label(struct label *label, char *element_name,
    struct sbuf *sb, int *claimed)
{

	LABEL_CHECK(label, MAGIC_SOCKET);
	COUNTER_INC(socketpeer_externalize_label);

	return (0);
}

COUNTER_DECL(socketpeer_init_label);
static int
test_socketpeer_init_label(struct label *label, int flag)
{

	if (flag & M_WAITOK)
		WITNESS_WARN(WARN_GIANTOK | WARN_SLEEPOK, NULL,
		    "test_socketpeer_init_label() at %s:%d", __FILE__,
		    __LINE__);

	LABEL_INIT(label, MAGIC_SOCKET);
	COUNTER_INC(socketpeer_init_label);
	return (0);
}

COUNTER_DECL(socketpeer_set_from_mbuf);
static void
test_socketpeer_set_from_mbuf(struct mbuf *m, struct label *mlabel,
    struct socket *so, struct label *sopeerlabel)
{

	LABEL_CHECK(mlabel, MAGIC_MBUF);
	SOCK_LOCK(so);
	LABEL_CHECK(sopeerlabel, MAGIC_SOCKET);
	SOCK_UNLOCK(so);
	COUNTER_INC(socketpeer_set_from_mbuf);
}

COUNTER_DECL(socketpeer_set_from_socket);
static void
test_socketpeer_set_from_socket(struct socket *oldso,
    struct label *oldsolabel, struct socket *newso,
    struct label *newsopeerlabel)
{

	SOCK_LOCK(oldso);
	LABEL_CHECK(oldsolabel, MAGIC_SOCKET);
	SOCK_UNLOCK(oldso);
	SOCK_LOCK(newso);
	LABEL_CHECK(newsopeerlabel, MAGIC_SOCKET);
	SOCK_UNLOCK(newso);
	COUNTER_INC(socketpeer_set_from_socket);
}

COUNTER_DECL(syncache_create);
static void
test_syncache_create(struct label *label, struct inpcb *inp)
{

	LABEL_CHECK(label, MAGIC_SYNCACHE);
	COUNTER_INC(syncache_create);
}

COUNTER_DECL(syncache_create_mbuf);
static void
test_syncache_create_mbuf(struct label *sc_label, struct mbuf *m,
    struct label *mlabel)
{

	LABEL_CHECK(sc_label, MAGIC_SYNCACHE);
	LABEL_CHECK(mlabel, MAGIC_MBUF);
	COUNTER_INC(syncache_create_mbuf);
}

COUNTER_DECL(syncache_destroy_label);
static void
test_syncache_destroy_label(struct label *label)
{

	LABEL_DESTROY(label, MAGIC_SYNCACHE);
	COUNTER_INC(syncache_destroy_label);
}

COUNTER_DECL(syncache_init_label);
static int
test_syncache_init_label(struct label *label, int flag)
{

	if (flag & M_WAITOK)
		WITNESS_WARN(WARN_GIANTOK | WARN_SLEEPOK, NULL,
		    "test_syncache_init_label() at %s:%d", __FILE__,
		    __LINE__);
	LABEL_INIT(label, MAGIC_SYNCACHE);
	COUNTER_INC(syncache_init_label);
	return (0);
}

COUNTER_DECL(system_check_acct);
static int
test_system_check_acct(struct ucred *cred, struct vnode *vp,
    struct label *vplabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(vplabel, MAGIC_VNODE);
	COUNTER_INC(system_check_acct);

	return (0);
}

COUNTER_DECL(system_check_audit);
static int
test_system_check_audit(struct ucred *cred, void *record, int length)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	COUNTER_INC(system_check_audit);

	return (0);
}

COUNTER_DECL(system_check_auditctl);
static int
test_system_check_auditctl(struct ucred *cred, struct vnode *vp,
    struct label *vplabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(vplabel, MAGIC_VNODE);
	COUNTER_INC(system_check_auditctl);

	return (0);
}

COUNTER_DECL(system_check_auditon);
static int
test_system_check_auditon(struct ucred *cred, int cmd)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	COUNTER_INC(system_check_auditon);

	return (0);
}

COUNTER_DECL(system_check_reboot);
static int
test_system_check_reboot(struct ucred *cred, int how)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	COUNTER_INC(system_check_reboot);

	return (0);
}

COUNTER_DECL(system_check_swapoff);
static int
test_system_check_swapoff(struct ucred *cred, struct vnode *vp,
    struct label *vplabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(vplabel, MAGIC_VNODE);
	COUNTER_INC(system_check_swapoff);

	return (0);
}

COUNTER_DECL(system_check_swapon);
static int
test_system_check_swapon(struct ucred *cred, struct vnode *vp,
    struct label *vplabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(vplabel, MAGIC_VNODE);
	COUNTER_INC(system_check_swapon);

	return (0);
}

COUNTER_DECL(system_check_sysctl);
static int
test_system_check_sysctl(struct ucred *cred, struct sysctl_oid *oidp,
    void *arg1, int arg2, struct sysctl_req *req)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	COUNTER_INC(system_check_sysctl);

	return (0);
}

COUNTER_DECL(sysvmsg_cleanup);
static void
test_sysvmsg_cleanup(struct label *msglabel)
{

	LABEL_CHECK(msglabel, MAGIC_SYSV_MSG);
	COUNTER_INC(sysvmsg_cleanup);
}

COUNTER_DECL(sysvmsg_create);
static void
test_sysvmsg_create(struct ucred *cred, struct msqid_kernel *msqkptr,
    struct label *msqlabel, struct msg *msgptr, struct label *msglabel)
{

	LABEL_CHECK(msglabel, MAGIC_SYSV_MSG);
	LABEL_CHECK(msqlabel, MAGIC_SYSV_MSQ);
	COUNTER_INC(sysvmsg_create);
}

COUNTER_DECL(sysvmsg_destroy_label);
static void
test_sysvmsg_destroy_label(struct label *label)
{

	LABEL_DESTROY(label, MAGIC_SYSV_MSG);
	COUNTER_INC(sysvmsg_destroy_label);
}

COUNTER_DECL(sysvmsg_init_label);
static void
test_sysvmsg_init_label(struct label *label)
{
	LABEL_INIT(label, MAGIC_SYSV_MSG);
	COUNTER_INC(sysvmsg_init_label);
}

COUNTER_DECL(sysvmsq_check_msgmsq);
static int
test_sysvmsq_check_msgmsq(struct ucred *cred, struct msg *msgptr,
    struct label *msglabel, struct msqid_kernel *msqkptr,
    struct label *msqklabel)
{

	LABEL_CHECK(msqklabel, MAGIC_SYSV_MSQ);
	LABEL_CHECK(msglabel, MAGIC_SYSV_MSG);
	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	COUNTER_INC(sysvmsq_check_msgmsq);

  	return (0);
}

COUNTER_DECL(sysvmsq_check_msgrcv);
static int
test_sysvmsq_check_msgrcv(struct ucred *cred, struct msg *msgptr,
    struct label *msglabel)
{

	LABEL_CHECK(msglabel, MAGIC_SYSV_MSG);
	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	COUNTER_INC(sysvmsq_check_msgrcv);

	return (0);
}

COUNTER_DECL(sysvmsq_check_msgrmid);
static int
test_sysvmsq_check_msgrmid(struct ucred *cred, struct msg *msgptr,
    struct label *msglabel)
{

	LABEL_CHECK(msglabel, MAGIC_SYSV_MSG);
	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	COUNTER_INC(sysvmsq_check_msgrmid);

	return (0);
}

COUNTER_DECL(sysvmsq_check_msqget);
static int
test_sysvmsq_check_msqget(struct ucred *cred,
    struct msqid_kernel *msqkptr, struct label *msqklabel)
{

	LABEL_CHECK(msqklabel, MAGIC_SYSV_MSQ);
	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	COUNTER_INC(sysvmsq_check_msqget);

	return (0);
}

COUNTER_DECL(sysvmsq_check_msqsnd);
static int
test_sysvmsq_check_msqsnd(struct ucred *cred,
    struct msqid_kernel *msqkptr, struct label *msqklabel)
{

	LABEL_CHECK(msqklabel, MAGIC_SYSV_MSQ);
	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	COUNTER_INC(sysvmsq_check_msqsnd);

	return (0);
}

COUNTER_DECL(sysvmsq_check_msqrcv);
static int
test_sysvmsq_check_msqrcv(struct ucred *cred,
    struct msqid_kernel *msqkptr, struct label *msqklabel)
{

	LABEL_CHECK(msqklabel, MAGIC_SYSV_MSQ);
	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	COUNTER_INC(sysvmsq_check_msqrcv);

	return (0);
}

COUNTER_DECL(sysvmsq_check_msqctl);
static int
test_sysvmsq_check_msqctl(struct ucred *cred,
    struct msqid_kernel *msqkptr, struct label *msqklabel, int cmd)
{

	LABEL_CHECK(msqklabel, MAGIC_SYSV_MSQ);
	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	COUNTER_INC(sysvmsq_check_msqctl);

	return (0);
}

COUNTER_DECL(sysvmsq_cleanup);
static void
test_sysvmsq_cleanup(struct label *msqlabel)
{

	LABEL_CHECK(msqlabel, MAGIC_SYSV_MSQ);
	COUNTER_INC(sysvmsq_cleanup);
}

COUNTER_DECL(sysvmsq_create);
static void
test_sysvmsq_create(struct ucred *cred,
    struct msqid_kernel *msqkptr, struct label *msqlabel)
{

	LABEL_CHECK(msqlabel, MAGIC_SYSV_MSQ);
	COUNTER_INC(sysvmsq_create);
}

COUNTER_DECL(sysvmsq_destroy_label);
static void
test_sysvmsq_destroy_label(struct label *label)
{

	LABEL_DESTROY(label, MAGIC_SYSV_MSQ);
	COUNTER_INC(sysvmsq_destroy_label);
}

COUNTER_DECL(sysvmsq_init_label);
static void
test_sysvmsq_init_label(struct label *label)
{
	LABEL_INIT(label, MAGIC_SYSV_MSQ);
	COUNTER_INC(sysvmsq_init_label);
}

COUNTER_DECL(sysvsem_check_semctl);
static int
test_sysvsem_check_semctl(struct ucred *cred,
    struct semid_kernel *semakptr, struct label *semaklabel, int cmd)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(semaklabel, MAGIC_SYSV_SEM);
	COUNTER_INC(sysvsem_check_semctl);

  	return (0);
}

COUNTER_DECL(sysvsem_check_semget);
static int
test_sysvsem_check_semget(struct ucred *cred,
    struct semid_kernel *semakptr, struct label *semaklabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(semaklabel, MAGIC_SYSV_SEM);
	COUNTER_INC(sysvsem_check_semget);

	return (0);
}

COUNTER_DECL(sysvsem_check_semop);
static int
test_sysvsem_check_semop(struct ucred *cred,
    struct semid_kernel *semakptr, struct label *semaklabel, size_t accesstype)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(semaklabel, MAGIC_SYSV_SEM);
	COUNTER_INC(sysvsem_check_semop);

	return (0);
}

COUNTER_DECL(sysvsem_cleanup);
static void
test_sysvsem_cleanup(struct label *semalabel)
{

	LABEL_CHECK(semalabel, MAGIC_SYSV_SEM);
	COUNTER_INC(sysvsem_cleanup);
}

COUNTER_DECL(sysvsem_create);
static void
test_sysvsem_create(struct ucred *cred, struct semid_kernel *semakptr,
    struct label *semalabel)
{

	LABEL_CHECK(semalabel, MAGIC_SYSV_SEM);
	COUNTER_INC(sysvsem_create);
}

COUNTER_DECL(sysvsem_destroy_label);
static void
test_sysvsem_destroy_label(struct label *label)
{

	LABEL_DESTROY(label, MAGIC_SYSV_SEM);
	COUNTER_INC(sysvsem_destroy_label);
}

COUNTER_DECL(sysvsem_init_label);
static void
test_sysvsem_init_label(struct label *label)
{
	LABEL_INIT(label, MAGIC_SYSV_SEM);
	COUNTER_INC(sysvsem_init_label);
}

COUNTER_DECL(sysvshm_check_shmat);
static int
test_sysvshm_check_shmat(struct ucred *cred,
    struct shmid_kernel *shmsegptr, struct label *shmseglabel, int shmflg)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(shmseglabel, MAGIC_SYSV_SHM);
	COUNTER_INC(sysvshm_check_shmat);

  	return (0);
}

COUNTER_DECL(sysvshm_check_shmctl);
static int
test_sysvshm_check_shmctl(struct ucred *cred,
    struct shmid_kernel *shmsegptr, struct label *shmseglabel, int cmd)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(shmseglabel, MAGIC_SYSV_SHM);
	COUNTER_INC(sysvshm_check_shmctl);

  	return (0);
}

COUNTER_DECL(sysvshm_check_shmdt);
static int
test_sysvshm_check_shmdt(struct ucred *cred,
    struct shmid_kernel *shmsegptr, struct label *shmseglabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(shmseglabel, MAGIC_SYSV_SHM);
	COUNTER_INC(sysvshm_check_shmdt);

	return (0);
}

COUNTER_DECL(sysvshm_check_shmget);
static int
test_sysvshm_check_shmget(struct ucred *cred,
    struct shmid_kernel *shmsegptr, struct label *shmseglabel, int shmflg)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(shmseglabel, MAGIC_SYSV_SHM);
	COUNTER_INC(sysvshm_check_shmget);

	return (0);
}

COUNTER_DECL(sysvshm_cleanup);
static void
test_sysvshm_cleanup(struct label *shmlabel)
{

	LABEL_CHECK(shmlabel, MAGIC_SYSV_SHM);
	COUNTER_INC(sysvshm_cleanup);
}

COUNTER_DECL(sysvshm_create);
static void
test_sysvshm_create(struct ucred *cred, struct shmid_kernel *shmsegptr,
    struct label *shmlabel)
{

	LABEL_CHECK(shmlabel, MAGIC_SYSV_SHM);
	COUNTER_INC(sysvshm_create);
}

COUNTER_DECL(sysvshm_destroy_label);
static void
test_sysvshm_destroy_label(struct label *label)
{

	LABEL_DESTROY(label, MAGIC_SYSV_SHM);
	COUNTER_INC(sysvshm_destroy_label);
}

COUNTER_DECL(sysvshm_init_label);
static void
test_sysvshm_init_label(struct label *label)
{
	LABEL_INIT(label, MAGIC_SYSV_SHM);
	COUNTER_INC(sysvshm_init_label);
}

COUNTER_DECL(thread_userret);
static void
test_thread_userret(struct thread *td)
{

	COUNTER_INC(thread_userret);
}

COUNTER_DECL(vnode_associate_extattr);
static int
test_vnode_associate_extattr(struct mount *mp, struct label *mplabel,
    struct vnode *vp, struct label *vplabel)
{

	LABEL_CHECK(mplabel, MAGIC_MOUNT);
	LABEL_CHECK(vplabel, MAGIC_VNODE);
	COUNTER_INC(vnode_associate_extattr);

	return (0);
}

COUNTER_DECL(vnode_associate_singlelabel);
static void
test_vnode_associate_singlelabel(struct mount *mp, struct label *mplabel,
    struct vnode *vp, struct label *vplabel)
{

	LABEL_CHECK(mplabel, MAGIC_MOUNT);
	LABEL_CHECK(vplabel, MAGIC_VNODE);
	COUNTER_INC(vnode_associate_singlelabel);
}

COUNTER_DECL(vnode_check_access);
static int
test_vnode_check_access(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, accmode_t accmode)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(vplabel, MAGIC_VNODE);
	COUNTER_INC(vnode_check_access);

	return (0);
}

COUNTER_DECL(vnode_check_chdir);
static int
test_vnode_check_chdir(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(dvplabel, MAGIC_VNODE);
	COUNTER_INC(vnode_check_chdir);

	return (0);
}

COUNTER_DECL(vnode_check_chroot);
static int
test_vnode_check_chroot(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(dvplabel, MAGIC_VNODE);
	COUNTER_INC(vnode_check_chroot);

	return (0);
}

COUNTER_DECL(vnode_check_create);
static int
test_vnode_check_create(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel, struct componentname *cnp, struct vattr *vap)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(dvplabel, MAGIC_VNODE);
	COUNTER_INC(vnode_check_create);

	return (0);
}

COUNTER_DECL(vnode_check_deleteacl);
static int
test_vnode_check_deleteacl(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, acl_type_t type)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(vplabel, MAGIC_VNODE);
	COUNTER_INC(vnode_check_deleteacl);

	return (0);
}

COUNTER_DECL(vnode_check_deleteextattr);
static int
test_vnode_check_deleteextattr(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, int attrnamespace, const char *name)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(vplabel, MAGIC_VNODE);
	COUNTER_INC(vnode_check_deleteextattr);

	return (0);
}

COUNTER_DECL(vnode_check_exec);
static int
test_vnode_check_exec(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, struct image_params *imgp,
    struct label *execlabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(vplabel, MAGIC_VNODE);
	LABEL_CHECK(execlabel, MAGIC_CRED);
	COUNTER_INC(vnode_check_exec);

	return (0);
}

COUNTER_DECL(vnode_check_getacl);
static int
test_vnode_check_getacl(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, acl_type_t type)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(vplabel, MAGIC_VNODE);
	COUNTER_INC(vnode_check_getacl);

	return (0);
}

COUNTER_DECL(vnode_check_getextattr);
static int
test_vnode_check_getextattr(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, int attrnamespace, const char *name)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(vplabel, MAGIC_VNODE);
	COUNTER_INC(vnode_check_getextattr);

	return (0);
}

COUNTER_DECL(vnode_check_link);
static int
test_vnode_check_link(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel, struct vnode *vp, struct label *vplabel,
    struct componentname *cnp)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(dvplabel, MAGIC_VNODE);
	LABEL_CHECK(vplabel, MAGIC_VNODE);
	COUNTER_INC(vnode_check_link);

	return (0);
}

COUNTER_DECL(vnode_check_listextattr);
static int
test_vnode_check_listextattr(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, int attrnamespace)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(vplabel, MAGIC_VNODE);
	COUNTER_INC(vnode_check_listextattr);

	return (0);
}

COUNTER_DECL(vnode_check_lookup);
static int
test_vnode_check_lookup(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel, struct componentname *cnp)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(dvplabel, MAGIC_VNODE);
	COUNTER_INC(vnode_check_lookup);

	return (0);
}

COUNTER_DECL(vnode_check_mmap);
static int
test_vnode_check_mmap(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, int prot, int flags)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(vplabel, MAGIC_VNODE);
	COUNTER_INC(vnode_check_mmap);

	return (0);
}

COUNTER_DECL(vnode_check_open);
static int
test_vnode_check_open(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, accmode_t accmode)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(vplabel, MAGIC_VNODE);
	COUNTER_INC(vnode_check_open);

	return (0);
}

COUNTER_DECL(vnode_check_poll);
static int
test_vnode_check_poll(struct ucred *active_cred, struct ucred *file_cred,
    struct vnode *vp, struct label *vplabel)
{

	LABEL_CHECK(active_cred->cr_label, MAGIC_CRED);
	if (file_cred != NULL)
		LABEL_CHECK(file_cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(vplabel, MAGIC_VNODE);
	COUNTER_INC(vnode_check_poll);

	return (0);
}

COUNTER_DECL(vnode_check_read);
static int
test_vnode_check_read(struct ucred *active_cred, struct ucred *file_cred,
    struct vnode *vp, struct label *vplabel)
{

	LABEL_CHECK(active_cred->cr_label, MAGIC_CRED);
	if (file_cred != NULL)
		LABEL_CHECK(file_cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(vplabel, MAGIC_VNODE);
	COUNTER_INC(vnode_check_read);

	return (0);
}

COUNTER_DECL(vnode_check_readdir);
static int
test_vnode_check_readdir(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(dvplabel, MAGIC_VNODE);
	COUNTER_INC(vnode_check_readdir);

	return (0);
}

COUNTER_DECL(vnode_check_readlink);
static int
test_vnode_check_readlink(struct ucred *cred, struct vnode *vp,
    struct label *vplabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(vplabel, MAGIC_VNODE);
	COUNTER_INC(vnode_check_readlink);

	return (0);
}

COUNTER_DECL(vnode_check_relabel);
static int
test_vnode_check_relabel(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, struct label *newlabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(vplabel, MAGIC_VNODE);
	LABEL_CHECK(newlabel, MAGIC_VNODE);
	COUNTER_INC(vnode_check_relabel);

	return (0);
}

COUNTER_DECL(vnode_check_rename_from);
static int
test_vnode_check_rename_from(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel, struct vnode *vp, struct label *vplabel,
    struct componentname *cnp)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(dvplabel, MAGIC_VNODE);
	LABEL_CHECK(vplabel, MAGIC_VNODE);
	COUNTER_INC(vnode_check_rename_from);

	return (0);
}

COUNTER_DECL(vnode_check_rename_to);
static int
test_vnode_check_rename_to(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel, struct vnode *vp, struct label *vplabel,
    int samedir, struct componentname *cnp)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(dvplabel, MAGIC_VNODE);
	LABEL_CHECK(vplabel, MAGIC_VNODE);
	COUNTER_INC(vnode_check_rename_to);

	return (0);
}

COUNTER_DECL(vnode_check_revoke);
static int
test_vnode_check_revoke(struct ucred *cred, struct vnode *vp,
    struct label *vplabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(vplabel, MAGIC_VNODE);
	COUNTER_INC(vnode_check_revoke);

	return (0);
}

COUNTER_DECL(vnode_check_setacl);
static int
test_vnode_check_setacl(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, acl_type_t type, struct acl *acl)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(vplabel, MAGIC_VNODE);
	COUNTER_INC(vnode_check_setacl);

	return (0);
}

COUNTER_DECL(vnode_check_setextattr);
static int
test_vnode_check_setextattr(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, int attrnamespace, const char *name)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(vplabel, MAGIC_VNODE);
	COUNTER_INC(vnode_check_setextattr);

	return (0);
}

COUNTER_DECL(vnode_check_setflags);
static int
test_vnode_check_setflags(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, u_long flags)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(vplabel, MAGIC_VNODE);
	COUNTER_INC(vnode_check_setflags);

	return (0);
}

COUNTER_DECL(vnode_check_setmode);
static int
test_vnode_check_setmode(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, mode_t mode)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(vplabel, MAGIC_VNODE);
	COUNTER_INC(vnode_check_setmode);

	return (0);
}

COUNTER_DECL(vnode_check_setowner);
static int
test_vnode_check_setowner(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, uid_t uid, gid_t gid)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(vplabel, MAGIC_VNODE);
	COUNTER_INC(vnode_check_setowner);

	return (0);
}

COUNTER_DECL(vnode_check_setutimes);
static int
test_vnode_check_setutimes(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, struct timespec atime, struct timespec mtime)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(vplabel, MAGIC_VNODE);
	COUNTER_INC(vnode_check_setutimes);

	return (0);
}

COUNTER_DECL(vnode_check_stat);
static int
test_vnode_check_stat(struct ucred *active_cred, struct ucred *file_cred,
    struct vnode *vp, struct label *vplabel)
{

	LABEL_CHECK(active_cred->cr_label, MAGIC_CRED);
	if (file_cred != NULL)
		LABEL_CHECK(file_cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(vplabel, MAGIC_VNODE);
	COUNTER_INC(vnode_check_stat);

	return (0);
}

COUNTER_DECL(vnode_check_unlink);
static int
test_vnode_check_unlink(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel, struct vnode *vp, struct label *vplabel,
    struct componentname *cnp)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(dvplabel, MAGIC_VNODE);
	LABEL_CHECK(vplabel, MAGIC_VNODE);
	COUNTER_INC(vnode_check_unlink);

	return (0);
}

COUNTER_DECL(vnode_check_write);
static int
test_vnode_check_write(struct ucred *active_cred,
    struct ucred *file_cred, struct vnode *vp, struct label *vplabel)
{

	LABEL_CHECK(active_cred->cr_label, MAGIC_CRED);
	if (file_cred != NULL)
		LABEL_CHECK(file_cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(vplabel, MAGIC_VNODE);
	COUNTER_INC(vnode_check_write);

	return (0);
}

COUNTER_DECL(vnode_copy_label);
static void
test_vnode_copy_label(struct label *src, struct label *dest)
{

	LABEL_CHECK(src, MAGIC_VNODE);
	LABEL_CHECK(dest, MAGIC_VNODE);
	COUNTER_INC(vnode_copy_label);
}

COUNTER_DECL(vnode_create_extattr);
static int
test_vnode_create_extattr(struct ucred *cred, struct mount *mp,
    struct label *mplabel, struct vnode *dvp, struct label *dvplabel,
    struct vnode *vp, struct label *vplabel, struct componentname *cnp)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(mplabel, MAGIC_MOUNT);
	LABEL_CHECK(dvplabel, MAGIC_VNODE);
	COUNTER_INC(vnode_create_extattr);

	return (0);
}

COUNTER_DECL(vnode_destroy_label);
static void
test_vnode_destroy_label(struct label *label)
{

	LABEL_DESTROY(label, MAGIC_VNODE);
	COUNTER_INC(vnode_destroy_label);
}

COUNTER_DECL(vnode_execve_transition);
static void
test_vnode_execve_transition(struct ucred *old, struct ucred *new,
    struct vnode *vp, struct label *filelabel,
    struct label *interpvplabel, struct image_params *imgp,
    struct label *execlabel)
{

	LABEL_CHECK(old->cr_label, MAGIC_CRED);
	LABEL_CHECK(new->cr_label, MAGIC_CRED);
	LABEL_CHECK(filelabel, MAGIC_VNODE);
	LABEL_CHECK(interpvplabel, MAGIC_VNODE);
	LABEL_CHECK(execlabel, MAGIC_CRED);
	COUNTER_INC(vnode_execve_transition);
}

COUNTER_DECL(vnode_execve_will_transition);
static int
test_vnode_execve_will_transition(struct ucred *old, struct vnode *vp,
    struct label *filelabel, struct label *interpvplabel,
    struct image_params *imgp, struct label *execlabel)
{

	LABEL_CHECK(old->cr_label, MAGIC_CRED);
	LABEL_CHECK(filelabel, MAGIC_VNODE);
	LABEL_CHECK(interpvplabel, MAGIC_VNODE);
	LABEL_CHECK(execlabel, MAGIC_CRED);
	COUNTER_INC(vnode_execve_will_transition);

	return (0);
}

COUNTER_DECL(vnode_externalize_label);
static int
test_vnode_externalize_label(struct label *label, char *element_name,
    struct sbuf *sb, int *claimed)
{

	LABEL_CHECK(label, MAGIC_VNODE);
	COUNTER_INC(vnode_externalize_label);

	return (0);
}

COUNTER_DECL(vnode_init_label);
static void
test_vnode_init_label(struct label *label)
{

	LABEL_INIT(label, MAGIC_VNODE);
	COUNTER_INC(vnode_init_label);
}

COUNTER_DECL(vnode_internalize_label);
static int
test_vnode_internalize_label(struct label *label, char *element_name,
    char *element_data, int *claimed)
{

	LABEL_CHECK(label, MAGIC_VNODE);
	COUNTER_INC(vnode_internalize_label);

	return (0);
}

COUNTER_DECL(vnode_relabel);
static void
test_vnode_relabel(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, struct label *label)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(vplabel, MAGIC_VNODE);
	LABEL_CHECK(label, MAGIC_VNODE);
	COUNTER_INC(vnode_relabel);
}

COUNTER_DECL(vnode_setlabel_extattr);
static int
test_vnode_setlabel_extattr(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, struct label *intlabel)
{

	LABEL_CHECK(cred->cr_label, MAGIC_CRED);
	LABEL_CHECK(vplabel, MAGIC_VNODE);
	LABEL_CHECK(intlabel, MAGIC_VNODE);
	COUNTER_INC(vnode_setlabel_extattr);

	return (0);
}

static struct mac_policy_ops test_ops =
{
	.mpo_bpfdesc_check_receive = test_bpfdesc_check_receive,
	.mpo_bpfdesc_create = test_bpfdesc_create,
	.mpo_bpfdesc_create_mbuf = test_bpfdesc_create_mbuf,
	.mpo_bpfdesc_destroy_label = test_bpfdesc_destroy_label,
	.mpo_bpfdesc_init_label = test_bpfdesc_init_label,

	.mpo_cred_check_relabel = test_cred_check_relabel,
	.mpo_cred_check_setaudit = test_cred_check_setaudit,
	.mpo_cred_check_setaudit_addr = test_cred_check_setaudit_addr,
	.mpo_cred_check_setauid = test_cred_check_setauid,
	.mpo_cred_check_seteuid = test_cred_check_seteuid,
	.mpo_cred_check_setegid = test_cred_check_setegid,
	.mpo_cred_check_setgid = test_cred_check_setgid,
	.mpo_cred_check_setgroups = test_cred_check_setgroups,
	.mpo_cred_check_setregid = test_cred_check_setregid,
	.mpo_cred_check_setresgid = test_cred_check_setresgid,
	.mpo_cred_check_setresuid = test_cred_check_setresuid,
	.mpo_cred_check_setreuid = test_cred_check_setreuid,
	.mpo_cred_check_setuid = test_cred_check_setuid,
	.mpo_cred_check_visible = test_cred_check_visible,
	.mpo_cred_copy_label = test_cred_copy_label,
	.mpo_cred_create_init = test_cred_create_init,
	.mpo_cred_create_swapper = test_cred_create_swapper,
	.mpo_cred_destroy_label = test_cred_destroy_label,
	.mpo_cred_externalize_label = test_cred_externalize_label,
	.mpo_cred_init_label = test_cred_init_label,
	.mpo_cred_internalize_label = test_cred_internalize_label,
	.mpo_cred_relabel = test_cred_relabel,

	.mpo_devfs_create_device = test_devfs_create_device,
	.mpo_devfs_create_directory = test_devfs_create_directory,
	.mpo_devfs_create_symlink = test_devfs_create_symlink,
	.mpo_devfs_destroy_label = test_devfs_destroy_label,
	.mpo_devfs_init_label = test_devfs_init_label,
	.mpo_devfs_update = test_devfs_update,
	.mpo_devfs_vnode_associate = test_devfs_vnode_associate,

	.mpo_ifnet_check_relabel = test_ifnet_check_relabel,
	.mpo_ifnet_check_transmit = test_ifnet_check_transmit,
	.mpo_ifnet_copy_label = test_ifnet_copy_label,
	.mpo_ifnet_create = test_ifnet_create,
	.mpo_ifnet_create_mbuf = test_ifnet_create_mbuf,
	.mpo_ifnet_destroy_label = test_ifnet_destroy_label,
	.mpo_ifnet_externalize_label = test_ifnet_externalize_label,
	.mpo_ifnet_init_label = test_ifnet_init_label,
	.mpo_ifnet_internalize_label = test_ifnet_internalize_label,
	.mpo_ifnet_relabel = test_ifnet_relabel,

	.mpo_syncache_destroy_label = test_syncache_destroy_label,
	.mpo_syncache_init_label = test_syncache_init_label,

	.mpo_sysvmsg_destroy_label = test_sysvmsg_destroy_label,
	.mpo_sysvmsg_init_label = test_sysvmsg_init_label,

	.mpo_sysvmsq_destroy_label = test_sysvmsq_destroy_label,
	.mpo_sysvmsq_init_label = test_sysvmsq_init_label,

	.mpo_sysvsem_destroy_label = test_sysvsem_destroy_label,
	.mpo_sysvsem_init_label = test_sysvsem_init_label,

	.mpo_sysvshm_destroy_label = test_sysvshm_destroy_label,
	.mpo_sysvshm_init_label = test_sysvshm_init_label,

	.mpo_inpcb_check_deliver = test_inpcb_check_deliver,
	.mpo_inpcb_check_visible = test_inpcb_check_visible,
	.mpo_inpcb_create = test_inpcb_create,
	.mpo_inpcb_create_mbuf = test_inpcb_create_mbuf,
	.mpo_inpcb_destroy_label = test_inpcb_destroy_label,
	.mpo_inpcb_init_label = test_inpcb_init_label,
	.mpo_inpcb_sosetlabel = test_inpcb_sosetlabel,

	.mpo_ip6q_create = test_ip6q_create,
	.mpo_ip6q_destroy_label = test_ip6q_destroy_label,
	.mpo_ip6q_init_label = test_ip6q_init_label,
	.mpo_ip6q_match = test_ip6q_match,
	.mpo_ip6q_reassemble = test_ip6q_reassemble,
	.mpo_ip6q_update = test_ip6q_update,

	.mpo_ipq_create = test_ipq_create,
	.mpo_ipq_destroy_label = test_ipq_destroy_label,
	.mpo_ipq_init_label = test_ipq_init_label,
	.mpo_ipq_match = test_ipq_match,
	.mpo_ipq_reassemble = test_ipq_reassemble,
	.mpo_ipq_update = test_ipq_update,

	.mpo_kenv_check_dump = test_kenv_check_dump,
	.mpo_kenv_check_get = test_kenv_check_get,
	.mpo_kenv_check_set = test_kenv_check_set,
	.mpo_kenv_check_unset = test_kenv_check_unset,

	.mpo_kld_check_load = test_kld_check_load,
	.mpo_kld_check_stat = test_kld_check_stat,

	.mpo_mbuf_copy_label = test_mbuf_copy_label,
	.mpo_mbuf_destroy_label = test_mbuf_destroy_label,
	.mpo_mbuf_init_label = test_mbuf_init_label,

	.mpo_mount_check_stat = test_mount_check_stat,
	.mpo_mount_create = test_mount_create,
	.mpo_mount_destroy_label = test_mount_destroy_label,
	.mpo_mount_init_label = test_mount_init_label,

	.mpo_netinet_arp_send = test_netinet_arp_send,
	.mpo_netinet_fragment = test_netinet_fragment,
	.mpo_netinet_icmp_reply = test_netinet_icmp_reply,
	.mpo_netinet_icmp_replyinplace = test_netinet_icmp_replyinplace,
	.mpo_netinet_igmp_send = test_netinet_igmp_send,
	.mpo_netinet_tcp_reply = test_netinet_tcp_reply,

	.mpo_netinet6_nd6_send = test_netinet6_nd6_send,

	.mpo_pipe_check_ioctl = test_pipe_check_ioctl,
	.mpo_pipe_check_poll = test_pipe_check_poll,
	.mpo_pipe_check_read = test_pipe_check_read,
	.mpo_pipe_check_relabel = test_pipe_check_relabel,
	.mpo_pipe_check_stat = test_pipe_check_stat,
	.mpo_pipe_check_write = test_pipe_check_write,
	.mpo_pipe_copy_label = test_pipe_copy_label,
	.mpo_pipe_create = test_pipe_create,
	.mpo_pipe_destroy_label = test_pipe_destroy_label,
	.mpo_pipe_externalize_label = test_pipe_externalize_label,
	.mpo_pipe_init_label = test_pipe_init_label,
	.mpo_pipe_internalize_label = test_pipe_internalize_label,
	.mpo_pipe_relabel = test_pipe_relabel,

	.mpo_posixsem_check_getvalue = test_posixsem_check_getvalue,
	.mpo_posixsem_check_open = test_posixsem_check_open,
	.mpo_posixsem_check_post = test_posixsem_check_post,
	.mpo_posixsem_check_setmode = test_posixsem_check_setmode,
	.mpo_posixsem_check_setowner = test_posixsem_check_setowner,
	.mpo_posixsem_check_stat = test_posixsem_check_stat,
	.mpo_posixsem_check_unlink = test_posixsem_check_unlink,
	.mpo_posixsem_check_wait = test_posixsem_check_wait,
	.mpo_posixsem_create = test_posixsem_create,
	.mpo_posixsem_destroy_label = test_posixsem_destroy_label,
	.mpo_posixsem_init_label = test_posixsem_init_label,

	.mpo_posixshm_check_create = test_posixshm_check_create,
	.mpo_posixshm_check_mmap = test_posixshm_check_mmap,
	.mpo_posixshm_check_open = test_posixshm_check_open,
	.mpo_posixshm_check_read = test_posixshm_check_read,
	.mpo_posixshm_check_setmode = test_posixshm_check_setmode,
	.mpo_posixshm_check_setowner = test_posixshm_check_setowner,
	.mpo_posixshm_check_stat = test_posixshm_check_stat,
	.mpo_posixshm_check_truncate = test_posixshm_check_truncate,
	.mpo_posixshm_check_unlink = test_posixshm_check_unlink,
	.mpo_posixshm_check_write = test_posixshm_check_write,
	.mpo_posixshm_create = test_posixshm_create,
	.mpo_posixshm_destroy_label = test_posixshm_destroy_label,
	.mpo_posixshm_init_label = test_posixshm_init_label,

	.mpo_proc_check_debug = test_proc_check_debug,
	.mpo_proc_check_sched = test_proc_check_sched,
	.mpo_proc_check_signal = test_proc_check_signal,
	.mpo_proc_check_wait = test_proc_check_wait,
	.mpo_proc_destroy_label = test_proc_destroy_label,
	.mpo_proc_init_label = test_proc_init_label,

	.mpo_socket_check_accept = test_socket_check_accept,
	.mpo_socket_check_bind = test_socket_check_bind,
	.mpo_socket_check_connect = test_socket_check_connect,
	.mpo_socket_check_deliver = test_socket_check_deliver,
	.mpo_socket_check_listen = test_socket_check_listen,
	.mpo_socket_check_poll = test_socket_check_poll,
	.mpo_socket_check_receive = test_socket_check_receive,
	.mpo_socket_check_relabel = test_socket_check_relabel,
	.mpo_socket_check_send = test_socket_check_send,
	.mpo_socket_check_stat = test_socket_check_stat,
	.mpo_socket_check_visible = test_socket_check_visible,
	.mpo_socket_copy_label = test_socket_copy_label,
	.mpo_socket_create = test_socket_create,
	.mpo_socket_create_mbuf = test_socket_create_mbuf,
	.mpo_socket_destroy_label = test_socket_destroy_label,
	.mpo_socket_externalize_label = test_socket_externalize_label,
	.mpo_socket_init_label = test_socket_init_label,
	.mpo_socket_internalize_label = test_socket_internalize_label,
	.mpo_socket_newconn = test_socket_newconn,
	.mpo_socket_relabel = test_socket_relabel,

	.mpo_socketpeer_destroy_label = test_socketpeer_destroy_label,
	.mpo_socketpeer_externalize_label = test_socketpeer_externalize_label,
	.mpo_socketpeer_init_label = test_socketpeer_init_label,
	.mpo_socketpeer_set_from_mbuf = test_socketpeer_set_from_mbuf,
	.mpo_socketpeer_set_from_socket = test_socketpeer_set_from_socket,

	.mpo_syncache_create = test_syncache_create,
	.mpo_syncache_create_mbuf = test_syncache_create_mbuf,

	.mpo_system_check_acct = test_system_check_acct,
	.mpo_system_check_audit = test_system_check_audit,
	.mpo_system_check_auditctl = test_system_check_auditctl,
	.mpo_system_check_auditon = test_system_check_auditon,
	.mpo_system_check_reboot = test_system_check_reboot,
	.mpo_system_check_swapoff = test_system_check_swapoff,
	.mpo_system_check_swapon = test_system_check_swapon,
	.mpo_system_check_sysctl = test_system_check_sysctl,

	.mpo_vnode_check_access = test_vnode_check_access,
	.mpo_sysvmsg_cleanup = test_sysvmsg_cleanup,
	.mpo_sysvmsg_create = test_sysvmsg_create,

	.mpo_sysvmsq_check_msgmsq = test_sysvmsq_check_msgmsq,
	.mpo_sysvmsq_check_msgrcv = test_sysvmsq_check_msgrcv,
	.mpo_sysvmsq_check_msgrmid = test_sysvmsq_check_msgrmid,
	.mpo_sysvmsq_check_msqget = test_sysvmsq_check_msqget,
	.mpo_sysvmsq_check_msqsnd = test_sysvmsq_check_msqsnd,
	.mpo_sysvmsq_check_msqrcv = test_sysvmsq_check_msqrcv,
	.mpo_sysvmsq_check_msqctl = test_sysvmsq_check_msqctl,
	.mpo_sysvmsq_cleanup = test_sysvmsq_cleanup,
	.mpo_sysvmsq_create = test_sysvmsq_create,

	.mpo_sysvsem_check_semctl = test_sysvsem_check_semctl,
	.mpo_sysvsem_check_semget = test_sysvsem_check_semget,
	.mpo_sysvsem_check_semop = test_sysvsem_check_semop,
	.mpo_sysvsem_cleanup = test_sysvsem_cleanup,
	.mpo_sysvsem_create = test_sysvsem_create,

	.mpo_sysvshm_check_shmat = test_sysvshm_check_shmat,
	.mpo_sysvshm_check_shmctl = test_sysvshm_check_shmctl,
	.mpo_sysvshm_check_shmdt = test_sysvshm_check_shmdt,
	.mpo_sysvshm_check_shmget = test_sysvshm_check_shmget,
	.mpo_sysvshm_cleanup = test_sysvshm_cleanup,
	.mpo_sysvshm_create = test_sysvshm_create,

	.mpo_thread_userret = test_thread_userret,

	.mpo_vnode_associate_extattr = test_vnode_associate_extattr,
	.mpo_vnode_associate_singlelabel = test_vnode_associate_singlelabel,
	.mpo_vnode_check_chdir = test_vnode_check_chdir,
	.mpo_vnode_check_chroot = test_vnode_check_chroot,
	.mpo_vnode_check_create = test_vnode_check_create,
	.mpo_vnode_check_deleteacl = test_vnode_check_deleteacl,
	.mpo_vnode_check_deleteextattr = test_vnode_check_deleteextattr,
	.mpo_vnode_check_exec = test_vnode_check_exec,
	.mpo_vnode_check_getacl = test_vnode_check_getacl,
	.mpo_vnode_check_getextattr = test_vnode_check_getextattr,
	.mpo_vnode_check_link = test_vnode_check_link,
	.mpo_vnode_check_listextattr = test_vnode_check_listextattr,
	.mpo_vnode_check_lookup = test_vnode_check_lookup,
	.mpo_vnode_check_mmap = test_vnode_check_mmap,
	.mpo_vnode_check_open = test_vnode_check_open,
	.mpo_vnode_check_poll = test_vnode_check_poll,
	.mpo_vnode_check_read = test_vnode_check_read,
	.mpo_vnode_check_readdir = test_vnode_check_readdir,
	.mpo_vnode_check_readlink = test_vnode_check_readlink,
	.mpo_vnode_check_relabel = test_vnode_check_relabel,
	.mpo_vnode_check_rename_from = test_vnode_check_rename_from,
	.mpo_vnode_check_rename_to = test_vnode_check_rename_to,
	.mpo_vnode_check_revoke = test_vnode_check_revoke,
	.mpo_vnode_check_setacl = test_vnode_check_setacl,
	.mpo_vnode_check_setextattr = test_vnode_check_setextattr,
	.mpo_vnode_check_setflags = test_vnode_check_setflags,
	.mpo_vnode_check_setmode = test_vnode_check_setmode,
	.mpo_vnode_check_setowner = test_vnode_check_setowner,
	.mpo_vnode_check_setutimes = test_vnode_check_setutimes,
	.mpo_vnode_check_stat = test_vnode_check_stat,
	.mpo_vnode_check_unlink = test_vnode_check_unlink,
	.mpo_vnode_check_write = test_vnode_check_write,
	.mpo_vnode_copy_label = test_vnode_copy_label,
	.mpo_vnode_create_extattr = test_vnode_create_extattr,
	.mpo_vnode_destroy_label = test_vnode_destroy_label,
	.mpo_vnode_execve_transition = test_vnode_execve_transition,
	.mpo_vnode_execve_will_transition = test_vnode_execve_will_transition,
	.mpo_vnode_externalize_label = test_vnode_externalize_label,
	.mpo_vnode_init_label = test_vnode_init_label,
	.mpo_vnode_internalize_label = test_vnode_internalize_label,
	.mpo_vnode_relabel = test_vnode_relabel,
	.mpo_vnode_setlabel_extattr = test_vnode_setlabel_extattr,
};

MAC_POLICY_SET(&test_ops, mac_test, "TrustedBSD MAC/Test",
    MPC_LOADTIME_FLAG_UNLOADOK, &test_slot);
