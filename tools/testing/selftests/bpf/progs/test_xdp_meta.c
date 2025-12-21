#include <stdbool.h>
#include <linux/bpf.h>
#include <linux/errno.h>
#include <linux/if_ether.h>
#include <linux/pkt_cls.h>

#include <bpf/bpf_endian.h>
#include <bpf/bpf_helpers.h>
#include "bpf_kfuncs.h"

#define META_SIZE 32

#define ctx_ptr(ctx, mem) (void *)(unsigned long)ctx->mem

/* Demonstrate passing metadata from XDP to TC using bpf_xdp_adjust_meta.
 *
 * The XDP program extracts a fixed-size payload following the Ethernet header
 * and stores it as packet metadata to test the driver's metadata support. The
 * TC program then verifies if the passed metadata is correct.
 */

bool test_pass;

static const __u8 smac_want[ETH_ALEN] = {
	0x12, 0x34, 0xDE, 0xAD, 0xBE, 0xEF,
};

static const __u8 meta_want[META_SIZE] = {
	0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
	0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
	0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
	0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
};

static bool check_smac(const struct ethhdr *eth)
{
	return !__builtin_memcmp(eth->h_source, smac_want, ETH_ALEN);
}

static bool check_metadata(const char *file, int line, __u8 *meta_have)
{
	if (!__builtin_memcmp(meta_have, meta_want, META_SIZE))
		return true;

	bpf_stream_printk(BPF_STREAM_STDERR,
			  "FAIL:%s:%d: metadata mismatch\n"
			  "  have:\n    %pI6\n    %pI6\n"
			  "  want:\n    %pI6\n    %pI6\n",
			  file, line,
			  &meta_have[0x00], &meta_have[0x10],
			  &meta_want[0x00], &meta_want[0x10]);
	return false;
}

#define check_metadata(meta_have) check_metadata(__FILE__, __LINE__, meta_have)

static bool check_skb_metadata(const char *file, int line, struct __sk_buff *skb)
{
	__u8 *data_meta = ctx_ptr(skb, data_meta);
	__u8 *data = ctx_ptr(skb, data);

	return data_meta + META_SIZE <= data && (check_metadata)(file, line, data_meta);
}

#define check_skb_metadata(skb) check_skb_metadata(__FILE__, __LINE__, skb)

SEC("tc")
int ing_cls(struct __sk_buff *ctx)
{
	__u8 *meta_have = ctx_ptr(ctx, data_meta);
	__u8 *data = ctx_ptr(ctx, data);

	if (meta_have + META_SIZE > data)
		goto out;

	if (!check_metadata(meta_have))
		goto out;

	test_pass = true;
out:
	return TC_ACT_SHOT;
}

/* Read from metadata using bpf_dynptr_read helper */
SEC("tc")
int ing_cls_dynptr_read(struct __sk_buff *ctx)
{
	__u8 meta_have[META_SIZE];
	struct bpf_dynptr meta;

	bpf_dynptr_from_skb_meta(ctx, 0, &meta);
	bpf_dynptr_read(meta_have, META_SIZE, &meta, 0, 0);

	if (!check_metadata(meta_have))
		goto out;

	test_pass = true;
out:
	return TC_ACT_SHOT;
}

/* Write to metadata using bpf_dynptr_write helper */
SEC("tc")
int ing_cls_dynptr_write(struct __sk_buff *ctx)
{
	struct bpf_dynptr data, meta;
	__u8 *src;

	bpf_dynptr_from_skb(ctx, 0, &data);
	src = bpf_dynptr_slice(&data, sizeof(struct ethhdr), NULL, META_SIZE);
	if (!src)
		return TC_ACT_SHOT;

	bpf_dynptr_from_skb_meta(ctx, 0, &meta);
	bpf_dynptr_write(&meta, 0, src, META_SIZE, 0);

	return TC_ACT_UNSPEC; /* pass */
}

/* Read from metadata using read-only dynptr slice */
SEC("tc")
int ing_cls_dynptr_slice(struct __sk_buff *ctx)
{
	struct bpf_dynptr meta;
	__u8 *meta_have;

	bpf_dynptr_from_skb_meta(ctx, 0, &meta);
	meta_have = bpf_dynptr_slice(&meta, 0, NULL, META_SIZE);
	if (!meta_have)
		goto out;

	if (!check_metadata(meta_have))
		goto out;

	test_pass = true;
out:
	return TC_ACT_SHOT;
}

