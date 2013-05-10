#ifndef __LINUX_NET_SCM_H
#define __LINUX_NET_SCM_H

#include <linux/limits.h>
#include <linux/net.h>
#include <linux/security.h>
#include <linux/pid.h>
#include <linux/nsproxy.h>

/* Well, we should have at least one descriptor open
 * to accept passed FDs 8)
 */
#define SCM_MAX_FD	253

struct scm_fp_list {
	struct list_head	list;
	short			count;
	short			max;
	struct file		*fp[SCM_MAX_FD];
};

struct scm_cookie {
	struct pid		*pid;		/* Skb credentials */
	const struct cred	*cred;
	struct scm_fp_list	*fp;		/* Passed files		*/
	struct ucred		creds;		/* Skb credentials	*/
#ifdef CONFIG_SECURITY_NETWORK
	u32			secid;		/* Passed security ID 	*/
#endif
};

extern void scm_detach_fds(struct msghdr *msg, struct scm_cookie *scm);
extern void scm_detach_fds_compat(struct msghdr *msg, struct scm_cookie *scm);
extern int __scm_send(struct socket *sock, struct msghdr *msg, struct scm_cookie *scm);
extern void __scm_destroy(struct scm_cookie *scm);
extern struct scm_fp_list * scm_fp_dup(struct scm_fp_list *fpl);

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
				    struct pid *pid, const struct cred *cred)
{
	scm->pid  = get_pid(pid);
	scm->cred = get_cred(cred);
	cred_to_ucred(pid, cred, &scm->creds, false);
}

static __inline__ void scm_destroy_cred(struct scm_cookie *scm)
{
	put_pid(scm->pid);
	scm->pid  = NULL;

	if (scm->cred)
		put_cred(scm->cred);
	scm->cred = NULL;
}

static __inline__ void scm_destroy(struct scm_cookie *scm)
{
	scm_destroy_cred(scm);
	if (scm && scm->fp)
		__scm_destroy(scm);
}

static __inline__ int scm_send(struct socket *sock, struct msghdr *msg,
			       struct scm_cookie *scm)
{
	scm_set_cred(scm, task_tgid(current), current_cred());
	scm->fp = NULL;
	unix_get_peersec_dgram(sock, scm);
	if (msg->msg_controllen <= 0)
		return 0;
	return __scm_send(sock, msg, scm);
}

#ifdef CONFIG_SECURITY_NETWORK
static inline void scm_passec(struct socket *sock, struct msghdr *msg, struct scm_cookie *scm)
{
	char *secdata;
	u32 seclen;
	int err;

	if (test_bit(SOCK_PASSSEC, &sock->flags)) {
		err = security_secid_to_secctx(scm->secid, &secdata, &seclen);

		if (!err) {
			put_cmsg(msg, SOL_SOCKET, SCM_SECURITY, seclen, secdata);
			security_release_secctx(secdata, seclen);
		}
	}
}
#else
static inline void scm_passec(struct socket *sock, struct msghdr *msg, struct scm_cookie *scm)
{ }
#endif /* CONFIG_SECURITY_NETWORK */

static __inline__ void scm_recv(struct socket *sock, struct msghdr *msg,
				struct scm_cookie *scm, int flags)
{
	if (!msg->msg_control) {
		if (test_bit(SOCK_PASSCRED, &sock->flags) || scm->fp)
			msg->msg_flags |= MSG_CTRUNC;
		scm_destroy(scm);
		return;
	}

	if (test_bit(SOCK_PASSCRED, &sock->flags))
		put_cmsg(msg, SOL_SOCKET, SCM_CREDENTIALS, sizeof(scm->creds), &scm->creds);

	scm_destroy_cred(scm);

	scm_passec(sock, msg, scm);

	if (!scm->fp)
		return;
	
	scm_detach_fds(msg, scm);
}


#endif /* __LINUX_NET_SCM_H */

