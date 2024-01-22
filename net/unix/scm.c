// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/net.h>
#include <linux/fs.h>
#include <net/af_unix.h>
#include <net/scm.h>
#include <linux/init.h>
#include <linux/io_uring.h>

#include "scm.h"

unsigned int unix_tot_inflight;
EXPORT_SYMBOL(unix_tot_inflight);

LIST_HEAD(gc_inflight_list);
EXPORT_SYMBOL(gc_inflight_list);

DEFINE_SPINLOCK(unix_gc_lock);
EXPORT_SYMBOL(unix_gc_lock);

struct sock *unix_get_socket(struct file *filp)
{
	struct sock *u_sock = NULL;
	struct inode *inode = file_inode(filp);

	/* Socket ? */
	if (S_ISSOCK(inode->i_mode) && !(filp->f_mode & FMODE_PATH)) {
		struct socket *sock = SOCKET_I(inode);
		const struct proto_ops *ops = READ_ONCE(sock->ops);
		struct sock *s = sock->sk;

		/* PF_UNIX ? */
		if (s && ops && ops->family == PF_UNIX)
			u_sock = s;
	}

	return u_sock;
}
EXPORT_SYMBOL(unix_get_socket);

/* Keep the number of times in flight count for the file
 * descriptor if it is for an AF_UNIX socket.
 */
void unix_inflight(struct user_struct *user, struct file *fp)
{
	struct sock *s = unix_get_socket(fp);

	spin_lock(&unix_gc_lock);

	if (s) {
		struct unix_sock *u = unix_sk(s);

		if (atomic_long_inc_return(&u->inflight) == 1) {
			BUG_ON(!list_empty(&u->link));
			list_add_tail(&u->link, &gc_inflight_list);
		} else {
			BUG_ON(list_empty(&u->link));
		}
		/* Paired with READ_ONCE() in wait_for_unix_gc() */
		WRITE_ONCE(unix_tot_inflight, unix_tot_inflight + 1);
	}
	WRITE_ONCE(user->unix_inflight, user->unix_inflight + 1);
	spin_unlock(&unix_gc_lock);
}

void unix_notinflight(struct user_struct *user, struct file *fp)
{
	struct sock *s = unix_get_socket(fp);

	spin_lock(&unix_gc_lock);

	if (s) {
		struct unix_sock *u = unix_sk(s);

		BUG_ON(!atomic_long_read(&u->inflight));
		BUG_ON(list_empty(&u->link));

		if (atomic_long_dec_and_test(&u->inflight))
			list_del_init(&u->link);
		/* Paired with READ_ONCE() in wait_for_unix_gc() */
		WRITE_ONCE(unix_tot_inflight, unix_tot_inflight - 1);
	}
	WRITE_ONCE(user->unix_inflight, user->unix_inflight - 1);
	spin_unlock(&unix_gc_lock);
}

/*
 * The "user->unix_inflight" variable is protected by the garbage
 * collection lock, and we just read it locklessly here. If you go
 * over the limit, there might be a tiny race in actually noticing
 * it across threads. Tough.
 */
static inline bool too_many_unix_fds(struct task_struct *p)
{
	struct user_struct *user = current_user();

	if (unlikely(READ_ONCE(user->unix_inflight) > task_rlimit(p, RLIMIT_NOFILE)))
		return !capable(CAP_SYS_RESOURCE) && !capable(CAP_SYS_ADMIN);
	return false;
}

int unix_attach_fds(struct scm_cookie *scm, struct sk_buff *skb)
{
	int i;

	if (too_many_unix_fds(current))
		return -ETOOMANYREFS;

	/*
	 * Need to duplicate file references for the sake of garbage
	 * collection.  Otherwise a socket in the fps might become a
	 * candidate for GC while the skb is not yet queued.
	 */
	UNIXCB(skb).fp = scm_fp_dup(scm->fp);
	if (!UNIXCB(skb).fp)
		return -ENOMEM;

	for (i = scm->fp->count - 1; i >= 0; i--)
		unix_inflight(scm->fp->user, scm->fp->fp[i]);
	return 0;
}
EXPORT_SYMBOL(unix_attach_fds);

void unix_detach_fds(struct scm_cookie *scm, struct sk_buff *skb)
{
	int i;

	scm->fp = UNIXCB(skb).fp;
	UNIXCB(skb).fp = NULL;

	for (i = scm->fp->count-1; i >= 0; i--)
		unix_notinflight(scm->fp->user, scm->fp->fp[i]);
}
EXPORT_SYMBOL(unix_detach_fds);

void unix_destruct_scm(struct sk_buff *skb)
{
	struct scm_cookie scm;

	memset(&scm, 0, sizeof(scm));
	scm.pid  = UNIXCB(skb).pid;
	if (UNIXCB(skb).fp)
		unix_detach_fds(&scm, skb);

	/* Alas, it calls VFS */
	/* So fscking what? fput() had been SMP-safe since the last Summer */
	scm_destroy(&scm);
	sock_wfree(skb);
}
EXPORT_SYMBOL(unix_destruct_scm);

void io_uring_destruct_scm(struct sk_buff *skb)
{
	unix_destruct_scm(skb);
}
EXPORT_SYMBOL(io_uring_destruct_scm);