/* Write to metadata using writeable dynptr slice */
SEC("tc")
int ing_cls_dynptr_slice_rdwr(struct __sk_buff *ctx)
{
	struct bpf_dynptr data, meta;
	__u8 *src, *dst;

	bpf_dynptr_from_skb(ctx, 0, &data);
	src = bpf_dynptr_slice(&data, sizeof(struct ethhdr), NULL, META_SIZE);
	if (!src)
		return TC_ACT_SHOT;

	bpf_dynptr_from_skb_meta(ctx, 0, &meta);
	dst = bpf_dynptr_slice_rdwr(&meta, 0, NULL, META_SIZE);
	if (!dst)
		return TC_ACT_SHOT;

	__builtin_memcpy(dst, src, META_SIZE);

	return TC_ACT_UNSPEC; /* pass */
}

/* Read skb metadata in chunks from various offsets in different ways. */
SEC("tc")
int ing_cls_dynptr_offset_rd(struct __sk_buff *ctx)
{
	const __u32 chunk_len = META_SIZE / 4;
	__u8 meta_have[META_SIZE];
	struct bpf_dynptr meta;
	__u8 *dst, *src;

	dst = meta_have;

	/* 1. Regular read */
	bpf_dynptr_from_skb_meta(ctx, 0, &meta);
	bpf_dynptr_read(dst, chunk_len, &meta, 0, 0);
	dst += chunk_len;

	/* 2. Read from an offset-adjusted dynptr */
	bpf_dynptr_adjust(&meta, chunk_len, bpf_dynptr_size(&meta));
	bpf_dynptr_read(dst, chunk_len, &meta, 0, 0);
	dst += chunk_len;

	/* 3. Read at an offset */
	bpf_dynptr_read(dst, chunk_len, &meta, chunk_len, 0);
	dst += chunk_len;

	/* 4. Read from a slice starting at an offset */
	src = bpf_dynptr_slice(&meta, 2 * chunk_len, NULL, chunk_len);
	if (!src)
		goto out;
	__builtin_memcpy(dst, src, chunk_len);

	if (!check_metadata(meta_have))
		goto out;

	test_pass = true;
out:
	return TC_ACT_SHOT;
}

/* Write skb metadata in chunks at various offsets in different ways. */
SEC("tc")
int ing_cls_dynptr_offset_wr(struct __sk_buff *ctx)
{
	const __u32 chunk_len = META_SIZE / 4;
	__u8 payload[META_SIZE];
	struct bpf_dynptr meta;
	__u8 *dst, *src;

	bpf_skb_load_bytes(ctx, sizeof(struct ethhdr), payload, sizeof(payload));
	src = payload;

	/* 1. Regular write */
	bpf_dynptr_from_skb_meta(ctx, 0, &meta);
	bpf_dynptr_write(&meta, 0, src, chunk_len, 0);
	src += chunk_len;

	/* 2. Write to an offset-adjusted dynptr */
	bpf_dynptr_adjust(&meta, chunk_len, bpf_dynptr_size(&meta));
	bpf_dynptr_write(&meta, 0, src, chunk_len, 0);
	src += chunk_len;

	/* 3. Write at an offset */
	bpf_dynptr_write(&meta, chunk_len, src, chunk_len, 0);
	src += chunk_len;

	/* 4. Write to a slice starting at an offset */
	dst = bpf_dynptr_slice_rdwr(&meta, 2 * chunk_len, NULL, chunk_len);
	if (!dst)
		return TC_ACT_SHOT;
	__builtin_memcpy(dst, src, chunk_len);

	return TC_ACT_UNSPEC; /* pass */
}

