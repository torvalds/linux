// SPDX-License-Identifier: GPL-2.0

#include <linux/ceph/decode.h>

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

	/*
	 * TYPE_NONE == 0
	 * TYPE_LEGACY == 1
	 *
	 * Clients that don't support ADDR2 always send TYPE_NONE.
	 * For now, since all we support is msgr1, just set this to 0
	 * when we get a TYPE_LEGACY type.
	 */
	if (addr->type == cpu_to_le32(1))
		addr->type = 0;

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
	addr->type = 0;
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

