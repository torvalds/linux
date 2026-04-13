// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/hfsplus/btree.c
 *
 * Copyright (C) 2001
 * Brad Boyer (flar@allandria.com)
 * (C) 2003 Ardis Technologies <roman@ardistech.com>
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
	 * For SnowLeopard 10A298, a FullNetInstall with all packages selected
	 * results in:
	 * Catalog B-tree Header
	 *	nodeSize:          8192
	 *	totalNodes:       31616
	 *	freeNodes:         1978
	 * (used = 231.55 MB)
	 * Attributes B-tree Header
	 *	nodeSize:          8192
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

u32 hfsplus_calc_btree_clump_size(u32 block_size, u32 node_size,
					u64 sectors, int file_id)
{
	u32 mod = max(node_size, block_size);
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
	 * it must also be a multiple of the node and block size.
	 */
	if (sectors < 0x200000) {
		clump_size = sectors << 2;	/*  0.8 %  */
		if (clump_size < (8 * node_size))
			clump_size = 8 * node_size;
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
	 * Round the clump size to a multiple of node and block size.
	 * NOTE: This rounds down.
	 */
	clump_size /= mod;
	clump_size *= mod;

	/*
	 * Rounding down could have rounded down to 0 if the block size was
	 * greater than the clump size.  If so, just use one block or node.
	 */
	if (clump_size == 0)
		clump_size = mod;

	return clump_size;
}

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

	if (node->this == HFSPLUS_TREE_HEAD) {
		if (node->type != HFS_NODE_HEADER) {
			pr_err("hfsplus: invalid btree header node\n");
			return ERR_PTR(-EIO);
		}
		rec_idx = HFSPLUS_BTREE_HDR_MAP_REC_INDEX;
	} else {
		if (node->type != HFS_NODE_MAP) {
			pr_err("hfsplus: invalid btree map node\n");
			return ERR_PTR(-EIO);
		}
		rec_idx = HFSPLUS_BTREE_MAP_NODE_REC_INDEX;
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

#define HFS_EXTENT_TREE_NAME  "Extents Overflow File"
#define HFS_CATALOG_TREE_NAME "Catalog File"
#define HFS_ATTR_TREE_NAME    "Attributes File"
#define HFS_UNKNOWN_TREE_NAME "Unknown B-tree"

static const char *hfs_btree_name(u32 cnid)
{
	switch (cnid) {
	case HFSPLUS_EXT_CNID:
		return HFS_EXTENT_TREE_NAME;
	case HFSPLUS_CAT_CNID:
		return HFS_CATALOG_TREE_NAME;
	case HFSPLUS_ATTR_CNID:
		return HFS_ATTR_TREE_NAME;
	default:
		return HFS_UNKNOWN_TREE_NAME;
	}
}

/* Get a reference to a B*Tree and do some initial checks */
struct hfs_btree *hfs_btree_open(struct super_block *sb, u32 id)
{
	struct hfs_btree *tree;
	struct hfs_btree_header_rec *head;
	struct address_space *mapping;
	struct hfs_bnode *node;
	struct inode *inode;
	struct page *page;
	unsigned int size;

	tree = kzalloc_obj(*tree);
	if (!tree)
		return NULL;

	mutex_init(&tree->tree_lock);
	spin_lock_init(&tree->hash_lock);
	tree->sb = sb;
	tree->cnid = id;
	inode = hfsplus_iget(sb, id);
	if (IS_ERR(inode))
		goto free_tree;
	tree->inode = inode;

	if (!HFSPLUS_I(tree->inode)->first_blocks) {
		pr_err("invalid btree extent records (0 size)\n");
		goto free_inode;
	}

	mapping = tree->inode->i_mapping;
	page = read_mapping_page(mapping, 0, NULL);
	if (IS_ERR(page))
		goto free_inode;

	/* Load the header */
	head = (struct hfs_btree_header_rec *)(kmap_local_page(page) +
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
		pr_err("unknown B*Tree requested\n");
		goto fail_page;
	}

	if (!(tree->attributes & HFS_TREE_BIGKEYS)) {
		pr_err("invalid btree flag\n");
		goto fail_page;
	}

	size = tree->node_size;
	if (!is_power_of_2(size))
		goto fail_page;
	if (!tree->node_count)
		goto fail_page;

	tree->node_size_shift = ffs(size) - 1;

	tree->pages_per_bnode =
		(tree->node_size + PAGE_SIZE - 1) >>
		PAGE_SHIFT;

	kunmap_local(head);
	put_page(page);

	node = hfs_bnode_find(tree, HFSPLUS_TREE_HEAD);
	if (IS_ERR(node))
		goto free_inode;

	if (!hfs_bmap_test_bit(node, 0)) {
		pr_warn("(%s): %s (cnid 0x%x) map record invalid or bitmap corruption detected, forcing read-only.\n",
				sb->s_id, hfs_btree_name(id), id);
		pr_warn("Run fsck.hfsplus to repair.\n");
		sb->s_flags |= SB_RDONLY;
	}

	hfs_bnode_put(node);

	return tree;

 fail_page:
	kunmap_local(head);
	put_page(page);
 free_inode:
	tree->inode->i_mapping->a_ops = &hfsplus_aops;
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
				pr_crit("node %d:%d "
						"still has %d user(s)!\n",
					node->tree->cnid, node->this,
					atomic_read(&node->refcnt));
			hfs_bnode_free(node);
			tree->node_hash_cnt--;
		}
	}
	iput(tree->inode);
	kfree(tree);
}

