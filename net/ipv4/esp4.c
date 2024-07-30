// SPDX-License-Identifier: GPL-2.0-only
#define pr_fmt(fmt) "IPsec: " fmt

#include <crypto/aead.h>
#include <crypto/authenc.h>
#include <linux/err.h>
#include <linux/module.h>
#include <net/ip.h>
#include <net/xfrm.h>
#include <net/esp.h>
#include <linux/scatterlist.h>
#include <linux/kernel.h>
#include <linux/pfkeyv2.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/in6.h>
#include <net/icmp.h>
#include <net/protocol.h>
#include <net/udp.h>
#include <net/tcp.h>
#include <net/espintcp.h>
#include <linux/skbuff_ref.h>

#include <linux/highmem.h>

struct esp_skb_cb {
	struct xfrm_skb_cb xfrm;
	void *tmp;
};

struct esp_output_extra {
	__be32 seqhi;
	u32 esphoff;
};

#define ESP_SKB_CB(__skb) ((struct esp_skb_cb *)&((__skb)->cb[0]))

/*
 * Allocate an AEAD request structure with extra space for SG and IV.
 *
 * For alignment considerations the IV is placed at the front, followed
 * by the request and finally the SG list.
 *
 * TODO: Use spare space in skb for this where possible.
 */
static void *esp_alloc_tmp(struct crypto_aead *aead, int nfrags, int extralen)
{
	unsigned int len;

	len = extralen;

	len += crypto_aead_ivsize(aead);

	if (len) {
		len += crypto_aead_alignmask(aead) &
		       ~(crypto_tfm_ctx_alignment() - 1);
		len = ALIGN(len, crypto_tfm_ctx_alignment());
	}

	len += sizeof(struct aead_request) + crypto_aead_reqsize(aead);
	len = ALIGN(len, __alignof__(struct scatterlist));

	len += sizeof(struct scatterlist) * nfrags;

	return kmalloc(len, GFP_ATOMIC);
}

static inline void *esp_tmp_extra(void *tmp)
{
	return PTR_ALIGN(tmp, __alignof__(struct esp_output_extra));
}

static inline u8 *esp_tmp_iv(struct crypto_aead *aead, void *tmp, int extralen)
{
	return crypto_aead_ivsize(aead) ?
	       PTR_ALIGN((u8 *)tmp + extralen,
			 crypto_aead_alignmask(aead) + 1) : tmp + extralen;
}

static inline struct aead_request *esp_tmp_req(struct crypto_aead *aead, u8 *iv)
{
	struct aead_request *req;

	req = (void *)PTR_ALIGN(iv + crypto_aead_ivsize(aead),
				crypto_tfm_ctx_alignment());
	aead_request_set_tfm(req, aead);
	return req;
}

static inline struct scatterlist *esp_req_sg(struct crypto_aead *aead,
					     struct aead_request *req)
{
	return (void *)ALIGN((unsigned long)(req + 1) +
			     crypto_aead_reqsize(aead),
			     __alignof__(struct scatterlist));
}

static void esp_ssg_unref(struct xfrm_state *x, void *tmp, struct sk_buff *skb)
{
	struct crypto_aead *aead = x->data;
	int extralen = 0;
	u8 *iv;
	struct aead_request *req;
	struct scatterlist *sg;

	if (x->props.flags & XFRM_STATE_ESN)
		extralen += sizeof(struct esp_output_extra);

	iv = esp_tmp_iv(aead, tmp, extralen);
	req = esp_tmp_req(aead, iv);

	/* Unref skb_frag_pages in the src scatterlist if necessary.
	 * Skip the first sg which comes from skb->data.
	 */
	if (req->src != req->dst)
		for (sg = sg_next(req->src); sg; sg = sg_next(sg))
			skb_page_unref(sg_page(sg), skb->pp_recycle);
}

#ifdef CONFIG_INET_ESPINTCP
struct esp_tcp_sk {
	struct sock *sk;
	struct rcu_head rcu;
};

static void esp_free_tcp_sk(struct rcu_head *head)
{
	struct esp_tcp_sk *esk = container_of(head, struct esp_tcp_sk, rcu);

	sock_put(esk->sk);
	kfree(esk);
}

static struct sock *esp_find_tcp_sk(struct xfrm_state *x)
{
	struct xfrm_encap_tmpl *encap = x->encap;
	struct net *net = xs_net(x);
	struct esp_tcp_sk *esk;
	__be16 sport, dport;
	struct sock *nsk;
	struct sock *sk;

	sk = rcu_dereference(x->encap_sk);
	if (sk && sk->sk_state == TCP_ESTABLISHED)
		return sk;

	spin_lock_bh(&x->lock);
	sport = encap->encap_sport;
	dport = encap->encap_dport;
	nsk = rcu_dereference_protected(x->encap_sk,
					lockdep_is_held(&x->lock));
	if (sk && sk == nsk) {
		esk = kmalloc(sizeof(*esk), GFP_ATOMIC);
		if (!esk) {
			spin_unlock_bh(&x->lock);
			return ERR_PTR(-ENOMEM);
		}
		RCU_INIT_POINTER(x->encap_sk, NULL);
		esk->sk = sk;
		call_rcu(&esk->rcu, esp_free_tcp_sk);
	}
	spin_unlock_bh(&x->lock);

