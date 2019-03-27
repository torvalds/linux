/*-
 * Copyright (c) 1999-2002, 2007-2011 Robert N. M. Watson
 * Copyright (c) 2001-2005 Networks Associates Technology, Inc.
 * Copyright (c) 2005-2006 SPARTA, Inc.
 * All rights reserved.
 *
 * This software was developed by Robert Watson for the TrustedBSD Project.
 *
 * This software was developed for the FreeBSD Project in part by Network
 * Associates Laboratories, the Security Research Division of Network
 * Associates, Inc. under DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"),
 * as part of the DARPA CHATS research program.
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
 * Kernel interface for Mandatory Access Control -- how kernel services
 * interact with the TrustedBSD MAC Framework.
 */

#ifndef _SECURITY_MAC_MAC_FRAMEWORK_H_
#define	_SECURITY_MAC_MAC_FRAMEWORK_H_

#ifndef _KERNEL
#error "no user-serviceable parts inside"
#endif

struct auditinfo;
struct auditinfo_addr;
struct bpf_d;
struct cdev;
struct componentname;
struct devfs_dirent;
struct ifnet;
struct ifreq;
struct image_params;
struct inpcb;
struct ip6q;
struct ipq;
struct ksem;
struct label;
struct m_tag;
struct mac;
struct mbuf;
struct mount;
struct msg;
struct msqid_kernel;
struct proc;
struct semid_kernel;
struct shmfd;
struct shmid_kernel;
struct sockaddr;
struct socket;
struct sysctl_oid;
struct sysctl_req;
struct pipepair;
struct thread;
struct timespec;
struct ucred;
struct vattr;
struct vnode;
struct vop_setlabel_args;

#include <sys/acl.h>			/* XXX acl_type_t */
#include <sys/types.h>			/* accmode_t */

/*
 * Entry points to the TrustedBSD MAC Framework from the remainder of the
 * kernel: entry points are named based on a principle object type and an
 * action relating to it.  They are sorted alphabetically first by object
 * type and then action.  In some situations, the principle object type is
 * obvious, and in other cases, less so as multiple objects may be inolved
 * in the operation.
 */
int	mac_bpfdesc_check_receive(struct bpf_d *d, struct ifnet *ifp);
void	mac_bpfdesc_create(struct ucred *cred, struct bpf_d *d);
void	mac_bpfdesc_create_mbuf(struct bpf_d *d, struct mbuf *m);
void	mac_bpfdesc_destroy(struct bpf_d *);
void	mac_bpfdesc_init(struct bpf_d *);

void	mac_cred_associate_nfsd(struct ucred *cred);
int	mac_cred_check_setaudit(struct ucred *cred, struct auditinfo *ai);
int	mac_cred_check_setaudit_addr(struct ucred *cred,
	    struct auditinfo_addr *aia);
int	mac_cred_check_setauid(struct ucred *cred, uid_t auid);
int	mac_cred_check_setegid(struct ucred *cred, gid_t egid);
int	mac_cred_check_seteuid(struct ucred *cred, uid_t euid);
int	mac_cred_check_setgid(struct ucred *cred, gid_t gid);
int	mac_cred_check_setgroups(struct ucred *cred, int ngroups,
	    gid_t *gidset);
int	mac_cred_check_setregid(struct ucred *cred, gid_t rgid, gid_t egid);
int	mac_cred_check_setresgid(struct ucred *cred, gid_t rgid, gid_t egid,
	    gid_t sgid);
int	mac_cred_check_setresuid(struct ucred *cred, uid_t ruid, uid_t euid,
	    uid_t suid);
int	mac_cred_check_setreuid(struct ucred *cred, uid_t ruid, uid_t euid);
int	mac_cred_check_setuid(struct ucred *cred, uid_t uid);
int	mac_cred_check_visible(struct ucred *cr1, struct ucred *cr2);
void	mac_cred_copy(struct ucred *cr1, struct ucred *cr2);
void	mac_cred_create_init(struct ucred *cred);
void	mac_cred_create_swapper(struct ucred *cred);
void	mac_cred_destroy(struct ucred *);
void	mac_cred_init(struct ucred *);