int hfs_btree_write(struct hfs_btree *tree)
{
	struct hfs_btree_header_rec *head;
	struct hfs_bnode *node;
	struct page *page;

	node = hfs_bnode_find(tree, 0);
	if (IS_ERR(node))
		/* panic? */
		return -EIO;
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
	return 0;
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
	struct hfsplus_inode_info *hip = HFSPLUS_I(inode);
	u32 count;
	int res;

	lockdep_assert_held(&tree->tree_lock);

	if (rsvd_nodes <= 0)
		return 0;

	while (tree->free_nodes < rsvd_nodes) {
		res = hfsplus_file_extend(inode, hfs_bnode_need_zeroout(tree));
		if (res)
			return res;
		hip->phys_size = inode->i_size =
			(loff_t)hip->alloc_blocks <<
				HFSPLUS_SB(tree->sb)->alloc_blksz_shift;
		hip->fs_blocks =
			hip->alloc_blocks << HFSPLUS_SB(tree->sb)->fs_shift;
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

	lockdep_assert_held(&tree->tree_lock);

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
						hfs_btree_write(tree);
						mark_inode_dirty(tree->inode);
						hfs_bnode_put(node);
						return hfs_bnode_create(tree,
							idx);
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
			hfs_dbg("create new bmap node\n");
			next_node = hfs_bmap_new_bmap(node, idx);
			hfs_btree_write(tree);
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
	BUG_ON(!node->this);
	tree = node->tree;
	lockdep_assert_held(&tree->tree_lock);
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
			pr_crit("unable to free bnode %u. "
					"bmap not found!\n",
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
			pr_crit("invalid bmap found! "
					"(%u,%d)\n",
				node->this, node->type);
			hfs_bnode_put(node);
			return;
		}
		len = hfs_brec_lenoff(node, 0, &off);
	}

	res = hfs_bmap_clear_bit(node, nidx);
	if (res == -EINVAL) {
		pr_crit("trying to free the freed bnode %u(%d)\n",
			nidx, node->type);
	} else if (res) {
		pr_crit("fail to free bnode %u(%d)\n",
			nidx, node->type);
	} else {
		tree->free_nodes++;
		hfs_btree_write(tree);
		mark_inode_dirty(tree->inode);
	}

	hfs_bnode_put(node);
}
