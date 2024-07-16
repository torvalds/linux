// SPDX-License-Identifier: GPL-2.0
/*
 *
 * Copyright (C) 2019-2021 Paragon Software GmbH, All rights reserved.
 *
 */

#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/kernel.h>

#include "debug.h"
#include "ntfs.h"
#include "ntfs_fs.h"

static const struct INDEX_NAMES {
	const __le16 *name;
	u8 name_len;
} s_index_names[INDEX_MUTEX_TOTAL] = {
	{ I30_NAME, ARRAY_SIZE(I30_NAME) }, { SII_NAME, ARRAY_SIZE(SII_NAME) },
	{ SDH_NAME, ARRAY_SIZE(SDH_NAME) }, { SO_NAME, ARRAY_SIZE(SO_NAME) },
	{ SQ_NAME, ARRAY_SIZE(SQ_NAME) },   { SR_NAME, ARRAY_SIZE(SR_NAME) },
};

/*
 * cmp_fnames - Compare two names in index.
 *
 * if l1 != 0
 *   Both names are little endian on-disk ATTR_FILE_NAME structs.
 * else
 *   key1 - cpu_str, key2 - ATTR_FILE_NAME
 */
static int cmp_fnames(const void *key1, size_t l1, const void *key2, size_t l2,
		      const void *data)
{
	const struct ATTR_FILE_NAME *f2 = key2;
	const struct ntfs_sb_info *sbi = data;
	const struct ATTR_FILE_NAME *f1;
	u16 fsize2;
	bool both_case;

	if (l2 <= offsetof(struct ATTR_FILE_NAME, name))
		return -1;

	fsize2 = fname_full_size(f2);
	if (l2 < fsize2)
		return -1;

	both_case = f2->type != FILE_NAME_DOS /*&& !sbi->options.nocase*/;
	if (!l1) {
		const struct le_str *s2 = (struct le_str *)&f2->name_len;

		/*
		 * If names are equal (case insensitive)
		 * try to compare it case sensitive.
		 */
		return ntfs_cmp_names_cpu(key1, s2, sbi->upcase, both_case);
	}

	f1 = key1;
	return ntfs_cmp_names(f1->name, f1->name_len, f2->name, f2->name_len,
			      sbi->upcase, both_case);
}

/*
 * cmp_uint - $SII of $Secure and $Q of Quota
 */
static int cmp_uint(const void *key1, size_t l1, const void *key2, size_t l2,
		    const void *data)
{
	const u32 *k1 = key1;
	const u32 *k2 = key2;

	if (l2 < sizeof(u32))
		return -1;

	if (*k1 < *k2)
		return -1;
	if (*k1 > *k2)
		return 1;
	return 0;
}

/*
 * cmp_sdh - $SDH of $Secure
 */
static int cmp_sdh(const void *key1, size_t l1, const void *key2, size_t l2,
		   const void *data)
{
	const struct SECURITY_KEY *k1 = key1;
	const struct SECURITY_KEY *k2 = key2;
	u32 t1, t2;

	if (l2 < sizeof(struct SECURITY_KEY))
		return -1;

	t1 = le32_to_cpu(k1->hash);
	t2 = le32_to_cpu(k2->hash);

	/* First value is a hash value itself. */
	if (t1 < t2)
		return -1;
	if (t1 > t2)
		return 1;

	/* Second value is security Id. */
	if (data) {
		t1 = le32_to_cpu(k1->sec_id);
		t2 = le32_to_cpu(k2->sec_id);
		if (t1 < t2)
			return -1;
		if (t1 > t2)
			return 1;
	}

	return 0;
}

/*
 * cmp_uints - $O of ObjId and "$R" for Reparse.
 */
static int cmp_uints(const void *key1, size_t l1, const void *key2, size_t l2,
		     const void *data)
{
	const __le32 *k1 = key1;
	const __le32 *k2 = key2;
	size_t count;

	if ((size_t)data == 1) {
		/*
		 * ni_delete_all -> ntfs_remove_reparse ->
		 * delete all with this reference.
		 * k1, k2 - pointers to REPARSE_KEY
		 */

		k1 += 1; // Skip REPARSE_KEY.ReparseTag
		k2 += 1; // Skip REPARSE_KEY.ReparseTag
		if (l2 <= sizeof(int))
			return -1;
		l2 -= sizeof(int);
		if (l1 <= sizeof(int))
			return 1;
		l1 -= sizeof(int);
	}

	if (l2 < sizeof(int))
		return -1;

	for (count = min(l1, l2) >> 2; count > 0; --count, ++k1, ++k2) {
		u32 t1 = le32_to_cpu(*k1);
		u32 t2 = le32_to_cpu(*k2);

		if (t1 > t2)
			return 1;
		if (t1 < t2)
			return -1;
	}

	if (l1 > l2)
		return 1;
	if (l1 < l2)
		return -1;

	return 0;
}