	sk = inet_lookup_established(net, net->ipv4.tcp_death_row.hashinfo, x->id.daddr.a4,
				     dport, x->props.saddr.a4, sport, 0);
	if (!sk)
		return ERR_PTR(-ENOENT);

	if (!tcp_is_ulp_esp(sk)) {
		sock_put(sk);
		return ERR_PTR(-EINVAL);
	}

	spin_lock_bh(&x->lock);
	nsk = rcu_dereference_protected(x->encap_sk,
					lockdep_is_held(&x->lock));
	if (encap->encap_sport != sport ||
	    encap->encap_dport != dport) {
		sock_put(sk);
		sk = nsk ?: ERR_PTR(-EREMCHG);
	} else if (sk == nsk) {
		sock_put(sk);
	} else {
		rcu_assign_pointer(x->encap_sk, sk);
	}
	spin_unlock_bh(&x->lock);

	return sk;
}

static int esp_output_tcp_finish(struct xfrm_state *x, struct sk_buff *skb)
{
	struct sock *sk;
	int err;

	rcu_read_lock();

	sk = esp_find_tcp_sk(x);
	err = PTR_ERR_OR_ZERO(sk);
	if (err)
		goto out;

	bh_lock_sock(sk);
	if (sock_owned_by_user(sk))
		err = espintcp_queue_out(sk, skb);
	else
		err = espintcp_push_skb(sk, skb);
	bh_unlock_sock(sk);

out:
	rcu_read_unlock();
	return err;
}

static int esp_output_tcp_encap_cb(struct net *net, struct sock *sk,
				   struct sk_buff *skb)
{
	struct dst_entry *dst = skb_dst(skb);
	struct xfrm_state *x = dst->xfrm;

	return esp_output_tcp_finish(x, skb);
}

static int esp_output_tail_tcp(struct xfrm_state *x, struct sk_buff *skb)
{
	int err;

	local_bh_disable();
	err = xfrm_trans_queue_net(xs_net(x), skb, esp_output_tcp_encap_cb);
	local_bh_enable();

	/* EINPROGRESS just happens to do the right thing.  It
	 * actually means that the skb has been consumed and
	 * isn't coming back.
	 */
	return err ?: -EINPROGRESS;
}
#else
static int esp_output_tail_tcp(struct xfrm_state *x, struct sk_buff *skb)
{
	WARN_ON(1);
	return -EOPNOTSUPP;
}
#endif

static void esp_output_done(void *data, int err)
{
	struct sk_buff *skb = data;
	struct xfrm_offload *xo = xfrm_offload(skb);
	void *tmp;
	struct xfrm_state *x;

	if (xo && (xo->flags & XFRM_DEV_RESUME)) {
		struct sec_path *sp = skb_sec_path(skb);

		x = sp->xvec[sp->len - 1];
	} else {
		x = skb_dst(skb)->xfrm;
	}

	tmp = ESP_SKB_CB(skb)->tmp;
	esp_ssg_unref(x, tmp, skb);
	kfree(tmp);

	if (xo && (xo->flags & XFRM_DEV_RESUME)) {
		if (err) {
			XFRM_INC_STATS(xs_net(x), LINUX_MIB_XFRMOUTSTATEPROTOERROR);
			kfree_skb(skb);
			return;
		}

		skb_push(skb, skb->data - skb_mac_header(skb));
		secpath_reset(skb);
		xfrm_dev_resume(skb);
	} else {
		if (!err &&
		    x->encap && x->encap->encap_type == TCP_ENCAP_ESPINTCP)
			esp_output_tail_tcp(x, skb);
		else
			xfrm_output_resume(skb->sk, skb, err);
	}
}

/* Move ESP header back into place. */
static void esp_restore_header(struct sk_buff *skb, unsigned int offset)
{
	struct ip_esp_hdr *esph = (void *)(skb->data + offset);
	void *tmp = ESP_SKB_CB(skb)->tmp;
	__be32 *seqhi = esp_tmp_extra(tmp);

	esph->seq_no = esph->spi;
	esph->spi = *seqhi;
}

static void esp_output_restore_header(struct sk_buff *skb)
{
	void *tmp = ESP_SKB_CB(skb)->tmp;
	struct esp_output_extra *extra = esp_tmp_extra(tmp);

	esp_restore_header(skb, skb_transport_offset(skb) + extra->esphoff -
				sizeof(__be32));
}

