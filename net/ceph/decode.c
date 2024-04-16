// SPDX-License-Identifier: GPL-2.0
#include <linux/ceph/ceph_debug.h>

#include <linux/inet.h>

#include <linux/ceph/decode.h>
#include <linux/ceph/messenger.h>  /* for ceph_pr_addr() */

static int
ceph_decode_entity_addr_versioned(void **p, void *end,
				  struct ceph_entity_addr *addr)
{
	int ret;
	u8 struct_v;
	u32 struct_len, addr_len;
	void *struct_end;

	ret = ceph_start_decoding(p, end, 1, "entity_addr_t", &struct_v,
				  &struct_len);
	if (ret)
		goto bad;

	ret = -EINVAL;
	struct_end = *p + struct_len;

	ceph_decode_copy_safe(p, end, &addr->type, sizeof(addr->type), bad);

	ceph_decode_copy_safe(p, end, &addr->nonce, sizeof(addr->nonce), bad);

	ceph_decode_32_safe(p, end, addr_len, bad);
	if (addr_len > sizeof(addr->in_addr))
		goto bad;

	memset(&addr->in_addr, 0, sizeof(addr->in_addr));
	if (addr_len) {
		ceph_decode_copy_safe(p, end, &addr->in_addr, addr_len, bad);

		addr->in_addr.ss_family =
			le16_to_cpu((__force __le16)addr->in_addr.ss_family);
	}

	/* Advance past anything the client doesn't yet understand */
	*p = struct_end;
	ret = 0;
bad:
	return ret;
}

static int
ceph_decode_entity_addr_legacy(void **p, void *end,
			       struct ceph_entity_addr *addr)
{
	int ret = -EINVAL;

	/* Skip rest of type field */
	ceph_decode_skip_n(p, end, 3, bad);

	/*
	 * Clients that don't support ADDR2 always send TYPE_NONE, change it
	 * to TYPE_LEGACY for forward compatibility.
	 */
	addr->type = CEPH_ENTITY_ADDR_TYPE_LEGACY;
	ceph_decode_copy_safe(p, end, &addr->nonce, sizeof(addr->nonce), bad);
	memset(&addr->in_addr, 0, sizeof(addr->in_addr));
	ceph_decode_copy_safe(p, end, &addr->in_addr,
			      sizeof(addr->in_addr), bad);
	addr->in_addr.ss_family =
			be16_to_cpu((__force __be16)addr->in_addr.ss_family);
	ret = 0;
bad:
	return ret;
}

int
ceph_decode_entity_addr(void **p, void *end, struct ceph_entity_addr *addr)
{
	u8 marker;

	ceph_decode_8_safe(p, end, marker, bad);
	if (marker == 1)
		return ceph_decode_entity_addr_versioned(p, end, addr);
	else if (marker == 0)
		return ceph_decode_entity_addr_legacy(p, end, addr);
bad:
	return -EINVAL;
}
EXPORT_SYMBOL(ceph_decode_entity_addr);

/*
 * Return addr of desired type (MSGR2 or LEGACY) or error.
 * Make sure there is only one match.
 *
 * Assume encoding with MSG_ADDR2.
 */
int ceph_decode_entity_addrvec(void **p, void *end, bool msgr2,
			       struct ceph_entity_addr *addr)
{
	__le32 my_type = msgr2 ? CEPH_ENTITY_ADDR_TYPE_MSGR2 :
				 CEPH_ENTITY_ADDR_TYPE_LEGACY;
	struct ceph_entity_addr tmp_addr;
	int addr_cnt;
	bool found;
	u8 marker;
	int ret;
	int i;

	ceph_decode_8_safe(p, end, marker, e_inval);
	if (marker != 2) {
		pr_err("bad addrvec marker %d\n", marker);
		return -EINVAL;
	}

	ceph_decode_32_safe(p, end, addr_cnt, e_inval);
	dout("%s addr_cnt %d\n", __func__, addr_cnt);

	found = false;
	for (i = 0; i < addr_cnt; i++) {
		ret = ceph_decode_entity_addr(p, end, &tmp_addr);
		if (ret)
			return ret;

		dout("%s i %d addr %s\n", __func__, i, ceph_pr_addr(&tmp_addr));
		if (tmp_addr.type == my_type) {
			if (found) {
				pr_err("another match of type %d in addrvec\n",
				       le32_to_cpu(my_type));
				return -EINVAL;
			}

			memcpy(addr, &tmp_addr, sizeof(*addr));
			found = true;
		}
	}

	if (found)
		return 0;

	if (!addr_cnt)
		return 0;  /* normal -- e.g. unused OSD id/slot */

	if (addr_cnt == 1 && !memchr_inv(&tmp_addr, 0, sizeof(tmp_addr)))
		return 0;  /* weird but effectively the same as !addr_cnt */

	pr_err("no match of type %d in addrvec\n", le32_to_cpu(my_type));
	return -ENOENT;

e_inval:
	return -EINVAL;
}
EXPORT_SYMBOL(ceph_decode_entity_addrvec);

static int get_sockaddr_encoding_len(sa_family_t family)
{
	union {
		struct sockaddr sa;
		struct sockaddr_in sin;
		struct sockaddr_in6 sin6;
	} u;

	switch (family) {
	case AF_INET:
		return sizeof(u.sin);
	case AF_INET6:
		return sizeof(u.sin6);
	default:
		return sizeof(u);
	}
}

int ceph_entity_addr_encoding_len(const struct ceph_entity_addr *addr)
{
	sa_family_t family = get_unaligned(&addr->in_addr.ss_family);
	int addr_len = get_sockaddr_encoding_len(family);

	return 1 + CEPH_ENCODING_START_BLK_LEN + 4 + 4 + 4 + addr_len;
}

void ceph_encode_entity_addr(void **p, const struct ceph_entity_addr *addr)
{
	sa_family_t family = get_unaligned(&addr->in_addr.ss_family);
	int addr_len = get_sockaddr_encoding_len(family);

	ceph_encode_8(p, 1);  /* marker */
	ceph_start_encoding(p, 1, 1, sizeof(addr->type) +
				     sizeof(addr->nonce) +
				     sizeof(u32) + addr_len);
	ceph_encode_copy(p, &addr->type, sizeof(addr->type));
	ceph_encode_copy(p, &addr->nonce, sizeof(addr->nonce));

	ceph_encode_32(p, addr_len);
	ceph_encode_16(p, family);
	ceph_encode_copy(p, addr->in_addr.__data, addr_len - sizeof(family));
}
