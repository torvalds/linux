// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024, SUSE LLC
 *
 * Authors: Enzo Matsumiya <ematsumiya@suse.de>
 *
 * This file implements I/O compression support for SMB2 messages (SMB 3.1.1 only).
 * See compress/ for implementation details of each algorithm.
 *
 * References:
 * MS-SMB2 "3.1.4.4 Compressing the Message"
 * MS-SMB2 "3.1.5.3 Decompressing the Chained Message"
 * MS-XCA - for details of the supported algorithms
 */
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/uio.h>
#include <linux/sort.h>

#include "cifsglob.h"
#include "../common/smb2pdu.h"
#include "cifsproto.h"
#include "smb2proto.h"

#include "compress/lz77.h"
#include "compress.h"

/*
 * The heuristic_*() functions below try to determine data compressibility.
 *
 * Derived from fs/btrfs/compression.c, changing coding style, some parameters, and removing
 * unused parts.
 *
 * Read that file for better and more detailed explanation of the calculations.
 *
 * The algorithms are ran in a collected sample of the input (uncompressed) data.
 * The sample is formed of 2K reads in PAGE_SIZE intervals, with a maximum size of 4M.
 *
 * Parsing the sample goes from "low-hanging fruits" (fastest algorithms, likely compressible)
 * to "need more analysis" (likely uncompressible).
 */

struct bucket {
	unsigned int count;
};

/**
 * has_low_entropy() - Compute Shannon entropy of the sampled data.
 * @bkt:	Bytes counts of the sample.
 * @slen:	Size of the sample.
 *
 * Return: true if the level (percentage of number of bits that would be required to
 *	   compress the data) is below the minimum threshold.
 *
 * Note:
 * There _is_ an entropy level here that's > 65 (minimum threshold) that would indicate a
 * possibility of compression, but compressing, or even further analysing, it would waste so much
 * resources that it's simply not worth it.
 *
 * Also Shannon entropy is the last computed heuristic; if we got this far and ended up
 * with uncertainty, just stay on the safe side and call it uncompressible.
 */
static bool has_low_entropy(struct bucket *bkt, size_t slen)
{
	const size_t threshold = 65, max_entropy = 8 * ilog2(16);
	size_t i, p, p2, len, sum = 0;

#define pow4(n) (n * n * n * n)
	len = ilog2(pow4(slen));

	for (i = 0; i < 256 && bkt[i].count > 0; i++) {
		p = bkt[i].count;
		p2 = ilog2(pow4(p));
		sum += p * (len - p2);
	}

	sum /= slen;

	return ((sum * 100 / max_entropy) <= threshold);
}

#define BYTE_DIST_BAD		0
#define BYTE_DIST_GOOD		1
#define BYTE_DIST_MAYBE		2
/**
 * calc_byte_distribution() - Compute byte distribution on the sampled data.
 * @bkt:	Byte counts of the sample.
 * @slen:	Size of the sample.
 *
 * Return:
 * BYTE_DIST_BAD:	A "hard no" for compression -- a computed uniform distribution of
 *			the bytes (e.g. random or encrypted data).
 * BYTE_DIST_GOOD:	High probability (normal (Gaussian) distribution) of the data being
 *			compressible.
 * BYTE_DIST_MAYBE:	When computed byte distribution resulted in "low > n < high"
 *			grounds.  has_low_entropy() should be used for a final decision.
 */
static int calc_byte_distribution(struct bucket *bkt, size_t slen)
{
	const size_t low = 64, high = 200, threshold = slen * 90 / 100;
	size_t sum = 0;
	int i;

	for (i = 0; i < low; i++)
		sum += bkt[i].count;

	if (sum > threshold)
		return BYTE_DIST_BAD;

	for (; i < high && bkt[i].count > 0; i++) {
		sum += bkt[i].count;
		if (sum > threshold)
			break;
	}

	if (i <= low)
		return BYTE_DIST_GOOD;

	if (i >= high)
		return BYTE_DIST_BAD;

	return BYTE_DIST_MAYBE;
}

static bool is_mostly_ascii(const struct bucket *bkt)
{
	size_t count = 0;
	int i;

	for (i = 0; i < 256; i++)
		if (bkt[i].count > 0)
			/* Too many non-ASCII (0-63) bytes. */
			if (++count > 64)
				return false;

	return true;
}

static bool has_repeated_data(const u8 *sample, size_t len)
{
	size_t s = len / 2;

	return (!memcmp(&sample[0], &sample[s], s));
}

static int cmp_bkt(const void *_a, const void *_b)
{
	const struct bucket *a = _a, *b = _b;

	/* Reverse sort. */
	if (a->count > b->count)
		return -1;

	return 1;
}

/*
 * Collect some 2K samples with 2K gaps between.
 */
static int collect_sample(const struct iov_iter *source, ssize_t max, u8 *sample)
{
	struct iov_iter iter = *source;
	size_t s = 0;

	while (iov_iter_count(&iter) >= SZ_2K) {
		size_t part = umin(umin(iov_iter_count(&iter), SZ_2K), max);
		size_t n;

		n = copy_from_iter(sample + s, part, &iter);
		if (n != part)
			return -EFAULT;

		s += n;
		max -= n;

		if (iov_iter_count(&iter) < PAGE_SIZE - SZ_2K)
			break;

		iov_iter_advance(&iter, SZ_2K);
	}

	return s;
}