static struct ip_esp_hdr *esp_output_set_extra(struct sk_buff *skb,
					       struct xfrm_state *x,
					       struct ip_esp_hdr *esph,
					       struct esp_output_extra *extra)
{
	/* For ESN we move the header forward by 4 bytes to
	 * accommodate the high bits.  We will move it back after
	 * encryption.
	 */
	if ((x->props.flags & XFRM_STATE_ESN)) {
		__u32 seqhi;
		struct xfrm_offload *xo = xfrm_offload(skb);

		if (xo)
			seqhi = xo->seq.hi;
		else
			seqhi = XFRM_SKB_CB(skb)->seq.output.hi;

		extra->esphoff = (unsigned char *)esph -
				 skb_transport_header(skb);
		esph = (struct ip_esp_hdr *)((unsigned char *)esph - 4);
		extra->seqhi = esph->spi;
		esph->seq_no = htonl(seqhi);
	}

	esph->spi = x->id.spi;

	return esph;
}

static void esp_output_done_esn(void *data, int err)
{
	struct sk_buff *skb = data;

	esp_output_restore_header(skb);
	esp_output_done(data, err);
}

static struct ip_esp_hdr *esp_output_udp_encap(struct sk_buff *skb,
					       int encap_type,
					       struct esp_info *esp,
					       __be16 sport,
					       __be16 dport)
{
	struct udphdr *uh;
	unsigned int len;
	struct xfrm_offload *xo = xfrm_offload(skb);

	len = skb->len + esp->tailen - skb_transport_offset(skb);
	if (len + sizeof(struct iphdr) > IP_MAX_MTU)
		return ERR_PTR(-EMSGSIZE);

	uh = (struct udphdr *)esp->esph;
	uh->source = sport;
	uh->dest = dport;
	uh->len = htons(len);
	uh->check = 0;

	/* For IPv4 ESP with UDP encapsulation, if xo is not null, the skb is in the crypto offload
	 * data path, which means that esp_output_udp_encap is called outside of the XFRM stack.
	 * In this case, the mac header doesn't point to the IPv4 protocol field, so don't set it.
	 */
	if (!xo || encap_type != UDP_ENCAP_ESPINUDP)
		*skb_mac_header(skb) = IPPROTO_UDP;

	return (struct ip_esp_hdr *)(uh + 1);
}

#ifdef CONFIG_INET_ESPINTCP
static struct ip_esp_hdr *esp_output_tcp_encap(struct xfrm_state *x,
						    struct sk_buff *skb,
						    struct esp_info *esp)
{
	__be16 *lenp = (void *)esp->esph;
	struct ip_esp_hdr *esph;
	unsigned int len;
	struct sock *sk;

	len = skb->len + esp->tailen - skb_transport_offset(skb);
	if (len > IP_MAX_MTU)
		return ERR_PTR(-EMSGSIZE);

	rcu_read_lock();
	sk = esp_find_tcp_sk(x);
	rcu_read_unlock();

	if (IS_ERR(sk))
		return ERR_CAST(sk);

	*lenp = htons(len);
	esph = (struct ip_esp_hdr *)(lenp + 1);

	return esph;
}
#else
static struct ip_esp_hdr *esp_output_tcp_encap(struct xfrm_state *x,
						    struct sk_buff *skb,
						    struct esp_info *esp)
{
	return ERR_PTR(-EOPNOTSUPP);
}
#endif

static int esp_output_encap(struct xfrm_state *x, struct sk_buff *skb,
			    struct esp_info *esp)
{
	struct xfrm_encap_tmpl *encap = x->encap;
	struct ip_esp_hdr *esph;
	__be16 sport, dport;
	int encap_type;

	spin_lock_bh(&x->lock);
	sport = encap->encap_sport;
	dport = encap->encap_dport;
	encap_type = encap->encap_type;
	spin_unlock_bh(&x->lock);

	switch (encap_type) {
	default:
	case UDP_ENCAP_ESPINUDP:
		esph = esp_output_udp_encap(skb, encap_type, esp, sport, dport);
		break;
	case TCP_ENCAP_ESPINTCP:
		esph = esp_output_tcp_encap(x, skb, esp);
		break;
	}

	if (IS_ERR(esph))
		return PTR_ERR(esph);

	esp->esph = esph;

	return 0;
}

