/*-
 * Copyright (c) 1999-2002, 2007-2011 Robert N. M. Watson
 * Copyright (c) 2001-2005 McAfee, Inc.
 * Copyright (c) 2005-2006 SPARTA, Inc.
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
 * Stub module that implements a NOOP for most (if not all) MAC Framework
 * policy entry points.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/acl.h>
#include <sys/conf.h>
#include <sys/extattr.h>
#include <sys/kernel.h>
#include <sys/ksem.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/sysent.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/pipe.h>
#include <sys/sx.h>
#include <sys/sysctl.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>

#include <fs/devfs/devfs.h>

#include <net/bpfdesc.h>
#include <net/if.h>
#include <net/if_types.h>
#include <net/if_var.h>

#include <netinet/in.h>
#include <netinet/in_pcb.h>
#include <netinet/ip_var.h>

#include <vm/vm.h>

#include <security/mac/mac_policy.h>

SYSCTL_DECL(_security_mac);

static SYSCTL_NODE(_security_mac, OID_AUTO, stub, CTLFLAG_RW, 0,
    "TrustedBSD mac_stub policy controls");

static int	stub_enabled = 1;
SYSCTL_INT(_security_mac_stub, OID_AUTO, enabled, CTLFLAG_RW,
    &stub_enabled, 0, "Enforce mac_stub policy");

/*
 * Policy module operations.
 */
static void
stub_destroy(struct mac_policy_conf *conf)
{

}

static void
stub_init(struct mac_policy_conf *conf)
{

}

static int
stub_syscall(struct thread *td, int call, void *arg)
{

	return (0);
}

/*
 * Label operations.
 */
static void
stub_init_label(struct label *label)
{

}

static int
stub_init_label_waitcheck(struct label *label, int flag)
{

	return (0);
}

static void
stub_destroy_label(struct label *label)
{

}

static void
stub_copy_label(struct label *src, struct label *dest)
{

}

static int
stub_externalize_label(struct label *label, char *element_name,
    struct sbuf *sb, int *claimed)
{

	return (0);
}

static int
stub_internalize_label(struct label *label, char *element_name,
    char *element_data, int *claimed)
{

	return (0);
}

/*
 * Object-specific entry point imeplementations are sorted alphabetically by
 * object type name and then by operation.
 */
static int
stub_bpfdesc_check_receive(struct bpf_d *d, struct label *dlabel,
    struct ifnet *ifp, struct label *ifplabel)
{

        return (0);
}

static void
stub_bpfdesc_create(struct ucred *cred, struct bpf_d *d,
    struct label *dlabel)
{

}

static void
stub_bpfdesc_create_mbuf(struct bpf_d *d, struct label *dlabel,
    struct mbuf *m, struct label *mlabel)
{

}

static void
stub_cred_associate_nfsd(struct ucred *cred)
{

}

static int
stub_cred_check_relabel(struct ucred *cred, struct label *newlabel)
{

	return (0);
}

static int
stub_cred_check_setaudit(struct ucred *cred, struct auditinfo *ai)
{

	return (0);
}

static int
stub_cred_check_setaudit_addr(struct ucred *cred, struct auditinfo_addr *aia)
{

	return (0);
}

static int
stub_cred_check_setauid(struct ucred *cred, uid_t auid)
{

	return (0);
}

static int
stub_cred_check_setegid(struct ucred *cred, gid_t egid)
{

	return (0);
}

static int
stub_cred_check_seteuid(struct ucred *cred, uid_t euid)
{

	return (0);
}

static int
stub_cred_check_setgid(struct ucred *cred, gid_t gid)
{

	return (0);
}

static int
stub_cred_check_setgroups(struct ucred *cred, int ngroups,
	gid_t *gidset)
{

	return (0);
}

static int
stub_cred_check_setregid(struct ucred *cred, gid_t rgid, gid_t egid)
{

	return (0);
}

static int
stub_cred_check_setresgid(struct ucred *cred, gid_t rgid, gid_t egid,
	gid_t sgid)
{

	return (0);
}

static int
stub_cred_check_setresuid(struct ucred *cred, uid_t ruid, uid_t euid,
	uid_t suid)
{

	return (0);
}

static int
stub_cred_check_setreuid(struct ucred *cred, uid_t ruid, uid_t euid)
{

	return (0);
}

static int
stub_cred_check_setuid(struct ucred *cred, uid_t uid)
{

	return (0);
}

static int
stub_cred_check_visible(struct ucred *cr1, struct ucred *cr2)
{

	return (0);
}

static void
stub_cred_create_init(struct ucred *cred)
{

}

static void
stub_cred_create_swapper(struct ucred *cred)
{

}

static void
stub_cred_relabel(struct ucred *cred, struct label *newlabel)
{

}

static void
stub_devfs_create_device(struct ucred *cred, struct mount *mp,
    struct cdev *dev, struct devfs_dirent *de, struct label *delabel)
{

}

static void
stub_devfs_create_directory(struct mount *mp, char *dirname,
    int dirnamelen, struct devfs_dirent *de, struct label *delabel)
{

}

static void
stub_devfs_create_symlink(struct ucred *cred, struct mount *mp,
    struct devfs_dirent *dd, struct label *ddlabel, struct devfs_dirent *de,
    struct label *delabel)
{

}

static void
stub_devfs_update(struct mount *mp, struct devfs_dirent *de,
    struct label *delabel, struct vnode *vp, struct label *vplabel)
{

}

static void
stub_devfs_vnode_associate(struct mount *mp, struct label *mplabel,
    struct devfs_dirent *de, struct label *delabel, struct vnode *vp,
    struct label *vplabel)
{

}

static int
stub_ifnet_check_relabel(struct ucred *cred, struct ifnet *ifp,
    struct label *ifplabel, struct label *newlabel)
{

	return (0);
}

