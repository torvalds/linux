// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  SR-IPv6 implementation -- HMAC functions
 *
 *  Author:
 *  David Lebrun <david.lebrun@uclouvain.be>
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/in6.h>
#include <linux/icmpv6.h>
#include <linux/mroute6.h>
#include <linux/slab.h>
#include <linux/rhashtable.h>

#include <linux/netfilter.h>
#include <linux/netfilter_ipv6.h>

#include <net/sock.h>
#include <net/snmp.h>

#include <net/ipv6.h>
#include <net/protocol.h>
#include <net/transp_v6.h>
#include <net/rawv6.h>
#include <net/ndisc.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>
#include <net/xfrm.h>

#include <crypto/hash.h>
#include <crypto/sha.h>
#include <net/seg6.h>
#include <net/genetlink.h>
#include <net/seg6_hmac.h>
#include <linux/random.h>

static DEFINE_PER_CPU(char [SEG6_HMAC_RING_SIZE], hmac_ring);

static int seg6_hmac_cmpfn(struct rhashtable_compare_arg *arg, const void *obj)
{
	const struct seg6_hmac_info *hinfo = obj;

	return (hinfo->hmackeyid != *(__u32 *)arg->key);
}

static inline void seg6_hinfo_release(struct seg6_hmac_info *hinfo)
{
	kfree_rcu(hinfo, rcu);
}

static void seg6_free_hi(void *ptr, void *arg)
{
	struct seg6_hmac_info *hinfo = (struct seg6_hmac_info *)ptr;

	if (hinfo)
		seg6_hinfo_release(hinfo);
}

static const struct rhashtable_params rht_params = {
	.head_offset		= offsetof(struct seg6_hmac_info, node),
	.key_offset		= offsetof(struct seg6_hmac_info, hmackeyid),
	.key_len		= sizeof(u32),
	.automatic_shrinking	= true,
	.obj_cmpfn		= seg6_hmac_cmpfn,
};

static struct seg6_hmac_algo hmac_algos[] = {
	{
		.alg_id = SEG6_HMAC_ALGO_SHA1,
		.name = "hmac(sha1)",
	},
	{
		.alg_id = SEG6_HMAC_ALGO_SHA256,
		.name = "hmac(sha256)",
	},
};

static struct sr6_tlv_hmac *seg6_get_tlv_hmac(struct ipv6_sr_hdr *srh)
{
	struct sr6_tlv_hmac *tlv;

	if (srh->hdrlen < (srh->first_segment + 1) * 2 + 5)
		return NULL;

	if (!sr_has_hmac(srh))
		return NULL;

	tlv = (struct sr6_tlv_hmac *)
	      ((char *)srh + ((srh->hdrlen + 1) << 3) - 40);

	if (tlv->tlvhdr.type != SR6_TLV_HMAC || tlv->tlvhdr.len != 38)
		return NULL;

	return tlv;
}

static struct seg6_hmac_algo *__hmac_get_algo(u8 alg_id)
{
	struct seg6_hmac_algo *algo;
	int i, alg_count;

	alg_count = ARRAY_SIZE(hmac_algos);
	for (i = 0; i < alg_count; i++) {
		algo = &hmac_algos[i];
		if (algo->alg_id == alg_id)
			return algo;
	}

	return NULL;
}

static int __do_hmac(struct seg6_hmac_info *hinfo, const char *text, u8 psize,
		     u8 *output, int outlen)
{
	struct seg6_hmac_algo *algo;
	struct crypto_shash *tfm;
	struct shash_desc *shash;
	int ret, dgsize;

	algo = __hmac_get_algo(hinfo->alg_id);
	if (!algo)
		return -ENOENT;

	tfm = *this_cpu_ptr(algo->tfms);

	dgsize = crypto_shash_digestsize(tfm);
	if (dgsize > outlen) {
		pr_debug("sr-ipv6: __do_hmac: digest size too big (%d / %d)\n",
			 dgsize, outlen);
		return -ENOMEM;
	}

	ret = crypto_shash_setkey(tfm, hinfo->secret, hinfo->slen);
	if (ret < 0) {
		pr_debug("sr-ipv6: crypto_shash_setkey failed: err %d\n", ret);
		goto failed;
	}

	shash = *this_cpu_ptr(algo->shashs);
	shash->tfm = tfm;

	ret = crypto_shash_digest(shash, text, psize, output);
	if (ret < 0) {
		pr_debug("sr-ipv6: crypto_shash_digest failed: err %d\n", ret);
		goto failed;
	}

	return dgsize;

failed:
	return ret;
}