int esp_output_head(struct xfrm_state *x, struct sk_buff *skb, struct esp_info *esp)
{
	u8 *tail;
	int nfrags;
	int esph_offset;
	struct page *page;
	struct sk_buff *trailer;
	int tailen = esp->tailen;

	/* this is non-NULL only with TCP/UDP Encapsulation */
	if (x->encap) {
		int err = esp_output_encap(x, skb, esp);

		if (err < 0)
			return err;
	}

	if (ALIGN(tailen, L1_CACHE_BYTES) > PAGE_SIZE ||
	    ALIGN(skb->data_len, L1_CACHE_BYTES) > PAGE_SIZE)
		goto cow;

	if (!skb_cloned(skb)) {
		if (tailen <= skb_tailroom(skb)) {
			nfrags = 1;
			trailer = skb;
			tail = skb_tail_pointer(trailer);

			goto skip_cow;
		} else if ((skb_shinfo(skb)->nr_frags < MAX_SKB_FRAGS)
			   && !skb_has_frag_list(skb)) {
			int allocsize;
			struct sock *sk = skb->sk;
			struct page_frag *pfrag = &x->xfrag;

			esp->inplace = false;

			allocsize = ALIGN(tailen, L1_CACHE_BYTES);

			spin_lock_bh(&x->lock);

			if (unlikely(!skb_page_frag_refill(allocsize, pfrag, GFP_ATOMIC))) {
				spin_unlock_bh(&x->lock);
				goto cow;
			}

			page = pfrag->page;
			get_page(page);

			tail = page_address(page) + pfrag->offset;

			esp_output_fill_trailer(tail, esp->tfclen, esp->plen, esp->proto);

			nfrags = skb_shinfo(skb)->nr_frags;

			__skb_fill_page_desc(skb, nfrags, page, pfrag->offset,
					     tailen);
			skb_shinfo(skb)->nr_frags = ++nfrags;

			pfrag->offset = pfrag->offset + allocsize;

			spin_unlock_bh(&x->lock);

			nfrags++;

			skb_len_add(skb, tailen);
			if (sk && sk_fullsock(sk))
				refcount_add(tailen, &sk->sk_wmem_alloc);

			goto out;
		}
	}

cow:
	esph_offset = (unsigned char *)esp->esph - skb_transport_header(skb);

	nfrags = skb_cow_data(skb, tailen, &trailer);
	if (nfrags < 0)
		goto out;
	tail = skb_tail_pointer(trailer);
	esp->esph = (struct ip_esp_hdr *)(skb_transport_header(skb) + esph_offset);

skip_cow:
	esp_output_fill_trailer(tail, esp->tfclen, esp->plen, esp->proto);
	pskb_put(skb, trailer, tailen);

out:
	return nfrags;
}
EXPORT_SYMBOL_GPL(esp_output_head);

int esp_output_tail(struct xfrm_state *x, struct sk_buff *skb, struct esp_info *esp)
{
	u8 *iv;
	int alen;
	void *tmp;
	int ivlen;
	int assoclen;
	int extralen;
	struct page *page;
	struct ip_esp_hdr *esph;
	struct crypto_aead *aead;
	struct aead_request *req;
	struct scatterlist *sg, *dsg;
	struct esp_output_extra *extra;
	int err = -ENOMEM;

	assoclen = sizeof(struct ip_esp_hdr);
	extralen = 0;

	if (x->props.flags & XFRM_STATE_ESN) {
		extralen += sizeof(*extra);
		assoclen += sizeof(__be32);
	}

	aead = x->data;
	alen = crypto_aead_authsize(aead);
	ivlen = crypto_aead_ivsize(aead);

	tmp = esp_alloc_tmp(aead, esp->nfrags + 2, extralen);
	if (!tmp)
		goto error;

	extra = esp_tmp_extra(tmp);
	iv = esp_tmp_iv(aead, tmp, extralen);
	req = esp_tmp_req(aead, iv);
	sg = esp_req_sg(aead, req);

	if (esp->inplace)
		dsg = sg;
	else
		dsg = &sg[esp->nfrags];

	esph = esp_output_set_extra(skb, x, esp->esph, extra);
	esp->esph = esph;

	sg_init_table(sg, esp->nfrags);
	err = skb_to_sgvec(skb, sg,
		           (unsigned char *)esph - skb->data,
		           assoclen + ivlen + esp->clen + alen);
	if (unlikely(err < 0))
		goto error_free;

	if (!esp->inplace) {
		int allocsize;
		struct page_frag *pfrag = &x->xfrag;

		allocsize = ALIGN(skb->data_len, L1_CACHE_BYTES);

		spin_lock_bh(&x->lock);
		if (unlikely(!skb_page_frag_refill(allocsize, pfrag, GFP_ATOMIC))) {
			spin_unlock_bh(&x->lock);
			goto error_free;
		}

		skb_shinfo(skb)->nr_frags = 1;

		page = pfrag->page;
		get_page(page);
		/* replace page frags in skb with new page */
		__skb_fill_page_desc(skb, 0, page, pfrag->offset, skb->data_len);
		pfrag->offset = pfrag->offset + allocsize;
		spin_unlock_bh(&x->lock);

		sg_init_table(dsg, skb_shinfo(skb)->nr_frags + 1);
		err = skb_to_sgvec(skb, dsg,
			           (unsigned char *)esph - skb->data,
			           assoclen + ivlen + esp->clen + alen);
		if (unlikely(err < 0))
			goto error_free;
	}

	if ((x->props.flags & XFRM_STATE_ESN))
		aead_request_set_callback(req, 0, esp_output_done_esn, skb);
	else
		aead_request_set_callback(req, 0, esp_output_done, skb);

	aead_request_set_crypt(req, sg, dsg, ivlen + esp->clen, iv);
	aead_request_set_ad(req, assoclen);

	memset(iv, 0, ivlen);
	memcpy(iv + ivlen - min(ivlen, 8), (u8 *)&esp->seqno + 8 - min(ivlen, 8),
	       min(ivlen, 8));

	ESP_SKB_CB(skb)->tmp = tmp;
	err = crypto_aead_encrypt(req);

	switch (err) {
	case -EINPROGRESS:
		goto error;

	case -ENOSPC:
		err = NET_XMIT_DROP;
		break;

	case 0:
		if ((x->props.flags & XFRM_STATE_ESN))
			esp_output_restore_header(skb);
	}

	if (sg != dsg)
		esp_ssg_unref(x, tmp, skb);

	if (!err && x->encap && x->encap->encap_type == TCP_ENCAP_ESPINTCP)
		err = esp_output_tail_tcp(x, skb);

error_free:
	kfree(tmp);
error:
	return err;
}
EXPORT_SYMBOL_GPL(esp_output_tail);

