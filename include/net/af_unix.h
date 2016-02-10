#ifndef __LINUX_NET_AFUNIX_H
#define __LINUX_NET_AFUNIX_H

#include <linux/socket.h>
#include <linux/un.h>
#include <linux/mutex.h>
#include <net/sock.h>

extern void unix_inflight(struct file *fp);
extern void unix_notinflight(struct file *fp);
extern void unix_gc(void);
extern void wait_for_unix_gc(void);
extern struct sock *unix_get_socket(struct file *filp);
extern struct sock *unix_peer_get(struct sock *);

#define UNIX_HASH_SIZE	256
#define UNIX_HASH_BITS	8

extern unsigned int unix_tot_inflight;
extern spinlock_t unix_table_lock;
extern struct hlist_head unix_socket_table[2 * UNIX_HASH_SIZE];

struct unix_address {
	atomic_t	refcnt;
	int		len;
	unsigned int	hash;
	struct sockaddr_un name[0];
};

struct unix_skb_parms {
	struct pid		*pid;		/* Skb credentials	*/
	kuid_t			uid;
	kgid_t			gid;
	struct scm_fp_list	*fp;		/* Passed files		*/
#ifdef CONFIG_SECURITY_NETWORK
	u32			secid;		/* Security ID		*/
#endif
};

#define UNIXCB(skb) 	(*(struct unix_skb_parms *)&((skb)->cb))
#define UNIXSID(skb)	(&UNIXCB((skb)).secid)

#define unix_state_lock(s)	spin_lock(&unix_sk(s)->lock)
#define unix_state_unlock(s)	spin_unlock(&unix_sk(s)->lock)
#define unix_state_lock_nested(s) \
				spin_lock_nested(&unix_sk(s)->lock, \
				SINGLE_DEPTH_NESTING)

/* The AF_UNIX socket */
struct unix_sock {
	/* WARNING: sk has to be the first member */
	struct sock		sk;
	struct unix_address     *addr;
	struct path		path;
	struct mutex		readlock;
	struct sock		*peer;
	struct list_head	link;
	atomic_long_t		inflight;
	spinlock_t		lock;
	unsigned char		recursion_level;
	unsigned long		gc_flags;
#define UNIX_GC_CANDIDATE	0
#define UNIX_GC_MAYBE_CYCLE	1
	struct socket_wq	peer_wq;
	wait_queue_t		peer_wake;
};

static inline struct unix_sock *unix_sk(struct sock *sk)
{
	return (struct unix_sock *)sk;
}

#define peer_wait peer_wq.wait

long unix_inq_len(struct sock *sk);
long unix_outq_len(struct sock *sk);

#ifdef CONFIG_SYSCTL
extern int unix_sysctl_register(struct net *net);
extern void unix_sysctl_unregister(struct net *net);
#else
static inline int unix_sysctl_register(struct net *net) { return 0; }
static inline void unix_sysctl_unregister(struct net *net) {}
#endif
#endif
