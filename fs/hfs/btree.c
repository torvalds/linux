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

/* Context for iterating b-tree map pages
 * @page_idx: The index of the page within the b-node's page array
 * @off: The byte offset within the mapped page
 * @len: The remaining length of the map record
 */
struct hfs_bmap_ctx {
	unsigned int page_idx;
	unsigned int off;
	u16 len;
};

/*
 * Finds the specific page containing the requested byte offset within the map
 * record. Automatically handles the difference between header and map nodes.
 * Returns the struct page pointer, or an ERR_PTR on failure.
 * Note: The caller is responsible for mapping/unmapping the returned page.
 */
static struct page *hfs_bmap_get_map_page(struct hfs_bnode *node,
					  struct hfs_bmap_ctx *ctx,
					  u32 byte_offset)
{
	u16 rec_idx, off16;
	unsigned int page_off;

	if (node->this == HFS_TREE_HEAD) {
		if (node->type != HFS_NODE_HEADER) {
			pr_err("hfs: invalid btree header node\n");
			return ERR_PTR(-EIO);
		}
		rec_idx = HFS_BTREE_HDR_MAP_REC_INDEX;
	} else {
		if (node->type != HFS_NODE_MAP) {
			pr_err("hfs: invalid btree map node\n");
			return ERR_PTR(-EIO);
		}
		rec_idx = HFS_BTREE_MAP_NODE_REC_INDEX;
	}

	ctx->len = hfs_brec_lenoff(node, rec_idx, &off16);
	if (!ctx->len)
		return ERR_PTR(-ENOENT);

	if (!is_bnode_offset_valid(node, off16))
		return ERR_PTR(-EIO);

	ctx->len = check_and_correct_requested_length(node, off16, ctx->len);

	if (byte_offset >= ctx->len)
		return ERR_PTR(-EINVAL);

	page_off = (u32)off16 + node->page_offset + byte_offset;
	ctx->page_idx = page_off >> PAGE_SHIFT;
	ctx->off = page_off & ~PAGE_MASK;

	return node->page[ctx->page_idx];
}

/**
 * hfs_bmap_test_bit - test a bit in the b-tree map
 * @node: the b-tree node containing the map record
 * @node_bit_idx: the relative bit index within the node's map record
 *
 * Returns true if set, false if clear or on failure.
 */
static bool hfs_bmap_test_bit(struct hfs_bnode *node, u32 node_bit_idx)
{
	struct hfs_bmap_ctx ctx;
	struct page *page;
	u8 *bmap, byte, mask;

	page = hfs_bmap_get_map_page(node, &ctx, node_bit_idx / BITS_PER_BYTE);
	if (IS_ERR(page))
		return false;

	bmap = kmap_local_page(page);
	byte = bmap[ctx.off];
	kunmap_local(bmap);

	mask = 1 << (7 - (node_bit_idx % BITS_PER_BYTE));
	return (byte & mask) != 0;
}

/**
 * hfs_bmap_clear_bit - clear a bit in the b-tree map
 * @node: the b-tree node containing the map record
 * @node_bit_idx: the relative bit index within the node's map record
 *
 * Returns 0 on success, -EINVAL if already clear, or negative error code.
 */
static int hfs_bmap_clear_bit(struct hfs_bnode *node, u32 node_bit_idx)
{
	struct hfs_bmap_ctx ctx;
	struct page *page;
	u8 *bmap, mask;

	page = hfs_bmap_get_map_page(node, &ctx, node_bit_idx / BITS_PER_BYTE);
	if (IS_ERR(page))
		return PTR_ERR(page);

	bmap = kmap_local_page(page);

	mask = 1 << (7 - (node_bit_idx % BITS_PER_BYTE));

	if (!(bmap[ctx.off] & mask)) {
		kunmap_local(bmap);
		return -EINVAL;
	}

	bmap[ctx.off] &= ~mask;
	set_page_dirty(page);
	kunmap_local(bmap);

	return 0;
}

/* Get a reference to a B*Tree and do some initial checks */
struct hfs_btree *hfs_btree_open(struct super_block *sb, u32 id, btree_keycmp keycmp)
{
	struct hfs_btree *tree;
	struct hfs_btree_header_rec *head;
	struct address_space *mapping;
	struct folio *folio;
	struct buffer_head *bh;
	struct hfs_bnode *node;
	unsigned int size;
	u16 dblock;
	sector_t start_block;
	loff_t offset;

	tree = kzalloc_obj(*tree);
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
	BUG_ON(!(inode_state_read_once(tree->inode) & I_NEW));
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

	node = hfs_bnode_find(tree, HFS_TREE_HEAD);
	if (IS_ERR(node))
		goto free_inode;

	if (!hfs_bmap_test_bit(node, HFS_TREE_HEAD)) {
		pr_warn("(%s): %s (cnid 0x%x) bitmap corrupted, forcing rdonly\n",
			sb->s_id, id == HFS_EXT_CNID ? "extents" : "catalog", id);
		pr_warn("Run fsck.hfs to repair.\n");
		sb->s_flags |= SB_RDONLY;
	}

	hfs_bnode_put(node);

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
int hfs_bmap_reserve(struct hfs_btree *tree, u32 rsvd_nodes)
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
	struct hfs_bmap_ctx ctx;
	struct page *page;
	u32 nidx, idx;
	u8 *data, byte, m;
	int i, res;

	res = hfs_bmap_reserve(tree, 1);
	if (res)
		return ERR_PTR(res);

	nidx = 0;
	node = hfs_bnode_find(tree, nidx);
	if (IS_ERR(node))
		return node;

	page = hfs_bmap_get_map_page(node, &ctx, 0);
	if (IS_ERR(page)) {
		res = PTR_ERR(page);
		hfs_bnode_put(node);
		return ERR_PTR(res);
	}

	data = kmap_local_page(page);
	idx = 0;

	for (;;) {
		while (ctx.len) {
			byte = data[ctx.off];
			if (byte != 0xff) {
				for (m = 0x80, i = 0; i < 8; m >>= 1, i++) {
					if (!(byte & m)) {
						idx += i;
						data[ctx.off] |= m;
						set_page_dirty(page);
						kunmap_local(data);
						tree->free_nodes--;
						mark_inode_dirty(tree->inode);
						hfs_bnode_put(node);
						return hfs_bnode_create(tree, idx);
					}
				}
			}
			if (++ctx.off >= PAGE_SIZE) {
				kunmap_local(data);
				page = node->page[++ctx.page_idx];
				data = kmap_local_page(page);
				ctx.off = 0;
			}
			idx += 8;
			ctx.len--;
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

		page = hfs_bmap_get_map_page(node, &ctx, 0);
		if (IS_ERR(page)) {
			res = PTR_ERR(page);
			hfs_bnode_put(node);
			return ERR_PTR(res);
		}
		data = kmap_local_page(page);
	}
}

void hfs_bmap_free(struct hfs_bnode *node)
{
	struct hfs_btree *tree;
	u16 off, len;
	u32 nidx;
	int res;

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

	res = hfs_bmap_clear_bit(node, nidx);
	if (res == -EINVAL) {
		pr_crit("trying to free free bnode %u(%d)\n",
			nidx, node->type);
	} else if (res) {
		pr_crit("fail to free bnode %u(%d)\n",
			nidx, node->type);
	} else {
		tree->free_nodes++;
		mark_inode_dirty(tree->inode);
	}

	hfs_bnode_put(node);
}