static int
stub_ifnet_check_transmit(struct ifnet *ifp, struct label *ifplabel,
    struct mbuf *m, struct label *mlabel)
{

	return (0);
}

static void
stub_ifnet_create(struct ifnet *ifp, struct label *ifplabel)
{

}

static void
stub_ifnet_create_mbuf(struct ifnet *ifp, struct label *ifplabel,
    struct mbuf *m, struct label *mlabel)
{

}

static void
stub_ifnet_relabel(struct ucred *cred, struct ifnet *ifp,
    struct label *ifplabel, struct label *newlabel)
{

}

static int
stub_inpcb_check_deliver(struct inpcb *inp, struct label *inplabel,
    struct mbuf *m, struct label *mlabel)
{

	return (0);
}

static void
stub_inpcb_create(struct socket *so, struct label *solabel,
    struct inpcb *inp, struct label *inplabel)
{

}

static void
stub_inpcb_create_mbuf(struct inpcb *inp, struct label *inplabel,
    struct mbuf *m, struct label *mlabel)
{

}

static void
stub_inpcb_sosetlabel(struct socket *so, struct label *solabel,
    struct inpcb *inp, struct label *inplabel)
{

	SOCK_LOCK_ASSERT(so);

}

static void
stub_ip6q_create(struct mbuf *m, struct label *mlabel, struct ip6q *q6,
    struct label *q6label)
{

}

static int
stub_ip6q_match(struct mbuf *m, struct label *mlabel, struct ip6q *q6,
    struct label *q6label)
{

	return (1);
}

static void
stub_ip6q_reassemble(struct ip6q *q6, struct label *q6label, struct mbuf *m,
    struct label *mlabel)
{

}

static void
stub_ip6q_update(struct mbuf *m, struct label *mlabel, struct ip6q *q6,
    struct label *q6label)
{

}

static void
stub_ipq_create(struct mbuf *m, struct label *mlabel, struct ipq *q,
    struct label *qlabel)
{

}

static int
stub_ipq_match(struct mbuf *m, struct label *mlabel, struct ipq *q,
    struct label *qlabel)
{

	return (1);
}

static void
stub_ipq_reassemble(struct ipq *q, struct label *qlabel, struct mbuf *m,
    struct label *mlabel)
{

}

static void
stub_ipq_update(struct mbuf *m, struct label *mlabel, struct ipq *q,
    struct label *qlabel)
{

}

static int
stub_kenv_check_dump(struct ucred *cred)
{

	return (0);
}

static int
stub_kenv_check_get(struct ucred *cred, char *name)
{

	return (0);
}

static int
stub_kenv_check_set(struct ucred *cred, char *name, char *value)
{

	return (0);
}

static int
stub_kenv_check_unset(struct ucred *cred, char *name)
{

	return (0);
}

static int
stub_kld_check_load(struct ucred *cred, struct vnode *vp,
    struct label *vplabel)
{

	return (0);
}

static int
stub_kld_check_stat(struct ucred *cred)
{

	return (0);
}

static int
stub_mount_check_stat(struct ucred *cred, struct mount *mp,
    struct label *mplabel)
{

	return (0);
}

static void
stub_mount_create(struct ucred *cred, struct mount *mp,
    struct label *mplabel)
{

}

static void
stub_netinet_arp_send(struct ifnet *ifp, struct label *iflpabel,
    struct mbuf *m, struct label *mlabel)
{

}

static void
stub_netinet_firewall_reply(struct mbuf *mrecv, struct label *mrecvlabel,
    struct mbuf *msend, struct label *msendlabel)
{

}

static void
stub_netinet_firewall_send(struct mbuf *m, struct label *mlabel)
{

}

static void
stub_netinet_fragment(struct mbuf *m, struct label *mlabel, struct mbuf *frag,
    struct label *fraglabel)
{

}

static void
stub_netinet_icmp_reply(struct mbuf *mrecv, struct label *mrecvlabel,
    struct mbuf *msend, struct label *msendlabel)
{

}

static void
stub_netinet_icmp_replyinplace(struct mbuf *m, struct label *mlabel)
{

}

static void
stub_netinet_igmp_send(struct ifnet *ifp, struct label *iflpabel,
    struct mbuf *m, struct label *mlabel)
{

}

static void
stub_netinet_tcp_reply(struct mbuf *m, struct label *mlabel)
{

}

static void
stub_netinet6_nd6_send(struct ifnet *ifp, struct label *iflpabel,
    struct mbuf *m, struct label *mlabel)
{

}

static int
stub_pipe_check_ioctl(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel, unsigned long cmd, void /* caddr_t */ *data)
{

	return (0);
}

static int
stub_pipe_check_poll(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel)
{

	return (0);
}

static int
stub_pipe_check_read(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel)
{

	return (0);
}

static int
stub_pipe_check_relabel(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel, struct label *newlabel)
{

	return (0);
}

static int
stub_pipe_check_stat(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel)
{

	return (0);
}

static int
stub_pipe_check_write(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel)
{

	return (0);
}

static void
stub_pipe_create(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel)
{

}

static void
stub_pipe_relabel(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel, struct label *newlabel)
{

}

static int
stub_posixsem_check_getvalue(struct ucred *active_cred, struct ucred *file_cred,
    struct ksem *ks, struct label *kslabel)
{

	return (0);
}

static int
stub_posixsem_check_open(struct ucred *cred, struct ksem *ks,
    struct label *kslabel)
{

	return (0);
}

static int
stub_posixsem_check_post(struct ucred *active_cred, struct ucred *file_cred,
    struct ksem *ks, struct label *kslabel)
{

	return (0);
}

static int
stub_posixsem_check_setmode(struct ucred *cred, struct ksem *ks,
    struct label *kslabel, mode_t mode)
{

	return (0);
}