static int esp_output(struct xfrm_state *x, struct sk_buff *skb)
{
	int alen;
	int blksize;
	struct ip_esp_hdr *esph;
	struct crypto_aead *aead;
	struct esp_info esp;

	esp.inplace = true;

	esp.proto = *skb_mac_header(skb);
	*skb_mac_header(skb) = IPPROTO_ESP;

	/* skb is pure payload to encrypt */

	aead = x->data;
	alen = crypto_aead_authsize(aead);

	esp.tfclen = 0;
	if (x->tfcpad) {
		struct xfrm_dst *dst = (struct xfrm_dst *)skb_dst(skb);
		u32 padto;

		padto = min(x->tfcpad, xfrm_state_mtu(x, dst->child_mtu_cached));
		if (skb->len < padto)
			esp.tfclen = padto - skb->len;
	}
	blksize = ALIGN(crypto_aead_blocksize(aead), 4);
	esp.clen = ALIGN(skb->len + 2 + esp.tfclen, blksize);
	esp.plen = esp.clen - skb->len - esp.tfclen;
	esp.tailen = esp.tfclen + esp.plen + alen;

	esp.esph = ip_esp_hdr(skb);

	esp.nfrags = esp_output_head(x, skb, &esp);
	if (esp.nfrags < 0)
		return esp.nfrags;

	esph = esp.esph;
	esph->spi = x->id.spi;

	esph->seq_no = htonl(XFRM_SKB_CB(skb)->seq.output.low);
	esp.seqno = cpu_to_be64(XFRM_SKB_CB(skb)->seq.output.low +
				 ((u64)XFRM_SKB_CB(skb)->seq.output.hi << 32));

	skb_push(skb, -skb_network_offset(skb));

	return esp_output_tail(x, skb, &esp);
}

static inline int esp_remove_trailer(struct sk_buff *skb)
{
	struct xfrm_state *x = xfrm_input_state(skb);
	struct crypto_aead *aead = x->data;
	int alen, hlen, elen;
	int padlen, trimlen;
	__wsum csumdiff;
	u8 nexthdr[2];
	int ret;

	alen = crypto_aead_authsize(aead);
	hlen = sizeof(struct ip_esp_hdr) + crypto_aead_ivsize(aead);
	elen = skb->len - hlen;

	if (skb_copy_bits(skb, skb->len - alen - 2, nexthdr, 2))
		BUG();

	ret = -EINVAL;
	padlen = nexthdr[0];
	if (padlen + 2 + alen >= elen) {
		net_dbg_ratelimited("ipsec esp packet is garbage padlen=%d, elen=%d\n",
				    padlen + 2, elen - alen);
		goto out;
	}

	trimlen = alen + padlen + 2;
	if (skb->ip_summed == CHECKSUM_COMPLETE) {
		csumdiff = skb_checksum(skb, skb->len - trimlen, trimlen, 0);
		skb->csum = csum_block_sub(skb->csum, csumdiff,
					   skb->len - trimlen);
	}
	ret = pskb_trim(skb, skb->len - trimlen);
	if (unlikely(ret))
		return ret;

	ret = nexthdr[1];

out:
	return ret;
}