static inline NTFS_CMP_FUNC get_cmp_func(const struct INDEX_ROOT *root)
{
	switch (root->type) {
	case ATTR_NAME:
		if (root->rule == NTFS_COLLATION_TYPE_FILENAME)
			return &cmp_fnames;
		break;
	case ATTR_ZERO:
		switch (root->rule) {
		case NTFS_COLLATION_TYPE_UINT:
			return &cmp_uint;
		case NTFS_COLLATION_TYPE_SECURITY_HASH:
			return &cmp_sdh;
		case NTFS_COLLATION_TYPE_UINTS:
			return &cmp_uints;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return NULL;
}

struct bmp_buf {
	struct ATTRIB *b;
	struct mft_inode *mi;
	struct buffer_head *bh;
	ulong *buf;
	size_t bit;
	u32 nbits;
	u64 new_valid;
};

static int bmp_buf_get(struct ntfs_index *indx, struct ntfs_inode *ni,
		       size_t bit, struct bmp_buf *bbuf)
{
	struct ATTRIB *b;
	size_t data_size, valid_size, vbo, off = bit >> 3;
	struct ntfs_sb_info *sbi = ni->mi.sbi;
	CLST vcn = off >> sbi->cluster_bits;
	struct ATTR_LIST_ENTRY *le = NULL;
	struct buffer_head *bh;
	struct super_block *sb;
	u32 blocksize;
	const struct INDEX_NAMES *in = &s_index_names[indx->type];

	bbuf->bh = NULL;

	b = ni_find_attr(ni, NULL, &le, ATTR_BITMAP, in->name, in->name_len,
			 &vcn, &bbuf->mi);
	bbuf->b = b;
	if (!b)
		return -EINVAL;

	if (!b->non_res) {
		data_size = le32_to_cpu(b->res.data_size);

		if (off >= data_size)
			return -EINVAL;

		bbuf->buf = (ulong *)resident_data(b);
		bbuf->bit = 0;
		bbuf->nbits = data_size * 8;

		return 0;
	}

	data_size = le64_to_cpu(b->nres.data_size);
	if (WARN_ON(off >= data_size)) {
		/* Looks like filesystem error. */
		return -EINVAL;
	}

	valid_size = le64_to_cpu(b->nres.valid_size);

	bh = ntfs_bread_run(sbi, &indx->bitmap_run, off);
	if (!bh)
		return -EIO;

	if (IS_ERR(bh))
		return PTR_ERR(bh);

	bbuf->bh = bh;

	if (buffer_locked(bh))
		__wait_on_buffer(bh);

	lock_buffer(bh);

	sb = sbi->sb;
	blocksize = sb->s_blocksize;

	vbo = off & ~(size_t)sbi->block_mask;

	bbuf->new_valid = vbo + blocksize;
	if (bbuf->new_valid <= valid_size)
		bbuf->new_valid = 0;
	else if (bbuf->new_valid > data_size)
		bbuf->new_valid = data_size;

	if (vbo >= valid_size) {
		memset(bh->b_data, 0, blocksize);
	} else if (vbo + blocksize > valid_size) {
		u32 voff = valid_size & sbi->block_mask;

		memset(bh->b_data + voff, 0, blocksize - voff);
	}

	bbuf->buf = (ulong *)bh->b_data;
	bbuf->bit = 8 * (off & ~(size_t)sbi->block_mask);
	bbuf->nbits = 8 * blocksize;

	return 0;
}

static void bmp_buf_put(struct bmp_buf *bbuf, bool dirty)
{
	struct buffer_head *bh = bbuf->bh;
	struct ATTRIB *b = bbuf->b;

	if (!bh) {
		if (b && !b->non_res && dirty)
			bbuf->mi->dirty = true;
		return;
	}

	if (!dirty)
		goto out;

	if (bbuf->new_valid) {
		b->nres.valid_size = cpu_to_le64(bbuf->new_valid);
		bbuf->mi->dirty = true;
	}

	set_buffer_uptodate(bh);
	mark_buffer_dirty(bh);

out:
	unlock_buffer(bh);
	put_bh(bh);
}

/*
 * indx_mark_used - Mark the bit @bit as used.
 */
static int indx_mark_used(struct ntfs_index *indx, struct ntfs_inode *ni,
			  size_t bit)
{
	int err;
	struct bmp_buf bbuf;

	err = bmp_buf_get(indx, ni, bit, &bbuf);
	if (err)
		return err;

	__set_bit(bit - bbuf.bit, bbuf.buf);

	bmp_buf_put(&bbuf, true);

	return 0;
}

/*
 * indx_mark_free - Mark the bit @bit as free.
 */
static int indx_mark_free(struct ntfs_index *indx, struct ntfs_inode *ni,
			  size_t bit)
{
	int err;
	struct bmp_buf bbuf;

	err = bmp_buf_get(indx, ni, bit, &bbuf);
	if (err)
		return err;

	__clear_bit(bit - bbuf.bit, bbuf.buf);

	bmp_buf_put(&bbuf, true);

	return 0;
}

/*
 * scan_nres_bitmap
 *
 * If ntfs_readdir calls this function (indx_used_bit -> scan_nres_bitmap),
 * inode is shared locked and no ni_lock.
 * Use rw_semaphore for read/write access to bitmap_run.
 */
static int scan_nres_bitmap(struct ntfs_inode *ni, struct ATTRIB *bitmap,
			    struct ntfs_index *indx, size_t from,
			    bool (*fn)(const ulong *buf, u32 bit, u32 bits,
				       size_t *ret),
			    size_t *ret)
{
	struct ntfs_sb_info *sbi = ni->mi.sbi;
	struct super_block *sb = sbi->sb;
	struct runs_tree *run = &indx->bitmap_run;
	struct rw_semaphore *lock = &indx->run_lock;
	u32 nbits = sb->s_blocksize * 8;
	u32 blocksize = sb->s_blocksize;
	u64 valid_size = le64_to_cpu(bitmap->nres.valid_size);
	u64 data_size = le64_to_cpu(bitmap->nres.data_size);
	sector_t eblock = bytes_to_block(sb, data_size);
	size_t vbo = from >> 3;
	sector_t blk = (vbo & sbi->cluster_mask) >> sb->s_blocksize_bits;
	sector_t vblock = vbo >> sb->s_blocksize_bits;
	sector_t blen, block;
	CLST lcn, clen, vcn, vcn_next;
	size_t idx;
	struct buffer_head *bh;
	bool ok;

	*ret = MINUS_ONE_T;

	if (vblock >= eblock)
		return 0;

	from &= nbits - 1;
	vcn = vbo >> sbi->cluster_bits;

	down_read(lock);
	ok = run_lookup_entry(run, vcn, &lcn, &clen, &idx);
	up_read(lock);

next_run:
	if (!ok) {
		int err;
		const struct INDEX_NAMES *name = &s_index_names[indx->type];

		down_write(lock);
		err = attr_load_runs_vcn(ni, ATTR_BITMAP, name->name,
					 name->name_len, run, vcn);
		up_write(lock);
		if (err)
			return err;
		down_read(lock);
		ok = run_lookup_entry(run, vcn, &lcn, &clen, &idx);
		up_read(lock);
		if (!ok)
			return -EINVAL;
	}

	blen = (sector_t)clen * sbi->blocks_per_cluster;
	block = (sector_t)lcn * sbi->blocks_per_cluster;

	for (; blk < blen; blk++, from = 0) {
		bh = ntfs_bread(sb, block + blk);
		if (!bh)
			return -EIO;

		vbo = (u64)vblock << sb->s_blocksize_bits;
		if (vbo >= valid_size) {
			memset(bh->b_data, 0, blocksize);
		} else if (vbo + blocksize > valid_size) {
			u32 voff = valid_size & sbi->block_mask;

			memset(bh->b_data + voff, 0, blocksize - voff);
		}

		if (vbo + blocksize > data_size)
			nbits = 8 * (data_size - vbo);

		ok = nbits > from ? (*fn)((ulong *)bh->b_data, from, nbits, ret)
				  : false;
		put_bh(bh);

		if (ok) {
			*ret += 8 * vbo;
			return 0;
		}

		if (++vblock >= eblock) {
			*ret = MINUS_ONE_T;
			return 0;
		}
	}
	blk = 0;
	vcn_next = vcn + clen;
	down_read(lock);
	ok = run_get_entry(run, ++idx, &vcn, &lcn, &clen) && vcn == vcn_next;
	if (!ok)
		vcn = vcn_next;
	up_read(lock);
	goto next_run;
}

static bool scan_for_free(const ulong *buf, u32 bit, u32 bits, size_t *ret)
{
	size_t pos = find_next_zero_bit(buf, bits, bit);

	if (pos >= bits)
		return false;
	*ret = pos;
	return true;
}

/*
 * indx_find_free - Look for free bit.
 *
 * Return: -1 if no free bits.
 */
static int indx_find_free(struct ntfs_index *indx, struct ntfs_inode *ni,
			  size_t *bit, struct ATTRIB **bitmap)
{
	struct ATTRIB *b;
	struct ATTR_LIST_ENTRY *le = NULL;
	const struct INDEX_NAMES *in = &s_index_names[indx->type];
	int err;

	b = ni_find_attr(ni, NULL, &le, ATTR_BITMAP, in->name, in->name_len,
			 NULL, NULL);

	if (!b)
		return -ENOENT;

	*bitmap = b;
	*bit = MINUS_ONE_T;

	if (!b->non_res) {
		u32 nbits = 8 * le32_to_cpu(b->res.data_size);
		size_t pos = find_next_zero_bit(resident_data(b), nbits, 0);

		if (pos < nbits)
			*bit = pos;
	} else {
		err = scan_nres_bitmap(ni, b, indx, 0, &scan_for_free, bit);

		if (err)
			return err;
	}

	return 0;
}

static bool scan_for_used(const ulong *buf, u32 bit, u32 bits, size_t *ret)
{
	size_t pos = find_next_bit(buf, bits, bit);

	if (pos >= bits)
		return false;
	*ret = pos;
	return true;
}

/*
 * indx_used_bit - Look for used bit.
 *
 * Return: MINUS_ONE_T if no used bits.
 */
int indx_used_bit(struct ntfs_index *indx, struct ntfs_inode *ni, size_t *bit)
{
	struct ATTRIB *b;
	struct ATTR_LIST_ENTRY *le = NULL;
	size_t from = *bit;
	const struct INDEX_NAMES *in = &s_index_names[indx->type];
	int err;

	b = ni_find_attr(ni, NULL, &le, ATTR_BITMAP, in->name, in->name_len,
			 NULL, NULL);

	if (!b)
		return -ENOENT;

	*bit = MINUS_ONE_T;

	if (!b->non_res) {
		u32 nbits = le32_to_cpu(b->res.data_size) * 8;
		size_t pos = find_next_bit(resident_data(b), nbits, from);

		if (pos < nbits)
			*bit = pos;
	} else {
		err = scan_nres_bitmap(ni, b, indx, from, &scan_for_used, bit);
		if (err)
			return err;
	}

	return 0;
}

/*
 * hdr_find_split
 *
 * Find a point at which the index allocation buffer would like to be split.
 * NOTE: This function should never return 'END' entry NULL returns on error.
 */
static const struct NTFS_DE *hdr_find_split(const struct INDEX_HDR *hdr)
{
	size_t o;
	const struct NTFS_DE *e = hdr_first_de(hdr);
	u32 used_2 = le32_to_cpu(hdr->used) >> 1;
	u16 esize;

	if (!e || de_is_last(e))
		return NULL;

	esize = le16_to_cpu(e->size);
	for (o = le32_to_cpu(hdr->de_off) + esize; o < used_2; o += esize) {
		const struct NTFS_DE *p = e;

		e = Add2Ptr(hdr, o);

		/* We must not return END entry. */
		if (de_is_last(e))
			return p;

		esize = le16_to_cpu(e->size);
	}

	return e;
}

/*
 * hdr_insert_head - Insert some entries at the beginning of the buffer.
 *
 * It is used to insert entries into a newly-created buffer.
 */
static const struct NTFS_DE *hdr_insert_head(struct INDEX_HDR *hdr,
					     const void *ins, u32 ins_bytes)
{
	u32 to_move;
	struct NTFS_DE *e = hdr_first_de(hdr);
	u32 used = le32_to_cpu(hdr->used);

	if (!e)
		return NULL;

	/* Now we just make room for the inserted entries and jam it in. */
	to_move = used - le32_to_cpu(hdr->de_off);
	memmove(Add2Ptr(e, ins_bytes), e, to_move);
	memcpy(e, ins, ins_bytes);
	hdr->used = cpu_to_le32(used + ins_bytes);

	return e;
}

/*
 * index_hdr_check
 *
 * return true if INDEX_HDR is valid
 */
static bool index_hdr_check(const struct INDEX_HDR *hdr, u32 bytes)
{
	u32 end = le32_to_cpu(hdr->used);
	u32 tot = le32_to_cpu(hdr->total);
	u32 off = le32_to_cpu(hdr->de_off);

	if (!IS_ALIGNED(off, 8) || tot > bytes || end > tot ||
	    off + sizeof(struct NTFS_DE) > end) {
		/* incorrect index buffer. */
		return false;
	}

	return true;
}

/*
 * index_buf_check
 *
 * return true if INDEX_BUFFER seems is valid
 */
static bool index_buf_check(const struct INDEX_BUFFER *ib, u32 bytes,
			    const CLST *vbn)
{
	const struct NTFS_RECORD_HEADER *rhdr = &ib->rhdr;
	u16 fo = le16_to_cpu(rhdr->fix_off);
	u16 fn = le16_to_cpu(rhdr->fix_num);

	if (bytes <= offsetof(struct INDEX_BUFFER, ihdr) ||
	    rhdr->sign != NTFS_INDX_SIGNATURE ||
	    fo < sizeof(struct INDEX_BUFFER)
	    /* Check index buffer vbn. */
	    || (vbn && *vbn != le64_to_cpu(ib->vbn)) || (fo % sizeof(short)) ||
	    fo + fn * sizeof(short) >= bytes ||
	    fn != ((bytes >> SECTOR_SHIFT) + 1)) {
		/* incorrect index buffer. */
		return false;
	}

	return index_hdr_check(&ib->ihdr,
			       bytes - offsetof(struct INDEX_BUFFER, ihdr));
}

void fnd_clear(struct ntfs_fnd *fnd)
{
	int i;

	for (i = fnd->level - 1; i >= 0; i--) {
		struct indx_node *n = fnd->nodes[i];

		if (!n)
			continue;

		put_indx_node(n);
		fnd->nodes[i] = NULL;
	}
	fnd->level = 0;
	fnd->root_de = NULL;
}

static int fnd_push(struct ntfs_fnd *fnd, struct indx_node *n,
		    struct NTFS_DE *e)
{
	int i;

	i = fnd->level;
	if (i < 0 || i >= ARRAY_SIZE(fnd->nodes))
		return -EINVAL;
	fnd->nodes[i] = n;
	fnd->de[i] = e;
	fnd->level += 1;
	return 0;
}

static struct indx_node *fnd_pop(struct ntfs_fnd *fnd)
{
	struct indx_node *n;
	int i = fnd->level;

	i -= 1;
	n = fnd->nodes[i];
	fnd->nodes[i] = NULL;
	fnd->level = i;

	return n;
}

static bool fnd_is_empty(struct ntfs_fnd *fnd)
{
	if (!fnd->level)
		return !fnd->root_de;

	return !fnd->de[fnd->level - 1];
}

/*
 * hdr_find_e - Locate an entry the index buffer.
 *
 * If no matching entry is found, it returns the first entry which is greater
 * than the desired entry If the search key is greater than all the entries the
 * buffer, it returns the 'end' entry. This function does a binary search of the
 * current index buffer, for the first entry that is <= to the search value.
 *
 * Return: NULL if error.
 */
static struct NTFS_DE *hdr_find_e(const struct ntfs_index *indx,
				  const struct INDEX_HDR *hdr, const void *key,
				  size_t key_len, const void *ctx, int *diff)
{
	struct NTFS_DE *e, *found = NULL;
	NTFS_CMP_FUNC cmp = indx->cmp;
	int min_idx = 0, mid_idx, max_idx = 0;
	int diff2;
	int table_size = 8;
	u32 e_size, e_key_len;
	u32 end = le32_to_cpu(hdr->used);
	u32 off = le32_to_cpu(hdr->de_off);
	u32 total = le32_to_cpu(hdr->total);
	u16 offs[128];

	if (unlikely(!cmp))
		return NULL;

fill_table:
	if (end > total)
		return NULL;

	if (off + sizeof(struct NTFS_DE) > end)
		return NULL;

	e = Add2Ptr(hdr, off);
	e_size = le16_to_cpu(e->size);

	if (e_size < sizeof(struct NTFS_DE) || off + e_size > end)
		return NULL;

	if (!de_is_last(e)) {
		offs[max_idx] = off;
		off += e_size;

		max_idx++;
		if (max_idx < table_size)
			goto fill_table;

		max_idx--;
	}

binary_search:
	e_key_len = le16_to_cpu(e->key_size);

	diff2 = (*cmp)(key, key_len, e + 1, e_key_len, ctx);
	if (diff2 > 0) {
		if (found) {
			min_idx = mid_idx + 1;
		} else {
			if (de_is_last(e))
				return NULL;

			max_idx = 0;
			table_size = min(table_size * 2,
					 (int)ARRAY_SIZE(offs));
			goto fill_table;
		}
	} else if (diff2 < 0) {
		if (found)
			max_idx = mid_idx - 1;
		else
			max_idx--;

		found = e;
	} else {
		*diff = 0;
		return e;
	}

	if (min_idx > max_idx) {
		*diff = -1;
		return found;
	}

	mid_idx = (min_idx + max_idx) >> 1;
	e = Add2Ptr(hdr, offs[mid_idx]);

	goto binary_search;
}

/*
 * hdr_insert_de - Insert an index entry into the buffer.
 *
 * 'before' should be a pointer previously returned from hdr_find_e.
 */
static struct NTFS_DE *hdr_insert_de(const struct ntfs_index *indx,
				     struct INDEX_HDR *hdr,
				     const struct NTFS_DE *de,
				     struct NTFS_DE *before, const void *ctx)
{
	int diff;
	size_t off = PtrOffset(hdr, before);
	u32 used = le32_to_cpu(hdr->used);
	u32 total = le32_to_cpu(hdr->total);
	u16 de_size = le16_to_cpu(de->size);

	/* First, check to see if there's enough room. */
	if (used + de_size > total)
		return NULL;

	/* We know there's enough space, so we know we'll succeed. */
	if (before) {
		/* Check that before is inside Index. */
		if (off >= used || off < le32_to_cpu(hdr->de_off) ||
		    off + le16_to_cpu(before->size) > total) {
			return NULL;
		}
		goto ok;
	}
	/* No insert point is applied. Get it manually. */
	before = hdr_find_e(indx, hdr, de + 1, le16_to_cpu(de->key_size), ctx,
			    &diff);
	if (!before)
		return NULL;
	off = PtrOffset(hdr, before);

ok:
	/* Now we just make room for the entry and jam it in. */
	memmove(Add2Ptr(before, de_size), before, used - off);

	hdr->used = cpu_to_le32(used + de_size);
	memcpy(before, de, de_size);

	return before;
}

/*
 * hdr_delete_de - Remove an entry from the index buffer.
 */
static inline struct NTFS_DE *hdr_delete_de(struct INDEX_HDR *hdr,
					    struct NTFS_DE *re)
{
	u32 used = le32_to_cpu(hdr->used);
	u16 esize = le16_to_cpu(re->size);
	u32 off = PtrOffset(hdr, re);
	int bytes = used - (off + esize);

	/* check INDEX_HDR valid before using INDEX_HDR */
	if (!check_index_header(hdr, le32_to_cpu(hdr->total)))
		return NULL;

	if (off >= used || esize < sizeof(struct NTFS_DE) ||
	    bytes < sizeof(struct NTFS_DE))
		return NULL;

	hdr->used = cpu_to_le32(used - esize);
	memmove(re, Add2Ptr(re, esize), bytes);

	return re;
}

void indx_clear(struct ntfs_index *indx)
{
	run_close(&indx->alloc_run);
	run_close(&indx->bitmap_run);
}

int indx_init(struct ntfs_index *indx, struct ntfs_sb_info *sbi,
	      const struct ATTRIB *attr, enum index_mutex_classed type)
{
	u32 t32;
	const struct INDEX_ROOT *root = resident_data(attr);

	t32 = le32_to_cpu(attr->res.data_size);
	if (t32 <= offsetof(struct INDEX_ROOT, ihdr) ||
	    !index_hdr_check(&root->ihdr,
			     t32 - offsetof(struct INDEX_ROOT, ihdr))) {
		goto out;
	}

	/* Check root fields. */
	if (!root->index_block_clst)
		goto out;

	indx->type = type;
	indx->idx2vbn_bits = __ffs(root->index_block_clst);

	t32 = le32_to_cpu(root->index_block_size);
	indx->index_bits = blksize_bits(t32);

	/* Check index record size. */
	if (t32 < sbi->cluster_size) {
		/* Index record is smaller than a cluster, use 512 blocks. */
		if (t32 != root->index_block_clst * SECTOR_SIZE)
			goto out;

		/* Check alignment to a cluster. */
		if ((sbi->cluster_size >> SECTOR_SHIFT) &
		    (root->index_block_clst - 1)) {
			goto out;
		}

		indx->vbn2vbo_bits = SECTOR_SHIFT;
	} else {
		/* Index record must be a multiple of cluster size. */
		if (t32 != root->index_block_clst << sbi->cluster_bits)
			goto out;

		indx->vbn2vbo_bits = sbi->cluster_bits;
	}

	init_rwsem(&indx->run_lock);

	indx->cmp = get_cmp_func(root);
	if (!indx->cmp)
		goto out;

	return 0;

out:
	ntfs_set_state(sbi, NTFS_DIRTY_DIRTY);
	return -EINVAL;
}

static struct indx_node *indx_new(struct ntfs_index *indx,
				  struct ntfs_inode *ni, CLST vbn,
				  const __le64 *sub_vbn)
{
	int err;
	struct NTFS_DE *e;
	struct indx_node *r;
	struct INDEX_HDR *hdr;
	struct INDEX_BUFFER *index;
	u64 vbo = (u64)vbn << indx->vbn2vbo_bits;
	u32 bytes = 1u << indx->index_bits;
	u16 fn;
	u32 eo;

	r = kzalloc(sizeof(struct indx_node), GFP_NOFS);
	if (!r)
		return ERR_PTR(-ENOMEM);

	index = kzalloc(bytes, GFP_NOFS);
	if (!index) {
		kfree(r);
		return ERR_PTR(-ENOMEM);
	}

	err = ntfs_get_bh(ni->mi.sbi, &indx->alloc_run, vbo, bytes, &r->nb);

	if (err) {
		kfree(index);
		kfree(r);
		return ERR_PTR(err);
	}

	/* Create header. */
	index->rhdr.sign = NTFS_INDX_SIGNATURE;
	index->rhdr.fix_off = cpu_to_le16(sizeof(struct INDEX_BUFFER)); // 0x28
	fn = (bytes >> SECTOR_SHIFT) + 1; // 9
	index->rhdr.fix_num = cpu_to_le16(fn);
	index->vbn = cpu_to_le64(vbn);
	hdr = &index->ihdr;
	eo = ALIGN(sizeof(struct INDEX_BUFFER) + fn * sizeof(short), 8);
	hdr->de_off = cpu_to_le32(eo);

	e = Add2Ptr(hdr, eo);

	if (sub_vbn) {
		e->flags = NTFS_IE_LAST | NTFS_IE_HAS_SUBNODES;
		e->size = cpu_to_le16(sizeof(struct NTFS_DE) + sizeof(u64));
		hdr->used =
			cpu_to_le32(eo + sizeof(struct NTFS_DE) + sizeof(u64));
		de_set_vbn_le(e, *sub_vbn);
		hdr->flags = 1;
	} else {
		e->size = cpu_to_le16(sizeof(struct NTFS_DE));
		hdr->used = cpu_to_le32(eo + sizeof(struct NTFS_DE));
		e->flags = NTFS_IE_LAST;
	}

	hdr->total = cpu_to_le32(bytes - offsetof(struct INDEX_BUFFER, ihdr));

	r->index = index;
	return r;
}

struct INDEX_ROOT *indx_get_root(struct ntfs_index *indx, struct ntfs_inode *ni,
				 struct ATTRIB **attr, struct mft_inode **mi)
{
	struct ATTR_LIST_ENTRY *le = NULL;
	struct ATTRIB *a;
	const struct INDEX_NAMES *in = &s_index_names[indx->type];
	struct INDEX_ROOT *root = NULL;

	a = ni_find_attr(ni, NULL, &le, ATTR_ROOT, in->name, in->name_len, NULL,
			 mi);
	if (!a)
		return NULL;

	if (attr)
		*attr = a;

	root = resident_data_ex(a, sizeof(struct INDEX_ROOT));

	/* length check */
	if (root && offsetof(struct INDEX_ROOT, ihdr) + le32_to_cpu(root->ihdr.used) >
			le32_to_cpu(a->res.data_size)) {
		return NULL;
	}

	return root;
}

static int indx_write(struct ntfs_index *indx, struct ntfs_inode *ni,
		      struct indx_node *node, int sync)
{
	struct INDEX_BUFFER *ib = node->index;

	return ntfs_write_bh(ni->mi.sbi, &ib->rhdr, &node->nb, sync);
}

/*
 * indx_read
 *
 * If ntfs_readdir calls this function
 * inode is shared locked and no ni_lock.
 * Use rw_semaphore for read/write access to alloc_run.
 */
int indx_read(struct ntfs_index *indx, struct ntfs_inode *ni, CLST vbn,
	      struct indx_node **node)
{
	int err;
	struct INDEX_BUFFER *ib;
	struct runs_tree *run = &indx->alloc_run;
	struct rw_semaphore *lock = &indx->run_lock;
	u64 vbo = (u64)vbn << indx->vbn2vbo_bits;
	u32 bytes = 1u << indx->index_bits;
	struct indx_node *in = *node;
	const struct INDEX_NAMES *name;

	if (!in) {
		in = kzalloc(sizeof(struct indx_node), GFP_NOFS);
		if (!in)
			return -ENOMEM;
	} else {
		nb_put(&in->nb);
	}

	ib = in->index;
	if (!ib) {
		ib = kmalloc(bytes, GFP_NOFS);
		if (!ib) {
			err = -ENOMEM;
			goto out;
		}
	}

	down_read(lock);
	err = ntfs_read_bh(ni->mi.sbi, run, vbo, &ib->rhdr, bytes, &in->nb);
	up_read(lock);
	if (!err)
		goto ok;

	if (err == -E_NTFS_FIXUP)
		goto ok;

	if (err != -ENOENT)
		goto out;

	name = &s_index_names[indx->type];
	down_write(lock);
	err = attr_load_runs_range(ni, ATTR_ALLOC, name->name, name->name_len,
				   run, vbo, vbo + bytes);
	up_write(lock);
	if (err)
		goto out;

	down_read(lock);
	err = ntfs_read_bh(ni->mi.sbi, run, vbo, &ib->rhdr, bytes, &in->nb);
	up_read(lock);
	if (err == -E_NTFS_FIXUP)
		goto ok;

	if (err)
		goto out;

ok:
	if (!index_buf_check(ib, bytes, &vbn)) {
		ntfs_inode_err(&ni->vfs_inode, "directory corrupted");
		ntfs_set_state(ni->mi.sbi, NTFS_DIRTY_ERROR);
		err = -EINVAL;
		goto out;
	}

	if (err == -E_NTFS_FIXUP) {
		ntfs_write_bh(ni->mi.sbi, &ib->rhdr, &in->nb, 0);
		err = 0;
	}

	/* check for index header length */
	if (offsetof(struct INDEX_BUFFER, ihdr) + le32_to_cpu(ib->ihdr.used) >
	    bytes) {
		err = -EINVAL;
		goto out;
	}

	in->index = ib;
	*node = in;

out:
	if (err == -E_NTFS_CORRUPT) {
		ntfs_inode_err(&ni->vfs_inode, "directory corrupted");
		ntfs_set_state(ni->mi.sbi, NTFS_DIRTY_ERROR);
		err = -EINVAL;
	}

	if (ib != in->index)
		kfree(ib);

	if (*node != in) {
		nb_put(&in->nb);
		kfree(in);
	}

	return err;
}

/*
 * indx_find - Scan NTFS directory for given entry.
 */
int indx_find(struct ntfs_index *indx, struct ntfs_inode *ni,
	      const struct INDEX_ROOT *root, const void *key, size_t key_len,
	      const void *ctx, int *diff, struct NTFS_DE **entry,
	      struct ntfs_fnd *fnd)
{
	int err;
	struct NTFS_DE *e;
	struct indx_node *node;

	if (!root)
		root = indx_get_root(&ni->dir, ni, NULL, NULL);

	if (!root) {
		/* Should not happen. */
		return -EINVAL;
	}

	/* Check cache. */
	e = fnd->level ? fnd->de[fnd->level - 1] : fnd->root_de;
	if (e && !de_is_last(e) &&
	    !(*indx->cmp)(key, key_len, e + 1, le16_to_cpu(e->key_size), ctx)) {
		*entry = e;
		*diff = 0;
		return 0;
	}

	/* Soft finder reset. */
	fnd_clear(fnd);

	/* Lookup entry that is <= to the search value. */
	e = hdr_find_e(indx, &root->ihdr, key, key_len, ctx, diff);
	if (!e)
		return -EINVAL;

	fnd->root_de = e;

	for (;;) {
		node = NULL;
		if (*diff >= 0 || !de_has_vcn_ex(e))
			break;

		/* Read next level. */
		err = indx_read(indx, ni, de_get_vbn(e), &node);
		if (err)
			return err;

		/* Lookup entry that is <= to the search value. */
		e = hdr_find_e(indx, &node->index->ihdr, key, key_len, ctx,
			       diff);
		if (!e) {
			put_indx_node(node);
			return -EINVAL;
		}

		fnd_push(fnd, node, e);
	}

	*entry = e;
	return 0;
}

int indx_find_sort(struct ntfs_index *indx, struct ntfs_inode *ni,
		   const struct INDEX_ROOT *root, struct NTFS_DE **entry,
		   struct ntfs_fnd *fnd)
{
	int err;
	struct indx_node *n = NULL;
	struct NTFS_DE *e;
	size_t iter = 0;
	int level = fnd->level;

	if (!*entry) {
		/* Start find. */
		e = hdr_first_de(&root->ihdr);
		if (!e)
			return 0;
		fnd_clear(fnd);
		fnd->root_de = e;
	} else if (!level) {
		if (de_is_last(fnd->root_de)) {
			*entry = NULL;
			return 0;
		}

		e = hdr_next_de(&root->ihdr, fnd->root_de);
		if (!e)
			return -EINVAL;
		fnd->root_de = e;
	} else {
		n = fnd->nodes[level - 1];
		e = fnd->de[level - 1];

		if (de_is_last(e))
			goto pop_level;

		e = hdr_next_de(&n->index->ihdr, e);
		if (!e)
			return -EINVAL;

		fnd->de[level - 1] = e;
	}

	/* Just to avoid tree cycle. */
next_iter:
	if (iter++ >= 1000)
		return -EINVAL;

	while (de_has_vcn_ex(e)) {
		if (le16_to_cpu(e->size) <
		    sizeof(struct NTFS_DE) + sizeof(u64)) {
			if (n) {
				fnd_pop(fnd);
				kfree(n);
			}
			return -EINVAL;
		}

		/* Read next level. */
		err = indx_read(indx, ni, de_get_vbn(e), &n);
		if (err)
			return err;

		/* Try next level. */
		e = hdr_first_de(&n->index->ihdr);
		if (!e) {
			kfree(n);
			return -EINVAL;
		}

		fnd_push(fnd, n, e);
	}

	if (le16_to_cpu(e->size) > sizeof(struct NTFS_DE)) {
		*entry = e;
		return 0;
	}

pop_level:
	for (;;) {
		if (!de_is_last(e))
			goto next_iter;

		/* Pop one level. */
		if (n) {
			fnd_pop(fnd);
			kfree(n);
		}

		level = fnd->level;

		if (level) {
			n = fnd->nodes[level - 1];
			e = fnd->de[level - 1];
		} else if (fnd->root_de) {
			n = NULL;
			e = fnd->root_de;
			fnd->root_de = NULL;
		} else {
			*entry = NULL;
			return 0;
		}

		if (le16_to_cpu(e->size) > sizeof(struct NTFS_DE)) {
			*entry = e;
			if (!fnd->root_de)
				fnd->root_de = e;
			return 0;
		}
	}
}

int indx_find_raw(struct ntfs_index *indx, struct ntfs_inode *ni,
		  const struct INDEX_ROOT *root, struct NTFS_DE **entry,
		  size_t *off, struct ntfs_fnd *fnd)
{
	int err;
	struct indx_node *n = NULL;
	struct NTFS_DE *e = NULL;
	struct NTFS_DE *e2;
	size_t bit;
	CLST next_used_vbn;
	CLST next_vbn;
	u32 record_size = ni->mi.sbi->record_size;

	/* Use non sorted algorithm. */
	if (!*entry) {
		/* This is the first call. */
		e = hdr_first_de(&root->ihdr);
		if (!e)
			return 0;
		fnd_clear(fnd);
		fnd->root_de = e;

		/* The first call with setup of initial element. */
		if (*off >= record_size) {
			next_vbn = (((*off - record_size) >> indx->index_bits))
				   << indx->idx2vbn_bits;
			/* Jump inside cycle 'for'. */
			goto next;
		}

		/* Start enumeration from root. */
		*off = 0;
	} else if (!fnd->root_de)
		return -EINVAL;

	for (;;) {
		/* Check if current entry can be used. */
		if (e && le16_to_cpu(e->size) > sizeof(struct NTFS_DE))
			goto ok;

		if (!fnd->level) {
			/* Continue to enumerate root. */
			if (!de_is_last(fnd->root_de)) {
				e = hdr_next_de(&root->ihdr, fnd->root_de);
				if (!e)
					return -EINVAL;
				fnd->root_de = e;
				continue;
			}

			/* Start to enumerate indexes from 0. */
			next_vbn = 0;
		} else {
			/* Continue to enumerate indexes. */
			e2 = fnd->de[fnd->level - 1];

			n = fnd->nodes[fnd->level - 1];

			if (!de_is_last(e2)) {
				e = hdr_next_de(&n->index->ihdr, e2);
				if (!e)
					return -EINVAL;
				fnd->de[fnd->level - 1] = e;
				continue;
			}

			/* Continue with next index. */
			next_vbn = le64_to_cpu(n->index->vbn) +
				   root->index_block_clst;
		}

next:
		/* Release current index. */
		if (n) {
			fnd_pop(fnd);
			put_indx_node(n);
			n = NULL;
		}

		/* Skip all free indexes. */
		bit = next_vbn >> indx->idx2vbn_bits;
		err = indx_used_bit(indx, ni, &bit);
		if (err == -ENOENT || bit == MINUS_ONE_T) {
			/* No used indexes. */
			*entry = NULL;
			return 0;
		}

		next_used_vbn = bit << indx->idx2vbn_bits;

		/* Read buffer into memory. */
		err = indx_read(indx, ni, next_used_vbn, &n);
		if (err)
			return err;

		e = hdr_first_de(&n->index->ihdr);
		fnd_push(fnd, n, e);
		if (!e)
			return -EINVAL;
	}

ok:
	/* Return offset to restore enumerator if necessary. */
	if (!n) {
		/* 'e' points in root, */
		*off = PtrOffset(&root->ihdr, e);
	} else {
		/* 'e' points in index, */
		*off = (le64_to_cpu(n->index->vbn) << indx->vbn2vbo_bits) +
		       record_size + PtrOffset(&n->index->ihdr, e);
	}

	*entry = e;
	return 0;
}

/*
 * indx_create_allocate - Create "Allocation + Bitmap" attributes.
 */
static int indx_create_allocate(struct ntfs_index *indx, struct ntfs_inode *ni,
				CLST *vbn)
{
	int err;
	struct ntfs_sb_info *sbi = ni->mi.sbi;
	struct ATTRIB *bitmap;
	struct ATTRIB *alloc;
	u32 data_size = 1u << indx->index_bits;
	u32 alloc_size = ntfs_up_cluster(sbi, data_size);
	CLST len = alloc_size >> sbi->cluster_bits;
	const struct INDEX_NAMES *in = &s_index_names[indx->type];
	CLST alen;
	struct runs_tree run;

	run_init(&run);

	err = attr_allocate_clusters(sbi, &run, 0, 0, len, NULL, 0, &alen, 0,
				     NULL);
	if (err)
		goto out;

	err = ni_insert_nonresident(ni, ATTR_ALLOC, in->name, in->name_len,
				    &run, 0, len, 0, &alloc, NULL, NULL);
	if (err)
		goto out1;

	alloc->nres.valid_size = alloc->nres.data_size = cpu_to_le64(data_size);

	err = ni_insert_resident(ni, bitmap_size(1), ATTR_BITMAP, in->name,
				 in->name_len, &bitmap, NULL, NULL);
	if (err)
		goto out2;

	if (in->name == I30_NAME) {
		ni->vfs_inode.i_size = data_size;
		inode_set_bytes(&ni->vfs_inode, alloc_size);
	}

	memcpy(&indx->alloc_run, &run, sizeof(run));

	*vbn = 0;

	return 0;

out2:
	mi_remove_attr(NULL, &ni->mi, alloc);

out1:
	run_deallocate(sbi, &run, false);

out:
	return err;
}

/*
 * indx_add_allocate - Add clusters to index.
 */
static int indx_add_allocate(struct ntfs_index *indx, struct ntfs_inode *ni,
			     CLST *vbn)
{
	int err;
	size_t bit;
	u64 data_size;
	u64 bmp_size, bmp_size_v;
	struct ATTRIB *bmp, *alloc;
	struct mft_inode *mi;
	const struct INDEX_NAMES *in = &s_index_names[indx->type];

	err = indx_find_free(indx, ni, &bit, &bmp);
	if (err)
		goto out1;

	if (bit != MINUS_ONE_T) {
		bmp = NULL;
	} else {
		if (bmp->non_res) {
			bmp_size = le64_to_cpu(bmp->nres.data_size);
			bmp_size_v = le64_to_cpu(bmp->nres.valid_size);
		} else {
			bmp_size = bmp_size_v = le32_to_cpu(bmp->res.data_size);
		}

		bit = bmp_size << 3;
	}

	data_size = (u64)(bit + 1) << indx->index_bits;

	if (bmp) {
		/* Increase bitmap. */
		err = attr_set_size(ni, ATTR_BITMAP, in->name, in->name_len,
				    &indx->bitmap_run, bitmap_size(bit + 1),
				    NULL, true, NULL);
		if (err)
			goto out1;
	}

	alloc = ni_find_attr(ni, NULL, NULL, ATTR_ALLOC, in->name, in->name_len,
			     NULL, &mi);
	if (!alloc) {
		err = -EINVAL;
		if (bmp)
			goto out2;
		goto out1;
	}

	/* Increase allocation. */
	err = attr_set_size(ni, ATTR_ALLOC, in->name, in->name_len,
			    &indx->alloc_run, data_size, &data_size, true,
			    NULL);
	if (err) {
		if (bmp)
			goto out2;
		goto out1;
	}

	*vbn = bit << indx->idx2vbn_bits;

	return 0;

out2:
	/* Ops. No space? */
	attr_set_size(ni, ATTR_BITMAP, in->name, in->name_len,
		      &indx->bitmap_run, bmp_size, &bmp_size_v, false, NULL);

out1:
	return err;
}

/*
 * indx_insert_into_root - Attempt to insert an entry into the index root.
 *
 * @undo - True if we undoing previous remove.
 * If necessary, it will twiddle the index b-tree.
 */
static int indx_insert_into_root(struct ntfs_index *indx, struct ntfs_inode *ni,
				 const struct NTFS_DE *new_de,
				 struct NTFS_DE *root_de, const void *ctx,
				 struct ntfs_fnd *fnd, bool undo)
{
	int err = 0;
	struct NTFS_DE *e, *e0, *re;
	struct mft_inode *mi;
	struct ATTRIB *attr;
	struct INDEX_HDR *hdr;
	struct indx_node *n;
	CLST new_vbn;
	__le64 *sub_vbn, t_vbn;
	u16 new_de_size;
	u32 hdr_used, hdr_total, asize, to_move;
	u32 root_size, new_root_size;
	struct ntfs_sb_info *sbi;
	int ds_root;
	struct INDEX_ROOT *root, *a_root;

	/* Get the record this root placed in. */
	root = indx_get_root(indx, ni, &attr, &mi);
	if (!root)
		return -EINVAL;

	/*
	 * Try easy case:
	 * hdr_insert_de will succeed if there's
	 * room the root for the new entry.
	 */
	hdr = &root->ihdr;
	sbi = ni->mi.sbi;
	new_de_size = le16_to_cpu(new_de->size);
	hdr_used = le32_to_cpu(hdr->used);
	hdr_total = le32_to_cpu(hdr->total);
	asize = le32_to_cpu(attr->size);
	root_size = le32_to_cpu(attr->res.data_size);

	ds_root = new_de_size + hdr_used - hdr_total;

	/* If 'undo' is set then reduce requirements. */
	if ((undo || asize + ds_root < sbi->max_bytes_per_attr) &&
	    mi_resize_attr(mi, attr, ds_root)) {
		hdr->total = cpu_to_le32(hdr_total + ds_root);
		e = hdr_insert_de(indx, hdr, new_de, root_de, ctx);
		WARN_ON(!e);
		fnd_clear(fnd);
		fnd->root_de = e;

		return 0;
	}

	/* Make a copy of root attribute to restore if error. */
	a_root = kmemdup(attr, asize, GFP_NOFS);
	if (!a_root)
		return -ENOMEM;

	/*
	 * Copy all the non-end entries from
	 * the index root to the new buffer.
	 */
	to_move = 0;
	e0 = hdr_first_de(hdr);

	/* Calculate the size to copy. */
	for (e = e0;; e = hdr_next_de(hdr, e)) {
		if (!e) {
			err = -EINVAL;
			goto out_free_root;
		}

		if (de_is_last(e))
			break;
		to_move += le16_to_cpu(e->size);
	}

	if (!to_move) {
		re = NULL;
	} else {
		re = kmemdup(e0, to_move, GFP_NOFS);
		if (!re) {
			err = -ENOMEM;
			goto out_free_root;
		}
	}

	sub_vbn = NULL;
	if (de_has_vcn(e)) {
		t_vbn = de_get_vbn_le(e);
		sub_vbn = &t_vbn;
	}

	new_root_size = sizeof(struct INDEX_ROOT) + sizeof(struct NTFS_DE) +
			sizeof(u64);
	ds_root = new_root_size - root_size;

	if (ds_root > 0 && asize + ds_root > sbi->max_bytes_per_attr) {
		/* Make root external. */
		err = -EOPNOTSUPP;
		goto out_free_re;
	}

	if (ds_root)
		mi_resize_attr(mi, attr, ds_root);

	/* Fill first entry (vcn will be set later). */
	e = (struct NTFS_DE *)(root + 1);
	memset(e, 0, sizeof(struct NTFS_DE));
	e->size = cpu_to_le16(sizeof(struct NTFS_DE) + sizeof(u64));
	e->flags = NTFS_IE_HAS_SUBNODES | NTFS_IE_LAST;

	hdr->flags = 1;
	hdr->used = hdr->total =
		cpu_to_le32(new_root_size - offsetof(struct INDEX_ROOT, ihdr));

	fnd->root_de = hdr_first_de(hdr);
	mi->dirty = true;

	/* Create alloc and bitmap attributes (if not). */
	err = run_is_empty(&indx->alloc_run)
		      ? indx_create_allocate(indx, ni, &new_vbn)
		      : indx_add_allocate(indx, ni, &new_vbn);

	/* Layout of record may be changed, so rescan root. */
	root = indx_get_root(indx, ni, &attr, &mi);
	if (!root) {
		/* Bug? */
		ntfs_set_state(sbi, NTFS_DIRTY_ERROR);
		err = -EINVAL;
		goto out_free_re;
	}

	if (err) {
		/* Restore root. */
		if (mi_resize_attr(mi, attr, -ds_root)) {
			memcpy(attr, a_root, asize);
		} else {
			/* Bug? */
			ntfs_set_state(sbi, NTFS_DIRTY_ERROR);
		}
		goto out_free_re;
	}

	e = (struct NTFS_DE *)(root + 1);
	*(__le64 *)(e + 1) = cpu_to_le64(new_vbn);
	mi->dirty = true;

	/* Now we can create/format the new buffer and copy the entries into. */
	n = indx_new(indx, ni, new_vbn, sub_vbn);
	if (IS_ERR(n)) {
		err = PTR_ERR(n);
		goto out_free_re;
	}

	hdr = &n->index->ihdr;
	hdr_used = le32_to_cpu(hdr->used);
	hdr_total = le32_to_cpu(hdr->total);

	/* Copy root entries into new buffer. */
	hdr_insert_head(hdr, re, to_move);

	/* Update bitmap attribute. */
	indx_mark_used(indx, ni, new_vbn >> indx->idx2vbn_bits);

	/* Check if we can insert new entry new index buffer. */
	if (hdr_used + new_de_size > hdr_total) {
		/*
		 * This occurs if MFT record is the same or bigger than index
		 * buffer. Move all root new index and have no space to add
		 * new entry classic case when MFT record is 1K and index
		 * buffer 4K the problem should not occurs.
		 */
		kfree(re);
		indx_write(indx, ni, n, 0);

		put_indx_node(n);
		fnd_clear(fnd);
		err = indx_insert_entry(indx, ni, new_de, ctx, fnd, undo);
		goto out_free_root;
	}

	/*
	 * Now root is a parent for new index buffer.
	 * Insert NewEntry a new buffer.
	 */
	e = hdr_insert_de(indx, hdr, new_de, NULL, ctx);
	if (!e) {
		err = -EINVAL;
		goto out_put_n;
	}
	fnd_push(fnd, n, e);

	/* Just write updates index into disk. */
	indx_write(indx, ni, n, 0);

	n = NULL;

out_put_n:
	put_indx_node(n);
out_free_re:
	kfree(re);
out_free_root:
	kfree(a_root);
	return err;
}

/*
 * indx_insert_into_buffer
 *
 * Attempt to insert an entry into an Index Allocation Buffer.
 * If necessary, it will split the buffer.
 */
static int
indx_insert_into_buffer(struct ntfs_index *indx, struct ntfs_inode *ni,
			struct INDEX_ROOT *root, const struct NTFS_DE *new_de,
			const void *ctx, int level, struct ntfs_fnd *fnd)
{
	int err;
	const struct NTFS_DE *sp;
	struct NTFS_DE *e, *de_t, *up_e;
	struct indx_node *n2;
	struct indx_node *n1 = fnd->nodes[level];
	struct INDEX_HDR *hdr1 = &n1->index->ihdr;
	struct INDEX_HDR *hdr2;
	u32 to_copy, used;
	CLST new_vbn;
	__le64 t_vbn, *sub_vbn;
	u16 sp_size;

	/* Try the most easy case. */
	e = fnd->level - 1 == level ? fnd->de[level] : NULL;
	e = hdr_insert_de(indx, hdr1, new_de, e, ctx);
	fnd->de[level] = e;
	if (e) {
		/* Just write updated index into disk. */
		indx_write(indx, ni, n1, 0);
		return 0;
	}

	/*
	 * No space to insert into buffer. Split it.
	 * To split we:
	 *  - Save split point ('cause index buffers will be changed)
	 * - Allocate NewBuffer and copy all entries <= sp into new buffer
	 * - Remove all entries (sp including) from TargetBuffer
	 * - Insert NewEntry into left or right buffer (depending on sp <=>
	 *     NewEntry)
	 * - Insert sp into parent buffer (or root)
	 * - Make sp a parent for new buffer
	 */
	sp = hdr_find_split(hdr1);
	if (!sp)
		return -EINVAL;

	sp_size = le16_to_cpu(sp->size);
	up_e = kmalloc(sp_size + sizeof(u64), GFP_NOFS);
	if (!up_e)
		return -ENOMEM;
	memcpy(up_e, sp, sp_size);

	if (!hdr1->flags) {
		up_e->flags |= NTFS_IE_HAS_SUBNODES;
		up_e->size = cpu_to_le16(sp_size + sizeof(u64));
		sub_vbn = NULL;
	} else {
		t_vbn = de_get_vbn_le(up_e);
		sub_vbn = &t_vbn;
	}

	/* Allocate on disk a new index allocation buffer. */
	err = indx_add_allocate(indx, ni, &new_vbn);
	if (err)
		goto out;

	/* Allocate and format memory a new index buffer. */
	n2 = indx_new(indx, ni, new_vbn, sub_vbn);
	if (IS_ERR(n2)) {
		err = PTR_ERR(n2);
		goto out;
	}

	hdr2 = &n2->index->ihdr;

	/* Make sp a parent for new buffer. */
	de_set_vbn(up_e, new_vbn);

	/* Copy all the entries <= sp into the new buffer. */
	de_t = hdr_first_de(hdr1);
	to_copy = PtrOffset(de_t, sp);
	hdr_insert_head(hdr2, de_t, to_copy);

	/* Remove all entries (sp including) from hdr1. */
	used = le32_to_cpu(hdr1->used) - to_copy - sp_size;
	memmove(de_t, Add2Ptr(sp, sp_size), used - le32_to_cpu(hdr1->de_off));
	hdr1->used = cpu_to_le32(used);

	/*
	 * Insert new entry into left or right buffer
	 * (depending on sp <=> new_de).
	 */
	hdr_insert_de(indx,
		      (*indx->cmp)(new_de + 1, le16_to_cpu(new_de->key_size),
				   up_e + 1, le16_to_cpu(up_e->key_size),
				   ctx) < 0
			      ? hdr2
			      : hdr1,
		      new_de, NULL, ctx);

	indx_mark_used(indx, ni, new_vbn >> indx->idx2vbn_bits);

	indx_write(indx, ni, n1, 0);
	indx_write(indx, ni, n2, 0);

	put_indx_node(n2);

	/*
	 * We've finished splitting everybody, so we are ready to
	 * insert the promoted entry into the parent.
	 */
	if (!level) {
		/* Insert in root. */
		err = indx_insert_into_root(indx, ni, up_e, NULL, ctx, fnd, 0);
		if (err)
			goto out;
	} else {
		/*
		 * The target buffer's parent is another index buffer.
		 * TODO: Remove recursion.
		 */
		err = indx_insert_into_buffer(indx, ni, root, up_e, ctx,
					      level - 1, fnd);
		if (err)
			goto out;
	}

out:
	kfree(up_e);

	return err;
}

/*
 * indx_insert_entry - Insert new entry into index.
 *
 * @undo - True if we undoing previous remove.
 */
int indx_insert_entry(struct ntfs_index *indx, struct ntfs_inode *ni,
		      const struct NTFS_DE *new_de, const void *ctx,
		      struct ntfs_fnd *fnd, bool undo)
{
	int err;
	int diff;
	struct NTFS_DE *e;
	struct ntfs_fnd *fnd_a = NULL;
	struct INDEX_ROOT *root;

	if (!fnd) {
		fnd_a = fnd_get();
		if (!fnd_a) {
			err = -ENOMEM;
			goto out1;
		}
		fnd = fnd_a;
	}

	root = indx_get_root(indx, ni, NULL, NULL);
	if (!root) {
		err = -EINVAL;
		goto out;
	}

	if (fnd_is_empty(fnd)) {
		/*
		 * Find the spot the tree where we want to
		 * insert the new entry.
		 */
		err = indx_find(indx, ni, root, new_de + 1,
				le16_to_cpu(new_de->key_size), ctx, &diff, &e,
				fnd);
		if (err)
			goto out;

		if (!diff) {
			err = -EEXIST;
			goto out;
		}
	}

	if (!fnd->level) {
		/*
		 * The root is also a leaf, so we'll insert the
		 * new entry into it.
		 */
		err = indx_insert_into_root(indx, ni, new_de, fnd->root_de, ctx,
					    fnd, undo);
		if (err)
			goto out;
	} else {
		/*
		 * Found a leaf buffer, so we'll insert the new entry into it.
		 */
		err = indx_insert_into_buffer(indx, ni, root, new_de, ctx,
					      fnd->level - 1, fnd);
		if (err)
			goto out;
	}

out:
	fnd_put(fnd_a);
out1:
	return err;
}

/*
 * indx_find_buffer - Locate a buffer from the tree.
 */
static struct indx_node *indx_find_buffer(struct ntfs_index *indx,
					  struct ntfs_inode *ni,
					  const struct INDEX_ROOT *root,
					  __le64 vbn, struct indx_node *n)
{
	int err;
	const struct NTFS_DE *e;
	struct indx_node *r;
	const struct INDEX_HDR *hdr = n ? &n->index->ihdr : &root->ihdr;

	/* Step 1: Scan one level. */
	for (e = hdr_first_de(hdr);; e = hdr_next_de(hdr, e)) {
		if (!e)
			return ERR_PTR(-EINVAL);

		if (de_has_vcn(e) && vbn == de_get_vbn_le(e))
			return n;

		if (de_is_last(e))
			break;
	}

	/* Step2: Do recursion. */
	e = Add2Ptr(hdr, le32_to_cpu(hdr->de_off));
	for (;;) {
		if (de_has_vcn_ex(e)) {
			err = indx_read(indx, ni, de_get_vbn(e), &n);
			if (err)
				return ERR_PTR(err);

			r = indx_find_buffer(indx, ni, root, vbn, n);
			if (r)
				return r;
		}

		if (de_is_last(e))
			break;

		e = Add2Ptr(e, le16_to_cpu(e->size));
	}

	return NULL;
}

/*
 * indx_shrink - Deallocate unused tail indexes.
 */
static int indx_shrink(struct ntfs_index *indx, struct ntfs_inode *ni,
		       size_t bit)
{
	int err = 0;
	u64 bpb, new_data;
	size_t nbits;
	struct ATTRIB *b;
	struct ATTR_LIST_ENTRY *le = NULL;
	const struct INDEX_NAMES *in = &s_index_names[indx->type];

	b = ni_find_attr(ni, NULL, &le, ATTR_BITMAP, in->name, in->name_len,
			 NULL, NULL);

	if (!b)
		return -ENOENT;

	if (!b->non_res) {
		unsigned long pos;
		const unsigned long *bm = resident_data(b);

		nbits = (size_t)le32_to_cpu(b->res.data_size) * 8;

		if (bit >= nbits)
			return 0;

		pos = find_next_bit(bm, nbits, bit);
		if (pos < nbits)
			return 0;
	} else {
		size_t used = MINUS_ONE_T;

		nbits = le64_to_cpu(b->nres.data_size) * 8;

		if (bit >= nbits)
			return 0;

		err = scan_nres_bitmap(ni, b, indx, bit, &scan_for_used, &used);
		if (err)
			return err;

		if (used != MINUS_ONE_T)
			return 0;
	}

	new_data = (u64)bit << indx->index_bits;

	err = attr_set_size(ni, ATTR_ALLOC, in->name, in->name_len,
			    &indx->alloc_run, new_data, &new_data, false, NULL);
	if (err)
		return err;

	bpb = bitmap_size(bit);
	if (bpb * 8 == nbits)
		return 0;

	err = attr_set_size(ni, ATTR_BITMAP, in->name, in->name_len,
			    &indx->bitmap_run, bpb, &bpb, false, NULL);

	return err;
}

static int indx_free_children(struct ntfs_index *indx, struct ntfs_inode *ni,
			      const struct NTFS_DE *e, bool trim)
{
	int err;
	struct indx_node *n = NULL;
	struct INDEX_HDR *hdr;
	CLST vbn = de_get_vbn(e);
	size_t i;

	err = indx_read(indx, ni, vbn, &n);
	if (err)
		return err;

	hdr = &n->index->ihdr;
	/* First, recurse into the children, if any. */
	if (hdr_has_subnode(hdr)) {
		for (e = hdr_first_de(hdr); e; e = hdr_next_de(hdr, e)) {
			indx_free_children(indx, ni, e, false);
			if (de_is_last(e))
				break;
		}
	}

	put_indx_node(n);

	i = vbn >> indx->idx2vbn_bits;
	/*
	 * We've gotten rid of the children; add this buffer to the free list.
	 */
	indx_mark_free(indx, ni, i);

	if (!trim)
		return 0;

	/*
	 * If there are no used indexes after current free index
	 * then we can truncate allocation and bitmap.
	 * Use bitmap to estimate the case.
	 */
	indx_shrink(indx, ni, i + 1);
	return 0;
}

/*
 * indx_get_entry_to_replace
 *
 * Find a replacement entry for a deleted entry.
 * Always returns a node entry:
 * NTFS_IE_HAS_SUBNODES is set the flags and the size includes the sub_vcn.
 */
static int indx_get_entry_to_replace(struct ntfs_index *indx,
				     struct ntfs_inode *ni,
				     const struct NTFS_DE *de_next,
				     struct NTFS_DE **de_to_replace,
				     struct ntfs_fnd *fnd)
{
	int err;
	int level = -1;
	CLST vbn;
	struct NTFS_DE *e, *te, *re;
	struct indx_node *n;
	struct INDEX_BUFFER *ib;

	*de_to_replace = NULL;

	/* Find first leaf entry down from de_next. */
	vbn = de_get_vbn(de_next);
	for (;;) {
		n = NULL;
		err = indx_read(indx, ni, vbn, &n);
		if (err)
			goto out;

		e = hdr_first_de(&n->index->ihdr);
		fnd_push(fnd, n, e);

		if (!de_is_last(e)) {
			/*
			 * This buffer is non-empty, so its first entry
			 * could be used as the replacement entry.
			 */
			level = fnd->level - 1;
		}

		if (!de_has_vcn(e))
			break;

		/* This buffer is a node. Continue to go down. */
		vbn = de_get_vbn(e);
	}

	if (level == -1)
		goto out;

	n = fnd->nodes[level];
	te = hdr_first_de(&n->index->ihdr);
	/* Copy the candidate entry into the replacement entry buffer. */
	re = kmalloc(le16_to_cpu(te->size) + sizeof(u64), GFP_NOFS);
	if (!re) {
		err = -ENOMEM;
		goto out;
	}

	*de_to_replace = re;
	memcpy(re, te, le16_to_cpu(te->size));

	if (!de_has_vcn(re)) {
		/*
		 * The replacement entry we found doesn't have a sub_vcn.
		 * increase its size to hold one.
		 */
		le16_add_cpu(&re->size, sizeof(u64));
		re->flags |= NTFS_IE_HAS_SUBNODES;
	} else {
		/*
		 * The replacement entry we found was a node entry, which
		 * means that all its child buffers are empty. Return them
		 * to the free pool.
		 */
		indx_free_children(indx, ni, te, true);
	}

	/*
	 * Expunge the replacement entry from its former location,
	 * and then write that buffer.
	 */
	ib = n->index;
	e = hdr_delete_de(&ib->ihdr, te);

	fnd->de[level] = e;
	indx_write(indx, ni, n, 0);

	/* Check to see if this action created an empty leaf. */
	if (ib_is_leaf(ib) && ib_is_empty(ib))
		return 0;

out:
	fnd_clear(fnd);
	return err;
}

/*
 * indx_delete_entry - Delete an entry from the index.
 */
int indx_delete_entry(struct ntfs_index *indx, struct ntfs_inode *ni,
		      const void *key, u32 key_len, const void *ctx)
{
	int err, diff;
	struct INDEX_ROOT *root;
	struct INDEX_HDR *hdr;
	struct ntfs_fnd *fnd, *fnd2;
	struct INDEX_BUFFER *ib;
	struct NTFS_DE *e, *re, *next, *prev, *me;
	struct indx_node *n, *n2d = NULL;
	__le64 sub_vbn;
	int level, level2;
	struct ATTRIB *attr;
	struct mft_inode *mi;
	u32 e_size, root_size, new_root_size;
	size_t trim_bit;
	const struct INDEX_NAMES *in;

	fnd = fnd_get();
	if (!fnd) {
		err = -ENOMEM;
		goto out2;
	}

	fnd2 = fnd_get();
	if (!fnd2) {
		err = -ENOMEM;
		goto out1;
	}

	root = indx_get_root(indx, ni, &attr, &mi);
	if (!root) {
		err = -EINVAL;
		goto out;
	}

	/* Locate the entry to remove. */
	err = indx_find(indx, ni, root, key, key_len, ctx, &diff, &e, fnd);
	if (err)
		goto out;

	if (!e || diff) {
		err = -ENOENT;
		goto out;
	}

	level = fnd->level;

	if (level) {
		n = fnd->nodes[level - 1];
		e = fnd->de[level - 1];
		ib = n->index;
		hdr = &ib->ihdr;
	} else {
		hdr = &root->ihdr;
		e = fnd->root_de;
		n = NULL;
	}

	e_size = le16_to_cpu(e->size);

	if (!de_has_vcn_ex(e)) {
		/* The entry to delete is a leaf, so we can just rip it out. */
		hdr_delete_de(hdr, e);

		if (!level) {
			hdr->total = hdr->used;

			/* Shrink resident root attribute. */
			mi_resize_attr(mi, attr, 0 - e_size);
			goto out;
		}

		indx_write(indx, ni, n, 0);

		/*
		 * Check to see if removing that entry made
		 * the leaf empty.
		 */
		if (ib_is_leaf(ib) && ib_is_empty(ib)) {
			fnd_pop(fnd);
			fnd_push(fnd2, n, e);
		}
	} else {
		/*
		 * The entry we wish to delete is a node buffer, so we
		 * have to find a replacement for it.
		 */
		next = de_get_next(e);

		err = indx_get_entry_to_replace(indx, ni, next, &re, fnd2);
		if (err)
			goto out;

		if (re) {
			de_set_vbn_le(re, de_get_vbn_le(e));
			hdr_delete_de(hdr, e);

			err = level ? indx_insert_into_buffer(indx, ni, root,
							      re, ctx,
							      fnd->level - 1,
							      fnd)
				    : indx_insert_into_root(indx, ni, re, e,
							    ctx, fnd, 0);
			kfree(re);

			if (err)
				goto out;
		} else {
			/*
			 * There is no replacement for the current entry.
			 * This means that the subtree rooted at its node
			 * is empty, and can be deleted, which turn means
			 * that the node can just inherit the deleted
			 * entry sub_vcn.
			 */
			indx_free_children(indx, ni, next, true);

			de_set_vbn_le(next, de_get_vbn_le(e));
			hdr_delete_de(hdr, e);
			if (level) {
				indx_write(indx, ni, n, 0);
			} else {
				hdr->total = hdr->used;

				/* Shrink resident root attribute. */
				mi_resize_attr(mi, attr, 0 - e_size);
			}
		}
	}

	/* Delete a branch of tree. */
	if (!fnd2 || !fnd2->level)
		goto out;

	/* Reinit root 'cause it can be changed. */
	root = indx_get_root(indx, ni, &attr, &mi);
	if (!root) {
		err = -EINVAL;
		goto out;
	}

	n2d = NULL;
	sub_vbn = fnd2->nodes[0]->index->vbn;
	level2 = 0;
	level = fnd->level;

	hdr = level ? &fnd->nodes[level - 1]->index->ihdr : &root->ihdr;

	/* Scan current level. */
	for (e = hdr_first_de(hdr);; e = hdr_next_de(hdr, e)) {
		if (!e) {
			err = -EINVAL;
			goto out;
		}

		if (de_has_vcn(e) && sub_vbn == de_get_vbn_le(e))
			break;

		if (de_is_last(e)) {
			e = NULL;
			break;
		}
	}

	if (!e) {
		/* Do slow search from root. */
		struct indx_node *in;

		fnd_clear(fnd);

		in = indx_find_buffer(indx, ni, root, sub_vbn, NULL);
		if (IS_ERR(in)) {
			err = PTR_ERR(in);
			goto out;
		}

		if (in)
			fnd_push(fnd, in, NULL);
	}

	/* Merge fnd2 -> fnd. */
	for (level = 0; level < fnd2->level; level++) {
		fnd_push(fnd, fnd2->nodes[level], fnd2->de[level]);
		fnd2->nodes[level] = NULL;
	}
	fnd2->level = 0;

	hdr = NULL;
	for (level = fnd->level; level; level--) {
		struct indx_node *in = fnd->nodes[level - 1];

		ib = in->index;
		if (ib_is_empty(ib)) {
			sub_vbn = ib->vbn;
		} else {
			hdr = &ib->ihdr;
			n2d = in;
			level2 = level;
			break;
		}
	}

	if (!hdr)
		hdr = &root->ihdr;

	e = hdr_first_de(hdr);
	if (!e) {
		err = -EINVAL;
		goto out;
	}

	if (hdr != &root->ihdr || !de_is_last(e)) {
		prev = NULL;
		while (!de_is_last(e)) {
			if (de_has_vcn(e) && sub_vbn == de_get_vbn_le(e))
				break;
			prev = e;
			e = hdr_next_de(hdr, e);
			if (!e) {
				err = -EINVAL;
				goto out;
			}
		}

		if (sub_vbn != de_get_vbn_le(e)) {
			/*
			 * Didn't find the parent entry, although this buffer
			 * is the parent trail. Something is corrupt.
			 */
			err = -EINVAL;
			goto out;
		}

		if (de_is_last(e)) {
			/*
			 * Since we can't remove the end entry, we'll remove
			 * its predecessor instead. This means we have to
			 * transfer the predecessor's sub_vcn to the end entry.
			 * Note: This index block is not empty, so the
			 * predecessor must exist.
			 */
			if (!prev) {
				err = -EINVAL;
				goto out;
			}

			if (de_has_vcn(prev)) {
				de_set_vbn_le(e, de_get_vbn_le(prev));
			} else if (de_has_vcn(e)) {
				le16_sub_cpu(&e->size, sizeof(u64));
				e->flags &= ~NTFS_IE_HAS_SUBNODES;
				le32_sub_cpu(&hdr->used, sizeof(u64));
			}
			e = prev;
		}

		/*
		 * Copy the current entry into a temporary buffer (stripping
		 * off its down-pointer, if any) and delete it from the current
		 * buffer or root, as appropriate.
		 */
		e_size = le16_to_cpu(e->size);
		me = kmemdup(e, e_size, GFP_NOFS);
		if (!me) {
			err = -ENOMEM;
			goto out;
		}

		if (de_has_vcn(me)) {
			me->flags &= ~NTFS_IE_HAS_SUBNODES;
			le16_sub_cpu(&me->size, sizeof(u64));
		}

		hdr_delete_de(hdr, e);

		if (hdr == &root->ihdr) {
			level = 0;
			hdr->total = hdr->used;

			/* Shrink resident root attribute. */
			mi_resize_attr(mi, attr, 0 - e_size);
		} else {
			indx_write(indx, ni, n2d, 0);
			level = level2;
		}

		/* Mark unused buffers as free. */
		trim_bit = -1;
		for (; level < fnd->level; level++) {
			ib = fnd->nodes[level]->index;
			if (ib_is_empty(ib)) {
				size_t k = le64_to_cpu(ib->vbn) >>
					   indx->idx2vbn_bits;

				indx_mark_free(indx, ni, k);
				if (k < trim_bit)
					trim_bit = k;
			}
		}

		fnd_clear(fnd);
		/*fnd->root_de = NULL;*/

		/*
		 * Re-insert the entry into the tree.
		 * Find the spot the tree where we want to insert the new entry.
		 */
		err = indx_insert_entry(indx, ni, me, ctx, fnd, 0);
		kfree(me);
		if (err)
			goto out;

		if (trim_bit != -1)
			indx_shrink(indx, ni, trim_bit);
	} else {
		/*
		 * This tree needs to be collapsed down to an empty root.
		 * Recreate the index root as an empty leaf and free all
		 * the bits the index allocation bitmap.
		 */
		fnd_clear(fnd);
		fnd_clear(fnd2);

		in = &s_index_names[indx->type];

		err = attr_set_size(ni, ATTR_ALLOC, in->name, in->name_len,
				    &indx->alloc_run, 0, NULL, false, NULL);
		err = ni_remove_attr(ni, ATTR_ALLOC, in->name, in->name_len,
				     false, NULL);
		run_close(&indx->alloc_run);

		err = attr_set_size(ni, ATTR_BITMAP, in->name, in->name_len,
				    &indx->bitmap_run, 0, NULL, false, NULL);
		err = ni_remove_attr(ni, ATTR_BITMAP, in->name, in->name_len,
				     false, NULL);
		run_close(&indx->bitmap_run);

		root = indx_get_root(indx, ni, &attr, &mi);
		if (!root) {
			err = -EINVAL;
			goto out;
		}

		root_size = le32_to_cpu(attr->res.data_size);
		new_root_size =
			sizeof(struct INDEX_ROOT) + sizeof(struct NTFS_DE);

		if (new_root_size != root_size &&
		    !mi_resize_attr(mi, attr, new_root_size - root_size)) {
			err = -EINVAL;
			goto out;
		}

		/* Fill first entry. */
		e = (struct NTFS_DE *)(root + 1);
		e->ref.low = 0;
		e->ref.high = 0;
		e->ref.seq = 0;
		e->size = cpu_to_le16(sizeof(struct NTFS_DE));
		e->flags = NTFS_IE_LAST; // 0x02
		e->key_size = 0;
		e->res = 0;

		hdr = &root->ihdr;
		hdr->flags = 0;
		hdr->used = hdr->total = cpu_to_le32(
			new_root_size - offsetof(struct INDEX_ROOT, ihdr));
		mi->dirty = true;
	}

out:
	fnd_put(fnd2);
out1:
	fnd_put(fnd);
out2:
	return err;
}

/*
 * Update duplicated information in directory entry
 * 'dup' - info from MFT record
 */
int indx_update_dup(struct ntfs_inode *ni, struct ntfs_sb_info *sbi,
		    const struct ATTR_FILE_NAME *fname,
		    const struct NTFS_DUP_INFO *dup, int sync)
{
	int err, diff;
	struct NTFS_DE *e = NULL;
	struct ATTR_FILE_NAME *e_fname;
	struct ntfs_fnd *fnd;
	struct INDEX_ROOT *root;
	struct mft_inode *mi;
	struct ntfs_index *indx = &ni->dir;

	fnd = fnd_get();
	if (!fnd)
		return -ENOMEM;

	root = indx_get_root(indx, ni, NULL, &mi);
	if (!root) {
		err = -EINVAL;
		goto out;
	}

	/* Find entry in directory. */
	err = indx_find(indx, ni, root, fname, fname_full_size(fname), sbi,
			&diff, &e, fnd);
	if (err)
		goto out;

	if (!e) {
		err = -EINVAL;
		goto out;
	}

	if (diff) {
		err = -EINVAL;
		goto out;
	}

	e_fname = (struct ATTR_FILE_NAME *)(e + 1);

	if (!memcmp(&e_fname->dup, dup, sizeof(*dup))) {
		/*
		 * Nothing to update in index! Try to avoid this call.
		 */
		goto out;
	}

	memcpy(&e_fname->dup, dup, sizeof(*dup));

	if (fnd->level) {
		/* Directory entry in index. */
		err = indx_write(indx, ni, fnd->nodes[fnd->level - 1], sync);
	} else {
		/* Directory entry in directory MFT record. */
		mi->dirty = true;
		if (sync)
			err = mi_write(mi, 1);
		else
			mark_inode_dirty(&ni->vfs_inode);
	}

out:
	fnd_put(fnd);
	return err;
}