static int
stub_posixsem_check_setowner(struct ucred *cred, struct ksem *ks,
    struct label *kslabel, uid_t uid, gid_t gid)
{

	return (0);
}

static int
stub_posixsem_check_stat(struct ucred *active_cred, struct ucred *file_cred,
    struct ksem *ks, struct label *kslabel)
{

	return (0);
}

static int
stub_posixsem_check_unlink(struct ucred *cred, struct ksem *ks,
    struct label *kslabel)
{

	return (0);
}

static int
stub_posixsem_check_wait(struct ucred *active_cred, struct ucred *file_cred,
    struct ksem *ks, struct label *kslabel)
{

	return (0);
}

static void
stub_posixsem_create(struct ucred *cred, struct ksem *ks,
    struct label *kslabel)
{

}

static int
stub_posixshm_check_create(struct ucred *cred, const char *path)
{

	return (0);
}

static int
stub_posixshm_check_mmap(struct ucred *cred, struct shmfd *shmfd,
    struct label *shmlabel, int prot, int flags)
{

	return (0);
}

static int
stub_posixshm_check_open(struct ucred *cred, struct shmfd *shmfd,
    struct label *shmlabel, accmode_t accmode)
{

	return (0);
}

static int
stub_posixshm_check_read(struct ucred *active_cred, struct ucred *file_cred,
    struct shmfd *shm, struct label *shmlabel)
{

	return (0);
}

static int
stub_posixshm_check_setmode(struct ucred *cred, struct shmfd *shmfd,
    struct label *shmlabel, mode_t mode)
{

	return (0);
}

static int
stub_posixshm_check_setowner(struct ucred *cred, struct shmfd *shmfd,
    struct label *shmlabel, uid_t uid, gid_t gid)
{

	return (0);
}

static int
stub_posixshm_check_stat(struct ucred *active_cred, struct ucred *file_cred,
    struct shmfd *shmfd, struct label *shmlabel)
{

	return (0);
}

static int
stub_posixshm_check_truncate(struct ucred *active_cred,
    struct ucred *file_cred, struct shmfd *shmfd, struct label *shmlabel)
{

	return (0);
}

static int
stub_posixshm_check_unlink(struct ucred *cred, struct shmfd *shmfd,
    struct label *shmlabel)
{

	return (0);
}

static int
stub_posixshm_check_write(struct ucred *active_cred, struct ucred *file_cred,
    struct shmfd *shm, struct label *shmlabel)
{

	return (0);
}

static void
stub_posixshm_create(struct ucred *cred, struct shmfd *shmfd,
    struct label *shmlabel)
{

}

static int
stub_priv_check(struct ucred *cred, int priv)
{

	return (0);
}

static int
stub_priv_grant(struct ucred *cred, int priv)
{

	return (EPERM);
}

static int
stub_proc_check_debug(struct ucred *cred, struct proc *p)
{

	return (0);
}

static int
stub_proc_check_sched(struct ucred *cred, struct proc *p)
{

	return (0);
}

static int
stub_proc_check_signal(struct ucred *cred, struct proc *p, int signum)
{

	return (0);
}

static int
stub_proc_check_wait(struct ucred *cred, struct proc *p)
{

	return (0);
}

static int
stub_socket_check_accept(struct ucred *cred, struct socket *so,
    struct label *solabel)
{

#if 0
	SOCK_LOCK(so);
	SOCK_UNLOCK(so);
#endif

	return (0);
}

static int
stub_socket_check_bind(struct ucred *cred, struct socket *so,
    struct label *solabel, struct sockaddr *sa)
{

#if 0
	SOCK_LOCK(so);
	SOCK_UNLOCK(so);
#endif

	return (0);
}

static int
stub_socket_check_connect(struct ucred *cred, struct socket *so,
    struct label *solabel, struct sockaddr *sa)
{

#if 0
	SOCK_LOCK(so);
	SOCK_UNLOCK(so);
#endif

	return (0);
}

static int
stub_socket_check_create(struct ucred *cred, int domain, int type, int proto)
{

	return (0);
}

static int
stub_socket_check_deliver(struct socket *so, struct label *solabel,
    struct mbuf *m, struct label *mlabel)
{

#if 0
	SOCK_LOCK(so);
	SOCK_UNLOCK(so);
#endif

	return (0);
}

static int
stub_socket_check_listen(struct ucred *cred, struct socket *so,
    struct label *solabel)
{

#if 0
	SOCK_LOCK(so);
	SOCK_UNLOCK(so);
#endif

	return (0);
}

static int
stub_socket_check_poll(struct ucred *cred, struct socket *so,
    struct label *solabel)
{

#if 0
	SOCK_LOCK(so);
	SOCK_UNLOCK(so);
#endif

	return (0);
}

static int
stub_socket_check_receive(struct ucred *cred, struct socket *so,
    struct label *solabel)
{

#if 0
	SOCK_LOCK(so);
	SOCK_UNLOCK(so);
#endif

	return (0);
}

static int
stub_socket_check_relabel(struct ucred *cred, struct socket *so,
    struct label *solabel, struct label *newlabel)
{

	SOCK_LOCK_ASSERT(so);

	return (0);
}
static int
stub_socket_check_send(struct ucred *cred, struct socket *so,
    struct label *solabel)
{

#if 0
	SOCK_LOCK(so);
	SOCK_UNLOCK(so);
#endif

	return (0);
}

static int
stub_socket_check_stat(struct ucred *cred, struct socket *so,
    struct label *solabel)
{

#if 0
	SOCK_LOCK(so);
	SOCK_UNLOCK(so);
#endif

	return (0);
}

static int
stub_inpcb_check_visible(struct ucred *cred, struct inpcb *inp,
   struct label *inplabel)
{

	return (0);
}

static int
stub_socket_check_visible(struct ucred *cred, struct socket *so,
   struct label *solabel)
{

#if 0
	SOCK_LOCK(so);
	SOCK_UNLOCK(so);
#endif

