// SPDX-License-Identifier: GPL-2.0
/*
 * XFRM compat layer
 * Author: Dmitry Safonov <dima@arista.com>
 * Based on code and translator idea by: Florian Westphal <fw@strlen.de>
 */
#include <linux/compat.h>
#include <linux/nospec.h>
#include <linux/xfrm.h>
#include <net/xfrm.h>

struct compat_xfrm_lifetime_cfg {
	compat_u64 soft_byte_limit, hard_byte_limit;
	compat_u64 soft_packet_limit, hard_packet_limit;
	compat_u64 soft_add_expires_seconds, hard_add_expires_seconds;
	compat_u64 soft_use_expires_seconds, hard_use_expires_seconds;
}; /* same size on 32bit, but only 4 byte alignment required */

struct compat_xfrm_lifetime_cur {
	compat_u64 bytes, packets, add_time, use_time;
}; /* same size on 32bit, but only 4 byte alignment required */

struct compat_xfrm_userpolicy_info {
	struct xfrm_selector sel;
	struct compat_xfrm_lifetime_cfg lft;
	struct compat_xfrm_lifetime_cur curlft;
	__u32 priority, index;
	u8 dir, action, flags, share;
	/* 4 bytes additional padding on 64bit */
};

struct compat_xfrm_usersa_info {
	struct xfrm_selector sel;
	struct xfrm_id id;
	xfrm_address_t saddr;
	struct compat_xfrm_lifetime_cfg lft;
	struct compat_xfrm_lifetime_cur curlft;
	struct xfrm_stats stats;
	__u32 seq, reqid;
	u16 family;
	u8 mode, replay_window, flags;
	/* 4 bytes additional padding on 64bit */
};

struct compat_xfrm_user_acquire {
	struct xfrm_id id;
	xfrm_address_t saddr;
	struct xfrm_selector sel;
	struct compat_xfrm_userpolicy_info policy;
	/* 4 bytes additional padding on 64bit */
	__u32 aalgos, ealgos, calgos, seq;
};

struct compat_xfrm_userspi_info {
	struct compat_xfrm_usersa_info info;
	/* 4 bytes additional padding on 64bit */
	__u32 min, max;
};

struct compat_xfrm_user_expire {
	struct compat_xfrm_usersa_info state;
	/* 8 bytes additional padding on 64bit */
	u8 hard;
};

struct compat_xfrm_user_polexpire {
	struct compat_xfrm_userpolicy_info pol;
	/* 8 bytes additional padding on 64bit */
	u8 hard;
};

#define XMSGSIZE(type) sizeof(struct type)

static const int compat_msg_min[XFRM_NR_MSGTYPES] = {
	[XFRM_MSG_NEWSA       - XFRM_MSG_BASE] = XMSGSIZE(compat_xfrm_usersa_info),
	[XFRM_MSG_DELSA       - XFRM_MSG_BASE] = XMSGSIZE(xfrm_usersa_id),
	[XFRM_MSG_GETSA       - XFRM_MSG_BASE] = XMSGSIZE(xfrm_usersa_id),
	[XFRM_MSG_NEWPOLICY   - XFRM_MSG_BASE] = XMSGSIZE(compat_xfrm_userpolicy_info),
	[XFRM_MSG_DELPOLICY   - XFRM_MSG_BASE] = XMSGSIZE(xfrm_userpolicy_id),
	[XFRM_MSG_GETPOLICY   - XFRM_MSG_BASE] = XMSGSIZE(xfrm_userpolicy_id),
	[XFRM_MSG_ALLOCSPI    - XFRM_MSG_BASE] = XMSGSIZE(compat_xfrm_userspi_info),
	[XFRM_MSG_ACQUIRE     - XFRM_MSG_BASE] = XMSGSIZE(compat_xfrm_user_acquire),
	[XFRM_MSG_EXPIRE      - XFRM_MSG_BASE] = XMSGSIZE(compat_xfrm_user_expire),
	[XFRM_MSG_UPDPOLICY   - XFRM_MSG_BASE] = XMSGSIZE(compat_xfrm_userpolicy_info),
	[XFRM_MSG_UPDSA       - XFRM_MSG_BASE] = XMSGSIZE(compat_xfrm_usersa_info),
	[XFRM_MSG_POLEXPIRE   - XFRM_MSG_BASE] = XMSGSIZE(compat_xfrm_user_polexpire),
	[XFRM_MSG_FLUSHSA     - XFRM_MSG_BASE] = XMSGSIZE(xfrm_usersa_flush),
	[XFRM_MSG_FLUSHPOLICY - XFRM_MSG_BASE] = 0,
	[XFRM_MSG_NEWAE       - XFRM_MSG_BASE] = XMSGSIZE(xfrm_aevent_id),
	[XFRM_MSG_GETAE       - XFRM_MSG_BASE] = XMSGSIZE(xfrm_aevent_id),
	[XFRM_MSG_REPORT      - XFRM_MSG_BASE] = XMSGSIZE(xfrm_user_report),
	[XFRM_MSG_MIGRATE     - XFRM_MSG_BASE] = XMSGSIZE(xfrm_userpolicy_id),
	[XFRM_MSG_NEWSADINFO  - XFRM_MSG_BASE] = sizeof(u32),
	[XFRM_MSG_GETSADINFO  - XFRM_MSG_BASE] = sizeof(u32),
	[XFRM_MSG_NEWSPDINFO  - XFRM_MSG_BASE] = sizeof(u32),
	[XFRM_MSG_GETSPDINFO  - XFRM_MSG_BASE] = sizeof(u32),
	[XFRM_MSG_MAPPING     - XFRM_MSG_BASE] = XMSGSIZE(xfrm_user_mapping)
};