void	mac_devfs_create_device(struct ucred *cred, struct mount *mp,
	    struct cdev *dev, struct devfs_dirent *de);
void	mac_devfs_create_directory(struct mount *mp, char *dirname,
	    int dirnamelen, struct devfs_dirent *de);
void	mac_devfs_create_symlink(struct ucred *cred, struct mount *mp,
	    struct devfs_dirent *dd, struct devfs_dirent *de);
void	mac_devfs_destroy(struct devfs_dirent *);
void	mac_devfs_init(struct devfs_dirent *);
void	mac_devfs_update(struct mount *mp, struct devfs_dirent *de,
	    struct vnode *vp);
void	mac_devfs_vnode_associate(struct mount *mp, struct devfs_dirent *de,
	    struct vnode *vp);

int	mac_ifnet_check_transmit(struct ifnet *ifp, struct mbuf *m);
void	mac_ifnet_create(struct ifnet *ifp);
void	mac_ifnet_create_mbuf(struct ifnet *ifp, struct mbuf *m);
void	mac_ifnet_destroy(struct ifnet *);
void	mac_ifnet_init(struct ifnet *);
int	mac_ifnet_ioctl_get(struct ucred *cred, struct ifreq *ifr,
	    struct ifnet *ifp);
int	mac_ifnet_ioctl_set(struct ucred *cred, struct ifreq *ifr,
	    struct ifnet *ifp);

int	mac_inpcb_check_deliver(struct inpcb *inp, struct mbuf *m);
int	mac_inpcb_check_visible(struct ucred *cred, struct inpcb *inp);
void	mac_inpcb_create(struct socket *so, struct inpcb *inp);
void	mac_inpcb_create_mbuf(struct inpcb *inp, struct mbuf *m);
void	mac_inpcb_destroy(struct inpcb *);
int	mac_inpcb_init(struct inpcb *, int);
void	mac_inpcb_sosetlabel(struct socket *so, struct inpcb *inp);

void	mac_ip6q_create(struct mbuf *m, struct ip6q *q6);
void	mac_ip6q_destroy(struct ip6q *q6);
int	mac_ip6q_init(struct ip6q *q6, int);
int	mac_ip6q_match(struct mbuf *m, struct ip6q *q6);
void	mac_ip6q_reassemble(struct ip6q *q6, struct mbuf *m);
void	mac_ip6q_update(struct mbuf *m, struct ip6q *q6);

void	mac_ipq_create(struct mbuf *m, struct ipq *q);
void	mac_ipq_destroy(struct ipq *q);
int	mac_ipq_init(struct ipq *q, int);
int	mac_ipq_match(struct mbuf *m, struct ipq *q);
void	mac_ipq_reassemble(struct ipq *q, struct mbuf *m);
void	mac_ipq_update(struct mbuf *m, struct ipq *q);

int	mac_kenv_check_dump(struct ucred *cred);
int	mac_kenv_check_get(struct ucred *cred, char *name);
int	mac_kenv_check_set(struct ucred *cred, char *name, char *value);
int	mac_kenv_check_unset(struct ucred *cred, char *name);

int	mac_kld_check_load(struct ucred *cred, struct vnode *vp);
int	mac_kld_check_stat(struct ucred *cred);

void	mac_mbuf_copy(struct mbuf *, struct mbuf *);
int	mac_mbuf_init(struct mbuf *, int);

void	mac_mbuf_tag_copy(struct m_tag *, struct m_tag *);
void	mac_mbuf_tag_destroy(struct m_tag *);
int	mac_mbuf_tag_init(struct m_tag *, int);

int	mac_mount_check_stat(struct ucred *cred, struct mount *mp);
void	mac_mount_create(struct ucred *cred, struct mount *mp);
void	mac_mount_destroy(struct mount *);
void	mac_mount_init(struct mount *);