int esp_input_done2(struct sk_buff *skb, int err)
{
	const struct iphdr *iph;
	struct xfrm_state *x = xfrm_input_state(skb);
	struct xfrm_offload *xo = xfrm_offload(skb);
	struct crypto_aead *aead = x->data;
	int hlen = sizeof(struct ip_esp_hdr) + crypto_aead_ivsize(aead);
	int ihl;

	if (!xo || !(xo->flags & CRYPTO_DONE))
		kfree(ESP_SKB_CB(skb)->tmp);

	if (unlikely(err))
		goto out;

	err = esp_remove_trailer(skb);
	if (unlikely(err < 0))
		goto out;

	iph = ip_hdr(skb);
	ihl = iph->ihl * 4;

	if (x->encap) {
		struct xfrm_encap_tmpl *encap = x->encap;
		struct tcphdr *th = (void *)(skb_network_header(skb) + ihl);
		struct udphdr *uh = (void *)(skb_network_header(skb) + ihl);
		__be16 source;

		switch (x->encap->encap_type) {
		case TCP_ENCAP_ESPINTCP:
			source = th->source;
			break;
		case UDP_ENCAP_ESPINUDP:
			source = uh->source;
			break;
		default:
			WARN_ON_ONCE(1);
			err = -EINVAL;
			goto out;
		}

		/*
		 * 1) if the NAT-T peer's IP or port changed then
		 *    advertise the change to the keying daemon.
		 *    This is an inbound SA, so just compare
		 *    SRC ports.
		 */
		if (iph->saddr != x->props.saddr.a4 ||
		    source != encap->encap_sport) {
			xfrm_address_t ipaddr;

			ipaddr.a4 = iph->saddr;
			km_new_mapping(x, &ipaddr, source);

			/* XXX: perhaps add an extra
			 * policy check here, to see
			 * if we should allow or
			 * reject a packet from a
			 * different source
			 * address/port.
			 */
		}

		/*
		 * 2) ignore UDP/TCP checksums in case
		 *    of NAT-T in Transport Mode, or
		 *    perform other post-processing fixes
		 *    as per draft-ietf-ipsec-udp-encaps-06,
		 *    section 3.1.2
		 */
		if (x->props.mode == XFRM_MODE_TRANSPORT)
			skb->ip_summed = CHECKSUM_UNNECESSARY;
	}

	skb_pull_rcsum(skb, hlen);
	if (x->props.mode == XFRM_MODE_TUNNEL)
		skb_reset_transport_header(skb);
	else
		skb_set_transport_header(skb, -ihl);

	/* RFC4303: Drop dummy packets without any error */
	if (err == IPPROTO_NONE)
		err = -EINVAL;

out:
	return err;
}
EXPORT_SYMBOL_GPL(esp_input_done2);

static void esp_input_done(void *data, int err)
{
	struct sk_buff *skb = data;

	xfrm_input_resume(skb, esp_input_done2(skb, err));
}

static void esp_input_restore_header(struct sk_buff *skb)
{
	esp_restore_header(skb, 0);
	__skb_pull(skb, 4);
}

static void esp_input_set_header(struct sk_buff *skb, __be32 *seqhi)
{
	struct xfrm_state *x = xfrm_input_state(skb);
	struct ip_esp_hdr *esph;

	/* For ESN we move the header forward by 4 bytes to
	 * accommodate the high bits.  We will move it back after
	 * decryption.
	 */
	if ((x->props.flags & XFRM_STATE_ESN)) {
		esph = skb_push(skb, 4);
		*seqhi = esph->spi;
		esph->spi = esph->seq_no;
		esph->seq_no = XFRM_SKB_CB(skb)->seq.input.hi;
	}
}

static void esp_input_done_esn(void *data, int err)
{
	struct sk_buff *skb = data;

	esp_input_restore_header(skb);
	esp_input_done(data, err);
}

/*
 * Note: detecting truncated vs. non-truncated authentication data is very
 * expensive, so we only support truncated data, which is the recommended
 * and common case.
 */
static int esp_input(struct xfrm_state *x, struct sk_buff *skb)
{
	struct crypto_aead *aead = x->data;
	struct aead_request *req;
	struct sk_buff *trailer;
	int ivlen = crypto_aead_ivsize(aead);
	int elen = skb->len - sizeof(struct ip_esp_hdr) - ivlen;
	int nfrags;
	int assoclen;
	int seqhilen;
	__be32 *seqhi;
	void *tmp;
	u8 *iv;
	struct scatterlist *sg;
	int err = -EINVAL;

	if (!pskb_may_pull(skb, sizeof(struct ip_esp_hdr) + ivlen))
		goto out;

	if (elen <= 0)
		goto out;

	assoclen = sizeof(struct ip_esp_hdr);
	seqhilen = 0;

	if (x->props.flags & XFRM_STATE_ESN) {
		seqhilen += sizeof(__be32);
		assoclen += seqhilen;
	}

	if (!skb_cloned(skb)) {
		if (!skb_is_nonlinear(skb)) {
			nfrags = 1;

			goto skip_cow;
		} else if (!skb_has_frag_list(skb)) {
			nfrags = skb_shinfo(skb)->nr_frags;
			nfrags++;

			goto skip_cow;
		}
	}

	err = skb_cow_data(skb, 0, &trailer);
	if (err < 0)
		goto out;

	nfrags = err;

skip_cow:
	err = -ENOMEM;
	tmp = esp_alloc_tmp(aead, nfrags, seqhilen);
	if (!tmp)
		goto out;

	ESP_SKB_CB(skb)->tmp = tmp;
	seqhi = esp_tmp_extra(tmp);
	iv = esp_tmp_iv(aead, tmp, seqhilen);
	req = esp_tmp_req(aead, iv);
	sg = esp_req_sg(aead, req);

	esp_input_set_header(skb, seqhi);

	sg_init_table(sg, nfrags);
	err = skb_to_sgvec(skb, sg, 0, skb->len);
	if (unlikely(err < 0)) {
		kfree(tmp);
		goto out;
	}

	skb->ip_summed = CHECKSUM_NONE;

	if ((x->props.flags & XFRM_STATE_ESN))
		aead_request_set_callback(req, 0, esp_input_done_esn, skb);
	else
		aead_request_set_callback(req, 0, esp_input_done, skb);

	aead_request_set_crypt(req, sg, sg, elen + ivlen, iv);
	aead_request_set_ad(req, assoclen);

	err = crypto_aead_decrypt(req);
	if (err == -EINPROGRESS)
		goto out;

	if ((x->props.flags & XFRM_STATE_ESN))
		esp_input_restore_header(skb);

	err = esp_input_done2(skb, err);

out:
	return err;
}