static const struct nla_policy compat_policy[XFRMA_MAX+1] = {
	[XFRMA_SA]		= { .len = XMSGSIZE(compat_xfrm_usersa_info)},
	[XFRMA_POLICY]		= { .len = XMSGSIZE(compat_xfrm_userpolicy_info)},
	[XFRMA_LASTUSED]	= { .type = NLA_U64},
	[XFRMA_ALG_AUTH_TRUNC]	= { .len = sizeof(struct xfrm_algo_auth)},
	[XFRMA_ALG_AEAD]	= { .len = sizeof(struct xfrm_algo_aead) },
	[XFRMA_ALG_AUTH]	= { .len = sizeof(struct xfrm_algo) },
	[XFRMA_ALG_CRYPT]	= { .len = sizeof(struct xfrm_algo) },
	[XFRMA_ALG_COMP]	= { .len = sizeof(struct xfrm_algo) },
	[XFRMA_ENCAP]		= { .len = sizeof(struct xfrm_encap_tmpl) },
	[XFRMA_TMPL]		= { .len = sizeof(struct xfrm_user_tmpl) },
	[XFRMA_SEC_CTX]		= { .len = sizeof(struct xfrm_sec_ctx) },
	[XFRMA_LTIME_VAL]	= { .len = sizeof(struct xfrm_lifetime_cur) },
	[XFRMA_REPLAY_VAL]	= { .len = sizeof(struct xfrm_replay_state) },
	[XFRMA_REPLAY_THRESH]	= { .type = NLA_U32 },
	[XFRMA_ETIMER_THRESH]	= { .type = NLA_U32 },
	[XFRMA_SRCADDR]		= { .len = sizeof(xfrm_address_t) },
	[XFRMA_COADDR]		= { .len = sizeof(xfrm_address_t) },
	[XFRMA_POLICY_TYPE]	= { .len = sizeof(struct xfrm_userpolicy_type)},
	[XFRMA_MIGRATE]		= { .len = sizeof(struct xfrm_user_migrate) },
	[XFRMA_KMADDRESS]	= { .len = sizeof(struct xfrm_user_kmaddress) },
	[XFRMA_MARK]		= { .len = sizeof(struct xfrm_mark) },
	[XFRMA_TFCPAD]		= { .type = NLA_U32 },
	[XFRMA_REPLAY_ESN_VAL]	= { .len = sizeof(struct xfrm_replay_state_esn) },
	[XFRMA_SA_EXTRA_FLAGS]	= { .type = NLA_U32 },
	[XFRMA_PROTO]		= { .type = NLA_U8 },
	[XFRMA_ADDRESS_FILTER]	= { .len = sizeof(struct xfrm_address_filter) },
	[XFRMA_OFFLOAD_DEV]	= { .len = sizeof(struct xfrm_user_offload) },
	[XFRMA_SET_MARK]	= { .type = NLA_U32 },
	[XFRMA_SET_MARK_MASK]	= { .type = NLA_U32 },
	[XFRMA_IF_ID]		= { .type = NLA_U32 },
};

