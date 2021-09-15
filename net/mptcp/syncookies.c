// SPDX-License-Identifier: GPL-2.0
#include <linux/skbuff.h>

#include "protocol.h"

/* Syncookies do not work for JOIN requests.
 *
 * Unlike MP_CAPABLE, where the ACK cookie contains the needed MPTCP
 * options to reconstruct the initial syn state, MP_JOIN does not contain
 * the token to obtain the mptcp socket nor the server-generated nonce
 * that was used in the cookie SYN/ACK response.
 *
 * Keep a small best effort state table to store the syn/synack data,
 * indexed by skb hash.
 *
 * A MP_JOIN SYN packet handled by syn cookies is only stored if the 32bit
 * token matches a known mptcp connection that can still accept more subflows.
 *
 * There is no timeout handling -- state is only re-constructed
 * when the TCP ACK passed the cookie validation check.
 */

struct join_entry {
	u32 token;
	u32 remote_nonce;
	u32 local_nonce;
	u8 join_id;
	u8 local_id;
	u8 backup;
	u8 valid;
};

#define COOKIE_JOIN_SLOTS	1024

static struct join_entry join_entries[COOKIE_JOIN_SLOTS] __cacheline_aligned_in_smp;
static spinlock_t join_entry_locks[COOKIE_JOIN_SLOTS] __cacheline_aligned_in_smp;

static u32 mptcp_join_entry_hash(struct sk_buff *skb, struct net *net)
{
	static u32 mptcp_join_hash_secret __read_mostly;
	struct tcphdr *th = tcp_hdr(skb);
	u32 seq, i;

	net_get_random_once(&mptcp_join_hash_secret,
			    sizeof(mptcp_join_hash_secret));

	if (th->syn)
		seq = TCP_SKB_CB(skb)->seq;
	else
		seq = TCP_SKB_CB(skb)->seq - 1;

	i = jhash_3words(seq, net_hash_mix(net),
			 (__force __u32)th->source << 16 | (__force __u32)th->dest,
			 mptcp_join_hash_secret);

	return i % ARRAY_SIZE(join_entries);
}

static void mptcp_join_store_state(struct join_entry *entry,
				   const struct mptcp_subflow_request_sock *subflow_req)
{
	entry->token = subflow_req->token;
	entry->remote_nonce = subflow_req->remote_nonce;
	entry->local_nonce = subflow_req->local_nonce;
	entry->backup = subflow_req->backup;
	entry->join_id = subflow_req->remote_id;
	entry->local_id = subflow_req->local_id;
	entry->valid = 1;
}

void subflow_init_req_cookie_join_save(const struct mptcp_subflow_request_sock *subflow_req,
				       struct sk_buff *skb)
{
	struct net *net = read_pnet(&subflow_req->sk.req.ireq_net);
	u32 i = mptcp_join_entry_hash(skb, net);

	/* No use in waiting if other cpu is already using this slot --
	 * would overwrite the data that got stored.
	 */
	spin_lock_bh(&join_entry_locks[i]);
	mptcp_join_store_state(&join_entries[i], subflow_req);
	spin_unlock_bh(&join_entry_locks[i]);
}

/* Called for a cookie-ack with MP_JOIN option present.
 * Look up the saved state based on skb hash & check token matches msk
 * in same netns.
 *
 * Caller will check msk can still accept another subflow.  The hmac
 * present in the cookie ACK mptcp option space will be checked later.
 */
bool mptcp_token_join_cookie_init_state(struct mptcp_subflow_request_sock *subflow_req,
					struct sk_buff *skb)
{
	struct net *net = read_pnet(&subflow_req->sk.req.ireq_net);
	u32 i = mptcp_join_entry_hash(skb, net);
	struct mptcp_sock *msk;
	struct join_entry *e;

	e = &join_entries[i];

	spin_lock_bh(&join_entry_locks[i]);

	if (e->valid == 0) {
		spin_unlock_bh(&join_entry_locks[i]);
		return false;
	}

	e->valid = 0;

	msk = mptcp_token_get_sock(e->token);
	if (!msk) {
		spin_unlock_bh(&join_entry_locks[i]);
		return false;
	}

	/* If this fails, the token got re-used in the mean time by another
	 * mptcp socket in a different netns, i.e. entry is outdated.
	 */
	if (!net_eq(sock_net((struct sock *)msk), net))
		goto err_put;

	subflow_req->remote_nonce = e->remote_nonce;
	subflow_req->local_nonce = e->local_nonce;
	subflow_req->backup = e->backup;
	subflow_req->remote_id = e->join_id;
	subflow_req->token = e->token;
	subflow_req->msk = msk;
	spin_unlock_bh(&join_entry_locks[i]);
	return true;

err_put:
	spin_unlock_bh(&join_entry_locks[i]);
	sock_put((struct sock *)msk);
	return false;
}

void __init mptcp_join_cookie_init(void)
{
	int i;

	for (i = 0; i < COOKIE_JOIN_SLOTS; i++)
		spin_lock_init(&join_entry_locks[i]);
}
