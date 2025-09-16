/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_NET_SCM_H
#define __LINUX_NET_SCM_H

#include <linux/limits.h>
#include <linux/net.h>
#include <linux/cred.h>
#include <linux/file.h>
#include <linux/security.h>
#include <linux/pid.h>
#include <linux/nsproxy.h>
#include <linux/sched/signal.h>
#include <net/compat.h>

/* Well, we should have at least one descriptor open
 * to accept passed FDs 8)
 */
#define SCM_MAX_FD	253

struct scm_creds {
	u32	pid;
	kuid_t	uid;
	kgid_t	gid;
};

#ifdef CONFIG_UNIX
struct unix_edge;
#endif

struct scm_fp_list {
	short			count;
	short			count_unix;
	short			max;
#ifdef CONFIG_UNIX
	bool			inflight;
	bool			dead;
	struct list_head	vertices;
	struct unix_edge	*edges;
#endif
	struct user_struct	*user;
	struct file		*fp[SCM_MAX_FD];
};

struct scm_cookie {
	struct pid		*pid;		/* Skb credentials */
	struct scm_fp_list	*fp;		/* Passed files		*/
	struct scm_creds	creds;		/* Skb credentials	*/
#ifdef CONFIG_SECURITY_NETWORK
	u32			secid;		/* Passed security ID 	*/
#endif
};

void scm_detach_fds(struct msghdr *msg, struct scm_cookie *scm);
void scm_detach_fds_compat(struct msghdr *msg, struct scm_cookie *scm);
int __scm_send(struct socket *sock, struct msghdr *msg, struct scm_cookie *scm);
void __scm_destroy(struct scm_cookie *scm);
struct scm_fp_list *scm_fp_dup(struct scm_fp_list *fpl);

#ifdef CONFIG_SECURITY_NETWORK
static __inline__ void unix_get_peersec_dgram(struct socket *sock, struct scm_cookie *scm)
{
	security_socket_getpeersec_dgram(sock, NULL, &scm->secid);
}
#else
static __inline__ void unix_get_peersec_dgram(struct socket *sock, struct scm_cookie *scm)
{ }
#endif /* CONFIG_SECURITY_NETWORK */

static __inline__ void scm_set_cred(struct scm_cookie *scm,
				    struct pid *pid, kuid_t uid, kgid_t gid)
{
	scm->pid = get_pid(pid);
	scm->creds.pid = pid_vnr(pid);
	scm->creds.uid = uid;
	scm->creds.gid = gid;
}

static __inline__ void scm_destroy_cred(struct scm_cookie *scm)
{
	put_pid(scm->pid);
	scm->pid = NULL;
}

static __inline__ void scm_destroy(struct scm_cookie *scm)
{
	scm_destroy_cred(scm);
	if (scm->fp)
		__scm_destroy(scm);
}

static __inline__ int scm_send(struct socket *sock, struct msghdr *msg,
			       struct scm_cookie *scm, bool forcecreds)
{
	memset(scm, 0, sizeof(*scm));
	scm->creds.uid = INVALID_UID;
	scm->creds.gid = INVALID_GID;
	if (forcecreds)
		scm_set_cred(scm, task_tgid(current), current_uid(), current_gid());
	unix_get_peersec_dgram(sock, scm);
	if (msg->msg_controllen <= 0)
		return 0;
	return __scm_send(sock, msg, scm);
}

void scm_recv(struct socket *sock, struct msghdr *msg,
	      struct scm_cookie *scm, int flags);
void scm_recv_unix(struct socket *sock, struct msghdr *msg,
		   struct scm_cookie *scm, int flags);

static inline int scm_recv_one_fd(struct file *f, int __user *ufd,
				  unsigned int flags)
{
	if (!ufd)
		return -EFAULT;
	return receive_fd(f, ufd, flags);
}

#endif /* __LINUX_NET_SCM_H */