/* Pass an OOB offset to dynptr read, write, adjust, slice. */
SEC("tc")
int ing_cls_dynptr_offset_oob(struct __sk_buff *ctx)
{
	struct bpf_dynptr meta;
	__u8 md, *p;
	int err;

	err = bpf_dynptr_from_skb_meta(ctx, 0, &meta);
	if (err)
		goto fail;

	/* read offset OOB */
	err = bpf_dynptr_read(&md, sizeof(md), &meta, META_SIZE, 0);
	if (err != -E2BIG)
		goto fail;

	/* write offset OOB */
	err = bpf_dynptr_write(&meta, META_SIZE, &md, sizeof(md), 0);
	if (err != -E2BIG)
		goto fail;

	/* adjust end offset OOB */
	err = bpf_dynptr_adjust(&meta, 0, META_SIZE + 1);
	if (err != -ERANGE)
		goto fail;

	/* adjust start offset OOB */
	err = bpf_dynptr_adjust(&meta, META_SIZE + 1, META_SIZE + 1);
	if (err != -ERANGE)
		goto fail;

	/* slice offset OOB */
	p = bpf_dynptr_slice(&meta, META_SIZE, NULL, sizeof(*p));
	if (p)
		goto fail;

	/* slice rdwr offset OOB */
	p = bpf_dynptr_slice_rdwr(&meta, META_SIZE, NULL, sizeof(*p));
	if (p)
		goto fail;

	return TC_ACT_UNSPEC;
fail:
	return TC_ACT_SHOT;
}

/* Reserve and clear space for metadata but don't populate it */
SEC("xdp")
int ing_xdp_zalloc_meta(struct xdp_md *ctx)
{
	struct ethhdr *eth = ctx_ptr(ctx, data);
	__u8 *meta;
	int ret;

	/* Drop any non-test packets */
	if (eth + 1 > ctx_ptr(ctx, data_end))
		return XDP_DROP;
	if (!check_smac(eth))
		return XDP_DROP;

	ret = bpf_xdp_adjust_meta(ctx, -META_SIZE);
	if (ret < 0)
		return XDP_DROP;

	meta = ctx_ptr(ctx, data_meta);
	if (meta + META_SIZE > ctx_ptr(ctx, data))
		return XDP_DROP;

	__builtin_memset(meta, 0, META_SIZE);

	return XDP_PASS;
}

SEC("xdp")
int ing_xdp(struct xdp_md *ctx)
{
	__u8 *data, *data_meta, *data_end, *payload;
	struct ethhdr *eth;
	int ret;

	ret = bpf_xdp_adjust_meta(ctx, -META_SIZE);
	if (ret < 0)
		return XDP_DROP;

	data_meta = ctx_ptr(ctx, data_meta);
	data_end  = ctx_ptr(ctx, data_end);
	data      = ctx_ptr(ctx, data);

	eth = (struct ethhdr *)data;
	payload = data + sizeof(struct ethhdr);

	if (payload + META_SIZE > data_end ||
	    data_meta + META_SIZE > data)
		return XDP_DROP;

	/* The Linux networking stack may send other packets on the test
	 * interface that interfere with the test. Just drop them.
	 * The test packets can be recognized by their source MAC address.
	 */
	if (!check_smac(eth))
		return XDP_DROP;

	__builtin_memcpy(data_meta, payload, META_SIZE);
	return XDP_PASS;
}

/*
 * Check that, when operating on a cloned packet, skb->data_meta..skb->data is
 * kept intact if prog writes to packet _payload_ using packet pointers.
 */
SEC("tc")
int clone_data_meta_survives_data_write(struct __sk_buff *ctx)
{
	__u8 *meta_have = ctx_ptr(ctx, data_meta);
	struct ethhdr *eth = ctx_ptr(ctx, data);

	if (eth + 1 > ctx_ptr(ctx, data_end))
		goto out;
	/* Ignore non-test packets */
	if (!check_smac(eth))
		goto out;

	if (meta_have + META_SIZE > eth)
		goto out;

	if (!check_metadata(meta_have))
		goto out;

	/* Packet write to trigger unclone in prologue */
	eth->h_proto = 42;

	test_pass = true;
out:
	return TC_ACT_SHOT;
}

/*
 * Check that, when operating on a cloned packet, skb->data_meta..skb->data is
 * kept intact if prog writes to packet _metadata_ using packet pointers.
 */
