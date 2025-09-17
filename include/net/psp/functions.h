/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __NET_PSP_HELPERS_H
#define __NET_PSP_HELPERS_H

#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/psp/types.h>

struct inet_timewait_sock;

/* Driver-facing API */
struct psp_dev *
psp_dev_create(struct net_device *netdev, struct psp_dev_ops *psd_ops,
	       struct psp_dev_caps *psd_caps, void *priv_ptr);
void psp_dev_unregister(struct psp_dev *psd);

/* Kernel-facing API */
#if IS_ENABLED(CONFIG_INET_PSP)
static inline void psp_sk_assoc_free(struct sock *sk) { }
static inline void
psp_twsk_init(struct inet_timewait_sock *tw, const struct sock *sk) { }
static inline void psp_twsk_assoc_free(struct inet_timewait_sock *tw) { }
static inline void
psp_reply_set_decrypted(struct sk_buff *skb) { }

static inline void
psp_enqueue_set_decrypted(struct sock *sk, struct sk_buff *skb)
{
}

static inline unsigned long
__psp_skb_coalesce_diff(const struct sk_buff *one, const struct sk_buff *two,
			unsigned long diffs)
{
	return diffs;
}

static inline enum skb_drop_reason
psp_sk_rx_policy_check(struct sock *sk, struct sk_buff *skb)
{
	return 0;
}

static inline enum skb_drop_reason
psp_twsk_rx_policy_check(struct inet_timewait_sock *tw, struct sk_buff *skb)
{
	return 0;
}

static inline struct psp_assoc *psp_skb_get_assoc_rcu(struct sk_buff *skb)
{
	return NULL;
}
#else
static inline void psp_sk_assoc_free(struct sock *sk) { }
static inline void
psp_twsk_init(struct inet_timewait_sock *tw, const struct sock *sk) { }
static inline void psp_twsk_assoc_free(struct inet_timewait_sock *tw) { }
static inline void
psp_reply_set_decrypted(struct sk_buff *skb) { }

static inline void
psp_enqueue_set_decrypted(struct sock *sk, struct sk_buff *skb) { }

static inline unsigned long
__psp_skb_coalesce_diff(const struct sk_buff *one, const struct sk_buff *two,
			unsigned long diffs)
{
	return diffs;
}

static inline enum skb_drop_reason
psp_sk_rx_policy_check(struct sock *sk, struct sk_buff *skb)
{
	return 0;
}

static inline enum skb_drop_reason
psp_twsk_rx_policy_check(struct inet_timewait_sock *tw, struct sk_buff *skb)
{
	return 0;
}

static inline struct psp_assoc *psp_skb_get_assoc_rcu(struct sk_buff *skb)
{
	return NULL;
}
#endif

static inline unsigned long
psp_skb_coalesce_diff(const struct sk_buff *one, const struct sk_buff *two)
{
	return __psp_skb_coalesce_diff(one, two, 0);
}

#endif /* __NET_PSP_HELPERS_H */