	return (0);
}

static void
stub_socket_create(struct ucred *cred, struct socket *so,
    struct label *solabel)
{

}

static void
stub_socket_create_mbuf(struct socket *so, struct label *solabel,
    struct mbuf *m, struct label *mlabel)
{

#if 0
	SOCK_LOCK(so);
	SOCK_UNLOCK(so);
#endif
}

static void
stub_socket_newconn(struct socket *oldso, struct label *oldsolabel,
    struct socket *newso, struct label *newsolabel)
{

#if 0
	SOCK_LOCK(oldso);
	SOCK_UNLOCK(oldso);
#endif
#if 0
	SOCK_LOCK(newso);
	SOCK_UNLOCK(newso);
#endif
}

static void
stub_socket_relabel(struct ucred *cred, struct socket *so,
    struct label *solabel, struct label *newlabel)
{

	SOCK_LOCK_ASSERT(so);
}

static void
stub_socketpeer_set_from_mbuf(struct mbuf *m, struct label *mlabel,
    struct socket *so, struct label *sopeerlabel)
{

#if 0
	SOCK_LOCK(so);
	SOCK_UNLOCK(so);
#endif
}

static void
stub_socketpeer_set_from_socket(struct socket *oldso,
    struct label *oldsolabel, struct socket *newso,
    struct label *newsopeerlabel)
{

#if 0
	SOCK_LOCK(oldso);
	SOCK_UNLOCK(oldso);
#endif
#if 0
	SOCK_LOCK(newso);
	SOCK_UNLOCK(newso);
#endif
}

static void
stub_syncache_create(struct label *label, struct inpcb *inp)
{

}

static void
stub_syncache_create_mbuf(struct label *sc_label, struct mbuf *m,
    struct label *mlabel)
{

}

static int
stub_system_check_acct(struct ucred *cred, struct vnode *vp,
    struct label *vplabel)
{

	return (0);
}

static int
stub_system_check_audit(struct ucred *cred, void *record, int length)
{

	return (0);
}

static int
stub_system_check_auditctl(struct ucred *cred, struct vnode *vp,
    struct label *vplabel)
{

	return (0);
}

static int
stub_system_check_auditon(struct ucred *cred, int cmd)
{

	return (0);
}

static int
stub_system_check_reboot(struct ucred *cred, int how)
{

	return (0);
}

static int
stub_system_check_swapoff(struct ucred *cred, struct vnode *vp,
    struct label *vplabel)
{

	return (0);
}

static int
stub_system_check_swapon(struct ucred *cred, struct vnode *vp,
    struct label *vplabel)
{

	return (0);
}

static int
stub_system_check_sysctl(struct ucred *cred, struct sysctl_oid *oidp,
    void *arg1, int arg2, struct sysctl_req *req)
{

	return (0);
}

static void
stub_sysvmsg_cleanup(struct label *msglabel)
{

}

static void
stub_sysvmsg_create(struct ucred *cred, struct msqid_kernel *msqkptr,
    struct label *msqlabel, struct msg *msgptr, struct label *msglabel)
{

}

static int
stub_sysvmsq_check_msgmsq(struct ucred *cred, struct msg *msgptr,
    struct label *msglabel, struct msqid_kernel *msqkptr,
    struct label *msqklabel)
{

	return (0);
}

static int
stub_sysvmsq_check_msgrcv(struct ucred *cred, struct msg *msgptr,
    struct label *msglabel)
{

	return (0);
}


static int
stub_sysvmsq_check_msgrmid(struct ucred *cred, struct msg *msgptr,
    struct label *msglabel)
{

	return (0);
}


static int
stub_sysvmsq_check_msqget(struct ucred *cred, struct msqid_kernel *msqkptr,
    struct label *msqklabel)
{

	return (0);
}


static int
stub_sysvmsq_check_msqsnd(struct ucred *cred, struct msqid_kernel *msqkptr,
    struct label *msqklabel)
{

	return (0);
}

static int
stub_sysvmsq_check_msqrcv(struct ucred *cred, struct msqid_kernel *msqkptr,
    struct label *msqklabel)
{

	return (0);
}


static int
stub_sysvmsq_check_msqctl(struct ucred *cred, struct msqid_kernel *msqkptr,
    struct label *msqklabel, int cmd)
{

	return (0);
}


static void
stub_sysvmsq_cleanup(struct label *msqlabel)
{

}

static void
stub_sysvmsq_create(struct ucred *cred, struct msqid_kernel *msqkptr,
    struct label *msqlabel)
{

}

static int
stub_sysvsem_check_semctl(struct ucred *cred, struct semid_kernel *semakptr,
    struct label *semaklabel, int cmd)
{

	return (0);
}

static int
stub_sysvsem_check_semget(struct ucred *cred, struct semid_kernel *semakptr,
    struct label *semaklabel)
{

	return (0);
}


static int
stub_sysvsem_check_semop(struct ucred *cred, struct semid_kernel *semakptr,
    struct label *semaklabel, size_t accesstype)
{

	return (0);
}

static void
stub_sysvsem_cleanup(struct label *semalabel)
{

}

static void
stub_sysvsem_create(struct ucred *cred, struct semid_kernel *semakptr,
    struct label *semalabel)
{

}

static int
stub_sysvshm_check_shmat(struct ucred *cred, struct shmid_kernel *shmsegptr,
    struct label *shmseglabel, int shmflg)
{

	return (0);
}

static int
stub_sysvshm_check_shmctl(struct ucred *cred, struct shmid_kernel *shmsegptr,
    struct label *shmseglabel, int cmd)
{

	return (0);
}

static int
stub_sysvshm_check_shmdt(struct ucred *cred, struct shmid_kernel *shmsegptr,
    struct label *shmseglabel)
{

