// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/hfs/btree.c
 *
 * Copyright (C) 2001
 * Brad Boyer (flar@allandria.com)
 * (C) 2003 Ardis Technologies <roman@ardistech.com>
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
	struct folio *folio;
	struct buffer_head *bh;
	unsigned int size;
	u16 dblock;
	sector_t start_block;
	loff_t offset;

	tree = kzalloc(sizeof(*tree), GFP_KERNEL);
	if (!tree)
		return NULL;

	mutex_init(&tree->tree_lock);
	spin_lock_init(&tree->hash_lock);
	/* Set the correct compare function */
	tree->sb = sb;
	tree->cnid = id;
	tree->keycmp = keycmp;

	tree->inode = iget_locked(sb, id);
	if (!tree->inode)
		goto free_tree;
	BUG_ON(!(tree->inode->i_state & I_NEW));
	{
	struct hfs_mdb *mdb = HFS_SB(sb)->mdb;
	HFS_I(tree->inode)->flags = 0;
	mutex_init(&HFS_I(tree->inode)->extents_lock);
	switch (id) {
	case HFS_EXT_CNID:
		hfs_inode_read_fork(tree->inode, mdb->drXTExtRec, mdb->drXTFlSize,
				    mdb->drXTFlSize, be32_to_cpu(mdb->drXTClpSiz));
		if (HFS_I(tree->inode)->alloc_blocks >
					HFS_I(tree->inode)->first_blocks) {
			pr_err("invalid btree extent records\n");
			unlock_new_inode(tree->inode);
			goto free_inode;
		}

		tree->inode->i_mapping->a_ops = &hfs_btree_aops;
		break;
	case HFS_CAT_CNID:
		hfs_inode_read_fork(tree->inode, mdb->drCTExtRec, mdb->drCTFlSize,
				    mdb->drCTFlSize, be32_to_cpu(mdb->drCTClpSiz));

		if (!HFS_I(tree->inode)->first_blocks) {
			pr_err("invalid btree extent records (0 size)\n");
			unlock_new_inode(tree->inode);
			goto free_inode;
		}

		tree->inode->i_mapping->a_ops = &hfs_btree_aops;
		break;
	default:
		BUG();
	}
	}
	unlock_new_inode(tree->inode);

	mapping = tree->inode->i_mapping;
	folio = filemap_grab_folio(mapping, 0);
	if (IS_ERR(folio))
		goto free_inode;

	folio_zero_range(folio, 0, folio_size(folio));

	dblock = hfs_ext_find_block(HFS_I(tree->inode)->first_extents, 0);
	start_block = HFS_SB(sb)->fs_start + (dblock * HFS_SB(sb)->fs_div);

	size = folio_size(folio);
	offset = 0;
	while (size > 0) {
		size_t len;

		bh = sb_bread(sb, start_block);
		if (!bh) {
			pr_err("unable to read tree header\n");
			goto put_folio;
		}

		len = min_t(size_t, folio_size(folio), sb->s_blocksize);
		memcpy_to_folio(folio, offset, bh->b_data, sb->s_blocksize);

		brelse(bh);

		start_block++;
		offset += len;
		size -= len;
	}

	folio_mark_uptodate(folio);

	/* Load the header */
	head = (struct hfs_btree_header_rec *)(kmap_local_folio(folio, 0) +
					       sizeof(struct hfs_bnode_desc));
	tree->root = be32_to_cpu(head->root);
	tree->leaf_count = be32_to_cpu(head->leaf_count);
	tree->leaf_head = be32_to_cpu(head->leaf_head);
	tree->leaf_tail = be32_to_cpu(head->leaf_tail);
	tree->node_count = be32_to_cpu(head->node_count);
	tree->free_nodes = be32_to_cpu(head->free_nodes);
	tree->attributes = be32_to_cpu(head->attributes);
	tree->node_size = be16_to_cpu(head->node_size);
	tree->max_key_len = be16_to_cpu(head->max_key_len);
	tree->depth = be16_to_cpu(head->depth);

	size = tree->node_size;
	if (!is_power_of_2(size))
		goto fail_folio;
	if (!tree->node_count)
		goto fail_folio;
	switch (id) {
	case HFS_EXT_CNID:
		if (tree->max_key_len != HFS_MAX_EXT_KEYLEN) {
			pr_err("invalid extent max_key_len %d\n",
			       tree->max_key_len);
			goto fail_folio;
		}
		break;
	case HFS_CAT_CNID:
		if (tree->max_key_len != HFS_MAX_CAT_KEYLEN) {
			pr_err("invalid catalog max_key_len %d\n",
			       tree->max_key_len);
			goto fail_folio;
		}
		break;
	default:
		BUG();
	}

	tree->node_size_shift = ffs(size) - 1;
	tree->pages_per_bnode = (tree->node_size + PAGE_SIZE - 1) >> PAGE_SHIFT;

	kunmap_local(head);
	folio_unlock(folio);
	folio_put(folio);
	return tree;

