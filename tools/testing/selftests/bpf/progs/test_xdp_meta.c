#include <stdbool.h>
#include <linux/bpf.h>
#include <linux/errno.h>
#include <linux/if_ether.h>
#include <linux/pkt_cls.h>

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

static const __u8 meta_want[META_SIZE] = {
	0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
	0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
	0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
	0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
};

SEC("tc")
int ing_cls(struct __sk_buff *ctx)
{
	__u8 *meta_have = ctx_ptr(ctx, data_meta);
	__u8 *data = ctx_ptr(ctx, data);

	if (meta_have + META_SIZE > data)
		goto out;

	if (__builtin_memcmp(meta_want, meta_have, META_SIZE))
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

	if (__builtin_memcmp(meta_want, meta_have, META_SIZE))
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

	if (__builtin_memcmp(meta_want, meta_have, META_SIZE))
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

	if (__builtin_memcmp(meta_want, meta_have, META_SIZE))
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
	if (eth->h_proto != 0)
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
	 * The test packets can be recognized by their ethertype of zero.
	 */
	if (eth->h_proto != 0)
		return XDP_DROP;

	__builtin_memcpy(data_meta, payload, META_SIZE);
	return XDP_PASS;
}

/*
 * Check that skb->data_meta..skb->data is empty if prog writes to packet
 * _payload_ using packet pointers. Applies only to cloned skbs.
 */
SEC("tc")
int clone_data_meta_empty_on_data_write(struct __sk_buff *ctx)
{
	struct ethhdr *eth = ctx_ptr(ctx, data);

	if (eth + 1 > ctx_ptr(ctx, data_end))
		goto out;
	/* Ignore non-test packets */
	if (eth->h_proto != 0)
		goto out;

	/* Expect no metadata */
	if (ctx->data_meta != ctx->data)
		goto out;

	/* Packet write to trigger unclone in prologue */
	eth->h_proto = 42;

	test_pass = true;
out:
	return TC_ACT_SHOT;
}

/*
 * Check that skb->data_meta..skb->data is empty if prog writes to packet
 * _metadata_ using packet pointers. Applies only to cloned skbs.
 */
SEC("tc")
int clone_data_meta_empty_on_meta_write(struct __sk_buff *ctx)
{
	struct ethhdr *eth = ctx_ptr(ctx, data);
	__u8 *md = ctx_ptr(ctx, data_meta);

	if (eth + 1 > ctx_ptr(ctx, data_end))
		goto out;
	/* Ignore non-test packets */
	if (eth->h_proto != 0)
		goto out;

	if (md + 1 > ctx_ptr(ctx, data)) {
		/* Expect no metadata */
		test_pass = true;
	} else {
		/* Metadata write to trigger unclone in prologue */
		*md = 42;
	}
out:
	return TC_ACT_SHOT;
}

/*
 * Check that skb_meta dynptr is writable but empty if prog writes to packet
 * _payload_ using a dynptr slice. Applies only to cloned skbs.
 */
SEC("tc")
int clone_dynptr_empty_on_data_slice_write(struct __sk_buff *ctx)
{
	struct bpf_dynptr data, meta;
	struct ethhdr *eth;

	bpf_dynptr_from_skb(ctx, 0, &data);
	eth = bpf_dynptr_slice_rdwr(&data, 0, NULL, sizeof(*eth));
	if (!eth)
		goto out;
	/* Ignore non-test packets */
	if (eth->h_proto != 0)
		goto out;

	/* Expect no metadata */
	bpf_dynptr_from_skb_meta(ctx, 0, &meta);
	if (bpf_dynptr_is_rdonly(&meta) || bpf_dynptr_size(&meta) > 0)
		goto out;

	/* Packet write to trigger unclone in prologue */
	eth->h_proto = 42;

	test_pass = true;
out:
	return TC_ACT_SHOT;
}

/*
 * Check that skb_meta dynptr is writable but empty if prog writes to packet
 * _metadata_ using a dynptr slice. Applies only to cloned skbs.
 */
SEC("tc")
int clone_dynptr_empty_on_meta_slice_write(struct __sk_buff *ctx)
{
	struct bpf_dynptr data, meta;
	const struct ethhdr *eth;
	__u8 *md;

	bpf_dynptr_from_skb(ctx, 0, &data);
	eth = bpf_dynptr_slice(&data, 0, NULL, sizeof(*eth));
	if (!eth)
		goto out;
	/* Ignore non-test packets */
	if (eth->h_proto != 0)
		goto out;

	/* Expect no metadata */
	bpf_dynptr_from_skb_meta(ctx, 0, &meta);
	if (bpf_dynptr_is_rdonly(&meta) || bpf_dynptr_size(&meta) > 0)
		goto out;

	/* Metadata write to trigger unclone in prologue */
	bpf_dynptr_from_skb_meta(ctx, 0, &meta);
	md = bpf_dynptr_slice_rdwr(&meta, 0, NULL, sizeof(*md));
	if (md)
		*md = 42;

	test_pass = true;
out:
	return TC_ACT_SHOT;
}

/*
 * Check that skb_meta dynptr is read-only before prog writes to packet payload
 * using dynptr_write helper. Applies only to cloned skbs.
 */
SEC("tc")
int clone_dynptr_rdonly_before_data_dynptr_write(struct __sk_buff *ctx)
{
	struct bpf_dynptr data, meta;
	const struct ethhdr *eth;

	bpf_dynptr_from_skb(ctx, 0, &data);
	eth = bpf_dynptr_slice(&data, 0, NULL, sizeof(*eth));
	if (!eth)
		goto out;
	/* Ignore non-test packets */
	if (eth->h_proto != 0)
		goto out;

	/* Expect read-only metadata before unclone */
	bpf_dynptr_from_skb_meta(ctx, 0, &meta);
	if (!bpf_dynptr_is_rdonly(&meta) || bpf_dynptr_size(&meta) != META_SIZE)
		goto out;

	/* Helper write to payload will unclone the packet */
	bpf_dynptr_write(&data, offsetof(struct ethhdr, h_proto), "x", 1, 0);

	/* Expect no metadata after unclone */
	bpf_dynptr_from_skb_meta(ctx, 0, &meta);
	if (bpf_dynptr_is_rdonly(&meta) || bpf_dynptr_size(&meta) != 0)
		goto out;

	test_pass = true;
out:
	return TC_ACT_SHOT;
}

/*
 * Check that skb_meta dynptr is read-only if prog writes to packet
 * metadata using dynptr_write helper. Applies only to cloned skbs.
 */
SEC("tc")
int clone_dynptr_rdonly_before_meta_dynptr_write(struct __sk_buff *ctx)
{
	struct bpf_dynptr data, meta;
	const struct ethhdr *eth;

	bpf_dynptr_from_skb(ctx, 0, &data);
	eth = bpf_dynptr_slice(&data, 0, NULL, sizeof(*eth));
	if (!eth)
		goto out;
	/* Ignore non-test packets */
	if (eth->h_proto != 0)
		goto out;

	/* Expect read-only metadata */
	bpf_dynptr_from_skb_meta(ctx, 0, &meta);
	if (!bpf_dynptr_is_rdonly(&meta) || bpf_dynptr_size(&meta) != META_SIZE)
		goto out;

	/* Metadata write. Expect failure. */
	bpf_dynptr_from_skb_meta(ctx, 0, &meta);
	if (bpf_dynptr_write(&meta, 0, "x", 1, 0) != -EINVAL)
		goto out;

	test_pass = true;
out:
	return TC_ACT_SHOT;
}

char _license[] SEC("license") = "GPL";