static struct nlmsghdr *xfrm_nlmsg_put_compat(struct sk_buff *skb,
			const struct nlmsghdr *nlh_src, u16 type)
{
	int payload = compat_msg_min[type];
	int src_len = xfrm_msg_min[type];
	struct nlmsghdr *nlh_dst;

	/* Compat messages are shorter or equal to native (+padding) */
	if (WARN_ON_ONCE(src_len < payload))
		return ERR_PTR(-EMSGSIZE);

	nlh_dst = nlmsg_put(skb, nlh_src->nlmsg_pid, nlh_src->nlmsg_seq,
			    nlh_src->nlmsg_type, payload, nlh_src->nlmsg_flags);
	if (!nlh_dst)
		return ERR_PTR(-EMSGSIZE);

	memset(nlmsg_data(nlh_dst), 0, payload);

	switch (nlh_src->nlmsg_type) {
	/* Compat message has the same layout as native */
	case XFRM_MSG_DELSA:
	case XFRM_MSG_DELPOLICY:
	case XFRM_MSG_FLUSHSA:
	case XFRM_MSG_FLUSHPOLICY:
	case XFRM_MSG_NEWAE:
	case XFRM_MSG_REPORT:
	case XFRM_MSG_MIGRATE:
	case XFRM_MSG_NEWSADINFO:
	case XFRM_MSG_NEWSPDINFO:
	case XFRM_MSG_MAPPING:
		WARN_ON_ONCE(src_len != payload);
		memcpy(nlmsg_data(nlh_dst), nlmsg_data(nlh_src), src_len);
		break;
	/* 4 byte alignment for trailing u64 on native, but not on compat */
	case XFRM_MSG_NEWSA:
	case XFRM_MSG_NEWPOLICY:
	case XFRM_MSG_UPDSA:
	case XFRM_MSG_UPDPOLICY:
		WARN_ON_ONCE(src_len != payload + 4);
		memcpy(nlmsg_data(nlh_dst), nlmsg_data(nlh_src), payload);
		break;
	case XFRM_MSG_EXPIRE: {
		const struct xfrm_user_expire *src_ue  = nlmsg_data(nlh_src);
		struct compat_xfrm_user_expire *dst_ue = nlmsg_data(nlh_dst);

		/* compat_xfrm_user_expire has 4-byte smaller state */
		memcpy(dst_ue, src_ue, sizeof(dst_ue->state));
		dst_ue->hard = src_ue->hard;
		break;
	}
	case XFRM_MSG_ACQUIRE: {
		const struct xfrm_user_acquire *src_ua  = nlmsg_data(nlh_src);
		struct compat_xfrm_user_acquire *dst_ua = nlmsg_data(nlh_dst);

		memcpy(dst_ua, src_ua, offsetof(struct compat_xfrm_user_acquire, aalgos));
		dst_ua->aalgos = src_ua->aalgos;
		dst_ua->ealgos = src_ua->ealgos;
		dst_ua->calgos = src_ua->calgos;
		dst_ua->seq    = src_ua->seq;
		break;
	}
	case XFRM_MSG_POLEXPIRE: {
		const struct xfrm_user_polexpire *src_upe  = nlmsg_data(nlh_src);
		struct compat_xfrm_user_polexpire *dst_upe = nlmsg_data(nlh_dst);

		/* compat_xfrm_user_polexpire has 4-byte smaller state */
		memcpy(dst_upe, src_upe, sizeof(dst_upe->pol));
		dst_upe->hard = src_upe->hard;
		break;
	}
	case XFRM_MSG_ALLOCSPI: {
		const struct xfrm_userspi_info *src_usi = nlmsg_data(nlh_src);
		struct compat_xfrm_userspi_info *dst_usi = nlmsg_data(nlh_dst);

		/* compat_xfrm_user_polexpire has 4-byte smaller state */
		memcpy(dst_usi, src_usi, sizeof(src_usi->info));
		dst_usi->min = src_usi->min;
		dst_usi->max = src_usi->max;
		break;
	}
	/* Not being sent by kernel */
	case XFRM_MSG_GETSA:
	case XFRM_MSG_GETPOLICY:
	case XFRM_MSG_GETAE:
	case XFRM_MSG_GETSADINFO:
	case XFRM_MSG_GETSPDINFO:
	default:
		pr_warn_once("unsupported nlmsg_type %d\n", nlh_src->nlmsg_type);
		return ERR_PTR(-EOPNOTSUPP);
	}

	return nlh_dst;
}