fail_folio:
	kunmap_local(head);
put_folio:
	folio_unlock(folio);
	folio_put(folio);
free_inode:
	tree->inode->i_mapping->a_ops = &hfs_aops;
	iput(tree->inode);
free_tree:
	kfree(tree);
	return NULL;
}

/* Release resources used by a btree */
void hfs_btree_close(struct hfs_btree *tree)
{
	struct hfs_bnode *node;
	int i;

	if (!tree)
		return;

	for (i = 0; i < NODE_HASH_SIZE; i++) {
		while ((node = tree->node_hash[i])) {
			tree->node_hash[i] = node->next_hash;
			if (atomic_read(&node->refcnt))
				pr_err("node %d:%d still has %d user(s)!\n",
				       node->tree->cnid, node->this,
				       atomic_read(&node->refcnt));
			hfs_bnode_free(node);
			tree->node_hash_cnt--;
		}
	}
	iput(tree->inode);
	kfree(tree);
}

void hfs_btree_write(struct hfs_btree *tree)
{
	struct hfs_btree_header_rec *head;
	struct hfs_bnode *node;
	struct page *page;

	node = hfs_bnode_find(tree, 0);
	if (IS_ERR(node))
		/* panic? */
		return;
	/* Load the header */
	page = node->page[0];
	head = (struct hfs_btree_header_rec *)(kmap_local_page(page) +
					       sizeof(struct hfs_bnode_desc));

	head->root = cpu_to_be32(tree->root);
	head->leaf_count = cpu_to_be32(tree->leaf_count);
	head->leaf_head = cpu_to_be32(tree->leaf_head);
	head->leaf_tail = cpu_to_be32(tree->leaf_tail);
	head->node_count = cpu_to_be32(tree->node_count);
	head->free_nodes = cpu_to_be32(tree->free_nodes);
	head->attributes = cpu_to_be32(tree->attributes);
	head->depth = cpu_to_be16(tree->depth);

	kunmap_local(head);
	set_page_dirty(page);
	hfs_bnode_put(node);
}

static struct hfs_bnode *hfs_bmap_new_bmap(struct hfs_bnode *prev, u32 idx)
{
	struct hfs_btree *tree = prev->tree;
	struct hfs_bnode *node;
	struct hfs_bnode_desc desc;
	__be32 cnid;

	node = hfs_bnode_create(tree, idx);
	if (IS_ERR(node))
		return node;

	if (!tree->free_nodes)
		panic("FIXME!!!");
	tree->free_nodes--;
	prev->next = idx;
	cnid = cpu_to_be32(idx);
	hfs_bnode_write(prev, &cnid, offsetof(struct hfs_bnode_desc, next), 4);

	node->type = HFS_NODE_MAP;
	node->num_recs = 1;
	hfs_bnode_clear(node, 0, tree->node_size);
	desc.next = 0;
	desc.prev = 0;
	desc.type = HFS_NODE_MAP;
	desc.height = 0;
	desc.num_recs = cpu_to_be16(1);
	desc.reserved = 0;
	hfs_bnode_write(node, &desc, 0, sizeof(desc));
	hfs_bnode_write_u16(node, 14, 0x8000);
	hfs_bnode_write_u16(node, tree->node_size - 2, 14);
	hfs_bnode_write_u16(node, tree->node_size - 4, tree->node_size - 6);

