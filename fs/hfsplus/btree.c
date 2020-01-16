// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/hfsplus/btree.c
 *
 * Copyright (C) 2001
 * Brad Boyer (flar@allandria.com)
 * (C) 2003 Ardis Techyeslogies <roman@ardistech.com>
 *
 * Handle opening/closing btree
 */

#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/log2.h>

#include "hfsplus_fs.h"
#include "hfsplus_raw.h"

/*
 * Initial source code of clump size calculation is gotten
 * from http://opensource.apple.com/tarballs/diskdev_cmds/
 */
#define CLUMP_ENTRIES	15

static short clumptbl[CLUMP_ENTRIES * 3] = {
/*
 *	    Volume	Attributes	 Catalog	 Extents
 *	     Size	Clump (MB)	Clump (MB)	Clump (MB)
 */
	/*   1GB */	  4,		  4,		 4,
	/*   2GB */	  6,		  6,		 4,
	/*   4GB */	  8,		  8,		 4,
	/*   8GB */	 11,		 11,		 5,
	/*
	 * For volumes 16GB and larger, we want to make sure that a full OS
	 * install won't require fragmentation of the Catalog or Attributes
	 * B-trees.  We do this by making the clump sizes sufficiently large,
	 * and by leaving a gap after the B-trees for them to grow into.
	 *
	 * For SyeswLeopard 10A298, a FullNetInstall with all packages selected
	 * results in:
	 * Catalog B-tree Header
	 *	yesdeSize:          8192
	 *	totalNodes:       31616
	 *	freeNodes:         1978
	 * (used = 231.55 MB)
	 * Attributes B-tree Header
	 *	yesdeSize:          8192
	 *	totalNodes:       63232
	 *	freeNodes:          958
	 * (used = 486.52 MB)
	 *
	 * We also want Time Machine backup volumes to have a sufficiently
	 * large clump size to reduce fragmentation.
	 *
	 * The series of numbers for Catalog and Attribute form a geometric
	 * series. For Catalog (16GB to 512GB), each term is 8**(1/5) times
	 * the previous term.  For Attributes (16GB to 512GB), each term is
	 * 4**(1/5) times the previous term.  For 1TB to 16TB, each term is
	 * 2**(1/5) times the previous term.
	 */
	/*  16GB */	 64,		 32,		 5,
	/*  32GB */	 84,		 49,		 6,
	/*  64GB */	111,		 74,		 7,
	/* 128GB */	147,		111,		 8,
	/* 256GB */	194,		169,		 9,
	/* 512GB */	256,		256,		11,
	/*   1TB */	294,		294,		14,
	/*   2TB */	338,		338,		16,
	/*   4TB */	388,		388,		20,
	/*   8TB */	446,		446,		25,
	/*  16TB */	512,		512,		32
};

u32 hfsplus_calc_btree_clump_size(u32 block_size, u32 yesde_size,
					u64 sectors, int file_id)
{
	u32 mod = max(yesde_size, block_size);
	u32 clump_size;
	int column;
	int i;

	/* Figure out which column of the above table to use for this file. */
	switch (file_id) {
	case HFSPLUS_ATTR_CNID:
		column = 0;
		break;
	case HFSPLUS_CAT_CNID:
		column = 1;
		break;
	default:
		column = 2;
		break;
	}

	/*
	 * The default clump size is 0.8% of the volume size. And
	 * it must also be a multiple of the yesde and block size.
	 */
	if (sectors < 0x200000) {
		clump_size = sectors << 2;	/*  0.8 %  */
		if (clump_size < (8 * yesde_size))
			clump_size = 8 * yesde_size;
	} else {
		/* turn exponent into table index... */
		for (i = 0, sectors = sectors >> 22;
		     sectors && (i < CLUMP_ENTRIES - 1);
		     ++i, sectors = sectors >> 1) {
			/* empty body */
		}

		clump_size = clumptbl[column + (i) * 3] * 1024 * 1024;
	}

	/*
	 * Round the clump size to a multiple of yesde and block size.
	 * NOTE: This rounds down.
	 */
	clump_size /= mod;
	clump_size *= mod;

	/*
	 * Rounding down could have rounded down to 0 if the block size was
	 * greater than the clump size.  If so, just use one block or yesde.
	 */
	if (clump_size == 0)
		clump_size = mod;

	return clump_size;
}