SEC("tc")
int clone_data_meta_survives_meta_write(struct __sk_buff *ctx)
{
	__u8 *meta_have = ctx_ptr(ctx, data_meta);
	struct ethhdr *eth = ctx_ptr(ctx, data);

	if (eth + 1 > ctx_ptr(ctx, data_end))
		goto out;
	/* Ignore non-test packets */
	if (!check_smac(eth))
		goto out;

	if (meta_have + META_SIZE > eth)
		goto out;

	if (!check_metadata(meta_have))
		goto out;

	/* Metadata write to trigger unclone in prologue */
	*meta_have = 42;

	test_pass = true;
out:
	return TC_ACT_SHOT;
}

/*
 * Check that, when operating on a cloned packet, metadata remains intact if
 * prog creates a r/w slice to packet _payload_.
 */
SEC("tc")
int clone_meta_dynptr_survives_data_slice_write(struct __sk_buff *ctx)
{
	struct bpf_dynptr data, meta;
	__u8 meta_have[META_SIZE];
	struct ethhdr *eth;

	bpf_dynptr_from_skb(ctx, 0, &data);
	eth = bpf_dynptr_slice_rdwr(&data, 0, NULL, sizeof(*eth));
	if (!eth)
		goto out;
	/* Ignore non-test packets */
	if (!check_smac(eth))
		goto out;

	bpf_dynptr_from_skb_meta(ctx, 0, &meta);
	bpf_dynptr_read(meta_have, META_SIZE, &meta, 0, 0);
	if (!check_metadata(meta_have))
		goto out;

	test_pass = true;
out:
	return TC_ACT_SHOT;
}

/*
 * Check that, when operating on a cloned packet, metadata remains intact if
 * prog creates an r/w slice to packet _metadata_.
 */
SEC("tc")
int clone_meta_dynptr_survives_meta_slice_write(struct __sk_buff *ctx)
{
	struct bpf_dynptr data, meta;
	const struct ethhdr *eth;
	__u8 *meta_have;

	bpf_dynptr_from_skb(ctx, 0, &data);
	eth = bpf_dynptr_slice(&data, 0, NULL, sizeof(*eth));
	if (!eth)
		goto out;
	/* Ignore non-test packets */
	if (!check_smac(eth))
		goto out;

	bpf_dynptr_from_skb_meta(ctx, 0, &meta);
	meta_have = bpf_dynptr_slice_rdwr(&meta, 0, NULL, META_SIZE);
	if (!meta_have)
		goto out;

	if (!check_metadata(meta_have))
		goto out;

	test_pass = true;
out:
	return TC_ACT_SHOT;
}

/*
 * Check that, when operating on a cloned packet, skb_meta dynptr is read-write
 * before prog writes to packet _payload_ using dynptr_write helper and metadata
 * remains intact before and after the write.
 */
SEC("tc")
int clone_meta_dynptr_rw_before_data_dynptr_write(struct __sk_buff *ctx)
{
	struct bpf_dynptr data, meta;
	__u8 meta_have[META_SIZE];
	const struct ethhdr *eth;
	int err;

	bpf_dynptr_from_skb(ctx, 0, &data);
	eth = bpf_dynptr_slice(&data, 0, NULL, sizeof(*eth));
	if (!eth)
		goto out;
	/* Ignore non-test packets */
	if (!check_smac(eth))
		goto out;

	/* Expect read-write metadata before unclone */
	bpf_dynptr_from_skb_meta(ctx, 0, &meta);
	if (bpf_dynptr_is_rdonly(&meta))
		goto out;

	err = bpf_dynptr_read(meta_have, META_SIZE, &meta, 0, 0);
	if (err || !check_metadata(meta_have))
		goto out;

	/* Helper write to payload will unclone the packet */
	bpf_dynptr_write(&data, offsetof(struct ethhdr, h_proto), "x", 1, 0);

	err = bpf_dynptr_read(meta_have, META_SIZE, &meta, 0, 0);
	if (err || !check_metadata(meta_have))
		goto out;

	test_pass = true;
out:
	return TC_ACT_SHOT;
}

/*
 * Check that, when operating on a cloned packet, skb_meta dynptr is read-write
 * before prog writes to packet _metadata_ using dynptr_write helper and
 * metadata remains intact before and after the write.
 */