int seg6_hmac_compute(struct seg6_hmac_info *hinfo, struct ipv6_sr_hdr *hdr,
		      struct in6_addr *saddr, u8 *output)
{
	__be32 hmackeyid = cpu_to_be32(hinfo->hmackeyid);
	u8 tmp_out[SEG6_HMAC_MAX_DIGESTSIZE];
	int plen, i, dgsize, wrsize;
	char *ring, *off;

	/* a 160-byte buffer for digest output allows to store highest known
	 * hash function (RadioGatun) with up to 1216 bits
	 */

	/* saddr(16) + first_seg(1) + flags(1) + keyid(4) + seglist(16n) */
	plen = 16 + 1 + 1 + 4 + (hdr->first_segment + 1) * 16;

	/* this limit allows for 14 segments */
	if (plen >= SEG6_HMAC_RING_SIZE)
		return -EMSGSIZE;

	/* Let's build the HMAC text on the ring buffer. The text is composed
	 * as follows, in order:
	 *
	 * 1. Source IPv6 address (128 bits)
	 * 2. first_segment value (8 bits)
	 * 3. Flags (8 bits)
	 * 4. HMAC Key ID (32 bits)
	 * 5. All segments in the segments list (n * 128 bits)
	 */

	local_bh_disable();
	ring = this_cpu_ptr(hmac_ring);
	off = ring;

	/* source address */
	memcpy(off, saddr, 16);
	off += 16;

	/* first_segment value */
	*off++ = hdr->first_segment;

	/* flags */
	*off++ = hdr->flags;

	/* HMAC Key ID */
	memcpy(off, &hmackeyid, 4);
	off += 4;

	/* all segments in the list */
	for (i = 0; i < hdr->first_segment + 1; i++) {
		memcpy(off, hdr->segments + i, 16);
		off += 16;
	}

	dgsize = __do_hmac(hinfo, ring, plen, tmp_out,
			   SEG6_HMAC_MAX_DIGESTSIZE);
	local_bh_enable();

	if (dgsize < 0)
		return dgsize;

	wrsize = SEG6_HMAC_FIELD_LEN;
	if (wrsize > dgsize)
		wrsize = dgsize;

	memset(output, 0, SEG6_HMAC_FIELD_LEN);
	memcpy(output, tmp_out, wrsize);

	return 0;
}
EXPORT_SYMBOL(seg6_hmac_compute);

/* checks if an incoming SR-enabled packet's HMAC status matches
 * the incoming policy.
 *
 * called with rcu_read_lock()
 */
bool seg6_hmac_validate_skb(struct sk_buff *skb)
{
	u8 hmac_output[SEG6_HMAC_FIELD_LEN];
	struct net *net = dev_net(skb->dev);
	struct seg6_hmac_info *hinfo;
	struct sr6_tlv_hmac *tlv;
	struct ipv6_sr_hdr *srh;
	struct inet6_dev *idev;

	idev = __in6_dev_get(skb->dev);

	srh = (struct ipv6_sr_hdr *)skb_transport_header(skb);

	tlv = seg6_get_tlv_hmac(srh);

	/* mandatory check but no tlv */
	if (idev->cnf.seg6_require_hmac > 0 && !tlv)
		return false;

	/* no check */
	if (idev->cnf.seg6_require_hmac < 0)
		return true;

	/* check only if present */
	if (idev->cnf.seg6_require_hmac == 0 && !tlv)
		return true;

	/* now, seg6_require_hmac >= 0 && tlv */

	hinfo = seg6_hmac_info_lookup(net, be32_to_cpu(tlv->hmackeyid));
	if (!hinfo)
		return false;

	if (seg6_hmac_compute(hinfo, srh, &ipv6_hdr(skb)->saddr, hmac_output))
		return false;

	if (memcmp(hmac_output, tlv->hmac, SEG6_HMAC_FIELD_LEN) != 0)
		return false;

	return true;
}
EXPORT_SYMBOL(seg6_hmac_validate_skb);

/* called with rcu_read_lock() */
struct seg6_hmac_info *seg6_hmac_info_lookup(struct net *net, u32 key)
{
	struct seg6_pernet_data *sdata = seg6_pernet(net);
	struct seg6_hmac_info *hinfo;

	hinfo = rhashtable_lookup_fast(&sdata->hmac_infos, &key, rht_params);

	return hinfo;
}
EXPORT_SYMBOL(seg6_hmac_info_lookup);

