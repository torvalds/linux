/*
 * 842 Software Compression
 *
 * Copyright (C) 2015 Dan Streetman, IBM Corp
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * See 842.h for details of the 842 compressed format.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#define MODULE_NAME "842_compress"

#include <linux/hashtable.h>

#include "842.h"
#include "842_debugfs.h"

#define SW842_HASHTABLE8_BITS	(10)
#define SW842_HASHTABLE4_BITS	(11)
#define SW842_HASHTABLE2_BITS	(10)

/* By default, we allow compressing input buffers of any length, but we must
 * use the non-standard "short data" template so the decompressor can correctly
 * reproduce the uncompressed data buffer at the right length.  However the
 * hardware 842 compressor will not recognize the "short data" template, and
 * will fail to decompress any compressed buffer containing it (I have no idea
 * why anyone would want to use software to compress and hardware to decompress
 * but that's beside the point).  This parameter forces the compression
 * function to simply reject any input buffer that isn't a multiple of 8 bytes
 * long, instead of using the "short data" template, so that all compressed
 * buffers produced by this function will be decompressable by the 842 hardware
 * decompressor.  Unless you have a specific need for that, leave this disabled
 * so that any length buffer can be compressed.
 */
static bool sw842_strict;
module_param_named(strict, sw842_strict, bool, 0644);

static u8 comp_ops[OPS_MAX][5] = { /* params size in bits */
	{ I8, N0, N0, N0, 0x19 }, /* 8 */
	{ I4, I4, N0, N0, 0x18 }, /* 18 */
	{ I4, I2, I2, N0, 0x17 }, /* 25 */
	{ I2, I2, I4, N0, 0x13 }, /* 25 */
	{ I2, I2, I2, I2, 0x12 }, /* 32 */
	{ I4, I2, D2, N0, 0x16 }, /* 33 */
	{ I4, D2, I2, N0, 0x15 }, /* 33 */
	{ I2, D2, I4, N0, 0x0e }, /* 33 */
	{ D2, I2, I4, N0, 0x09 }, /* 33 */
	{ I2, I2, I2, D2, 0x11 }, /* 40 */
	{ I2, I2, D2, I2, 0x10 }, /* 40 */
	{ I2, D2, I2, I2, 0x0d }, /* 40 */
	{ D2, I2, I2, I2, 0x08 }, /* 40 */
	{ I4, D4, N0, N0, 0x14 }, /* 41 */
	{ D4, I4, N0, N0, 0x04 }, /* 41 */
	{ I2, I2, D4, N0, 0x0f }, /* 48 */
	{ I2, D2, I2, D2, 0x0c }, /* 48 */
	{ I2, D4, I2, N0, 0x0b }, /* 48 */
	{ D2, I2, I2, D2, 0x07 }, /* 48 */
	{ D2, I2, D2, I2, 0x06 }, /* 48 */
	{ D4, I2, I2, N0, 0x03 }, /* 48 */
	{ I2, D2, D4, N0, 0x0a }, /* 56 */
	{ D2, I2, D4, N0, 0x05 }, /* 56 */
	{ D4, I2, D2, N0, 0x02 }, /* 56 */
	{ D4, D2, I2, N0, 0x01 }, /* 56 */
	{ D8, N0, N0, N0, 0x00 }, /* 64 */
};

struct sw842_hlist_node8 {
	struct hlist_node node;
	u64 data;
	u8 index;
};

struct sw842_hlist_node4 {
	struct hlist_node node;
	u32 data;
	u16 index;
};

struct sw842_hlist_node2 {
	struct hlist_node node;
	u16 data;
	u8 index;
};

#define INDEX_NOT_FOUND		(-1)
#define INDEX_NOT_CHECKED	(-2)

