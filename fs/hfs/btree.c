// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/hfs/btree.c
 *
 * Copyright (C) 2001
 * Brad Boyer (flar@allandria.com)
 * (C) 2003 Ardis Techyeslogies <roman@ardistech.com>
 *
 * Handle opening/closing btree
 */

#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/log2.h>

#include "btree.h"

/* Get a reference to a B*Tree and do some initial checks */
struct hfs_btree *hfs_btree_open(struct super_block *sb, u32 id, btree_keycmp keycmp)
{
	struct hfs_btree *tree;
	struct hfs_btree_header_rec *head;
	struct address_space *mapping;
	struct page *page;
	unsigned int size;

	tree = kzalloc(sizeof(*tree), GFP_KERNEL);
	if (!tree)
		return NULL;

	mutex_init(&tree->tree_lock);
	spin_lock_init(&tree->hash_lock);
	/* Set the correct compare function */
	tree->sb = sb;
	tree->cnid = id;
	tree->keycmp = keycmp;

	tree->iyesde = iget_locked(sb, id);
	if (!tree->iyesde)
		goto free_tree;
	BUG_ON(!(tree->iyesde->i_state & I_NEW));
	{
	struct hfs_mdb *mdb = HFS_SB(sb)->mdb;
	HFS_I(tree->iyesde)->flags = 0;
	mutex_init(&HFS_I(tree->iyesde)->extents_lock);
	switch (id) {
	case HFS_EXT_CNID:
		hfs_iyesde_read_fork(tree->iyesde, mdb->drXTExtRec, mdb->drXTFlSize,
				    mdb->drXTFlSize, be32_to_cpu(mdb->drXTClpSiz));
		if (HFS_I(tree->iyesde)->alloc_blocks >
					HFS_I(tree->iyesde)->first_blocks) {
			pr_err("invalid btree extent records\n");
			unlock_new_iyesde(tree->iyesde);
			goto free_iyesde;
		}

		tree->iyesde->i_mapping->a_ops = &hfs_btree_aops;
		break;
	case HFS_CAT_CNID:
		hfs_iyesde_read_fork(tree->iyesde, mdb->drCTExtRec, mdb->drCTFlSize,
				    mdb->drCTFlSize, be32_to_cpu(mdb->drCTClpSiz));

		if (!HFS_I(tree->iyesde)->first_blocks) {
			pr_err("invalid btree extent records (0 size)\n");
			unlock_new_iyesde(tree->iyesde);
			goto free_iyesde;
		}

		tree->iyesde->i_mapping->a_ops = &hfs_btree_aops;
		break;
	default:
		BUG();
	}
	}
	unlock_new_iyesde(tree->iyesde);

	mapping = tree->iyesde->i_mapping;
	page = read_mapping_page(mapping, 0, NULL);
	if (IS_ERR(page))
		goto free_iyesde;

	/* Load the header */
	head = (struct hfs_btree_header_rec *)(kmap(page) + sizeof(struct hfs_byesde_desc));
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

	size = tree->yesde_size;
	if (!is_power_of_2(size))
		goto fail_page;
	if (!tree->yesde_count)
		goto fail_page;
	switch (id) {
	case HFS_EXT_CNID:
		if (tree->max_key_len != HFS_MAX_EXT_KEYLEN) {
			pr_err("invalid extent max_key_len %d\n",
			       tree->max_key_len);
			goto fail_page;
		}
		break;
	case HFS_CAT_CNID:
		if (tree->max_key_len != HFS_MAX_CAT_KEYLEN) {
			pr_err("invalid catalog max_key_len %d\n",
			       tree->max_key_len);
			goto fail_page;
		}
		break;
	default:
		BUG();
	}

	tree->yesde_size_shift = ffs(size) - 1;
	tree->pages_per_byesde = (tree->yesde_size + PAGE_SIZE - 1) >> PAGE_SHIFT;

	kunmap(page);
	put_page(page);
	return tree;

fail_page:
	put_page(page);
free_iyesde:
	tree->iyesde->i_mapping->a_ops = &hfs_aops;
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
				pr_err("yesde %d:%d still has %d user(s)!\n",
				       yesde->tree->cnid, yesde->this,
				       atomic_read(&yesde->refcnt));
			hfs_byesde_free(yesde);
			tree->yesde_hash_cnt--;
		}
	}
	iput(tree->iyesde);
	kfree(tree);
}

void hfs_btree_write(struct hfs_btree *tree)
{
	struct hfs_btree_header_rec *head;
	struct hfs_byesde *yesde;
	struct page *page;

	yesde = hfs_byesde_find(tree, 0);
	if (IS_ERR(yesde))
		/* panic? */
		return;
	/* Load the header */
	page = yesde->page[0];
	head = (struct hfs_btree_header_rec *)(kmap(page) + sizeof(struct hfs_byesde_desc));

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

	if (!tree->free_yesdes)
		panic("FIXME!!!");
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
	u32 count;
	int res;

	while (tree->free_yesdes < rsvd_yesdes) {
		res = hfs_extend_file(iyesde);
		if (res)
			return res;
		HFS_I(iyesde)->phys_size = iyesde->i_size =
				(loff_t)HFS_I(iyesde)->alloc_blocks *
				HFS_SB(tree->sb)->alloc_blksz;
		HFS_I(iyesde)->fs_blocks = iyesde->i_size >>
					  tree->sb->s_blocksize_bits;
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
						return hfs_byesde_create(tree, idx);
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
			printk(KERN_DEBUG "create new bmap yesde...\n");
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
			pr_crit("unable to free byesde %u. bmap yest found!\n",
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
			pr_crit("invalid bmap found! (%u,%d)\n",
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
		pr_crit("trying to free free byesde %u(%d)\n",
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
