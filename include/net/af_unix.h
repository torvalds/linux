/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_NET_AFUNIX_H
#define __LINUX_NET_AFUNIX_H

#include <linux/atomic.h>
#include <linux/mutex.h>
#include <linux/net.h>
#include <linux/path.h>
#include <linux/refcount.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <net/sock.h>
#include <uapi/linux/un.h>

#if IS_ENABLED(CONFIG_UNIX)
struct unix_sock *unix_get_socket(struct file *filp);
#else
static inline struct unix_sock *unix_get_socket(struct file *filp)
{
	return NULL;
}
#endif

struct unix_address {
	refcount_t	refcnt;
	int		len;
	struct sockaddr_un name[];
};

struct scm_stat {
	atomic_t nr_fds;
	unsigned long nr_unix_fds;
};

/* The AF_UNIX socket */
struct unix_sock {
	/* WARNING: sk has to be the first member */
	struct sock		sk;
	struct unix_address	*addr;
	struct path		path;
	struct mutex		iolock, bindlock;
	struct sock		*peer;
	struct sock		*listener;
	struct unix_vertex	*vertex;
	spinlock_t		lock;
	struct socket_wq	peer_wq;
#define peer_wait		peer_wq.wait
	wait_queue_entry_t	peer_wake;
	struct scm_stat		scm_stat;
	int			inq_len;
	bool			recvmsg_inq;
#if IS_ENABLED(CONFIG_AF_UNIX_OOB)
	struct sk_buff		*oob_skb;
#endif
};

#define unix_sk(ptr) container_of_const(ptr, struct unix_sock, sk)
#define unix_peer(sk) (unix_sk(sk)->peer)

#define unix_state_lock(s)	spin_lock(&unix_sk(s)->lock)
#define unix_state_unlock(s)	spin_unlock(&unix_sk(s)->lock)

#endif