struct sw842_param {
	u8 *in;
	u8 *instart;
	u64 ilen;
	u8 *out;
	u64 olen;
	u8 bit;
	u64 data8[1];
	u32 data4[2];
	u16 data2[4];
	int index8[1];
	int index4[2];
	int index2[4];
	DECLARE_HASHTABLE(htable8, SW842_HASHTABLE8_BITS);
	DECLARE_HASHTABLE(htable4, SW842_HASHTABLE4_BITS);
	DECLARE_HASHTABLE(htable2, SW842_HASHTABLE2_BITS);
	struct sw842_hlist_node8 node8[1 << I8_BITS];
	struct sw842_hlist_node4 node4[1 << I4_BITS];
	struct sw842_hlist_node2 node2[1 << I2_BITS];
};

#define get_input_data(p, o, b)						\
	be##b##_to_cpu(get_unaligned((__be##b *)((p)->in + (o))))

#define init_hashtable_nodes(p, b)	do {			\
	int _i;							\
	hash_init((p)->htable##b);				\
	for (_i = 0; _i < ARRAY_SIZE((p)->node##b); _i++) {	\
		(p)->node##b[_i].index = _i;			\
		(p)->node##b[_i].data = 0;			\
		INIT_HLIST_NODE(&(p)->node##b[_i].node);	\
	}							\
} while (0)

#define find_index(p, b, n)	({					\
	struct sw842_hlist_node##b *_n;					\
	p->index##b[n] = INDEX_NOT_FOUND;				\
	hash_for_each_possible(p->htable##b, _n, node, p->data##b[n]) {	\
		if (p->data##b[n] == _n->data) {			\
			p->index##b[n] = _n->index;			\
			break;						\
		}							\
	}								\
	p->index##b[n] >= 0;						\
})