	return (0);
}


static int
stub_sysvshm_check_shmget(struct ucred *cred, struct shmid_kernel *shmsegptr,
    struct label *shmseglabel, int shmflg)
{

	return (0);
}

static void
stub_sysvshm_cleanup(struct label *shmlabel)
{

}

static void
stub_sysvshm_create(struct ucred *cred, struct shmid_kernel *shmsegptr,
    struct label *shmalabel)
{

}

static void
stub_thread_userret(struct thread *td)
{

}

static int
stub_vnode_associate_extattr(struct mount *mp, struct label *mplabel,
    struct vnode *vp, struct label *vplabel)
{

	return (0);
}

static void
stub_vnode_associate_singlelabel(struct mount *mp, struct label *mplabel,
    struct vnode *vp, struct label *vplabel)
{

}

static int
stub_vnode_check_access(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, accmode_t accmode)
{

	return (0);
}

static int
stub_vnode_check_chdir(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel)
{

	return (0);
}

static int
stub_vnode_check_chroot(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel)
{

	return (0);
}

static int
stub_vnode_check_create(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel, struct componentname *cnp, struct vattr *vap)
{

	return (0);
}

static int
stub_vnode_check_deleteacl(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, acl_type_t type)
{

	return (0);
}

static int
stub_vnode_check_deleteextattr(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, int attrnamespace, const char *name)
{

	return (0);
}

static int
stub_vnode_check_exec(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, struct image_params *imgp,
    struct label *execlabel)
{

	return (0);
}

static int
stub_vnode_check_getacl(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, acl_type_t type)
{

	return (0);
}

static int
stub_vnode_check_getextattr(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, int attrnamespace, const char *name)
{

	return (0);
}

static int
stub_vnode_check_link(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel, struct vnode *vp, struct label *vplabel,
    struct componentname *cnp)
{

	return (0);
}

static int
stub_vnode_check_listextattr(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, int attrnamespace)
{

	return (0);
}

static int
stub_vnode_check_lookup(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel, struct componentname *cnp)
{

	return (0);
}

static int
stub_vnode_check_mmap(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, int prot, int flags)
{

	return (0);
}

static void
stub_vnode_check_mmap_downgrade(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, int *prot)
{

}

static int
stub_vnode_check_mprotect(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, int prot)
{

	return (0);
}

static int
stub_vnode_check_open(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, accmode_t accmode)
{

	return (0);
}

static int
stub_vnode_check_poll(struct ucred *active_cred, struct ucred *file_cred,
    struct vnode *vp, struct label *vplabel)
{

	return (0);
}

static int
stub_vnode_check_read(struct ucred *active_cred, struct ucred *file_cred,
    struct vnode *vp, struct label *vplabel)
{

	return (0);
}

static int
stub_vnode_check_readdir(struct ucred *cred, struct vnode *vp,
    struct label *dvplabel)
{

	return (0);
}

static int
stub_vnode_check_readlink(struct ucred *cred, struct vnode *vp,
    struct label *vplabel)
{

	return (0);
}

static int
stub_vnode_check_relabel(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, struct label *newlabel)
{

	return (0);
}

static int
stub_vnode_check_rename_from(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel, struct vnode *vp, struct label *vplabel,
    struct componentname *cnp)
{

	return (0);
}

static int
stub_vnode_check_rename_to(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel, struct vnode *vp, struct label *vplabel,
    int samedir, struct componentname *cnp)
{

	return (0);
}

static int
stub_vnode_check_revoke(struct ucred *cred, struct vnode *vp,
    struct label *vplabel)
{

	return (0);
}

static int
stub_vnode_check_setacl(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, acl_type_t type, struct acl *acl)
{

	return (0);
}

static int
stub_vnode_check_setextattr(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, int attrnamespace, const char *name)
{

	return (0);
}

static int
stub_vnode_check_setflags(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, u_long flags)
{

	return (0);
}

static int
stub_vnode_check_setmode(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, mode_t mode)
{

	return (0);
}

static int
stub_vnode_check_setowner(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, uid_t uid, gid_t gid)
{

	return (0);
}

static int
stub_vnode_check_setutimes(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, struct timespec atime, struct timespec mtime)
{

	return (0);
}

static int
stub_vnode_check_stat(struct ucred *active_cred, struct ucred *file_cred,
    struct vnode *vp, struct label *vplabel)
{

	return (0);
}

static int
stub_vnode_check_unlink(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel, struct vnode *vp, struct label *vplabel,
    struct componentname *cnp)
{

	return (0);
}

static int
stub_vnode_check_write(struct ucred *active_cred, struct ucred *file_cred,
    struct vnode *vp, struct label *vplabel)
{

	return (0);
}

static int
stub_vnode_create_extattr(struct ucred *cred, struct mount *mp,
    struct label *mntlabel, struct vnode *dvp, struct label *dvplabel,
    struct vnode *vp, struct label *vplabel, struct componentname *cnp)
{

	return (0);
}

static void
stub_vnode_execve_transition(struct ucred *old, struct ucred *new,
    struct vnode *vp, struct label *vplabel, struct label *interpvplabel,
    struct image_params *imgp, struct label *execlabel)
{

}

static int
stub_vnode_execve_will_transition(struct ucred *old, struct vnode *vp,
    struct label *vplabel, struct label *interpvplabel,
    struct image_params *imgp, struct label *execlabel)
{

	return (0);
}

static void
stub_vnode_relabel(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, struct label *label)
{

}

static int
stub_vnode_setlabel_extattr(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, struct label *intlabel)
{

	return (0);
}

/*
 * Register functions with MAC Framework policy entry points.
 */
static struct mac_policy_ops stub_ops =
{
	.mpo_destroy = stub_destroy,
	.mpo_init = stub_init,
	.mpo_syscall = stub_syscall,

