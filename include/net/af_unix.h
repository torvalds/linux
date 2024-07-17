/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_NET_AFUNIX_H
#define __LINUX_NET_AFUNIX_H

#include <linux/socket.h>
#include <linux/un.h>
#include <linux/mutex.h>
#include <linux/refcount.h>
#include <net/sock.h>

#if IS_ENABLED(CONFIG_UNIX)
struct unix_sock *unix_get_socket(struct file *filp);
#else
static inline struct unix_sock *unix_get_socket(struct file *filp)
{
	return NULL;
}
#endif

extern unsigned int unix_tot_inflight;
void unix_add_edges(struct scm_fp_list *fpl, struct unix_sock *receiver);
void unix_del_edges(struct scm_fp_list *fpl);
void unix_update_edges(struct unix_sock *receiver);
int unix_prepare_fpl(struct scm_fp_list *fpl);
void unix_destroy_fpl(struct scm_fp_list *fpl);
void unix_gc(void);
void wait_for_unix_gc(struct scm_fp_list *fpl);

struct unix_vertex {
	struct list_head edges;
	struct list_head entry;
	struct list_head scc_entry;
	unsigned long out_degree;
	unsigned long index;
	unsigned long scc_index;
};

struct unix_edge {
	struct unix_sock *predecessor;
	struct unix_sock *successor;
	struct list_head vertex_entry;
	struct list_head stack_entry;
};

struct sock *unix_peer_get(struct sock *sk);

#define UNIX_HASH_MOD	(256 - 1)
#define UNIX_HASH_SIZE	(256 * 2)
#define UNIX_HASH_BITS	8

struct unix_address {
	refcount_t	refcnt;
	int		len;
	struct sockaddr_un name[];
};

struct unix_skb_parms {
	struct pid		*pid;		/* Skb credentials	*/
	kuid_t			uid;
	kgid_t			gid;
	struct scm_fp_list	*fp;		/* Passed files		*/
#ifdef CONFIG_SECURITY_NETWORK
	u32			secid;		/* Security ID		*/
#endif
	u32			consumed;
} __randomize_layout;

struct scm_stat {
	atomic_t nr_fds;
	unsigned long nr_unix_fds;
};

#define UNIXCB(skb)	(*(struct unix_skb_parms *)&((skb)->cb))

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
	wait_queue_entry_t	peer_wake;
	struct scm_stat		scm_stat;
#if IS_ENABLED(CONFIG_AF_UNIX_OOB)
	struct sk_buff		*oob_skb;
#endif
};

#define unix_sk(ptr) container_of_const(ptr, struct unix_sock, sk)
#define unix_peer(sk) (unix_sk(sk)->peer)

#define unix_state_lock(s)	spin_lock(&unix_sk(s)->lock)
#define unix_state_unlock(s)	spin_unlock(&unix_sk(s)->lock)

#define peer_wait peer_wq.wait

long unix_inq_len(struct sock *sk);
long unix_outq_len(struct sock *sk);

int __unix_dgram_recvmsg(struct sock *sk, struct msghdr *msg, size_t size,
			 int flags);
int __unix_stream_recvmsg(struct sock *sk, struct msghdr *msg, size_t size,
			  int flags);
#ifdef CONFIG_SYSCTL
int unix_sysctl_register(struct net *net);
void unix_sysctl_unregister(struct net *net);
#else
static inline int unix_sysctl_register(struct net *net) { return 0; }
static inline void unix_sysctl_unregister(struct net *net) {}
#endif

#ifdef CONFIG_BPF_SYSCALL
extern struct proto unix_dgram_proto;
extern struct proto unix_stream_proto;

int unix_dgram_bpf_update_proto(struct sock *sk, struct sk_psock *psock, bool restore);
int unix_stream_bpf_update_proto(struct sock *sk, struct sk_psock *psock, bool restore);
void __init unix_bpf_build_proto(void);
#else
static inline void __init unix_bpf_build_proto(void)
{}
#endif
#endif