void	mac_netinet_arp_send(struct ifnet *ifp, struct mbuf *m);
void	mac_netinet_firewall_reply(struct mbuf *mrecv, struct mbuf *msend);
void	mac_netinet_firewall_send(struct mbuf *m);
void	mac_netinet_fragment(struct mbuf *m, struct mbuf *frag);
void	mac_netinet_icmp_reply(struct mbuf *mrecv, struct mbuf *msend);
void	mac_netinet_icmp_replyinplace(struct mbuf *m);
void	mac_netinet_igmp_send(struct ifnet *ifp, struct mbuf *m);
void	mac_netinet_tcp_reply(struct mbuf *m);

void	mac_netinet6_nd6_send(struct ifnet *ifp, struct mbuf *m);

int	mac_pipe_check_ioctl(struct ucred *cred, struct pipepair *pp,
	    unsigned long cmd, void *data);
int	mac_pipe_check_poll(struct ucred *cred, struct pipepair *pp);
int	mac_pipe_check_read(struct ucred *cred, struct pipepair *pp);
int	mac_pipe_check_stat(struct ucred *cred, struct pipepair *pp);
int	mac_pipe_check_write(struct ucred *cred, struct pipepair *pp);
void	mac_pipe_create(struct ucred *cred, struct pipepair *pp);
void	mac_pipe_destroy(struct pipepair *);
void	mac_pipe_init(struct pipepair *);
int	mac_pipe_label_set(struct ucred *cred, struct pipepair *pp,
	    struct label *label);

int	mac_posixsem_check_getvalue(struct ucred *active_cred,
	    struct ucred *file_cred, struct ksem *ks);
int	mac_posixsem_check_open(struct ucred *cred, struct ksem *ks);
int	mac_posixsem_check_post(struct ucred *active_cred,
	    struct ucred *file_cred, struct ksem *ks);
int	mac_posixsem_check_setmode(struct ucred *cred, struct ksem *ks,
	    mode_t mode);
int	mac_posixsem_check_setowner(struct ucred *cred, struct ksem *ks,
	    uid_t uid, gid_t gid);
int	mac_posixsem_check_stat(struct ucred *active_cred,
	    struct ucred *file_cred, struct ksem *ks);
int	mac_posixsem_check_unlink(struct ucred *cred, struct ksem *ks);
int	mac_posixsem_check_wait(struct ucred *active_cred,
	    struct ucred *file_cred, struct ksem *ks);
void 	mac_posixsem_create(struct ucred *cred, struct ksem *ks);
void	mac_posixsem_destroy(struct ksem *);
void	mac_posixsem_init(struct ksem *);

int	mac_posixshm_check_create(struct ucred *cred, const char *path);
int	mac_posixshm_check_mmap(struct ucred *cred, struct shmfd *shmfd,
	    int prot, int flags);
int	mac_posixshm_check_open(struct ucred *cred, struct shmfd *shmfd,
	    accmode_t accmode);
int	mac_posixshm_check_read(struct ucred *active_cred,
	    struct ucred *file_cred, struct shmfd *shmfd);
int	mac_posixshm_check_setmode(struct ucred *cred, struct shmfd *shmfd,
	    mode_t mode);
int	mac_posixshm_check_setowner(struct ucred *cred, struct shmfd *shmfd,
	    uid_t uid, gid_t gid);
int	mac_posixshm_check_stat(struct ucred *active_cred,
	    struct ucred *file_cred, struct shmfd *shmfd);
int	mac_posixshm_check_truncate(struct ucred *active_cred,
	    struct ucred *file_cred, struct shmfd *shmfd);
int	mac_posixshm_check_unlink(struct ucred *cred, struct shmfd *shmfd);
int	mac_posixshm_check_write(struct ucred *active_cred,
	    struct ucred *file_cred, struct shmfd *shmfd);