int seg6_hmac_info_add(struct net *net, u32 key, struct seg6_hmac_info *hinfo)
{
	struct seg6_pernet_data *sdata = seg6_pernet(net);
	int err;

	err = rhashtable_lookup_insert_fast(&sdata->hmac_infos, &hinfo->node,
					    rht_params);

	return err;
}
EXPORT_SYMBOL(seg6_hmac_info_add);

int seg6_hmac_info_del(struct net *net, u32 key)
{
	struct seg6_pernet_data *sdata = seg6_pernet(net);
	struct seg6_hmac_info *hinfo;
	int err = -ENOENT;

	hinfo = rhashtable_lookup_fast(&sdata->hmac_infos, &key, rht_params);
	if (!hinfo)
		goto out;

	err = rhashtable_remove_fast(&sdata->hmac_infos, &hinfo->node,
				     rht_params);
	if (err)
		goto out;

	seg6_hinfo_release(hinfo);

out:
	return err;
}
EXPORT_SYMBOL(seg6_hmac_info_del);

int seg6_push_hmac(struct net *net, struct in6_addr *saddr,
		   struct ipv6_sr_hdr *srh)
{
	struct seg6_hmac_info *hinfo;
	struct sr6_tlv_hmac *tlv;
	int err = -ENOENT;

	tlv = seg6_get_tlv_hmac(srh);
	if (!tlv)
		return -EINVAL;

	rcu_read_lock();

	hinfo = seg6_hmac_info_lookup(net, be32_to_cpu(tlv->hmackeyid));
	if (!hinfo)
		goto out;

	memset(tlv->hmac, 0, SEG6_HMAC_FIELD_LEN);
	err = seg6_hmac_compute(hinfo, srh, saddr, tlv->hmac);

out:
	rcu_read_unlock();
	return err;
}
EXPORT_SYMBOL(seg6_push_hmac);

static int seg6_hmac_init_algo(void)
{
	struct seg6_hmac_algo *algo;
	struct crypto_shash *tfm;
	struct shash_desc *shash;
	int i, alg_count, cpu;

	alg_count = ARRAY_SIZE(hmac_algos);

	for (i = 0; i < alg_count; i++) {
		struct crypto_shash **p_tfm;
		int shsize;

		algo = &hmac_algos[i];
		algo->tfms = alloc_percpu(struct crypto_shash *);
		if (!algo->tfms)
			return -ENOMEM;

		for_each_possible_cpu(cpu) {
			tfm = crypto_alloc_shash(algo->name, 0, 0);
			if (IS_ERR(tfm))
				return PTR_ERR(tfm);
			p_tfm = per_cpu_ptr(algo->tfms, cpu);
			*p_tfm = tfm;
		}

		p_tfm = raw_cpu_ptr(algo->tfms);
		tfm = *p_tfm;

		shsize = sizeof(*shash) + crypto_shash_descsize(tfm);

		algo->shashs = alloc_percpu(struct shash_desc *);
		if (!algo->shashs)
			return -ENOMEM;

		for_each_possible_cpu(cpu) {
			shash = kzalloc_node(shsize, GFP_KERNEL,
					     cpu_to_node(cpu));
			if (!shash)
				return -ENOMEM;
			*per_cpu_ptr(algo->shashs, cpu) = shash;
		}
	}

	return 0;
}

int __init seg6_hmac_init(void)
{
	return seg6_hmac_init_algo();
}

int __net_init seg6_hmac_net_init(struct net *net)
{
	struct seg6_pernet_data *sdata = seg6_pernet(net);

	rhashtable_init(&sdata->hmac_infos, &rht_params);

	return 0;
}
EXPORT_SYMBOL(seg6_hmac_net_init);

void seg6_hmac_exit(void)
{
	struct seg6_hmac_algo *algo = NULL;
	int i, alg_count, cpu;

	alg_count = ARRAY_SIZE(hmac_algos);
	for (i = 0; i < alg_count; i++) {
		algo = &hmac_algos[i];
		for_each_possible_cpu(cpu) {
			struct crypto_shash *tfm;
			struct shash_desc *shash;

			shash = *per_cpu_ptr(algo->shashs, cpu);
			kfree(shash);
			tfm = *per_cpu_ptr(algo->tfms, cpu);
			crypto_free_shash(tfm);
		}
		free_percpu(algo->tfms);
		free_percpu(algo->shashs);
	}
}
EXPORT_SYMBOL(seg6_hmac_exit);

void __net_exit seg6_hmac_net_exit(struct net *net)
{
	struct seg6_pernet_data *sdata = seg6_pernet(net);

	rhashtable_free_and_destroy(&sdata->hmac_infos, seg6_free_hi, NULL);
}
EXPORT_SYMBOL(seg6_hmac_net_exit);
