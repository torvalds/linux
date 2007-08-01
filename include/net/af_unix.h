#ifndef __LINUX_NET_AFUNIX_H
#define __LINUX_NET_AFUNIX_H

#include <linux/socket.h>
#include <linux/un.h>
#include <linux/mutex.h>
#include <net/sock.h>

extern void unix_inflight(struct file *fp);
extern void unix_notinflight(struct file *fp);
extern void unix_gc(void);

#define UNIX_HASH_SIZE	256

extern atomic_t unix_tot_inflight;

struct unix_address {
	atomic_t	refcnt;
	int		len;
	unsigned	hash;
	struct sockaddr_un name[0];
};

struct unix_skb_parms {
	struct ucred		creds;		/* Skb credentials	*/
	struct scm_fp_list	*fp;		/* Passed files		*/
#ifdef CONFIG_SECURITY_NETWORK
	u32			secid;		/* Security ID		*/
#endif
};

#define UNIXCB(skb) 	(*(struct unix_skb_parms*)&((skb)->cb))
#define UNIXCREDS(skb)	(&UNIXCB((skb)).creds)
#define UNIXSID(skb)	(&UNIXCB((skb)).secid)

#define unix_state_lock(s)	spin_lock(&unix_sk(s)->lock)
#define unix_state_unlock(s)	spin_unlock(&unix_sk(s)->lock)
#define unix_state_lock_nested(s) \
				spin_lock_nested(&unix_sk(s)->lock, \
				SINGLE_DEPTH_NESTING)

#ifdef __KERNEL__
/* The AF_UNIX socket */
struct unix_sock {
	/* WARNING: sk has to be the first member */
	struct sock		sk;
        struct unix_address     *addr;
        struct dentry		*dentry;
        struct vfsmount		*mnt;
	struct mutex		readlock;
        struct sock		*peer;
        struct sock		*other;
	struct list_head	link;
        atomic_t                inflight;
        spinlock_t		lock;
	unsigned int		gc_candidate : 1;
        wait_queue_head_t       peer_wait;
};
#define unix_sk(__sk) ((struct unix_sock *)__sk)

#ifdef CONFIG_SYSCTL
extern int sysctl_unix_max_dgram_qlen;
extern void unix_sysctl_register(void);
extern void unix_sysctl_unregister(void);
#else
static inline void unix_sysctl_register(void) {}
static inline void unix_sysctl_unregister(void) {}
#endif
#endif
#endif