	.mpo_bpfdesc_check_receive = stub_bpfdesc_check_receive,
	.mpo_bpfdesc_create = stub_bpfdesc_create,
	.mpo_bpfdesc_create_mbuf = stub_bpfdesc_create_mbuf,
	.mpo_bpfdesc_destroy_label = stub_destroy_label,
	.mpo_bpfdesc_init_label = stub_init_label,

	.mpo_cred_associate_nfsd = stub_cred_associate_nfsd,
	.mpo_cred_check_relabel = stub_cred_check_relabel,
	.mpo_cred_check_setaudit = stub_cred_check_setaudit,
	.mpo_cred_check_setaudit_addr = stub_cred_check_setaudit_addr,
	.mpo_cred_check_setauid = stub_cred_check_setauid,
	.mpo_cred_check_setegid = stub_cred_check_setegid,
	.mpo_cred_check_seteuid = stub_cred_check_seteuid,
	.mpo_cred_check_setgid = stub_cred_check_setgid,
	.mpo_cred_check_setgroups = stub_cred_check_setgroups,
	.mpo_cred_check_setregid = stub_cred_check_setregid,
	.mpo_cred_check_setresgid = stub_cred_check_setresgid,
	.mpo_cred_check_setresuid = stub_cred_check_setresuid,
	.mpo_cred_check_setreuid = stub_cred_check_setreuid,
	.mpo_cred_check_setuid = stub_cred_check_setuid,
	.mpo_cred_check_visible = stub_cred_check_visible,
	.mpo_cred_copy_label = stub_copy_label,
	.mpo_cred_create_init = stub_cred_create_init,
	.mpo_cred_create_swapper = stub_cred_create_swapper,
	.mpo_cred_destroy_label = stub_destroy_label,
	.mpo_cred_externalize_label = stub_externalize_label,
	.mpo_cred_init_label = stub_init_label,
	.mpo_cred_internalize_label = stub_internalize_label,
	.mpo_cred_relabel= stub_cred_relabel,

	.mpo_devfs_create_device = stub_devfs_create_device,
	.mpo_devfs_create_directory = stub_devfs_create_directory,
	.mpo_devfs_create_symlink = stub_devfs_create_symlink,
	.mpo_devfs_destroy_label = stub_destroy_label,
	.mpo_devfs_init_label = stub_init_label,
	.mpo_devfs_update = stub_devfs_update,
	.mpo_devfs_vnode_associate = stub_devfs_vnode_associate,

	.mpo_ifnet_check_relabel = stub_ifnet_check_relabel,
	.mpo_ifnet_check_transmit = stub_ifnet_check_transmit,
	.mpo_ifnet_copy_label = stub_copy_label,
	.mpo_ifnet_create = stub_ifnet_create,
	.mpo_ifnet_create_mbuf = stub_ifnet_create_mbuf,
	.mpo_ifnet_destroy_label = stub_destroy_label,
	.mpo_ifnet_externalize_label = stub_externalize_label,
	.mpo_ifnet_init_label = stub_init_label,
	.mpo_ifnet_internalize_label = stub_internalize_label,
	.mpo_ifnet_relabel = stub_ifnet_relabel,

	.mpo_inpcb_check_deliver = stub_inpcb_check_deliver,
	.mpo_inpcb_check_visible = stub_inpcb_check_visible,
	.mpo_inpcb_create = stub_inpcb_create,
	.mpo_inpcb_create_mbuf = stub_inpcb_create_mbuf,
	.mpo_inpcb_destroy_label = stub_destroy_label,
	.mpo_inpcb_init_label = stub_init_label_waitcheck,
	.mpo_inpcb_sosetlabel = stub_inpcb_sosetlabel,

	.mpo_ip6q_create = stub_ip6q_create,
	.mpo_ip6q_destroy_label = stub_destroy_label,
	.mpo_ip6q_init_label = stub_init_label_waitcheck,
	.mpo_ip6q_match = stub_ip6q_match,
	.mpo_ip6q_update = stub_ip6q_update,
	.mpo_ip6q_reassemble = stub_ip6q_reassemble,

	.mpo_ipq_create = stub_ipq_create,
	.mpo_ipq_destroy_label = stub_destroy_label,
	.mpo_ipq_init_label = stub_init_label_waitcheck,
	.mpo_ipq_match = stub_ipq_match,
	.mpo_ipq_update = stub_ipq_update,
	.mpo_ipq_reassemble = stub_ipq_reassemble,

	.mpo_kenv_check_dump = stub_kenv_check_dump,
	.mpo_kenv_check_get = stub_kenv_check_get,
	.mpo_kenv_check_set = stub_kenv_check_set,
	.mpo_kenv_check_unset = stub_kenv_check_unset,

	.mpo_kld_check_load = stub_kld_check_load,
	.mpo_kld_check_stat = stub_kld_check_stat,

	.mpo_mbuf_copy_label = stub_copy_label,
	.mpo_mbuf_destroy_label = stub_destroy_label,
	.mpo_mbuf_init_label = stub_init_label_waitcheck,

	.mpo_mount_check_stat = stub_mount_check_stat,
	.mpo_mount_create = stub_mount_create,
	.mpo_mount_destroy_label = stub_destroy_label,
	.mpo_mount_init_label = stub_init_label,

	.mpo_netinet_arp_send = stub_netinet_arp_send,
	.mpo_netinet_firewall_reply = stub_netinet_firewall_reply,
	.mpo_netinet_firewall_send = stub_netinet_firewall_send,
	.mpo_netinet_fragment = stub_netinet_fragment,
	.mpo_netinet_icmp_reply = stub_netinet_icmp_reply,
	.mpo_netinet_icmp_replyinplace = stub_netinet_icmp_replyinplace,
	.mpo_netinet_tcp_reply = stub_netinet_tcp_reply,
	.mpo_netinet_igmp_send = stub_netinet_igmp_send,

