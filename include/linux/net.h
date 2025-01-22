/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * NET		An implementation of the SOCKET network access protocol.
 *		This is the master header file for the Linux NET layer,
 *		or, in plain English: the networking handling part of the
 *		kernel.
 *
 * Version:	@(#)net.h	1.0.3	05/25/93
 *
 * Authors:	Orest Zborowski, <obz@Kodak.COM>
 *		Ross Biro
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 */
#ifndef _LINUX_NET_H
#define _LINUX_NET_H

#include <linux/stringify.h>
#include <linux/random.h>
#include <linux/wait.h>
#include <linux/fcntl.h>	/* For O_CLOEXEC and O_NONBLOCK */
#include <linux/rcupdate.h>
#include <linux/once.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/sockptr.h>

#include <uapi/linux/net.h>

struct poll_table_struct;
struct pipe_inode_info;
struct inode;
struct file;
struct net;

/* Historically, SOCKWQ_ASYNC_NOSPACE & SOCKWQ_ASYNC_WAITDATA were located
 * in sock->flags, but moved into sk->sk_wq->flags to be RCU protected.
 * Eventually all flags will be in sk->sk_wq->flags.
 */
#define SOCKWQ_ASYNC_NOSPACE	0
#define SOCKWQ_ASYNC_WAITDATA	1
#define SOCK_NOSPACE		2
#define SOCK_PASSCRED		3
#define SOCK_PASSSEC		4
#define SOCK_SUPPORT_ZC		5
#define SOCK_CUSTOM_SOCKOPT	6
#define SOCK_PASSPIDFD		7

#ifndef ARCH_HAS_SOCKET_TYPES
/**
 * enum sock_type - Socket types
 * @SOCK_STREAM: stream (connection) socket
 * @SOCK_DGRAM: datagram (conn.less) socket
 * @SOCK_RAW: raw socket
 * @SOCK_RDM: reliably-delivered message
 * @SOCK_SEQPACKET: sequential packet socket
 * @SOCK_DCCP: Datagram Congestion Control Protocol socket
 * @SOCK_PACKET: linux specific way of getting packets at the dev level.
 *		  For writing rarp and other similar things on the user level.
 *
 * When adding some new socket type please
 * grep ARCH_HAS_SOCKET_TYPE include/asm-* /socket.h, at least MIPS
 * overrides this enum for binary compat reasons.
 */
enum sock_type {
	SOCK_STREAM	= 1,
	SOCK_DGRAM	= 2,
	SOCK_RAW	= 3,
	SOCK_RDM	= 4,
	SOCK_SEQPACKET	= 5,
	SOCK_DCCP	= 6,
	SOCK_PACKET	= 10,
};

#define SOCK_MAX (SOCK_PACKET + 1)
/* Mask which covers at least up to SOCK_MASK-1.  The
 * remaining bits are used as flags. */
#define SOCK_TYPE_MASK 0xf

/* Flags for socket, socketpair, accept4 */
#define SOCK_CLOEXEC	O_CLOEXEC
#ifndef SOCK_NONBLOCK
#define SOCK_NONBLOCK	O_NONBLOCK
#endif

#endif /* ARCH_HAS_SOCKET_TYPES */

/**
 * enum sock_shutdown_cmd - Shutdown types
 * @SHUT_RD: shutdown receptions
 * @SHUT_WR: shutdown transmissions
 * @SHUT_RDWR: shutdown receptions/transmissions
 */
enum sock_shutdown_cmd {
	SHUT_RD,
	SHUT_WR,
	SHUT_RDWR,
};

struct socket_wq {
	/* Note: wait MUST be first field of socket_wq */
	wait_queue_head_t	wait;
	struct fasync_struct	*fasync_list;
	unsigned long		flags; /* %SOCKWQ_ASYNC_NOSPACE, etc */
	struct rcu_head		rcu;
} ____cacheline_aligned_in_smp;

/**
 *  struct socket - general BSD socket
 *  @state: socket state (%SS_CONNECTED, etc)
 *  @type: socket type (%SOCK_STREAM, etc)
 *  @flags: socket flags (%SOCK_NOSPACE, etc)
 *  @ops: protocol specific socket operations
 *  @file: File back pointer for gc
 *  @sk: internal networking protocol agnostic socket representation
 *  @wq: wait queue for several uses
 */
struct socket {
	socket_state		state;

	short			type;

	unsigned long		flags;

	struct file		*file;
	struct sock		*sk;
	const struct proto_ops	*ops; /* Might change with IPV6_ADDRFORM or MPTCP. */

	struct socket_wq	wq;
};

/*
 * "descriptor" for what we're up to with a read.
 * This allows us to use the same read code yet
 * have multiple different users of the data that
 * we read from a file.
 *
 * The simplest case just copies the data to user
 * mode.
 */
typedef struct {
	size_t written;
	size_t count;
	union {
		char __user *buf;
		void *data;
	} arg;
	int error;
} read_descriptor_t;

struct vm_area_struct;
struct page;
struct sockaddr;
struct msghdr;
struct module;
struct sk_buff;
struct proto_accept_arg;
typedef int (*sk_read_actor_t)(read_descriptor_t *, struct sk_buff *,
			       unsigned int, size_t);
typedef int (*skb_read_actor_t)(struct sock *, struct sk_buff *);


struct proto_ops {
	int		family;
	struct module	*owner;
	int		(*release)   (struct socket *sock);
	int		(*bind)	     (struct socket *sock,
				      struct sockaddr *myaddr,
				      int sockaddr_len);
	int		(*connect)   (struct socket *sock,
				      struct sockaddr *vaddr,
				      int sockaddr_len, int flags);
	int		(*socketpair)(struct socket *sock1,
				      struct socket *sock2);
	int		(*accept)    (struct socket *sock,
				      struct socket *newsock,
				      struct proto_accept_arg *arg);
	int		(*getname)   (struct socket *sock,
				      struct sockaddr *addr,
				      int peer);
	__poll_t	(*poll)	     (struct file *file, struct socket *sock,
				      struct poll_table_struct *wait);
	int		(*ioctl)     (struct socket *sock, unsigned int cmd,
				      unsigned long arg);
#ifdef CONFIG_COMPAT
	int	 	(*compat_ioctl) (struct socket *sock, unsigned int cmd,
				      unsigned long arg);
#endif
	int		(*gettstamp) (struct socket *sock, void __user *userstamp,
				      bool timeval, bool time32);
	int		(*listen)    (struct socket *sock, int len);
	int		(*shutdown)  (struct socket *sock, int flags);
	int		(*setsockopt)(struct socket *sock, int level,
				      int optname, sockptr_t optval,
				      unsigned int optlen);
	int		(*getsockopt)(struct socket *sock, int level,
				      int optname, char __user *optval, int __user *optlen);
	void		(*show_fdinfo)(struct seq_file *m, struct socket *sock);
	int		(*sendmsg)   (struct socket *sock, struct msghdr *m,
				      size_t total_len);
	/* Notes for implementing recvmsg:
	 * ===============================
	 * msg->msg_namelen should get updated by the recvmsg handlers
	 * iff msg_name != NULL. It is by default 0 to prevent
	 * returning uninitialized memory to user space.  The recvfrom
	 * handlers can assume that msg.msg_name is either NULL or has
	 * a minimum size of sizeof(struct sockaddr_storage).
	 */
	int		(*recvmsg)   (struct socket *sock, struct msghdr *m,
				      size_t total_len, int flags);
	int		(*mmap)	     (struct file *file, struct socket *sock,
				      struct vm_area_struct * vma);
	ssize_t 	(*splice_read)(struct socket *sock,  loff_t *ppos,
				       struct pipe_inode_info *pipe, size_t len, unsigned int flags);
	void		(*splice_eof)(struct socket *sock);
	int		(*set_peek_off)(struct sock *sk, int val);
	int		(*peek_len)(struct socket *sock);

	/* The following functions are called internally by kernel with
	 * sock lock already held.
	 */
	int		(*read_sock)(struct sock *sk, read_descriptor_t *desc,
				     sk_read_actor_t recv_actor);
	/* This is different from read_sock(), it reads an entire skb at a time. */
	int		(*read_skb)(struct sock *sk, skb_read_actor_t recv_actor);
	int		(*sendmsg_locked)(struct sock *sk, struct msghdr *msg,
					  size_t size);
	int		(*set_rcvlowat)(struct sock *sk, int val);
};

#define DECLARE_SOCKADDR(type, dst, src)	\
	type dst = ({ __sockaddr_check_size(sizeof(*dst)); (type) src; })

struct net_proto_family {
	int		family;
	int		(*create)(struct net *net, struct socket *sock,
				  int protocol, int kern);
	struct module	*owner;
};

struct iovec;
struct kvec;

enum {
	SOCK_WAKE_IO,
	SOCK_WAKE_WAITD,
	SOCK_WAKE_SPACE,
	SOCK_WAKE_URG,
};

int sock_wake_async(struct socket_wq *sk_wq, int how, int band);
int sock_register(const struct net_proto_family *fam);
void sock_unregister(int family);
bool sock_is_registered(int family);
int __sock_create(struct net *net, int family, int type, int proto,
		  struct socket **res, int kern);
int sock_create(int family, int type, int proto, struct socket **res);
int sock_create_kern(struct net *net, int family, int type, int proto, struct socket **res);
int sock_create_lite(int family, int type, int proto, struct socket **res);
struct socket *sock_alloc(void);
void sock_release(struct socket *sock);
int sock_sendmsg(struct socket *sock, struct msghdr *msg);
int sock_recvmsg(struct socket *sock, struct msghdr *msg, int flags);
struct file *sock_alloc_file(struct socket *sock, int flags, const char *dname);
struct socket *sockfd_lookup(int fd, int *err);
struct socket *sock_from_file(struct file *file);
#define		     sockfd_put(sock) fput(sock->file)
int net_ratelimit(void);

#define net_ratelimited_function(function, ...)			\
do {								\
	if (net_ratelimit())					\
		function(__VA_ARGS__);				\
} while (0)

#define net_emerg_ratelimited(fmt, ...)				\
	net_ratelimited_function(pr_emerg, fmt, ##__VA_ARGS__)
#define net_alert_ratelimited(fmt, ...)				\
	net_ratelimited_function(pr_alert, fmt, ##__VA_ARGS__)
#define net_crit_ratelimited(fmt, ...)				\
	net_ratelimited_function(pr_crit, fmt, ##__VA_ARGS__)
#define net_err_ratelimited(fmt, ...)				\
	net_ratelimited_function(pr_err, fmt, ##__VA_ARGS__)
#define net_notice_ratelimited(fmt, ...)			\
	net_ratelimited_function(pr_notice, fmt, ##__VA_ARGS__)
#define net_warn_ratelimited(fmt, ...)				\
	net_ratelimited_function(pr_warn, fmt, ##__VA_ARGS__)
#define net_info_ratelimited(fmt, ...)				\
	net_ratelimited_function(pr_info, fmt, ##__VA_ARGS__)
#if defined(CONFIG_DYNAMIC_DEBUG) || \
	(defined(CONFIG_DYNAMIC_DEBUG_CORE) && defined(DYNAMIC_DEBUG_MODULE))
#define net_dbg_ratelimited(fmt, ...)					\
do {									\
	DEFINE_DYNAMIC_DEBUG_METADATA(descriptor, fmt);			\
	if (DYNAMIC_DEBUG_BRANCH(descriptor) &&				\
	    net_ratelimit())						\
		__dynamic_pr_debug(&descriptor, pr_fmt(fmt),		\
		                   ##__VA_ARGS__);			\
} while (0)
#elif defined(DEBUG)
#define net_dbg_ratelimited(fmt, ...)				\
	net_ratelimited_function(pr_debug, fmt, ##__VA_ARGS__)
#else
#define net_dbg_ratelimited(fmt, ...)				\
	no_printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)
#endif

#define net_get_random_once(buf, nbytes)			\
	get_random_once((buf), (nbytes))

/*
 * E.g. XFS meta- & log-data is in slab pages, or bcache meta
 * data pages, or other high order pages allocated by
 * __get_free_pages() without __GFP_COMP, which have a page_count
 * of 0 and/or have PageSlab() set. We cannot use send_page for
 * those, as that does get_page(); put_page(); and would cause
 * either a VM_BUG directly, or __page_cache_release a page that
 * would actually still be referenced by someone, leading to some
 * obscure delayed Oops somewhere else.
 */
static inline bool sendpage_ok(struct page *page)
{
	return !PageSlab(page) && page_count(page) >= 1;
}

/*
 * Check sendpage_ok on contiguous pages.
 */
static inline bool sendpages_ok(struct page *page, size_t len, size_t offset)
{
	struct page *p = page + (offset >> PAGE_SHIFT);
	size_t count = 0;

	while (count < len) {
		if (!sendpage_ok(p))
			return false;

		p++;
		count += PAGE_SIZE;
	}

	return true;
}

int kernel_sendmsg(struct socket *sock, struct msghdr *msg, struct kvec *vec,
		   size_t num, size_t len);
int kernel_recvmsg(struct socket *sock, struct msghdr *msg, struct kvec *vec,
		   size_t num, size_t len, int flags);

int kernel_bind(struct socket *sock, struct sockaddr *addr, int addrlen);
int kernel_listen(struct socket *sock, int backlog);
int kernel_accept(struct socket *sock, struct socket **newsock, int flags);
int kernel_connect(struct socket *sock, struct sockaddr *addr, int addrlen,
		   int flags);
int kernel_getsockname(struct socket *sock, struct sockaddr *addr);
int kernel_getpeername(struct socket *sock, struct sockaddr *addr);
int kernel_sock_shutdown(struct socket *sock, enum sock_shutdown_cmd how);

/* Routine returns the IP overhead imposed by a (caller-protected) socket. */
u32 kernel_sock_ip_overhead(struct sock *sk);

#define MODULE_ALIAS_NETPROTO(proto) \
	MODULE_ALIAS("net-pf-" __stringify(proto))

#define MODULE_ALIAS_NET_PF_PROTO(pf, proto) \
	MODULE_ALIAS("net-pf-" __stringify(pf) "-proto-" __stringify(proto))

#define MODULE_ALIAS_NET_PF_PROTO_TYPE(pf, proto, type) \
	MODULE_ALIAS("net-pf-" __stringify(pf) "-proto-" __stringify(proto) \
		     "-type-" __stringify(type))

#define MODULE_ALIAS_NET_PF_PROTO_NAME(pf, proto, name) \
	MODULE_ALIAS("net-pf-" __stringify(pf) "-proto-" __stringify(proto) \
		     name)
#endif	/* _LINUX_NET_H */