static int esp4_err(struct sk_buff *skb, u32 info)
{
	struct net *net = dev_net(skb->dev);
	const struct iphdr *iph = (const struct iphdr *)skb->data;
	struct ip_esp_hdr *esph = (struct ip_esp_hdr *)(skb->data+(iph->ihl<<2));
	struct xfrm_state *x;

	switch (icmp_hdr(skb)->type) {
	case ICMP_DEST_UNREACH:
		if (icmp_hdr(skb)->code != ICMP_FRAG_NEEDED)
			return 0;
		break;
	case ICMP_REDIRECT:
		break;
	default:
		return 0;
	}

	x = xfrm_state_lookup(net, skb->mark, (const xfrm_address_t *)&iph->daddr,
			      esph->spi, IPPROTO_ESP, AF_INET);
	if (!x)
		return 0;

	if (icmp_hdr(skb)->type == ICMP_DEST_UNREACH)
		ipv4_update_pmtu(skb, net, info, 0, IPPROTO_ESP);
	else
		ipv4_redirect(skb, net, 0, IPPROTO_ESP);
	xfrm_state_put(x);

	return 0;
}

static void esp_destroy(struct xfrm_state *x)
{
	struct crypto_aead *aead = x->data;

	if (!aead)
		return;

	crypto_free_aead(aead);
}

static int esp_init_aead(struct xfrm_state *x, struct netlink_ext_ack *extack)
{
	char aead_name[CRYPTO_MAX_ALG_NAME];
	struct crypto_aead *aead;
	int err;

	if (snprintf(aead_name, CRYPTO_MAX_ALG_NAME, "%s(%s)",
		     x->geniv, x->aead->alg_name) >= CRYPTO_MAX_ALG_NAME) {
		NL_SET_ERR_MSG(extack, "Algorithm name is too long");
		return -ENAMETOOLONG;
	}

	aead = crypto_alloc_aead(aead_name, 0, 0);
	err = PTR_ERR(aead);
	if (IS_ERR(aead))
		goto error;

	x->data = aead;

	err = crypto_aead_setkey(aead, x->aead->alg_key,
				 (x->aead->alg_key_len + 7) / 8);
	if (err)
		goto error;

	err = crypto_aead_setauthsize(aead, x->aead->alg_icv_len / 8);
	if (err)
		goto error;

	return 0;

error:
	NL_SET_ERR_MSG(extack, "Kernel was unable to initialize cryptographic operations");
	return err;
}

static int esp_init_authenc(struct xfrm_state *x,
			    struct netlink_ext_ack *extack)
{
	struct crypto_aead *aead;
	struct crypto_authenc_key_param *param;
	struct rtattr *rta;
	char *key;
	char *p;
	char authenc_name[CRYPTO_MAX_ALG_NAME];
	unsigned int keylen;
	int err;

	err = -ENAMETOOLONG;

	if ((x->props.flags & XFRM_STATE_ESN)) {
		if (snprintf(authenc_name, CRYPTO_MAX_ALG_NAME,
			     "%s%sauthencesn(%s,%s)%s",
			     x->geniv ?: "", x->geniv ? "(" : "",
			     x->aalg ? x->aalg->alg_name : "digest_null",
			     x->ealg->alg_name,
			     x->geniv ? ")" : "") >= CRYPTO_MAX_ALG_NAME) {
			NL_SET_ERR_MSG(extack, "Algorithm name is too long");
			goto error;
		}
	} else {
		if (snprintf(authenc_name, CRYPTO_MAX_ALG_NAME,
			     "%s%sauthenc(%s,%s)%s",
			     x->geniv ?: "", x->geniv ? "(" : "",
			     x->aalg ? x->aalg->alg_name : "digest_null",
			     x->ealg->alg_name,
			     x->geniv ? ")" : "") >= CRYPTO_MAX_ALG_NAME) {
			NL_SET_ERR_MSG(extack, "Algorithm name is too long");
			goto error;
		}
	}

	aead = crypto_alloc_aead(authenc_name, 0, 0);
	err = PTR_ERR(aead);
	if (IS_ERR(aead)) {
		NL_SET_ERR_MSG(extack, "Kernel was unable to initialize cryptographic operations");
		goto error;
	}

	x->data = aead;

	keylen = (x->aalg ? (x->aalg->alg_key_len + 7) / 8 : 0) +
		 (x->ealg->alg_key_len + 7) / 8 + RTA_SPACE(sizeof(*param));
	err = -ENOMEM;
	key = kmalloc(keylen, GFP_KERNEL);
	if (!key)
		goto error;

