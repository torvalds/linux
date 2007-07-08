#ifndef __LINUX_NETFILTER_H
#define __LINUX_NETFILTER_H

#ifdef __KERNEL__
#include <linux/init.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/net.h>
#include <linux/if.h>
#include <linux/wait.h>
#include <linux/list.h>
#endif
#include <linux/compiler.h>

/* Responses from hook functions. */
#define NF_DROP 0
#define NF_ACCEPT 1
#define NF_STOLEN 2
#define NF_QUEUE 3
#define NF_REPEAT 4
#define NF_STOP 5
#define NF_MAX_VERDICT NF_STOP

/* we overload the higher bits for encoding auxiliary data such as the queue
 * number. Not nice, but better than additional function arguments. */
#define NF_VERDICT_MASK 0x0000ffff
#define NF_VERDICT_BITS 16

#define NF_VERDICT_QMASK 0xffff0000
#define NF_VERDICT_QBITS 16

#define NF_QUEUE_NR(x) (((x << NF_VERDICT_QBITS) & NF_VERDICT_QMASK) | NF_QUEUE)

/* only for userspace compatibility */
#ifndef __KERNEL__
/* Generic cache responses from hook functions.
   <= 0x2000 is used for protocol-flags. */
#define NFC_UNKNOWN 0x4000
#define NFC_ALTERED 0x8000
#endif

#ifdef __KERNEL__
#ifdef CONFIG_NETFILTER

extern void netfilter_init(void);

/* Largest hook number + 1 */
#define NF_MAX_HOOKS 8

struct sk_buff;
struct net_device;

typedef unsigned int nf_hookfn(unsigned int hooknum,
			       struct sk_buff **skb,
			       const struct net_device *in,
			       const struct net_device *out,
			       int (*okfn)(struct sk_buff *));

struct nf_hook_ops
{
	struct list_head list;

	/* User fills in from here down. */
	nf_hookfn *hook;
	struct module *owner;
	int pf;
	int hooknum;
	/* Hooks are ordered in ascending priority. */
	int priority;
};

struct nf_sockopt_ops
{
	struct list_head list;

	int pf;

	/* Non-inclusive ranges: use 0/0/NULL to never get called. */
	int set_optmin;
	int set_optmax;
	int (*set)(struct sock *sk, int optval, void __user *user, unsigned int len);
	int (*compat_set)(struct sock *sk, int optval,
			void __user *user, unsigned int len);

	int get_optmin;
	int get_optmax;
	int (*get)(struct sock *sk, int optval, void __user *user, int *len);
	int (*compat_get)(struct sock *sk, int optval,
			void __user *user, int *len);

	/* Number of users inside set() or get(). */
	unsigned int use;
	struct task_struct *cleanup_task;
};

/* Each queued (to userspace) skbuff has one of these. */
struct nf_info
{
	/* The ops struct which sent us to userspace. */
	struct nf_hook_ops *elem;
	
	/* If we're sent to userspace, this keeps housekeeping info */
	int pf;
	unsigned int hook;
	struct net_device *indev, *outdev;
	int (*okfn)(struct sk_buff *);
};
                                                                                
/* Function to register/unregister hook points. */
int nf_register_hook(struct nf_hook_ops *reg);
void nf_unregister_hook(struct nf_hook_ops *reg);
int nf_register_hooks(struct nf_hook_ops *reg, unsigned int n);
void nf_unregister_hooks(struct nf_hook_ops *reg, unsigned int n);

/* Functions to register get/setsockopt ranges (non-inclusive).  You
   need to check permissions yourself! */
int nf_register_sockopt(struct nf_sockopt_ops *reg);
void nf_unregister_sockopt(struct nf_sockopt_ops *reg);

#ifdef CONFIG_SYSCTL
/* Sysctl registration */
struct ctl_table_header *nf_register_sysctl_table(struct ctl_table *path,
						  struct ctl_table *table);
void nf_unregister_sysctl_table(struct ctl_table_header *header,
				struct ctl_table *table);
extern struct ctl_table nf_net_netfilter_sysctl_path[];
extern struct ctl_table nf_net_ipv4_netfilter_sysctl_path[];
#endif /* CONFIG_SYSCTL */

extern struct list_head nf_hooks[NPROTO][NF_MAX_HOOKS];

/* those NF_LOG_* defines and struct nf_loginfo are legacy definitios that will
 * disappear once iptables is replaced with pkttables.  Please DO NOT use them
 * for any new code! */
#define NF_LOG_TCPSEQ		0x01	/* Log TCP sequence numbers */
#define NF_LOG_TCPOPT		0x02	/* Log TCP options */
#define NF_LOG_IPOPT		0x04	/* Log IP options */
#define NF_LOG_UID		0x08	/* Log UID owning local socket */
#define NF_LOG_MASK		0x0f