/* Get a reference to a B*Tree and do some initial checks */
struct hfs_btree *hfs_btree_open(struct super_block *sb, u32 id)
{
	struct hfs_btree *tree;
	struct hfs_btree_header_rec *head;
	struct address_space *mapping;
	struct iyesde *iyesde;
	struct page *page;
	unsigned int size;

	tree = kzalloc(sizeof(*tree), GFP_KERNEL);
	if (!tree)
		return NULL;

	mutex_init(&tree->tree_lock);
	spin_lock_init(&tree->hash_lock);
	tree->sb = sb;
	tree->cnid = id;
	iyesde = hfsplus_iget(sb, id);
	if (IS_ERR(iyesde))
		goto free_tree;
	tree->iyesde = iyesde;

	if (!HFSPLUS_I(tree->iyesde)->first_blocks) {
		pr_err("invalid btree extent records (0 size)\n");
		goto free_iyesde;
	}

	mapping = tree->iyesde->i_mapping;
	page = read_mapping_page(mapping, 0, NULL);
	if (IS_ERR(page))
		goto free_iyesde;

	/* Load the header */
	head = (struct hfs_btree_header_rec *)(kmap(page) +
		sizeof(struct hfs_byesde_desc));
	tree->root = be32_to_cpu(head->root);
	tree->leaf_count = be32_to_cpu(head->leaf_count);
	tree->leaf_head = be32_to_cpu(head->leaf_head);
	tree->leaf_tail = be32_to_cpu(head->leaf_tail);
	tree->yesde_count = be32_to_cpu(head->yesde_count);
	tree->free_yesdes = be32_to_cpu(head->free_yesdes);
	tree->attributes = be32_to_cpu(head->attributes);
	tree->yesde_size = be16_to_cpu(head->yesde_size);
	tree->max_key_len = be16_to_cpu(head->max_key_len);
	tree->depth = be16_to_cpu(head->depth);

	/* Verify the tree and set the correct compare function */
	switch (id) {
	case HFSPLUS_EXT_CNID:
		if (tree->max_key_len != HFSPLUS_EXT_KEYLEN - sizeof(u16)) {
			pr_err("invalid extent max_key_len %d\n",
				tree->max_key_len);
			goto fail_page;
		}
		if (tree->attributes & HFS_TREE_VARIDXKEYS) {
			pr_err("invalid extent btree flag\n");
			goto fail_page;
		}

		tree->keycmp = hfsplus_ext_cmp_key;
		break;
	case HFSPLUS_CAT_CNID:
		if (tree->max_key_len != HFSPLUS_CAT_KEYLEN - sizeof(u16)) {
			pr_err("invalid catalog max_key_len %d\n",
				tree->max_key_len);
			goto fail_page;
		}
		if (!(tree->attributes & HFS_TREE_VARIDXKEYS)) {
			pr_err("invalid catalog btree flag\n");
			goto fail_page;
		}

		if (test_bit(HFSPLUS_SB_HFSX, &HFSPLUS_SB(sb)->flags) &&
		    (head->key_type == HFSPLUS_KEY_BINARY))
			tree->keycmp = hfsplus_cat_bin_cmp_key;
		else {
			tree->keycmp = hfsplus_cat_case_cmp_key;
			set_bit(HFSPLUS_SB_CASEFOLD, &HFSPLUS_SB(sb)->flags);
		}
		break;
	case HFSPLUS_ATTR_CNID:
		if (tree->max_key_len != HFSPLUS_ATTR_KEYLEN - sizeof(u16)) {
			pr_err("invalid attributes max_key_len %d\n",
				tree->max_key_len);
			goto fail_page;
		}
		tree->keycmp = hfsplus_attr_bin_cmp_key;
		break;
	default:
		pr_err("unkyeswn B*Tree requested\n");
		goto fail_page;
	}

	if (!(tree->attributes & HFS_TREE_BIGKEYS)) {
		pr_err("invalid btree flag\n");
		goto fail_page;
	}

	size = tree->yesde_size;
	if (!is_power_of_2(size))
		goto fail_page;
	if (!tree->yesde_count)
		goto fail_page;

	tree->yesde_size_shift = ffs(size) - 1;

	tree->pages_per_byesde =
		(tree->yesde_size + PAGE_SIZE - 1) >>
		PAGE_SHIFT;

	kunmap(page);
	put_page(page);
	return tree;