void 	mac_posixshm_create(struct ucred *cred, struct shmfd *shmfd);
void	mac_posixshm_destroy(struct shmfd *);
void	mac_posixshm_init(struct shmfd *);

int	mac_priv_check(struct ucred *cred, int priv);
int	mac_priv_grant(struct ucred *cred, int priv);

int	mac_proc_check_debug(struct ucred *cred, struct proc *p);
int	mac_proc_check_sched(struct ucred *cred, struct proc *p);
int	mac_proc_check_signal(struct ucred *cred, struct proc *p,
	    int signum);
int	mac_proc_check_wait(struct ucred *cred, struct proc *p);
void	mac_proc_destroy(struct proc *);
void	mac_proc_init(struct proc *);
void	mac_proc_vm_revoke(struct thread *td);
int	mac_execve_enter(struct image_params *imgp, struct mac *mac_p);
void	mac_execve_exit(struct image_params *imgp);
void	mac_execve_interpreter_enter(struct vnode *interpvp,
	    struct label **interplabel);
void	mac_execve_interpreter_exit(struct label *interpvplabel);

int	mac_socket_check_accept(struct ucred *cred, struct socket *so);
int	mac_socket_check_bind(struct ucred *cred, struct socket *so,
	    struct sockaddr *sa);
int	mac_socket_check_connect(struct ucred *cred, struct socket *so,
	    struct sockaddr *sa);
int	mac_socket_check_create(struct ucred *cred, int domain, int type,
	    int proto);
int	mac_socket_check_deliver(struct socket *so, struct mbuf *m);
int	mac_socket_check_listen(struct ucred *cred, struct socket *so);
int	mac_socket_check_poll(struct ucred *cred, struct socket *so);
int	mac_socket_check_receive(struct ucred *cred, struct socket *so);
int	mac_socket_check_send(struct ucred *cred, struct socket *so);
int	mac_socket_check_stat(struct ucred *cred, struct socket *so);
int	mac_socket_check_visible(struct ucred *cred, struct socket *so);
void	mac_socket_create_mbuf(struct socket *so, struct mbuf *m);
void	mac_socket_create(struct ucred *cred, struct socket *so);
void	mac_socket_destroy(struct socket *);
int	mac_socket_init(struct socket *, int);
void	mac_socket_newconn(struct socket *oldso, struct socket *newso);
int	mac_getsockopt_label(struct ucred *cred, struct socket *so,
	    struct mac *extmac);
int	mac_getsockopt_peerlabel(struct ucred *cred, struct socket *so,
	    struct mac *extmac);
int	mac_setsockopt_label(struct ucred *cred, struct socket *so,
	    struct mac *extmac);

void	mac_socketpeer_set_from_mbuf(struct mbuf *m, struct socket *so);
void	mac_socketpeer_set_from_socket(struct socket *oldso,
	    struct socket *newso);

void	mac_syncache_create(struct label *l, struct inpcb *inp);
void	mac_syncache_create_mbuf(struct label *l, struct mbuf *m);
void	mac_syncache_destroy(struct label **l);
int	mac_syncache_init(struct label **l);

int	mac_system_check_acct(struct ucred *cred, struct vnode *vp);
int	mac_system_check_audit(struct ucred *cred, void *record, int length);
int	mac_system_check_auditctl(struct ucred *cred, struct vnode *vp);
int	mac_system_check_auditon(struct ucred *cred, int cmd);
int	mac_system_check_reboot(struct ucred *cred, int howto);
int	mac_system_check_swapon(struct ucred *cred, struct vnode *vp);
int	mac_system_check_swapoff(struct ucred *cred, struct vnode *vp);
int	mac_system_check_sysctl(struct ucred *cred, struct sysctl_oid *oidp,
	    void *arg1, int arg2, struct sysctl_req *req);

void	mac_sysvmsg_cleanup(struct msg *msgptr);
void	mac_sysvmsg_create(struct ucred *cred, struct msqid_kernel *msqkptr,
	    struct msg *msgptr);