static int xfrm_nla_cpy(struct sk_buff *dst, const struct nlattr *src, int len)
{
	return nla_put(dst, src->nla_type, len, nla_data(src));
}

static int xfrm_xlate64_attr(struct sk_buff *dst, const struct nlattr *src)
{
	switch (src->nla_type) {
	case XFRMA_PAD:
		/* Ignore */
		return 0;
	case XFRMA_UNSPEC:
	case XFRMA_ALG_AUTH:
	case XFRMA_ALG_CRYPT:
	case XFRMA_ALG_COMP:
	case XFRMA_ENCAP:
	case XFRMA_TMPL:
		return xfrm_nla_cpy(dst, src, nla_len(src));
	case XFRMA_SA:
		return xfrm_nla_cpy(dst, src, XMSGSIZE(compat_xfrm_usersa_info));
	case XFRMA_POLICY:
		return xfrm_nla_cpy(dst, src, XMSGSIZE(compat_xfrm_userpolicy_info));
	case XFRMA_SEC_CTX:
		return xfrm_nla_cpy(dst, src, nla_len(src));
	case XFRMA_LTIME_VAL:
		return nla_put_64bit(dst, src->nla_type, nla_len(src),
			nla_data(src), XFRMA_PAD);
	case XFRMA_REPLAY_VAL:
	case XFRMA_REPLAY_THRESH:
	case XFRMA_ETIMER_THRESH:
	case XFRMA_SRCADDR:
	case XFRMA_COADDR:
		return xfrm_nla_cpy(dst, src, nla_len(src));
	case XFRMA_LASTUSED:
		return nla_put_64bit(dst, src->nla_type, nla_len(src),
			nla_data(src), XFRMA_PAD);
	case XFRMA_POLICY_TYPE:
	case XFRMA_MIGRATE:
	case XFRMA_ALG_AEAD:
	case XFRMA_KMADDRESS:
	case XFRMA_ALG_AUTH_TRUNC:
	case XFRMA_MARK:
	case XFRMA_TFCPAD:
	case XFRMA_REPLAY_ESN_VAL:
	case XFRMA_SA_EXTRA_FLAGS:
	case XFRMA_PROTO:
	case XFRMA_ADDRESS_FILTER:
	case XFRMA_OFFLOAD_DEV:
	case XFRMA_SET_MARK:
	case XFRMA_SET_MARK_MASK:
	case XFRMA_IF_ID:
		return xfrm_nla_cpy(dst, src, nla_len(src));
	default:
		BUILD_BUG_ON(XFRMA_MAX != XFRMA_IF_ID);
		pr_warn_once("unsupported nla_type %d\n", src->nla_type);
		return -EOPNOTSUPP;
	}
}

/* Take kernel-built (64bit layout) and create 32bit layout for userspace */
static int xfrm_xlate64(struct sk_buff *dst, const struct nlmsghdr *nlh_src)
{
	u16 type = nlh_src->nlmsg_type - XFRM_MSG_BASE;
	const struct nlattr *nla, *attrs;
	struct nlmsghdr *nlh_dst;
	int len, remaining;

	nlh_dst = xfrm_nlmsg_put_compat(dst, nlh_src, type);
	if (IS_ERR(nlh_dst))
		return PTR_ERR(nlh_dst);

	attrs = nlmsg_attrdata(nlh_src, xfrm_msg_min[type]);
	len = nlmsg_attrlen(nlh_src, xfrm_msg_min[type]);

	nla_for_each_attr(nla, attrs, len, remaining) {
		int err;

		switch (nlh_src->nlmsg_type) {
		case XFRM_MSG_NEWSPDINFO:
			err = xfrm_nla_cpy(dst, nla, nla_len(nla));
			break;
		default:
			err = xfrm_xlate64_attr(dst, nla);
			break;
		}
		if (err)
			return err;
	}

	nlmsg_end(dst, nlh_dst);

	return 0;
}