SEC("tc")
int clone_meta_dynptr_rw_before_meta_dynptr_write(struct __sk_buff *ctx)
{
	struct bpf_dynptr data, meta;
	__u8 meta_have[META_SIZE];
	const struct ethhdr *eth;
	int err;

	bpf_dynptr_from_skb(ctx, 0, &data);
	eth = bpf_dynptr_slice(&data, 0, NULL, sizeof(*eth));
	if (!eth)
		goto out;
	/* Ignore non-test packets */
	if (!check_smac(eth))
		goto out;

	/* Expect read-write metadata before unclone */
	bpf_dynptr_from_skb_meta(ctx, 0, &meta);
	if (bpf_dynptr_is_rdonly(&meta))
		goto out;

	err = bpf_dynptr_read(meta_have, META_SIZE, &meta, 0, 0);
	if (err || !check_metadata(meta_have))
		goto out;

	/* Helper write to metadata will unclone the packet */
	bpf_dynptr_write(&meta, 0, &meta_have[0], 1, 0);

	err = bpf_dynptr_read(meta_have, META_SIZE, &meta, 0, 0);
	if (err || !check_metadata(meta_have))
		goto out;

	test_pass = true;
out:
	return TC_ACT_SHOT;
}

SEC("tc")
int helper_skb_vlan_push_pop(struct __sk_buff *ctx)
{
	int err;

	/* bpf_skb_vlan_push assumes HW offload for primary VLAN tag. Only
	 * secondary tag push triggers an actual MAC header modification.
	 */
	err = bpf_skb_vlan_push(ctx, 0, 42);
	if (err)
		goto out;
	err = bpf_skb_vlan_push(ctx, 0, 207);
	if (err)
		goto out;

	if (!check_skb_metadata(ctx))
		goto out;

	err = bpf_skb_vlan_pop(ctx);
	if (err)
		goto out;
	err = bpf_skb_vlan_pop(ctx);
	if (err)
		goto out;

	if (!check_skb_metadata(ctx))
		goto out;

	test_pass = true;
out:
	return TC_ACT_SHOT;
}

SEC("tc")
int helper_skb_adjust_room(struct __sk_buff *ctx)
{
	int err;

	/* Grow a 1 byte hole after the MAC header */
	err = bpf_skb_adjust_room(ctx, 1, BPF_ADJ_ROOM_MAC, 0);
	if (err)
		goto out;

	if (!check_skb_metadata(ctx))
		goto out;

	/* Shrink a 1 byte hole after the MAC header */
	err = bpf_skb_adjust_room(ctx, -1, BPF_ADJ_ROOM_MAC, 0);
	if (err)
		goto out;

	if (!check_skb_metadata(ctx))
		goto out;

	/* Grow a 256 byte hole to trigger head reallocation */
	err = bpf_skb_adjust_room(ctx, 256, BPF_ADJ_ROOM_MAC, 0);
	if (err)
		goto out;

	if (!check_skb_metadata(ctx))
		goto out;

	test_pass = true;
out:
	return TC_ACT_SHOT;
}

SEC("tc")
int helper_skb_change_head_tail(struct __sk_buff *ctx)
{
	int err;

	/* Reserve 1 extra in the front for packet data */
	err = bpf_skb_change_head(ctx, 1, 0);
	if (err)
		goto out;

	if (!check_skb_metadata(ctx))
		goto out;

	/* Reserve 256 extra bytes in the front to trigger head reallocation */
	err = bpf_skb_change_head(ctx, 256, 0);
	if (err)
		goto out;

	if (!check_skb_metadata(ctx))
		goto out;

	/* Reserve 4k extra bytes in the back to trigger head reallocation */
	err = bpf_skb_change_tail(ctx, ctx->len + 4096, 0);
	if (err)
		goto out;

	if (!check_skb_metadata(ctx))
		goto out;

	test_pass = true;
out:
	return TC_ACT_SHOT;
}

SEC("tc")
int helper_skb_change_proto(struct __sk_buff *ctx)
{
	int err;

	err = bpf_skb_change_proto(ctx, bpf_htons(ETH_P_IPV6), 0);
	if (err)
		goto out;

	if (!check_skb_metadata(ctx))
		goto out;

	err = bpf_skb_change_proto(ctx, bpf_htons(ETH_P_IP), 0);
	if (err)
		goto out;

	if (!check_skb_metadata(ctx))
		goto out;

	test_pass = true;
out:
	return TC_ACT_SHOT;
}

char _license[] SEC("license") = "GPL";