void	mac_sysvmsg_destroy(struct msg *);
void	mac_sysvmsg_init(struct msg *);

int	mac_sysvmsq_check_msgmsq(struct ucred *cred, struct msg *msgptr,
	    struct msqid_kernel *msqkptr);
int	mac_sysvmsq_check_msgrcv(struct ucred *cred, struct msg *msgptr);
int	mac_sysvmsq_check_msgrmid(struct ucred *cred, struct msg *msgptr);
int	mac_sysvmsq_check_msqctl(struct ucred *cred,
	    struct msqid_kernel *msqkptr, int cmd);
int	mac_sysvmsq_check_msqget(struct ucred *cred,
	    struct msqid_kernel *msqkptr);
int	mac_sysvmsq_check_msqrcv(struct ucred *cred,
	    struct msqid_kernel *msqkptr);
int	mac_sysvmsq_check_msqsnd(struct ucred *cred,
	    struct msqid_kernel *msqkptr);
void	mac_sysvmsq_cleanup(struct msqid_kernel *msqkptr);
void	mac_sysvmsq_create(struct ucred *cred, struct msqid_kernel *msqkptr);
void	mac_sysvmsq_destroy(struct msqid_kernel *);
void	mac_sysvmsq_init(struct msqid_kernel *);

int	mac_sysvsem_check_semctl(struct ucred *cred,
	    struct semid_kernel *semakptr, int cmd);
int	mac_sysvsem_check_semget(struct ucred *cred,
	   struct semid_kernel *semakptr);
int	mac_sysvsem_check_semop(struct ucred *cred,
	    struct semid_kernel *semakptr, size_t accesstype);
void	mac_sysvsem_cleanup(struct semid_kernel *semakptr);
void	mac_sysvsem_create(struct ucred *cred,
	    struct semid_kernel *semakptr);
void	mac_sysvsem_destroy(struct semid_kernel *);
void	mac_sysvsem_init(struct semid_kernel *);

int	mac_sysvshm_check_shmat(struct ucred *cred,
	    struct shmid_kernel *shmsegptr, int shmflg);
int	mac_sysvshm_check_shmctl(struct ucred *cred,
	    struct shmid_kernel *shmsegptr, int cmd);
int	mac_sysvshm_check_shmdt(struct ucred *cred,
	    struct shmid_kernel *shmsegptr);
int	mac_sysvshm_check_shmget(struct ucred *cred,
	    struct shmid_kernel *shmsegptr, int shmflg);
void	mac_sysvshm_cleanup(struct shmid_kernel *shmsegptr);
void	mac_sysvshm_create(struct ucred *cred,
	    struct shmid_kernel *shmsegptr);
void	mac_sysvshm_destroy(struct shmid_kernel *);
void	mac_sysvshm_init(struct shmid_kernel *);

void	mac_thread_userret(struct thread *td);

int	mac_vnode_associate_extattr(struct mount *mp, struct vnode *vp);
void	mac_vnode_associate_singlelabel(struct mount *mp, struct vnode *vp);
int	mac_vnode_check_access(struct ucred *cred, struct vnode *vp,
	    accmode_t accmode);
int	mac_vnode_check_chdir(struct ucred *cred, struct vnode *dvp);
int	mac_vnode_check_chroot(struct ucred *cred, struct vnode *dvp);
int	mac_vnode_check_create(struct ucred *cred, struct vnode *dvp,
	    struct componentname *cnp, struct vattr *vap);
int	mac_vnode_check_deleteacl(struct ucred *cred, struct vnode *vp,
	    acl_type_t type);
int	mac_vnode_check_deleteextattr(struct ucred *cred, struct vnode *vp,
	    int attrnamespace, const char *name);
int	mac_vnode_check_exec(struct ucred *cred, struct vnode *vp,
	    struct image_params *imgp);