#define NF_LOG_TYPE_LOG		0x01
#define NF_LOG_TYPE_ULOG	0x02

struct nf_loginfo {
	u_int8_t type;
	union {
		struct {
			u_int32_t copy_len;
			u_int16_t group;
			u_int16_t qthreshold;
		} ulog;
		struct {
			u_int8_t level;
			u_int8_t logflags;
		} log;
	} u;
};

typedef void nf_logfn(unsigned int pf,
		      unsigned int hooknum,
		      const struct sk_buff *skb,
		      const struct net_device *in,
		      const struct net_device *out,
		      const struct nf_loginfo *li,
		      const char *prefix);

struct nf_logger {
	struct module	*me;
	nf_logfn 	*logfn;
	char		*name;
};

/* Function to register/unregister log function. */
int nf_log_register(int pf, struct nf_logger *logger);
void nf_log_unregister(struct nf_logger *logger);
void nf_log_unregister_pf(int pf);

/* Calls the registered backend logging function */
void nf_log_packet(int pf,
		   unsigned int hooknum,
		   const struct sk_buff *skb,
		   const struct net_device *in,
		   const struct net_device *out,
		   struct nf_loginfo *li,
		   const char *fmt, ...);

int nf_hook_slow(int pf, unsigned int hook, struct sk_buff **pskb,
		 struct net_device *indev, struct net_device *outdev,
		 int (*okfn)(struct sk_buff *), int thresh);

/**
 *	nf_hook_thresh - call a netfilter hook
 *	
 *	Returns 1 if the hook has allowed the packet to pass.  The function
 *	okfn must be invoked by the caller in this case.  Any other return
 *	value indicates the packet has been consumed by the hook.
 */
static inline int nf_hook_thresh(int pf, unsigned int hook,
				 struct sk_buff **pskb,
				 struct net_device *indev,
				 struct net_device *outdev,
				 int (*okfn)(struct sk_buff *), int thresh,
				 int cond)
{
	if (!cond)
		return 1;
#ifndef CONFIG_NETFILTER_DEBUG
	if (list_empty(&nf_hooks[pf][hook]))
		return 1;
#endif
	return nf_hook_slow(pf, hook, pskb, indev, outdev, okfn, thresh);
}

static inline int nf_hook(int pf, unsigned int hook, struct sk_buff **pskb,
			  struct net_device *indev, struct net_device *outdev,
			  int (*okfn)(struct sk_buff *))
{
	return nf_hook_thresh(pf, hook, pskb, indev, outdev, okfn, INT_MIN, 1);
}
                   
/* Activate hook; either okfn or kfree_skb called, unless a hook
   returns NF_STOLEN (in which case, it's up to the hook to deal with
   the consequences).

   Returns -ERRNO if packet dropped.  Zero means queued, stolen or
   accepted.
*/

/* RR:
   > I don't want nf_hook to return anything because people might forget
   > about async and trust the return value to mean "packet was ok".

   AK:
   Just document it clearly, then you can expect some sense from kernel
   coders :)
*/

/* This is gross, but inline doesn't cut it for avoiding the function
   call in fast path: gcc doesn't inline (needs value tracking?). --RR */

/* HX: It's slightly less gross now. */

#define NF_HOOK_THRESH(pf, hook, skb, indev, outdev, okfn, thresh)	       \
({int __ret;								       \
if ((__ret=nf_hook_thresh(pf, hook, &(skb), indev, outdev, okfn, thresh, 1)) == 1)\
	__ret = (okfn)(skb);						       \
__ret;})

#define NF_HOOK_COND(pf, hook, skb, indev, outdev, okfn, cond)		       \
({int __ret;								       \
if ((__ret=nf_hook_thresh(pf, hook, &(skb), indev, outdev, okfn, INT_MIN, cond)) == 1)\
	__ret = (okfn)(skb);						       \
__ret;})

#define NF_HOOK(pf, hook, skb, indev, outdev, okfn) \
	NF_HOOK_THRESH(pf, hook, skb, indev, outdev, okfn, INT_MIN)

/* Call setsockopt() */
int nf_setsockopt(struct sock *sk, int pf, int optval, char __user *opt, 
		  int len);
int nf_getsockopt(struct sock *sk, int pf, int optval, char __user *opt,
		  int *len);

int compat_nf_setsockopt(struct sock *sk, int pf, int optval,
		char __user *opt, int len);
int compat_nf_getsockopt(struct sock *sk, int pf, int optval,
		char __user *opt, int *len);

/* Packet queuing */
struct nf_queue_handler {
	int (*outfn)(struct sk_buff *skb, struct nf_info *info,
		     unsigned int queuenum, void *data);
	void *data;
	char *name;
};
extern int nf_register_queue_handler(int pf, 
                                     struct nf_queue_handler *qh);
extern int nf_unregister_queue_handler(int pf,
				       struct nf_queue_handler *qh);
