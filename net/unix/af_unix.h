/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __AF_UNIX_H
#define __AF_UNIX_H

#include <linux/uidgid.h>

#define UNIX_HASH_MOD	(256 - 1)
#define UNIX_HASH_SIZE	(256 * 2)
#define UNIX_HASH_BITS	8

struct sock *unix_peer_get(struct sock *sk);

struct unix_skb_parms {
	struct pid		*pid;		/* skb credentials	*/
	kuid_t			uid;
	kgid_t			gid;
	struct scm_fp_list	*fp;		/* Passed files		*/
#ifdef CONFIG_SECURITY_NETWORK
	u32			secid;		/* Security ID		*/
#endif
	u32			consumed;
} __randomize_layout;

#define UNIXCB(skb)	(*(struct unix_skb_parms *)&((skb)->cb))

/* GC for SCM_RIGHTS */
extern unsigned int unix_tot_inflight;
void unix_add_edges(struct scm_fp_list *fpl, struct unix_sock *receiver);
void unix_del_edges(struct scm_fp_list *fpl);
void unix_update_edges(struct unix_sock *receiver);
int unix_prepare_fpl(struct scm_fp_list *fpl);
void unix_destroy_fpl(struct scm_fp_list *fpl);
void unix_gc(void);
void wait_for_unix_gc(struct scm_fp_list *fpl);

/* SOCK_DIAG */
long unix_inq_len(struct sock *sk);
long unix_outq_len(struct sock *sk);

/* sysctl */
#ifdef CONFIG_SYSCTL
int unix_sysctl_register(struct net *net);
void unix_sysctl_unregister(struct net *net);
#else
static inline int unix_sysctl_register(struct net *net)
{
	return 0;
}

static inline void unix_sysctl_unregister(struct net *net)
{
}
#endif

/* BPF SOCKMAP */
int __unix_dgram_recvmsg(struct sock *sk, struct msghdr *msg, size_t size, int flags);
int __unix_stream_recvmsg(struct sock *sk, struct msghdr *msg, size_t size, int flags);

#ifdef CONFIG_BPF_SYSCALL
extern struct proto unix_dgram_proto;
extern struct proto unix_stream_proto;

int unix_dgram_bpf_update_proto(struct sock *sk, struct sk_psock *psock, bool restore);
int unix_stream_bpf_update_proto(struct sock *sk, struct sk_psock *psock, bool restore);
void __init unix_bpf_build_proto(void);
#else
static inline void __init unix_bpf_build_proto(void)
{
}
#endif

#endif