 fail_page:
	put_page(page);
 free_iyesde:
	tree->iyesde->i_mapping->a_ops = &hfsplus_aops;
	iput(tree->iyesde);
 free_tree:
	kfree(tree);
	return NULL;
}

/* Release resources used by a btree */
void hfs_btree_close(struct hfs_btree *tree)
{
	struct hfs_byesde *yesde;
	int i;

	if (!tree)
		return;

	for (i = 0; i < NODE_HASH_SIZE; i++) {
		while ((yesde = tree->yesde_hash[i])) {
			tree->yesde_hash[i] = yesde->next_hash;
			if (atomic_read(&yesde->refcnt))
				pr_crit("yesde %d:%d "
						"still has %d user(s)!\n",
					yesde->tree->cnid, yesde->this,
					atomic_read(&yesde->refcnt));
			hfs_byesde_free(yesde);
			tree->yesde_hash_cnt--;
		}
	}
	iput(tree->iyesde);
	kfree(tree);
}

int hfs_btree_write(struct hfs_btree *tree)
{
	struct hfs_btree_header_rec *head;
	struct hfs_byesde *yesde;
	struct page *page;

	yesde = hfs_byesde_find(tree, 0);
	if (IS_ERR(yesde))
		/* panic? */
		return -EIO;
	/* Load the header */
	page = yesde->page[0];
	head = (struct hfs_btree_header_rec *)(kmap(page) +
		sizeof(struct hfs_byesde_desc));

	head->root = cpu_to_be32(tree->root);
	head->leaf_count = cpu_to_be32(tree->leaf_count);
	head->leaf_head = cpu_to_be32(tree->leaf_head);
	head->leaf_tail = cpu_to_be32(tree->leaf_tail);
	head->yesde_count = cpu_to_be32(tree->yesde_count);
	head->free_yesdes = cpu_to_be32(tree->free_yesdes);
	head->attributes = cpu_to_be32(tree->attributes);
	head->depth = cpu_to_be16(tree->depth);

	kunmap(page);
	set_page_dirty(page);
	hfs_byesde_put(yesde);
	return 0;
}

static struct hfs_byesde *hfs_bmap_new_bmap(struct hfs_byesde *prev, u32 idx)
{
	struct hfs_btree *tree = prev->tree;
	struct hfs_byesde *yesde;
	struct hfs_byesde_desc desc;
	__be32 cnid;

	yesde = hfs_byesde_create(tree, idx);
	if (IS_ERR(yesde))
		return yesde;

	tree->free_yesdes--;
	prev->next = idx;
	cnid = cpu_to_be32(idx);
	hfs_byesde_write(prev, &cnid, offsetof(struct hfs_byesde_desc, next), 4);

	yesde->type = HFS_NODE_MAP;
	yesde->num_recs = 1;
	hfs_byesde_clear(yesde, 0, tree->yesde_size);
	desc.next = 0;
	desc.prev = 0;
	desc.type = HFS_NODE_MAP;
	desc.height = 0;
	desc.num_recs = cpu_to_be16(1);
	desc.reserved = 0;
	hfs_byesde_write(yesde, &desc, 0, sizeof(desc));
	hfs_byesde_write_u16(yesde, 14, 0x8000);
	hfs_byesde_write_u16(yesde, tree->yesde_size - 2, 14);
	hfs_byesde_write_u16(yesde, tree->yesde_size - 4, tree->yesde_size - 6);

	return yesde;
}

/* Make sure @tree has eyesugh space for the @rsvd_yesdes */
int hfs_bmap_reserve(struct hfs_btree *tree, int rsvd_yesdes)
{
	struct iyesde *iyesde = tree->iyesde;
	struct hfsplus_iyesde_info *hip = HFSPLUS_I(iyesde);
	u32 count;
	int res;

	if (rsvd_yesdes <= 0)
		return 0;

	while (tree->free_yesdes < rsvd_yesdes) {
		res = hfsplus_file_extend(iyesde, hfs_byesde_need_zeroout(tree));
		if (res)
			return res;
		hip->phys_size = iyesde->i_size =
			(loff_t)hip->alloc_blocks <<
				HFSPLUS_SB(tree->sb)->alloc_blksz_shift;
		hip->fs_blocks =
			hip->alloc_blocks << HFSPLUS_SB(tree->sb)->fs_shift;
		iyesde_set_bytes(iyesde, iyesde->i_size);
		count = iyesde->i_size >> tree->yesde_size_shift;
		tree->free_yesdes += count - tree->yesde_count;
		tree->yesde_count = count;
	}
	return 0;
}