extern void nf_unregister_queue_handlers(struct nf_queue_handler *qh);
extern void nf_reinject(struct sk_buff *skb,
			struct nf_info *info,
			unsigned int verdict);

/* FIXME: Before cache is ever used, this must be implemented for real. */
extern void nf_invalidate_cache(int pf);

/* Call this before modifying an existing packet: ensures it is
   modifiable and linear to the point you care about (writable_len).
   Returns true or false. */
extern int skb_make_writable(struct sk_buff **pskb, unsigned int writable_len);

static inline void nf_csum_replace4(__sum16 *sum, __be32 from, __be32 to)
{
	__be32 diff[] = { ~from, to };

	*sum = csum_fold(csum_partial((char *)diff, sizeof(diff), ~csum_unfold(*sum)));
}

static inline void nf_csum_replace2(__sum16 *sum, __be16 from, __be16 to)
{
	nf_csum_replace4(sum, (__force __be32)from, (__force __be32)to);
}

extern void nf_proto_csum_replace4(__sum16 *sum, struct sk_buff *skb,
				      __be32 from, __be32 to, int pseudohdr);

static inline void nf_proto_csum_replace2(__sum16 *sum, struct sk_buff *skb,
				      __be16 from, __be16 to, int pseudohdr)
{
	nf_proto_csum_replace4(sum, skb, (__force __be32)from,
				(__force __be32)to, pseudohdr);
}

struct nf_afinfo {
	unsigned short	family;
	__sum16		(*checksum)(struct sk_buff *skb, unsigned int hook,
				    unsigned int dataoff, u_int8_t protocol);
	void		(*saveroute)(const struct sk_buff *skb,
				     struct nf_info *info);
	int		(*reroute)(struct sk_buff **skb,
				   const struct nf_info *info);
	int		route_key_size;
};

extern struct nf_afinfo *nf_afinfo[];
static inline struct nf_afinfo *nf_get_afinfo(unsigned short family)
{
	return rcu_dereference(nf_afinfo[family]);
}

static inline __sum16
nf_checksum(struct sk_buff *skb, unsigned int hook, unsigned int dataoff,
	    u_int8_t protocol, unsigned short family)
{
	struct nf_afinfo *afinfo;
	__sum16 csum = 0;

	rcu_read_lock();
	afinfo = nf_get_afinfo(family);
	if (afinfo)
		csum = afinfo->checksum(skb, hook, dataoff, protocol);
	rcu_read_unlock();
	return csum;
}

extern int nf_register_afinfo(struct nf_afinfo *afinfo);
extern void nf_unregister_afinfo(struct nf_afinfo *afinfo);

#define nf_info_reroute(x) ((void *)x + sizeof(struct nf_info))

#include <net/flow.h>
extern void (*ip_nat_decode_session)(struct sk_buff *, struct flowi *);

static inline void
nf_nat_decode_session(struct sk_buff *skb, struct flowi *fl, int family)
{
#if defined(CONFIG_IP_NF_NAT_NEEDED) || defined(CONFIG_NF_NAT_NEEDED)
	void (*decodefn)(struct sk_buff *, struct flowi *);

	if (family == AF_INET && (decodefn = ip_nat_decode_session) != NULL)
		decodefn(skb, fl);
#endif
}

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
extern struct proc_dir_entry *proc_net_netfilter;
#endif

#else /* !CONFIG_NETFILTER */
#define NF_HOOK(pf, hook, skb, indev, outdev, okfn) (okfn)(skb)
#define NF_HOOK_COND(pf, hook, skb, indev, outdev, okfn, cond) (okfn)(skb)
static inline int nf_hook_thresh(int pf, unsigned int hook,
				 struct sk_buff **pskb,
				 struct net_device *indev,
				 struct net_device *outdev,
				 int (*okfn)(struct sk_buff *), int thresh,
				 int cond)
{
	return okfn(*pskb);
}
static inline int nf_hook(int pf, unsigned int hook, struct sk_buff **pskb,
			  struct net_device *indev, struct net_device *outdev,
			  int (*okfn)(struct sk_buff *))
{
	return 1;
}
struct flowi;
static inline void
nf_nat_decode_session(struct sk_buff *skb, struct flowi *fl, int family) {}
#endif /*CONFIG_NETFILTER*/

#if defined(CONFIG_NF_CONNTRACK) || defined(CONFIG_NF_CONNTRACK_MODULE)
extern void (*ip_ct_attach)(struct sk_buff *, struct sk_buff *);
extern void nf_ct_attach(struct sk_buff *, struct sk_buff *);
extern void (*nf_ct_destroy)(struct nf_conntrack *);
#else
static inline void nf_ct_attach(struct sk_buff *new, struct sk_buff *skb) {}
#endif

#endif /*__KERNEL__*/
#endif /*__LINUX_NETFILTER_H*/