	.mpo_netinet6_nd6_send = stub_netinet6_nd6_send,

	.mpo_pipe_check_ioctl = stub_pipe_check_ioctl,
	.mpo_pipe_check_poll = stub_pipe_check_poll,
	.mpo_pipe_check_read = stub_pipe_check_read,
	.mpo_pipe_check_relabel = stub_pipe_check_relabel,
	.mpo_pipe_check_stat = stub_pipe_check_stat,
	.mpo_pipe_check_write = stub_pipe_check_write,
	.mpo_pipe_copy_label = stub_copy_label,
	.mpo_pipe_create = stub_pipe_create,
	.mpo_pipe_destroy_label = stub_destroy_label,
	.mpo_pipe_externalize_label = stub_externalize_label,
	.mpo_pipe_init_label = stub_init_label,
	.mpo_pipe_internalize_label = stub_internalize_label,
	.mpo_pipe_relabel = stub_pipe_relabel,

	.mpo_posixsem_check_getvalue = stub_posixsem_check_getvalue,
	.mpo_posixsem_check_open = stub_posixsem_check_open,
	.mpo_posixsem_check_post = stub_posixsem_check_post,
	.mpo_posixsem_check_setmode = stub_posixsem_check_setmode,
	.mpo_posixsem_check_setowner = stub_posixsem_check_setowner,
	.mpo_posixsem_check_stat = stub_posixsem_check_stat,
	.mpo_posixsem_check_unlink = stub_posixsem_check_unlink,
	.mpo_posixsem_check_wait = stub_posixsem_check_wait,
	.mpo_posixsem_create = stub_posixsem_create,
	.mpo_posixsem_destroy_label = stub_destroy_label,
	.mpo_posixsem_init_label = stub_init_label,

	.mpo_posixshm_check_create = stub_posixshm_check_create,
	.mpo_posixshm_check_mmap = stub_posixshm_check_mmap,
	.mpo_posixshm_check_open = stub_posixshm_check_open,
	.mpo_posixshm_check_read = stub_posixshm_check_read,
	.mpo_posixshm_check_setmode = stub_posixshm_check_setmode,
	.mpo_posixshm_check_setowner = stub_posixshm_check_setowner,
	.mpo_posixshm_check_stat = stub_posixshm_check_stat,
	.mpo_posixshm_check_truncate = stub_posixshm_check_truncate,
	.mpo_posixshm_check_unlink = stub_posixshm_check_unlink,
	.mpo_posixshm_check_write = stub_posixshm_check_write,
	.mpo_posixshm_create = stub_posixshm_create,
	.mpo_posixshm_destroy_label = stub_destroy_label,
	.mpo_posixshm_init_label = stub_init_label,

	.mpo_priv_check = stub_priv_check,
	.mpo_priv_grant = stub_priv_grant,

	.mpo_proc_check_debug = stub_proc_check_debug,
	.mpo_proc_check_sched = stub_proc_check_sched,
	.mpo_proc_check_signal = stub_proc_check_signal,
	.mpo_proc_check_wait = stub_proc_check_wait,

	.mpo_socket_check_accept = stub_socket_check_accept,
	.mpo_socket_check_bind = stub_socket_check_bind,
	.mpo_socket_check_connect = stub_socket_check_connect,
	.mpo_socket_check_create = stub_socket_check_create,
	.mpo_socket_check_deliver = stub_socket_check_deliver,
	.mpo_socket_check_listen = stub_socket_check_listen,
	.mpo_socket_check_poll = stub_socket_check_poll,
	.mpo_socket_check_receive = stub_socket_check_receive,
	.mpo_socket_check_relabel = stub_socket_check_relabel,
	.mpo_socket_check_send = stub_socket_check_send,
	.mpo_socket_check_stat = stub_socket_check_stat,
	.mpo_socket_check_visible = stub_socket_check_visible,
	.mpo_socket_copy_label = stub_copy_label,
	.mpo_socket_create = stub_socket_create,
	.mpo_socket_create_mbuf = stub_socket_create_mbuf,
	.mpo_socket_destroy_label = stub_destroy_label,
	.mpo_socket_externalize_label = stub_externalize_label,
	.mpo_socket_init_label = stub_init_label_waitcheck,
	.mpo_socket_internalize_label = stub_internalize_label,
	.mpo_socket_newconn = stub_socket_newconn,
	.mpo_socket_relabel = stub_socket_relabel,

	.mpo_socketpeer_destroy_label = stub_destroy_label,
	.mpo_socketpeer_externalize_label = stub_externalize_label,
	.mpo_socketpeer_init_label = stub_init_label_waitcheck,
	.mpo_socketpeer_set_from_mbuf = stub_socketpeer_set_from_mbuf,
	.mpo_socketpeer_set_from_socket = stub_socketpeer_set_from_socket,

	.mpo_syncache_init_label = stub_init_label_waitcheck,
	.mpo_syncache_destroy_label = stub_destroy_label,
	.mpo_syncache_create = stub_syncache_create,
	.mpo_syncache_create_mbuf= stub_syncache_create_mbuf,

	.mpo_sysvmsg_cleanup = stub_sysvmsg_cleanup,
	.mpo_sysvmsg_create = stub_sysvmsg_create,
	.mpo_sysvmsg_destroy_label = stub_destroy_label,
	.mpo_sysvmsg_init_label = stub_init_label,