/**
 * is_compressible() - Determines if a chunk of data is compressible.
 * @data: Iterator containing uncompressed data.
 *
 * Return: true if @data is compressible, false otherwise.
 *
 * Tests shows that this function is quite reliable in predicting data compressibility,
 * matching close to 1:1 with the behaviour of LZ77 compression success and failures.
 */
static bool is_compressible(const struct iov_iter *data)
{
	const size_t read_size = SZ_2K, bkt_size = 256, max = SZ_4M;
	struct bucket *bkt = NULL;
	size_t len;
	u8 *sample;
	bool ret = false;
	int i;

	/* Preventive double check -- already checked in should_compress(). */
	len = iov_iter_count(data);
	if (unlikely(len < read_size))
		return ret;

	if (len - read_size > max)
		len = max;

	sample = kvzalloc(len, GFP_KERNEL);
	if (!sample) {
		WARN_ON_ONCE(1);

		return ret;
	}

	/* Sample 2K bytes per page of the uncompressed data. */
	i = collect_sample(data, len, sample);
	if (i <= 0) {
		WARN_ON_ONCE(1);

		goto out;
	}

	len = i;
	ret = true;

	if (has_repeated_data(sample, len))
		goto out;

	bkt = kcalloc(bkt_size, sizeof(*bkt), GFP_KERNEL);
	if (!bkt) {
		WARN_ON_ONCE(1);
		ret = false;

		goto out;
	}

	for (i = 0; i < len; i++)
		bkt[sample[i]].count++;

	if (is_mostly_ascii(bkt))
		goto out;

	/* Sort in descending order */
	sort(bkt, bkt_size, sizeof(*bkt), cmp_bkt, NULL);

	i = calc_byte_distribution(bkt, len);
	if (i != BYTE_DIST_MAYBE) {
		ret = !!i;

		goto out;
	}

	ret = has_low_entropy(bkt, len);
out:
	kvfree(sample);
	kfree(bkt);

	return ret;
}

bool should_compress(const struct cifs_tcon *tcon, const struct smb_rqst *rq)
{
	const struct smb2_hdr *shdr = rq->rq_iov->iov_base;

	if (unlikely(!tcon || !tcon->ses || !tcon->ses->server))
		return false;

	if (!tcon->ses->server->compression.enabled)
		return false;

	if (!(tcon->share_flags & SMB2_SHAREFLAG_COMPRESS_DATA))
		return false;

	if (shdr->Command == SMB2_WRITE) {
		const struct smb2_write_req *wreq = rq->rq_iov->iov_base;

		if (le32_to_cpu(wreq->Length) < SMB_COMPRESS_MIN_LEN)
			return false;

		return is_compressible(&rq->rq_iter);
	}

	return (shdr->Command == SMB2_READ);
}

int smb_compress(struct TCP_Server_Info *server, struct smb_rqst *rq, compress_send_fn send_fn)
{
	struct iov_iter iter;
	u32 slen, dlen;
	void *src, *dst = NULL;
	int ret;

	if (!server || !rq || !rq->rq_iov || !rq->rq_iov->iov_base)
		return -EINVAL;

	if (rq->rq_iov->iov_len != sizeof(struct smb2_write_req))
		return -EINVAL;

	slen = iov_iter_count(&rq->rq_iter);
	src = kvzalloc(slen, GFP_KERNEL);
	if (!src) {
		ret = -ENOMEM;
		goto err_free;
	}

	/* Keep the original iter intact. */
	iter = rq->rq_iter;

	if (!copy_from_iter_full(src, slen, &iter)) {
		ret = -EIO;
		goto err_free;
	}

	/*
	 * This is just overprovisioning, as the algorithm will error out if @dst reaches 7/8
	 * of @slen.
	 */
	dlen = slen;
	dst = kvzalloc(dlen, GFP_KERNEL);
	if (!dst) {
		ret = -ENOMEM;
		goto err_free;
	}

	ret = lz77_compress(src, slen, dst, &dlen);
	if (!ret) {
		struct smb2_compression_hdr hdr = { 0 };
		struct smb_rqst comp_rq = { .rq_nvec = 3, };
		struct kvec iov[3];

		hdr.ProtocolId = SMB2_COMPRESSION_TRANSFORM_ID;
		hdr.OriginalCompressedSegmentSize = cpu_to_le32(slen);
		hdr.CompressionAlgorithm = SMB3_COMPRESS_LZ77;
		hdr.Flags = SMB2_COMPRESSION_FLAG_NONE;
		hdr.Offset = cpu_to_le32(rq->rq_iov[0].iov_len);

		iov[0].iov_base = &hdr;
		iov[0].iov_len = sizeof(hdr);
		iov[1] = rq->rq_iov[0];
		iov[2].iov_base = dst;
		iov[2].iov_len = dlen;

		comp_rq.rq_iov = iov;

		ret = send_fn(server, 1, &comp_rq);
	} else if (ret == -EMSGSIZE || dlen >= slen) {
		ret = send_fn(server, 1, rq);
	}
err_free:
	kvfree(dst);
	kvfree(src);

	return ret;
}