	p = key;
	rta = (void *)p;
	rta->rta_type = CRYPTO_AUTHENC_KEYA_PARAM;
	rta->rta_len = RTA_LENGTH(sizeof(*param));
	param = RTA_DATA(rta);
	p += RTA_SPACE(sizeof(*param));

	if (x->aalg) {
		struct xfrm_algo_desc *aalg_desc;

		memcpy(p, x->aalg->alg_key, (x->aalg->alg_key_len + 7) / 8);
		p += (x->aalg->alg_key_len + 7) / 8;

		aalg_desc = xfrm_aalg_get_byname(x->aalg->alg_name, 0);
		BUG_ON(!aalg_desc);

		err = -EINVAL;
		if (aalg_desc->uinfo.auth.icv_fullbits / 8 !=
		    crypto_aead_authsize(aead)) {
			NL_SET_ERR_MSG(extack, "Kernel was unable to initialize cryptographic operations");
			goto free_key;
		}

		err = crypto_aead_setauthsize(
			aead, x->aalg->alg_trunc_len / 8);
		if (err) {
			NL_SET_ERR_MSG(extack, "Kernel was unable to initialize cryptographic operations");
			goto free_key;
		}
	}

	param->enckeylen = cpu_to_be32((x->ealg->alg_key_len + 7) / 8);
	memcpy(p, x->ealg->alg_key, (x->ealg->alg_key_len + 7) / 8);

	err = crypto_aead_setkey(aead, key, keylen);

free_key:
	kfree_sensitive(key);

error:
	return err;
}

static int esp_init_state(struct xfrm_state *x, struct netlink_ext_ack *extack)
{
	struct crypto_aead *aead;
	u32 align;
	int err;

	x->data = NULL;

	if (x->aead) {
		err = esp_init_aead(x, extack);
	} else if (x->ealg) {
		err = esp_init_authenc(x, extack);
	} else {
		NL_SET_ERR_MSG(extack, "ESP: AEAD or CRYPT must be provided");
		err = -EINVAL;
	}

	if (err)
		goto error;

	aead = x->data;

	x->props.header_len = sizeof(struct ip_esp_hdr) +
			      crypto_aead_ivsize(aead);
	if (x->props.mode == XFRM_MODE_TUNNEL)
		x->props.header_len += sizeof(struct iphdr);
	else if (x->props.mode == XFRM_MODE_BEET && x->sel.family != AF_INET6)
		x->props.header_len += IPV4_BEET_PHMAXLEN;
	if (x->encap) {
		struct xfrm_encap_tmpl *encap = x->encap;

		switch (encap->encap_type) {
		default:
			NL_SET_ERR_MSG(extack, "Unsupported encapsulation type for ESP");
			err = -EINVAL;
			goto error;
		case UDP_ENCAP_ESPINUDP:
			x->props.header_len += sizeof(struct udphdr);
			break;
#ifdef CONFIG_INET_ESPINTCP
		case TCP_ENCAP_ESPINTCP:
			/* only the length field, TCP encap is done by
			 * the socket
			 */
			x->props.header_len += 2;
			break;
#endif
		}
	}

	align = ALIGN(crypto_aead_blocksize(aead), 4);
	x->props.trailer_len = align + 1 + crypto_aead_authsize(aead);

error:
	return err;
}

static int esp4_rcv_cb(struct sk_buff *skb, int err)
{
	return 0;
}

static const struct xfrm_type esp_type =
{
	.owner		= THIS_MODULE,
	.proto	     	= IPPROTO_ESP,
	.flags		= XFRM_TYPE_REPLAY_PROT,
	.init_state	= esp_init_state,
	.destructor	= esp_destroy,
	.input		= esp_input,
	.output		= esp_output,
};

static struct xfrm4_protocol esp4_protocol = {
	.handler	=	xfrm4_rcv,
	.input_handler	=	xfrm_input,
	.cb_handler	=	esp4_rcv_cb,
	.err_handler	=	esp4_err,
	.priority	=	0,
};

static int __init esp4_init(void)
{
	if (xfrm_register_type(&esp_type, AF_INET) < 0) {
		pr_info("%s: can't add xfrm type\n", __func__);
		return -EAGAIN;
	}
	if (xfrm4_protocol_register(&esp4_protocol, IPPROTO_ESP) < 0) {
		pr_info("%s: can't add protocol\n", __func__);
		xfrm_unregister_type(&esp_type, AF_INET);
		return -EAGAIN;
	}
	return 0;
}

static void __exit esp4_fini(void)
{
	if (xfrm4_protocol_deregister(&esp4_protocol, IPPROTO_ESP) < 0)
		pr_info("%s: can't remove protocol\n", __func__);
	xfrm_unregister_type(&esp_type, AF_INET);
}

module_init(esp4_init);
module_exit(esp4_fini);
MODULE_DESCRIPTION("IPv4 ESP transformation library");
MODULE_LICENSE("GPL");
MODULE_ALIAS_XFRM_TYPE(AF_INET, XFRM_PROTO_ESP);
