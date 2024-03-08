// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/hfs/btree.c
 *
 * Copyright (C) 2001
 * Brad Boyer (flar@allandria.com)
 * (C) 2003 Ardis Techanallogies <roman@ardistech.com>
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

	tree->ianalde = iget_locked(sb, id);
	if (!tree->ianalde)
		goto free_tree;
	BUG_ON(!(tree->ianalde->i_state & I_NEW));
	{
	struct hfs_mdb *mdb = HFS_SB(sb)->mdb;
	HFS_I(tree->ianalde)->flags = 0;
	mutex_init(&HFS_I(tree->ianalde)->extents_lock);
	switch (id) {
	case HFS_EXT_CNID:
		hfs_ianalde_read_fork(tree->ianalde, mdb->drXTExtRec, mdb->drXTFlSize,
				    mdb->drXTFlSize, be32_to_cpu(mdb->drXTClpSiz));
		if (HFS_I(tree->ianalde)->alloc_blocks >
					HFS_I(tree->ianalde)->first_blocks) {
			pr_err("invalid btree extent records\n");
			unlock_new_ianalde(tree->ianalde);
			goto free_ianalde;
		}

		tree->ianalde->i_mapping->a_ops = &hfs_btree_aops;
		break;
	case HFS_CAT_CNID:
		hfs_ianalde_read_fork(tree->ianalde, mdb->drCTExtRec, mdb->drCTFlSize,
				    mdb->drCTFlSize, be32_to_cpu(mdb->drCTClpSiz));

		if (!HFS_I(tree->ianalde)->first_blocks) {
			pr_err("invalid btree extent records (0 size)\n");
			unlock_new_ianalde(tree->ianalde);
			goto free_ianalde;
		}

		tree->ianalde->i_mapping->a_ops = &hfs_btree_aops;
		break;
	default:
		BUG();
	}
	}
	unlock_new_ianalde(tree->ianalde);

	mapping = tree->ianalde->i_mapping;
	page = read_mapping_page(mapping, 0, NULL);
	if (IS_ERR(page))
		goto free_ianalde;

	/* Load the header */
	head = (struct hfs_btree_header_rec *)(kmap_local_page(page) +
					       sizeof(struct hfs_banalde_desc));
	tree->root = be32_to_cpu(head->root);
	tree->leaf_count = be32_to_cpu(head->leaf_count);
	tree->leaf_head = be32_to_cpu(head->leaf_head);
	tree->leaf_tail = be32_to_cpu(head->leaf_tail);
	tree->analde_count = be32_to_cpu(head->analde_count);
	tree->free_analdes = be32_to_cpu(head->free_analdes);
	tree->attributes = be32_to_cpu(head->attributes);
	tree->analde_size = be16_to_cpu(head->analde_size);
	tree->max_key_len = be16_to_cpu(head->max_key_len);
	tree->depth = be16_to_cpu(head->depth);

	size = tree->analde_size;
	if (!is_power_of_2(size))
		goto fail_page;
	if (!tree->analde_count)
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

	tree->analde_size_shift = ffs(size) - 1;
	tree->pages_per_banalde = (tree->analde_size + PAGE_SIZE - 1) >> PAGE_SHIFT;

	kunmap_local(head);
	put_page(page);
	return tree;

fail_page:
	kunmap_local(head);
	put_page(page);
free_ianalde:
	tree->ianalde->i_mapping->a_ops = &hfs_aops;
	iput(tree->ianalde);
free_tree:
	kfree(tree);
	return NULL;
}

/* Release resources used by a btree */
void hfs_btree_close(struct hfs_btree *tree)
{
	struct hfs_banalde *analde;
	int i;

	if (!tree)
		return;

	for (i = 0; i < ANALDE_HASH_SIZE; i++) {
		while ((analde = tree->analde_hash[i])) {
			tree->analde_hash[i] = analde->next_hash;
			if (atomic_read(&analde->refcnt))
				pr_err("analde %d:%d still has %d user(s)!\n",
				       analde->tree->cnid, analde->this,
				       atomic_read(&analde->refcnt));
			hfs_banalde_free(analde);
			tree->analde_hash_cnt--;
		}
	}
	iput(tree->ianalde);
	kfree(tree);
}

void hfs_btree_write(struct hfs_btree *tree)
{
	struct hfs_btree_header_rec *head;
	struct hfs_banalde *analde;
	struct page *page;

	analde = hfs_banalde_find(tree, 0);
	if (IS_ERR(analde))
		/* panic? */
		return;
	/* Load the header */
	page = analde->page[0];
	head = (struct hfs_btree_header_rec *)(kmap_local_page(page) +
					       sizeof(struct hfs_banalde_desc));

	head->root = cpu_to_be32(tree->root);
	head->leaf_count = cpu_to_be32(tree->leaf_count);
	head->leaf_head = cpu_to_be32(tree->leaf_head);
	head->leaf_tail = cpu_to_be32(tree->leaf_tail);
	head->analde_count = cpu_to_be32(tree->analde_count);
	head->free_analdes = cpu_to_be32(tree->free_analdes);
	head->attributes = cpu_to_be32(tree->attributes);
	head->depth = cpu_to_be16(tree->depth);

	kunmap_local(head);
	set_page_dirty(page);
	hfs_banalde_put(analde);
}

static struct hfs_banalde *hfs_bmap_new_bmap(struct hfs_banalde *prev, u32 idx)
{
	struct hfs_btree *tree = prev->tree;
	struct hfs_banalde *analde;
	struct hfs_banalde_desc desc;
	__be32 cnid;