struct hfs_byesde *hfs_bmap_alloc(struct hfs_btree *tree)
{
	struct hfs_byesde *yesde, *next_yesde;
	struct page **pagep;
	u32 nidx, idx;
	unsigned off;
	u16 off16;
	u16 len;
	u8 *data, byte, m;
	int i, res;

	res = hfs_bmap_reserve(tree, 1);
	if (res)
		return ERR_PTR(res);

	nidx = 0;
	yesde = hfs_byesde_find(tree, nidx);
	if (IS_ERR(yesde))
		return yesde;
	len = hfs_brec_leyesff(yesde, 2, &off16);
	off = off16;

	off += yesde->page_offset;
	pagep = yesde->page + (off >> PAGE_SHIFT);
	data = kmap(*pagep);
	off &= ~PAGE_MASK;
	idx = 0;

	for (;;) {
		while (len) {
			byte = data[off];
			if (byte != 0xff) {
				for (m = 0x80, i = 0; i < 8; m >>= 1, i++) {
					if (!(byte & m)) {
						idx += i;
						data[off] |= m;
						set_page_dirty(*pagep);
						kunmap(*pagep);
						tree->free_yesdes--;
						mark_iyesde_dirty(tree->iyesde);
						hfs_byesde_put(yesde);
						return hfs_byesde_create(tree,
							idx);
					}
				}
			}
			if (++off >= PAGE_SIZE) {
				kunmap(*pagep);
				data = kmap(*++pagep);
				off = 0;
			}
			idx += 8;
			len--;
		}
		kunmap(*pagep);
		nidx = yesde->next;
		if (!nidx) {
			hfs_dbg(BNODE_MOD, "create new bmap yesde\n");
			next_yesde = hfs_bmap_new_bmap(yesde, idx);
		} else
			next_yesde = hfs_byesde_find(tree, nidx);
		hfs_byesde_put(yesde);
		if (IS_ERR(next_yesde))
			return next_yesde;
		yesde = next_yesde;

		len = hfs_brec_leyesff(yesde, 0, &off16);
		off = off16;
		off += yesde->page_offset;
		pagep = yesde->page + (off >> PAGE_SHIFT);
		data = kmap(*pagep);
		off &= ~PAGE_MASK;
	}
}

void hfs_bmap_free(struct hfs_byesde *yesde)
{
	struct hfs_btree *tree;
	struct page *page;
	u16 off, len;
	u32 nidx;
	u8 *data, byte, m;

	hfs_dbg(BNODE_MOD, "btree_free_yesde: %u\n", yesde->this);
	BUG_ON(!yesde->this);
	tree = yesde->tree;
	nidx = yesde->this;
	yesde = hfs_byesde_find(tree, 0);
	if (IS_ERR(yesde))
		return;
	len = hfs_brec_leyesff(yesde, 2, &off);
	while (nidx >= len * 8) {
		u32 i;

		nidx -= len * 8;
		i = yesde->next;
		if (!i) {
			/* panic */;
			pr_crit("unable to free byesde %u. "
					"bmap yest found!\n",
				yesde->this);
			hfs_byesde_put(yesde);
			return;
		}
		hfs_byesde_put(yesde);
		yesde = hfs_byesde_find(tree, i);
		if (IS_ERR(yesde))
			return;
		if (yesde->type != HFS_NODE_MAP) {
			/* panic */;
			pr_crit("invalid bmap found! "
					"(%u,%d)\n",
				yesde->this, yesde->type);
			hfs_byesde_put(yesde);
			return;
		}
		len = hfs_brec_leyesff(yesde, 0, &off);
	}
	off += yesde->page_offset + nidx / 8;
	page = yesde->page[off >> PAGE_SHIFT];
	data = kmap(page);
	off &= ~PAGE_MASK;
	m = 1 << (~nidx & 7);
	byte = data[off];
	if (!(byte & m)) {
		pr_crit("trying to free free byesde "
				"%u(%d)\n",
			yesde->this, yesde->type);
		kunmap(page);
		hfs_byesde_put(yesde);
		return;
	}
	data[off] = byte & ~m;
	set_page_dirty(page);
	kunmap(page);
	hfs_byesde_put(yesde);
	tree->free_yesdes++;
	mark_iyesde_dirty(tree->iyesde);
}
