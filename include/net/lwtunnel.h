#ifndef __NET_LWTUNNEL_H
#define __NET_LWTUNNEL_H 1

#include <linux/lwtunnel.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/types.h>
#include <net/route.h>

#define LWTUNNEL_HASH_BITS   7
#define LWTUNNEL_HASH_SIZE   (1 << LWTUNNEL_HASH_BITS)

/* lw tunnel state flags */
#define LWTUNNEL_STATE_OUTPUT_REDIRECT 0x1

struct lwtunnel_state {
	__u16		type;
	__u16		flags;
	atomic_t	refcnt;
	int             len;
	__u8            data[0];
};

struct lwtunnel_encap_ops {
	int (*build_state)(struct net_device *dev, struct nlattr *encap,
			   struct lwtunnel_state **ts);
	int (*output)(struct sock *sk, struct sk_buff *skb);
	int (*fill_encap)(struct sk_buff *skb,
			  struct lwtunnel_state *lwtstate);
	int (*get_encap_size)(struct lwtunnel_state *lwtstate);
	int (*cmp_encap)(struct lwtunnel_state *a, struct lwtunnel_state *b);
};

extern const struct lwtunnel_encap_ops __rcu *
		lwtun_encaps[LWTUNNEL_ENCAP_MAX+1];

#ifdef CONFIG_LWTUNNEL
static inline void lwtunnel_state_get(struct lwtunnel_state *lws)
{
	atomic_inc(&lws->refcnt);
}

static inline void lwtunnel_state_put(struct lwtunnel_state *lws)
{
	if (!lws)
		return;

	if (atomic_dec_and_test(&lws->refcnt))
		kfree(lws);
}

static inline bool lwtunnel_output_redirect(struct lwtunnel_state *lwtstate)
{
	if (lwtstate && (lwtstate->flags & LWTUNNEL_STATE_OUTPUT_REDIRECT))
		return true;

	return false;
}

int lwtunnel_encap_add_ops(const struct lwtunnel_encap_ops *op,
			   unsigned int num);
int lwtunnel_encap_del_ops(const struct lwtunnel_encap_ops *op,
			   unsigned int num);
int lwtunnel_build_state(struct net_device *dev, u16 encap_type,
			 struct nlattr *encap,
			 struct lwtunnel_state **lws);
int lwtunnel_fill_encap(struct sk_buff *skb,
			struct lwtunnel_state *lwtstate);
int lwtunnel_get_encap_size(struct lwtunnel_state *lwtstate);
struct lwtunnel_state *lwtunnel_state_alloc(int hdr_len);
int lwtunnel_cmp_encap(struct lwtunnel_state *a, struct lwtunnel_state *b);
int lwtunnel_output(struct sock *sk, struct sk_buff *skb);
int lwtunnel_output6(struct sock *sk, struct sk_buff *skb);

#else

static inline void lwtunnel_state_get(struct lwtunnel_state *lws)
{
}

static inline void lwtunnel_state_put(struct lwtunnel_state *lws)
{
}

static inline bool lwtunnel_output_redirect(struct lwtunnel_state *lwtstate)
{
	return false;
}

static inline int lwtunnel_encap_add_ops(const struct lwtunnel_encap_ops *op,
					 unsigned int num)
{
	return -EOPNOTSUPP;

}

static inline int lwtunnel_encap_del_ops(const struct lwtunnel_encap_ops *op,
					 unsigned int num)
{
	return -EOPNOTSUPP;
}

static inline int lwtunnel_build_state(struct net_device *dev, u16 encap_type,
				       struct nlattr *encap,
				       struct lwtunnel_state **lws)
{
	return -EOPNOTSUPP;
}

static inline int lwtunnel_fill_encap(struct sk_buff *skb,
				      struct lwtunnel_state *lwtstate)
{
	return 0;
}

static inline int lwtunnel_get_encap_size(struct lwtunnel_state *lwtstate)
{
	return 0;
}

static inline struct lwtunnel_state *lwtunnel_state_alloc(int hdr_len)
{
	return NULL;
}

static inline int lwtunnel_cmp_encap(struct lwtunnel_state *a,
				     struct lwtunnel_state *b)
{
	return 0;
}

static inline int lwtunnel_output(struct sock *sk, struct sk_buff *skb)
{
	return -EOPNOTSUPP;
}

static inline int lwtunnel_output6(struct sock *sk, struct sk_buff *skb)
{
	return -EOPNOTSUPP;
}

#endif

#endif /* __NET_LWTUNNEL_H */