int	mac_vnode_check_getacl(struct ucred *cred, struct vnode *vp,
	    acl_type_t type);
int	mac_vnode_check_getextattr(struct ucred *cred, struct vnode *vp,
	    int attrnamespace, const char *name);
int	mac_vnode_check_link(struct ucred *cred, struct vnode *dvp,
	    struct vnode *vp, struct componentname *cnp);
int	mac_vnode_check_listextattr(struct ucred *cred, struct vnode *vp,
	    int attrnamespace);
int	mac_vnode_check_lookup(struct ucred *cred, struct vnode *dvp,
 	    struct componentname *cnp);
int	mac_vnode_check_mmap(struct ucred *cred, struct vnode *vp, int prot,
	    int flags);
int	mac_vnode_check_mprotect(struct ucred *cred, struct vnode *vp,
	    int prot);
int	mac_vnode_check_open(struct ucred *cred, struct vnode *vp,
	    accmode_t accmode);
int	mac_vnode_check_poll(struct ucred *active_cred,
	    struct ucred *file_cred, struct vnode *vp);
int	mac_vnode_check_read(struct ucred *active_cred,
	    struct ucred *file_cred, struct vnode *vp);
int	mac_vnode_check_readdir(struct ucred *cred, struct vnode *vp);
int	mac_vnode_check_readlink(struct ucred *cred, struct vnode *vp);
int	mac_vnode_check_rename_from(struct ucred *cred, struct vnode *dvp,
	    struct vnode *vp, struct componentname *cnp);
int	mac_vnode_check_rename_to(struct ucred *cred, struct vnode *dvp,
	    struct vnode *vp, int samedir, struct componentname *cnp);
int	mac_vnode_check_revoke(struct ucred *cred, struct vnode *vp);
int	mac_vnode_check_setacl(struct ucred *cred, struct vnode *vp,
	    acl_type_t type, struct acl *acl);
int	mac_vnode_check_setextattr(struct ucred *cred, struct vnode *vp,
	    int attrnamespace, const char *name);
int	mac_vnode_check_setflags(struct ucred *cred, struct vnode *vp,
	    u_long flags);
int	mac_vnode_check_setmode(struct ucred *cred, struct vnode *vp,
	    mode_t mode);
int	mac_vnode_check_setowner(struct ucred *cred, struct vnode *vp,
	    uid_t uid, gid_t gid);
int	mac_vnode_check_setutimes(struct ucred *cred, struct vnode *vp,
	    struct timespec atime, struct timespec mtime);
int	mac_vnode_check_stat(struct ucred *active_cred,
	    struct ucred *file_cred, struct vnode *vp);
int	mac_vnode_check_unlink(struct ucred *cred, struct vnode *dvp,
	    struct vnode *vp, struct componentname *cnp);
int	mac_vnode_check_write(struct ucred *active_cred,
	    struct ucred *file_cred, struct vnode *vp);
void	mac_vnode_copy_label(struct label *, struct label *);
void	mac_vnode_init(struct vnode *);
int	mac_vnode_create_extattr(struct ucred *cred, struct mount *mp,
	    struct vnode *dvp, struct vnode *vp, struct componentname *cnp);
void	mac_vnode_destroy(struct vnode *);
void	mac_vnode_execve_transition(struct ucred *oldcred,
	    struct ucred *newcred, struct vnode *vp,
	    struct label *interpvplabel, struct image_params *imgp);
int	mac_vnode_execve_will_transition(struct ucred *cred,
	    struct vnode *vp, struct label *interpvplabel,
	    struct image_params *imgp);
void	mac_vnode_relabel(struct ucred *cred, struct vnode *vp,
	    struct label *newlabel);

/*
 * Calls to help various file systems implement labeling functionality using
 * their existing EA implementation.
 */
int	vop_stdsetlabel_ea(struct vop_setlabel_args *ap);

#endif /* !_SECURITY_MAC_MAC_FRAMEWORK_H_ */