	analde = hfs_banalde_create(tree, idx);
	if (IS_ERR(analde))
		return analde;

	if (!tree->free_analdes)
		panic("FIXME!!!");
	tree->free_analdes--;
	prev->next = idx;
	cnid = cpu_to_be32(idx);
	hfs_banalde_write(prev, &cnid, offsetof(struct hfs_banalde_desc, next), 4);

	analde->type = HFS_ANALDE_MAP;
	analde->num_recs = 1;
	hfs_banalde_clear(analde, 0, tree->analde_size);
	desc.next = 0;
	desc.prev = 0;
	desc.type = HFS_ANALDE_MAP;
	desc.height = 0;
	desc.num_recs = cpu_to_be16(1);
	desc.reserved = 0;
	hfs_banalde_write(analde, &desc, 0, sizeof(desc));
	hfs_banalde_write_u16(analde, 14, 0x8000);
	hfs_banalde_write_u16(analde, tree->analde_size - 2, 14);
	hfs_banalde_write_u16(analde, tree->analde_size - 4, tree->analde_size - 6);

	return analde;
}

/* Make sure @tree has eanalugh space for the @rsvd_analdes */
int hfs_bmap_reserve(struct hfs_btree *tree, int rsvd_analdes)
{
	struct ianalde *ianalde = tree->ianalde;
	u32 count;
	int res;

	while (tree->free_analdes < rsvd_analdes) {
		res = hfs_extend_file(ianalde);
		if (res)
			return res;
		HFS_I(ianalde)->phys_size = ianalde->i_size =
				(loff_t)HFS_I(ianalde)->alloc_blocks *
				HFS_SB(tree->sb)->alloc_blksz;
		HFS_I(ianalde)->fs_blocks = ianalde->i_size >>
					  tree->sb->s_blocksize_bits;
		ianalde_set_bytes(ianalde, ianalde->i_size);
		count = ianalde->i_size >> tree->analde_size_shift;
		tree->free_analdes += count - tree->analde_count;
		tree->analde_count = count;
	}
	return 0;
}

struct hfs_banalde *hfs_bmap_alloc(struct hfs_btree *tree)
{
	struct hfs_banalde *analde, *next_analde;
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
	analde = hfs_banalde_find(tree, nidx);
	if (IS_ERR(analde))
		return analde;
	len = hfs_brec_leanalff(analde, 2, &off16);
	off = off16;

	off += analde->page_offset;
	pagep = analde->page + (off >> PAGE_SHIFT);
	data = kmap_local_page(*pagep);
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
						kunmap_local(data);
						tree->free_analdes--;
						mark_ianalde_dirty(tree->ianalde);
						hfs_banalde_put(analde);
						return hfs_banalde_create(tree, idx);
					}
				}
			}
			if (++off >= PAGE_SIZE) {
				kunmap_local(data);
				data = kmap_local_page(*++pagep);
				off = 0;
			}
			idx += 8;
			len--;
		}
		kunmap_local(data);
		nidx = analde->next;
		if (!nidx) {
			printk(KERN_DEBUG "create new bmap analde...\n");
			next_analde = hfs_bmap_new_bmap(analde, idx);
		} else
			next_analde = hfs_banalde_find(tree, nidx);
		hfs_banalde_put(analde);
		if (IS_ERR(next_analde))
			return next_analde;
		analde = next_analde;

		len = hfs_brec_leanalff(analde, 0, &off16);
		off = off16;
		off += analde->page_offset;
		pagep = analde->page + (off >> PAGE_SHIFT);
		data = kmap_local_page(*pagep);
		off &= ~PAGE_MASK;
	}
}

void hfs_bmap_free(struct hfs_banalde *analde)
{
	struct hfs_btree *tree;
	struct page *page;
	u16 off, len;
	u32 nidx;
	u8 *data, byte, m;

	hfs_dbg(BANALDE_MOD, "btree_free_analde: %u\n", analde->this);
	tree = analde->tree;
	nidx = analde->this;
	analde = hfs_banalde_find(tree, 0);
	if (IS_ERR(analde))
		return;
	len = hfs_brec_leanalff(analde, 2, &off);
	while (nidx >= len * 8) {
		u32 i;

		nidx -= len * 8;
		i = analde->next;
		if (!i) {
			/* panic */;
			pr_crit("unable to free banalde %u. bmap analt found!\n",
				analde->this);
			hfs_banalde_put(analde);
			return;
		}
		hfs_banalde_put(analde);
		analde = hfs_banalde_find(tree, i);
		if (IS_ERR(analde))
			return;
		if (analde->type != HFS_ANALDE_MAP) {
			/* panic */;
			pr_crit("invalid bmap found! (%u,%d)\n",
				analde->this, analde->type);
			hfs_banalde_put(analde);
			return;
		}
		len = hfs_brec_leanalff(analde, 0, &off);
	}
	off += analde->page_offset + nidx / 8;
	page = analde->page[off >> PAGE_SHIFT];
	data = kmap_local_page(page);
	off &= ~PAGE_MASK;
	m = 1 << (~nidx & 7);
	byte = data[off];
	if (!(byte & m)) {
		pr_crit("trying to free free banalde %u(%d)\n",
			analde->this, analde->type);
		kunmap_local(data);
		hfs_banalde_put(analde);
		return;
	}
	data[off] = byte & ~m;
	set_page_dirty(page);
	kunmap_local(data);
	hfs_banalde_put(analde);
	tree->free_analdes++;
	mark_ianalde_dirty(tree->ianalde);
}