	.mpo_sysvmsq_check_msgmsq = stub_sysvmsq_check_msgmsq,
	.mpo_sysvmsq_check_msgrcv = stub_sysvmsq_check_msgrcv,
	.mpo_sysvmsq_check_msgrmid = stub_sysvmsq_check_msgrmid,
	.mpo_sysvmsq_check_msqget = stub_sysvmsq_check_msqget,
	.mpo_sysvmsq_check_msqsnd = stub_sysvmsq_check_msqsnd,
	.mpo_sysvmsq_check_msqrcv = stub_sysvmsq_check_msqrcv,
	.mpo_sysvmsq_check_msqctl = stub_sysvmsq_check_msqctl,
	.mpo_sysvmsq_cleanup = stub_sysvmsq_cleanup,
	.mpo_sysvmsq_create = stub_sysvmsq_create,
	.mpo_sysvmsq_destroy_label = stub_destroy_label,
	.mpo_sysvmsq_init_label = stub_init_label,

	.mpo_sysvsem_check_semctl = stub_sysvsem_check_semctl,
	.mpo_sysvsem_check_semget = stub_sysvsem_check_semget,
	.mpo_sysvsem_check_semop = stub_sysvsem_check_semop,
	.mpo_sysvsem_cleanup = stub_sysvsem_cleanup,
	.mpo_sysvsem_create = stub_sysvsem_create,
	.mpo_sysvsem_destroy_label = stub_destroy_label,
	.mpo_sysvsem_init_label = stub_init_label,

	.mpo_sysvshm_check_shmat = stub_sysvshm_check_shmat,
	.mpo_sysvshm_check_shmctl = stub_sysvshm_check_shmctl,
	.mpo_sysvshm_check_shmdt = stub_sysvshm_check_shmdt,
	.mpo_sysvshm_check_shmget = stub_sysvshm_check_shmget,
	.mpo_sysvshm_cleanup = stub_sysvshm_cleanup,
	.mpo_sysvshm_create = stub_sysvshm_create,
	.mpo_sysvshm_destroy_label = stub_destroy_label,
	.mpo_sysvshm_init_label = stub_init_label,

	.mpo_system_check_acct = stub_system_check_acct,
	.mpo_system_check_audit = stub_system_check_audit,
	.mpo_system_check_auditctl = stub_system_check_auditctl,
	.mpo_system_check_auditon = stub_system_check_auditon,
	.mpo_system_check_reboot = stub_system_check_reboot,
	.mpo_system_check_swapoff = stub_system_check_swapoff,
	.mpo_system_check_swapon = stub_system_check_swapon,
	.mpo_system_check_sysctl = stub_system_check_sysctl,

	.mpo_thread_userret = stub_thread_userret,

	.mpo_vnode_associate_extattr = stub_vnode_associate_extattr,
	.mpo_vnode_associate_singlelabel = stub_vnode_associate_singlelabel,
	.mpo_vnode_check_access = stub_vnode_check_access,
	.mpo_vnode_check_chdir = stub_vnode_check_chdir,
	.mpo_vnode_check_chroot = stub_vnode_check_chroot,
	.mpo_vnode_check_create = stub_vnode_check_create,
	.mpo_vnode_check_deleteacl = stub_vnode_check_deleteacl,
	.mpo_vnode_check_deleteextattr = stub_vnode_check_deleteextattr,
	.mpo_vnode_check_exec = stub_vnode_check_exec,
	.mpo_vnode_check_getacl = stub_vnode_check_getacl,
	.mpo_vnode_check_getextattr = stub_vnode_check_getextattr,
	.mpo_vnode_check_link = stub_vnode_check_link,
	.mpo_vnode_check_listextattr = stub_vnode_check_listextattr,
	.mpo_vnode_check_lookup = stub_vnode_check_lookup,
	.mpo_vnode_check_mmap = stub_vnode_check_mmap,
	.mpo_vnode_check_mmap_downgrade = stub_vnode_check_mmap_downgrade,
	.mpo_vnode_check_mprotect = stub_vnode_check_mprotect,
	.mpo_vnode_check_open = stub_vnode_check_open,
	.mpo_vnode_check_poll = stub_vnode_check_poll,
	.mpo_vnode_check_read = stub_vnode_check_read,
	.mpo_vnode_check_readdir = stub_vnode_check_readdir,
	.mpo_vnode_check_readlink = stub_vnode_check_readlink,
	.mpo_vnode_check_relabel = stub_vnode_check_relabel,
	.mpo_vnode_check_rename_from = stub_vnode_check_rename_from,
	.mpo_vnode_check_rename_to = stub_vnode_check_rename_to,
	.mpo_vnode_check_revoke = stub_vnode_check_revoke,
	.mpo_vnode_check_setacl = stub_vnode_check_setacl,
	.mpo_vnode_check_setextattr = stub_vnode_check_setextattr,
	.mpo_vnode_check_setflags = stub_vnode_check_setflags,
	.mpo_vnode_check_setmode = stub_vnode_check_setmode,
	.mpo_vnode_check_setowner = stub_vnode_check_setowner,
	.mpo_vnode_check_setutimes = stub_vnode_check_setutimes,
	.mpo_vnode_check_stat = stub_vnode_check_stat,
	.mpo_vnode_check_unlink = stub_vnode_check_unlink,
	.mpo_vnode_check_write = stub_vnode_check_write,
	.mpo_vnode_copy_label = stub_copy_label,
	.mpo_vnode_create_extattr = stub_vnode_create_extattr,
	.mpo_vnode_destroy_label = stub_destroy_label,
	.mpo_vnode_execve_transition = stub_vnode_execve_transition,
	.mpo_vnode_execve_will_transition = stub_vnode_execve_will_transition,
	.mpo_vnode_externalize_label = stub_externalize_label,
	.mpo_vnode_init_label = stub_init_label,
	.mpo_vnode_internalize_label = stub_internalize_label,
	.mpo_vnode_relabel = stub_vnode_relabel,
	.mpo_vnode_setlabel_extattr = stub_vnode_setlabel_extattr,
};

MAC_POLICY_SET(&stub_ops, mac_stub, "TrustedBSD MAC/Stub",
    MPC_LOADTIME_FLAG_UNLOADOK, NULL);