#define check_index(p, b, n)			\
	((p)->index##b[n] == INDEX_NOT_CHECKED	\
	 ? find_index(p, b, n)			\
	 : (p)->index##b[n] >= 0)

#define replace_hash(p, b, i, d)	do {				\
	struct sw842_hlist_node##b *_n = &(p)->node##b[(i)+(d)];	\
	hash_del(&_n->node);						\
	_n->data = (p)->data##b[d];					\
	pr_debug("add hash index%x %x pos %x data %lx\n", b,		\
		 (unsigned int)_n->index,				\
		 (unsigned int)((p)->in - (p)->instart),		\
		 (unsigned long)_n->data);				\
	hash_add((p)->htable##b, &_n->node, _n->data);			\
} while (0)

static u8 bmask[8] = { 0x00, 0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe };

static int add_bits(struct sw842_param *p, u64 d, u8 n);

static int __split_add_bits(struct sw842_param *p, u64 d, u8 n, u8 s)
{
	int ret;

	if (n <= s)
		return -EINVAL;

	ret = add_bits(p, d >> s, n - s);
	if (ret)
		return ret;
	return add_bits(p, d & GENMASK_ULL(s - 1, 0), s);
}

static int add_bits(struct sw842_param *p, u64 d, u8 n)
{
	int b = p->bit, bits = b + n, s = round_up(bits, 8) - bits;
	u64 o;
	u8 *out = p->out;

	pr_debug("add %u bits %lx\n", (unsigned char)n, (unsigned long)d);

	if (n > 64)
		return -EINVAL;

	/* split this up if writing to > 8 bytes (i.e. n == 64 && p->bit > 0),
	 * or if we're at the end of the output buffer and would write past end
	 */
	if (bits > 64)
		return __split_add_bits(p, d, n, 32);
	else if (p->olen < 8 && bits > 32 && bits <= 56)
		return __split_add_bits(p, d, n, 16);
	else if (p->olen < 4 && bits > 16 && bits <= 24)
		return __split_add_bits(p, d, n, 8);

	if (DIV_ROUND_UP(bits, 8) > p->olen)
		return -ENOSPC;

	o = *out & bmask[b];
	d <<= s;

	if (bits <= 8)
		*out = o | d;
	else if (bits <= 16)
		put_unaligned(cpu_to_be16(o << 8 | d), (__be16 *)out);
	else if (bits <= 24)
		put_unaligned(cpu_to_be32(o << 24 | d << 8), (__be32 *)out);
	else if (bits <= 32)
		put_unaligned(cpu_to_be32(o << 24 | d), (__be32 *)out);
	else if (bits <= 40)
		put_unaligned(cpu_to_be64(o << 56 | d << 24), (__be64 *)out);
	else if (bits <= 48)
		put_unaligned(cpu_to_be64(o << 56 | d << 16), (__be64 *)out);
	else if (bits <= 56)
		put_unaligned(cpu_to_be64(o << 56 | d << 8), (__be64 *)out);
	else
		put_unaligned(cpu_to_be64(o << 56 | d), (__be64 *)out);

	p->bit += n;

	if (p->bit > 7) {
		p->out += p->bit / 8;
		p->olen -= p->bit / 8;
		p->bit %= 8;
	}

	return 0;
}

static int add_template(struct sw842_param *p, u8 c)
{
	int ret, i, b = 0;
	u8 *t = comp_ops[c];
	bool inv = false;

	if (c >= OPS_MAX)
		return -EINVAL;

	pr_debug("template %x\n", t[4]);

	ret = add_bits(p, t[4], OP_BITS);
	if (ret)
		return ret;

	for (i = 0; i < 4; i++) {
		pr_debug("op %x\n", t[i]);

		switch (t[i] & OP_AMOUNT) {
		case OP_AMOUNT_8:
			if (b)
				inv = true;
			else if (t[i] & OP_ACTION_INDEX)
				ret = add_bits(p, p->index8[0], I8_BITS);
			else if (t[i] & OP_ACTION_DATA)
				ret = add_bits(p, p->data8[0], 64);
			else
				inv = true;
			break;
		case OP_AMOUNT_4:
			if (b == 2 && t[i] & OP_ACTION_DATA)
				ret = add_bits(p, get_input_data(p, 2, 32), 32);
			else if (b != 0 && b != 4)
				inv = true;
			else if (t[i] & OP_ACTION_INDEX)
				ret = add_bits(p, p->index4[b >> 2], I4_BITS);
			else if (t[i] & OP_ACTION_DATA)
				ret = add_bits(p, p->data4[b >> 2], 32);
			else
				inv = true;
			break;
		case OP_AMOUNT_2:
			if (b != 0 && b != 2 && b != 4 && b != 6)
				inv = true;
			if (t[i] & OP_ACTION_INDEX)
				ret = add_bits(p, p->index2[b >> 1], I2_BITS);
			else if (t[i] & OP_ACTION_DATA)
				ret = add_bits(p, p->data2[b >> 1], 16);
			else
				inv = true;
			break;
		case OP_AMOUNT_0:
			inv = (b != 8) || !(t[i] & OP_ACTION_NOOP);
			break;
		default:
			inv = true;
			break;
		}

		if (ret)
			return ret;

		if (inv) {
			pr_err("Invalid templ %x op %d : %x %x %x %x\n",
			       c, i, t[0], t[1], t[2], t[3]);
			return -EINVAL;
		}

		b += t[i] & OP_AMOUNT;
	}

	if (b != 8) {
		pr_err("Invalid template %x len %x : %x %x %x %x\n",
		       c, b, t[0], t[1], t[2], t[3]);
		return -EINVAL;
	}

	if (sw842_template_counts)
		atomic_inc(&template_count[t[4]]);

	return 0;
}

static int add_repeat_template(struct sw842_param *p, u8 r)
{
	int ret;

	/* repeat param is 0-based */
	if (!r || --r > REPEAT_BITS_MAX)
		return -EINVAL;

	ret = add_bits(p, OP_REPEAT, OP_BITS);
	if (ret)
		return ret;

	ret = add_bits(p, r, REPEAT_BITS);
	if (ret)
		return ret;

	if (sw842_template_counts)
		atomic_inc(&template_repeat_count);

	return 0;
}

static int add_short_data_template(struct sw842_param *p, u8 b)
{
	int ret, i;

	if (!b || b > SHORT_DATA_BITS_MAX)
		return -EINVAL;

	ret = add_bits(p, OP_SHORT_DATA, OP_BITS);
	if (ret)
		return ret;

	ret = add_bits(p, b, SHORT_DATA_BITS);
	if (ret)
		return ret;

	for (i = 0; i < b; i++) {
		ret = add_bits(p, p->in[i], 8);
		if (ret)
			return ret;
	}

	if (sw842_template_counts)
		atomic_inc(&template_short_data_count);

	return 0;
}

static int add_zeros_template(struct sw842_param *p)
{
	int ret = add_bits(p, OP_ZEROS, OP_BITS);

	if (ret)
		return ret;

	if (sw842_template_counts)
		atomic_inc(&template_zeros_count);

	return 0;
}

static int add_end_template(struct sw842_param *p)
{
	int ret = add_bits(p, OP_END, OP_BITS);

	if (ret)
		return ret;

	if (sw842_template_counts)
		atomic_inc(&template_end_count);

	return 0;
}

static bool check_template(struct sw842_param *p, u8 c)
{
	u8 *t = comp_ops[c];
	int i, match, b = 0;

	if (c >= OPS_MAX)
		return false;

	for (i = 0; i < 4; i++) {
		if (t[i] & OP_ACTION_INDEX) {
			if (t[i] & OP_AMOUNT_2)
				match = check_index(p, 2, b >> 1);
			else if (t[i] & OP_AMOUNT_4)
				match = check_index(p, 4, b >> 2);
			else if (t[i] & OP_AMOUNT_8)
				match = check_index(p, 8, 0);
			else
				return false;
			if (!match)
				return false;
		}

		b += t[i] & OP_AMOUNT;
	}

	return true;
}

static void get_next_data(struct sw842_param *p)
{
	p->data8[0] = get_input_data(p, 0, 64);
	p->data4[0] = get_input_data(p, 0, 32);
	p->data4[1] = get_input_data(p, 4, 32);
	p->data2[0] = get_input_data(p, 0, 16);
	p->data2[1] = get_input_data(p, 2, 16);
	p->data2[2] = get_input_data(p, 4, 16);
	p->data2[3] = get_input_data(p, 6, 16);
}

/* update the hashtable entries.
 * only call this after finding/adding the current template
 * the dataN fields for the current 8 byte block must be already updated
 */
static void update_hashtables(struct sw842_param *p)
{
	u64 pos = p->in - p->instart;
	u64 n8 = (pos >> 3) % (1 << I8_BITS);
	u64 n4 = (pos >> 2) % (1 << I4_BITS);
	u64 n2 = (pos >> 1) % (1 << I2_BITS);

	replace_hash(p, 8, n8, 0);
	replace_hash(p, 4, n4, 0);
	replace_hash(p, 4, n4, 1);
	replace_hash(p, 2, n2, 0);
	replace_hash(p, 2, n2, 1);
	replace_hash(p, 2, n2, 2);
	replace_hash(p, 2, n2, 3);
}

/* find the next template to use, and add it
 * the p->dataN fields must already be set for the current 8 byte block
 */
static int process_next(struct sw842_param *p)
{
	int ret, i;

	p->index8[0] = INDEX_NOT_CHECKED;
	p->index4[0] = INDEX_NOT_CHECKED;
	p->index4[1] = INDEX_NOT_CHECKED;
	p->index2[0] = INDEX_NOT_CHECKED;
	p->index2[1] = INDEX_NOT_CHECKED;
	p->index2[2] = INDEX_NOT_CHECKED;
	p->index2[3] = INDEX_NOT_CHECKED;

	/* check up to OPS_MAX - 1; last op is our fallback */
	for (i = 0; i < OPS_MAX - 1; i++) {
		if (check_template(p, i))
			break;
	}

	ret = add_template(p, i);
	if (ret)
		return ret;

	return 0;
}

/**
 * sw842_compress
 *
 * Compress the uncompressed buffer of length @ilen at @in to the output buffer
 * @out, using no more than @olen bytes, using the 842 compression format.
 *
 * Returns: 0 on success, error on failure.  The @olen parameter
 * will contain the number of output bytes written on success, or
 * 0 on error.
 */
int sw842_compress(const u8 *in, unsigned int ilen,
		   u8 *out, unsigned int *olen, void *wmem)
{
	struct sw842_param *p = (struct sw842_param *)wmem;
	int ret;
	u64 last, next, pad, total;
	u8 repeat_count = 0;

	BUILD_BUG_ON(sizeof(*p) > SW842_MEM_COMPRESS);

	init_hashtable_nodes(p, 8);
	init_hashtable_nodes(p, 4);
	init_hashtable_nodes(p, 2);

	p->in = (u8 *)in;
	p->instart = p->in;
	p->ilen = ilen;
	p->out = out;
	p->olen = *olen;
	p->bit = 0;

	total = p->olen;

	*olen = 0;

	/* if using strict mode, we can only compress a multiple of 8 */
	if (sw842_strict && (ilen % 8)) {
		pr_err("Using strict mode, can't compress len %d\n", ilen);
		return -EINVAL;
	}

	/* let's compress at least 8 bytes, mkay? */
	if (unlikely(ilen < 8))
		goto skip_comp;

	/* make initial 'last' different so we don't match the first time */
	last = ~get_unaligned((u64 *)p->in);

	while (p->ilen > 7) {
		next = get_unaligned((u64 *)p->in);

		/* must get the next data, as we need to update the hashtable
		 * entries with the new data every time
		 */
		get_next_data(p);

		/* we don't care about endianness in last or next;
		 * we're just comparing 8 bytes to another 8 bytes,
		 * they're both the same endianness
		 */
		if (next == last) {
			/* repeat count bits are 0-based, so we stop at +1 */
			if (++repeat_count <= REPEAT_BITS_MAX)
				goto repeat;
		}
		if (repeat_count) {
			ret = add_repeat_template(p, repeat_count);
			repeat_count = 0;
			if (next == last) /* reached max repeat bits */
				goto repeat;
		}

		if (next == 0)
			ret = add_zeros_template(p);
		else
			ret = process_next(p);

		if (ret)
			return ret;

repeat:
		last = next;
		update_hashtables(p);
		p->in += 8;
		p->ilen -= 8;
	}

	if (repeat_count) {
		ret = add_repeat_template(p, repeat_count);
		if (ret)
			return ret;
	}

skip_comp:
	if (p->ilen > 0) {
		ret = add_short_data_template(p, p->ilen);
		if (ret)
			return ret;

		p->in += p->ilen;
		p->ilen = 0;
	}

	ret = add_end_template(p);
	if (ret)
		return ret;

	if (p->bit) {
		p->out++;
		p->olen--;
		p->bit = 0;
	}

	/* pad compressed length to multiple of 8 */
	pad = (8 - ((total - p->olen) % 8)) % 8;
	if (pad) {
		if (pad > p->olen) /* we were so close! */
			return -ENOSPC;
		memset(p->out, 0, pad);
		p->out += pad;
		p->olen -= pad;
	}

	if (unlikely((total - p->olen) > UINT_MAX))
		return -ENOSPC;

	*olen = total - p->olen;

	return 0;
}
EXPORT_SYMBOL_GPL(sw842_compress);

static int __init sw842_init(void)
{
	if (sw842_template_counts)
		sw842_debugfs_create();

	return 0;
}
module_init(sw842_init);

static void __exit sw842_exit(void)
{
	if (sw842_template_counts)
		sw842_debugfs_remove();
}
module_exit(sw842_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Software 842 Compressor");
MODULE_AUTHOR("Dan Streetman <ddstreet@ieee.org>");
