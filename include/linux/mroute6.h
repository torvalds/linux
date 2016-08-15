#ifndef __LINUX_MROUTE6_H
#define __LINUX_MROUTE6_H


#include <linux/pim.h>
#include <linux/skbuff.h>	/* for struct sk_buff_head */
#include <net/net_namespace.h>
#include <uapi/linux/mroute6.h>

#ifdef CONFIG_IPV6_MROUTE
static inline int ip6_mroute_opt(int opt)
{
	return (opt >= MRT6_BASE) && (opt <= MRT6_MAX);
}
#else
static inline int ip6_mroute_opt(int opt)
{
	return 0;
}
#endif

struct sock;

#ifdef CONFIG_IPV6_MROUTE
extern int ip6_mroute_setsockopt(struct sock *, int, char __user *, unsigned int);
extern int ip6_mroute_getsockopt(struct sock *, int, char __user *, int __user *);
extern int ip6_mr_input(struct sk_buff *skb);
extern int ip6mr_ioctl(struct sock *sk, int cmd, void __user *arg);
extern int ip6mr_compat_ioctl(struct sock *sk, unsigned int cmd, void __user *arg);
extern int ip6_mr_init(void);
extern void ip6_mr_cleanup(void);
#else
static inline
int ip6_mroute_setsockopt(struct sock *sock,
			  int optname, char __user *optval, unsigned int optlen)
{
	return -ENOPROTOOPT;
}

static inline
int ip6_mroute_getsockopt(struct sock *sock,
			  int optname, char __user *optval, int __user *optlen)
{
	return -ENOPROTOOPT;
}

static inline
int ip6mr_ioctl(struct sock *sk, int cmd, void __user *arg)
{
	return -ENOIOCTLCMD;
}

static inline int ip6_mr_init(void)
{
	return 0;
}

static inline void ip6_mr_cleanup(void)
{
	return;
}
#endif

struct mif_device {
	struct net_device 	*dev;			/* Device we are using */
	unsigned long	bytes_in,bytes_out;
	unsigned long	pkt_in,pkt_out;		/* Statistics 			*/
	unsigned long	rate_limit;		/* Traffic shaping (NI) 	*/
	unsigned char	threshold;		/* TTL threshold 		*/
	unsigned short	flags;			/* Control flags 		*/
	int		link;			/* Physical interface index	*/
};

#define VIFF_STATIC 0x8000

struct mfc6_cache {
	struct list_head list;
	struct in6_addr mf6c_mcastgrp;			/* Group the entry belongs to 	*/
	struct in6_addr mf6c_origin;			/* Source of packet 		*/
	mifi_t mf6c_parent;			/* Source interface		*/
	int mfc_flags;				/* Flags on line		*/

	union {
		struct {
			unsigned long expires;
			struct sk_buff_head unresolved;	/* Unresolved buffers		*/
		} unres;
		struct {
			unsigned long last_assert;
			int minvif;
			int maxvif;
			unsigned long bytes;
			unsigned long pkt;
			unsigned long wrong_if;
			unsigned long lastuse;
			unsigned char ttls[MAXMIFS];	/* TTL thresholds		*/
		} res;
	} mfc_un;
};

#define MFC_STATIC		1
#define MFC_NOTIFY		2

#define MFC6_LINES		64

#define MFC6_HASH(a, g) (((__force u32)(a)->s6_addr32[0] ^ \
			  (__force u32)(a)->s6_addr32[1] ^ \
			  (__force u32)(a)->s6_addr32[2] ^ \
			  (__force u32)(a)->s6_addr32[3] ^ \
			  (__force u32)(g)->s6_addr32[0] ^ \
			  (__force u32)(g)->s6_addr32[1] ^ \
			  (__force u32)(g)->s6_addr32[2] ^ \
			  (__force u32)(g)->s6_addr32[3]) % MFC6_LINES)

#define MFC_ASSERT_THRESH (3*HZ)		/* Maximal freq. of asserts */

struct rtmsg;
extern int ip6mr_get_route(struct net *net, struct sk_buff *skb,
			   struct rtmsg *rtm, int nowait);

#ifdef CONFIG_IPV6_MROUTE
extern struct sock *mroute6_socket(struct net *net, struct sk_buff *skb);
extern int ip6mr_sk_done(struct sock *sk);
#else
static inline struct sock *mroute6_socket(struct net *net, struct sk_buff *skb)
{
	return NULL;
}
static inline int ip6mr_sk_done(struct sock *sk)
{
	return 0;
}
#endif
#endif