	return node;
}

/* Make sure @tree has enough space for the @rsvd_nodes */
int hfs_bmap_reserve(struct hfs_btree *tree, int rsvd_nodes)
{
	struct inode *inode = tree->inode;
	u32 count;
	int res;

	while (tree->free_nodes < rsvd_nodes) {
		res = hfs_extend_file(inode);
		if (res)
			return res;
		HFS_I(inode)->phys_size = inode->i_size =
				(loff_t)HFS_I(inode)->alloc_blocks *
				HFS_SB(tree->sb)->alloc_blksz;
		HFS_I(inode)->fs_blocks = inode->i_size >>
					  tree->sb->s_blocksize_bits;
		inode_set_bytes(inode, inode->i_size);
		count = inode->i_size >> tree->node_size_shift;
		tree->free_nodes += count - tree->node_count;
		tree->node_count = count;
	}
	return 0;
}

struct hfs_bnode *hfs_bmap_alloc(struct hfs_btree *tree)
{
	struct hfs_bnode *node, *next_node;
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
	node = hfs_bnode_find(tree, nidx);
	if (IS_ERR(node))
		return node;
	len = hfs_brec_lenoff(node, 2, &off16);
	off = off16;

	off += node->page_offset;
	pagep = node->page + (off >> PAGE_SHIFT);
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
						tree->free_nodes--;
						mark_inode_dirty(tree->inode);
						hfs_bnode_put(node);
						return hfs_bnode_create(tree, idx);
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
		nidx = node->next;
		if (!nidx) {
			printk(KERN_DEBUG "create new bmap node...\n");
			next_node = hfs_bmap_new_bmap(node, idx);
		} else
			next_node = hfs_bnode_find(tree, nidx);
		hfs_bnode_put(node);
		if (IS_ERR(next_node))
			return next_node;
		node = next_node;

		len = hfs_brec_lenoff(node, 0, &off16);
		off = off16;
		off += node->page_offset;
		pagep = node->page + (off >> PAGE_SHIFT);
		data = kmap_local_page(*pagep);
		off &= ~PAGE_MASK;
	}
}

void hfs_bmap_free(struct hfs_bnode *node)
{
	struct hfs_btree *tree;
	struct page *page;
	u16 off, len;
	u32 nidx;
	u8 *data, byte, m;

	hfs_dbg("node %u\n", node->this);
	tree = node->tree;
	nidx = node->this;
	node = hfs_bnode_find(tree, 0);
	if (IS_ERR(node))
		return;
	len = hfs_brec_lenoff(node, 2, &off);
	while (nidx >= len * 8) {
		u32 i;

		nidx -= len * 8;
		i = node->next;
		if (!i) {
			/* panic */;
			pr_crit("unable to free bnode %u. bmap not found!\n",
				node->this);
			hfs_bnode_put(node);
			return;
		}
		hfs_bnode_put(node);
		node = hfs_bnode_find(tree, i);
		if (IS_ERR(node))
			return;
		if (node->type != HFS_NODE_MAP) {
			/* panic */;
			pr_crit("invalid bmap found! (%u,%d)\n",
				node->this, node->type);
			hfs_bnode_put(node);
			return;
		}
		len = hfs_brec_lenoff(node, 0, &off);
	}
	off += node->page_offset + nidx / 8;
	page = node->page[off >> PAGE_SHIFT];
	data = kmap_local_page(page);
	off &= ~PAGE_MASK;
	m = 1 << (~nidx & 7);
	byte = data[off];
	if (!(byte & m)) {
		pr_crit("trying to free free bnode %u(%d)\n",
			node->this, node->type);
		kunmap_local(data);
		hfs_bnode_put(node);
		return;
	}
	data[off] = byte & ~m;
	set_page_dirty(page);
	kunmap_local(data);
	hfs_bnode_put(node);
	tree->free_nodes++;
	mark_inode_dirty(tree->inode);
}