static int xfrm_alloc_compat(struct sk_buff *skb, const struct nlmsghdr *nlh_src)
{
	u16 type = nlh_src->nlmsg_type - XFRM_MSG_BASE;
	struct sk_buff *new = NULL;
	int err;

	if (type >= ARRAY_SIZE(xfrm_msg_min)) {
		pr_warn_once("unsupported nlmsg_type %d\n", nlh_src->nlmsg_type);
		return -EOPNOTSUPP;
	}

	if (skb_shinfo(skb)->frag_list == NULL) {
		new = alloc_skb(skb->len + skb_tailroom(skb), GFP_ATOMIC);
		if (!new)
			return -ENOMEM;
		skb_shinfo(skb)->frag_list = new;
	}

	err = xfrm_xlate64(skb_shinfo(skb)->frag_list, nlh_src);
	if (err) {
		if (new) {
			kfree_skb(new);
			skb_shinfo(skb)->frag_list = NULL;
		}
		return err;
	}

	return 0;
}

/* Calculates len of translated 64-bit message. */
static size_t xfrm_user_rcv_calculate_len64(const struct nlmsghdr *src,
					    struct nlattr *attrs[XFRMA_MAX + 1],
					    int maxtype)
{
	size_t len = nlmsg_len(src);

	switch (src->nlmsg_type) {
	case XFRM_MSG_NEWSA:
	case XFRM_MSG_NEWPOLICY:
	case XFRM_MSG_ALLOCSPI:
	case XFRM_MSG_ACQUIRE:
	case XFRM_MSG_UPDPOLICY:
	case XFRM_MSG_UPDSA:
		len += 4;
		break;
	case XFRM_MSG_EXPIRE:
	case XFRM_MSG_POLEXPIRE:
		len += 8;
		break;
	case XFRM_MSG_NEWSPDINFO:
		/* attirbutes are xfrm_spdattr_type_t, not xfrm_attr_type_t */
		return len;
	default:
		break;
	}

	/* Unexpected for anything, but XFRM_MSG_NEWSPDINFO, please
	 * correct both 64=>32-bit and 32=>64-bit translators to copy
	 * new attributes.
	 */
	if (WARN_ON_ONCE(maxtype))
		return len;

	if (attrs[XFRMA_SA])
		len += 4;
	if (attrs[XFRMA_POLICY])
		len += 4;

	/* XXX: some attrs may need to be realigned
	 * if !CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
	 */

	return len;
}

static int xfrm_attr_cpy32(void *dst, size_t *pos, const struct nlattr *src,
			   size_t size, int copy_len, int payload)
{
	struct nlmsghdr *nlmsg = dst;
	struct nlattr *nla;

	/* xfrm_user_rcv_msg_compat() relies on fact that 32-bit messages
	 * have the same len or shorted than 64-bit ones.
	 * 32-bit translation that is bigger than 64-bit original is unexpected.
	 */
	if (WARN_ON_ONCE(copy_len > payload))
		copy_len = payload;

	if (size - *pos < nla_attr_size(payload))
		return -ENOBUFS;

	nla = dst + *pos;

	memcpy(nla, src, nla_attr_size(copy_len));
	nla->nla_len = nla_attr_size(payload);
	*pos += nla_attr_size(copy_len);
	nlmsg->nlmsg_len += nla->nla_len;

	memset(dst + *pos, 0, payload - copy_len);
	*pos += payload - copy_len;

	return 0;
}

static int xfrm_xlate32_attr(void *dst, const struct nlattr *nla,
			     size_t *pos, size_t size,
			     struct netlink_ext_ack *extack)
{
	int type = nla_type(nla);
	u16 pol_len32, pol_len64;
	int err;

	if (type > XFRMA_MAX) {
		BUILD_BUG_ON(XFRMA_MAX != XFRMA_IF_ID);
		NL_SET_ERR_MSG(extack, "Bad attribute");
		return -EOPNOTSUPP;
	}
	type = array_index_nospec(type, XFRMA_MAX + 1);
	if (nla_len(nla) < compat_policy[type].len) {
		NL_SET_ERR_MSG(extack, "Attribute bad length");
		return -EOPNOTSUPP;
	}

	pol_len32 = compat_policy[type].len;
	pol_len64 = xfrma_policy[type].len;

	/* XFRMA_SA and XFRMA_POLICY - need to know how-to translate */
	if (pol_len32 != pol_len64) {
		if (nla_len(nla) != compat_policy[type].len) {
			NL_SET_ERR_MSG(extack, "Attribute bad length");
			return -EOPNOTSUPP;
		}
		err = xfrm_attr_cpy32(dst, pos, nla, size, pol_len32, pol_len64);
		if (err)
			return err;
	}

	return xfrm_attr_cpy32(dst, pos, nla, size, nla_len(nla), nla_len(nla));
}

static int xfrm_xlate32(struct nlmsghdr *dst, const struct nlmsghdr *src,
			struct nlattr *attrs[XFRMA_MAX+1],
			size_t size, u8 type, int maxtype,
			struct netlink_ext_ack *extack)
{
	size_t pos;
	int i;

	memcpy(dst, src, NLMSG_HDRLEN);
	dst->nlmsg_len = NLMSG_HDRLEN + xfrm_msg_min[type];
	memset(nlmsg_data(dst), 0, xfrm_msg_min[type]);

	switch (src->nlmsg_type) {
	/* Compat message has the same layout as native */
	case XFRM_MSG_DELSA:
	case XFRM_MSG_GETSA:
	case XFRM_MSG_DELPOLICY:
	case XFRM_MSG_GETPOLICY:
	case XFRM_MSG_FLUSHSA:
	case XFRM_MSG_FLUSHPOLICY:
	case XFRM_MSG_NEWAE:
	case XFRM_MSG_GETAE:
	case XFRM_MSG_REPORT:
	case XFRM_MSG_MIGRATE:
	case XFRM_MSG_NEWSADINFO:
	case XFRM_MSG_GETSADINFO:
	case XFRM_MSG_NEWSPDINFO:
	case XFRM_MSG_GETSPDINFO:
	case XFRM_MSG_MAPPING:
		memcpy(nlmsg_data(dst), nlmsg_data(src), compat_msg_min[type]);
		break;
	/* 4 byte alignment for trailing u64 on native, but not on compat */
	case XFRM_MSG_NEWSA:
	case XFRM_MSG_NEWPOLICY:
	case XFRM_MSG_UPDSA:
	case XFRM_MSG_UPDPOLICY:
		memcpy(nlmsg_data(dst), nlmsg_data(src), compat_msg_min[type]);
		break;
	case XFRM_MSG_EXPIRE: {
		const struct compat_xfrm_user_expire *src_ue = nlmsg_data(src);
		struct xfrm_user_expire *dst_ue = nlmsg_data(dst);

		/* compat_xfrm_user_expire has 4-byte smaller state */
		memcpy(dst_ue, src_ue, sizeof(src_ue->state));
		dst_ue->hard = src_ue->hard;
		break;
	}
	case XFRM_MSG_ACQUIRE: {
		const struct compat_xfrm_user_acquire *src_ua = nlmsg_data(src);
		struct xfrm_user_acquire *dst_ua = nlmsg_data(dst);

		memcpy(dst_ua, src_ua, offsetof(struct compat_xfrm_user_acquire, aalgos));
		dst_ua->aalgos = src_ua->aalgos;
		dst_ua->ealgos = src_ua->ealgos;
		dst_ua->calgos = src_ua->calgos;
		dst_ua->seq    = src_ua->seq;
		break;
	}
	case XFRM_MSG_POLEXPIRE: {
		const struct compat_xfrm_user_polexpire *src_upe = nlmsg_data(src);
		struct xfrm_user_polexpire *dst_upe = nlmsg_data(dst);

		/* compat_xfrm_user_polexpire has 4-byte smaller state */
		memcpy(dst_upe, src_upe, sizeof(src_upe->pol));
		dst_upe->hard = src_upe->hard;
		break;
	}
	case XFRM_MSG_ALLOCSPI: {
		const struct compat_xfrm_userspi_info *src_usi = nlmsg_data(src);
		struct xfrm_userspi_info *dst_usi = nlmsg_data(dst);

		/* compat_xfrm_user_polexpire has 4-byte smaller state */
		memcpy(dst_usi, src_usi, sizeof(src_usi->info));
		dst_usi->min = src_usi->min;
		dst_usi->max = src_usi->max;
		break;
	}
	default:
		NL_SET_ERR_MSG(extack, "Unsupported message type");
		return -EOPNOTSUPP;
	}
	pos = dst->nlmsg_len;

	if (maxtype) {
		/* attirbutes are xfrm_spdattr_type_t, not xfrm_attr_type_t */
		WARN_ON_ONCE(src->nlmsg_type != XFRM_MSG_NEWSPDINFO);

		for (i = 1; i <= maxtype; i++) {
			int err;

			if (!attrs[i])
				continue;

			/* just copy - no need for translation */
			err = xfrm_attr_cpy32(dst, &pos, attrs[i], size,
					nla_len(attrs[i]), nla_len(attrs[i]));
			if (err)
				return err;
		}
		return 0;
	}

	for (i = 1; i < XFRMA_MAX + 1; i++) {
		int err;

		if (i == XFRMA_PAD)
			continue;

		if (!attrs[i])
			continue;

		err = xfrm_xlate32_attr(dst, attrs[i], &pos, size, extack);
		if (err)
			return err;
	}

	return 0;
}

static struct nlmsghdr *xfrm_user_rcv_msg_compat(const struct nlmsghdr *h32,
			int maxtype, const struct nla_policy *policy,
			struct netlink_ext_ack *extack)
{
	/* netlink_rcv_skb() checks if a message has full (struct nlmsghdr) */
	u16 type = h32->nlmsg_type - XFRM_MSG_BASE;
	struct nlattr *attrs[XFRMA_MAX+1];
	struct nlmsghdr *h64;
	size_t len;
	int err;

	BUILD_BUG_ON(ARRAY_SIZE(xfrm_msg_min) != ARRAY_SIZE(compat_msg_min));

	if (type >= ARRAY_SIZE(xfrm_msg_min))
		return ERR_PTR(-EINVAL);

	/* Don't call parse: the message might have only nlmsg header */
	if ((h32->nlmsg_type == XFRM_MSG_GETSA ||
	     h32->nlmsg_type == XFRM_MSG_GETPOLICY) &&
	    (h32->nlmsg_flags & NLM_F_DUMP))
		return NULL;

	err = nlmsg_parse_deprecated(h32, compat_msg_min[type], attrs,
			maxtype ? : XFRMA_MAX, policy ? : compat_policy, extack);
	if (err < 0)
		return ERR_PTR(err);

	len = xfrm_user_rcv_calculate_len64(h32, attrs, maxtype);
	/* The message doesn't need translation */
	if (len == nlmsg_len(h32))
		return NULL;

	len += NLMSG_HDRLEN;
	h64 = kvmalloc(len, GFP_KERNEL);
	if (!h64)
		return ERR_PTR(-ENOMEM);

	err = xfrm_xlate32(h64, h32, attrs, len, type, maxtype, extack);
	if (err < 0) {
		kvfree(h64);
		return ERR_PTR(err);
	}

	return h64;
}

static int xfrm_user_policy_compat(u8 **pdata32, int optlen)
{
	struct compat_xfrm_userpolicy_info *p = (void *)*pdata32;
	u8 *src_templates, *dst_templates;
	u8 *data64;

	if (optlen < sizeof(*p))
		return -EINVAL;

	data64 = kmalloc_track_caller(optlen + 4, GFP_USER | __GFP_NOWARN);
	if (!data64)
		return -ENOMEM;

	memcpy(data64, *pdata32, sizeof(*p));
	memset(data64 + sizeof(*p), 0, 4);

	src_templates = *pdata32 + sizeof(*p);
	dst_templates = data64 + sizeof(*p) + 4;
	memcpy(dst_templates, src_templates, optlen - sizeof(*p));

	kfree(*pdata32);
	*pdata32 = data64;
	return 0;
}

static struct xfrm_translator xfrm_translator = {
	.owner				= THIS_MODULE,
	.alloc_compat			= xfrm_alloc_compat,
	.rcv_msg_compat			= xfrm_user_rcv_msg_compat,
	.xlate_user_policy_sockptr	= xfrm_user_policy_compat,
};

static int __init xfrm_compat_init(void)
{
	return xfrm_register_translator(&xfrm_translator);
}

static void __exit xfrm_compat_exit(void)
{
	xfrm_unregister_translator(&xfrm_translator);
}

module_init(xfrm_compat_init);
module_exit(xfrm_compat_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dmitry Safonov");
MODULE_DESCRIPTION("XFRM 32-bit compatibility layer");
